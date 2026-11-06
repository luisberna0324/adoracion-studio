#!/usr/bin/env python3
"""Present recorded guide-scenario evaluations as offline review documents.

Python 3.10+, standard library only. No audio is generated or transcribed here;
expected references and actual production results remain separate. Exported
human feedback is an input for code review, never a runtime rule import.
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
import tempfile
from urllib.parse import quote


STATUSES = {"passed": "Aprobado", "failed": "Con diferencias", "pending": "Sin ejecutar"}
KINDS = {"section": "Sección", "instruction": "Indicación", "count": "Conteo"}


def finite(value: object) -> bool:
    return not isinstance(value, bool) and isinstance(value, (int, float)) and math.isfinite(value)


def nonempty(value: object) -> bool:
    return isinstance(value, str) and bool(value.strip())


def validate_acoustic_evidence(segment: dict, case: dict) -> None:
    """Display recorded evidence only; never infer annotations from ASR text."""
    announcements = segment.get("acousticAnnouncements", [])
    ratio = segment.get("acousticResidualRatio")
    if not isinstance(announcements, list) or len(announcements) > 8:
        raise ValueError(f"{case['id']}: acousticAnnouncements debe ser una lista de hasta ocho puntos.")
    if not announcements:
        if "acousticResidualRatio" in segment:
            raise ValueError(f"{case['id']}: no se admite residuo acústico sin anuncios.")
        return
    if not finite(ratio) or not 0 <= ratio <= 1:
        raise ValueError(f"{case['id']}: acousticResidualRatio debe ser finito y estar entre cero y uno.")
    previous = -1.0
    for announcement in announcements:
        if not isinstance(announcement, dict):
            raise ValueError(f"{case['id']}: cada anuncio acústico debe ser un objeto.")
        for key in ("text", "language", "templateIdentity"):
            if not nonempty(announcement.get(key)) or len(announcement[key]) > 512:
                raise ValueError(f"{case['id']}: anuncio acústico sin {key} válido.")
        start, end, correlation = (announcement.get(key) for key in
                                   ("startSeconds", "endSeconds", "correlation"))
        if (not finite(start) or not finite(end) or not 0 <= start < end <= case["durationSeconds"] + 1e-6
                or start <= previous or not finite(correlation) or not 0 <= correlation <= 1):
            raise ValueError(f"{case['id']}: tiempos o correlación acústica inválidos.")
        previous = start


def read_json(path: Path, maximum_bytes: int) -> dict:
    if not path.is_file() or path.stat().st_size > maximum_bytes:
        raise ValueError(f"Archivo inexistente o demasiado grande: {path}")
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"El JSON debe contener un objeto: {path}")
    return value


def read_inputs(manifest_path: Path, evaluation_path: Path) -> tuple[dict, dict]:
    manifest = read_json(manifest_path, 10 * 1024 * 1024)
    evaluation = read_json(evaluation_path, 32 * 1024 * 1024)
    if manifest.get("schemaVersion") != 1 or evaluation.get("schemaVersion") != 1:
        raise ValueError("Sólo se admite schemaVersion 1.")
    if not nonempty(manifest.get("suiteId")) or manifest["suiteId"] != evaluation.get("suiteId"):
        raise ValueError("El manifiesto y la evaluación deben identificar el mismo conjunto.")
    cases = manifest.get("cases")
    actual_cases = evaluation.get("cases")
    if not isinstance(cases, list) or not cases or not isinstance(actual_cases, list):
        raise ValueError("Se necesitan casos de referencia y resultados por caso.")
    references: dict[str, dict] = {}
    expected_count = 0
    for case in cases:
        if not isinstance(case, dict) or not nonempty(case.get("id")) or case["id"] in references:
            raise ValueError("Cada caso del manifiesto necesita un ID único.")
        for key in ("title", "group", "audioPath", "meter"):
            if not nonempty(case.get(key)):
                raise ValueError(f"{case['id']}: falta {key}.")
        if Path(case["audioPath"]).is_absolute():
            raise ValueError(f"{case['id']}: audioPath debe ser relativo al manifiesto.")
        for key in ("bpm", "originSeconds", "durationSeconds"):
            if not finite(case.get(key)):
                raise ValueError(f"{case['id']}: {key} debe ser finito.")
        if case["bpm"] <= 0 or case["originSeconds"] < 0 or case["durationSeconds"] <= 0:
            raise ValueError(f"{case['id']}: BPM, origen o duración inválidos.")
        events = case.get("expectedEvents")
        if not isinstance(events, list):
            raise ValueError(f"{case['id']}: falta expectedEvents.")
        event_ids = set()
        for event in events:
            if not isinstance(event, dict) or not nonempty(event.get("id")) or event["id"] in event_ids:
                raise ValueError(f"{case['id']}: cada evento necesita un ID único.")
            event_ids.add(event["id"])
            if event.get("kind") not in KINDS or not nonempty(event.get("key")):
                raise ValueError(f"{case['id']}: tipo o clave de referencia inválidos.")
            for key in ("announcementSeconds", "expectedTimeSeconds", "toleranceSeconds"):
                if not finite(event.get(key)) or event[key] < 0:
                    raise ValueError(f"{case['id']}/{event['id']}: {key} inválido.")
            ordinal = event.get("ordinal")
            if ordinal is not None and (isinstance(ordinal, bool) or not isinstance(ordinal, int) or not 1 <= ordinal <= 999):
                raise ValueError(f"{case['id']}: ordinal inválido.")
        references[case["id"]] = case
        expected_count += len(events)
    seen = set()
    totals = {key: 0 for key in STATUSES}
    matched = unexpected = 0
    for case in actual_cases:
        if not isinstance(case, dict) or case.get("id") not in references or case["id"] in seen:
            raise ValueError("La evaluación contiene un caso desconocido o duplicado.")
        seen.add(case["id"])
        if case.get("status") not in STATUSES:
            raise ValueError(f"{case['id']}: estado de evaluación desconocido.")
        totals[case["status"]] += 1
        checks = case.get("checks", [])
        extras = case.get("unexpected", [])
        policies = case.get("policyChecks", [])
        if not isinstance(checks, list) or not isinstance(extras, list) or not isinstance(policies, list):
            raise ValueError(f"{case['id']}: checks y unexpected deben ser listas.")
        if any(not isinstance(item, dict) or not isinstance(item.get("passed"), bool) for item in policies):
            raise ValueError(f"{case['id']}: policyChecks necesita resultados booleanos.")
        if any(not isinstance(item, dict) for item in extras):
            raise ValueError(f"{case['id']}: cada evento adicional debe ser un objeto.")
        event_ids = {event["id"] for event in references[case["id"]]["expectedEvents"]}
        checked = set()
        for check in checks:
            if not isinstance(check, dict) or check.get("eventId") not in event_ids or check["eventId"] in checked:
                raise ValueError(f"{case['id']}: check desconocido o duplicado.")
            checked.add(check["eventId"])
            if not isinstance(check.get("passed"), bool):
                raise ValueError(f"{case['id']}: cada check debe indicar passed.")
            if check.get("errorSeconds") is not None and not finite(check["errorSeconds"]):
                raise ValueError(f"{case['id']}: errorSeconds inválido.")
            if not isinstance(check.get("expected"), dict) or check.get("actual") is not None and not isinstance(check["actual"], dict):
                raise ValueError(f"{case['id']}: check sin referencias o resultado válido.")
            matched += int(check["passed"])
        if case["status"] != "pending" and checked != event_ids:
            raise ValueError(f"{case['id']}: la evaluación ejecutada no comprobó todos los eventos esperados.")
        if case["status"] == "passed" and (extras or any(not check["passed"] for check in checks) or any(not check["passed"] for check in policies)):
            raise ValueError(f"{case['id']}: un caso aprobado contiene diferencias.")
        unexpected += len(extras)
        if not isinstance(case.get("rawSegments", []), list):
            raise ValueError(f"{case['id']}: rawSegments debe ser una lista.")
        for segment in case.get("rawSegments", []):
            if not isinstance(segment, dict):
                raise ValueError(f"{case['id']}: cada segmento original debe ser un objeto.")
            validate_acoustic_evidence(segment, references[case["id"]])
    if seen != set(references):
        raise ValueError("La evaluación debe incluir todos los casos, aunque estén pendientes.")
    summary = evaluation.get("summary")
    if not isinstance(summary, dict):
        raise ValueError("La evaluación necesita summary.")
    observed = {"cases": len(cases), **totals, "expectedEvents": expected_count,
                "matchedEvents": matched, "unexpectedEvents": unexpected}
    for key, count in observed.items():
        if isinstance(summary.get(key), bool) or summary.get(key) != count:
            raise ValueError(f"summary.{key} no coincide con los resultados originales.")
    return manifest, evaluation


def relative_url(target: Path, output: Path) -> str:
    return quote(os.path.relpath(target.resolve(), output.resolve()), safe="/")


def label(value: object) -> str:
    if not isinstance(value, dict):
        return "—"
    result = f"{KINDS.get(value.get('kind'), value.get('kind', 'Sin tipo'))}: {value.get('key', '—')}"
    if value.get("ordinal") is not None:
        result += f" {value['ordinal']}"
    if finite(value.get("timeSeconds")):
        result += f" · {value['timeSeconds']:.3f} s"
    return result


def build_data(manifest: dict, evaluation: dict, manifest_path: Path,
               evaluation_path: Path, output: Path) -> dict:
    results = {case["id"]: case for case in evaluation["cases"]}
    cases = []
    for reference in manifest["cases"]:
        actual = results[reference["id"]]
        item = dict(reference)
        item["result"] = actual
        item["audioUrl"] = relative_url(manifest_path.parent / reference["audioPath"], output)
        item["audioAvailable"] = (manifest_path.parent / reference["audioPath"]).is_file()
        transcript_path = actual.get("rawTranscriptPath")
        item["transcriptUrl"] = relative_url(evaluation_path.parent / transcript_path, output) if nonempty(transcript_path) else None
        cases.append(item)
    return {"schemaVersion": 1, "suiteId": manifest["suiteId"],
            "title": manifest.get("title", "Pruebas de detección de secciones"),
            "summary": evaluation["summary"], "run": evaluation.get("run", {}), "cases": cases}


PAGE = r'''<!doctype html><html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Pruebas de guía · PlayWorship</title><style>
:root{color-scheme:dark;--bg:#111713;--panel:#1b251f;--line:#364c3d;--text:#eef5ef;--muted:#afc0b3;--green:#85d49e;--amber:#efc389;--red:#edb6ad;--blue:#96cbe9}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:14px/1.5 system-ui,sans-serif}main{max-width:1480px;margin:auto;padding:28px 24px 60px}h1{font-size:clamp(25px,3vw,35px);line-height:1.2;margin:0 0 12px}h2{font-size:19px;margin:0}.eyebrow{color:var(--green);text-transform:uppercase;letter-spacing:.09em;font-size:12px}.muted,small{color:var(--muted)}p{max-width:1150px}.stats{display:grid;grid-template-columns:repeat(auto-fit,minmax(145px,1fr));gap:10px;margin:22px 0}.stat{padding:13px 16px;border:1px solid var(--line);border-radius:9px;background:var(--panel)}.stat strong{display:block;font-size:28px}.stat span{font-size:12px;color:var(--muted)}.toolbar{position:sticky;top:0;z-index:3;background:#17251df7;border:1px solid var(--line);border-radius:9px;padding:13px;backdrop-filter:blur(10px)}.filters,.actions{display:flex;gap:12px;align-items:end;flex-wrap:wrap}.actions{align-items:center;margin-top:12px}label{display:grid;gap:4px;font-size:12px;color:var(--muted)}button,select,input,textarea{font:inherit;color:var(--text);background:#24392b;border:1px solid #506d58;border-radius:6px;padding:8px 10px}button{cursor:pointer}button:hover{background:#365740}button:focus-visible,input:focus-visible,select:focus-visible,textarea:focus-visible{outline:2px solid var(--green);outline-offset:2px}button.primary{background:var(--green);color:#14251a;font-weight:650}input[type=checkbox]{accent-color:#82d49a;width:17px;height:17px}.toggle{display:flex;align-items:center;gap:7px}.search{min-width:230px}.notice{color:var(--amber);margin:7px 0 0}.cases{display:grid;gap:18px;margin-top:20px}.case{border:1px solid var(--line);border-radius:10px;background:var(--panel);overflow:hidden}.case-header{padding:14px 16px;background:#203026;display:flex;gap:15px;justify-content:space-between;align-items:start}.case-title{display:flex;gap:10px;align-items:baseline;flex-wrap:wrap}.badge{font-size:12px;border:1px solid #64756b;border-radius:5px;padding:2px 7px;white-space:nowrap}.passed{color:var(--green);border-color:#5a8d66}.failed{color:var(--red);border-color:#a77567}.pending{color:var(--amber);border-color:#9b8358}.case-body{padding:15px 16px}.case-meta{display:flex;gap:14px;flex-wrap:wrap;color:var(--muted);font-size:12px;margin:0 0 10px}.audio{width:min(700px,100%);height:37px;margin:7px 0 12px}.table-scroll{overflow-x:auto}table{border-collapse:collapse;width:100%;text-align:left}th,td{padding:10px 11px;border-bottom:1px solid var(--line);vertical-align:top}th{font-size:12px;color:var(--muted);background:#1b2c22}td{font-size:13px}td small{display:block;margin-top:5px}.event-ref{min-width:175px}.event-actual{min-width:190px}.event-review{min-width:230px;width:300px}.review-fields{display:grid;gap:7px}.review-fields .row{display:flex;gap:7px}.review-fields input[type=text]{min-width:0;flex:1;width:100%}.review-fields input[type=number]{width:110px}.review-fields select{max-width:130px}.review-fields textarea{width:100%;min-height:55px;resize:vertical}.review-label{display:flex;gap:7px;align-items:center;font-size:12px}.review-fields.changed{border-left:2px solid var(--amber);padding-left:8px}.case-note{margin-top:14px;border-top:1px solid var(--line);padding-top:12px}.case-note textarea{width:100%;min-height:60px;resize:vertical}.case-note label{margin-bottom:7px}.extras{margin:12px 0;padding:10px 13px;border-left:3px solid var(--amber);background:#2a2d20}.error{color:var(--red);white-space:pre-wrap}.raw{margin-top:15px}.raw summary{cursor:pointer;color:var(--muted)}pre{font:12px/1.5 ui-monospace,monospace;white-space:pre-wrap;overflow-wrap:anywhere;background:#142018;padding:12px;border-radius:6px}.raw-list{padding:0;list-style:none}.raw-list li{padding:7px 0;border-bottom:1px dotted var(--line)}.raw-list time{display:inline-block;min-width:135px;color:var(--muted);font-size:12px}.empty{text-align:center;padding:35px;color:var(--muted);border:1px dashed var(--line);margin-top:20px;border-radius:8px}a{color:var(--green)}footer{font-size:12px;color:var(--muted);margin-top:27px;overflow-wrap:anywhere}[hidden]{display:none!important}@media(max-width:700px){main{padding:20px 10px 45px}.case-body{padding:12px}.case-header{padding:12px}.stats{grid-template-columns:repeat(2,1fr)}.filters label{flex:1}.search{width:100%;min-width:0}.actions button{flex:1}}
</style></head><body><main>
<p class="eyebrow">PlayWorship · laboratorio de guías</p><h1 id="title"></h1>
<p class="muted">Escucha cada audio, compara la referencia con el resultado real y anota las diferencias. El manifiesto define qué esperamos; Whisper sólo recibe el audio. Estos escenarios comprueban regresiones de las voces y condiciones probadas, no una precisión general sobre todas las canciones.</p>
<div class="stats" id="stats"></div>
<section class="toolbar" aria-label="Filtros y revisión"><div class="filters"><label>Buscar<input type="search" id="search" class="search" placeholder="Caso, sección, indicación…"></label><label>Estado<select id="status"><option value="">Todos</option><option value="failed">Con diferencias</option><option value="pending">Sin ejecutar</option><option value="passed">Aprobados</option></select></label><label>Grupo<select id="group"><option value="">Todos</option></select></label><label class="toggle"><input type="checkbox" id="unreviewed">Sólo sin revisión humana</label><span id="visible" class="muted" role="status"></span></div>
<div class="actions"><button type="button" id="save">Guardar en este navegador</button><button type="button" id="export" class="primary">Exportar observaciones JSON</button><button type="button" id="import">Importar observaciones</button><input type="file" accept="application/json,.json" id="import-file" hidden><span id="save-status" class="muted" role="status">Nada está validado inicialmente.</span></div>
<p class="notice">Las observaciones son insumo para adaptar y probar el código. Este informe no modifica PlayWorship ni enseña reglas automáticamente.</p></section>
<div id="cases" class="cases"></div><div id="empty" class="empty" hidden>No hay casos con estos filtros.</div>
<footer id="run"></footer></main>
<script type="application/json" id="scenario-data">@@DATA@@</script><script>
'use strict';
const DATA=JSON.parse(document.getElementById('scenario-data').textContent);
const $=id=>document.getElementById(id);
const statusLabels={passed:'Aprobado',failed:'Con diferencias',pending:'Sin ejecutar'};
const kinds={section:'Sección',instruction:'Indicación',count:'Conteo'};
const refs=new Map(), state=new Map();
const storageKey='playworship-guide-scenarios:v1:'+location.pathname+':'+DATA.suiteId;
function node(tag,text,cls){const n=document.createElement(tag);if(text!==undefined)n.textContent=text;if(cls)n.className=cls;return n;}
function finite(n){return typeof n==='number'&&Number.isFinite(n);}
function seconds(n){return finite(n)?n.toFixed(3)+' s':'—';}
function meaning(v){return v?(kinds[v.kind]||v.kind||'Sin tipo')+': '+(v.key||'—')+(v.ordinal!==undefined?' '+v.ordinal:'')+(finite(v.timeSeconds)?' · '+seconds(v.timeSeconds):''):'—';}
function appendAcousticEvidence(target,segment){
 if(!segment.acousticAnnouncements?.length)return;
 const evidence=node('div',undefined,'extras');evidence.append(node('strong','Coincidencias acústicas con voces del pack'));
 evidence.append(node('p','El texto original de Whisper permanece intacto. Estos puntos se midieron por coincidencia con grabaciones conocidas de PlayWorship; la correlación no es un porcentaje de exactitud.','muted'));
 for(const announcement of segment.acousticAnnouncements){const line=node('p');line.append(node('time',seconds(announcement.startSeconds)+'–'+seconds(announcement.endSeconds)),node('span',announcement.text+' · idioma '+announcement.language+' · correlación '+announcement.correlation.toFixed(4)));evidence.append(line);}
 evidence.append(node('p','Residuo de reconstrucción: '+segment.acousticResidualRatio.toFixed(4)+' (proporción de energía sin explicar; no es una probabilidad de reconocimiento).','muted'));
 const identities=node('details');identities.append(node('summary','Identidades de las grabaciones comparadas'));for(const announcement of segment.acousticAnnouncements)identities.append(node('pre',announcement.text+' · '+announcement.language+'\n'+announcement.templateIdentity));evidence.append(identities);target.append(evidence);
}
function rowKey(caseId,eventId){return JSON.stringify([caseId,eventId||null]);}
function addReview(target,caseInfo,event){
 const key=rowKey(caseInfo.id,event?event.id:null), item={caseId:caseInfo.id,confirmed:false,note:''};
 if(event)item.eventId=event.id;
 state.set(key,item);const box=node('div',undefined,event?'review-fields':'review-fields case-note');
 let kindInput,keyInput,timeInput,ordinalInput;
 if(event){
  const line=node('div',undefined,'row');kindInput=node('select');
  for(const [value,text]of Object.entries(kinds)){const o=node('option',text);o.value=value;kindInput.append(o);}kindInput.value=event.kind;kindInput.setAttribute('aria-label','Tipo correcto para '+event.id);
  keyInput=node('input');keyInput.type='text';keyInput.value=event.key;keyInput.maxLength=128;keyInput.setAttribute('aria-label','Nombre correcto para '+event.id);line.append(kindInput,keyInput);box.append(line);
  const timingRow=node('div',undefined,'row'),timeLabel=node('label','Tiempo correcto (s)');timeInput=node('input');timeInput.type='number';timeInput.min='0';timeInput.max=String(caseInfo.durationSeconds);timeInput.step='0.001';timeInput.value=String(event.expectedTimeSeconds);timeLabel.append(timeInput);timingRow.append(timeLabel);
  const ordinalLabel=node('label','Número opcional');ordinalInput=node('input');ordinalInput.type='number';ordinalInput.min='1';ordinalInput.max='999';ordinalInput.step='1';ordinalInput.value=event.ordinal===undefined?'':String(event.ordinal);ordinalInput.disabled=event.kind!=='section';ordinalLabel.append(ordinalInput);timingRow.append(ordinalLabel);box.append(timingRow);
 }
 const note=node('textarea');note.maxLength=1000;note.placeholder=event?'Qué se oye, dónde debe quedar o qué falta…':'Observaciones generales, falsas detecciones o indicaciones ausentes…';note.setAttribute('aria-label',event?'Observaciones para '+event.id:'Observaciones del caso '+caseInfo.id);box.append(note);
 const label=node('label',undefined,'review-label');const checkbox=node('input');checkbox.type='checkbox';label.append(checkbox,node('span','Revisado y validado por mí'));box.append(label);
 const defaults=event?{kind:event.kind,key:event.key,timeSeconds:event.expectedTimeSeconds,ordinal:event.ordinal}:null;
 const update=()=>{item.confirmed=checkbox.checked;item.note=note.value;delete item.expectedCorrection;
  if(event){const correction={kind:kindInput.value,key:keyInput.value.trim(),timeSeconds:Number(timeInput.value)};ordinalInput.disabled=correction.kind!=='section';if(!ordinalInput.disabled&&ordinalInput.value)correction.ordinal=Number(ordinalInput.value);
   if(correction.kind!==defaults.kind||correction.key!==defaults.key||correction.timeSeconds!==defaults.timeSeconds||correction.ordinal!==defaults.ordinal){
    if(!correction.key||!timeInput.value||!finite(correction.timeSeconds)||correction.timeSeconds<0||correction.timeSeconds>caseInfo.durationSeconds||(correction.ordinal!==undefined&&(!Number.isInteger(correction.ordinal)||correction.ordinal<1||correction.ordinal>999))){checkbox.checked=false;item.confirmed=false;$('save-status').textContent='Completa un nombre, número y tiempo válidos para validar una corrección.';}
    else item.expectedCorrection=correction;
   }
  }box.classList.toggle('changed',Boolean(item.expectedCorrection||item.note));scheduleSave();filter();};
 for(const input of [note,checkbox,kindInput,keyInput,timeInput,ordinalInput].filter(Boolean))input.addEventListener('input',update);
 refs.set(key,{item,note,checkbox,kindInput,keyInput,timeInput,ordinalInput,defaults,box,caseInfo});target.append(box);
}
let saveTimer=0;
function snapshot(){return {schemaVersion:1,suiteId:DATA.suiteId,run:{providerVersion:DATA.run.providerVersion||null,startedAt:DATA.run.startedAt||null},decisions:[...state.values()].filter(x=>x.confirmed||x.note||x.expectedCorrection)};}
function save(){try{localStorage.setItem(storageKey,JSON.stringify(snapshot()));$('save-status').textContent='Observaciones guardadas sólo en este navegador.';}catch(error){$('save-status').textContent='El navegador no permite guardar aquí; exporta el JSON para conservar las observaciones.';}}
function scheduleSave(){clearTimeout(saveTimer);saveTimer=setTimeout(save,500);}
function restore(data){
 if(!data||data.schemaVersion!==1||data.suiteId!==DATA.suiteId||!Array.isArray(data.decisions)||data.decisions.length>refs.size)throw Error('El archivo no corresponde a este conjunto.');
 const seen=new Set(), decoded=[];
 for(const item of data.decisions){
  if(!item||typeof item.caseId!=='string'||typeof item.confirmed!=='boolean'||typeof item.note!=='string'||item.note.length>1000)throw Error('Hay una observación inválida.');
  const key=rowKey(item.caseId,item.eventId),ref=refs.get(key);if(!ref||seen.has(key))throw Error('Hay un caso o evento desconocido o repetido.');seen.add(key);
  const correction=item.expectedCorrection;
  if(correction&&(!ref.defaults||!kinds[correction.kind]||typeof correction.key!=='string'||!correction.key.trim()||correction.key.length>128||!finite(correction.timeSeconds)||correction.timeSeconds<0||correction.timeSeconds>ref.caseInfo.durationSeconds||(correction.ordinal!==undefined&&(correction.kind!=='section'||!Number.isInteger(correction.ordinal)||correction.ordinal<1||correction.ordinal>999))))throw Error('Hay una corrección inválida.');
  decoded.push([key,item]);
 }
 for(const [key,ref]of refs){Object.assign(ref.item,{confirmed:false,note:''});delete ref.item.expectedCorrection;ref.checkbox.checked=false;ref.note.value='';if(ref.defaults){ref.kindInput.value=ref.defaults.kind;ref.keyInput.value=ref.defaults.key;ref.timeInput.value=String(ref.defaults.timeSeconds);ref.ordinalInput.value=ref.defaults.ordinal===undefined?'':String(ref.defaults.ordinal);ref.ordinalInput.disabled=ref.defaults.kind!=='section';}ref.box.classList.remove('changed');}
 for(const [key,item]of decoded){const ref=refs.get(key);ref.item.confirmed=item.confirmed;ref.item.note=item.note;ref.checkbox.checked=item.confirmed;ref.note.value=item.note;if(item.expectedCorrection){ref.item.expectedCorrection={...item.expectedCorrection};ref.kindInput.value=item.expectedCorrection.kind;ref.keyInput.value=item.expectedCorrection.key;ref.timeInput.value=String(item.expectedCorrection.timeSeconds);ref.ordinalInput.value=item.expectedCorrection.ordinal===undefined?'':String(item.expectedCorrection.ordinal);ref.ordinalInput.disabled=item.expectedCorrection.kind!=='section';}ref.box.classList.toggle('changed',Boolean(item.note||item.expectedCorrection));}
 filter();
}
function filter(){let shown=0;const term=$('search').value.trim().toLocaleLowerCase(),status=$('status').value,group=$('group').value;
 for(const c of DATA.cases){const card=document.getElementById('case-'+c.id),reviews=[...state.values()].filter(v=>v.caseId===c.id),events=reviews.filter(v=>v.eventId),reviewed=reviews.some(v=>!v.eventId&&v.confirmed)||(events.length>0&&events.every(v=>v.confirmed)),text=[c.id,c.title,c.description,c.group,...c.expectedEvents.map(e=>e.key)].join(' ').toLocaleLowerCase();
  const visible=(!status||c.result.status===status)&&(!group||c.group===group)&&(!term||text.includes(term))&&(!$('unreviewed').checked||!reviewed);card.hidden=!visible;if(visible)shown++;
 }$('visible').textContent=shown+' de '+DATA.cases.length+' casos';$('empty').hidden=shown!==0;
}
$('title').textContent=DATA.title;
for(const [key,title]of [['cases','Casos'],['passed','Aprobados'],['failed','Con diferencias'],['pending','Sin ejecutar'],['matchedEvents','Eventos que coinciden'],['unexpectedEvents','Eventos adicionales']]){const n=node('div',undefined,'stat');n.append(node('strong',String(DATA.summary[key]??'—')),node('span',title));$('stats').append(n);}
for(const group of [...new Set(DATA.cases.map(c=>c.group))].sort()){const o=node('option',group);o.value=group;$('group').append(o);}
for(const c of DATA.cases){
 const card=node('article',undefined,'case');card.id='case-'+c.id;const head=node('header',undefined,'case-header'),heading=node('div',undefined,'case-title');heading.append(node('small',c.id),node('h2',c.title));head.append(heading,node('span',statusLabels[c.result.status],'badge '+c.result.status));card.append(head);
 const body=node('div',undefined,'case-body');const meta=node('p',undefined,'case-meta');for(const value of [c.group,c.bpm+' BPM',c.meter,'Origen '+seconds(c.originSeconds),'Duración '+seconds(c.durationSeconds)])meta.append(node('span',value));body.append(meta);if(c.description)body.append(node('p',c.description));if(c.tags?.length)body.append(node('p',c.tags.join(' · '),'muted'));
 const audio=node('audio',undefined,'audio');audio.controls=true;audio.preload='metadata';audio.src=c.audioUrl;audio.setAttribute('aria-label','Audio de '+c.title);audio.addEventListener('play',()=>{for(const other of document.querySelectorAll('audio'))if(other!==audio)other.pause();});body.append(audio);
 const mediaStatus=node('p',undefined,'muted');const audioLink=node('a','Abrir archivo de audio');audioLink.href=c.audioUrl;mediaStatus.append(audioLink);const mediaError=node('span');mediaStatus.append(mediaError);body.append(mediaStatus);audio.addEventListener('error',()=>{mediaError.textContent=' · El navegador no pudo reproducir el archivo. Puedes abrir el audio directamente; los resultados y las observaciones permanecen disponibles.';mediaError.className='error';});if(!c.audioAvailable)mediaError.textContent=' · Archivo no encontrado en la ruta del manifiesto.';
 if(c.result.error)body.append(node('p',c.result.error,'error'));
 if(c.result.policyChecks?.length){const policies=node('div',undefined,'extras');policies.append(node('strong','Condiciones del escenario'));for(const policy of c.result.policyChecks){const line=node('p');line.append(node('span',policy.passed?'Cumple':'Revisar','badge '+(policy.passed?'passed':'failed')),node('span',' '+(policy.reason||policy.key||'Condición')));if(policy.expected!==undefined||policy.actual!==undefined)line.append(node('small','Esperado: '+JSON.stringify(policy.expected)+' · Real: '+JSON.stringify(policy.actual)));policies.append(line);}body.append(policies);}
 const scroll=node('div',undefined,'table-scroll'),table=node('table'),thead=node('thead'),header=node('tr');for(const text of ['Referencia esperada','Resultado real','Comparación','Revisión humana'])header.append(node('th',text));thead.append(header);table.append(thead);const tbody=node('tbody');
 const checkMap=new Map((c.result.checks||[]).map(x=>[x.eventId,x]));
 for(const event of c.expectedEvents){const check=checkMap.get(event.id),tr=node('tr'),expected=node('td',undefined,'event-ref');expected.append(node('strong',meaning({kind:event.kind,key:event.key,ordinal:event.ordinal,timeSeconds:event.expectedTimeSeconds})),node('small','Anuncio: '+seconds(event.announcementSeconds)+' · tolerancia ±'+(event.toleranceSeconds*1000).toFixed(1)+' ms'),node('small',event.id));tr.append(expected);
  const actual=node('td',undefined,'event-actual');actual.append(node('span',check?meaning(check.actual):'Sin resultado registrado'));tr.append(actual);const verdict=node('td');verdict.append(node('span',!check?'Sin ejecutar':check.passed?'Coincide':'Revisar','badge '+(!check?'pending':check.passed?'passed':'failed')));if(finite(check?.errorSeconds))verdict.append(node('small','Diferencia: '+(check.errorSeconds*1000).toFixed(1)+' ms'));if(check?.reason)verdict.append(node('small',check.reason));tr.append(verdict);const review=node('td',undefined,'event-review');addReview(review,c,event);tr.append(review);tbody.append(tr);
 }
 if(!c.expectedEvents.length){const tr=node('tr'),td=node('td','No se esperan eventos. Se comprueba que el resultado permanezca vacío.');td.colSpan=4;tr.append(td);tbody.append(tr);}table.append(tbody);scroll.append(table);body.append(scroll);
 if(c.result.unexpected?.length){const extras=node('div',undefined,'extras');extras.append(node('strong','Eventos adicionales: '+c.result.unexpected.length));for(const extra of c.result.unexpected)extras.append(node('p',meaning(extra.actual||extra)));body.append(extras);}
 const raw=node('details',undefined,'raw');raw.append(node('summary','Texto y tiempos originales de Whisper'));if(c.result.rawSegments?.length){const list=node('ul',undefined,'raw-list');for(const seg of c.result.rawSegments){const li=node('li');li.append(node('time',seconds(seg.startSeconds)+'–'+seconds(seg.endSeconds)),node('span',seg.text||''));appendAcousticEvidence(li,seg);list.append(li);}raw.append(list);}else raw.append(node('p',c.result.status==='pending'?'La inferencia todavía no se ejecutó.':'No hay segmentos en el resultado original.','muted'));if(c.transcriptUrl){const link=node('a','Abrir JSON original');link.href=c.transcriptUrl;raw.append(link);}body.append(raw);
 const general=node('div',undefined,'case-note');general.append(node('strong','Revisión general del caso'));addReview(general,c,null);body.append(general);card.append(body);$('cases').append(card);
}
for(const id of ['search','status','group','unreviewed'])$(id).addEventListener('input',filter);
$('save').addEventListener('click',save);
$('export').addEventListener('click',()=>{clearTimeout(saveTimer);save();const blob=new Blob([JSON.stringify(snapshot(),null,2)+'\n'],{type:'application/json'}),url=URL.createObjectURL(blob),a=node('a');a.href=url;a.download='guide-scenario-feedback-'+DATA.suiteId.slice(0,16)+'.json';document.body.append(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(url),1000);});
$('import').addEventListener('click',()=>$('import-file').click());$('import-file').addEventListener('change',async event=>{const file=event.target.files[0];if(!file)return;try{if(file.size>1024*1024)throw Error('El archivo supera 1 MiB.');restore(JSON.parse(await file.text()));save();}catch(error){$('save-status').textContent='No se importó: '+error.message;}event.target.value='';});
try{const old=localStorage.getItem(storageKey);if(old){restore(JSON.parse(old));$('save-status').textContent='Observaciones recuperadas de este navegador.';}}catch(error){$('save-status').textContent='No se recuperaron observaciones locales; puedes exportar un JSON al terminar.';}
$('run').textContent='Conjunto '+DATA.suiteId+' · Proveedor '+(DATA.run.providerVersion||'sin ejecutar')+' · Modelo '+(DATA.run.modelFingerprint||'sin ejecutar')+' · Ejecución '+(DATA.run.startedAt||'sin fecha')+' · Caché '+(DATA.run.cacheMode||'sin datos')+'. '+(DATA.run.scope||'Regresión de escenarios controlados.');filter();
</script></body></html>'''


def render_html(data: dict) -> str:
    boot = json.dumps(data, ensure_ascii=False).replace("<", "\\u003c").replace(">", "\\u003e").replace("&", "\\u0026").replace("\u2028", "\\u2028").replace("\u2029", "\\u2029")
    return PAGE.replace("@@DATA@@", boot)


def md(value: object) -> str:
    return html.escape(str(value), quote=False).replace("|", "\\|").replace("\n", " ").replace("\r", " ")


def md_literal(value: object) -> str:
    text = md(value).replace("\\", "\\\\")
    for character in "`*_[]()!#":
        text = text.replace(character, "\\" + character)
    # md already escaped pipes for a table; avoid doubling that escape.
    return text.replace("\\\\|", "\\|")


def render_markdown(data: dict) -> str:
    summary = data["summary"]
    lines = [f"# {md(data['title'])}", "", "Informe de una ejecución del conjunto de pruebas de guías de PlayWorship.", "",
             f"Conjunto: `{data['suiteId']}`.", "",
             f"Casos: **{summary['cases']}** · aprobados: **{summary['passed']}** · con diferencias: **{summary['failed']}** · sin ejecutar: **{summary['pending']}**.", "",
             f"Eventos esperados: **{summary['expectedEvents']}** · coincidentes: **{summary['matchedEvents']}** · adicionales: **{summary['unexpectedEvents']}**.", "",
             "Las referencias se comparan después de la inferencia; no se entregan a Whisper. Los resultados corresponden a estas voces y condiciones, no a una precisión general sobre canciones nuevas.", "",
             "[Abrir informe interactivo con audio y observaciones](guide-scenarios-report.html). Las observaciones exportadas son insumo de revisión del código; no se aplican automáticamente a la app.", ""]
    run = data["run"]
    lines += ["## Ejecución", "", f"- Proveedor: {md(run.get('providerVersion', 'Sin ejecutar'))}",
              f"- Modelo: {md(run.get('modelFingerprint', 'Sin ejecutar'))}", f"- Inicio: {md(run.get('startedAt', 'Sin fecha'))}",
              f"- Caché: {md(run.get('cacheMode', 'Sin datos'))}", f"- Alcance: {md(run.get('scope', 'Escenarios controlados'))}", ""]
    for case in data["cases"]:
        result = case["result"]
        lines += [f"## {md(case['id'])} — {md(case['title'])}", "", f"**{STATUSES[result['status']]}** · {md(case['group'])} · {case['bpm']:g} BPM · {md(case['meter'])} · origen {case['originSeconds']:.3f} s · duración {case['durationSeconds']:.3f} s.", ""]
        if case.get("description"):
            lines += [md(case["description"]), ""]
        lines += [f"[Audio original]({case['audioUrl']})" + (f" · [JSON de inferencia]({case['transcriptUrl']})" if case["transcriptUrl"] else ""), ""]
        if result.get("error"):
            lines += ["Error: " + md(result["error"]), ""]
        if result.get("policyChecks"):
            lines += ["Condiciones del escenario:", ""]
            for policy in result["policyChecks"]:
                verdict = "Cumple" if policy["passed"] else "Revisar"
                lines.append(f"- **{verdict}**: {md(policy.get('reason') or policy.get('key', 'Condición'))}. Esperado: {md(json.dumps(policy.get('expected'), ensure_ascii=False))}; real: {md(json.dumps(policy.get('actual'), ensure_ascii=False))}.")
            lines.append("")
        lines += ["| Referencia | Resultado real | Diferencia | Estado |", "| --- | --- | --- | --- |"]
        checks = {item["eventId"]: item for item in result.get("checks", [])}
        for event in case["expectedEvents"]:
            check = checks.get(event["id"])
            expected = {"kind": event["kind"], "key": event["key"], "timeSeconds": event["expectedTimeSeconds"]}
            if "ordinal" in event:
                expected["ordinal"] = event["ordinal"]
            delta = f"{check['errorSeconds'] * 1000:+.1f} ms" if check and finite(check.get("errorSeconds")) else "—"
            lines.append(f"| {md(label(expected))} (±{event['toleranceSeconds'] * 1000:g} ms) | {md(label(check.get('actual')) if check else 'Sin resultado registrado')} | {delta} | {'Coincide' if check and check['passed'] else 'Sin ejecutar' if not check else 'Revisar'} |")
        if not case["expectedEvents"]:
            lines.append("| Ningún evento | Se comprueba que no haya detecciones | — | " + STATUSES[result["status"]] + " |")
        lines += [""]
        if result.get("unexpected"):
            lines += ["Eventos adicionales:", ""] + [f"- {md(label(item.get('actual', item)))}" for item in result["unexpected"]] + [""]
        if result.get("rawSegments"):
            lines += ["Transcripción literal:", ""]
            for segment in result["rawSegments"]:
                start, end = segment.get("startSeconds"), segment.get("endSeconds")
                interval = f"{start:.3f}–{end:.3f} s" if finite(start) and finite(end) else "Sin tiempos"
                lines.append(f"- {interval}: {md(segment.get('text', ''))}")
                if segment.get("acousticAnnouncements"):
                    lines += ["", "Coincidencias acústicas con voces conocidas del pack. El texto ASR anterior permanece intacto; la correlación no es un porcentaje de exactitud.", "",
                              "| Anuncio medido | Idioma de la voz | Inicio (s) | Final (s) | Correlación |",
                              "| --- | --- | ---: | ---: | ---: |"]
                    for announcement in segment["acousticAnnouncements"]:
                        lines.append(f"| {md_literal(announcement['text'])} | {md_literal(announcement['language'])} | {announcement['startSeconds']:.6f} | {announcement['endSeconds']:.6f} | {announcement['correlation']:.4f} |")
                    lines += ["", f"Residuo de reconstrucción: **{segment['acousticResidualRatio']:.4f}** (proporción de energía sin explicar; no es una probabilidad de reconocimiento).", "",
                              "<details><summary>Identidades de las grabaciones comparadas</summary>", ""]
                    for announcement in segment["acousticAnnouncements"]:
                        lines.append(f"- {md_literal(announcement['text'])} · {md_literal(announcement['language'])}: {md_literal(announcement['templateIdentity'])}")
                    lines += ["", "</details>", ""]
            lines.append("")
    return "\n".join(lines) + "\n"


def csv_safe(value: object) -> object:
    if isinstance(value, str) and value.startswith(("=", "+", "-", "@", "\t", "\r", "\n")):
        return "'" + value
    return value


def render_csv(data: dict) -> str:
    output = io.StringIO(newline="")
    writer = csv.writer(output)
    writer.writerow(["suiteId", "caseId", "title", "group", "caseStatus", "eventId", "expectedKind", "expectedKey", "expectedOrdinal", "announcementSeconds", "expectedTimeSeconds", "toleranceSeconds", "actualKind", "actualKey", "actualOrdinal", "actualTimeSeconds", "errorSeconds", "passed", "reason", "rawTranscriptPath"])
    for case in data["cases"]:
        checks = {item["eventId"]: item for item in case["result"].get("checks", [])}
        events = case["expectedEvents"] or [None]
        for event in events:
            check = checks.get(event["id"]) if event else None
            actual = (check or {}).get("actual") or {}
            row = [data["suiteId"], case["id"], case["title"], case["group"], case["result"]["status"],
                   event["id"] if event else "", event["kind"] if event else "", event["key"] if event else "", event.get("ordinal", "") if event else "",
                   event["announcementSeconds"] if event else "", event["expectedTimeSeconds"] if event else "", event["toleranceSeconds"] if event else "",
                   actual.get("kind", ""), actual.get("key", ""), actual.get("ordinal", ""), actual.get("timeSeconds", ""),
                   (check or {}).get("errorSeconds", ""), str(check["passed"]).lower() if check else "", (check or {}).get("reason", case["result"].get("error", "")), case["result"].get("rawTranscriptPath", "")]
            writer.writerow([csv_safe(value) for value in row])
        for index, extra in enumerate(case["result"].get("unexpected", []), 1):
            actual = extra.get("actual", extra)
            writer.writerow([csv_safe(value) for value in [data["suiteId"], case["id"], case["title"], case["group"], case["result"]["status"], f"unexpected-{index}", "", "", "", "", "", "", actual.get("kind", ""), actual.get("key", ""), actual.get("ordinal", ""), actual.get("timeSeconds", ""), "", "false", "Evento adicional", case["result"].get("rawTranscriptPath", "")]])
    return output.getvalue()


def atomic_write(path: Path, text: str) -> None:
    if path.is_symlink():
        raise ValueError(f"No se reemplaza un enlace simbólico: {path}")
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", newline="", dir=path.parent, prefix="." + path.name, delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(text)
        os.replace(temporary, path)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--evaluation", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    try:
        manifest_path, evaluation_path, output = args.manifest.resolve(), args.evaluation.resolve(), args.output_dir.resolve()
        manifest, evaluation = read_inputs(manifest_path, evaluation_path)
        output.mkdir(parents=True, exist_ok=True)
        data = build_data(manifest, evaluation, manifest_path, evaluation_path, output)
        files = {"guide-scenarios-report.html": render_html(data), "Informe.md": render_markdown(data), "guide-scenarios-checks.csv": render_csv(data)}
        if {manifest_path, evaluation_path} & {output / name for name in files}:
            raise ValueError("Las salidas no pueden reemplazar el manifiesto o la evaluación.")
        for name, content in files.items():
            atomic_write(output / name, content)
        print(json.dumps({"html": str(output / "guide-scenarios-report.html"), "markdown": str(output / "Informe.md"), "csv": str(output / "guide-scenarios-checks.csv"), "cases": len(data["cases"]), "summary": data["summary"]}, ensure_ascii=False))
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f"No se generó el informe: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
