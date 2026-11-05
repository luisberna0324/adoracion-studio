#!/usr/bin/env python3
"""Build one reference WAV from every distinct MP3 in the bundled guide pack.

Python 3.10+, standard library, ffmpeg and ffprobe only. First run --dry-run;
then --execute verifies that plan and decodes the unchanged source bytes.
Labels are reference metadata only and must never be supplied to recognition.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import sys
import unicodedata
import wave
from zipfile import ZipFile


PRODUCER = "build_guide_recognition_corpus.py"
RATE, CHANNELS, SAMPLE_BYTES = 16000, 1, 2
LEAD_FRAMES, GAP_FRAMES, TAIL_FRAMES = RATE, 2 * RATE, RATE
REPOSITORY = Path(__file__).resolve().parents[1]


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def words(value: str) -> str:
    value = "".join(c for c in unicodedata.normalize("NFD", value.casefold())
                    if not unicodedata.combining(c))
    return " ".join(re.sub(r"[^\w]+", " ", value, flags=re.UNICODE).replace("_", " ").split())


def run(command: list[str]) -> bytes:
    result = subprocess.run(command, stdin=subprocess.DEVNULL, capture_output=True, timeout=90)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): {command!r}\n"
                           + result.stderr.decode("utf-8", errors="replace"))
    return result.stdout


def probe(path: Path, ffprobe: str) -> dict:
    result = json.loads(run([ffprobe, "-v", "error", "-show_format", "-show_streams",
                             "-of", "json", str(path)]))
    streams = [stream for stream in result.get("streams", []) if stream.get("codec_type") == "audio"]
    duration = float(result.get("format", {}).get("duration", 0))
    if len(streams) != 1 or not math.isfinite(duration) or duration <= 0:
        raise ValueError(f"Input must contain one usable finite audio stream: {path}")
    return result


def json_write(path: Path, value: dict) -> None:
    if path.is_symlink():
        raise ValueError(f"Refusing a symlink output: {path}")
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def vocabulary(path: Path) -> tuple[dict, dict, list[str], list[str]]:
    """Read the shared static label tables; no app or recognition engine runs."""
    source = path.read_text(encoding="utf-8")
    sections, utilities, section_keys, utility_keys = {}, {}, [], []
    for line in source.splitlines():
        if "{SectionFamily::" in line:
            strings = re.findall(r'"([^"\\]*)"', line)
            if len(strings) < 6:
                raise ValueError("Unsupported shared section vocabulary row")
            key = strings[0]
            section_keys.append(key)
            for alias in [key, *strings[5:]]:
                sections[words(alias)] = key
        elif re.match(r'\s*\{"', line) and re.search(r'}, (?:true|false)},?\s*$', line):
            strings = re.findall(r'"([^"\\]*)"', line)
            key = strings[0]
            utility_keys.append(key)
            for alias in [key, *strings[3:]]:
                utilities[words(alias)] = key
    if not section_keys or not utility_keys:
        raise ValueError("Shared vocabulary could not be read")
    return sections, utilities, section_keys, utility_keys


def classify(stem: str, sections: dict, utilities: dict) -> dict:
    # Native filenames expose aliases both outside and inside parentheses.
    candidates = re.findall(r"\(([^)]+)\)", stem) + [stem.split("(", 1)[0].strip(), stem]
    for candidate in candidates:
        key = words(candidate)
        if re.fullmatch(r"[1-7]", key):
            return {"kind": "count", "expectedKey": key, "canonicalFamily": "Count"}
        if key in utilities:
            return {"kind": "utility", "expectedKey": utilities[key], "canonicalFamily": utilities[key]}
        match = re.fullmatch(r"(.+?) (\d+)", key)
        base, ordinal = (match[1], int(match[2])) if match else (key, None)
        if base in sections:
            family = sections[base]
            result = {"kind": "primary", "canonicalFamily": family,
                      "expectedKey": family + (f" {ordinal}" if ordinal else "")}
            if ordinal:
                result["spokenOrdinal"] = ordinal
            return result
    return {"kind": "unknown", "expectedKey": stem, "canonicalFamily": ""}


def safe_source_name(info) -> str:
    name = PurePosixPath(info.filename)
    if (name.is_absolute() or ".." in name.parts or "\\" in info.filename
            or len(name.parts) != 3 or name.parts[0] not in ("en", "es")
            or name.parts[1] not in ("sections", "cues")
            or stat.S_ISLNK(info.external_attr >> 16) or info.file_size > 16 * 1024 * 1024):
        raise ValueError(f"Unsafe or oversized archive entry: {info.filename!r}")
    return name.parts[0]


def inventory(archive: Path, output: Path, vocabulary_path: Path, ffprobe: str) -> dict:
    sections, utilities, section_keys, utility_keys = vocabulary(vocabulary_path)
    sources = output / "sources"
    if sources.is_symlink():
        raise ValueError(f"Refusing a symlink extraction directory: {sources}")
    sources.mkdir(parents=True, exist_ok=True)
    by_hash, all_entries = {}, []
    total_bytes = 0
    with ZipFile(archive) as zipped:
        for info in zipped.infolist():
            if not info.filename.lower().endswith(".mp3"):
                continue
            language = safe_source_name(info)
            total_bytes += info.file_size
            if total_bytes > 256 * 1024 * 1024:
                raise ValueError("Guide archive exceeds the 256 MiB extraction bound")
            data = zipped.read(info)
            digest = sha256(data)
            label = PurePosixPath(info.filename).stem
            entry = {"sourceZipPath": info.filename, "language": language,
                     "expectedLabel": label, **classify(label, sections, utilities)}
            all_entries.append({**entry, "sourceSha256": digest})
            if digest not in by_hash:
                path = sources / (digest + ".mp3")
                if path.is_symlink() or (path.exists() and sha256(path.read_bytes()) != digest):
                    raise ValueError(f"Existing extracted source is not the expected input: {path}")
                if not path.exists():
                    path.write_bytes(data)
                by_hash[digest] = {"sourceSha256": digest, "sourcePath": str(path), "references": []}
            by_hash[digest]["references"].append(entry)
    clips = []
    for source in by_hash.values():
        references = sorted(source["references"], key=lambda entry: (
            entry["kind"] == "unknown", "(" in entry["expectedLabel"], entry["sourceZipPath"].casefold()))
        representative = references[0]
        original_probe = probe(Path(source["sourcePath"]), ffprobe)
        clips.append({**source, **representative, "sourceReferences": references,
                      "sourceZipAliases": sorted(entry["sourceZipPath"] for entry in references),
                      "languages": sorted({entry["language"] for entry in references}),
                      "expectedKeys": sorted({entry["expectedKey"] for entry in references}),
                      "sourceProbe": original_probe,
                      "probeDurationSeconds": float(original_probe["format"]["duration"])})
        del clips[-1]["references"]
    clips.sort(key=lambda item: (item["canonicalFamily"].casefold(), item.get("spokenOrdinal", 0),
                                 item["expectedKey"].casefold(), item["language"], item["sourceZipPath"].casefold(),
                                 item["sourceSha256"]))
    coverage = {}
    for entry in all_entries:
        if entry["kind"] != "unknown":
            coverage.setdefault((entry["kind"], entry["canonicalFamily"]), set()).add(entry["language"])
    missing = []
    for kind, keys in (("primary", section_keys), ("utility", utility_keys), ("count", [str(i) for i in range(1, 8)])):
        for key in keys:
            available = coverage.get((kind, key if kind != "count" else "Count"), set())
            if kind == "count":
                available = {entry["language"] for entry in all_entries if entry["kind"] == "count" and entry["expectedKey"] == key}
            for language in ("en", "es"):
                if language not in available:
                    missing.append({"kind": kind, "key": key, "language": language})
    cursor = LEAD_FRAMES / RATE
    for i, clip in enumerate(clips):
        clip["id"] = f"clip-{i + 1:03d}-{clip['sourceSha256'][:12]}"
        clip["startSeconds"] = cursor
        clip["endSeconds"] = cursor + clip["probeDurationSeconds"]
        cursor = clip["endSeconds"] + GAP_FRAMES / RATE
    return {"schemaVersion": 1, "producer": PRODUCER, "status": "planned",
            "archivePath": str(archive), "archiveSha256": sha256(archive.read_bytes()),
            "vocabularyPath": str(vocabulary_path), "vocabularySha256": sha256(vocabulary_path.read_bytes()),
            "sourceZipCount": len(all_entries), "uniqueSourceCount": len(clips),
            "duplicatesOmitted": len(all_entries) - len(clips),
            "languageEntryCounts": {language: sum(e["language"] == language for e in all_entries) for language in ("en", "es")},
            "clips": clips, "missingLanguageClips": missing,
            "outputPath": str(output / "all-guide-clips.wav"),
            "audio": {"sampleRate": RATE, "channels": CHANNELS, "sampleWidthBytes": SAMPLE_BYTES,
                      "leadSeconds": 1, "betweenClipSeconds": 2, "tailSeconds": 1,
                      "effects": [], "normalization": False},
            "estimatedDurationSeconds": sum(c["probeDurationSeconds"] for c in clips) + 2 * len(clips),
            "referenceNotice": "Filename labels are evaluation references, not human-verified transcripts. Never feed them or their order to ASR."}


def command_for(clip: dict, ffmpeg: str) -> list[str]:
    return [ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin", "-i", clip["sourcePath"],
            "-map", "0:a:0", "-vn", "-ac", str(CHANNELS), "-ar", str(RATE),
            "-c:a", "pcm_s16le", "-f", "s16le", "pipe:1"]


def srt_time(seconds: float) -> str:
    milliseconds = round(seconds * 1000)
    hours, milliseconds = divmod(milliseconds, 3600000)
    minutes, milliseconds = divmod(milliseconds, 60000)
    whole, milliseconds = divmod(milliseconds, 1000)
    return f"{hours:02}:{minutes:02}:{whole:02},{milliseconds:03}"


def execute(plan: dict, output: Path, ffmpeg: str, ffprobe: str, overwrite: bool) -> dict:
    destination = Path(plan["outputPath"])
    if destination.is_symlink() or destination.exists() and not overwrite:
        raise ValueError("Output already exists; use --overwrite only for a corpus generated by this job")
    temporary = output / "all-guide-clips.partial.wav"
    if temporary.exists() or temporary.is_symlink():
        raise ValueError(f"Partial output already exists: {temporary}")
    total_frames = 0
    commands = []
    with wave.open(str(temporary), "wb") as master:
        master.setnchannels(CHANNELS); master.setsampwidth(SAMPLE_BYTES); master.setframerate(RATE)
        master.writeframesraw(bytes(LEAD_FRAMES * SAMPLE_BYTES)); total_frames += LEAD_FRAMES
        for i, clip in enumerate(plan["clips"]):
            source = Path(clip["sourcePath"])
            if sha256(source.read_bytes()) != clip["sourceSha256"]:
                raise ValueError("An extracted source changed after planning")
            command = command_for(clip, ffmpeg)
            pcm = run(command)
            if not pcm or len(pcm) % SAMPLE_BYTES:
                raise ValueError(f"Decoded PCM is empty or has incomplete frames: {clip['sourceZipPath']}")
            frames = len(pcm) // SAMPLE_BYTES
            clip["startFrame"] = total_frames; clip["startSeconds"] = total_frames / RATE
            clip["decodedFrameCount"] = frames; clip["decodedPcmSha256"] = sha256(pcm)
            master.writeframesraw(pcm); total_frames += frames
            clip["endFrame"] = total_frames; clip["endSeconds"] = total_frames / RATE
            clip["decodedDurationSeconds"] = frames / RATE
            commands.append(command)
            if i + 1 < len(plan["clips"]):
                master.writeframesraw(bytes(GAP_FRAMES * SAMPLE_BYTES)); total_frames += GAP_FRAMES
        master.writeframesraw(bytes(TAIL_FRAMES * SAMPLE_BYTES)); total_frames += TAIL_FRAMES
    with wave.open(str(temporary), "rb") as check:
        if (check.getnchannels(), check.getsampwidth(), check.getframerate(), check.getnframes()) != (CHANNELS, SAMPLE_BYTES, RATE, total_frames):
            raise ValueError("Output PCM header does not match the requested corpus")
    actual_probe = probe(temporary, ffprobe)
    stream = next(s for s in actual_probe["streams"] if s.get("codec_type") == "audio")
    if stream.get("codec_name") != "pcm_s16le" or int(stream["sample_rate"]) != RATE or stream["channels"] != CHANNELS:
        raise ValueError("Output probe does not match mono 16 kHz PCM")
    if abs(float(actual_probe["format"]["duration"]) - total_frames / RATE) > 1 / RATE:
        raise ValueError("Output probe duration differs from the exact assembled frames")
    if sha256(Path(plan["archivePath"]).read_bytes()) != plan["archiveSha256"]:
        raise ValueError("The original ZIP changed during corpus creation")
    temporary.replace(destination)
    actual_probe["format"]["filename"] = str(destination)
    plan.update(status="complete", masterSha256=sha256(destination.read_bytes()),
                frameCount=total_frames, durationSeconds=total_frames / RATE,
                outputProbe=actual_probe, decodeCommands=commands)
    info = destination.stat()
    plan["outputIdentity"] = {"size": info.st_size, "mtime_ns": info.st_mtime_ns}
    json_write(output / "manifest.json", plan)
    fields = ["id", "language", "kind", "expectedLabel", "expectedKey", "canonicalFamily", "startSeconds", "endSeconds",
              "startFrame", "endFrame", "sourceZipPath", "sourceSha256", "probeDurationSeconds", "decodedDurationSeconds"]
    with (output / "clips.csv").open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, extrasaction="ignore")
        writer.writeheader(); writer.writerows(plan["clips"])
    chapters = []
    for index, clip in enumerate(plan["clips"], 1):
        chapters.append(f"{index}\n{srt_time(clip['startSeconds'])} --> {srt_time(clip['endSeconds'])}\n"
                        f"{clip['language']} | {clip['kind']} | {clip['expectedLabel']}\n")
    (output / "reference-chapters.srt").write_text("\n".join(chapters), encoding="utf-8")
    return plan


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, default=REPOSITORY / "assets/guides/guides-en-es.zip")
    parser.add_argument("--vocabulary", type=Path, default=REPOSITORY / "src/data/GuideVocabulary.h")
    parser.add_argument("--output-dir", type=Path, required=True)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--dry-run", action="store_true")
    mode.add_argument("--execute", action="store_true")
    parser.add_argument("--overwrite", action="store_true")
    parser.add_argument("--ffmpeg", default=shutil.which("ffmpeg"))
    parser.add_argument("--ffprobe", default=shutil.which("ffprobe"))
    args = parser.parse_args()
    if not args.ffmpeg or not args.ffprobe:
        parser.error("ffmpeg and ffprobe must already be installed")
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    plan = inventory(args.archive.resolve(), output, args.vocabulary.resolve(), args.ffprobe)
    plan["decodeCommands"] = [command_for(clip, args.ffmpeg) for clip in plan["clips"]]
    plan["tools"] = {name: run([path, "-version"]).decode("utf-8").splitlines()[0]
                     for name, path in (("ffmpeg", args.ffmpeg), ("ffprobe", args.ffprobe))}
    plan_path = output / "plan.json"
    if args.execute:
        if not plan_path.is_file() or json.loads(plan_path.read_text(encoding="utf-8")) != plan:
            raise ValueError("Run --dry-run first; the archive, vocabulary and plan must remain unchanged")
        plan["planSha256"] = sha256(plan_path.read_bytes())
        plan = execute(plan, output, args.ffmpeg, args.ffprobe, args.overwrite)
    else:
        json_write(plan_path, plan)
    print(json.dumps({key: plan[key] for key in ("status", "sourceZipCount", "uniqueSourceCount", "duplicatesOmitted",
          "estimatedDurationSeconds", "outputPath", "missingLanguageClips")}, ensure_ascii=False))
    if args.execute:
        print(json.dumps({key: plan[key] for key in ("durationSeconds", "frameCount", "masterSha256", "outputIdentity")}))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"Corpus build failed: {error}", file=sys.stderr)
        raise SystemExit(1)
