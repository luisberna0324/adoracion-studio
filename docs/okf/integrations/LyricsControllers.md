---
type: Integration Contract
title: LyricsControllers
description: Control externo de letras mediante marcas temporales independientes
tags: [lyrics, freeshow, holyrics, cues, desktop]
timestamp: 2026-11-06
---

# Controladores de letras

La proyección de escritorio dispone de adaptadores para FreeShow y Holyrics.
La vista Letras y el scheduler temporal se comparten; cada destino conserva
su proveedor y el cliente comprueba que corresponda a la conexión activa.
Los contratos detallan la API, identidad de pantallas, threading y persistencia.
[FreeShow](FreeShow.md), [Holyrics](Holyrics.md).

Las marcas FreeShow anteriores usan la versión 1 y su proveedor implícito es
`freeshow`. Las marcas Holyrics usan la versión 2. Cambiar el proveedor conectado
requiere revisar los destinos de otro programa antes de utilizarlos.

Las marcas de letras son puntos independientes en el tiempo musical del
arreglo. No pertenecen a secciones, no poseen rangos de loop, y no disparan
saltos ni automatización de mezcla. Las secciones existentes sólo ayudan a
orientarse en el mapa. Seleccionar o editar un punto no proyecta; la prueba
manual es explícita y el seguimiento de reproducción se activa por canción.

ProPresenter, EasyWorship y WorshipTools no tienen adaptadores implementados.
Las propuestas históricas de vincular acciones directamente a secciones o
avanzar slides por `next` no son el contrato vigente.
