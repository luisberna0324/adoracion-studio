#!/usr/bin/env python3
"""Compose deterministic integration fixtures from the verified bundled-voice corpus.

This never runs recognition. References are frozen before ASR; only generated
copies are trimmed/mixed. Python 3.12+, standard library, no downloads.
"""
from __future__ import annotations

import argparse
from array import array
import csv
import hashlib
import io
import json
import math
from pathlib import Path
import random
import re
import sys
import wave

ROOT = Path(__file__).resolve().parents[1]
RATE = 16000
VERSION = "guide-scenarios-1"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def encoded(value: object) -> bytes:
    return (json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2) + "\n").encode()


def write_json(path: Path, value: object) -> None:
    if path.is_symlink():
        raise ValueError(f"Refusing a symlink output: {path}")
    path.write_bytes(encoded(value))


def pcm_values(data: bytes) -> array:
    values = array("h")
    values.frombytes(data)
    if sys.byteorder != "little":
        values.byteswap()
    return values


def active_bounds(samples: array) -> tuple[int, int]:
    """Same declared energy criterion, independently measured on source PCM."""
    active = []
    for begin in range(0, len(samples), 320):
        frame = samples[begin:begin + 320]
        rms = math.sqrt(sum(float(x) * x for x in frame) / len(frame)) / 32768
        peak = max(abs(x) for x in frame) / 32768
        if rms >= .0025 and peak >= .006:
            active.append((begin, begin + len(frame)))
    if not active:
        raise ValueError("A selected source recording has no measured speech activity")
    return active[0][0], active[-1][1]


def next_bar(onset: float, origin: float, bar: float) -> float:
    phase = (onset - origin) / bar
    nearest = round(phase)
    if abs(onset - (origin + nearest * bar)) <= min(.020, bar * .25) + 1e-7:
        phase = float(nearest)
    return origin + (math.floor(phase + 1e-9) + 1) * bar


def make_wav(samples: list[float]) -> tuple[bytes, float]:
    peak = max((abs(x) for x in samples), default=0)
    scale = min(1.0, .95 / peak) if peak else 1.0
    values = array("h", (max(-32768, min(32767, round(x * scale * 32767))) for x in samples))
    if sys.byteorder != "little":
        values.byteswap()
    buffer = io.BytesIO()
    with wave.open(buffer, "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(RATE)
        output.writeframes(values.tobytes())
    return buffer.getvalue(), scale


def prepare(spec_path: Path, corpus_path: Path) -> tuple[dict, dict[str, bytes]]:
    spec_bytes = spec_path.read_bytes()
    spec = json.loads(spec_bytes)
    corpus = json.loads(corpus_path.read_text())
    if spec.get("schemaVersion") != 1 or not 1 <= len(spec.get("cases", [])) <= 100:
        raise ValueError("Expected a version-one specification with 1..100 cases")
    master_path = Path(corpus["outputPath"])
    master_bytes = master_path.read_bytes()
    if digest(master_bytes) != corpus["masterSha256"]:
        raise ValueError("The reference corpus WAV changed; rebuild/verify it first")
    archive = ROOT / "assets/guides/guides-en-es.zip"
    if digest(archive.read_bytes()) != corpus["archiveSha256"]:
        raise ValueError("The bundled source archive differs from the corpus")
    if spec.get("sourceArchiveSha256", corpus["archiveSha256"]) != corpus["archiveSha256"]:
        raise ValueError("The specification pins a different source archive")
    with wave.open(io.BytesIO(master_bytes)) as source:
        if (source.getnchannels(), source.getsampwidth(), source.getframerate()) != (1, 2, RATE):
            raise ValueError("The source corpus must be mono PCM16/16 kHz")
        master_pcm = source.readframes(source.getnframes())
    recordings = {}
    for clip in corpus["clips"]:
        raw = master_pcm[clip["startFrame"] * 2:clip["endFrame"] * 2]
        if digest(raw) != clip["decodedPcmSha256"]:
            raise ValueError(f"Source PCM changed: {clip['id']}")
        recordings[clip["id"]] = (clip, pcm_values(raw))

    def select(event: dict) -> tuple[dict, array]:
        kind = {"section": "primary", "instruction": "utility", "count": "count"}[event["kind"]]
        candidates = [(c, pcm) for c, pcm in recordings.values()
                      if c["kind"] == kind and c["language"] == event["language"]
                      and (c["expectedKey"] if kind == "count" else c["canonicalFamily"]) == event["key"]
                      and c.get("spokenOrdinal") == event.get("ordinal")]
        if len(candidates) != 1:
            raise ValueError(f"Need exactly one available recording for {event!r}; got {len(candidates)}")
        return candidates[0]

    suite_id = digest(VERSION.encode() + spec_bytes + corpus["masterSha256"].encode())
    result = {"schemaVersion": 1, "producer": VERSION, "suiteId": suite_id,
              "title": spec.get("title", "PlayWorship · pruebas de guía"),
              "specSha256": digest(spec_bytes), "sourceArchiveSha256": corpus["archiveSha256"],
              "sourceCorpusSha256": corpus["masterSha256"],
              "scope": "Controlled regression with bundled voices, not unseen-voice accuracy. References never enter ASR.",
              "format": {"sampleRate": RATE, "channels": 1, "sampleWidthBytes": 2}, "cases": []}
    payloads = {}
    ids = set()
    for case in spec["cases"]:
        case_id = case["id"]
        if not re.fullmatch(r"[a-z0-9][a-z0-9-]{0,79}", case_id) or case_id in ids:
            raise ValueError(f"Invalid or duplicate case ID: {case_id}")
        ids.add(case_id)
        bpm = float(case["bpm"])
        numerator, denominator = map(int, case["meter"].split("/"))
        origin = float(case["originSeconds"])
        if not 20 <= bpm <= 300 or not 1 <= numerator <= 64 or not 1 <= denominator <= 64 or origin < 0:
            raise ValueError(f"Invalid musical grid: {case_id}")
        pulse = 60 / bpm * 4 / denominator
        bar = pulse * numerator
        length = round((origin + int(case["durationBars"]) * bar) * RATE)
        if not 0 < length <= 120 * RATE:
            raise ValueError(f"Scenario must last at most 120 seconds: {case_id}")
        audio = [0.0] * length
        expected, sources = [], []
        previous_active_end = None
        event_ids = set()
        for event in case.get("events", []):
            if event["id"] in event_ids:
                raise ValueError(f"Duplicate event ID in {case_id}")
            event_ids.add(event["id"])
            clip, pcm = select(event)
            first, last = active_bounds(pcm)
            begin, end = max(0, first - 320), min(len(pcm), last + 320)
            if "onsetSeconds" in event:
                onset = float(event["onsetSeconds"])
            elif "gapSeconds" in event:
                if previous_active_end is None:
                    raise ValueError("A gap needs a previous event")
                onset = previous_active_end + float(event["gapSeconds"])
            else:
                onset = origin + float(event.get("bar", 0)) * bar + float(event.get("floatOffsetSeconds", .12))
            onset_frame = round(onset * RATE)
            copy_begin = onset_frame - (first - begin)
            if copy_begin < 0 or copy_begin + end - begin > length:
                raise ValueError(f"Source would be clipped by scenario bounds: {case_id}/{event['id']}")
            for index, value in enumerate(pcm[begin:end]):
                audio[copy_begin + index] += value / 32768
            onset = onset_frame / RATE
            active_end = (onset_frame + last - first) / RATE
            source = {"id": event["id"], "kind": event["kind"], "key": event["key"],
                      "language": event["language"], "sourceClipId": clip["id"],
                      "sourceZipPath": clip["sourceZipPath"], "sourcePcmSha256": clip["decodedPcmSha256"],
                      "sourceTrimBeginFrame": begin, "sourceTrimEndFrame": end,
                      "copyBeginFrame": copy_begin, "activeOnsetFrame": onset_frame,
                      "announcementSeconds": onset, "activeEndSeconds": active_end,
                      "actualGapSeconds": None if previous_active_end is None else onset - previous_active_end}
            if "ordinal" in event:
                source["ordinal"] = event["ordinal"]
            sources.append(source)
            previous_active_end = active_end
            target = next_bar(onset, origin, bar) if event["kind"] == "section" else onset
            if event.get("expectReview"):
                if event["kind"] != "section" or target + .25 <= length / RATE:
                    raise ValueError("A requires-review fixture must lack a valid following-bar destination")
                source["expectReview"] = True
                continue
            reference = {"id": event["id"], "kind": event["kind"], "key": event["key"],
                         "announcementSeconds": onset, "expectedTimeSeconds": target,
                         "toleranceSeconds": float(event.get("toleranceSeconds", .04))}
            for key in ("ordinal", "requireUnnumbered"):
                if key in event:
                    reference[key] = event[key]
            expected.append(reference)
        effects = dict(case.get("effects", {}))
        gain = 10 ** (float(effects.get("gainDb", 0)) / 20)
        if not 0 < gain <= 4:
            raise ValueError("Fixture gain is outside the supported bound")
        seed = int(digest((suite_id + case_id).encode())[:16], 16)
        rng = random.Random(seed)
        noise = 10 ** (float(effects["noiseDbfs"]) / 20) if "noiseDbfs" in effects else 0
        if noise > .1:
            raise ValueError("Fixture noise is outside the supported bound")
        for index in range(length):
            audio[index] = audio[index] * gain + (rng.gauss(0, noise) if noise else 0)
        if case.get("background") == "click":
            step = 0
            while (time := origin + step * pulse) < length / RATE:
                start = round(time * RATE)
                frequency = 1760 if step % numerator == 0 else 1320
                for index in range(min(192, length - start)):
                    audio[start + index] += .25 * math.sin(2 * math.pi * frequency * index / RATE) * math.exp(-index / 42)
                step += 1
        wav, peak_scale = make_wav(audio)
        payloads[f"audio/{case_id}.wav"] = wav
        case_manifest = {key: case[key] for key in ("id", "title", "group", "bpm", "meter", "originSeconds", "tags", "description")}
        case_manifest.update({"audioPath": f"audio/{case_id}.wav", "durationSeconds": length / RATE,
                              "frameCount": length, "audioSha256": digest(wav), "expectedEvents": expected,
                              "sourceEvents": sources, "effects": {**effects, "noiseSeed": seed, "peakScale": peak_scale}})
        if "expectedPolicy" in case:
            case_manifest["expectedPolicy"] = case["expectedPolicy"]
        result["cases"].append(case_manifest)
    return result, payloads


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--spec", type=Path, default=ROOT / "tests/fixtures/guide-scenarios/suite.json")
    parser.add_argument("--corpus-manifest", type=Path, default=ROOT / "output/guide-recognition-corpus-20261104/manifest.json")
    parser.add_argument("--output-dir", required=True, type=Path)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--dry-run", action="store_true")
    mode.add_argument("--execute", action="store_true")
    args = parser.parse_args()
    try:
        manifest, payloads = prepare(args.spec.resolve(), args.corpus_manifest.resolve())
        directory = args.output_dir.resolve()
        directory.mkdir(parents=True, exist_ok=True)
        plan_file = directory / "plan.json"
        plan = {"producer": VERSION, "suiteId": manifest["suiteId"],
                "manifestSha256": digest(encoded(manifest)),
                "files": {key: digest(value) for key, value in payloads.items()}}
        if args.dry_run:
            if (directory / "suite-manifest.json").exists():
                raise ValueError("An executed suite already exists; choose a new output directory")
            write_json(plan_file, plan)
            print(f"Planned {len(payloads)} WAV files; no audio written. {plan_file}")
            return 0
        if not plan_file.exists() or json.loads(plan_file.read_text()) != plan:
            raise ValueError("Plan missing or changed; run --dry-run and inspect the new plan first")
        if (directory / "suite-manifest.json").exists() or any((directory / key).exists() for key in payloads):
            raise ValueError("Refusing to replace an existing suite or audio file")
        checks = []
        for name, data in payloads.items():
            target = directory / name
            target.parent.mkdir(exist_ok=True)
            if target.is_symlink():
                raise ValueError(f"Refusing a symlink output: {target}")
            target.write_bytes(data)
            with wave.open(str(target)) as decoded:
                valid = (decoded.getnchannels(), decoded.getsampwidth(), decoded.getframerate()) == (1, 2, RATE)
                frames = decoded.getnframes()
            if not valid or digest(target.read_bytes()) != plan["files"][name]:
                raise ValueError(f"Generated audio verification failed: {target}")
            checks.append({"file": name, "frames": frames, "sha256": plan["files"][name], "valid": True})
        corpus = json.loads(args.corpus_manifest.read_text())
        archive_after = digest((ROOT / "assets/guides/guides-en-es.zip").read_bytes())
        corpus_after = digest(Path(corpus["outputPath"]).read_bytes())
        if archive_after != manifest["sourceArchiveSha256"] or corpus_after != manifest["sourceCorpusSha256"]:
            raise ValueError("An original source changed during generation; outputs are not certified")
        write_json(directory / "suite-manifest.json", manifest)
        write_json(directory / "assembly-verification.json", {"suiteId": manifest["suiteId"], "cases": checks,
                   "originalSourcesChanged": False, "sourceArchiveSha256After": archive_after,
                   "sourceCorpusSha256After": corpus_after, "referenceNotice": manifest["scope"]})
        with (directory / "references.csv").open("w", newline="", encoding="utf-8") as output:
            writer = csv.writer(output)
            writer.writerow(["caseId", "eventId", "kind", "key", "ordinal", "announcementSeconds", "expectedTimeSeconds", "toleranceSeconds"])
            for case in manifest["cases"]:
                for event in case["expectedEvents"]:
                    writer.writerow([case["id"], event["id"], event["kind"], event["key"], event.get("ordinal", ""),
                                     event["announcementSeconds"], event["expectedTimeSeconds"], event["toleranceSeconds"]])
        print(f"Built and verified {len(checks)} audio scenarios: {directory / 'suite-manifest.json'}")
        return 0
    except (ValueError, OSError, KeyError, wave.Error) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
