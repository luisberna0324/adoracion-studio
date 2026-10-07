#!/usr/bin/env python3
"""Genera el manual de secciones a partir de capturas reales de PlayWorship.

Ejecutar con el Python que contiene ReportLab y Pillow. No modifica proyectos.
"""
from pathlib import Path

from PIL import Image
from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.styles import ParagraphStyle
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfgen import canvas
from reportlab.platypus import Paragraph


ROOT = Path(__file__).resolve().parents[3]
SHOTS = Path(__file__).resolve().parent / "capturas"
TEMP = ROOT / "tmp/pdfs/secciones"
OUTPUT = ROOT / "output/pdf/PlayWorship-Manual-de-secciones.pdf"
TEMP.mkdir(parents=True, exist_ok=True)
OUTPUT.parent.mkdir(parents=True, exist_ok=True)

FONT_ROOT = Path("/System/Library/Fonts/Supplemental")
for font, file in [("PW", "Arial.ttf"), ("PW-Bold", "Arial Bold.ttf"),
                   ("PW-Italic", "Arial Italic.ttf")]:
    pdfmetrics.registerFont(TTFont(font, str(FONT_ROOT / file)))
pdfmetrics.registerFontFamily("PW", normal="PW", bold="PW-Bold", italic="PW-Italic")

W, H = 595.276, 841.890
M = 42
CW = W - 2 * M
INK = colors.HexColor("#202926")
SECONDARY = colors.HexColor("#626D67")
ACCENT = colors.HexColor("#B8731C")
GREEN = colors.HexColor("#27715D")
PALE = colors.HexColor("#F1F4F0")
AMBER_PALE = colors.HexColor("#FFF4DF")
RULE = colors.HexColor("#DDE3DD")

STYLES = {
    "body": ParagraphStyle("body", fontName="PW", fontSize=11.2, leading=15.5,
                           textColor=INK, alignment=TA_LEFT),
    "small": ParagraphStyle("small", fontName="PW", fontSize=9.2, leading=12.5,
                            textColor=SECONDARY),
    "lead": ParagraphStyle("lead", fontName="PW", fontSize=12.5, leading=17.5,
                           textColor=SECONDARY),
    "heading": ParagraphStyle("heading", fontName="PW-Bold", fontSize=14,
                              leading=18, textColor=INK),
}

c = canvas.Canvas(str(OUTPUT), pagesize=(W, H), pageCompression=1)
c.setTitle("Secciones en PlayWorship | Manual práctico")
c.setAuthor("PlayWorship")
c.setSubject("Creación y edición de secciones, indicaciones, loop, mute y automatización")
page = 0


def text(value, x=M, top=0, width=CW, style="body"):
    p = Paragraph(value, STYLES[style])
    _, height = p.wrap(width, H)
    assert top + height <= H - 46, f"Texto fuera de página {page}: {value[:70]}"
    p.drawOn(c, x, H - top - height)
    return top + height


def heading(value, top, x=M, width=CW):
    return text(value, x, top, width, "heading") + 8


def start(title, subtitle, dest):
    global page
    page += 1
    c.setFillColor(colors.white)
    c.rect(0, 0, W, H, fill=1, stroke=0)
    c.bookmarkPage(dest)
    c.addOutlineEntry(title, dest, level=0)
    c.setFont("PW-Bold", 10)
    c.setFillColor(GREEN)
    c.drawString(M, H - 35, "PLAYWORSHIP  /  MANUAL DE SECCIONES")
    c.setStrokeColor(RULE)
    c.line(M, H - 48, W - M, H - 48)
    c.setFont("PW-Bold", 25)
    c.setFillColor(INK)
    c.drawString(M, H - 85, title)
    return text(subtitle, top=101, style="lead") + 18


def finish():
    c.setStrokeColor(RULE)
    c.line(M, 39, W - M, 39)
    c.setFont("PW", 8)
    c.setFillColor(SECONDARY)
    c.drawString(M, 25, "PlayWorship 1.1.4 · Escritorio · 3 oct 2026")
    c.drawRightString(W - M, 25, f"{page} / 8")
    c.showPage()


def crop(name, box, key):
    target = TEMP / f"figura-{key}.png"
    im = Image.open(SHOTS / name)
    assert 0 <= box[0] < box[2] <= im.width
    assert 0 <= box[1] < box[3] <= im.height
    im.crop(box).save(target)
    return target


def figure(path, top, caption, x=M, width=CW, max_height=None):
    im = Image.open(path)
    height = width * im.height / im.width
    if max_height is not None and height > max_height:
        height = max_height
        actual_width = height * im.width / im.height
        x += (width - actual_width) / 2
    else:
        actual_width = width
    assert top + height <= H - 64
    c.drawImage(str(path), x, H - top - height, width=actual_width, height=height)
    end = text(caption, x=x, top=top + height + 7, width=actual_width, style="small")
    return end + 15


def note(title, value, top, width=CW, x=M, tint=AMBER_PALE):
    p_title = Paragraph(title, STYLES["heading"])
    p_body = Paragraph(value, STYLES["body"])
    _, ht = p_title.wrap(width - 26, H)
    _, hb = p_body.wrap(width - 26, H)
    height = ht + hb + 31
    assert top + height < H - 46, f"Recuadro fuera de página {page}: {top=}, {height=}, {title}"
    c.setFillColor(tint)
    c.roundRect(x, H - top - height, width, height, 10, fill=1, stroke=0)
    p_title.drawOn(c, x + 13, H - top - 13 - ht)
    p_body.drawOn(c, x + 13, H - top - 19 - ht - hb)
    return top + height + 16


def step(number, title, value, top, x=M, width=CW):
    c.setFillColor(GREEN)
    c.circle(x + 11, H - top - 11, 11, fill=1, stroke=0)
    c.setFont("PW-Bold", 10)
    c.setFillColor(colors.white)
    c.drawCentredString(x + 11, H - top - 14, str(number))
    end = text(f"<b>{title}</b><br/>{value}", x + 34, top,
               width - 34, "body")
    return max(top + 24, end) + 13


def table(rows, widths, top, headers=None, font="body"):
    all_rows = ([headers] if headers else []) + rows
    y = top
    for row_idx, row in enumerate(all_rows):
        paras = [Paragraph(v, STYLES[font]) for v in row]
        heights = [p.wrap(width - 20, H)[1] for p, width in zip(paras, widths)]
        rh = max(heights) + 19
        assert y + rh < H - 47
        if row_idx == 0 and headers:
            c.setFillColor(PALE)
            c.rect(M, H - y - rh, sum(widths), rh, fill=1, stroke=0)
        x = M
        for p, width, height in zip(paras, widths, heights):
            p.drawOn(c, x + 10, H - y - 9 - height)
            x += width
        c.setStrokeColor(RULE)
        c.line(M, H - y - rh, M + sum(widths), H - y - rh)
        y += rh
    return y + 16


# Los recortes conservan los píxeles de las capturas; no se recrea la interfaz.
overview = crop("01-reproduccion.png", (0, 54, 4112, 2580), "vista-general")
normal_wave = crop("07-reproduccion-detalle.png", (770, 190, 2010, 455), "puntos")
edit_wave = crop("02-edicion.png", (770, 248, 2010, 515), "etiquetas")
insertion = crop("06-insercion.png", (900, 188, 1703, 530), "insercion")
selector = crop("03-selector-secciones.png", (1460, 352, 1787, 1414), "selector")
toolbar = crop("02-edicion.png", (900, 255, 1705, 520), "acciones-editar")
count_menu = crop("04-conteos.png", (1755, 732, 2125, 1354), "conteos")
cue_detail = crop("02-edicion.png", (1704, 245, 2404, 516), "detalle-marca")


# 1. Portada y navegación.
top = start("Secciones en PlayWorship", "Manual práctico con capturas reales de la aplicación.", "inicio")
top = figure(overview, top, "Vista de escritorio: onda y secciones arriba, mezcla y Pads debajo.",
             max_height=321)
top = heading("Lo que aprenderás", top + 2)
topics = [
    ("02", "Entender los dos tipos de secciones", "tipos"),
    ("03", "Crear una sección musical", "crear"),
    ("04", "Mover, ajustar y copiar", "editar"),
    ("05", "Añadir conteos e indicaciones", "indicaciones"),
    ("06", "Usar loop, mute y saltos", "reproducir"),
    ("07", "Automatizar instrumentos y Pads", "automatizar"),
    ("08", "Consultar el catálogo y practicar", "catalogo"),
]
for page_label, label, dest in topics:
    c.setFont("PW-Bold", 11)
    c.setFillColor(ACCENT)
    c.drawString(M, H - top - 12, page_label)
    text(label, M + 34, top, CW - 34)
    c.linkRect("", dest, (M, H - top - 21, W - M, H - top), relative=0, thickness=0)
    top += 28
text("Las capturas corresponden a la versión de escritorio. Los ejemplos usan una canción "
     "ya cargada y muestran el flujo del editor de secciones.", top=top + 6, style="small")
finish()


# 2. Modelo mental y cambio visual solicitado.
top = start("Dos niveles en la onda", "La estructura musical y las indicaciones conviven sin partir la canción.", "tipos")
top = table([
    ["<b>Sección musical</b><br/>Tiene inicio y fin.",
     "Intro, Verso, Coro, Puente…<br/>Organiza un tramo de la canción. Admite copia, loop, mute y automatización."],
    ["<b>Indicación secundaria</b><br/>Es una marca en un instante.",
     "Conteos, Baja intensidad, Toda la banda…<br/>Da una señal a los músicos sin dividir el Verso o el Coro que la contiene."],
], [157, CW - 157], top)
top = heading("Durante la reproducción", top)
top = figure(normal_wave, top, "Las indicaciones se ven solo como puntos de color sobre la onda.")
top = heading("Al activar Editar", top)
top = figure(edit_wave, top, "Aparecen la etiqueta y la línea de cada marca. Las etiquetas cercanas se apilan.")
top = note("Elige el tipo según lo que necesitas", "Usa una sección para una parte musical con duración. "
     "Usa una indicación para avisar algo dentro de esa parte: por ejemplo, «Toda la banda» "
     "a mitad de un Verso.", top, tint=PALE)
text("GUÍA DYN anuncia la sección musical un compás antes de su entrada. Si empieza en "
     "0:00, la anuncia al entrar. Las indicaciones secundarias se anuncian en su punto.",
     top=top, style="small")
finish()


# 3. Inserción: referencia temporal y selector.
top = start("Crear una sección", "Marca el momento en la regla y elige la parte musical.", "crear")
top = figure(insertion, top, "La línea discontinua y el botón + muestran dónde se insertará la nueva marca.",
             max_height=130)
left_width = 292
right_x = M + left_width + 20
y = top
y = step(1, "Activa Editar", "Pulsa el lápiz de la esquina inferior derecha de la onda.", y, width=left_width)
y = step(2, "Elige Mapa de guía", "Usa el icono de notas musicales. Ahí se editan las secciones y las indicaciones.", y, width=left_width)
y = step(3, "Sitúa el inicio", "Haz clic en la regla. Comprueba el compás y la línea de inserción.", y, width=left_width)
y = step(4, "Abre el selector", "Pulsa el <b>+</b> junto a la línea y selecciona Intro, Verso, Coro u otra parte.", y, width=left_width)
y = step(5, "Comprueba el resultado", "La nueva sección comienza en ese punto. Si cae dentro de otra sección, divide ese tramo.", y, width=left_width)
menu_end = figure(selector, top, "Selector de partes musicales.", x=right_x,
                  width=CW - left_width - 20, max_height=328)
top = max(y, menu_end) + 5
note("Dos botones +, dos referencias", "El <b>+</b> junto a la línea usa el punto de la regla. "
     "El <b>+</b> de la barra de una sección usa el final de esa sección. "
     "Comprueba esa referencia antes de elegir el nombre.", top)
finish()


# 4. Edición y protección de la fuente de las copias.
top = start("Mover, ajustar y copiar", "Trabaja en Editar y comprueba la posición antes de reproducir.", "editar")
top = figure(toolbar, top, "Barra sobre la sección: Mute, Skip, Duplicar, + y Automatizar.", max_height=112)
top = table([
    ["<b>Mover</b>", "Arrastra el cuerpo de la sección para trasladar su inicio y su fin."],
    ["<b>Ajustar la duración</b>", "Arrastra un borde. La duración se limita al audio disponible; el ajuste puede desplazar las secciones posteriores."],
    ["<b>Duplicar</b>", "Usa el icono de dos cuadrados. La copia se inserta después de la sección y conserva el audio de origen y sus indicaciones."],
    ["<b>Eliminar</b>", "Pulsa la X de la sección seleccionada. Para una indicación secundaria, haz doble clic en su punto."],
], [141, CW - 141], top)
top = heading("Ajuste y atajos", top)
top = text("El selector <b>Ajuste</b> permite Compás, Beat, 1/2 Beat o Libre. "
           "Option/Alt permite mover libremente durante el gesto.", top=top) + 13
top = table([
    ["Cmd/Ctrl + C", "Copiar la selección."],
    ["Cmd/Ctrl + V", "Pegar después de la sección seleccionada."],
    ["Cmd/Ctrl + Z / Y", "Deshacer / Rehacer dentro del editor."],
], [170, CW - 170], top, font="small")
note("Una copia mantiene su referencia", "Mover una copia hacia la izquierda conserva el tramo de "
     "audio del que se copió. Revisa también el límite <b>Fin</b> de reproducción: es distinto "
     "del punto de inserción y puede hacer que la canción termine antes.", top, tint=PALE)
finish()


# 5. Marcas y conteos: procedimiento concreto.
top = start("Conteos e indicaciones", "Añade avisos dentro de una sección sin cambiar su estructura.", "indicaciones")
left_width = 287
right_x = M + left_width + 22
y = top
y = step(1, "Elige el instante", "En Editar y Mapa de guía, sitúa la línea en la regla y pulsa su <b>+</b>.", y, width=left_width)
y = step(2, "Escoge el aviso", "Entra en <b>Conteos</b> o <b>Utilidades</b>. Para contar varios pulsos, elige Secuencia 1-4 o 1-7.", y, width=left_width)
y = step(3, "Ajusta la marca", "Arrastra el punto sobre la onda. Su etiqueta indica qué aviso contiene.", y, width=left_width)
y = step(4, "Elimina si hace falta", "Haz doble clic sobre el punto. El tooltip recuerda ese gesto. Puedes deshacer con Cmd/Ctrl + Z.", y, width=left_width)
menu_end = figure(count_menu, top, "Menú Conteos. Cada secuencia se guarda como una sola marca.",
                  x=right_x, width=CW - left_width - 22, max_height=297)
top = max(y, menu_end) + 3
top = figure(cue_detail, top, "Ejemplo: «Toda la banda» dentro de un Verso; la sección conserva su duración.",
             max_height=131)
top = note("Son indicaciones para los músicos", "«Baja intensidad», «Pausa» o «Baja tono» emiten "
           "avisos de guía. No cambian por sí solos el volumen, el transporte ni el tono. "
           "Para cambiar la mezcla automáticamente, usa <b>Automatizar</b>.", top)
text("Para escucharlos, comprueba volumen, Mute y salida del canal <b>GUÍA DYN</b>. "
     "Los conteos siguen los pulsos de la canción; una indicación se anuncia en su punto.",
     top=top, style="small")
finish()


# 6. Acciones de ejecución, diferenciadas de las marcas.
top = start("Usar las secciones al tocar", "Sal de Editar para manejar la reproducción del arreglo.", "reproducir")
top = figure(normal_wave, top, "Las secciones musicales siguen visibles; las indicaciones quedan como puntos.",
             max_height=122)
top = text("Selecciona una sección sobre la onda para acceder a sus acciones. "
           "Comprueba siempre qué tramo queda resaltado antes de activar un control.", top=top) + 17
top = table([
    ["<b>Loop</b><br/>Flechas de repetición", "Repite la sección. Vuelve a pulsar el control para desactivarlo."],
    ["<b>Mute</b><br/>Botón M", "Silencia los instrumentos en ese tramo. Click y guía conservan sus propios controles de Mute."],
    ["<b>Salto</b>", "Solicita ir a otra sección. Un doble clic en una sección fuera de Editar también solicita el salto."],
    ["<b>Skip</b><br/>En la barra de edición", "Omite ese tramo del arreglo. Se diferencia de Mute: no recorre una sección en silencio."],
], [173, CW - 173], top)
top = note("Loop y Mute pueden estar activos juntos", "Puedes repetir una sección con los "
           "instrumentos silenciados para mantener el click y la guía. Activar o quitar uno "
           "de esos controles no desactiva el otro.", top, tint=PALE)
top = heading("Prueba antes de usarlo en vivo", top)
top = text("Reproduce unos compases antes de la sección. Activa el loop, comprueba el regreso "
           "al inicio y prueba Mute. Después, desactiva ambos y verifica que el arreglo continúa.", top=top) + 16
text("La repetición A-B de Ensayo pertenece a otro flujo y puede tener prioridad sobre los "
     "loops y saltos de sección. Este manual describe el editor del arreglo.", top=top, style="small")
finish()


# 7. Automatización: los pasos disponibles en la implementación actual.
top = start("Automatizar una sección", "Guarda cambios de mezcla para un tramo concreto de la canción.", "automatizar")
top = figure(toolbar, top, "El botón Automatizar pertenece a la sección sobre la que aparece la barra.",
             max_height=147)
top = step(1, "Activa Automatizar", "Elige la sección y pulsa <b>Automatizar</b>. La reproducción se pausa y comienza un borrador.", top)
top = step(2, "Ajusta la mezcla", "Modifica los instrumentos y Pads que necesites. Los controles y la sección resaltan el estado de automatización.", top)
top = step(3, "Confirma o descarta", "Pulsa <b>Listo</b> para guardar o <b>Cancelar</b> para descartar. La canción permanece pausada; pulsa Play para comprobarla.", top)
top = step(4, "Escucha la entrada y la salida", "Reproduce desde antes de la sección. Verifica sus valores al entrar y la mezcla que corresponde al pasar a la siguiente sección.", top)
top = table([
    ["<b>Instrumentos</b>", "Volumen, ganancia y Mute."],
    ["<b>Pads</b>", "Volumen, ganancia, encendido, nota y octava."],
    ["<b>Control habitual</b>", "Master, Solo, click, guía y ruteo mantienen su manejo normal."],
], [151, CW - 151], top)
top = note("Solo se guardan los controles que modificas", "La automatización es una mezcla parcial "
           "de esa sección. «Baja intensidad» puede servir de aviso de guía y, en paralelo, "
           "la automatización puede bajar instrumentos o Pads.", top, tint=PALE)
text("Para quitar los ajustes guardados de una sección, abre su flujo de automatización y "
     "elige <b>Más → Borrar automatización</b>. Durante el borrador, termina o cancela antes de "
     "hacer cambios estructurales en las secciones.", top=top, style="small")
finish()


# 8. Catálogo completo del selector y ejercicio final.
top = start("Catálogo y práctica rápida", "Consulta qué pertenece a la estructura y qué es una indicación secundaria.", "catalogo")
top = heading("Secciones musicales", top)
top = text("Intro · Verso · Pre-Coro · Coro · Refrain · Post-Coro · Puente · Instrumental · "
           "Solo · Interlude · Tag · Vamp · Outro · Final.", top=top) + 8
top = text("Verso, Pre-Coro, Coro y Puente ofrecen variantes numeradas en el selector.", top=top, style="small") + 19
top = heading("Indicaciones secundarias", top)
top = text("<b>Conteos:</b> Uno, Dos, Tres, Cuatro, Cinco, Seis y Siete. "
           "<b>Secuencias:</b> 1-4 y 1-7.", top=top) + 12
top = table([
    ["<b>Intensidad y ejecución</b>", "Sube intensidad, Sube lento, Baja intensidad (Breakdown), Sostener, Suave, Pausa, Toda la banda, A capella."],
    ["<b>Instrumentos</b>", "Batería, Entra batería, Bajo, Guitarra, Teclado."],
    ["<b>Tono y cierre</b>", "Sube tono, Baja tono, Final grande, Última vez."],
], [174, CW - 174], top)
top = heading("Ejercicio: un Verso con una entrada de banda", top)
top = step(1, "Define la estructura", "Crea o selecciona un Verso que cubra el tramo que vas a tocar.", top)
top = step(2, "Añade la indicación", "Marca un pulso dentro del Verso y elige <b>Utilidades → Toda la banda</b>.", top)
top = step(3, "Comprueba la guía", "Reproduce desde antes de la marca. Revisa GUÍA DYN y la salida que usan los músicos.", top)
top = step(4, "Limpia la vista", "Sal de Editar: queda el punto. Vuelve a Editar para moverlo, leer su etiqueta o eliminarlo.", top)
top = step(5, "Añade cambios de mezcla si los necesitas", "Usa Automatizar sobre el Verso. Prueba la entrada y salida de la sección después de pulsar Listo.", top)
text("Si al cerrar la aplicación aparece un aviso de cambios pendientes en el proyecto, "
     "elige <b>Guardar y salir</b> para conservarlos en el archivo .pwproj. "
     "Las imágenes son recortes de capturas reales; no son mockups.", top=top + 4, style="small")
finish()

assert page == 8
c.save()
print(OUTPUT)
