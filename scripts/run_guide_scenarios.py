#!/usr/bin/env python3
"""Run generated guide audio through the real local probe, then compare outcomes.

Python 3.12+, standard library only. Reference labels are consumed only by this
evaluator: the transcription process receives audio, model and musical context.
This script does not download models, alter the library or apply corrections.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unicodedata
import uuid


REPOSITORY = Path(__file__).resolve().parents[1]
CASE_ID = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,95}\Z")
KINDS = {"section", "instruction", "count"}
MAX_JSON_BYTES = 16 * 1024 * 1024


def now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def number(value: object) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def normalized(value: str) -> str:
    text = "".join(c for c in unicodedata.normalize("NFD", value.casefold())
                   if not unicodedata.combining(c))
    return " ".join(re.sub(r"[^\w]+", " ", text).replace("_", " ").split())


def read_json(path: Path) -> dict:
    if not path.is_file() or path.stat().st_size > MAX_JSON_BYTES:
        raise ValueError(f"JSON ausente o demasiado grande: {path}")
    def invalid_constant(value: str):
        raise ValueError(f"Número JSON no finito: {value}")
    value = json.loads(path.read_text(encoding="utf-8"), parse_constant=invalid_constant)
    if not isinstance(value, dict):
        raise ValueError(f"Se requiere un objeto JSON: {path}")
    return value


def write_json(path: Path, value: dict) -> None:
    if path.is_symlink():
        raise ValueError(f"No se sustituye un enlace simbólico: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent,
                                     prefix=".scenario-", suffix=".tmp", delete=False) as output:
        temporary = Path(output.name)
        try:
            json.dump(value, output, ensure_ascii=False, indent=2, allow_nan=False)
            output.write("\n")
        except BaseException:
            temporary.unlink(missing_ok=True)
            raise
    try:
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def file_sha256(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def musical_context(case: dict) -> dict:
    return {key: case[key] for key in ("bpm", "meter", "originSeconds", "durationSeconds")}


def context_error(case: dict, raw: dict) -> str:
    """A successful probe must prove it aligned this case's actual context.

    Particularly important for silence: an unrelated empty result otherwise
    has no events through which a wrong grid or duration could be detected.
    """
    if raw.get("success") is not True:
        return ""  # The provider failure is reported by evaluate_case.
    fields = (("alignmentBpm", "bpm", 1e-7),
              ("alignmentOriginSeconds", "originSeconds", 1e-7),
              ("durationSeconds", "durationSeconds", 1.0 / 16000 + 1e-9))
    for actual_key, expected_key, tolerance in fields:
        if not number(raw.get(actual_key)) or abs(raw[actual_key] - case[expected_key]) > tolerance:
            return f"El JSON no corresponde al contexto de este caso: {actual_key}."
    if raw.get("alignmentMeter") != case["meter"]:
        return "El JSON no corresponde al contexto de este caso: alignmentMeter."
    if not isinstance(raw.get("alignmentError"), str):
        return "El JSON no conserva el estado explícito de alineación de este caso."
    return ""


def check_provenance(path: Path, manifest: dict, case: dict, manifest_hash: str,
                     audio_hash: str | None, raw_hash: str | None) -> tuple[str, str]:
    if not path.exists():
        return "legacy-context-verified", ""
    try:
        record = read_json(path)
    except (OSError, ValueError) as error:
        return "invalid", f"No se pudo verificar la procedencia: {error}"
    expected = {"schemaVersion": 1, "suiteId": manifest["suiteId"], "caseId": case["id"],
                "manifestSha256": manifest_hash, "inputSha256": audio_hash,
                "rawTranscriptSha256": raw_hash, "grid": musical_context(case)}
    for key, value in expected.items():
        if record.get(key) != value:
            return "invalid", f"La procedencia del JSON no coincide con este caso: {key}."
    if record.get("executionError"):
        return "verified-execution-failure", str(record["executionError"])
    return "input-and-transcript-verified", ""


def section_types() -> dict[int, str]:
    """Read enum identities from the actual shared project catalogue."""
    source = (REPOSITORY / "src/data/GuideVocabulary.h").read_text(encoding="utf-8")
    match = re.search(r"enum class SectionFamily\s*\{([^}]+)\}", source)
    if not match:
        raise ValueError("No se encontró el enum SectionFamily del proyecto.")
    members = [part.strip() for part in match[1].split(",")]
    if any(not re.fullmatch(r"[A-Za-z][A-Za-z0-9]*", member) for member in members):
        raise ValueError("El enum SectionFamily cambió; revisar el evaluador antes de usarlo.")
    keys = dict(re.findall(r'\{SectionFamily::(\w+),\s*"([^"\\]+)"', source))
    return {index: keys[member] for index, member in enumerate(members) if member in keys}


def load_manifest(path: Path) -> dict:
    value = read_json(path)
    if value.get("schemaVersion") != 1 or not isinstance(value.get("suiteId"), str) or not value["suiteId"]:
        raise ValueError("Manifiesto: schemaVersion=1 y suiteId son obligatorios.")
    cases = value.get("cases")
    if not isinstance(cases, list) or not cases or len(cases) > 256:
        raise ValueError("Manifiesto: cases debe contener de 1 a 256 casos.")
    seen = set()
    for case in cases:
        if not isinstance(case, dict) or not CASE_ID.fullmatch(str(case.get("id", ""))) or case["id"] in seen:
            raise ValueError("Cada caso necesita un ID único y seguro para archivos.")
        seen.add(case["id"])
        for key in ("title", "group", "audioPath", "meter"):
            if not isinstance(case.get(key), str) or not case[key]:
                raise ValueError(f"{case['id']}: falta {key}.")
        audio = Path(case["audioPath"])
        if audio.is_absolute() or not (path.parent / audio).resolve().is_relative_to(path.parent.resolve()):
            raise ValueError(f"{case['id']}: audioPath debe estar dentro de la carpeta del manifiesto.")
        if "audioSha256" in case and not re.fullmatch(r"[0-9a-f]{64}", str(case["audioSha256"])):
            raise ValueError(f"{case['id']}: audioSha256 debe ser una huella SHA-256 válida.")
        for key in ("bpm", "originSeconds", "durationSeconds"):
            if not number(case.get(key)):
                raise ValueError(f"{case['id']}: {key} debe ser finito.")
        if not 20 <= case["bpm"] <= 300 or not 0 <= case["originSeconds"] < case["durationSeconds"] <= 43200:
            raise ValueError(f"{case['id']}: contexto musical fuera de los límites del proveedor.")
        meter = re.fullmatch(r"(\d{1,2})/(\d{1,2})", case["meter"])
        if not meter or any(not 1 <= int(part) <= 64 for part in meter.groups()):
            raise ValueError(f"{case['id']}: métrica inválida.")
        events = case.get("expectedEvents")
        if not isinstance(events, list) or len(events) > 768:
            raise ValueError(f"{case['id']}: expectedEvents debe ser una lista acotada.")
        event_ids = set()
        for event in events:
            if not isinstance(event, dict) or not isinstance(event.get("id"), str) or not event["id"] or event["id"] in event_ids:
                raise ValueError(f"{case['id']}: cada referencia necesita un ID único.")
            event_ids.add(event["id"])
            if event.get("kind") not in KINDS or not isinstance(event.get("key"), str) or not event["key"]:
                raise ValueError(f"{case['id']}/{event['id']}: tipo o clave inválidos.")
            for key in ("announcementSeconds", "expectedTimeSeconds"):
                if not number(event.get(key)) or not 0 <= event[key] < case["durationSeconds"]:
                    raise ValueError(f"{case['id']}/{event['id']}: {key} fuera del audio.")
            if not number(event.get("toleranceSeconds", .04)) or not 0 <= event.get("toleranceSeconds", .04) <= 1:
                raise ValueError(f"{case['id']}/{event['id']}: tolerancia inválida.")
            if "ordinal" in event and (isinstance(event["ordinal"], bool) or not isinstance(event["ordinal"], int) or not 1 <= event["ordinal"] <= 999):
                raise ValueError(f"{case['id']}/{event['id']}: ordinal inválido.")
            if "requireUnnumbered" in event and not isinstance(event["requireUnnumbered"], bool):
                raise ValueError(f"{case['id']}/{event['id']}: requireUnnumbered debe ser booleano.")
        policy = case.get("expectedPolicy", {})
        if not isinstance(policy, dict) or set(policy) - {"alignmentError"} or not isinstance(policy.get("alignmentError", False), bool):
            raise ValueError(f"{case['id']}: política de revisión inválida.")
    return value


def actual_events(raw: dict, types: dict[int, str]) -> list[dict]:
    result = []
    for field, is_section in (("suggestions", True), ("secondaryCues", False)):
        items = raw.get(field)
        if not isinstance(items, list) or len(items) > 768:
            raise ValueError(f"El probe no entregó una lista válida de {field}.")
        for index, item in enumerate(items):
            if not isinstance(item, dict):
                result.append({"kind": "invalid", "key": "Objeto inválido", "timeSeconds": None,
                               "announcementSeconds": None, "source": field, "sourceIndex": index})
                continue
            name = str(item.get("name", ""))
            timing = item.get("startSeconds" if is_section else "timeSeconds")
            announcement = item.get("announcementSeconds")
            if is_section:
                type_id = item.get("type")
                known_type = isinstance(type_id, int) and not isinstance(type_id, bool) and type_id in types
                kind, key = ("section", types[type_id]) if known_type else ("unknown-section", name)
                if known_type and (item.get("tentative") is True or name.casefold().startswith(("posible ", "possible "))):
                    kind = "tentative-section"
            else:
                kind = item.get("kind")
                kind = kind if kind in {"instruction", "count"} else "invalid"
                key = name
            event = {"kind": kind, "key": key, "name": name,
                     "timeSeconds": timing if number(timing) else None,
                     "announcementSeconds": announcement if number(announcement) else None,
                     "source": field, "sourceIndex": index}
            ordinal = re.search(r"(?:^|\s)(\d{1,3})\s*$", name)
            if is_section and ordinal:
                event["ordinal"] = int(ordinal[1])
            duration = raw.get("durationSeconds")
            if (not name.strip() or event["timeSeconds"] is None or event["announcementSeconds"] is None
                    or event["timeSeconds"] < 0 or event["announcementSeconds"] < 0
                    or (number(duration) and (event["timeSeconds"] >= duration or event["announcementSeconds"] > duration))):
                event["kind"] = "invalid"
            result.append(event)
    return result


def compatible(expected: dict, actual: dict) -> bool:
    return (expected["kind"] == actual["kind"] and normalized(expected["key"]) == normalized(actual["key"])
            and ("ordinal" not in expected or expected["ordinal"] == actual.get("ordinal")))


def pair_events(expected: list[dict], actual: list[dict]) -> dict[int, int]:
    """Ordered one-to-one maximum matching; nearest announcements break ties.

    Missing early repeats cannot consume the sole correctly timed later repeat.
    Timing at the proposed boundary remains a separate pass/fail check.
    """
    matches = {}
    groups = {(event["kind"], normalized(event["key"])) for event in expected}
    for group in sorted(groups):
        reference = sorted((i for i, event in enumerate(expected)
                            if (event["kind"], normalized(event["key"])) == group),
                           key=lambda i: expected[i]["announcementSeconds"])
        observed = sorted((i for i, event in enumerate(actual)
                           if (event["kind"], normalized(event["key"])) == group
                           and number(event["announcementSeconds"])),
                          key=lambda i: actual[i]["announcementSeconds"])
        width = len(observed)
        previous = [(0, 0.0)] * (width + 1)
        trace = [bytearray(width + 1) for _ in range(len(reference) + 1)]
        for row, expected_index in enumerate(reference, 1):
            current = [(0, 0.0)] * (width + 1)
            trace[row][0] = 1  # skip reference
            e = expected[expected_index]
            for column, actual_index in enumerate(observed, 1):
                best, direction = previous[column], 1
                if current[column - 1] > best:
                    best, direction = current[column - 1], 2  # skip observed
                a = actual[actual_index]
                distance = abs(e["announcementSeconds"] - a["announcementSeconds"])
                # Token intervals can start earlier than the count's true pulse.
                # This gate associates nearby evidence only; it never relaxes
                # the manifest's independent boundary-position tolerance.
                if compatible(e, a) and distance <= .75:
                    count, score = previous[column - 1]
                    candidate = (count + 1, score - distance)
                    if candidate > best:
                        best, direction = candidate, 3
                current[column], trace[row][column] = best, direction
            previous = current
        row, column = len(reference), len(observed)
        while row and column:
            direction = trace[row][column]
            if direction == 3:
                matches[reference[row - 1]] = observed[column - 1]
                row -= 1; column -= 1
            elif direction == 1:
                row -= 1
            else:
                column -= 1
    return matches


def reference_json(event: dict) -> dict:
    value = {"kind": event["kind"], "key": event["key"], "timeSeconds": event["expectedTimeSeconds"],
             "announcementSeconds": event["announcementSeconds"], "toleranceSeconds": event.get("toleranceSeconds", .04)}
    for key in ("ordinal", "requireUnnumbered"):
        if key in event:
            value[key] = event[key]
    return value


def evaluate_case(case: dict, raw: dict | None, transcript_path: str, types: dict[int, str],
                  execution_error: str = "") -> dict:
    result = {"id": case["id"], "status": "failed", "checks": [], "policyChecks": [], "unexpected": [],
              "rawTranscriptPath": transcript_path, "rawSegments": raw.get("segments", []) if raw else []}
    failure = execution_error
    observed = []
    if raw is None:
        failure = failure or "No existe una transcripción real para evaluar."
    elif raw.get("success") is not True or raw.get("cancelled") is True:
        failure = failure or str(raw.get("error") or "La inferencia falló o fue cancelada.")
    else:
        try:
            observed = actual_events(raw, types)
        except ValueError as error:
            failure = failure or str(error)
    pairs = pair_events(case["expectedEvents"], observed) if not failure else {}
    for index, expected in enumerate(case["expectedEvents"]):
        actual = observed[pairs[index]] if index in pairs else None
        error_seconds = actual["timeSeconds"] - expected["expectedTimeSeconds"] if actual else None
        timing_ok = error_seconds is not None and abs(error_seconds) <= expected.get("toleranceSeconds", .04) + 1e-9
        name_ok = actual is not None and (not expected.get("requireUnnumbered") or not re.search(r"\d", actual["name"]))
        passed = bool(timing_ok and name_ok and not failure)
        reason = ("Coinciden tipo, nombre y posición." if passed else
                  failure if failure else "Falta un anuncio de ese tipo, clave y ordinal cerca del tiempo esperado." if actual is None else
                  "La etiqueta tiene un número que la referencia no admite." if not name_ok else
                  "La posición excede la tolerancia independiente de la referencia.")
        result["checks"].append({"eventId": expected["id"], "passed": passed,
                                 "expected": reference_json(expected), "actual": actual,
                                 "errorSeconds": error_seconds, "reason": reason})
    used = set(pairs.values())
    result["unexpected"] = [event for index, event in enumerate(observed) if index not in used]
    expected_error = case.get("expectedPolicy", {}).get("alignmentError", False)
    actual_error = bool(raw.get("alignmentError")) if raw else False
    range_error = bool(raw and normalized(str(raw.get("alignmentError", ""))).startswith(
        normalized("No hay un compás siguiente válido para") + " "))
    source_evidence = bool(raw and isinstance(raw.get("segments"), list) and any(
        isinstance(segment, dict) and isinstance(segment.get("text"), str) and segment["text"].strip()
        and number(segment.get("startSeconds")) and number(segment.get("endSeconds"))
        and 0 <= segment["startSeconds"] < segment["endSeconds"]
        for segment in raw["segments"]))
    policy_ok = (actual_error and range_error and source_evidence) if expected_error else not actual_error
    policy_reason = str(raw.get("alignmentError", "")) if raw else failure
    if expected_error and actual_error and not (range_error and source_evidence):
        policy_reason += " No acredita la revisión por falta de compás posterior con un anuncio transcrito."
    result["policyChecks"].append({"key": "alignmentError", "passed": not failure and bool(policy_ok),
                                   "expected": expected_error, "actual": actual_error,
                                   "recognisedRangeFailure": range_error, "sourceEvidence": source_evidence,
                                   "reason": policy_reason})
    if failure:
        result["error"] = failure
    elif all(check["passed"] for check in result["checks"]) and all(check["passed"] for check in result["policyChecks"]) and not result["unexpected"]:
        result["status"] = "passed"
    if raw:
        result.update({key: raw.get(key) for key in ("providerVersion", "modelFingerprint", "elapsedSeconds", "cacheHit")})
    return result


def summary(cases: list[dict], manifest: dict) -> dict:
    return {"cases": len(cases), "passed": sum(c["status"] == "passed" for c in cases),
            "failed": sum(c["status"] == "failed" for c in cases),
            "pending": sum(c["status"] == "pending" for c in cases),
            "expectedEvents": sum(len(c["expectedEvents"]) for c in manifest["cases"]),
            "matchedEvents": sum(check["passed"] for c in cases for check in c["checks"]),
            "unexpectedEvents": sum(len(c["unexpected"]) for c in cases)}


def run(args: argparse.Namespace) -> Path:
    manifest_file = args.manifest.resolve()
    manifest = load_manifest(manifest_file)
    manifest_hash = file_sha256(manifest_file)
    types = section_types()
    selected = set(args.case or [case["id"] for case in manifest["cases"]])
    unknown = selected - {case["id"] for case in manifest["cases"]}
    if unknown:
        raise ValueError("Casos inexistentes: " + ", ".join(sorted(unknown)))
    destination = args.output_dir.resolve()
    destination.mkdir(parents=True, exist_ok=True)
    if not args.evaluate_only:
        if args.probe is None or args.model is None:
            raise ValueError("La inferencia requiere --probe y --model locales existentes.")
        if not args.probe.is_file() or not os.access(args.probe, os.X_OK) or not args.model.is_file():
            raise ValueError("El probe ejecutable y el modelo deben existir; este runner no descarga componentes.")
        for case_id in selected:
            if (destination / "transcripts" / f"{case_id}.json").exists():
                raise ValueError(f"Ya existe una transcripción para {case_id}; elige otro output-dir o --evaluate-only.")
    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8]
    cache = destination / "cache" / run_id
    started = now()
    cases = []
    sandbox = Path("/usr/bin/sandbox-exec")
    sandboxed = sys.platform == "darwin" and sandbox.is_file()
    for index, case in enumerate(manifest["cases"], 1):
        raw_path = destination / "transcripts" / f"{case['id']}.json"
        relative = os.path.relpath(raw_path, destination)
        if case["id"] not in selected:
            cases.append({"id": case["id"], "status": "pending", "checks": [], "policyChecks": [],
                          "unexpected": [], "rawTranscriptPath": relative, "rawSegments": []})
            continue
        print(f"[{index}/{len(manifest['cases'])}] {case['id']}: " +
              ("evaluando JSON existente" if args.evaluate_only else "inferencia local"), flush=True)
        execution_error = ""
        command = []
        audio = (manifest_file.parent / case["audioPath"]).resolve()
        audio_hash = None
        if not audio.is_file():
            execution_error = f"Audio de prueba ausente: {audio}"
        else:
            audio_hash = file_sha256(audio)
            if case.get("audioSha256") and audio_hash != case["audioSha256"]:
                execution_error = "El audio cambió respecto de audioSha256; no se comparan referencias de otra señal."
        if not args.evaluate_only:
            if not execution_error:
                raw_path.parent.mkdir(parents=True, exist_ok=True)
                command = [str(args.probe.resolve()), "--audio", str(audio), "--output", str(raw_path),
                           "--model", str(args.model.resolve()), "--language", "es-en", "--bpm", str(case["bpm"]),
                           "--meter", case["meter"], "--grid-origin", str(case["originSeconds"]),
                           "--cache-directory", str(cache)]
                if sandboxed:
                    command = [str(sandbox), "-p", "(version 1)(allow default)(deny network*)", *command]
                log = destination / "logs" / f"{case['id']}.log"
                log.parent.mkdir(parents=True, exist_ok=True)
                try:
                    process = subprocess.run(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                             stderr=subprocess.STDOUT, timeout=args.timeout_seconds, check=False)
                    log.write_bytes(process.stdout)
                    if process.returncode:
                        execution_error = f"El probe terminó con código {process.returncode}; ver {os.path.relpath(log, destination)}."
                except subprocess.TimeoutExpired as error:
                    log.write_bytes((error.stdout or b"") + b"\nRunner: timeout.\n")
                    execution_error = f"Se agotó el límite de {args.timeout_seconds:g} s; no se considera una inferencia aprobada."
        raw = None
        if raw_path.is_file():
            try:
                raw = read_json(raw_path)
            except ValueError as error:
                execution_error = execution_error or str(error)
        elif args.evaluate_only and not execution_error:
            cases.append({"id": case["id"], "status": "pending", "checks": [], "policyChecks": [],
                          "unexpected": [], "rawTranscriptPath": relative, "rawSegments": [],
                          "error": "Aún no existe una transcripción real para este caso."})
            continue
        if not args.evaluate_only and raw and raw.get("cacheHit") is True:
            execution_error = execution_error or "La ejecución reutilizó caché; no se acredita como una inferencia nueva."
        raw_hash = file_sha256(raw_path) if raw_path.is_file() else None
        if raw:
            execution_error = execution_error or context_error(case, raw)
        provenance_path = destination / "provenance" / f"{case['id']}.json"
        if not args.evaluate_only and raw_hash is not None:
            write_json(provenance_path, {"schemaVersion": 1, "suiteId": manifest["suiteId"],
                "caseId": case["id"], "runId": run_id, "recordedAt": now(),
                "manifestSha256": manifest_hash, "inputSha256": audio_hash,
                "rawTranscriptSha256": raw_hash, "grid": musical_context(case),
                "audioPath": case["audioPath"], "rawTranscriptPath": relative,
                "command": command, "providerVersion": raw.get("providerVersion") if raw else None,
                "modelFingerprint": raw.get("modelFingerprint") if raw else None,
                "executionError": execution_error})
        provenance_status, provenance_error = check_provenance(
            provenance_path, manifest, case, manifest_hash, audio_hash, raw_hash)
        execution_error = execution_error or provenance_error
        evaluated = evaluate_case(case, raw, relative, types, execution_error)
        evaluated["inputSha256"] = audio_hash
        evaluated["rawTranscriptSha256"] = raw_hash
        evaluated["provenanceStatus"] = (
            "unavailable" if raw is None else "provider-failure" if raw.get("success") is not True
            else "invalid-context" if context_error(case, raw) else "invalid" if execution_error
            else provenance_status)
        cases.append(evaluated)
        print(f"  {evaluated['status']}: {sum(c['passed'] for c in evaluated['checks'])}/{len(case['expectedEvents'])} eventos; "
              f"{len(evaluated['unexpected'])} extra", flush=True)
    versions = sorted({str(c["providerVersion"]) for c in cases if c.get("providerVersion")})
    models = sorted({str(c["modelFingerprint"]) for c in cases if c.get("modelFingerprint")})
    evaluation = {"schemaVersion": 1, "suiteId": manifest["suiteId"], "summary": summary(cases, manifest), "cases": cases,
                  "run": {"id": run_id, "startedAt": started, "finishedAt": now(),
                          "providerVersion": versions[0] if len(versions) == 1 else "mixed" if versions else "unavailable",
                          "providerVersions": versions, "modelFingerprint": models[0] if len(models) == 1 else "mixed" if models else "unavailable",
                          "provenanceModes": sorted({c["provenanceStatus"] for c in cases if "provenanceStatus" in c}),
                          "cacheMode": "existing-transcripts-evaluation" if args.evaluate_only else "fresh-empty-run-cache",
                          "networkPolicy": "sandbox-exec deny network" if sandboxed and not args.evaluate_only else "no-inference" if args.evaluate_only else "local-probe-no-network-provider",
                          "scope": "Generated controlled guide-audio regression. References are evaluated after ASR, never supplied to the decoder. Not unseen-song accuracy."}}
    path = destination / "evaluation.json"
    write_json(path, evaluation)
    print(json.dumps(evaluation["summary"], ensure_ascii=False), flush=True)
    print(path, flush=True)
    return path


def self_test() -> None:
    """Exercise failure detection and repeat association without a model."""
    types = {1: "Intro", 2: "Verse"}
    event = {"id": "e1", "kind": "section", "key": "Verse", "ordinal": 2,
             "announcementSeconds": 1.0, "expectedTimeSeconds": 2.0, "toleranceSeconds": .04}
    case = {"id": "self-test", "expectedEvents": [event]}
    def raw(suggestions=None, cues=None, error=""):
        return {"success": True, "cancelled": False, "segments": [], "alignmentError": error,
                "suggestions": suggestions or [], "secondaryCues": cues or []}
    good = {"type": 2, "name": "Verso 2", "announcementSeconds": 1.0, "startSeconds": 2.0}
    assert evaluate_case(case, raw([good]), "raw.json", types)["status"] == "passed"
    missing = evaluate_case(case, raw(), "raw.json", types)
    assert not missing["checks"][0]["passed"] and missing["checks"][0]["actual"] is None
    wrong = {**good, "name": "Verso 1"}
    mismatch = evaluate_case(case, raw([wrong]), "raw.json", types)
    assert mismatch["status"] == "failed" and len(mismatch["unexpected"]) == 1
    tentative = evaluate_case(case, raw([{**good, "name": "Posible Verso 2"}]), "raw.json", types)
    assert tentative["status"] == "failed" and len(tentative["unexpected"]) == 1
    late = evaluate_case(case, raw([{**good, "startSeconds": 2.1}]), "raw.json", types)
    assert late["checks"][0]["actual"] and not late["checks"][0]["passed"]
    assert not late["unexpected"] and abs(late["checks"][0]["errorSeconds"] - .1) < 1e-9
    unrelated = evaluate_case(case, raw([{**good, "announcementSeconds": 20.0}]), "raw.json", types)
    assert not unrelated["checks"][0]["passed"] and len(unrelated["unexpected"]) == 1
    extra = {"kind": "instruction", "name": "All In", "announcementSeconds": 3.0, "timeSeconds": 3.0}
    assert len(evaluate_case(case, raw([good], [extra]), "raw.json", types)["unexpected"]) == 1
    repeated = [{"id": "first", "kind": "count", "key": "2", "announcementSeconds": 1.0, "expectedTimeSeconds": 1.0},
                {"id": "second", "kind": "count", "key": "2", "announcementSeconds": 1.4, "expectedTimeSeconds": 1.4}]
    observed = [{"kind": "count", "key": "2", "name": "2", "announcementSeconds": 1.4, "timeSeconds": 1.4}]
    assert pair_events(repeated, observed) == {1: 0}
    bare = {**event, "key": "Intro", "requireUnnumbered": True}
    bare.pop("ordinal")
    assert evaluate_case({"id": "bare", "expectedEvents": [bare]}, raw([{**good, "type": 1, "name": "Intro 2"}]), "raw.json", types)["status"] == "failed"
    review = {"id": "review", "expectedEvents": [], "expectedPolicy": {"alignmentError": True}}
    reviewed = raw(error="No hay un compás siguiente válido para Final (2.0 s).")
    reviewed["segments"] = [{"text": "Final", "startSeconds": 2.0, "endSeconds": 2.4}]
    assert evaluate_case(review, reviewed, "raw.json", types)["status"] == "passed"
    assert evaluate_case(review, raw(), "raw.json", types)["status"] == "failed"
    assert evaluate_case(review, raw(error="Mapa de tempo inválido."), "raw.json", types)["status"] == "failed"
    assert evaluate_case(review, raw(error="No hay un compás siguiente válido para Final (2.0 s)."), "raw.json", types)["status"] == "failed"
    assert evaluate_case({"id": "silent", "expectedEvents": []}, raw([good]), "raw.json", types)["status"] == "failed"
    assert evaluate_case({"id": "silent", "expectedEvents": []}, {"success": False, "error": "backend failure"}, "raw.json", types)["status"] == "failed"
    grid = {"bpm": 116, "meter": "4/4", "originSeconds": .5, "durationSeconds": 6.0}
    empty = {**raw(), "alignmentBpm": 116, "alignmentMeter": "4/4", "alignmentOriginSeconds": .5,
             "durationSeconds": 6.0}
    assert not context_error(grid, empty)
    for key, invalid in (("alignmentBpm", 120), ("alignmentMeter", "3/4"),
                         ("alignmentOriginSeconds", 0), ("durationSeconds", 5.9)):
        assert context_error(grid, {**empty, key: invalid})
        missing_context = dict(empty); missing_context.pop(key)
        assert context_error(grid, missing_context)
    with tempfile.TemporaryDirectory(prefix="guide-runner-provenance-") as directory:
        path = Path(directory) / "case.json"
        manifest = {"suiteId": "suite"}; with_id = {**grid, "id": "silent"}
        record = {"schemaVersion": 1, "suiteId": "suite", "caseId": "silent", "manifestSha256": "m",
                  "inputSha256": "a", "rawTranscriptSha256": "r", "grid": grid, "executionError": ""}
        assert check_provenance(path, manifest, with_id, "m", "a", "r")[0] == "legacy-context-verified"
        write_json(path, record)
        assert check_provenance(path, manifest, with_id, "m", "a", "r") == ("input-and-transcript-verified", "")
        assert check_provenance(path, manifest, with_id, "m", "a", "changed")[1]
        assert check_provenance(path, manifest, with_id, "m", "other-audio", "r")[1]
    print("Self-test: omisiones, extras, ordinales, tiempos, repeticiones, revisión y fallos del proveedor verificados.")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--probe", type=Path)
    parser.add_argument("--model", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--case", action="append", help="ID de caso a ejecutar; se puede repetir.")
    parser.add_argument("--evaluate-only", action="store_true", help="Comparar JSON existentes; no ejecuta Whisper.")
    parser.add_argument("--timeout-seconds", type=float, default=180.0)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        if args.self_test:
            self_test(); return 0
        if args.manifest is None or args.output_dir is None:
            parser.error("--manifest y --output-dir son obligatorios.")
        if not math.isfinite(args.timeout_seconds) or args.timeout_seconds <= 0:
            parser.error("--timeout-seconds debe ser finito y positivo.")
        run(args)
        # A completed evaluation can legitimately contain failed cases. Report
        # that through exit 2 so CI may flag it without losing the documents.
        return 2 if read_json(args.output_dir / "evaluation.json")["summary"]["failed"] else 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
