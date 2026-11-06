# Combinaciones rápidas de la guía

`rapid-suite.json` es un conjunto adicional de **30 casos y 116 eventos**:
62 cuentas, 25 anuncios principales y 29 indicaciones. Su duración compuesta
es **296,571375 s**; el caso más largo dura 14,2143125 s. Complementa los 24
escenarios anteriores, que conservan su especificación y sus resultados.

Las referencias se fijaron antes de ejecutar ASR. Este documento no afirma
que los casos estén aprobados: el runner debe acreditar cada marca contra
su tipo, nombre, ordinal e instante esperado. La tolerancia permanece en
**±40 ms**, sin ampliarla para favorecer los casos rápidos; un compás
incorrecto sigue siendo un fallo.

## Qué significa «rápido» en este conjunto

Se usan los clips originales del pack, **sin acelerar la pronunciación ni
cambiar el tono**. Se reducen las pausas reales entre anuncios y se colocan
cuentas sobre pulsos de 120, 140, 160 y 180 BPM. A esas velocidades algunas
colas vocales se tocan o solapan: **13 casos contienen solapamiento activo**.
El manifiesto conserva el inicio y final de cada voz y sus gaps negativos.
Esos casos son estrés con fuentes conocidas; no representan una nueva voz
humana pronunciando más deprisa.

El generador recorta exclusivamente las copias de trabajo hasta sus límites
de actividad y conserva el margen declarado. Nunca corta palabras para
simular una pausa ni modifica los archivos originales. Los gaps de la
escalera son 200, 100, 60, 40, 20 y 0 ms entre voces activas.

## Matriz de casos

| Grupo e IDs | Condición | Resultado esperado |
| --- | --- | --- |
| `rapid-count-es-120/140/160/180` | Cuenta ES sobre pulsos cada vez más próximos. | 1, 2, Intro, 2, 3 y 4; cinco cuentas distintas e Intro sin ordinal. |
| `rapid-count-en-120/140/160/180` | La misma secuencia EN. | Los mismos seis eventos, conservando tiempos e identidad de los dos «2». |
| `rapid-pair-es-200/100/060/040/020/000` | Mismos clips de Interludio y Toda la banda, misma cuadrícula a 160 BPM; sólo varía la pausa activa. | Una principal y una indicación independiente, sin perder All In dentro del nombre. |
| `rapid-triple-es-100` | Build, All In y Drums In; 140 BPM, pausas de 100 ms. | Tres indicaciones y cero límites musicales. |
| `rapid-triple-en-040` | La misma cadena EN; 160 BPM, pausas de 40 ms. | Tres indicaciones; la pausa interna de All In no crea otra marca. |
| `rapid-triple-mixed-000` | ES/EN/ES; 180 BPM y sin pausa añadida. | Tres indicaciones con sus tiempos hablados. |
| `rapid-chain-es-140` | Verso 1, Entra batería, Pausa y luego cuenta 1–4. | Una principal, dos indicaciones y cuatro cuentas; sin secciones artificiales de instrumentos. |
| `rapid-chain-en-160` | Bridge 2, All In, Hits y luego cuenta 1–4. | Conserva el ordinal 2 del puente y las cuatro cuentas independientes. |
| `rapid-chain-mixed-180` | Refrain EN, Entra batería ES, Build EN y cuenta ES. | Una principal, dos indicaciones y cuatro cuentas, aun con idiomas mezclados. |
| `rapid-ordinal-count-es-140` | Verso 2, un Dos aislado y después Intro, 2, 3, 4. | El ordinal de Verso no se confunde con una cuenta; Intro no se convierte en Intro 2. |
| `rapid-ordinal-count-en-180` | La misma distinción en inglés a 180 BPM. | Verse 2, Intro y las cuatro cuentas realmente incluidas, sin completar otra cuenta ausente. |
| `rapid-intro-single-count-es-180` / `rapid-intro-single-count-en-180` | Intro seguido de una única cuenta Dos/Two, con 20/0 ms de pausa activa. | Intro sin ordinal y un solo 2 independiente; no completar 1, 3 ni 4, ni acreditar Intro 2 como dos marcas. |
| `rapid-negative-silence-180` | Silencio sin voz. | Cero eventos; cualquier marca es un extra. |
| `rapid-negative-click-180` | Click a 180 BPM sin voz. | Cero eventos; no deducir números del click. |
| `rapid-low-gain-es-140` | Repite el triple ES de 100 ms con ganancia de −18 dB. | Misma referencia semántica y temporal que el caso original. |
| `rapid-noise-mixed-180` | Repite el triple mixto sin pausa con ruido de RMS −54 dBFS y semilla fija. | Las tres indicaciones conservan sus referencias; no se normalizan los fallos para aprobarlas. |
| `rapid-boundary-015ms-180` | Coro 15 ms antes de una línea y All In 20 ms después de acabar esa voz. | La principal usa la tolerancia contractual de frontera de 20 ms; la indicación conserva su tiempo. |
| `rapid-boundary-035ms-180` | El mismo patrón con Coro 35 ms antes de la línea. | Otro compás de destino que en el caso de 15 ms, por estar fuera de esa tolerancia. |

Cada caso tiene como máximo una principal, salvo los dos de ordinal frente a
cuenta: allí Verse y Intro se anuncian separados por cuatro compases y tienen
destinos distintos. Se comprobó que no hay dos referencias principales que
proyecten al mismo límite. Por tanto, una fusión legítima de límites del
editor no se confunde con un fallo de reconocimiento.

## Ejemplo verificable: cuenta ES a 180 BPM

Métrica 4/4, origen del compás 1 en 0,500000 s; negra de 0,333333… s.
Los tiempos se redondean a seis decimales sólo para mostrarlos. El manifiesto
conserva la referencia en frames de 16 kHz.

| Texto realmente incluido | Tipo / nombre | Inicio hablado (s) | Marca esperada (s) |
| --- | --- | ---: | ---: |
| Uno | Cuenta 1 | 0,500000 | 0,500000 |
| Dos | Cuenta 2 | 1,166688 | 1,166688 |
| Intro | Sección Intro, sin ordinal | 1,833313 | 3,166667 |
| Dos | Cuenta 2 | 2,166688 | 2,166688 |
| Tres | Cuenta 3 | 2,500000 | 2,500000 |
| Cuatro | Cuenta 4 | 2,833313 | 2,833313 |

La marca de Intro va al compás siguiente al anuncio. Las cuentas mantienen
sus tiempos hablados; no reciben ese desplazamiento ni se inventa otro Uno.
Una salida literal completa de Whisper sin tiempos útiles para cada número
no acredita seis marcas correctas.

## Congelación, ejecución y comparación

La validación de diseño utilizó `build_guide_scenarios.prepare` en memoria,
sin escribir WAV ni ejecutar ASR. Verificó todos los clips requeridos en el
idioma y ordinal indicados, los hashes del pack/corpus, los límites de audio,
los destinos principales únicos y los 116 eventos dentro de su fuente.

- SHA-256 de `rapid-suite.json`:
  `da60c8775693713e830760ad7c64ec8b53a68bd1d38cc2ac54158e56639e100d`.
- Identidad calculada del conjunto:
  `50433b40e62df15458f7a1ca56f57581445cbbc0bab3ac129d89a3941a3ac949`.
- Todas las referencias conservan la tolerancia de 40 ms.
- Ninguna voz ausente se sustituye por otra lengua.

Se genera con el builder existente y una carpeta nueva:

```sh
python3.12 -B scripts/build_guide_scenarios.py \
  --spec tests/fixtures/guide-scenarios/rapid-suite.json \
  --corpus-manifest output/guide-recognition-corpus-20261104/manifest.json \
  --output-dir output/guide-rapid-scenarios-20261104 --dry-run

python3.12 -B scripts/build_guide_scenarios.py \
  --spec tests/fixtures/guide-scenarios/rapid-suite.json \
  --corpus-manifest output/guide-recognition-corpus-20261104/manifest.json \
  --output-dir output/guide-rapid-scenarios-20261104 --execute
```

Primero debe ejecutarse el proveedor 9 sobre los 30 audios para conservar un
baseline. Después de modificar reconocimiento se repite exactamente este
conjunto con el mismo modelo en otra carpeta. El guion no se entrega a Whisper.
Se registran por evento texto literal, interpretación, anuncio y marca
esperados/obtenidos, error en milisegundos, omisiones y adicionales. Una
detección sólo puede satisfacer una referencia. Los fallos que sigan abiertos
permanecen visibles en el resultado final.

La cobertura del catálogo completo se mantiene en el conjunto anterior y el
corpus aislado de 121 voces. Este suplemento rápido ejercita seis familias
principales y cinco claves de indicación en combinaciones exigentes; no
pretende cubrir todas las familias y utilidades en todas las velocidades.
El procedimiento y los límites de la evidencia siguen definidos en
[GuideDetectionTestSet.md](../../../docs/okf/audio-systems/GuideDetectionTestSet.md).
