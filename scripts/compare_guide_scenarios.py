#!/usr/bin/env python3
"""Compare completed guide-scenario evaluations without changing their evidence.

Python 3.10+, standard library only. Does not run ASR, infer labels, change
references or import human feedback. Same suite, audio and model are required;
a provider-version change is recorded explicitly as the code under comparison.
"""
from __future__ import annotations

import argparse
import copy
import csv
import hashlib
import io
import json
import math
from pathlib import Path
import sys


MAX_BYTES = 32 * 1024 * 1024


def finite(value: object) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def present(value: object) -> bool:
    return isinstance(value, str) and bool(value.strip())


def read(path: Path) -> dict:
    if not path.is_file() or path.stat().st_size > MAX_BYTES:
        raise ValueError(f"Evaluación ausente o demasiado grande: {path}")
    def invalid(value: str):
        raise ValueError(f"Número no finito: {value}")
    value = json.loads(path.read_text(encoding="utf-8"), parse_constant=invalid)
    if not isinstance(value, dict):
        raise ValueError(f"La evaluación debe ser un objeto: {path}")
    return value


def index_evaluation(value: dict) -> dict[str, dict]:
    if value.get("schemaVersion") != 1 or not present(value.get("suiteId")):
        raise ValueError("Se requiere schemaVersion=1 y suiteId.")
    cases = value.get("cases")
    if not isinstance(cases, list) or not 1 <= len(cases) <= 256:
        raise ValueError("cases debe contener de 1 a 256 casos.")
    result = {}
    totals = dict(cases=len(cases), passed=0, failed=0, pending=0,
                  expectedEvents=0, matchedEvents=0, unexpectedEvents=0)
    for case in cases:
        if not isinstance(case, dict) or not present(case.get("id")) or case["id"] in result:
            raise ValueError("Cada caso debe tener un ID único.")
        case_id = case["id"]
        if case.get("status") not in {"passed", "failed"}:
            raise ValueError(f"{case_id}: sólo se comparan ejecuciones completas, sin casos pendientes.")
        for key in ("inputSha256", "providerVersion", "modelFingerprint"):
            if not present(case.get(key)):
                raise ValueError(f"{case_id}: falta {key}; no se puede acreditar la comparación.")
        checks, extras, policies = (case.get(key) for key in ("checks", "unexpected", "policyChecks"))
        if not all(isinstance(items, list) for items in (checks, extras, policies)):
            raise ValueError(f"{case_id}: faltan checks, unexpected o policyChecks.")
        event_ids = set()
        for check in checks:
            if (not isinstance(check, dict) or not present(check.get("eventId"))
                    or check["eventId"] in event_ids or not isinstance(check.get("passed"), bool)):
                raise ValueError(f"{case_id}: comprobación duplicada o inválida.")
            event_ids.add(check["eventId"])
            expected = check.get("expected")
            if (not isinstance(expected, dict) or not present(expected.get("key"))
                    or expected.get("kind") not in {"section", "instruction", "count"}):
                raise ValueError(f"{case_id}: falta la referencia completa del evento.")
            if ("ordinal" in expected and (isinstance(expected["ordinal"], bool)
                    or not isinstance(expected["ordinal"], int) or not 1 <= expected["ordinal"] <= 999)):
                raise ValueError(f"{case_id}: ordinal esperado inválido.")
            for key in ("timeSeconds", "announcementSeconds", "toleranceSeconds"):
                if not finite(expected.get(key)) or expected[key] < 0:
                    raise ValueError(f"{case_id}: referencia {key} inválida.")
            if check.get("actual") is not None and not isinstance(check["actual"], dict):
                raise ValueError(f"{case_id}: actual debe ser un objeto o null.")
            if check.get("errorSeconds") is not None and not finite(check["errorSeconds"]):
                raise ValueError(f"{case_id}: errorSeconds inválido.")
            if check["passed"] and (check.get("actual") is None or check.get("errorSeconds") is None):
                raise ValueError(f"{case_id}: evento aprobado sin resultado o diferencia temporal.")
        if any(not isinstance(item, dict) for item in extras):
            raise ValueError(f"{case_id}: evento adicional inválido.")
        policy_keys = set()
        for policy in policies:
            if (not isinstance(policy, dict) or not present(policy.get("key"))
                    or policy["key"] in policy_keys or not isinstance(policy.get("passed"), bool)
                    or "expected" not in policy):
                raise ValueError(f"{case_id}: política duplicada o inválida.")
            policy_keys.add(policy["key"])
        if case["status"] == "passed" and (extras or any(not c["passed"] for c in checks + policies)):
            raise ValueError(f"{case_id}: caso aprobado con diferencias.")
        totals[case["status"]] += 1
        totals["expectedEvents"] += len(checks)
        totals["matchedEvents"] += sum(check["passed"] for check in checks)
        totals["unexpectedEvents"] += len(extras)
        result[case_id] = case
    if value.get("summary") != totals:
        raise ValueError("summary no coincide con los resultados del archivo.")
    providers = {case["providerVersion"] for case in cases}
    models = {case["modelFingerprint"] for case in cases}
    if len(providers) != 1 or len(models) != 1:
        raise ValueError("Una ejecución mezcla proveedores o modelos; evaluar cada condición por separado.")
    run = value.get("run", {})
    if not isinstance(run, dict):
        raise ValueError("run debe ser un objeto.")
    for key, unique in (("providerVersion", providers), ("modelFingerprint", models)):
        if run.get(key) is not None and run[key] not in unique:
            raise ValueError(f"run.{key} no coincide con sus casos.")
    return result


def state(check: dict) -> str:
    if check["passed"]:
        return "correcto"
    if check.get("actual") is None:
        return "ausente"
    delta = check.get("errorSeconds")
    if finite(delta) and abs(delta) > check["expected"]["toleranceSeconds"] + 1e-9:
        return "fuera-de-tiempo"
    return "incorrecto"


def event_counts(cases: dict[str, dict]) -> dict:
    counts = {name: 0 for name in ("correcto", "ausente", "fuera-de-tiempo", "incorrecto")}
    for case in cases.values():
        for check in case["checks"]:
            counts[state(check)] += 1
    counts["adicionales"] = sum(len(case["unexpected"]) for case in cases.values())
    counts["politicasFallidas"] = sum(not p["passed"] for case in cases.values() for p in case["policyChecks"])
    counts["casosAprobados"] = sum(case["status"] == "passed" for case in cases.values())
    return counts


def compare(before: dict, after: dict) -> dict:
    left, right = index_evaluation(before), index_evaluation(after)
    if before["suiteId"] != after["suiteId"]:
        raise ValueError("suiteId cambió: se requiere el mismo conjunto congelado.")
    if left.keys() != right.keys():
        raise ValueError("Los IDs de caso no coinciden.")
    events, cases, policies = [], [], []
    for case_id, first in left.items():
        second = right[case_id]
        if first["inputSha256"] != second["inputSha256"]:
            raise ValueError(f"{case_id}: cambió el audio de entrada.")
        if first["modelFingerprint"] != second["modelFingerprint"]:
            raise ValueError(f"{case_id}: cambió el modelo; esta comparación exige el mismo modelo.")
        a = {check["eventId"]: check for check in first["checks"]}
        b = {check["eventId"]: check for check in second["checks"]}
        if a.keys() != b.keys():
            raise ValueError(f"{case_id}: cambió el conjunto de eventos esperados.")
        for event_id, one in a.items():
            two = b[event_id]
            if one["expected"] != two["expected"]:
                raise ValueError(f"{case_id}/{event_id}: cambió la referencia o la tolerancia.")
            transition = ("corregido" if not one["passed"] and two["passed"] else
                          "regresión" if one["passed"] and not two["passed"] else
                          "sigue-correcto" if two["passed"] else "sigue-fallando")
            events.append({"caseId": case_id, "eventId": event_id, "expected": one["expected"],
                           "beforeState": state(one), "afterState": state(two), "transition": transition,
                           "beforeActual": one.get("actual"), "afterActual": two.get("actual"),
                           "beforeErrorSeconds": one.get("errorSeconds"), "afterErrorSeconds": two.get("errorSeconds"),
                           "beforeReason": one.get("reason", ""), "afterReason": two.get("reason", "")})
        p = {policy["key"]: policy for policy in first["policyChecks"]}
        q = {policy["key"]: policy for policy in second["policyChecks"]}
        if p.keys() != q.keys() or any(p[key]["expected"] != q[key]["expected"] for key in p):
            raise ValueError(f"{case_id}: cambió una política esperada.")
        for key in p:
            policies.append({"caseId": case_id, "key": key,
                             "beforePassed": p[key]["passed"], "afterPassed": q[key]["passed"],
                             "beforeReason": p[key].get("reason", ""), "afterReason": q[key].get("reason", "")})
        cases.append({"caseId": case_id, "beforeStatus": first["status"], "afterStatus": second["status"],
                      "beforeCorrect": sum(check["passed"] for check in a.values()),
                      "afterCorrect": sum(check["passed"] for check in b.values()), "expectedEvents": len(a),
                      "beforeExtras": first["unexpected"], "afterExtras": second["unexpected"]})
    first_case, second_case = next(iter(left.values())), next(iter(right.values()))
    return {"schemaVersion": 1, "suiteId": before["suiteId"],
            "providerBefore": first_case["providerVersion"], "providerAfter": second_case["providerVersion"],
            "providerChanged": first_case["providerVersion"] != second_case["providerVersion"],
            "modelFingerprint": first_case["modelFingerprint"],
            "beforeCounts": event_counts(left), "afterCounts": event_counts(right),
            "eventsFixed": sum(e["transition"] == "corregido" for e in events),
            "eventsRegressed": sum(e["transition"] == "regresión" for e in events),
            "policiesRegressed": sum(p["beforePassed"] and not p["afterPassed"] for p in policies),
            "casesRegressed": sum(c["beforeStatus"] == "passed" and c["afterStatus"] != "passed" for c in cases),
            "casesWithMoreExtras": sum(len(c["afterExtras"]) > len(c["beforeExtras"]) for c in cases),
            "beforeRun": before.get("run", {}), "afterRun": after.get("run", {}),
            "cases": cases, "events": events, "policies": policies}


def md(value: object) -> str:
    return str(value).replace("\\", "\\\\").replace("|", "\\|").replace("\n", " ").replace("\r", " ").replace("<", "&lt;").replace(">", "&gt;")


def number(value: object) -> str:
    return f"{value:.6f}" if finite(value) else "—"


def describe(value: object) -> str:
    if not isinstance(value, dict):
        return "—"
    return f"{value.get('kind', '?')}:{value.get('key', '?')}" + (f" {value['ordinal']}" if "ordinal" in value else "") + f" @ {number(value.get('timeSeconds'))} s"


def markdown(data: dict) -> str:
    lines = ["# PlayWorship · comparación del reconocimiento", "",
             "Mismos audios, referencias, tolerancias y modelo; comparación posterior a ASR.", "",
             f"Conjunto: `{data['suiteId']}`.", "",
             f"Proveedor anterior: `{md(data['providerBefore'])}`.",
             f"Proveedor nuevo: `{md(data['providerAfter'])}`.",
             f"Cambio de proveedor registrado: **{'sí' if data['providerChanged'] else 'no'}**.",
             f"Modelo sin cambios: `{md(data['modelFingerprint'])}`.", "",
             "| Métrica | Antes | Después | Cambio |", "| --- | ---: | ---: | ---: |"]
    for key in data["beforeCounts"]:
        a, b = data["beforeCounts"][key], data["afterCounts"][key]
        lines.append(f"| {key} | {a} | {b} | {b-a:+d} |")
    lines += ["", f"Eventos corregidos: **{data['eventsFixed']}**. Regresiones de eventos: **{data['eventsRegressed']}**. Regresiones de políticas: **{data['policiesRegressed']}**. Casos antes aprobados que fallan: **{data['casesRegressed']}**. Casos con más extras: **{data['casesWithMoreExtras']}**.",
              "", "## Casos", "", "| Caso | Estado antes → después | Correctos antes → después | Extras antes → después |",
              "| --- | --- | ---: | ---: |"]
    for case in data["cases"]:
        lines.append(f"| {md(case['caseId'])} | {case['beforeStatus']} → {case['afterStatus']} | {case['beforeCorrect']} → {case['afterCorrect']} / {case['expectedEvents']} | {len(case['beforeExtras'])} → {len(case['afterExtras'])} |")
    lines += ["", "## Comparación por evento", "",
              "| Caso / evento | Marca esperada | Anuncio esperado (s) | Tolerancia (ms) | Antes | Después | Error antes → después (ms) | Resultado |",
              "| --- | --- | ---: | ---: | --- | --- | --- | --- |"]
    for event in data["events"]:
        one = event['beforeErrorSeconds']; two = event['afterErrorSeconds']
        lines.append(f"| {md(event['caseId'])} / {md(event['eventId'])} | {md(describe(event['expected']))} | {number(event['expected']['announcementSeconds'])} | {event['expected']['toleranceSeconds']*1000:.1f} | {md(describe(event['beforeActual']))} | {md(describe(event['afterActual']))} | {number(one*1000 if finite(one) else None)} → {number(two*1000 if finite(two) else None)} | {event['transition']} |")
    if not data["events"]:
        lines.append("| — | — | — | — | — | — | — | Sin eventos de referencia |")
    lines += ["", "## Eventos adicionales", ""]
    for case in data["cases"]:
        if case["beforeExtras"] or case["afterExtras"]:
            lines += [f"- **{md(case['caseId'])}**: antes: {md('; '.join(describe(e) for e in case['beforeExtras']) or 'ninguno')}; después: {md('; '.join(describe(e) for e in case['afterExtras']) or 'ninguno')}."]
    if not any(c["beforeExtras"] or c["afterExtras"] for c in data["cases"]):
        lines.append("Ningún evento adicional en ninguna ejecución.")
    lines += ["", "## Alcance", "",
              "Las decisiones y emparejamientos provienen del evaluador; el comparador no vuelve a interpretar texto ni corrige referencias. Los eventos adicionales se muestran por caso sin atribuirles una identidad estable entre versiones. Las cifras describen estos audios conocidos, no precisión independiente sobre canciones nuevas. Una mejora neta no oculta regresiones individuales.",
              "", "El CSV de eventos incluye todos los eventos, sus tiempos, errores y razones. El JSON conserva las políticas y los metadatos de ambas ejecuciones. Esta herramienta no ejecuta Whisper ni acredita reproducción o guardado en la app."]
    return "\n".join(lines) + "\n"


def safe_cell(value: object) -> object:
    # Keep report text inert when opened by spreadsheet applications.
    return "'" + value if isinstance(value, str) and value.lstrip().startswith(("=", "+", "-", "@")) else value


def csv_text(rows: list[dict]) -> str:
    if not rows:
        return ""
    stream = io.StringIO(newline="")
    writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows({key: safe_cell(value) for key, value in row.items()} for row in rows)
    return stream.getvalue()


def write_reports(data: dict, output: Path) -> None:
    if output.is_symlink() or (output.exists() and (not output.is_dir() or any(output.iterdir()))):
        raise ValueError("La carpeta de comparación debe ser nueva o estar vacía.")
    output.mkdir(parents=True, exist_ok=True)
    (output / "comparison.json").write_text(json.dumps(data, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    (output / "comparison.md").write_text(markdown(data), encoding="utf-8")
    rows = []
    for event in data["events"]:
        expected = event["expected"]
        rows.append({"caseId": event["caseId"], "eventId": event["eventId"],
                     "kind": expected["kind"], "key": expected["key"], "ordinal": expected.get("ordinal", ""),
                     "announcementSeconds": expected["announcementSeconds"], "expectedSeconds": expected["timeSeconds"],
                     "toleranceSeconds": expected["toleranceSeconds"], "beforeState": event["beforeState"],
                     "afterState": event["afterState"], "transition": event["transition"],
                     "beforeActual": describe(event["beforeActual"]), "afterActual": describe(event["afterActual"]),
                     "beforeAnnouncementSeconds": event["beforeActual"].get("announcementSeconds") if event["beforeActual"] else None,
                     "afterAnnouncementSeconds": event["afterActual"].get("announcementSeconds") if event["afterActual"] else None,
                     "beforeErrorSeconds": event["beforeErrorSeconds"], "afterErrorSeconds": event["afterErrorSeconds"],
                     "beforeErrorMs": event["beforeErrorSeconds"]*1000 if finite(event["beforeErrorSeconds"]) else None,
                     "afterErrorMs": event["afterErrorSeconds"]*1000 if finite(event["afterErrorSeconds"]) else None,
                     "beforeReason": event["beforeReason"], "afterReason": event["afterReason"]})
    (output / "comparison-events.csv").write_text(csv_text(rows), encoding="utf-8")
    (output / "comparison-cases.csv").write_text(csv_text([
        {key: value for key, value in case.items() if key not in {"beforeExtras", "afterExtras"}} |
        {"beforeExtras": json.dumps(case["beforeExtras"], ensure_ascii=False),
         "afterExtras": json.dumps(case["afterExtras"], ensure_ascii=False)} for case in data["cases"]]), encoding="utf-8")


def self_test() -> None:
    expected = dict(kind="section", key="Intro", timeSeconds=2.5, announcementSeconds=.62, toleranceSeconds=.04)
    actual = dict(kind="section", key="Intro", name="Intro", timeSeconds=2.5, announcementSeconds=.62)
    case = dict(id="case", status="failed", checks=[dict(eventId="intro", passed=False, expected=expected, actual=None, errorSeconds=None)],
                unexpected=[dict(kind="unknown-section", key="Revisar", timeSeconds=2.5)],
                policyChecks=[dict(key="alignmentError", passed=True, expected=False)], inputSha256="audio-a", providerVersion="provider-8", modelFingerprint="model-a")
    before = dict(schemaVersion=1, suiteId="suite-a", summary=dict(cases=1, passed=0, failed=1, pending=0, expectedEvents=1, matchedEvents=0, unexpectedEvents=1), cases=[case])
    after = copy.deepcopy(before)
    after["cases"][0].update(status="passed", unexpected=[], providerVersion="provider-9")
    after["cases"][0]["checks"][0].update(passed=True, actual=actual, errorSeconds=0)
    after["summary"].update(passed=1, failed=0, matchedEvents=1, unexpectedEvents=0)
    result = compare(before, after)
    assert result["eventsFixed"] == 1 and result["eventsRegressed"] == 0 and result["providerChanged"]
    assert result["afterCounts"]["ausente"] == 0 and result["beforeCounts"]["ausente"] == 1
    assert compare(after, before)["eventsRegressed"] == 1
    identical = compare(before, before)
    assert identical["eventsFixed"] == identical["eventsRegressed"] == 0
    assert identical["events"][0]["expected"].get("ordinal") is None
    assert identical["beforeCounts"]["adicionales"] == 1
    unknown = copy.deepcopy(before)
    unknown["cases"][0]["checks"][0].update(actual=dict(kind="unknown-section", key="desconocido", timeSeconds=2.5), errorSeconds=0)
    assert compare(before, unknown)["afterCounts"]["incorrecto"] == 1
    timing = copy.deepcopy(after)
    timing["cases"][0].update(status="failed")
    timing["cases"][0]["checks"][0].update(passed=False, errorSeconds=.2)
    timing["cases"][0]["checks"][0]["actual"]["timeSeconds"] = 2.7
    timing["summary"].update(passed=0, failed=1, matchedEvents=0)
    assert compare(after, timing)["afterCounts"]["fuera-de-tiempo"] == 1
    extra = copy.deepcopy(after)
    extra["cases"][0].update(status="failed", unexpected=[dict(kind="unknown-section", key="extra", timeSeconds=3)])
    extra["summary"].update(passed=0, failed=1, unexpectedEvents=1)
    assert compare(after, extra)["casesWithMoreExtras"] == 1
    assert compare(after, extra)["casesRegressed"] == 1
    for mutation in (lambda x: x.update(suiteId="other"),
                     lambda x: x["cases"][0].update(inputSha256="other"),
                     lambda x: x["cases"][0].update(modelFingerprint="other"),
                     lambda x: x["cases"][0]["checks"][0]["expected"].update(toleranceSeconds=.5),
                     lambda x: x["cases"][0]["checks"][0]["expected"].update(kind="desconocido"),
                     lambda x: x["cases"][0]["checks"][0]["expected"].update(ordinal="uno"),
                     lambda x: x["cases"][0]["checks"][0].update(actual=[]),
                     lambda x: x["cases"][0]["checks"][0].update(eventId="other"),
                     lambda x: x["cases"][0].update(status="pending"),
                     lambda x: x.update(run=dict(providerVersion="otra-versión")),
                     lambda x: x["summary"].update(matchedEvents=99)):
        altered = copy.deepcopy(after); mutation(altered)
        try:
            compare(before, altered)
        except ValueError:
            pass
        else:
            raise AssertionError("Se aceptó una comparación no equivalente.")
    assert "&lt;" in md("<b>|x") and "\\|" in md("<b>|x")
    assert safe_cell("=HYPERLINK(1)").startswith("'") and safe_cell(-.1) == -.1
    assert len(list(csv.DictReader(io.StringIO(csv_text([dict(a="a,b", b="dos\nlíneas")]))))) == 1
    print("Self-test: mejoras, regresiones, extras, referencias/audio/modelo distintos y ejecuciones parciales verificados.")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path)
    parser.add_argument("--after", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test(); return 0
    if not all((args.before, args.after, args.output_dir)):
        parser.error("--before, --after y --output-dir son obligatorios.")
    try:
        result = compare(read(args.before), read(args.after))
        result["inputs"] = {name: {"path": str(path.resolve()), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                            for name, path in (("before", args.before), ("after", args.after))}
        write_reports(result, args.output_dir)
    except (OSError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr); return 1
    print(f"Comparación: {result['eventsFixed']} eventos corregidos; {result['eventsRegressed']} regresiones; carpeta {args.output_dir}")
    return 2 if any(result[key] for key in ("eventsRegressed", "policiesRegressed", "casesRegressed", "casesWithMoreExtras")) else 0


if __name__ == "__main__":
    raise SystemExit(main())
