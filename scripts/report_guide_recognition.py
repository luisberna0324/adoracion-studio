#!/usr/bin/env python3
"""Render a real guide-corpus evaluation as an offline HTML report and CSV.

Python 3.12+, standard library only. This script never transcribes audio or
constructs evaluation results: it presents the evaluator's existing JSON.
"""

from __future__ import annotations

import argparse
import csv
import html
import io
import json
import math
import os
from pathlib import Path
import re
import tempfile
from urllib.parse import quote


STATUS_LABELS = {
    "recognized": "Reconocido",
    "mismatch": "No coincide",
    "review": "Propuesta por revisar",
    "missing": "Sin transcripción asignada",
    "split": "Varios fragmentos",
    "reference-not-in-catalog": "Referencia fuera del catálogo",
}
KIND_LABELS = {
    "section": "Sección",
    "tentative-section": "Posible sección",
    "instruction": "Indicación",
    "count": "Conteo",
    "unknown": "Sin reconocer",
}


def escaped(value: object) -> str:
    return html.escape("" if value is None else str(value), quote=True)


def time_label(seconds: float) -> str:
    minutes = int(seconds // 60)
    return f"{minutes}:{seconds % 60:06.3f}"


def read_evaluation(path: Path) -> dict:
    with path.open(encoding="utf-8") as source:
        evaluation = json.load(source)
    if not isinstance(evaluation, dict):
        raise ValueError("La evaluación debe ser un objeto JSON.")
    cases = evaluation.get("cases")
    summary = evaluation.get("summary")
    if not isinstance(cases, list) or not cases or not isinstance(summary, dict):
        raise ValueError("La evaluación necesita cases no vacíos y summary.")
    seen: set[str] = set()
    for case in cases:
        if not isinstance(case, dict):
            raise ValueError("Cada caso debe ser un objeto JSON.")
        for key in ("id", "language", "expectedLabel", "status"):
            if not isinstance(case.get(key), str) or not case[key].strip():
                raise ValueError(f"Cada caso necesita {key} de texto no vacío.")
        if case["id"] in seen:
            raise ValueError(f"ID de caso duplicado: {case['id']}")
        seen.add(case["id"])
        for key in ("startSeconds", "endSeconds"):
            value = case.get(key)
            if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
                raise ValueError(f"{case['id']}: {key} debe ser un número finito.")
        if case["startSeconds"] < 0 or case["endSeconds"] <= case["startSeconds"]:
            raise ValueError(f"{case['id']}: intervalo de audio inválido.")
        for key in ("rawCatalogCorrect", "interpretedCorrect"):
            if not isinstance(case.get(key), bool):
                raise ValueError(f"{case['id']}: {key} debe ser booleano.")
        if not isinstance(case.get("actual"), list) or any(
            not isinstance(item, dict) or not isinstance(item.get("text"), str)
            for item in case["actual"]
        ):
            raise ValueError(f"{case['id']}: actual debe contener los fragmentos originales.")
    for key, observed in (
        ("cases", len(cases)),
        ("rawCatalogCorrect", sum(case["rawCatalogCorrect"] for case in cases)),
        ("interpretedCorrect", sum(case["interpretedCorrect"] for case in cases)),
    ):
        if isinstance(summary.get(key), bool) or summary.get(key) != observed:
            raise ValueError(f"summary.{key} no coincide con los casos; no se generó un informe inconsistente.")
    if not isinstance(evaluation.get("byLanguage", {}), dict):
        raise ValueError("byLanguage debe ser un objeto JSON.")
    return evaluation


def label_text(label: object) -> str:
    if not isinstance(label, dict):
        return "Sin datos"
    kind = label.get("kind", "unknown")
    if kind == "unknown":
        return "Sin reconocer"
    key = str(label.get("key", ""))
    ordinal = label.get("ordinal")
    if ordinal is not None:
        key += f" {ordinal}"
    return f"{KIND_LABELS.get(str(kind), str(kind))}: {key}"


def fragments(case: dict, field: str) -> str:
    if not case["actual"]:
        return '<span class="muted">—</span>'
    items = []
    for item in case["actual"]:
        text = item["text"] if field == "text" else label_text(item.get(field))
        detail = ""
        if field == "interpreted" and item.get("interpretationReason"):
            detail = f"<small>{escaped(item['interpretationReason'])}</small>"
        items.append(f"<li>{escaped(text)}{detail}</li>")
    return '<ul class="fragments">' + "".join(items) + "</ul>"


def count(value: object) -> str:
    return str(value) if isinstance(value, int) and not isinstance(value, bool) and value >= 0 else "—"


def table_row(case: dict, number: int) -> str:
    status = case["status"]
    css_status = status if status in STATUS_LABELS else "other"
    badge = STATUS_LABELS.get(status, status)
    raw = "Sí" if case["rawCatalogCorrect"] else "No"
    interpreted = "Sí" if case["interpretedCorrect"] else "No"
    origin = case.get("sourceZipPath") or ""
    return f"""<tr data-language="{escaped(case['language'])}" data-status="{escaped(status)}">
<td class="number">{number}</td>
<td><strong>{escaped(case['expectedLabel'])}</strong><small>{escaped(case['language'])} · {escaped(case['id'])}</small>
<small class="source">{escaped(origin)}</small></td>
<td class="audio-cell"><button type="button" class="listen" data-start="{case['startSeconds']:.9f}" data-end="{case['endSeconds']:.9f}" aria-label="Escuchar {escaped(case['expectedLabel'])}" aria-pressed="false">▶ Escuchar</button>
<small>{time_label(case['startSeconds'])}–{time_label(case['endSeconds'])}</small></td>
<td>{fragments(case, 'text')}</td><td>{fragments(case, 'rawCatalog')}<small class="verdict">Coincide: {raw}</small></td>
<td>{fragments(case, 'interpreted')}<small class="verdict">Coincide: {interpreted}</small></td>
<td><span class="badge {css_status}">{escaped(badge)}</span></td></tr>"""


def render_html(evaluation: dict, audio_url: str, evaluation_name: str) -> str:
    cases = evaluation["cases"]
    summary = evaluation["summary"]
    languages = sorted({case["language"] for case in cases})
    statuses = sorted({case["status"] for case in cases})
    language_options = "".join(f'<option value="{escaped(language)}">{escaped(language)}</option>' for language in languages)
    status_options = "".join(f'<option value="{escaped(status)}">{escaped(STATUS_LABELS.get(status, status))}</option>' for status in statuses)
    rows = "\n".join(table_row(case, index) for index, case in enumerate(cases, 1))
    language_rows = []
    for language, totals in sorted(evaluation.get("byLanguage", {}).items()):
        if not isinstance(totals, dict):
            raise ValueError("Cada entrada de byLanguage debe contener sus conteos.")
        cells = "".join(f"<td>{count(totals.get(key))}</td>" for key in (
            "cases", "catalogCovered", "rawCatalogCorrect", "interpretedCorrect", "tentativeForReview", "missing", "split"))
        language_rows.append(f"<tr><th>{escaped(language)}</th>{cells}</tr>")
    extras = evaluation.get("unexpectedSegments", [])
    if not isinstance(extras, list):
        raise ValueError("unexpectedSegments debe ser una lista.")
    extra_rows = []
    for extra in extras:
        if not isinstance(extra, dict):
            continue
        segment = extra.get("segment", extra)
        if not isinstance(segment, dict):
            continue
        extra_rows.append(f"<li><strong>{escaped(extra.get('reason', 'Sin asignar'))}</strong>: {escaped(segment.get('text', ''))}</li>")
    extra_detail = (
        f'<details class="extras"><summary>{len(extras)} segmentos sin asignación válida a un clip</summary>'
        '<p>No se acreditan como aciertos. Incluyen voz en silencios o fragmentos ambiguos entre clips.</p>'
        '<ul>' + "".join(extra_rows) + '</ul></details>'
        if extras else '<p class="muted">El evaluador no registró segmentos sin asignación.</p>'
    )
    template = """<!doctype html>
<html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Evaluación local de Guía · PlayWorship</title>
<style>
:root{color-scheme:dark;--bg:#111614;--panel:#1b231f;--line:#344139;--text:#eef3ef;--muted:#b5c2b9;--green:#83cea1;--blue:#88bedc;--amber:#e8bb76}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px/1.5 system-ui,sans-serif}main{max-width:1600px;margin:auto;padding:30px 24px 60px}h1{font-size:clamp(24px,3vw,34px);margin:0 0 10px}h2{font-size:21px;margin:30px 0 12px}p{max-width:1050px}.muted,small{color:var(--muted)}small{display:block;font-size:12px;margin-top:5px}.intro{border-left:3px solid var(--green);padding-left:16px}.metrics{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px;margin:24px 0}.metric{border:1px solid var(--line);border-radius:10px;background:var(--panel);padding:15px}.metric strong{display:block;font-size:30px}.metric span{color:var(--muted);font-size:13px}.player{position:sticky;top:0;z-index:2;padding:14px 16px;background:#18231ff5;border:1px solid var(--line);border-radius:10px;backdrop-filter:blur(10px)}audio{width:min(600px,100%);height:38px;vertical-align:middle}.player-status{display:inline-block;margin:0 0 0 15px;color:var(--muted);font-size:13px}.filters{display:flex;gap:14px;align-items:end;flex-wrap:wrap;margin:20px 0 14px}label{display:grid;gap:5px;font-size:13px;color:var(--muted)}select,button{font:inherit;color:var(--text);border:1px solid #53675b;background:#22352b;border-radius:6px;padding:8px 12px}button{cursor:pointer}button:hover,button[aria-pressed=true]{background:#345b43;border-color:var(--green)}select:focus-visible,button:focus-visible{outline:2px solid var(--green);outline-offset:2px}button.listen{white-space:nowrap;font-size:13px}.table-scroll{overflow-x:auto;border:1px solid var(--line);border-radius:9px}table{border-collapse:collapse;width:100%;text-align:left}th,td{border-bottom:1px solid var(--line);padding:12px;vertical-align:top}thead th{background:var(--panel);font-size:12px;color:var(--muted)}tbody tr:last-child td,tbody tr:last-child th{border-bottom:0}tbody tr:hover{background:#1a251e}td.number{color:var(--muted);font-size:12px}td.audio-cell{min-width:155px}.source{overflow-wrap:anywhere;max-width:250px}.fragments{list-style:none;padding:0;margin:0;min-width:140px;max-width:300px}.fragments li+li{margin-top:8px;padding-top:8px;border-top:1px dotted var(--line)}.verdict{margin-top:10px}.badge{display:inline-block;border:1px solid #59615b;border-radius:6px;padding:3px 7px;font-size:12px;white-space:nowrap}.recognized{border-color:#426d51;color:#b2e6c1}.review{border-color:#896a3e;color:var(--amber)}.mismatch,.missing,.split{border-color:#865b55;color:#e8b8af}.reference-not-in-catalog{color:var(--blue)}.extras{padding:14px 16px;border:1px solid var(--line);border-radius:8px;margin:20px 0}.extras summary{cursor:pointer}footer{margin-top:28px;border-top:1px solid var(--line);padding-top:16px;font-size:12px;color:var(--muted);overflow-wrap:anywhere}[hidden]{display:none!important}@media(max-width:700px){main{padding:20px 12px 40px}.player-status{display:block;margin:8px 0 0}th,td{padding:10px}.metrics{grid-template-columns:repeat(2,minmax(0,1fr))}}
</style></head><body><main>
<h1>Evaluación local de los clips de Guía</h1>
<div class="intro"><p>Prueba del paquete de voces en clips aislados, separados por silencio. <strong>No mide precisión sobre canciones con música mezclada.</strong> Las etiquetas de referencia se usan para evaluar después de Whisper; no se envían al transcriptor.</p>
<p>El catálogo directo clasifica el texto elegido por Whisper. La interpretación añade equivalencias confirmadas y evidencia de alternativas. Una coincidencia exige tipo, familia y ordinal correctos; las propuestas tentativas y las referencias fuera del catálogo no cuentan como acierto.</p>@@REVIEW_NOTICE@@</div>
<div class="metrics"><div class="metric"><strong>@@CASES@@</strong><span>Clips evaluados</span></div><div class="metric"><strong>@@COVERED@@</strong><span>Referencias cubiertas por el catálogo</span></div><div class="metric"><strong>@@RAW@@</strong><span>Coincidencias del catálogo directo</span></div><div class="metric"><strong>@@INTERPRETED@@</strong><span>Coincidencias tras interpretación</span></div><div class="metric"><strong>@@TENTATIVE@@</strong><span>Propuestas tentativas por revisar</span></div></div>
<section class="player" aria-label="Reproductor único del corpus"><audio id="corpus-audio" controls preload="metadata" src="@@AUDIO@@"></audio><span id="player-status" class="player-status" role="status">Elige «Escuchar» para reproducir un clip.</span></section>
<h2>Resultados por idioma</h2><div class="table-scroll"><table><thead><tr><th>Idioma</th><th>Clips</th><th>Cubiertos</th><th>Catálogo directo</th><th>Interpretados</th><th>Por revisar</th><th>Sin voz asignada</th><th>Varios fragmentos</th></tr></thead><tbody>@@LANGUAGE_ROWS@@</tbody></table></div>
<h2>Revisión de cada clip</h2><div class="filters"><label>Idioma<select id="language-filter"><option value="">Todos</option>@@LANGUAGE_OPTIONS@@</select></label><label>Estado<select id="status-filter"><option value="">Todos</option>@@STATUS_OPTIONS@@</select></label><span id="visible-count" class="muted" role="status">@@CASES@@ clips</span></div>
<div class="table-scroll"><table id="case-table"><thead><tr><th>#</th><th>Referencia</th><th>Audio original</th><th>Texto de Whisper</th><th>Catálogo directo</th><th>Interpretación</th><th>Estado</th></tr></thead><tbody>@@ROWS@@</tbody></table></div>
@@EXTRAS@@
<footer>Resultados leídos de @@EVALUATION@@. Proveedor: @@PROVIDER@@. Modelo: @@MODEL@@. El audio WAV permanece junto al informe o en la ruta relativa indicada; el HTML funciona sin servicios externos. CSV completo: <a href="guide-recognition-cases.csv">guide-recognition-cases.csv</a>.</footer>
</main><script>
'use strict';
const audio=document.getElementById('corpus-audio');
const status=document.getElementById('player-status');
const languageFilter=document.getElementById('language-filter');
const statusFilter=document.getElementById('status-filter');
const rows=Array.from(document.querySelectorAll('#case-table tbody tr'));
let stopAt=null, activeButton=null, frame=0, playRequest=0;
function filterRows(){let visible=0;for(const row of rows){const show=(!languageFilter.value||row.dataset.language===languageFilter.value)&&(!statusFilter.value||row.dataset.status===statusFilter.value);row.hidden=!show;if(show)visible++;}document.getElementById('visible-count').textContent=visible+' de '+rows.length+' clips';}
languageFilter.addEventListener('change',filterRows);statusFilter.addEventListener('change',filterRows);
function releaseButton(){if(activeButton)activeButton.setAttribute('aria-pressed','false');activeButton=null;}
function monitor(){cancelAnimationFrame(frame);if(audio.paused||stopAt===null)return;if(audio.currentTime>=stopAt){const end=stopAt;stopAt=null;audio.pause();audio.currentTime=end;releaseButton();status.textContent='Clip finalizado.';return;}frame=requestAnimationFrame(monitor);}
for(const button of document.querySelectorAll('button.listen'))button.addEventListener('click',async()=>{const request=++playRequest;audio.pause();cancelAnimationFrame(frame);releaseButton();const start=Number(button.dataset.start),end=Number(button.dataset.end);if(!Number.isFinite(start)||!Number.isFinite(end)||end<=start)return;activeButton=button;button.setAttribute('aria-pressed','true');stopAt=end;status.textContent='Escuchando '+start.toFixed(3)+'–'+end.toFixed(3)+' s';try{audio.currentTime=start;await audio.play();if(request===playRequest)monitor();}catch(error){if(request!==playRequest)return;stopAt=null;releaseButton();status.textContent='No se pudo reproducir el WAV. Comprueba que el audio siga disponible en su ruta relativa.';}});
audio.addEventListener('play',monitor);audio.addEventListener('timeupdate',()=>{if(stopAt!==null&&audio.currentTime>=stopAt)monitor();});audio.addEventListener('pause',()=>cancelAnimationFrame(frame));audio.addEventListener('ended',()=>{stopAt=null;releaseButton();status.textContent='Audio finalizado.';});audio.addEventListener('error',()=>{stopAt=null;releaseButton();status.textContent='No se encuentra el WAV o el navegador no puede abrirlo.';});
filterRows();
</script></body></html>"""
    replacements = {
        "REVIEW_NOTICE": ('<p>Esta evaluación incluye las correcciones revisadas con el usuario. Comprueba que esos casos siguen funcionando; no mide la precisión en voces o canciones nuevas.</p>'
                          if evaluation.get("metricNotice") else ""),
        "CASES": count(summary.get("cases")),
        "COVERED": count(summary.get("catalogCovered")),
        "RAW": count(summary.get("rawCatalogCorrect")),
        "INTERPRETED": count(summary.get("interpretedCorrect")),
        "TENTATIVE": count(summary.get("tentativeForReview")),
        "AUDIO": escaped(audio_url),
        "LANGUAGE_ROWS": "".join(language_rows),
        "LANGUAGE_OPTIONS": language_options,
        "STATUS_OPTIONS": status_options,
        "ROWS": rows,
        "EXTRAS": extra_detail,
        "EVALUATION": escaped(evaluation_name),
        "PROVIDER": escaped(evaluation.get("providerVersion", "Sin datos")),
        "MODEL": escaped(evaluation.get("modelFingerprint", "Sin datos")),
    }
    # Substitute the template once: placeholder-looking input remains literal
    # user data and cannot be expanded into HTML by a later replacement.
    return re.sub(r"@@([A-Z_]+)@@", lambda match: replacements[match.group(1)], template)


def csv_value(value: object) -> str:
    text = "" if value is None else str(value)
    # Keep exported text from becoming a spreadsheet formula when opened.
    return "'" + text if text.lstrip().startswith(("=", "+", "-", "@", "\t", "\r")) else text


def render_csv(cases: list[dict]) -> str:
    output = io.StringIO(newline="")
    writer = csv.writer(output)
    writer.writerow([
        "id", "sourceZipPath", "language", "expectedLabel", "startSeconds", "endSeconds",
        "status", "rawCatalogCorrect", "interpretedCorrect", "actualText", "rawCatalog",
        "interpreted", "actualJson",
    ])
    for case in cases:
        values = [case.get(key, "") for key in (
            "id", "sourceZipPath", "language", "expectedLabel", "startSeconds", "endSeconds", "status")]
        values += [str(case["rawCatalogCorrect"]).lower(), str(case["interpretedCorrect"]).lower()]
        values += ["\n".join(item["text"] for item in case["actual"])]
        values += ["\n".join(label_text(item.get(field)) for item in case["actual"])
                   for field in ("rawCatalog", "interpreted")]
        values += [json.dumps(case["actual"], ensure_ascii=False, separators=(",", ":"))]
        writer.writerow(csv_value(value) for value in values)
    return output.getvalue()


def write_atomic(path: Path, text: str) -> None:
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", newline="", dir=path.parent,
                                         prefix=f".{path.name}.", suffix=".tmp", delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(text)
        temporary.replace(path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Presenta el JSON real del evaluador de clips de Guía en HTML sin dependencias externas y CSV."
    )
    parser.add_argument("--evaluation", required=True, type=Path, help="JSON producido por el evaluador del corpus.")
    parser.add_argument("--audio", required=True, type=Path, help="WAV real del corpus; se enlaza mediante una ruta relativa.")
    parser.add_argument("--output-directory", "--output-dir", type=Path,
                        help="Carpeta para HTML/CSV; por defecto, la del JSON de evaluación.")
    args = parser.parse_args()
    evaluation_path = args.evaluation.expanduser().resolve()
    audio_path = args.audio.expanduser().resolve()
    destination = (args.output_directory or evaluation_path.parent).expanduser().resolve()
    try:
        if not audio_path.is_file():
            raise ValueError(f"No existe el audio del corpus: {audio_path}")
        evaluation = read_evaluation(evaluation_path)
        audio_url = quote(Path(os.path.relpath(audio_path, destination)).as_posix(), safe="/.")
        html_text = render_html(evaluation, audio_url, evaluation_path.name)
        csv_text = render_csv(evaluation["cases"])
        destination.mkdir(parents=True, exist_ok=True)
        html_path = destination / "guide-recognition-report.html"
        csv_path = destination / "guide-recognition-cases.csv"
        if evaluation_path in (html_path, csv_path) or audio_path in (html_path, csv_path):
            raise ValueError("Las salidas no pueden reemplazar los archivos de evaluación o audio.")
        write_atomic(csv_path, csv_text)
        write_atomic(html_path, html_text)
    except (OSError, ValueError, TypeError) as error:
        parser.exit(1, f"Error: {error}\n")
    print(f"HTML: {html_path}")
    print(f"CSV: {csv_path}")
    print(f"Casos: {len(evaluation['cases'])}; resultados originales sin volver a ejecutar ASR.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
