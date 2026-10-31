---
type: Integration Contract
title: LyricsControllers
description: Control externo de letras mediante marcas temporales independientes
tags: [lyrics, freeshow, cues, desktop]
timestamp: 2026-10-17T00:00:00Z
---

# Controladores de letras

La implementación actual se limita a FreeShow en escritorio. Véase el contrato
[FreeShow](FreeShow.md) para API, identidad de pantallas, threading y persistencia.

Las marcas de letras son puntos independientes en el tiempo musical del
arreglo. No pertenecen a secciones, no poseen rangos de loop, y no disparan
saltos ni automatización de mezcla. Las secciones existentes sólo ayudan a
orientarse en el mapa. Seleccionar o editar un punto no proyecta; la prueba
manual es explícita y el seguimiento de reproducción se activa por canción.

ProPresenter, EasyWorship y WorshipTools no tienen adaptadores implementados.
Las propuestas históricas de vincular acciones directamente a secciones o
avanzar slides por `next` no son el contrato vigente.
