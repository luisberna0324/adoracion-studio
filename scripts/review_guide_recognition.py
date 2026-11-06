#!/usr/bin/env python3
"""Build an offline-capable, explicit review UI for a real guide evaluation.

Python 3.12+, standard library only. The generated page talks exclusively to
same-origin loopback APIs. This generator never reads/writes application data.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import tempfile

from report_guide_recognition import read_evaluation


def catalogue_entries(value: object, group: str) -> list[dict[str, str]]:
    if not isinstance(value, list) or not value:
        raise ValueError(f"El catálogo necesita una lista {group} no vacía.")
    entries = []
    seen: set[str] = set()
    for item in value:
        if not isinstance(item, dict) or not isinstance(item.get("key"), str) or not item["key"].strip():
            raise ValueError(f"Entrada inválida en el catálogo {group}.")
        key = item["key"]
        if key in seen:
            raise ValueError(f"Clave duplicada en {group}: {key}")
        seen.add(key)
        label = item.get("label", key)
        if not isinstance(label, str) or not label.strip():
            raise ValueError(f"Etiqueta inválida en {group}: {key}")
        entries.append({"key": key, "label": label})
    return entries


def boot_data(evaluation_path: Path, manifest_path: Path, catalogue_path: Path) -> dict:
    evaluation = read_evaluation(evaluation_path)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    catalogue = json.loads(catalogue_path.read_text(encoding="utf-8"))
    dataset = manifest.get("masterSha256")
    if not isinstance(dataset, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", dataset):
        raise ValueError("El manifest necesita masterSha256 para vincular las decisiones al audio real.")
    clips = manifest.get("clips")
    if not isinstance(clips, list):
        raise ValueError("El manifest necesita clips.")
    manifest_ids = [clip.get("id") for clip in clips if isinstance(clip, dict)]
    if len(manifest_ids) != len(clips) or len(set(manifest_ids)) != len(clips) or set(manifest_ids) != {
        case["id"] for case in evaluation["cases"]
    }:
        raise ValueError("Manifest y evaluación no contienen exactamente los mismos IDs de clips.")
    if not isinstance(catalogue, dict):
        raise ValueError("El catálogo debe ser un objeto JSON.")
    catalogue = {group: catalogue_entries(catalogue.get(group), group)
                 for group in ("sections", "instructions", "counts")}
    if {entry["key"] for entry in catalogue["counts"]} != {str(value) for value in range(1, 8)}:
        raise ValueError("El catálogo de conteos debe contener las claves canónicas del 1 al 7.")
    cases = []
    for index, case in enumerate(evaluation["cases"], 1):
        cases.append({key: case.get(key) for key in (
            "id", "language", "expectedLabel", "expected", "status", "rawCatalogCorrect",
            "interpretedCorrect", "startSeconds", "endSeconds", "actual", "sourceZipPath")} | {"index": index})
    return {"schemaVersion": 1, "datasetId": dataset.lower(), "catalogue": catalogue,
            "cases": cases, "summary": evaluation["summary"],
            "fileAudio": Path(manifest.get("outputPath", "all-guide-clips.wav")).name}


PAGE = r'''<!doctype html>
<html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Revisar voces de Guía · PlayWorship</title>
<style>
:root{color-scheme:dark;--bg:#101713;--panel:#1a241d;--panel2:#202e24;--line:#344b3b;--text:#f0f5ef;--muted:#aec0af;--green:#83d49b;--amber:#efc182;--blue:#8cc4e4;--red:#f0b6ad}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:14px/1.5 system-ui,sans-serif}main{max-width:1460px;margin:auto;padding:28px 24px 60px}h1{font-size:clamp(24px,3vw,33px);line-height:1.2;margin:0 0 10px}p{margin:8px 0;max-width:1100px}.muted,small{color:var(--muted)}small{font-size:12px}.eyebrow{font-size:12px;letter-spacing:.1em;text-transform:uppercase;color:var(--green);margin:0 0 8px}.stats{display:flex;flex-wrap:wrap;gap:10px;margin:20px 0}.stat{min-width:150px;flex:1;background:var(--panel);border:1px solid var(--line);border-radius:9px;padding:12px 15px}.stat strong{font-size:27px;display:block;line-height:1.2}.stat span{font-size:12px;color:var(--muted)}.controls{position:sticky;top:0;z-index:3;background:#132018f5;backdrop-filter:blur(10px);border:1px solid var(--line);border-radius:10px;padding:14px}.top-controls,.actions,.filters{display:flex;gap:10px;align-items:center;flex-wrap:wrap}.top-controls{justify-content:space-between}.actions{margin:12px 0 8px}audio{height:36px;max-width:100%;width:390px}#play-status{font-size:12px;color:var(--muted)}button,select,input,textarea{font:inherit;border:1px solid #49634f;border-radius:6px;color:var(--text);background:#1d3023;padding:8px 10px}button{cursor:pointer;white-space:nowrap}button:hover:not(:disabled){background:#31533a;border-color:var(--green)}button:disabled{opacity:.5;cursor:default}button.primary{background:#75c58c;color:#102417;border-color:#75c58c;font-weight:650}button.primary:hover:not(:disabled){background:#98dfa8}.secondary{background:transparent}button:focus-visible,select:focus-visible,input:focus-visible,textarea:focus-visible{outline:2px solid var(--green);outline-offset:2px}input[type=checkbox]{accent-color:#81d39a;width:17px;height:17px;margin:0;vertical-align:middle}input[type=search]{width:230px;max-width:100%}.filters{margin-top:12px;align-items:end}.filters label{display:grid;gap:4px;font-size:12px;color:var(--muted)}.filters label.toggle{display:flex;gap:7px;align-items:center;padding:9px 0}.filters select{min-width:155px}.save-state{font-size:12px;color:var(--muted);margin-top:8px;min-height:18px}.notice{padding:10px 12px;border:1px solid #7b6944;background:#2b281e;border-radius:7px;margin-top:10px}.notice.success{border-color:#4c8759;background:#1e3424}.notice.error{border-color:#a06f65;background:#382721;color:var(--red)}.notice button{margin:4px 4px 0 0;font-size:12px}.case-list{display:grid;gap:12px;margin-top:18px}.case{border:1px solid var(--line);background:var(--panel);border-radius:10px;overflow:hidden}.case.confirmed{border-color:#5c9568}.case.has-error{border-color:#b77b6c}.case-head{display:flex;gap:12px;align-items:center;justify-content:space-between;padding:10px 14px;border-bottom:1px solid var(--line);background:#1d2b21}.case-heading{display:flex;gap:10px;align-items:baseline;flex-wrap:wrap}.case-heading strong{font-size:16px}.case-number{font-size:11px;color:var(--muted)}.pill{display:inline-block;font-size:11px;border:1px solid #536b59;border-radius:5px;padding:2px 6px;color:var(--muted)}.pill.priority{color:var(--amber);border-color:#876b41}.pill.confirmed{color:#b4edbe;border-color:#5c9568}.case-grid{display:grid;grid-template-columns:minmax(180px,.85fr) minmax(210px,1fr) minmax(330px,1.4fr);gap:18px;padding:14px}.overline{display:block;font-size:11px;letter-spacing:.04em;text-transform:uppercase;color:var(--muted);margin-bottom:6px}.literal{margin:0;white-space:pre-wrap;overflow-wrap:anywhere;font-size:15px}.meaning{margin-top:10px;color:var(--blue);font-size:13px}.reference{color:var(--muted);font-size:13px}.reference strong{color:var(--text)}.reference small{display:block;margin-top:7px;overflow-wrap:anywhere}.edit-fields{display:grid;gap:8px}.meaning-select{width:100%;min-width:0}.ordinal-row,.request-row{display:grid;gap:4px;font-size:12px;color:var(--muted)}.ordinal-row input{width:100px}.request-row input{width:100%}.confirm-row{display:flex;align-items:center;gap:8px;padding:8px 0 0;font-weight:600}.row-status{font-size:12px;color:var(--muted)}.row-error{font-size:12px;color:var(--red);white-space:pre-wrap}.note{font-size:12px;color:var(--muted)}.note summary{cursor:pointer}.note textarea{width:100%;resize:vertical;min-height:58px;margin-top:7px}.use-reference{font-size:12px;padding:5px 8px;margin-top:10px}.listen{font-size:12px;padding:6px 10px}.listen[aria-pressed=true]{border-color:var(--green);background:#31533a}.empty{text-align:center;padding:35px;border:1px dashed var(--line);border-radius:10px;color:var(--muted);margin-top:18px}footer{font-size:12px;color:var(--muted);margin-top:25px;overflow-wrap:anywhere}.feedback-help{font-size:12px;color:var(--muted);flex:1;min-width:240px}.sr-only{position:absolute;width:1px;height:1px;padding:0;margin:-1px;overflow:hidden;clip:rect(0,0,0,0);white-space:nowrap;border:0}[hidden]{display:none!important}@media(max-width:1050px){.case-grid{grid-template-columns:1fr 1fr}.editor{grid-column:1/-1;border-top:1px solid var(--line);padding-top:12px}.edit-fields{grid-template-columns:minmax(240px,1fr) auto}.confirm-row,.row-status,.row-error,.note{grid-column:1/-1}}@media(max-width:680px){main{padding:18px 10px 40px}.controls{padding:10px}.case-grid{grid-template-columns:1fr;gap:14px}.editor{grid-column:auto}.edit-fields{grid-template-columns:1fr}.confirm-row,.row-status,.row-error,.note{grid-column:auto}.case-head{align-items:start}.case-heading{gap:6px}.stats{gap:7px}.stat{min-width:130px}.filters input[type=search]{width:100%}.filters label:first-child{width:100%}.actions button{flex:1}.actions button.primary{flex-basis:100%}}
</style></head><body><main>
<p class="eyebrow">PlayWorship · respuestas para desarrollo</p><h1>Revisa las voces de Guía y comparte tus respuestas</h1>
<p class="muted">Escucha el audio y valida el significado que corresponde. Tus respuestas se revisarán para adaptar en código el catálogo, los alias y las reglas de reconocimiento. Las etiquetas del archivo son referencias de evaluación; Whisper no las recibió. Esta prueba usa clips aislados con silencio y no mide precisión sobre canciones con música mezclada.</p>
<div class="stats"><div class="stat"><strong id="total-count">—</strong><span>Clips disponibles</span></div><div class="stat"><strong id="priority-count">—</strong><span>Casos por corregir en la evaluación</span></div><div class="stat"><strong id="confirmed-count">0</strong><span>Respuestas validadas para revisión</span></div><div class="stat"><strong id="request-count">0</strong><span>Solicitudes de catálogo validadas</span></div><div class="stat"><strong id="pending-count">—</strong><span>Filas sin validar</span></div></div>
<section class="controls" aria-label="Reproducción y progreso"><div class="top-controls"><audio id="audio" controls preload="metadata"></audio><span id="play-status" role="status">Escucha un clip desde su fila.</span></div>
<div class="actions"><button type="button" id="save" class="primary">Guardar respuestas</button><button type="button" id="export" class="secondary">Exportar respuestas JSON</button><button type="button" id="import" class="secondary">Importar respuestas JSON</button><input id="import-file" type="file" accept="application/json,.json" hidden></div>
<p class="feedback-help">Elige el significado, añade una nota si hace falta y marca «Validada para revisión». «Falta en catálogo» registra una propuesta para revisar en el código.</p>
<div id="save-state" class="save-state" role="status">Ninguna respuesta está validada inicialmente.</div><div id="message" hidden role="status"></div><div id="restore" class="notice" hidden><span>Hay otras respuestas guardadas en este navegador.</span> <button type="button" id="restore-local">Recuperar respuestas locales</button><button type="button" id="dismiss-local">Conservar las del servidor</button></div>
<div class="filters"><label>Buscar<input id="search" type="search" placeholder="Referencia, voz o interpretación…"></label><label>Idioma<select id="language"><option value="">Todos</option></select></label><label>Estado de evaluación<select id="status"><option value="priority">Por corregir</option><option value="">Todos los estados</option></select></label><label class="toggle"><input id="only-pending" type="checkbox" checked> Sólo sin validar</label><button type="button" id="show-all" class="secondary">Ver todos</button><span id="visible-count" class="muted" role="status"></span></div></section>
<div id="cases" class="case-list"></div><div id="empty" class="empty" hidden>No hay clips con estos filtros. Puedes cambiar el estado o ver todos.</div>
<footer><span id="catalogue-info"></span> · Dataset <span id="dataset"></span>. El texto original de Whisper y la identidad del clip no se editan desde esta vista.</footer>
</main><script type="application/json" id="review-data">@@BOOT@@</script><script>
'use strict';
const DATA=JSON.parse(document.getElementById('review-data').textContent);
const $=id=>document.getElementById(id);
const apiAllowed=['http:','https:'].includes(location.protocol)&&['127.0.0.1','localhost','[::1]','::1'].includes(location.hostname);
const localKey='playworship-guide-feedback:v1:'+location.origin+':'+DATA.datasetId;
const groupForKind={section:'sections',instruction:'instructions',count:'counts'};
const kindNames={section:'Sección',instruction:'Indicación',count:'Conteo','catalog-request':'Falta en catálogo'};
const statusNames={recognized:'Reconocido',mismatch:'No coincide',review:'Propuesta por revisar',missing:'Sin transcripción asignada',split:'Varios fragmentos','reference-not-in-catalog':'Referencia fuera del catálogo'};
const state=new Map(DATA.cases.map(c=>[c.id,{caseId:c.id,confirmed:false,kind:null,key:''}]));
const refs=new Map();let busy=false,autosaveTimer=0,localCandidate=null,revision=0;
function el(tag,text,cls){const n=document.createElement(tag);if(text!==undefined)n.textContent=text;if(cls)n.className=cls;return n;}
function message(text,type=''){const n=$('message');n.textContent=text;n.className='notice '+type;n.hidden=!text;}
function normalize(value){return String(value||'').normalize('NFD').replace(/\p{Diacritic}/gu,'').toLowerCase().replace(/[^\p{Letter}\p{Number}]+/gu,' ').trim();}
function meaning(label){if(!label||label.kind==='unknown')return 'Sin reconocer';const group=DATA.catalogue[groupForKind[label.kind]]||[];const match=group.find(e=>normalize(e.key)===normalize(label.key));return (kindNames[label.kind]||label.kind)+': '+(match?match.label:label.key||'—')+(label.ordinal!==undefined?' '+label.ordinal:'');}
function lookup(kind,key){return (DATA.catalogue[groupForKind[kind]]||[]).find(e=>e.key===key);}
function optionValue(kind,key){return JSON.stringify([kind,key]);}
function readChoice(value){return value?JSON.parse(value):[null,''];}
function choiceError(d){if(!d.kind)return 'Elige un significado antes de validar la respuesta.';if(d.kind==='catalog-request')return !String(d.name||'').trim()?'Escribe qué falta en el catálogo.':String(d.name).length>128?'La solicitud admite hasta 128 caracteres.':'';if(!lookup(d.kind,d.key))return 'Elige una opción válida del catálogo.';if(d.ordinal!==undefined&&(d.kind!=='section'||!Number.isInteger(d.ordinal)||d.ordinal<1||d.ordinal>999))return 'El ordinal debe ser un entero entre 1 y 999.';return '';}
function payload(){return {schemaVersion:1,datasetId:DATA.datasetId,decisions:Array.from(state.values()).filter(d=>d.kind).map(d=>{const out={caseId:d.caseId,confirmed:d.confirmed,kind:d.kind,key:d.key};if(d.kind==='section'&&d.ordinal!==undefined)out.ordinal=d.ordinal;if(d.kind==='catalog-request')out.name=String(d.name||'').trim();if(d.note)out.note=d.note;return out;})};}
function validateReview(input){const body=input&&input.review?input.review:input;if(!body||body.schemaVersion!==1||body.datasetId!==DATA.datasetId||!Array.isArray(body.decisions)||body.decisions.length>DATA.cases.length)throw new Error('El borrador no pertenece a este dataset o usa un formato incompatible.');const seen=new Set();const clean=[];for(const entry of body.decisions){if(!entry||!state.has(entry.caseId)||seen.has(entry.caseId)||typeof entry.confirmed!=='boolean'||!['section','instruction','count','catalog-request'].includes(entry.kind)||typeof entry.key!=='string')throw new Error('El borrador contiene IDs, campos o decisiones inválidas.');seen.add(entry.caseId);const d={caseId:entry.caseId,confirmed:entry.confirmed,kind:entry.kind,key:entry.key};if(entry.ordinal!==undefined)d.ordinal=entry.ordinal;if(entry.name!==undefined){if(typeof entry.name!=='string')throw new Error('La solicitud de catálogo debe ser texto.');d.name=entry.name;}if(entry.note!==undefined){if(typeof entry.note!=='string'||entry.note.length>1000)throw new Error('Una nota supera el límite de 1000 caracteres.');d.note=entry.note;}if(d.kind==='catalog-request'&&d.key!=='')throw new Error('Una solicitud de catálogo no puede usar una clave existente.');if(d.kind!=='catalog-request'&&d.name!==undefined)throw new Error('El nombre libre sólo corresponde a una solicitud de catálogo.');const error=choiceError(d);if(error&&(d.confirmed||d.kind!=='catalog-request'||d.name))throw new Error(error);clean.push(d);}return {schemaVersion:1,datasetId:DATA.datasetId,decisions:clean};}
function restoreReview(body){const clean=validateReview(body);for(const c of DATA.cases)state.set(c.id,{caseId:c.id,confirmed:false,kind:null,key:''});for(const d of clean.decisions)state.set(d.caseId,d);for(const c of DATA.cases)updateRow(c.id);updateStats();filter();}
function localSave(){try{localStorage.setItem(localKey,JSON.stringify({updatedAt:new Date().toISOString(),review:payload()}));return true;}catch(error){return false;}}
function changed(id,semantic=true){revision++;if(semantic)state.get(id).confirmed=false;const r=refs.get(id);r.error.textContent='';r.card.classList.remove('has-error');updateRow(id);updateStats();filter();clearTimeout(autosaveTimer);autosaveTimer=setTimeout(()=>{$('save-state').textContent=localSave()?'Respuestas conservadas en este navegador. Usa Guardar respuestas para enviarlas a revisión.':'El navegador no permite guardar respuestas locales. Puedes exportar JSON.';},250);}
function updateRow(id){const d=state.get(id),r=refs.get(id);r.choice.value=d.kind?optionValue(d.kind,d.key):'';r.ordinalRow.hidden=d.kind!=='section';r.ordinal.value=d.ordinal===undefined?'':String(d.ordinal);r.requestRow.hidden=d.kind!=='catalog-request';r.name.value=d.name||'';r.note.value=d.note||'';r.confirm.checked=d.confirmed;r.rowStatus.textContent=d.confirmed?(d.kind==='catalog-request'?'Solicitud validada para revisión del catálogo.':'Respuesta validada para revisar en el código.'):(d.kind?'Significado elegido; falta validar para revisión.':'Pendiente: elige un significado.');r.badge.textContent=d.confirmed?'Validada para revisión':'Pendiente';r.badge.className='pill'+(d.confirmed?' confirmed':'');r.card.classList.toggle('confirmed',d.confirmed);}
function updateStats(){let confirmed=0,requests=0;for(const d of state.values())if(d.confirmed){if(d.kind==='catalog-request')requests++;else confirmed++;}$('confirmed-count').textContent=confirmed;$('request-count').textContent=requests;$('pending-count').textContent=DATA.cases.length-confirmed-requests;$('save').disabled=busy;}
function filter(){const query=normalize($('search').value),language=$('language').value,status=$('status').value,onlyPending=$('only-pending').checked;let shown=0;for(const c of DATA.cases){const r=refs.get(c.id),d=state.get(c.id);const search=r.search+' '+normalize((d.name||'')+' '+(d.note||'')+' '+(d.key||''));const show=(!query||search.includes(query))&&(!language||c.language===language)&&(!status||(status==='priority'?!c.interpretedCorrect:c.status===status))&&(!onlyPending||!d.confirmed);r.card.hidden=!show;if(show)shown++;}$('visible-count').textContent=shown+' de '+DATA.cases.length+' clips';$('empty').hidden=shown!==0;}
function targetFromReference(c){const target=c.expected;if(!target||!groupForKind[target.kind])return null;const match=(DATA.catalogue[groupForKind[target.kind]]||[]).find(e=>normalize(e.key)===normalize(target.key));if(!match)return null;const d={caseId:c.id,confirmed:false,kind:target.kind,key:match.key};if(target.kind==='section'&&Number.isInteger(target.ordinal))d.ordinal=target.ordinal;return d;}
function buildCase(c){const card=el('article',undefined,'case');card.dataset.caseId=c.id;const head=el('div',undefined,'case-head'),heading=el('div',undefined,'case-heading');heading.append(el('span','#'+c.index+' · '+c.language,'case-number'),el('strong',c.expectedLabel));if(!c.interpretedCorrect)heading.append(el('span','Por corregir','pill priority'));const badge=el('span','Pendiente','pill');heading.append(badge);const listen=el('button','▶ Escuchar','listen');listen.type='button';listen.setAttribute('aria-pressed','false');listen.addEventListener('click',()=>playRegion(c,listen));head.append(heading,listen);const grid=el('div',undefined,'case-grid');const reference=el('div',undefined,'reference');reference.append(el('span','Referencia del archivo','overline'),el('strong',c.expectedLabel),el('small',c.startSeconds.toFixed(3)+'–'+c.endSeconds.toFixed(3)+' s'),el('small',c.sourceZipPath||c.id));const use=el('button','Usar referencia','use-reference secondary');use.type='button';use.disabled=!targetFromReference(c);use.addEventListener('click',()=>{const old=state.get(c.id),target=targetFromReference(c);if(target){if(old.note)target.note=old.note;state.set(c.id,target);changed(c.id);}});reference.append(use);const actual=el('div');actual.append(el('span','Whisper · texto original','overline'),el('p',c.actual.length?c.actual.map(a=>a.text).join('\n'):'Sin transcripción asignada','literal'),el('div','PlayWorship: '+(c.actual.length?c.actual.map(a=>meaning(a.interpreted)).join(' · '):'Sin reconocer'),'meaning'),el('small','Evaluación: '+(statusNames[c.status]||c.status)));const editor=el('div',undefined,'editor'),fields=el('div',undefined,'edit-fields');const choice=el('select',undefined,'meaning-select');choice.setAttribute('aria-label','Significado corregido de '+c.expectedLabel);choice.append(new Option('Pendiente — elige significado',''));for(const [kind,group,label] of [['section','sections','Secciones'],['instruction','instructions','Indicaciones'],['count','counts','Conteos']]){const options=el('optgroup');options.label=label;for(const entry of DATA.catalogue[group])options.append(new Option(entry.label,optionValue(kind,entry.key)));choice.append(options);}choice.append(new Option('Falta en catálogo…',optionValue('catalog-request','')));choice.addEventListener('change',()=>{const [kind,key]=readChoice(choice.value),old=state.get(c.id),d={caseId:c.id,confirmed:false,kind,key};if(old.note)d.note=old.note;state.set(c.id,d);changed(c.id);});const ordinalRow=el('label','Ordinal de sección (opcional)','ordinal-row'),ordinal=el('input');ordinal.type='number';ordinal.min='1';ordinal.max='999';ordinal.step='1';ordinal.placeholder='—';ordinal.addEventListener('input',()=>{const d=state.get(c.id);if(ordinal.value==='')delete d.ordinal;else d.ordinal=Number(ordinal.value);changed(c.id);});ordinalRow.append(ordinal);const requestRow=el('label','¿Qué falta en el catálogo?','request-row'),name=el('input');name.type='text';name.maxLength=128;name.placeholder='Nombre solicitado…';name.addEventListener('input',()=>{state.get(c.id).name=name.value;changed(c.id);});requestRow.append(name);const confirmRow=el('label',undefined,'confirm-row'),confirm=el('input');confirm.type='checkbox';confirm.checked=false;confirmRow.append(confirm,el('span','Validada para revisión'));const rowStatus=el('div',undefined,'row-status'),error=el('div',undefined,'row-error');error.setAttribute('role','alert');confirm.addEventListener('change',()=>{const d=state.get(c.id),issue=choiceError(d);if(confirm.checked&&issue){confirm.checked=false;error.textContent=issue;card.classList.add('has-error');return;}d.confirmed=confirm.checked;changed(c.id,false);});const note=el('details',undefined,'note'),noteInput=el('textarea');note.append(el('summary','Añadir una nota (opcional)'));noteInput.maxLength=1000;noteInput.setAttribute('aria-label','Nota para '+c.expectedLabel);noteInput.addEventListener('input',()=>{state.get(c.id).note=noteInput.value;changed(c.id,false);});note.append(noteInput);fields.append(choice,ordinalRow,requestRow,confirmRow,rowStatus,error,note);editor.append(el('span','Significado corregido','overline'),fields);grid.append(reference,actual,editor);card.append(head,grid);refs.set(c.id,{card,choice,ordinalRow,ordinal,requestRow,name,confirm,rowStatus,error,note:noteInput,badge,search:normalize(c.expectedLabel+' '+c.language+' '+c.id+' '+c.actual.map(a=>a.text+' '+meaning(a.interpreted)).join(' '))});$('cases').append(card);updateRow(c.id);}
const audio=$('audio');audio.src=apiAllowed?'/audio.wav':DATA.fileAudio;let stopAt=null,activeListen=null,frame=0,playRequest=0;
function releaseListen(){if(activeListen)activeListen.setAttribute('aria-pressed','false');activeListen=null;}
function monitor(){cancelAnimationFrame(frame);if(audio.paused||stopAt===null)return;if(audio.currentTime>=stopAt){const end=stopAt;stopAt=null;audio.pause();audio.currentTime=end;releaseListen();$('play-status').textContent='Clip finalizado.';return;}frame=requestAnimationFrame(monitor);}
async function playRegion(c,button){const request=++playRequest;audio.pause();cancelAnimationFrame(frame);releaseListen();activeListen=button;button.setAttribute('aria-pressed','true');stopAt=c.endSeconds;$('play-status').textContent=c.expectedLabel+' · '+c.startSeconds.toFixed(3)+'–'+c.endSeconds.toFixed(3)+' s';try{audio.currentTime=c.startSeconds;await audio.play();if(request===playRequest)monitor();}catch(error){if(request!==playRequest)return;stopAt=null;releaseListen();$('play-status').textContent='No se pudo reproducir el audio. Comprueba el servidor local o el WAV junto al HTML.';}}
audio.addEventListener('play',monitor);audio.addEventListener('timeupdate',()=>{if(stopAt!==null&&audio.currentTime>=stopAt)monitor();});audio.addEventListener('pause',()=>cancelAnimationFrame(frame));audio.addEventListener('ended',()=>{stopAt=null;releaseListen();});
function fieldErrors(errors){for(const r of refs.values()){r.error.textContent='';r.card.classList.remove('has-error');}if(!Array.isArray(errors))return;let first=null;for(const entry of errors){if(!entry)continue;const text=typeof entry==='string'?entry:String(entry.message||'Error de validación');const r=refs.get(entry.caseId);if(r){r.error.textContent+=(r.error.textContent?'\n':'')+(entry.field?entry.field+': ':'')+text;r.card.classList.add('has-error');if(!first)first=r;}}if(first){$('status').value='';$('language').value='';$('search').value='';$('only-pending').checked=false;filter();first.card.scrollIntoView({block:'center',behavior:'smooth'});}}
function validateForSend(){const errors=[];for(const d of state.values())if(d.kind){const issue=choiceError(d);if(issue)errors.push({caseId:d.caseId,field:d.kind==='catalog-request'?'name':'key',message:issue});if(d.note&&d.note.length>1000)errors.push({caseId:d.caseId,field:'note',message:'La nota supera 1000 caracteres.'});}fieldErrors(errors);if(errors.length){message('Hay '+errors.length+' campos por revisar. Se muestran en sus filas.','error');return false;}return true;}
async function api(path,options){if(!apiAllowed)throw new Error('Abre el formulario desde el servidor local para guardar las respuestas.');const response=await fetch(path,{credentials:'same-origin',cache:'no-store',...options});let result;try{result=await response.json();}catch(error){throw new Error('El servidor no devolvió una respuesta JSON válida (HTTP '+response.status+').');}if(!response.ok||result.ok===false){fieldErrors(result.errors);const error=new Error(result.message||result.error||'El servidor rechazó la operación (HTTP '+response.status+').');error.response=result;throw error;}return result;}
async function sendResponses(){if(!validateForSend())return;const body=payload();if(!apiAllowed){const kept=localSave();$('save-state').textContent=kept?'Respuestas guardadas sólo en este navegador.':'No se pudieron guardar respuestas en este navegador.';message('Puedes exportar el JSON para revisar las respuestas y adaptar el código. Para guardarlas en el servidor, abre el formulario desde su dirección local.');return;}busy=true;updateStats();const submittedRevision=revision;try{const result=await api('/api/review',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});if(result.review&&submittedRevision===revision)restoreReview(result.review);clearTimeout(autosaveTimer);localSave();fieldErrors(result.errors);const saved=Number.isInteger(result.savedCount)?result.savedCount:'cantidad no informada';message('Respuestas guardadas para revisión: '+saved+'. Se revisarán para adaptar el catálogo, los alias y las reglas en código.','success');$('save-state').textContent='Última respuesta del servidor: '+new Date().toLocaleTimeString();}catch(error){const kept=localSave();message(error.message+(kept?' Las respuestas permanecen guardadas en este navegador.':''),'error');$('save-state').textContent='No se confirmó el guardado en el servidor.';}finally{busy=false;updateStats();}}
$('save').addEventListener('click',sendResponses);
$('export').addEventListener('click',()=>{const blob=new Blob([JSON.stringify(payload(),null,2)+'\n'],{type:'application/json'}),url=URL.createObjectURL(blob),a=el('a');a.href=url;a.download='guide-feedback-'+DATA.datasetId.slice(0,12)+'.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);$('save-state').textContent='Respuestas exportadas como JSON para revisión del código.';});
$('import').addEventListener('click',()=>$('import-file').click());$('import-file').addEventListener('change',async event=>{const file=event.target.files[0];if(!file)return;try{if(file.size>1024*1024)throw new Error('El JSON de respuestas supera el límite de 1 MB.');restoreReview(JSON.parse(await file.text()));revision++;localSave();message('Respuestas importadas para continuar la revisión de este dataset.','success');}catch(error){message(error.message,'error');}finally{event.target.value='';}});
for(const id of ['search','language','status','only-pending'])$(id).addEventListener(id==='search'?'input':'change',filter);$('show-all').addEventListener('click',()=>{$('status').value='';$('language').value='';$('search').value='';$('only-pending').checked=false;filter();});
$('restore-local').addEventListener('click',()=>{try{restoreReview(localCandidate);revision++;localSave();$('restore').hidden=true;message('Se recuperaron las respuestas locales. Puedes continuar la revisión y guardarlas.','success');}catch(error){message(error.message,'error');}});$('dismiss-local').addEventListener('click',()=>{$('restore').hidden=true;localSave();});
async function loadDraft(){try{const stored=localStorage.getItem(localKey);if(stored)localCandidate=validateReview(JSON.parse(stored));}catch(error){message('Las respuestas locales no pudieron recuperarse: '+error.message,'error');}if(apiAllowed){try{const result=await api('/api/review');const remote=validateReview(result);restoreReview(remote);$('save-state').textContent='Respuestas recuperadas del servidor: '+remote.decisions.length+'.';if(localCandidate&&JSON.stringify(localCandidate)!==JSON.stringify(remote))$('restore').hidden=false;return;}catch(error){message('No se pudieron recuperar las respuestas del servidor. '+error.message,'error');}}else message('Modo archivo: puedes revisar, guardar en este navegador y exportar las respuestas JSON.');if(localCandidate){try{restoreReview(localCandidate);$('save-state').textContent='Respuestas locales recuperadas para este dataset.';}catch(error){message(error.message,'error');}}}
$('total-count').textContent=DATA.cases.length;$('priority-count').textContent=DATA.cases.filter(c=>!c.interpretedCorrect).length;$('status').options[0].textContent='Por corregir ('+$('priority-count').textContent+')';$('show-all').textContent='Ver los '+DATA.cases.length+' clips';$('dataset').textContent=DATA.datasetId.slice(0,16)+'…';$('catalogue-info').textContent=DATA.catalogue.sections.length+' secciones · '+DATA.catalogue.instructions.length+' indicaciones · '+DATA.catalogue.counts.length+' conteos';for(const language of [...new Set(DATA.cases.map(c=>c.language))].sort())$('language').append(new Option(language,language));for(const status of [...new Set(DATA.cases.map(c=>c.status))].sort())$('status').append(new Option(statusNames[status]||status,status));for(const c of [...DATA.cases].sort((a,b)=>Number(a.interpretedCorrect)-Number(b.interpretedCorrect)||a.index-b.index))buildCase(c);updateStats();filter();loadDraft();
</script></body></html>'''


def write_page(path: Path, data: dict) -> None:
    # JSON is data only; escape script delimiters before embedding it. Runtime
    # strings are rendered with DOM textContent, never interpolated as HTML.
    serialized = json.dumps(data, ensure_ascii=False, separators=(",", ":"))
    serialized = serialized.replace("&", "\\u0026").replace("<", "\\u003c").replace(">", "\\u003e")
    serialized = serialized.replace("\u2028", "\\u2028").replace("\u2029", "\\u2029")
    page = PAGE.replace("@@BOOT@@", serialized)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent,
                                         prefix=f".{path.name}.", suffix=".tmp", delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(page)
        temporary.replace(path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="Genera la revisión editable de los clips reales de Guía sin modificar PlayWorship.")
    parser.add_argument("--evaluation", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--catalogue", required=True, type=Path, help="JSON del catálogo real: sections, instructions y counts.")
    parser.add_argument("--output", type=Path, help="HTML nuevo; por defecto guide-recognition-review.html junto a la evaluación.")
    args = parser.parse_args()
    try:
        evaluation = args.evaluation.expanduser().resolve()
        manifest = args.manifest.expanduser().resolve()
        catalogue = args.catalogue.expanduser().resolve()
        output = (args.output or evaluation.parent / "guide-recognition-review.html").expanduser().resolve()
        if output in (evaluation, manifest, catalogue) or output.name == "guide-recognition-report.html":
            raise ValueError("La revisión editable no puede reemplazar los datos ni el informe de sólo lectura.")
        data = boot_data(evaluation, manifest, catalogue)
        write_page(output, data)
    except (OSError, ValueError, TypeError, AttributeError) as error:
        parser.exit(1, f"Error: {error}\n")
    print(f"HTML editable: {output}")
    print(f"Casos: {len(data['cases'])}; por corregir: {sum(not case['interpretedCorrect'] for case in data['cases'])}; validaciones iniciales: 0")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
