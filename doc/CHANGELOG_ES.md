# Registro de cambios — GPSDO FreeRTOS

[English](CHANGELOG_EN.md) | [Polski](CHANGELOG_PL.md) | **Español**

📖 [Inicio del proyecto](../README.md) · Volver al [README](README_ES.md)

Todos los cambios notables de este proyecto se documentan aquí.

Proyecto de **J. M. Niewiński** — <https://github.com/jmnlabs/GPSDO_FreeRTOS>
Basado en **GPSDO v0.06c** de André Balsa
(<https://github.com/AndrewBCN/STM32-GPSDO>), port a FreeRTOS y algoritmos
3–10 del autor, con Claude AI como asistente de programación y diseño de PCB
por Scrachi (foro EEVBlog).

El sufijo de versión `-rtos` marca el linaje del port a FreeRTOS.

> **Nota sobre la traducción.** Esta es la versión en español del registro de
> cambios. Las entradas más recientes se mantienen al día; para el máximo
> detalle histórico consúltense también las versiones en inglés
> (`CHANGELOG.md`) y polaco (`CHANGELOG_PL.md`).

---

## [v0.95-rtos] — 2026-07-16

### Añadido
- **Zonas horarias con horario de verano, en todo el mundo.** `TZ Adelaide`
  basta para que el reloj sea correcto, incluido su desfase de media hora y su
  DST del hemisferio sur. Los nombres de ciudad se aceptan solos: son únicos en
  toda la base IANA, así que la región es opcional (`TZ Australia/Adelaide`
  también funciona) y no importan las mayúsculas.

  La regla también puede escribirse completa:
  `TZ ACST-9:30ACDT,M10.1.0,M4.1.0/3`. Esa forma importa cuando un gobierno
  cambia las reglas y el firmware aún no lo sabe: el usuario lo corrige desde
  la CLI en lugar de esperar una versión.

  407 zonas y 88 reglas integradas, generadas desde la tzdata del sistema por
  `tools/gen_tz_table.py`. La base IANA completa ocupa ~2 MB, cuatro veces toda
  la flash de este MCU, y su valor real está en actualizarse varias veces al
  año, algo que un GPSDO sin internet no puede aprovechar. La cadena POSIX TZ a
  la que se reduce cada zona ocupa 4–44 bytes y recoge el mismo comportamiento
  actual. Coste: ~7 KB de flash.
- **`H TZ`** — primera página de ayuda por comando.
- **`TO` acepta minutos**: `TO 9:30`, `TO -3:30`, `TO 5:45`.
- **Vcc en pantalla (480×320).** Pedido por Dan Wiering. El raíl de 5 V ya se
  medía pero no tenía hueco; la celda `Alt` cede su mitad derecha y los campos se
  reagrupan: `qErr` sube junto a `Alt` (es lo que el receptor informa de su
  propio 1PPS) y `Vcc` ocupa el sitio que deja junto a `Vdd`, de modo que las
  alimentaciones quedan juntas. `Vdd` recupera su segundo decimal. Solo en 480:
  en 320 `Alt` y `qErr` piden ~168 px y la celda tiene 148.
- **`Vdd` solo se veía en compilaciones con LTIC.** Vive en la fila de fase, y la
  fila entera estaba tras `#ifdef GPSDO_LTIC`. Ahora los raíles quedan fuera de
  esa guarda: `Vcc` y `Vdd` se muestran siempre; solo el campo de fase sigue
  condicionado.

### Corregido
- **Reportado por Dan Wiering: la zona automática no detectaba el DST en
  Australia Meridional.** Dos errores distintos, solo uno visible. `TO A`
  deduce la zona de la longitud y aplica la regla europea, así que fuera de
  Europa no daba DST alguno. Pero además devolvía horas enteras, y Adelaide es
  UTC+9:30, así que el reloj erraba media hora incluso en invierno. India
  (+5:30), Nepal (+5:45), Terranova (−3:30) y Chatham (+12:45) sufrían el mismo
  error silencioso. `TZ <zona>` resuelve ambos; `TO A` sigue disponible sin
  cambios.
- **La lectura de frecuencia saltaba lateralmente en el panel 320×240.** v0.94
  quitó el ancho de campo de `dtostrf`; una cadena que pierde un carácter se
  recentra igualmente. El ancho de campo ha vuelto, como estaba desde v0.89. El
  panel 480×320 no se toca.
- **Los raíles laterales desaparecían junto a la frecuencia.** El sprite borra
  toda su banda y solo dibujaba la línea separadora superior. Ahora también
  lleva los raíles. Ambos paneles.
- **`CT` mostraba «Tune 0s» durante toda su ejecución.** No inicializaba la
  cuenta atrás, a diferencia de `C` y `LC`. Son 185 s.
- **`qErr` se desplazaba en el panel 480×320.** Anclar toda la cadena a la
  derecha fijó la unidad pero arrastró la etiqueta `qErr:` con los dígitos.
  Etiqueta y valor son ahora dos campos: la etiqueta pegada al borde izquierdo
  del hueco, el valor anclado a la derecha en 364.

### Cambiado
- **`dph` mostraba un número seguro mucho después de que el detector dejara de
  medir.** `ns_per_volt` es una pendiente local alrededor del ancla en 0.632·Vsat;
  pasada Vsat el pulso de parada ha fallado la ventana y el condensador carga
  hasta la alimentación. `dph` muestra ahora `ovf` fuera del 15–85% de Vsat.
  Vsat se recupera de `zero_offset`, que es 0.632·Vsat por construcción.
- **`dph` en pantalla nunca restaba el diente de sierra.** La pantalla calculaba
  la fase por su propia vía y se saltaba la corrección que el lazo aplica en
  `ltic_phase_error_ns()`: ~14 ns de dispersión 1σ medidos en el equipo. Ahora
  también la resta. Importa sobre todo fuera del algo 10, que es donde `dph` es
  la única vista de la fase real. El campo `qErr` deja de estar condicionado al
  algo 10.
- **Cada columna tiene una línea de alineación derecha (480×320).** La izquierda
  termina donde «hPa» en la fila BMP, la derecha donde «ns» en la fila de fase.
  `Vct`, `% rH` y la corriente del INA se anclan ahora a esas líneas. Las líneas
  se miden con `textWidth()` en el primer uso en vez de fijarse como constantes.
- **Las filas de sensores se agrupan por columna (480×320).** BMP y AHT ocupan
  la columna izquierda y los campos eléctricos la derecha: la fase arriba y los
  raíles justo debajo. `AHT` y `Vph`/`dph` intercambian su sitio.
- **`dPh:` pasa a `dph:`**, para coincidir con `Vph:`. Cambiado a la vez en el
  TFT y en el informe serie, que debían coincidir.
- **El aviso de survey-in pasa de la cabecera a la barra de estado.** Parpadeaba
  entre el nombre y el reloj, y en el panel de 480 no aparecía en absoluto. Ahora
  se añade a lo que la barra ya dice: `DISCIPLINED  FIX OK SURVEY` (`SV` en 320).
  La barra repinta todo su fondo antes de dibujar, así que la palabra no puede
  quedar recortada por el relleno de un campo vecino, y ahí no necesita parpadear
  para verse. La condición no cambia: aparece tras el timeout del survey-in si el
  receptor sigue midiendo, y desaparece al llegar el Time Mode.
- `g_time_offset` (int8, horas) pasa a `g_time_offset_min` (int16, minutos).
  `g_tz_auto` (bool) pasa a `g_tz_mode` (manual / auto-EU / POSIX).

### EEPROM
- Bloque de zona horaria en `[234..284]`.
- **Los ajustes existentes se migran automáticamente, sin reset de fábrica.**
- **Volver a una versión anterior es unidireccional**: v0.94 leerá un desfase
  sensato en horas enteras, pero una regla `TZ` no cabe allí y se perderá.

### Documentación
- Movida a [`doc/`](../doc/); los archivos en inglés llevan sufijo `_EN` para que
  los tres idiomas se nombren igual. El `README.md` raíz es ahora un índice breve
  que GitHub muestra en la página del proyecto.
- Las guías de puesta en marcha del flash-ring eran huérfanas; ahora llevan la
  misma navegación de idiomas que el resto.
- Su cifra de presupuesto de flash llevaba cinco versiones sin actualizar
  (~170 KB en v0.90). Ahora indica 216976 B (212 KB) en v0.95, ~44 KB de
  margen — medido, no estimado. La guía advierte además de que el porcentaje
  del IDE cuenta sobre los 512 KB completos: «41%» es en realidad el 83%.

### Notas
- `tz_table.h` se genera. Vuelve a ejecutar `tools/gen_tz_table.py` cuando se
  actualice tzdata.
- Africa/Casablanca y Africa/El_Aaiun degradan a su desfase estándar con aviso:
  su DST sigue el ramadán, que el formato POSIX no puede expresar.

---
## [v0.94-rtos] — 2026-07-15


### Corregido
- **El campo de frecuencia en 320×240 seguía dibujándose con las fuentes GFX.**
  La v0.93 devolvió el panel pequeño a las fuentes clásicas, pero la corrección
  solo llegó a la ruta de dibujo directo — y esa ruta nunca se ejecuta, porque
  los sprites se crean en *ambos* paneles, no solo en el de 480×320. La rama de
  sprite seguía con `GF_FREQ`/`GF_STATUS` codificados, así que la lectura (y
  `no signal`) se seguía renderizando en FreeMono. Ahora pasa por las mismas
  macros `TFT_FONT_*` que todo lo demás.
- **La frecuencia temblaba lateralmente en el panel 480×320.** La lectura
  estaba centrada, así que cualquier cambio de longitud movía todos los
  caracteres: la ventana de promediado cambia los decimales, y
  10000000.0000 → 9999999.9999 pierde un carácter entero, repartiendo el
  centrado esa diferencia entre ambos extremos. La lectura se ancla ahora por
  su borde derecho en x=464, elegido para que la nominal `10000000.0000 Hz`
  (16 caracteres × 28 px de ancho fijo = 448 px) siga cayendo justo en el
  centro, dejando 16 px de aire a cada lado. «Hz» ya no se mueve; solo los
  dígitos. Los mensajes de estado siguen centrados — usan la fuente
  proporcional, donde no hay columnas que alinear.

### Cambiado
- **El marco es blanco en ambos paneles.** Además de igualar al panel grande,
  esto es lo que permite que el sprite de datos de 1 bit lleve el marco él
  mismo: ese sprite tiene exactamente dos colores (blanco y fondo), así que el
  marco azul marino no podía dibujarse *dentro* de él y había que repintarlo en
  el panel tras cada envío. El blanco significa que marco y texto salen ahora
  juntos en una transferencia atómica, en ambos tamaños. El separador de la
  cabecera se movió al sprite de frecuencia por la misma razón (su paleta de
  4 bits ya contiene blanco).
- **El splash ya no usa las fuentes GFX.** Era el último reducto de GFX en el
  panel pequeño, lo que obligaba a quien actualizara desde la v0.92 a añadir
  `LOAD_GFXFF` a `User_Setup.h` o ver el subtítulo reducirse a una sola «p» —
  un fallo críptico a cambio de una mejora cosmética. El subtítulo usa ahora la
  fuente clásica 4 (que lleva el alfabeto completo — las fuentes 6/8 son las
  que no tienen letras) y los créditos la fuente 1 en ambos paneles. **Una
  compilación 320×240 solo necesita ahora `LOAD_GLCD`, `LOAD_FONT2` y
  `LOAD_FONT4`**; `LOAD_GFXFF` se requiere únicamente para la de 480×320. Las
  macros huérfanas `GF_TITLE`/`GF_SUB`/`GF_CREDIT` y la rama muerta de 320 del
  bloque `GF_*` desaparecen con ello.
- Las etiquetas de la barra de estado bajan 2 px en el panel 320×240. Van en
  mayúsculas, así que el espacio para descendentes al pie de la caja del glifo
  está vacío y el centrado geométrico se lee alto; el desplazamiento centra lo
  que el ojo realmente ve. El panel 480×320 no cambia.
- Subida de versión a v0.94-rtos, incluidas las cabeceras de cada archivo (que
  aún indicaban v0.92).

## [v0.93-rtos] — 2026-07-14

### Corregido
- **Las cuentas atrás iban más lentas que el reloj.** El calentamiento del OCXO
  y las calibraciones medían sus segundos con `vTaskDelay(1000)`, que duerme
  *durante* un segundo en vez de *hasta* el siguiente — así que las lecturas del
  ADC, las impresiones por serie y cualquier apropiación se sumaban encima, y la
  cifra mostrada se retrasaba respecto al tiempo real (tanto más cuanto más
  cargado el sistema). Ambas usan ahora `vTaskDelayUntil`, que absorbe el tiempo
  de trabajo y mantiene cada paso como un segundo real. La cuenta atrás de
  calibración además se paraba en 1 en lugar de llegar a 0.
- **Un survey-in que sobrevive a su ventana de monitorización ya no es
  invisible.** Cuando salta el tiempo de seguridad, el firmware deja de
  consultar pero el receptor sigue haciendo el survey («continuing anyway» en el
  registro) — y con la banda de frecuencia de vuelta mostrando la frecuencia,
  nada en pantalla lo decía. Un `SURVEY` de pulso lento se sitúa ahora en la
  cabecera entre la versión y el reloj, y se apaga solo cuando el receptor
  informa de Time Mode (`HDOP: TIME`), que es la señal real de finalización del
  survey.
- **`qErr` dejaba caracteres residuales en el panel ILI9488** (visto como
  `qErr: -1.6 nsss`). El relleno de texto del campo era de 55 unidades de autor
  (~82 px), mientras que el valor más ancho, `qErr: -21.3ns`, necesita ~104 px
  en FreeSans 9pt — TFT_eSPI solo repinta el fondo bajo el relleno, así que la
  cola de la cadena anterior, más larga, sobrevivía. Relleno ampliado a 75
  unidades (~112 px), que cubre el texto y sigue despejando el campo `Vdd`
  anclado a la derecha.
- **Vctl / Vcc / Vdd marcaban 0,000 V durante todo el calentamiento del OCXO.**
  Esas medias del ADC se muestrean en el bucle principal de la tarea de control,
  pero `do_warmup()` se ejecuta *antes* de entrar en ese bucle y solo dormía —
  así que nada las llenaba. La cuenta atrás del calentamiento ahora muestrea los
  mismos tres canales cada segundo, igual que ya hace `wait_secs_pwm()` durante
  la calibración.
- **La lectura de frecuencia quedaba a la derecha del centro y saltaba de
  lado.** El valor se formateaba con `dtostrf(..., 14, ...)`, rellenándolo por
  la izquierda hasta 14 caracteres; `MC_DATUM` centraba luego la cadena
  *incluyendo* esos espacios invisibles, así que los dígitos visibles quedaban
  ~40 px a la derecha del centro — y como el número de espacios varía con la
  ventana de promediado (1–4), la lectura se desplazaba al cambiar la precisión.
  Se elimina el ancho de campo: `GF_FREQ` es FreeMonoBold, que ya mantiene los
  dígitos en columnas fijas, así que el relleno no aportaba nada. Con él
  desaparece el apaño del espacio final en el panel de 480.

### Cambiado
- **Los paneles 320×240 vuelven a las fuentes clásicas en la pantalla de
  trabajo.** v0.92 pasó todos los paneles a las fuentes libres GFX; en 480×320
  fue una mejora clara, pero a 320×240 los tipos proporcionales son demasiado
  anchos para una maquetación pensada para las numéricas — los valores se salían
  de sus columnas hacia la vecina y el divisor central cortaba lo que
  desbordaba. Tampoco había un tipo menor al que recurrir (FreeSans empieza en
  9 pt; por debajo solo está el ilegible TomThumb de 3×5). El panel pequeño usa
  ahora la fuente 2 para la cabecera y la rejilla, la 4 para la barra de estado
  y la 1 ×3 (ancho fijo) para la frecuencia, mientras que el splash mantiene GFX
  en ambos paneles. Las macros `TFT_FONT_*` eligen esto en tiempo de compilación
  — sigue habiendo una maquetación, no dos. El divisor central de columnas es
  ahora solo de 480 (a 320 no hay sitio), y el marco vuelve al azul marino en el
  panel pequeño.
- **Las regiones vivas de la pantalla usan doble búfer con sprites.** La
  cabecera, la banda de frecuencia y el área de datos se renderizan cada una en
  un `TFT_eSprite` en RAM y se envían al panel en una sola transferencia SPI
  continua, en vez de borrar el panel con `setTextPadding` y dibujar encima. Ese
  borrar-y-luego-dibujar se veía como un parpadeo una vez por segundo, sobre
  todo en el panel 480×320, donde limpia 2,4× los píxeles. Las paletas lo
  abaratan (4 bits cabecera/frecuencia, 1 bit datos; ~25 KB en total en el panel
  grande). Si `createSprite()` falla con un montón fragmentado, cada banda
  recurre al dibujo directo — vuelve el parpadeo pero nada se rompe; el registro
  de arranque indica qué ruta está activa.
- **Los mensajes de estado ahora se escriben completos y nombran qué
  calibración corre.** `WARMUP 285s` → `OCXO warmup 285s`, `SVIN 120s 5m` →
  `Survey 120s +/-5m`, y el ambiguo `CAL 245s` pasa a ser `Calibrate`, `Tune` o
  `LTIC cal` — C, CT y LC tardan tiempos muy distintos, así que una cuenta atrás
  a secas decía poco al operador. Ambos paneles. Ojo: las dos cifras son de
  distinta naturaleza: el calentamiento y las calibraciones cuentan hacia abajo,
  mientras que el survey-in cuenta hacia arriba (el receptor informa del tiempo
  transcurrido, y la finalización depende también de la precisión, así que una
  cifra de «restante» sería una conjetura).
- **`SPI_FREQUENCY 40000000` es ahora el ajuste documentado** (el README decía
  27 MHz mientras `gpsdo_config.h` ya indicaba 40). El SPI1 del F411 llega hasta
  50 MHz, así que 40 deja margen; importa sobre todo en el panel 480×320, donde
  un envío de sprite es una única transferencia cuya duración escala con el
  reloj. Baja a 27 MHz si los cables largos dan problemas.
- **Las líneas de créditos del splash ganan interlineado en el panel 480×320.**
  El hueco de 12 unidades de autor escala allí a solo ~16 px, y los créditos son
  FreeSans 9pt (~13 px de alto), así que ambas líneas se fundían visualmente. El
  panel grande usa ahora un hueco de 16 unidades (~21 px, interlineado ~1,6×);
  el panel 320×240 mantiene 12, que encaja con su fuente 6×8.
- `dPh:` y `qErr:` en el ILI9488 pierden el espacio antes de su unidad `ns`.
- Incremento de versión a v0.93-rtos.

## [v0.92-rtos] — 2026-07-12


### Cambiado
- **Splash simplificado y proporciones de la pantalla de operación afinadas.**
  Se eliminó el gran título verde «GPSDO» de la pantalla de inicio; el subtítulo
  «GPS Disciplined OCXO» ahora aparece elevado en la parte superior, como en el
  diseño original 320×240. En la pantalla de operación, el texto de la cabecera
  se redujo al tamaño de la fuente de datos, la barra de estado inferior se
  redujo a la mitad de su altura con una fuente de estado más pequeña, y el
  espacio recuperado pasó a un mayor interlineado entre las filas de telemetría
  (paso de fila 17→20 autoral) para que la rejilla respire. La fuente de datos
  se mantiene en FreeSans 9pt.
- **Todo el texto del TFT migrado a las fuentes libres Adafruit GFX (GFXFF).**
  La cabecera, el gran indicador de frecuencia, la rejilla de datos, la barra de
  estado y el título/subtítulo de la pantalla de inicio se dibujan ahora con
  FreeSans / FreeMono en lugar de las clásicas fuentes GLCD numéricas. Esto
  corrige un error de larga data en el que las cadenas con letras dibujadas con
  las fuentes numéricas (6/8, que solo contienen `0-9 . : - a p m`) se reducían a
  un único glifo — de forma más visible el subtítulo «GPS Disciplined OCXO» que
  aparecía como una sola «p», y la etiqueta de la barra de estado que quedaba en
  blanco sobre una barra de color. Una capa de fuentes por rol y por panel
  (`GF_DATA` / `GF_HEAD` / `GF_STATUS` / `GF_TITLE` / `GF_SUB` / `GF_FREQ` en
  `gpsdo_config.h`) selecciona automáticamente FreeSans 9/12 pt, FreeSansBold
  12/18/24 pt y FreeMonoBold 18/24 pt para los paneles 320×240 y 480×320, de modo
  que el mismo código de diseño sirve para ambos. La frecuencia grande usa
  FreeMonoBold para que sus dígitos mantengan ancho fijo y no se desplacen al
  cambiar el valor.
- **Requiere `#define LOAD_GFXFF` en `User_Setup.h`** (ver README). Las antiguas
  líneas `LOAD_FONT2/4/6/8` ya no son necesarias; `LOAD_GLCD` se conserva solo
  para las dos líneas de créditos en letra pequeña de la pantalla de inicio.
- **Diseño de la pantalla de operación recalculado geométricamente para
  480×320.** Los límites de las bandas (frecuencia, rejilla, sensores, estado)
  se recalcularon para que las filas más altas de las fuentes proporcionales
  nunca crucen un separador en ninguno de los paneles, ambas columnas de datos
  llenan todo el ancho con un tenue divisor central, y la barra de estado llena
  toda la banda hasta el borde inferior de la pantalla (sin franja de color
  muerta bajo la etiqueta). Los valores de la rejilla se anclan con datum
  derecho, de modo que los anchos cambiantes quedan fijos en lugar de desplazarse.
  Verificado en el panel ILI9486 480×320.

### Corregido
- **Eliminado el texto obsoleto «aún no implementado / phase A» de la CLI y la
  telemetría.** `LA` con un valor incorrecto decía "0..9 (10=LTIC, not yet
  available)", `LL` imprimía "(loop not yet implemented — phase A)", y la
  ayuda/comentarios aún describían el algo 10 como una vista previa sin
  implementar. El algoritmo 10 disciplina el lazo desde hace muchas versiones;
  ahora todo describe el lazo de fase de 3 etapas ACQ→DPLL→LOCK en
  funcionamiento. (`Vdd:` en el TFT también ganó un espacio antes de su valor
  para coincidir con las demás etiquetas.)
- **Las animaciones del spinner LED (calentamiento / survey-in / calibración)
  iban ~5× demasiado lento y a saltos.** La tarea de pantalla despierta con la
  notificación PPS de 1 Hz, pero los spinners avanzan de fotograma cada 200 ms
  — así que con el despertar cada 1100 ms solo avanzaban una vez por segundo. La
  tarea ahora despierta ~cada 150 ms mientras hay una animación activa (y
  mantiene el ritmo lento de 1100 ms si no, ya que el reloj solo cambia una vez
  por segundo). Para que el despertar más rápido no reenvíe segmentos idénticos
  por el TM1637 bit-bangeado por software (~5–8 ms por escritura), una pequeña
  caché de escritura (`tm_set`) omite la transferencia cuando el patrón no
  cambia. Es una corrección de planificación/caché — sin DMA; DMA sigue siendo
  un paso futuro aparte para la ruta TFT SPI.
- **Un suelo de amortiguación elevado no surtía efecto tras reflashear — el
  enganche oscilaba LOCK↔DPLL.** El multiplicador de amortiguación se persiste
  en el anillo Flash (datos vivos) y se restaura al arrancar. La Flash escrita
  por un build con el suelo antiguo de 0,30 recargaba por tanto damp = 0,30
  incluso tras subir el suelo a 0,45, y como damp solo se adapta en cruces del
  ciclo límite, se quedaba ahí atascado — el lazo corría al 30 % de autoridad,
  la fase subía por encima del umbral de enganche y el lazo cazaba LOCK↔DPLL
  cada ~30 s (visto en hardware). El damp restaurado se acota ahora a la banda
  legal actual al cargar (anillo Flash y EEPROM), así que un reflasheo surte
  efecto de inmediato. Los límites de damp se movieron a la cabecera compartida
  para que el almacenamiento y el aprendiz coincidan.
- **El TFT ahora muestra qErr, y la fase recibe una etiqueta `dPh:`.** En algo
  10 con SAW activo, el hueco derecho de la fila de sensores encabeza con el
  diente de sierra del receptor `qErr:…ns` y sigue con `Vdd:` acortado a 1
  decimal. Ambos se dibujan por separado — qErr a la izquierda, Vdd anclado al
  borde derecho de la pantalla — así que Vdd ya no se desplaza lateralmente
  cuando qErr cambia de ancho. Con SAW apagado se muestra solo Vdd a plena
  precisión, aún anclado a la derecha. La fase LTIC de la izquierda se etiqueta
  `dPh:±…ns` (sin espacio tras `Vph:`) para una lectura más clara y coherente;
  el informe serie usa la misma etiqueta `dPh:` tras `Vphase:` para que
  coincidan. qErr y dPh usan un campo de ancho fijo con signo (signo siempre visible,
  magnitud justificada a la derecha), así que los dígitos y las unidades se
  quedan quietos en vez de saltar de lado al cruzar cero o cambiar de número de
  cifras.
### Corregido
- **LOCK podía perder el enganche con un OCXO a la deriva — ahora lleva un
  término de frecuencia suave.** En la rama normal de LOCK la ruta de frecuencia
  estaba desactivada (freq_term = 0), así que la única defensa contra una deriva
  real del OCXO era el lento feed-forward de deriva. En hardware caliente se
  retrasaba mucho y la fase se salía de la ventana de enganche (11 → −425 ns en
  51 s, luego LOCK→DPLL→ACQ). LOCK aplica ahora un término ligero de 0,1×Kp —
  suficiente para cancelar la deriva viva en cada actualización, lo bastante
  suave para no inyectar ruido de TIM2 en un enganche silencioso. Se combina con
  el feed-forward más rápido (abajo). Análisis de causa raíz: GML-5.2.
- **Eliminado el ciclo límite en ACQ (oscilación de PWM de ±150 LSB).** El
  algoritmo 10 tomaba su error de frecuencia de avg10 (cuantización de 0,1 Hz);
  por Kp (~1550 LSB/Hz) producía saltos de PWM de ±150 LSB en un ciclo de ~10 s,
  que impedían que la fase se asentara bajo el umbral de enganche y ralentizaban
  la adquisición. Ahora usa avg100 (0,01 Hz), 10× más fino, y la adquisición se
  asienta limpiamente. Análisis: GML-5.2.
- **El feed-forward de deriva ahora hace bootstrap tras el enganche.** Su primera
  ventana de aprendizaje era lenta (30 s), así que una deriva rápida tras el
  enganche escapaba antes de que se moviera. Ahora corre tres ventanas rápidas de
  8 s con un paso mayor justo tras el enganche (absorbiendo la deriva en
  ~10–20 s) y luego se relaja al régimen silencioso de 30 s.
- **Suelo de amortiguación subido 0,30 → 0,45.** El aprendiz podía amortiguar
  tan fuerte que el lazo tenía solo el 30 % de autoridad de corrección y no podía
  seguir la deriva; 0,45 aún amortigua un ciclo límite pero conserva autoridad
  para seguir.

### Cambiado
- **El anclaje de calibración LC es ahora universal — 0,632·Vsat, derivado por
  placa.** El detector es una rampa de carga RC V(φ) = Vsat·(1 − e^(−φ/τ)); el
  punto φ = τ, donde V = 0,632·Vsat, es la misma altura fraccional en todo
  detector exponencial sin importar Vsat. LC recupera ahora Vsat con un ajuste
  1-D (linealizar −ln(1 − V/Vsat) frente a t, elegir el Vsat de menor residuo) y
  ancla ahí. El 1,85 V fijo anterior solo funcionaba porque los detectores de
  Marek y Dan Wiering tienen ambos Vsat ≈ 2,93 V; un detector con otro Vsat
  habría perdido la banda. LC se autoadapta ahora por placa sin configuración, y
  `LTIC_ZERO_ANCHOR_V` queda retirado. Verificado en ejecuciones registradas:
  Vsat recuperado al ~0,3 %, los anclajes concuerdan al ~0,8 % entre ejecuciones.
  Física y derivación: GML-5.2.
- **El subtítulo del splash** ahora dice `GPS Disciplined OCXO` (espacio, no guión).
- **Calibración LC — punto de trabajo anclado + ns/V por pendiente local (Opción D).**
  El detector de fase de rampa es exponencial (1k/1n, τ≈1 µs), así que ns/V no es
  constante a lo largo de la rampa, y un promedio de todo el tránsito (range/span)
  variaba ~15–20 % entre ejecuciones — según dónde el arm del picDIV aparcara la
  fase. ns/V se toma ahora de la pendiente LOCAL dV/dt en una ventana de ±0,20 V
  alrededor de un punto de trabajo fijo (LTIC_ZERO_ANCHOR_V = 1,85 V). zero_offset
  se ancla en ese punto — el centro repetible de la rampa, lejos de las zonas
  muertas del detector medidas por Dan Wiering (la caída del Schottky + pull-down
  por debajo de ~0,05 V, y el riel/wraparound del ADC cerca de 3,3 V). Si un
  barrido nunca cruza la banda de anclaje, el código recurre al antiguo promedio
  range/span y lo indica.

  Hallazgos de banco en varias ejecuciones LC con resolución de 1 s:
  * El anclaje es exacto — ejecuciones consecutivas sitúan zero_offset en
    1,8500 V cada vez.
  * La dispersión de ns/V entre ejecuciones cayó de ~15–20 % (antiguo promedio
    range/span) a unos pocos por ciento. Con ambas ejecuciones barridas a la
    MISMA tasa es ~2,8 %; el residuo lo domina la cuantización de la tasa de
    barrido, no el ajuste de pendiente — avg100 resuelve la tasa a 1 ns/s, así
    que una etiqueta «−5» vs «−6» lleva ±0,5 ns/s y las bandas de confianza de
    ambos ns/V se solapan. Esto no perjudica a LOCK: el lazo usa el ns/V exacto
    que midió, a la tensión en la que realmente trabaja.
  * La ventana de ajuste se amplió a ±0,20 V (LTIC_ANCHOR_WIN_V): más puntos en
    la banda (~70 vs ~35) promedian el ruido del ADC, reduciendo la dispersión a
    igual tasa de ~5,9 % con ±0,10 V a ~2,8 %.
- **Registro de diagnóstico LC por segundo.** Durante el barrido de muestreo, LC
  imprime ahora una línea `t=/V=/n=` por segundo, haciendo visible toda la rampa
  en una captura (se usó para derivar la Opción D).

### Corregido
- **El informe serie se imprimía dos veces por segundo en RD/RH con fix del GPS.**
  vDisplayTask recibe notificación de dos fuentes de ~1 Hz — el relé de frecuencia
  (por PPS) y el parser del GPS (por sentencia de tiempo) — así que con fix se
  despertaba dos veces por segundo y emitía dos líneas de informe. La línea serie
  se controla ahora por un cambio del contador de PPS, de modo que se imprime
  exactamente una por segundo; la pantalla sigue refrescándose en cada
  notificación. Reportado por Dan Wiering.
- **Ortografía del nombre en los agradecimientos** corregida a «Wiering» (a
  petición del autor).
- **La lectura de fase `Vph` del TFT era código muerto, y erróneo si se activaba.**
  Dependía de la constante de compilación `LTIC_NS_PER_VOLT` (0 por defecto, así
  que el valor en ns nunca aparecía una vez calibrado el detector) y, de haberse
  fijado, calculaba `V × ns_per_volt` desde 0 V en vez de relativo a `zero_offset`.
  Ahora usa los `g_ltic.ns_per_volt` y `zero_offset` MEDIDOS por LC, mostrando una
  fase con signo `(V − zero_offset) × ns/V` que coincide con el error del lazo, o
  solo los voltios cuando no está calibrado.
- **CT rechazaba OCXOs de span estrecho (mejores).** El chequeo de ganancia
  del planta limitaba K a 0,1 mHz/LSB, pero un span EFC estrecho es deseable —
  menos Hz/LSB significa más resolución y es una vía hacia E-12. El equipo de
  Dan Wiering mide 0,048 mHz/LSB (~1,05 V de span EFC) y se rechazaba
  erróneamente. Límite inferior bajado a 0,02 mHz/LSB; ahora solo se rechazan
  ejecuciones de ruido/sin-GPS.
- **Correcciones del diseño ILI9488 (480×320) — a partir de fotos de usuarios,
  aún no verificadas en un panel.** Los primeros usuarios Dan Wiering y lucido
  enviaron fotos de sus montajes 480×320. Se abordaron varios problemas a
  partir de esas imágenes sin un ILI9488 a mano: (1) la fuente del cuerpo se
  sobre-escalaba — TFT_F mapeaba fuente 2→4 (creciendo 1.63× mientras las filas
  escalan solo 1.33×), así que las líneas se solapaban verticalmente y la barra
  de estado se salía de la pantalla; la fuente del cuerpo se mantiene ahora en
  2. (2) La fila del sensor BMP se acortó (temperatura y presión a 1 decimal)
  para que los glifos escalados más anchos no invadan la columna AHT. (3) A las
  instrucciones de `User_Setup.h` les faltaba `LOAD_FONT8`, que necesita la
  lectura de frecuencia — sin ella esa línea queda en blanco. Son correcciones
  «al mejor criterio» a partir de fotografías; un pase final de geometría
  seguirá cuando haya un panel ILI9488 a mano. Los paneles pequeños 320×240 no
  se ven afectados (TFT_F es identidad ahí).
- **LOCK podía perder el enganche con un OCXO a la deriva (rebote LOCK→DPLL→ACQ).**
  Con una deriva de frecuencia real (~8,5 ns/s medidos en hardware caliente), la
  fase se salía de la ventana de enganche — 11 → −425 ns en 51 s — mientras la
  corrección era demasiado débil para seguirla: el aprendiz de amortiguación
  había tocado suelo en 0,30 (corrección al 30 % de autoridad) y el
  feed-forward de deriva aún recogía su primera ventana de 30 s, así que nunca
  se movió antes de perder el enganche. Dos cambios: el suelo de amortiguación
  se subió 0,30 → 0,45 (conserva autoridad para seguir la deriva sin dejar de
  amortiguar un ciclo límite), y el feed-forward ahora hace BOOTSTRAP tras el
  enganche — tres ventanas rápidas de 8 s con un paso mayor absorben la deriva
  en ~10–20 s, luego se relaja al régimen lento y silencioso de 30 s. Simulado
  sobre la deriva registrada, la fase ahora se mantiene cerca de −125 ns en vez
  de escaparse. Los montajes estables de baja deriva (p. ej. con referencia Rb)
  no se ven afectados — el bootstrap converge de inmediato y el suelo más alto
  sigue siendo amortiguación neta.
- **Fase en ns añadida al informe serie**, tras `Vphase:`, una vez que LC ha
  calibrado el detector — `(V − zero_offset) × ns/V`, la misma convención que
  el lazo y la fila del TFT.
- **El anclaje de LC es ahora el punto medio medido de la rampa, no 1,85 V fijo.**
  El anclaje de pendiente local estaba fijado a la banda del detector de Marek;
  un equipo cuya rampa barre otro rango (el de Dan va más bajo, ~1,3 V) perdía
  la ventana de anclaje por completo y recurría al promedio range/span (resultado
  "débil"). El anclaje es ahora `vlow + span/2` del barrido real, usando la
  constante `LTIC_ZERO_ANCHOR_V` solo cuando cae dentro de la banda barrida. LC
  se autoadapta por placa.
- **La presión en el TFT podía invadir la columna AHT.** La presión BMP se
  imprimía con 2 decimales (`1013.25hPa`), que con presiones de 4 dígitos se salía
  de la columna izquierda. Reducida a 1 decimal (`1013.2hPa`), igual que el
  informe serie.
- **Rebote de LOCK en arranque en caliente (desperdiciaba ~1 min de los ~8 min
  de arranque a lock).** Un LOCK/DPLL persistido se reanudaba mientras la lectura
  de fase fuera válida (en la rampa), aunque estuviera lejos de zero_offset —
  p. ej. Vphase ≈2,09 V frente a un anclaje de 1,85 V (~260 ns de desvío). LOCK
  se enganchaba, DPLL juzgaba la fase demasiado lejos un minuto después y caía
  hasta ACQ, así que el pull-in completo corría igualmente tras un desvío inútil.
  La protección de arranque ahora degrada un LOCK/DPLL persistido a ACQ salvo que
  la fase sea válida Y esté dentro de la ventana ACQ de zero_offset. El arranque
  en frío no se ve afectado (el estado por defecto es ACQ); un arranque en
  caliente genuinamente centrado sigue reanudando LOCK de inmediato.

---

## [v0.90-rtos]

### Añadido
- **Búfer en anillo en Flash con nivelado de desgaste para datos "vivos".** La
  deriva/amortiguación aprendidas, la calibración LC y el último PWM se
  auto-guardan ahora en un sector de Flash dedicado (sector 6, 0x08040000,
  128 KB) como un anillo de slots de 32 bytes. Cada guardado programa el
  siguiente slot vacío; el sector se borra solo cuando el anillo da la vuelta
  (una vez cada 4095 guardados), así que a 100 guardados/día el Flash dura del
  orden de mil años. Cada slot lleva un CRC y un número de secuencia; un slot
  escrito a medias (corte de energía) falla el CRC y se usa el anterior válido.
  Una cabecera con firma + versión de formato hace el firmware robusto frente a
  borrado de chip completo, programación solo por sectores, primer arranque y
  restos de basura en Flash por igual (un sector ajeno o en blanco se detecta y
  reinicializa).
- **Auto-guardado con histéresis.** Los datos vivos se escriben solo cuando se
  han asentado en un nuevo nivel: la deriva cambió > 8 LSB o la amortiguación
  > 0,03, Y han pasado al menos 20 min desde el último guardado. Una calibración
  `LC` exitosa guarda de inmediato.
- **Comando `FR 0|1`** (guardado con `ES`, activo por defecto) conmuta el búfer
  en anillo en tiempo de ejecución — sin flag de compilación, así que sin
  sorpresas de caché de compilación. `FR 0` detiene toda actividad del anillo.
- **Comando `EW`** muestra diagnósticos de desgaste del Flash: ciclos de borrado
  y slots usados.
- **Corrección de diente de sierra (qErr) para LTIC (`SAW 0|1`).** Los receptores
  de temporización u-blox generan el 1PPS dividiendo un reloj interno, así que
  cada pulso cae hasta un periodo de reloj lejos del tiempo GPS real — un error
  de cuantización por pulso que el receptor reporta como `qErr` en UBX-TIM-TP.
  Un sniffer pasivo parsea ese mensaje (qErr es un campo con signo de 32 bits en
  picosegundos, en el mismo offset en LEA-6T, LEA/NEO-M8T y ZED-F9T, así que un
  solo parser sirve para todos) y la ruta de fase del TIC lo resta, eliminando
  el diente de sierra de granularidad del receptor y dejando el error propio del
  OCXO. En un LEA-6T (21 ns de granularidad) este es el término de fase de corto
  plazo dominante. TIM-TP se habilita automáticamente al iniciar el GPS; `SAW`
  conmuta la corrección (guardada con `ES`, desactivada por defecto) y muestra
  qErr en vivo.

### Cambiado
- **`ES` ya no sobrescribe los valores aprendidos/de calibración con el anillo
  activo.** Con `FR 1`, la calibración (ns_per_volt, zero_offset, range_ns,
  centre_v) y la deriva/amortiguación aprendidas pertenecen exclusivamente al
  anillo; `ES` escribe solo ajustes genuinos (ganancias PID, umbrales, flags).
  Con `FR 0`, `ES` sigue guardando esos valores vivos en EEPROM como respaldo, y
  `eeprom_recall()` los siembra al arrancar, de modo que migrar una EEPROM
  antigua conserva su calibración.

### Corregido
- **`LC` ya no pelea con el lazo de disciplina.** Ejecutar `LC` mientras el
  algoritmo 10 disciplinaba activamente permitía que el lazo moviera el PWM al
  mismo tiempo que el barrido de calibración, de modo que ambos se corrompían —
  la tasa de barrido medida salía a ±1 ns/s y el rango como valores absurdos
  (1502 / 3518 ns), que la comprobación física rechazaba correctamente. El lazo
  de control se suprime ahora siempre que hay una calibración activa
  (`g_calib_active`), así que `LC` puede ejecutarse en cualquier momento,
  incluso bajo `LA 10`.
- **Rutas de PWM seguras durante la calibración.** La misma protección cubre
  ahora también el pilotaje de holdover térmico del algoritmo 9 y los comandos
  manuales de PWM (`up1`/`up10`/`dp1`/`dp10`/`SP`), que se rechazan con un
  mensaje claro mientras corre `LC`/`CT`, de modo que ninguna ruta pueda
  perturbar un barrido en curso.
- **Que `LC` no dé la vuelta ya no se marca como fallo.** Un detector que no da
  la vuelta dentro del barrido ahora pasa con buena pendiente/centro/span y se
  auto-guarda; solo un resultado genuinamente débil (span diminuto o centro
  fuera de banda) se señala, con el motivo específico. Los mensajes ya no piden
  al usuario ejecutar `ES` tras `LC` — un `LC` exitoso auto-guarda en el anillo
  Flash (esto son datos vivos). `CT` sigue pidiendo `ES`, ya que ajusta valores
  PID.

### Créditos
- Atribución afinada: André Balsa acreditado como autor de v0.06c, la
  inspiración del port a RTOS. Enlace del repositorio corregido.

---

## [v0.89-rtos]

### Añadido
- **Ayuda de lazo auto-aprendida (`LRN`), compartida por el algoritmo 7 y LTIC.**
  Dos aprendices lentos y pasivos — informados por las trazas nocturnas
  referenciadas a Rb de Dan Wiering (un diente de sierra de fase de ~9000 s
  ±80 ns, un bache de ADEV en la constante de tiempo del lazo, y deriva de
  8E-12/día): (1) un **feed-forward de deriva** que estima la pendiente media de
  fase del OCXO en ventanas de 30 s y añade un término de PWM para cancelarla,
  de modo que el lazo deja de perseguir un objetivo móvil y la fase se aplana;
  (2) una **adaptación de amortiguación** que vigila los cruces por cero del
  error de fase y baja la ganancia de corrección ante sobreoscilación, la sube
  cuando va lento — aplanando el bache de ADEV en la constante de tiempo del
  lazo. Ambos corren SOLO en lock, se actualizan como mucho cada 30 s, y están
  fuertemente acotados (feed-forward ±400 LSB, amortiguación 0,5–1,5) de modo
  que una mala estimación no pueda desestabilizar el lazo; ninguno inyecta
  excitación. `LRN 1|0` activa/desactiva (activo por defecto), `LRN R` restaura
  a la teoría, `LRN` a secas imprime el estado en vivo; los valores aprendidos
  se guardan con `ES` (EEPROM 222–230) y se recuperan al arrancar. El informe
  serie muestra una línea `Learn:` en vivo (deriva, pendiente, amortiguación,
  periodo/amplitud del ciclo límite observado).
- **El aprendizaje cubre ahora todos los algoritmos de disciplina (3–10), no
  solo 7/8.** Un único envoltorio `lrn_apply()` alimenta el acumulador de fase y
  el error de frecuencia propios de cada lazo a los aprendices; la NN (algo 9),
  al no tener acumulador de fase explícito, usa solo amortiguación. El estado de
  `LRN` se comparte entre algoritmos.

### UI / Pantalla
- **TFT en color reelaborado para claridad y un poco de vida.** Formato de
  etiquetas consistente con un solo espacio en todo (`Alt: 144m`, `PWM:...`,
  `Uptime: ...`); los campos de valor se alinean ópticamente en la fuente
  proporcional. Un marco azul marino (a juego con la cabecera) enmarca ahora el
  área de datos, con los tres separadores unidos por rieles laterales. La
  frecuencia se vuelve verde en lock. Etiqueta `DATE:` añadida.
- **Splash de arranque refinado**: título a la altura de la frecuencia, dos ondas
  de oscilador que aparecen desfasadas, derivan hasta coincidir y se fusionan en
  una sola onda verde con un halo que crece y se desvanece, seguido de una lista
  de detección de hardware con scroll (ventana de altura fija, los créditos se
  quedan en su sitio).
- **Comando `SPL 0|1`** (guardado con `ES`, 1 por defecto) conmuta la animación
  de arranque. `SPL 0` muestra solo el título y los créditos durante dos
  segundos — para los indiferentes al arte.

---

## [v0.88-rtos]

### Corregido
- **El campo de frecuencia del TFT ya no conserva restos de dígitos tras los
  mensajes CAL/WARMUP/SVIN.** Los mensajes de ocupado y la frecuencia grande
  usan alturas de texto distintas, así que el relleno de texto borraba solo la
  banda de la fuente actual; ahora todo el campo se limpia en cada transición
  ocupado↔normal.

### Eliminado
- **Soporte del puente SPI→T6963C eliminado** (un experimento): `T6963C_Bridge.h`,
  su sección de tarea de pantalla, bloque de configuración y referencias cruzadas
  han desaparecido.

### Documentación
- READMEs (EN/PL/ES) actualizados con el conjunto de funciones LTIC v0.5x–v0.88
  (auto-calibración LC, ganancias auto-ajustadas, ruta de mediana del ADC,
  protección anti-fuga, WU, animaciones LED, color de lock fiable) y una nueva
  sección sobre soporte de TFT en color: cualquier panel TFT_eSPI a 320×240 o
  480×320 con los pasos de configuración.

---

## [v0.87-rtos]

### Corregido
- **Cero tiempo muerto antes del muestreo — la preparación se comía toda la
  banda.** El ADC sigue el ritmo bien (1 muestra/s ≈ 8 mV/paso a 9 ns/s); lo que
  fallaba eran los ~60 s de asentamiento y las lecturas d1/d2 entre comandar la
  rampa y la primera muestra. Un offset fijo se suma a cualquier df que el PWM
  guardado ya tenga (medido +9 ns/s en el banco), así que la fase voló
  0,061→2,62 V a través de toda la banda ANTES de que empezara el muestreo, y el
  ajuste solo veía saturación. Ahora LC re-arma el picDIV (arranque
  determinista desde abajo), comanda el offset y empieza a muestrear en ~3 s; la
  tasa exacta se lee DESPUÉS de la pasada desde un avg100 limpio. Si la
  saturación aún llega antes de 10 puntos de ajuste, el offset se reduce a la
  mitad, se re-arma el picDIV y la pasada se reintenta una vez. La medición
  d1/d2 previa al barrido y la maquinaria adaptativa de reducir/aumentar se
  eliminan — la comprobación física y la tasa precisa posterior a la pasada las
  hacen redundantes.

---

## [v0.86-rtos]

### Cambiado
- **LC rediseñado como una única pasada de abajo hacia arriba — sin sondeo de
  dirección, sin inversiones, sin necesidad de dar la vuelta.** Los registros de
  campo demostraron que el arm del picDIV aparca la fase de forma DETERMINISTA
  ~60 ns por encima del punto de sync (Vphase ≈0,061 V tras cada re-arm), que el
  lado negativo por debajo de ese punto está MUERTO (el orden de los flancos se
  invierte, el pulso desaparece — avg100 mostró una deriva real de −3 ns/s
  mientras la tensión no se movía), y que el lado positivo recorre toda la banda
  hasta una saturación suave. Ahora LC lo explota: tras armar, COMANDA un barrido
  positivo de ~+4 ns/s (offset a partir de la K medida), muestrea toda la banda
  en una pasada, y trata la saturación superior sostenida como el FINAL natural
  de la medición en vez de un fallo. La relectura precisa de avg100 (v0.85)
  escala ns/V con exactitud. La inversión de dirección en pleno barrido y su
  maquinaria de reinicio se eliminan.

---

## [v0.85-rtos]

### Corregido
- **La inversión de dirección ahora COMANDA una tasa de barrido en vez de fiarse
  de una lectura ciega — y la fase ya no se aparca en el borde de la banda.** En
  el banco, la iteración de inversión se detenía en un nominal «−1 ns/s» que en
  realidad era ≈0: avg10 cuantiza a 0,1 Hz (d1=0,1000, d2=0,0000 en el registro),
  así que por debajo de 0,1 Hz la lectura es ruido. Con df≈0 la fase se quedaba
  donde el re-arm del picDIV la dejaba (Vphase 0,061 V — el borde inferior de la
  banda, donde pulsos demasiado estrechos apenas cargan el RC), el barrido
  cubría 5 mV, y la comprobación física tenía que abortar. Ahora, cuando el
  signo se invierte entre iteraciones, LC interpola el punto de 10 MHz P0 a
  partir de los dos últimos offsets y fija la rampa en P0 − 0,06 Hz·(LSB/Hz) —
  un −6 ns/s COMANDADO derivado de la K medida, independiente de la lectura
  cuantizada. Al final del barrido (PWM constante en toda la pasada, así que
  avg100 es limpio a resolución de 0,01 Hz) la tasa real se relee y reemplaza a
  la comandada antes de calcular ns/V, de modo que la escala del ajuste es
  exacta.

---

<!-- ============================================================= -->
<!-- Las versiones anteriores a v0.85 aún no están traducidas al    -->
<!-- español. El texto en inglés se conserva abajo verbatim y se    -->
<!-- traducirá en revisiones sucesivas. Para el historial completo  -->
<!-- traducido, véanse CHANGELOG.md (EN) y CHANGELOG_PL.md (PL).     -->
<!-- ============================================================= -->

> **⚠ Traducción pendiente.** Las entradas de v0.84 hacia abajo todavía están
> en inglés; se irán traduciendo en próximas revisiones. El contenido técnico es
> idéntico al de `CHANGELOG.md`.

## [v0.84-rtos]

### Fixed
- **The in-sweep direction flip now re-measures the rate and FORCES the sign
  to change.** v0.83's defences all fired correctly on air (soft-saturation →
  flip → clean restart → bad result rejected), but the flip itself had two
  defects: (1) the fit's ns/V divides by phase_rate, and the pre-flip rate was
  reused after the flip — a guaranteed wrong scale (ns/V=9.09e6 rejected by
  the guard); (2) mirroring the offset around saved_pwm does not change the
  drift sign when saved_pwm sits far from the true 10 MHz point (+70 gave
  +0.100 Hz, −70 still +0.054 Hz — the railing side, just slower). After the
  flip LC now re-measures df, and if the sign has not flipped it pushes the
  offset further by −2·df·(LSB/Hz) from the measured K and re-checks (≤3
  iterations); the glitch-rejection window is rescaled to the new rate.
  Simulated on the exact on-air numbers: one push lands at −0.054 Hz
  (−5.4 ns/s), the wrapping side at an ideal sweep speed.

---

## [v0.83-rtos]

### Fixed
- **`LC` can no longer be fooled by soft RC saturation.** A run with a fast
  initial offset (10 ns/s) let the phase drift into the RC's soft-saturation
  region (2.9-3.27 V — below the 3.28 V rail threshold, so "live"): the linear
  fit ingested flat saturation points (ns/V ×74 too big), the later drop out
  of saturation (a 2.57 V "jump") was accepted as a wrap, and the result
  (range=209204 ns, zero_offset=1.34 V — outside the detector band) even
  PASSED the volt-vs-volt self-consistency. Three band-relative gates close
  this class: (1) **physics gate** — the committed range cannot exceed what
  the sweep could physically cover (~rate × window × 1.5), else params
  unchanged; (2) **wrap-jump endpoints** must lie within the clean fitted
  band ±50%, so a drop out of saturation is not a wrap; (3) **soft-saturation
  skip** — once the fit has shape, samples far outside its band are treated
  like railed ones (skipped; they feed the in-sweep direction-flip logic).
  All three scale from the run's own observations — full-swing 3.3 V
  detectors are unaffected.

### Added
- **Survey-in animation on the LED displays.** An upper-'o' spinner (segments
  A→B→G→F chasing around the digit's top loop), phase-shifted per digit into a
  wave — visually distinct from the warmup's lower-'o' wave.

---

## [v0.82-rtos]

### Fixed
- **ACQ parked the phase half a range away from the handover point — permanent
  ACQ (1401 cycles on air with Δf≈0).** The ACQ pull target was computed as
  `zero_offset + span/2`, a relic from before v0.66 when zero_offset was the
  band's floor; since then zero_offset IS the band middle, so the loop held the
  phase at its own "centre" while the ACQ→DPLL threshold (measured against
  zero_offset) could never be satisfied. One point of truth now: ACQ pulls
  exactly to zero_offset. A fresh `LC` also clears any old `LCV` override
  (which could silently re-introduce the same stalemate from EEPROM).

### Added
- **Warmup animation on the LED displays.** During OCXO warmup every digit of
  the TM1637/HT16K33 shows the lowercase-'o' chasing-segment spinner,
  phase-shifted per digit so the pattern travels across the display like a
  wave (survey-in keeps the dashes).

### Note
- After upgrading, re-run `LC` once: the previous calibration was taken
  through the old 10-second-averaged ADC path and its zero_offset/range are
  blurred; the rebuilt burst-median path (v0.79) gives a sharper measurement.

---

## [v0.81-rtos]

### Fixed
- **Build fix:** `p_eff` was used by the DPLL/LOCK integrator before its
  declaration (v0.79/v0.80 did not compile). The deadband/soft-knee block is
  now computed first, so both the integrator and the phase term see it.
- **Calibration countdown shows the REAL total time.** The counter used to
  restart for every internal wait segment (30 s, 20 s…), so the display never
  reflected the whole procedure. `LC`/`CT` now preload a realistic total and
  adaptive phases (ramp increase, rail-backoff, direction flip, sweep restart)
  top it up as they occur; every exit path clears it.
- **OCXO warmup restored and made a saved setting.** Warmup was silently
  skipped whenever the EEPROM was valid — so it "disappeared" once a
  configuration was saved, and a cold-started OCXO was disciplined while still
  drifting thermally. Warmup now runs by default on every boot and can be
  disabled with the new `WU 0` command (`WU 1` re-enables; state saved by `ES`
  in EEPROM byte 221, fresh-flash default: on).

### Added
- **LED "CAL" + spinner during every calibration.** TM1637 and HT16K33 show
  CAL on the first three digits and, on the fourth, a chasing-segment
  animation (G→C→D→E) tracing a lowercase 'o' — a clear "working" cue.

---

## [v0.80-rtos]

### Fixed
- **The green frequency colour now means a trustworthy, CURRENT lock.** After
  LTIC dropped from LOCK to ACQ, the display stayed green because the 1000-s
  average still read ~10 MHz — an echo of the past, not the present. Rules now:
  for algorithm 10 green comes ONLY from the loop's live LOCK state (no
  average fallback); for algorithms 0-9 the long-window criterion remains but
  must be backed by the fast 10-s average still within ±50 mHz of 10 MHz, so a
  loss of discipline kills the green in ~10 s instead of minutes.

---

## [v0.79-rtos]

### Fixed
- **LTIC ADC path rebuilt — the 10-second moving average was poisoning the
  loop.** The old path took ONE raw ADC read per PPS through a 10-sample
  (=10 s) moving average: ~5 s group delay (the loop corrected on stale data)
  and, worse, pre- and post-wrap voltages blended into phantom mid-levels — the
  loop saw a smooth ~30 ns/s drift that did not physically exist and kicked the
  real phase (LOCK steps up to 152 LSB, LOCK↔DPLL bouncing). Now each PPS slot
  takes a 16-read burst (~1 ms) and its MEDIAN — no cross-second memory, no
  lag, no wrap blending, single-read glitches fall out — plus an outlier gate:
  a jump >25% of the calibrated span must repeat in the next read to be
  believed (real wraps persist; glitches don't). Note: reading the ADC more
  often would add nothing — the detector charges the capacitor once per PPS,
  so phase information is inherently 1 Hz; the burst maximises the quality of
  that one sample.
- **LOCK is gentle by design: deadband + soft knee + step cap.** Inside a
  deadband (range/40, ≥6 ns — the ADC noise floor) the phase error counts as
  zero and the integrator holds; outside, the error ramps from zero (soft
  knee); the final LOCK step is hard-capped at ≈4 mHz (from measured K). Small
  offsets now get proportionally small pushes instead of full-gain kicks.

---

## [v0.78-rtos]

### Fixed
- **First confirmed on-air LOCK with the LTIC three-stage loop.** Two follow-ups:
  (1) the TFT frequency readout now turns green on LTIC LOCK — it only
  recognised the legacy "hit" trend, so the colour would have waited for the
  1000/10000-s averages to reach mHz; (2) the EEPROM recall guard rejected
  algorithm 10 (`algo > 9 → 0`), so a saved LTIC configuration silently
  reverted to algorithm 0 on reboot — now `> 10`. With this, `ES` fully
  preserves the LTIC setup: algorithm 10, the LC calibration and polarity are
  stored, and the loop gains are re-derived by autotune from the stored
  measurements on every entry, so a reboot comes back locked-capable with no
  manual steps.

---

## [v0.77-rtos]

### Fixed
- **State transitions no longer bounce on the stepped detector read.** With the
  frequency finally held (−0.02 Hz), the loop still ping-ponged ACQ↔DPLL: the
  ADC updates the phase voltage in steps, and each step produced a phantom
  50-100 ns/s "slope" that tripped the V-derived slope gates (entry to DPLL
  blocked for 183 cycles; DPLL demoted after 6). All frequency-quality gates in
  the transitions now use TIM2's Δf (immune to the stepping) — ACQ→DPLL at
  |Δf|≤0.05 Hz, DPLL→LOCK at ≤0.03 Hz, demotions at Δf>0.30 / 0.10 Hz — while
  the voltage is used only for phase POSITION. DPLL demotion also gained the
  same 3-strike persistence LOCK already had, so a single stepped read cannot
  demote. Simulated with stepped reads: no false demotions, clean promotion to
  LOCK.

---

## [v0.76-rtos]

### Added
- **Full LTIC auto-tuning — no hand-set coefficients.** `ltic_autotune()`
  derives EVERY loop gain from the two measured hardware constants: K (Hz/LSB
  from CT) and ns/V + range (from LC). Freq loop cancels ~50% of Δf per step;
  phase loop pulls with τ≈20 s; LOCK is 4× gentler; the ACQ threshold becomes a
  quarter of the measured detector range. Runs automatically after each
  successful LC and on entry to algorithm 10, and prints the derived values.

### Fixed
- **ACQ now drives the TIM2 frequency error, not the voltage-derived drift.**
  The stepped detector read goes flat at a band edge (on air: phase parked at
  0.336 V while a real −0.3 Hz offset persisted, with ACQ↔DPLL bouncing) — a
  V-derived slope is blind there; TIM2 is not.
- **Board polarity no longer inverts the frequency path.** K is positive on
  every board (+PWM → +f), so frequency terms take no `pol`; only the phase
  (Vphase) path does. Routing e_freq through pol=−1 had been inverting a
  correct frequency correction in DPLL — a co-cause of the state bouncing.

---

## [v0.75-rtos]

### Fixed
- **ACQ oscillated (±1 Hz swings, twice frozen by the runaway guard) once the
  calibration was finally CORRECT.** The drift gain used a guessed fixed
  multiplier (×60) that had been implicitly tuned against the old, wrongly
  scaled calibration; with the true ns/V the numeric drift grew ~2.3× and the
  loop over-corrected ~1.8× per step — a textbook overshoot oscillation. The
  gain is now derived from the MEASURED OCXO sensitivity (CT stores 0.40/K in
  g_pid[7].Kp, so LSB-per-Hz is recovered as Kp7/0.40) with a 0.5 damping
  factor: ~60% of the error cancelled per step, unconditionally stable on any
  unit, no per-board tuning. The DPLL frequency term (fixed ×1000, ~6× too weak
  on this unit) is scaled from measured K the same way.

---

## [v0.74-rtos]

### Fixed
- **Wrap-jump quality gate — closes the last known way LC could go wrong.** The
  stepped ADC can report a wrap mid-step, yielding a PARTIAL jump; one was
  accepted as the full span (0.122 V on a ~0.33 V detector), which parked
  zero_offset near the floor (0.09 V) and sent the loop chasing a false centre
  until the frequency ran 3 Hz away. A jump now counts only if it starts from a
  live (un-railed) sample AND is ≥80% of the min–max band actually observed;
  partial jumps are named in the log and the observed band (or time
  cross-check) is used instead. `zero_offset` is now ALWAYS the middle of the
  observed band, never derived from the jump position.
- **Operator verdict line.** LC ends with an explicit "PASSED checks — review
  LL, then 'ES'" or "MARGINAL result — prefer re-running LC before 'ES'", so a
  weak calibration is hard to save by accident.

---

## [v0.73-rtos]

### Fixed
- **Runaway guard rebuilt after a real 3 Hz escape reached PWM 63500 — the old
  guard had three false assumptions.** (1) Its baseline re-anchored on every
  un-railed sample, but during a runaway the phase periodically WRAPS (briefly
  un-railed), so the baseline chased the escape and the 6000-LSB trip never
  fired. It now re-baselines only when genuinely healthy (un-railed AND
  |Δf| < 0.25 Hz). (2) An LSB threshold silently assumes the OCXO's Hz/LSB
  sensitivity; the primary criterion is now the measured frequency error
  itself: phase railed AND |Δf| > 0.5 Hz → freeze (a 2000-LSB backstop
  remains). (3) Freezing the step left the DPLL/LOCK integrator winding up,
  ready to slam PWM on recovery — it is re-seeded to the held PWM while
  frozen. Behavioural test: old guard let the simulated escape reach 6.15 Hz;
  the new one freezes at 0.51 Hz.

---

## [v0.72-rtos]

### Fixed
- **Direction flip now happens IN the sweep, where the rail actually shows.**
  v0.71's 8 s pre-check could not catch the wrong direction: in the rail-prone
  direction the phase exits the sync window only after ~a full range of drift —
  tens of seconds into the sweep (the pre-check passed, then 137 samples
  railed). LC now counts consecutive railed samples during the sweep itself;
  a sustained run (≥15 s) is the direction verdict: it flips the offset sign
  (mirrored around the saved PWM), re-arms picDIV, wipes every accumulator and
  restarts the sweep once. Verified in simulation: wrong side rails at 40 s →
  flip at ~54 s → clean sweep from the good side with the full-span wrap jump
  captured. If both directions rail, the existing mostly-railed abort still
  reports it.

---

## [v0.71-rtos]

### Fixed
- **`LC` auto-detects the ramp DIRECTION — the root cause of every railed
  calibration.** Comparing all field runs revealed the pattern: every failed
  cal had a positive df (ramp pushing the frequency above 10 MHz) and the single
  clean one (range=318) had a negative df. On this detector family the phase
  wraps sawtooth-style only when drifting one way; the other way the pulse just
  widens until the RC pins at the 3.3 V rail and stays. The good direction is
  board-dependent, so LC now probes it: after settling it watches the phase for
  ~8 s and, if pinned to a rail, flips the offset sign, re-arms picDIV and
  settles again (aborting cleanly only if BOTH directions rail). The adaptive
  ramp keeps the detected direction. Also verified: algorithm 7 does NOT run
  during LC (the calibration blocks the control task), so loop interference is
  ruled out.

---

## [v0.70-rtos]

### Changed
- **`LC` is fully self-contained: it ignores the previous calibration.** Per a
  good operator principle — you recalibrate precisely because the stored values
  may be wrong — LC no longer inherits anything from EEPROM/g_ltic: the ramp
  target, wrap threshold, glitch window and prep criterion all start from
  neutral assumptions and everything is measured fresh. This ends the poisoning
  cascade where one bad cal (range=6035) mis-steered the next three runs.
- **Single-wrap range measurement.** The voltage JUMP at a wrap (sawtooth top →
  bottom in one sample) IS the full detector span, so one wrap suffices:
  range = |jump| × ns/V. The ramp target drops to one wrap in the window, i.e. a
  much gentler sweep that no longer pushes the phase out of the picDIV sync
  window onto a rail (the failure seen at 9-22 ns/s). Two wraps, when they occur
  naturally, still enable the independent time cross-check.
- **Prep criterion is universal:** waits for a valid, un-railed, steady phase —
  no assumed centre voltage (detector bands legitimately differ between builds).

---

## [v0.69-rtos]

### Fixed
- **`LC` adaptive ramp is now hardware-agnostic and self-limiting.** The v0.68
  log showed a cascade: a poisoned prior cal (range_ns=6035 from a noise fit)
  set an absurd ramp-speed target, the adaptive increase chased it (offset up to
  1120, 15 ns/s), and the fast ramp pushed the phase out of the picDIV sync
  window entirely — the detector pulse went wide and the voltage pinned at the
  rail for the whole sweep ("180 railed samples"). Three hardware-agnostic
  defences (no detector band is assumed; different builds range from ~0.3 V to
  full 3.3 V swings): (1) the stored range only *guides* the ramp target through
  a wide anti-garbage clamp (20..5000 ns); (2) **rail-backoff** — after each
  ramp increase LC watches ~8 s and, if the phase pins to a rail, halves the
  offset back, re-arms picDIV to regain sync, and proceeds at the speed the
  hardware allows; (3) **self-consistency gate** — results are committed only if
  range ÷ slope implies a physically possible voltage span (≤3.3 V), otherwise
  the previous calibration is left untouched (a bad LC can no longer poison the
  next one).

---

## [v0.68-rtos]

### Fixed
- **`LC` no longer produces garbage when the ramp lands near the OCXO's 10 MHz
  point.** A +70 LSB offset can barely detune the OCXO (df=0.01 Hz → 1 ns/s), so
  no real wrap could occur in the window — yet read glitches (the phase voltage
  updates in steps) exceeded the wrap threshold and produced fake "2 wraps", a
  noise-only fit, and absurd results (ns_per_volt=38615, range_ns=6035). Three
  defences added: (1) **adaptive ramp increase** — if the drift is too slow for
  two wraps in the window, the offset is doubled (capped ±4000) and re-settled;
  (2) **time-validated wraps** — a jump sooner than ~half the expected crossing
  time after the previous wrap is a glitch and is ignored; (3) **volt/time
  range cross-check** — the time between two wraps × phase rate gives an
  independent range measure; if it disagrees >2× with the voltage-span measure,
  the slope is suspect and the TIME range wins (ns/V rescaled to match).

---

## [v0.67-rtos]

### Added
- **`LC` now auto-preps before ramping (operator convenience).** Running `LC`
  used to require a manual `LA 7` / `AP` / "wait for the phase to reach centre"
  sequence first; starting with the phase against a rail was the main cause of
  poor calibrations. `LC` now, on its own: (1) arms picDIV to sync to 1PPS if a
  GPS fix is present, then (2) waits up to ~60 s for the phase voltage to settle
  inside the central band of the detector (centre ± ¼ range, held a few seconds)
  before starting the ramp. It prints each step and proceeds with a clear note
  if the phase can't be centred in time. Just run `LC` — no manual prep needed.

---

## [v0.66-rtos]

### Fixed
- **`LC` now measures the FULL detector range (was a fraction, e.g. <75 ns).**
  Two bugs collapsed `range_ns` on a narrow detector: (1) the wrap threshold was
  a fixed 0.5 V — larger than the whole ~0.33 V detector range — so wraps were
  never detected; (2) `range_ns` was taken from the small slice the phase
  happened to sweep during the ramp, not the detector's full unambiguous span.
  `LC` now sweeps until it has seen **two wraps** (one full cycle), tracks the
  true min/max across wraps for the range, and still fits the slope (ns/V) on
  the clean pre-wrap segment. The wrap threshold is now relative to the detector
  span. Ramp/window retuned (offset 70 LSB, 180 s) so both a long clean slope
  segment and two wraps fit. `LC` reports whether it saw 0/1/2 wraps so you know
  if the range is exact, approximate, or a lower bound.

---

## [v0.65-rtos]

### Fixed
- **DPLL corrected too infrequently for a narrow detector (looked "frozen").**
  DPLL only adjusted PWM every 10 s and LOCK every `lock_interval_s`; on a
  narrow detector the phase sweeps its whole range in ~10-15 s of residual
  drift, so between corrections the phase wandered and wrapped while PWM sat
  still (seen as PWM pinned at one value for 114 samples). DPLL now corrects
  every 2 s. This is *not* a schematic error: in every state PWM (via the RC
  filter → EFC) drives the OCXO — Vphase is only the ADC feedback measurement,
  so there is correctly no analog Vphase→EFC path.
- **LOCK interval clamped to a sane range (1..30 s).** A corrupted
  `lock_interval_s` (e.g. the 50373 seen in a log) would have made LOCK correct
  roughly once every 14 hours; it is now bounded at runtime and in the `LIV`
  command so LOCK keeps tracking.

---

## [v0.64-rtos]

### Changed
- **Removed the unreliable polarity auto-probe; polarity is now set manually.**
  The single-cycle probe could not separate the PWM effect from the phase's own
  drift on a narrow, drifting detector, so it repeatedly detected the wrong sign
  (+1 where the board is −1). ACQ now holds and prints a reminder to run
  `LPOL -1` (or `+1`) then `ES` when polarity is unset, and DPLL/LOCK already
  hold when polarity is unknown. Once `LPOL` is set and saved, all three stages
  use it consistently — this is reliable where the probe was not.

---

## [v0.63-rtos]

### Fixed
- **Detected polarity is now shared by all three stages.** The auto-detected
  sign lived in a static local inside ACQ, invisible to DPLL/LOCK, which then
  fell back to +1 and — on a reversed board with polarity unsaved — drove the
  phase to the ceiling rail with PWM climbing and frequency walking away from
  10 MHz. ACQ now writes the detected sign into `g_ltic.polarity`, so every
  stage uses it (and it prints a reminder to `ES`).
- **DPLL/LOCK hold instead of guessing when polarity is unknown.** With no
  established sign they now output zero correction and let the machine fall back
  to ACQ (which probes), rather than assuming +1 and running away.
- **Runaway guard.** If the phase is pinned to a rail while PWM is pushed more
  than ~6000 LSB from where the loop started, the loop freezes and warns once
  ("check LPOL / re-centre") instead of sliding PWM to an extreme and
  undisciplining the OCXO.

### Note
- Save your polarity: after the loop prints "detected …polarity -1", run `ES`
  so it survives a reboot (this was the root cause of the last runaway — the
  sign was set but never saved, so it reverted to auto/one).

---

## [v0.62-rtos]

### Fixed
- **DPLL and LOCK now apply the board polarity (was ACQ-only).** ACQ used the
  detected/forced `LPOL` sign, but DPLL and LOCK did not — so on a reversed
  board they drove the phase the wrong way, shoving Vphase onto the floor rail
  and dropping straight back to ACQ (the phase would centre in ACQ, hand over to
  DPLL, then get pushed to ~0 V and fall back). All three stages now share the
  same polarity, so DPLL/LOCK pull the phase toward centre instead of into a
  rail. With ACQ handover already working (v0.61), this is what lets DPLL hold
  and progress to LOCK.

---

## [v0.61-rtos]

### Fixed
- **ACQ now nulls the phase drift instead of chasing phase position.** With the
  polarity correct (`LPOL -1`) PWM stopped running away, but the phase still
  swept the whole detector and wrapped, so ACQ never met the "in-window + low
  slope" exit. The residual frequency offset (~-0.26 Hz) drove the phase at
  ~26 ns/s across a 318 ns detector — far too fast. ACQ's dominant term now acts
  on the phase DRIFT (dPhase/dt), driving the frequency offset to zero so the
  phase stops moving; a weak centring term parks it mid-range only once the
  drift is already small. Wrap-induced drift spikes (phase jumping >½ range in a
  step) are rejected so they don't corrupt the drift estimate or the
  slope-gated transitions.

---

## [v0.60-rtos]

### Fixed
- **ACQ ran PWM away when the board polarity was reversed.** ACQ walked PWM in a
  fixed direction toward `zero_offset`; on hardware where increasing PWM lowers
  the phase voltage (opposite sign), that drove PWM ever downward while the
  phase wrapped chaotically, so ACQ never settled (observed as a long ACQ hang
  with PWM sliding from ~41000 to ~17000). ACQ now **auto-detects the PWM→phase
  polarity** with a small probe step, then drives toward the target with the
  correct sign. A new `LPOL -1/0/1` command forces the sign (0 = auto).
- **ACQ now centres on the middle of the detector range, not `zero_offset`.**
  On a narrow low-band detector `zero_offset` can sit near the floor (e.g.
  0.097 V), so targeting it kept the phase against the rail (risking latch-up /
  wrap, per Dan's note about choosing mid-scale). ACQ now aims at the range
  middle, overridable with `LCV <volts>`.

### Added
- `LPOL` (PWM→phase polarity) and `LCV` (ACQ centring target) CLI commands,
  both persisted to EEPROM and shown by `LL`.

---

## [v0.59-rtos]

### Changed
- **Phase-slope gating on state transitions (algorithm 10).** On advice from
  Dan (time-nuts), both LTIC state transitions now check the phase SLOPE
  (dPhase/dt), not just the phase magnitude. Since frequency is the first
  derivative of phase, a small slope means the frequency is already close to
  10 MHz — so ACQ→DPLL now requires a wide slope window and DPLL→LOCK a ~5×
  tighter one, preventing a handover while the phase is merely sweeping through
  centre at speed (which would lock the wrong frequency). LOCK also drops back
  to DPLL if the slope grows. This is what makes the frequency land very close
  to nominal at each handover.

---

## [v0.58-rtos]

### Fixed
- **`LC` ramp far too fast for a narrow detector.** On hardware whose detector
  spans only a fraction of the ADC (e.g. ~0.33 V per unambiguous period), the
  old +2000 LSB ramp drove the phase across the whole detector every ~1-2 s, so
  every sample railed or wrapped and `LC` aborted with "mostly railed". The
  default ramp offset is now a gentle 60 LSB (≈4-5 ns/s on a typical OCXO), and
  `LC` adaptively steps the offset down further if the measured drift would
  cross the detector in under ~15 s. The frequency-measurement fix from v0.56
  is confirmed working (real df now reported, e.g. 1.4-2.0 Hz, not the old
  hard-coded 0.6).

---

## [v0.57-rtos]

### Fixed
- **ACQ now actively centres the phase (was frequency-only).** The ACQ stage
  previously corrected only the TIM2 frequency error; once the OCXO was already
  near 10 MHz nothing drove the phase, so it could sit stuck against a detector
  rail forever and never satisfy the ACQ→DPLL exit test (observed as an
  overnight hang with Vphase parked low). ACQ now walks PWM toward the detector
  centre when the reading is railed, and drives proportionally to the phase
  error once it is in-window.
- **Phase centre taken from calibration, not a hard-coded 1.65 V.** Real
  hardware can have a narrow detector band far from mid-ADC (e.g. 0..0.45 V), so
  the loop now centres on the calibrated `zero_offset` (with a coarse 0.22 V
  fallback) instead of assuming 1.65 V. Run `LC` so `zero_offset`/`ns_per_volt`
  reflect the real band.

---

## [v0.56-rtos]

### Fixed
- **`LC` frequency measurement.** The calibration read the 10 s frequency
  average once, immediately after a 10 s settle — on real hardware that window
  had not yet caught up to the forced ramp, so the ramp rate (and therefore
  `ns_per_volt`) came out wrong. `LC` now settles 30 s, then samples the 100 s
  average (steadier, with a 10 s fallback) twice ~5 s apart and averages them.
- **`LC` rail handling.** Samples where the TIC voltage sits at the ADC ceiling
  or floor (phase outside the detector window) are now skipped rather than
  flattening the least-squares fit, and `LC` aborts with a clear message if the
  ramp is mostly railed (telling you to centre Vphase near mid-rail first).
- **Build fix:** removed a duplicate `g_ltic_voltage` extern in
  GPSDO_algorithms.cpp that conflicted with the `gpsdo_state.h` declaration.

---

## [v0.55-rtos]

### Added
- **Algorithm 10 (LTIC three-stage PLL) — the loop is now implemented.**
  `LA 10` disciplines the OCXO from the hardware TIC phase (PA1) through a
  hybrid ACQ → DPLL → LOCK state machine. ACQ is frequency-led (TIM2) to pull
  the OCXO close to 10 MHz so the phase ramps slowly enough to catch; DPLL adds
  the LTIC phase term for fast centring; LOCK is phase-led with slow updates
  every `lock_interval_s` and a hysteresis band for dropping back to DPLL. The
  picDIV is armed automatically on entering ACQ. The loop works in nanoseconds
  when the TIC is calibrated (`LC`), and falls back to a nominal volt-based
  phase with a one-time warning when it is not. The state persists in
  `g_ltic.state`, so a warm reboot (`RB`) resumes mid-sequence rather than
  restarting from ACQ. The trend field shows `ACQ` / `DPLL` / `LOCK`.
- **Third PID set (ACQ).** `LticParams_t` gained an `acq` PID alongside `dpll`
  and `lock`, with its own CLI verbs `AQP` / `AQI` / `AQD` / `AQL` and EEPROM
  storage. `LL` now lists all three sets.

### Changed
- **EEPROM layout extended to 216 bytes (reserved to 224).** The ACQ PID block
  [200..215] was appended under the same `GPSD2` signature with the usual
  NaN/`0xFF` guards, so older saves still load with the ACQ gains defaulting.

---

## [v0.54-rtos]

### Added
- **`LC` — LTIC self-calibration.** Automatically measures the TIC's
  voltage→time slope without any external reference. `LC` forces a small PWM
  offset so the phase ramps linearly, derives the ramp rate from the TIM2
  frequency error (`phase_rate = df / BASE_FREQ × 1e9` ns/s), least-squares
  fits the TIC voltage against time to get `dV/dt`, and computes
  `ns_per_volt = phase_rate / (dV/dt)`. It also records the swept voltage span
  as `range_ns` and a mid-scale `zero_offset`, detecting one wrap to keep a
  single clean ramp segment. Runs in the control task like `CT`, with the same
  safety pattern (PWM saved and restored, range-guarded results, abort on
  no-GPS / too-few-points / singular or flat fit — params left unchanged on
  any failure). Results go to the live LTIC params; review with `LL`, then
  `ES` to save. New config constants `LTIC_CAL_PWM_OFFSET`, `LTIC_CAL_SECS`,
  `LTIC_CAL_MIN_POINTS`. This fills the calibration fields that the phase-A
  loop will need; the loop itself is still not implemented.

---

## [v0.53-rtos]

### Added
- **Warm/cold restart commands `RB` and `CR`.** `RB` does a warm reboot
  (`NVIC_SystemReset()`) keeping the EEPROM, so the still-warm OCXO recalls its
  disciplined state. `CR YES` does a cold restart: erases the EEPROM (back to
  factory defaults — PWM, model, calibration, LTIC params all reset) then
  reboots; the `YES` confirmation is required because it discards the learned
  OCXO model.
- **Algorithm 10 (LTIC) infrastructure — parameters, CLI and EEPROM.** Full
  parameter set, CLI editing and EEPROM persistence for the planned LTIC
  three-stage PLL (ACQ→DPLL→LOCK), so the configuration is ready before the
  loop itself is written ("phase A"). New `LticParams_t` holds TIC calibration
  (ns/V, zero offset, range), two PID sets (wide-band DPLL + narrow-band LOCK),
  state-transition thresholds, the LOCK interval, and the resumable state.
  Fifteen CLI commands set/show these (`LL`, `LNV/LZO/LRN`, `DPP/DPI/DPD/DPL`,
  `LKP/LKI/LKD/LKL`, `LAT/LDT/LIV`). `LA 10` is accepted by the parser but
  reports "not implemented yet" and refuses to select, so the OCXO is never
  left undisciplined. The loop itself is not implemented — that is phase A,
  pending the LTIC hardware.

### Changed
- **EEPROM layout extended to 200 bytes (reserved to 208).** The LTIC block
  [144..207] was added under the **same `GPSD2` signature**; every new field is
  NaN/`0xFF`-guarded, so EEPROM images saved by older firmware load cleanly with
  the LTIC parameters defaulting until set. No migration or re-init needed.

---

## [v0.52-rtos]

### Added
- **LTIC (Lars' TIC) phase-voltage preview.** The TIC voltage on PA1 was
  already sampled and sent over serial telemetry, but had no on-screen
  presence. Added (all gated by `GPSDO_LTIC`, so zero effect on builds without
  the TIC):
  - a **TFT row** showing `Vph:x.xxxV` (and `… NNNns` once calibrated);
  - an **LTIC entry in the boot-splash hardware checklist** (`[x] LTIC phase
    (PA1)` — shown when compiled in, like the TM1637/TFT, since the TIC is
    read-only and cannot be probed);
  - a **`LTIC_NS_PER_VOLT` calibration constant** in the config (0 =
    uncalibrated → volts only). When set to the measured ramp slope, the
    display and the planned phase-discipline algorithm convert volts to ns.
  This is a **preview/telemetry layer only** — the control loop does not yet
  discipline the OCXO from the TIC (planned as a separate phase, a new
  LTIC-based algorithm). OLED/LCD were intentionally left unchanged (their
  layouts are full); Vphase remains available there via serial logging, which
  is what characterising the TIC needs at this stage.

---

## [v0.52-rtos]

### Added
- **LTIC (Lars' TIC) phase-voltage preview layer.** When `GPSDO_LTIC` is
  compiled in, the latched TIC voltage (`g_ltic_voltage`, already sampled on
  PA1 and discharged each PPS) is now surfaced as a preview: a dedicated
  `Vph:` row on the TFT (below the sensor row, shown only with LTIC built in),
  and an `LTIC phase (PA1)` entry in the boot checklist. Serial telemetry
  already carried Vphase. A new `LTIC_NS_PER_VOLT` calibration constant lets a
  future build convert the voltage to a phase in nanoseconds: while it is 0
  (default, uncalibrated) the displays show volts only; once set, the TFT row
  also shows `<n>ns`. This is preview/telemetry only — the control loop does
  not yet discipline on LTIC; that is a planned separate algorithm. OLED/LCD
  layouts are unchanged (both are full); Vphase will be added there when LTIC
  becomes an operational loop input.

---

## [v0.51-rtos]

### Added
- **CLI commands are now case-insensitive.** The command dispatcher compared
  verbs with `strcmp()`, so `LA` worked but `la` did not. Command matching now
  uses a small case-insensitive helper (`cli_ieq`), so any letter case is
  accepted (`LA` / `la` / `La` are equivalent), including the lowercase verbs
  (`up1`, `dp10`, …) and the `KP`/`KI`/`KD`/`IL` family (whose parameter
  letter is also matched case-insensitively). Command arguments are unchanged;
  `TO A` already accepted either case.

### Changed
- **ZED-F9T (Gen9) support is no longer experimental.** The CFG-VALSET
  survey-in path and the NAV-SVIN monitor fallback were tested on real
  hardware by EEVblog user danieljw, so the "experimental / untested" markings
  have been removed from the code, config and READMEs. No code change to the
  F9T path itself — only its status.

---

## [v0.50-rtos]

### Added
- **ZED-F9T (Gen9) timing-receiver support — experimental, untested.** A third
  survey-in path was added alongside the proven LEA-6T / LEA-M8T ones.
  `ubx_start_survey_in()` now also sends a `CFG-VALSET` (0x06 0x8A) frame
  setting the Gen9 configuration keys `CFG-TMODE-MODE` (survey-in),
  `CFG-TMODE-SVIN_MIN_DUR` and `CFG-TMODE-SVIN_ACC_LIMIT` (the latter converted
  from mm to the F9T's 0.1 mm unit). The survey-in monitor gained a parallel
  `NAV-SVIN` (0x01 0x3B) parser and falls back to it when `TIM-SVIN` does not
  answer, since the F9 generation reports survey-in through NAV-SVIN. ⚠️
  Written from u-blox documentation/ubxtool with no F9T on hand — key IDs, the
  0.1 mm unit and the NAV-SVIN payload offsets are NOT verified on hardware.
  The legacy `CFG-NAV5` stationary frame may NAK on an F9T (harmless; the
  survey-in path is independent). The two tested receivers are unaffected:
  TIM-SVIN is still tried first, so LEA-6T / LEA-M8T / NEO-M8T behaviour is
  unchanged. Documented as experimental in the README and config.

### Changed
- **LCD 20×4 splash subtitle** changed from `GPS-Disciplined Osc.` to
  `GPS-Disciplined OCXO`, matching the TFT splash (both 20 chars, full width).

### Notes
- **NEO-M8T** confirmed (by datasheet analysis) fully compatible with the
  existing LEA-M8T path — same M8 silicon + FW3, same CFG-TMODE2 / TIM-SVIN —
  no code change required. Documented in the timing-receiver section.

---

## [v0.49-rtos]

### Fixed
- **Config macro ordering: `OUT_SERIAL` now respects `GPSDO_BLUETOOTH`.** The
  `OUT_SERIAL` routing macro was evaluated near the top of `gpsdo_config.h`,
  *before* `GPSDO_BLUETOOTH` (and several other feature switches) were defined
  further down. As a result `OUT_SERIAL` always resolved to USB `Serial` even
  when Bluetooth was enabled, and a build with `GPSDO_BLUETOOTH` commented out
  could fail to compile depending on what referenced it. All feature switches
  are now grouped together near the top of the file, and macros derived from
  them (`OUT_SERIAL`) are evaluated afterwards in a dedicated "Derived macros"
  section. No functional change to any enabled feature beyond Bluetooth output
  now actually going to Serial2. A scan of the other source files found no
  further define-after-use ordering issues.

### Changed
- **HT16K33 startup pattern unified with the TM1637.** At power-up the
  HT16K33 now shows `----` (segment-G dashes) instead of `oooo`, matching the
  TM1637's startup pattern — both LED clocks signal "alive, waiting for GPS"
  the same way. The `oooo` indicator is retained for the no-fix-during-
  operation case (where the TM1637 also shows `oooo`), so the two displays now
  behave identically in every state.
- **TFT splash credit line** changed from `jmnlabs + with Claude (Anthropic)`
  to `jmnlabs with Claude (Anthropic)` (dropped the `+`).

---

## [v0.48-rtos]

### Added
- **ILI9488 480×320 SPI TFT support (`GPSDO_TFT_ILI9488`).** ⚠️ Untested — no
  panel on hand yet. The existing 320×240 ILI9341/ST7789 operating screen and
  animated splash are shared and auto-scaled to 480×320 at compile time:
  width ×1.5 and height ×1.33 via independent `TFT_SX`/`TFT_SY` macros (the
  panel aspect differs from a pure 1.5×), and TFT_eSPI fonts mapped up one
  size via `TFT_F`. Geometry verified to fit the panel; not yet run on real
  hardware. Set `ILI9488_DRIVER` + `TFT_WIDTH 320`/`TFT_HEIGHT 480` (+
  `LOAD_FONT6`) in TFT_eSPI `User_Setup.h`.
- **SPI→T6963C bridge as a new display backend (`GPSDO_T6963C`).**
  ⚠️ Experimental / untested — backend is complete and compiles, but the link
  is not yet validated on clean hardware (long-wire bring-up showed ringing
  and spurious CS edges; same on the reference master → a signal-integrity
  issue, not firmware). Disabled by default; leave off until tested on
  short, point-to-point wiring.
  Drives a PowerTip PG240128 (240×128 mono) panel through the external
  `T6963C_SPI_bridge` over SPI1 using high-level drawing commands
  (`T6963C_Bridge.h`). Selectable in the config like the other displays;
  mutually exclusive with the TFT (shared SPI1 pins / display slot).
  - Reuses the TFT's SPI1 pins: `SCK PA5`, `MOSI PA7`, `CS PB13`,
    `READY PB12`; frees `PB15` (was TFT_RST).
  - Condensed 240×128 layout mirroring the TFT screen: header (title + LMT
    time), large frequency (LOGISOSO fonts), status row, value rows
    (PWM/Vctl, INA219, sensors) and a survey-in progress bar.
  - Monochrome panel → the lock/holdover colour cue becomes an inverted
    (filled) box around the status word (`LOCK` / `HOLD` / `H-LOST` /
    `NOFIX`).
  - One batched SPI transaction per refresh (single READY wait), with the
    bridge library's auto-split as a safety net; per-field change-cache to
    skip redundant redraws.
  - Static boot splash (logo + subtitle + hardware checklist); no wave
    animation, since batched SPI rendering would make it costly on a small
    mono panel.

---

## [v0.47-rtos]

### Added
- **`SV` CLI command** — enable/disable survey-in (Time Mode) on a timing
  receiver at runtime, stored in EEPROM (byte 143). `SV` shows state, `SV 0`
  disables (stay in nav mode — handy for bench testing), `SV 1` enables;
  `ES` saves, applied at next boot. Defaults to enabled on fresh EEPROM.

### Fixed
- **Survey-in polling no longer stalls the displays.** `ubx_poll_svin()`
  waited up to 1000 ms with a busy `delay()`, starving the higher-priority
  GPS task's siblings — the display task visibly lagged (worst on the
  slower-responding LEA-6T). The poll now uses a ~500 ms window that yields
  with `vTaskDelay()` between reads, so the display task runs normally while
  still reliably catching the module's TIM-SVIN reply (100-200 ms latency).
  NMEA bytes seen while scanning are forwarded to TinyGPS++ so the fix is
  not disrupted. Once a survey has replied, occasional missed polls no
  longer abort the monitor (the survey is in progress); gaps in the
  `svin dur=` sequence are gone.
- **Survey-in now exits reliably when its criteria are met.** Completion is
  declared when EITHER the receiver flags the mean position valid, OR the
  user criteria are met (accuracy ≤ limit AND duration ≥ minimum) — some
  receivers (notably the LEA-6T) reached ~0.45 m well past the minimum but
  left the survey "active", so the old `valid && !active` test never fired.
  The safety backstop is now `3 × SVIN_MIN` (min 600 s) so a slow-converging
  survey on a weak antenna gets a fair chance.
- TIM-SVIN early-survey accuracy of `0xFFFFFFFF` ("no estimate") is clamped
  to 65535 mm instead of overflowing.

### Changed
- **TFT precision**: INA219 now shows bus voltage to 3 decimals and current
  to 2 decimals; the PWM control voltage (Vctl) shows 3 decimals.

### Documentation
- README (EN/PL) notes that survey-in needs a good outdoor antenna with a
  full sky view, and records the field observation that the LEA-6T is more
  sensitive than the LEA-M8T in marginal conditions. Both modules were
  verified completing survey-in and entering Time Mode on a professional
  outdoor (survey-grade) antenna. Corrected a couple of stale source
  comments (EEPROM size 144 B, TIM-SVIN vs NAV-SVIN).

---

## [v0.46-rtos]

### Removed
- **Compile-time OCXO selection (CTI / Vectron) dropped entirely.** The `CT`
  command measures the plant gain and derives all coefficients for whatever
  oscillator is fitted, so per-OCXO defines, PID tables and the
  `DEFAULT_PWM` switch are no longer needed. The loop starts from a
  universal mid-range PWM (32767 ≈ 1.65 V) before the first `CT`.

### Added
- **Multi-variant survey-in start.** The LEA-6T and LEA-M8T accept
  different Time Mode commands (both verified in u-center), so the firmware
  tries each in turn and stops at the first ACK: `CFG-TMODE2` 0x06 0x3D
  (LEA-M8T), then the classic `CFG-TMODE` 0x06 0x1D (LEA-6T, u-blox 6). This
  auto-adapts to either module. If neither is ACKed the module is assumed to
  be already timing and is monitored anyway.

### Fixed
- **TIM-SVIN accuracy was nonsense (showed ~467 km).** The `meanV` field is
  a position *variance* in mm², not a distance — the firmware now takes its
  square root to report a 1-sigma accuracy in mm (verified against u-center:
  18113534 mm² → ~4.3 m). Survey-in duration/accuracy now read sensibly.
- **Boot hang when survey-in actually started (LEA-M8T).** The survey-in
  progress loop ran inside `gpsdo_gps_init()` — before the scheduler — and
  used `vTaskDelay()`, which hangs the system when called before
  `vTaskStartScheduler()`. It never showed on the LEA-6T because that unit
  NAKed the start and skipped the loop; the M8T ACKs it, entered the loop,
  and froze (blue LED stuck). Survey-in now only *starts* in init; progress
  is polled non-blocking from `vGpsTask` after the scheduler runs.
- **Intermittent boot hang / black displays** — `STACK_DISPLAY` raised from
  768 to 1024 words. Font scaling and the OLED clear loop had made 768
  marginal; with no stack-overflow hook this showed as a silent, sometimes-
  boots hang.
- **LEA-M8T timing module now works.** It was stuck in a 3D nav fix
  (HDOP ≈ 1) because the firmware sent it `CFG-TMODE3`, which its firmware
  (TIM 1.10, PROTVER 22) does not support. u-center confirmed the LEA-M8T
  uses the **same** `CFG-TMODE2` / `TIM-SVIN` messages as the LEA-6T. The
  timing path is unified to a single TMODE2 implementation; the separate
  `GPSDO_GPS_LEA6T` / `GPSDO_GPS_LEA8T` options are replaced by one
  `GPSDO_GPS_TIMING`, and the TMODE3 / NAV-SVIN branch is removed.
- **OLED**: the lower half of the big `GPSDO` splash (drawn with a two-row
  font) lingered behind the LMT clock — the panel is now cleared, every row
  blanked, the 2x2 font reset and the row cache invalidated when the splash
  ends. `GPSDO` and the version line are centred; footer uses
  `jmnlabs+Claude`.
- **LCD 20x4**: title/version line shifted right (two leading spaces) so the
  `-rtos` suffix is no longer truncated.
- EEPROM layout header comment corrected (143 bytes, was mislabelled 134).

### Changed
- **TFT**: the white frequency value uses a fixed-width font (font 1,
  size 3) so its digits keep a constant column position; subtitle enlarged
  and changed to `GPS-Disciplined OCXO`; logo, subtitle and the
  converging-wave animation raised; hardware checklist reveals more slowly
  with a lead-in pause so the first items are not missed; footer credit
  uses `+`. Sensor values (BMP/AHT temperature, pressure, humidity) now show
  two decimal places.

---

## [v0.45-rtos]

### Changed
- **TFT splash reworked again** to a phase-lock metaphor: the credits are
  drawn first and persist; two 2px sine waves (blue above, amber below)
  start with a visible phase offset and small vertical gap, then slowly
  converge until they coincide and merge into a single 4px green wave,
  held ~1.8 s. The hardware checklist follows.
- Serial human-readable report now shows `HDOP:TIME` in Time Mode (the
  tab-delimited machine format keeps the numeric value for plotting).

### Removed
- Redundant `SERIAL_*_BUFFER_SIZE` defines in `gpsdo_config.h` (they never
  reached the core anyway). The buffer sizes live solely in `build_opt.h`
  (`RX=256, TX=512`).

---

## [v0.44-rtos]

### Added
- **`build_opt.h`** enlarging the serial RX/TX buffers to 256 bytes
  (`-DSERIAL_RX_BUFFER_SIZE=256 -DSERIAL_TX_BUFFER_SIZE=256`). STM32duino
  applies these to the whole build including the core, which a sketch-level
  `#define` cannot reach. This prevents NMEA sentences being dropped or
  merged at 38400 baud when the GPS task is briefly preempted (the cause of
  the garbled NMEA seen on the LEA-6T).

### Changed
- **TFT boot splash reworked**: two sine waves of different colours (blue
  from the left, amber from the right) converge to the centre and merge
  into a single green 10 MHz wave — a synchronism metaphor — with the
  GPSDO logo and hardware checklist below. Timings stretched for
  readability.

### Notes
- Only GGA + RMC NMEA sentences are kept (GLL/GSA/GSV/VTG disabled), which
  together with the larger buffer keeps the bus well within budget.

---

## [v0.43-rtos]

### Added
- **Time Mode detection / `HDOP:TIME`.** A timing receiver in time-only
  mode keeps a frozen valid position but reports HDOP ≈ 99.99. Instead of
  showing that meaningless number, the displays now show `HDOP:TIME` when a
  valid position coincides with a non-meaningful HDOP (≥ 50.00). New
  `gGps.time_mode` flag.

### Changed
- **Survey-in NAK is handled gracefully.** Some timing modules (e.g.
  surplus units with a stored Time-Mode config) NAK `CFG-TMODE2/3`. The
  firmware no longer treats this as an error — it logs that the module may
  already be timing and continues; runtime Time Mode detection then reports
  the real state.
- Boot splash durations lengthened (TFT ~7 s, OLED/LCD ~4.5 s) so the
  welcome screen can actually be read.

### Fixed
- OLED splash footer no longer clips the last character (`jmnlabs/Claude`,
  spaces around the slash removed to fit the 16-column width).

---

## [v0.42-rtos]

### Fixed
- **Build error in the survey-in code** (`get_ubx_ack` called with
  class/id/timeout instead of the message-buffer pointer it expects). Both
  `ubx_start_survey_in` branches now pass the frame buffer, matching the
  function signature. LEA timing builds compile again.

### Notes
- The u-blox M8 timing module (**LEA-M8T**) is the same generation as the
  8T and uses CFG-TMODE3 / NAV-SVIN — enable `GPSDO_GPS_LEA8T` for it.

---

## [v0.41-rtos]

### Added
- **Animated boot splash on TFT**: a sweeping 10 MHz sine, the GPSDO logo,
  and a hardware checklist reconstructed from the real detection flags
  (modules show `[x]` / `[ ]`), with a discreet `jmnlabs · with Claude
  (Anthropic)` footer. Plays once, then the operating screen is drawn.
- **Boot splash on OLED** (character mode, U8x8): double-size `GPSDO`,
  version, accent line and footer.
- **Boot splash on LCD 20x4**: four-line welcome with title, subtitle and
  footer.

### Fixed
- **TFT did not update PWM / Vctl during calibration.** The display
  returned early after drawing the countdown, freezing the info grid. It
  now falls through so the PWM/Vctl cell keeps updating live during
  `C` / `CT` — matching the OLED behaviour.

---

## [v0.40-rtos]

### Added
- **LEA-6T / LEA-8T timing receiver support** (`GPSDO_GPS_LEA6T` /
  `GPSDO_GPS_LEA8T`). On these modules the firmware runs a survey-in at
  every power-up (CFG-TMODE2 on the 6T, CFG-TMODE3 on the 8T), then the
  receiver switches to a fixed-position time-only solution with a much
  cleaner 1PPS. Survey-in ends when either the minimum duration
  (`GPSDO_SVIN_MIN_SECS`, default 120 s) or the accuracy limit
  (`GPSDO_SVIN_ACC_LIMIT`, default 2000 mm) is met.
- Survey-in progress is shown on every display (`SVIN nnns nnm` on
  OLED/LCD/TFT, dashes on the LED clocks), via the new `g_svin_*` state.
- Position keeps streaming in NMEA throughout Time Mode, so location
  display and automatic timezone (`TO A`) continue to work — using the
  averaged, frozen survey-in position.
- `CHANGELOG.md` and `CHANGELOG_PL.md` are now included in the project archive.

### Notes
- NEO-6M / NEO-8M behaviour is unchanged (neither LEA option defined).

---

## [v0.39-rtos]

### Added
- OCXO warmup is now shown on every display with a live countdown
  (`WARMUP nnn s` on OLED/LCD/TFT, dashes on TM1637/HT16K33), driven by the
  new `g_warmup_active` / `g_warmup_remaining` state.

---

## [v0.38-rtos]

### Fixed
- **Steady-state PWM dither on the phase-locked algorithms (4, 5, 7, 8).**
  The dead-zone now tests the accumulated phase as well as the frequency
  error: when `|e| < 1 mHz` and `|phase| < 5 Hz·s` (≈500 ns) the loop holds
  the PWM and reports `hit`, so a locked oscillator stops being nudged by
  GPS noise every period. Small phase noise is held; real drift is still
  corrected.
- All phase algorithms now actually emit the `hit` trend on lock; FLL
  algorithms (3, 6) gained an equivalent frequency-only lock hold.
- PWM and Vctl readings on the displays now update live **during** `C` /
  `CT` calibration (a new `wait_secs_pwm` publishes PWM and samples the
  Vctl ADC each second while the main loop is busy).

---

## [v0.37-rtos]

### Changed
- `LP 8` and `LP 9` now show where those algorithms actually read their
  gains: algo 8 (hybrid) uses `g_pid[6]` (FLL branch) + `g_pid[7]` (PLL
  branch); algo 9 (NN) uses fixed network weights, so only `NS` / `IL`
  apply. Prevents the empty `g_pid[8]/[9]` from looking "untuned" after
  `CT`.

---

## [v0.36-rtos]

### Added
- Calibration progress shown on all displays: `CAL nnn s` countdown in the
  frequency field (OLED/LCD/TFT) and `CAL` on the LED clocks (TM1637 /
  HT16K33), via `g_calib_active` / `g_calib_remaining`.

---

## [v0.35-rtos]

### Added
- **`CT` (Calibrate & Tune) command.** Measures the plant gain `K` from a
  three-point PWM sweep (1.5 / 2.0 / 2.5 V) with a least-squares fit, finds
  the PWM for exactly 10 MHz, and derives PID coefficients for all
  algorithms from `K` (PLL: `Kp = 0.40/K`; FLL: `Kp = 0.35/K`,
  `Ki = Kp/300`, `Kd = Kp·73`; NN: `max_step = 0.05/K`). Sanity-checked,
  non-destructive; `ES` saves the result.

---

## [v0.34-rtos]

### Changed
- **Two-timescale PLL tuning for "fast capture, gentle phase-hold".** The
  dominant term acts on the frequency error (`Kp ≈ 0.4/K`) for quick,
  overshoot-free capture; small phase terms remove slow drift. A shared
  output stage adds a slew-rate limit (≈12 LSB/step for the PLLs, 40 for
  the hybrid) and a near-lock dead-zone, so a large overnight phase drift
  is spread over several periods instead of one big PWM jump.

---

## [v0.33-rtos]

### Fixed
- **Algorithm 9 (NN) ran away upward.** The previous "trained" weights had a
  large output bias (≈ −0.96 at zero error → constant PWM ramp). Replaced
  with an analytically constructed, bias-free, odd-symmetric network: zero
  input gives exactly zero output.
- **Algorithms 4 / 5 / 7 and the PLL branch of 8 drifted.** They used a
  rolling-window average as a stand-in for phase, which lagged the 10 s
  update by 500–1000 s and wound the integrator up. Replaced with true
  phase accumulation (`phase += (avg10 − 10 MHz)·10 s`, the exact cycle
  count), feeding back with a 10 s lag.
- The `GPS fix acquired` message now distinguishes the first fix after boot
  from a genuine recovery after fix loss.

### Added
- **Automatic timezone (`TO A`).** Local time follows the GPS position: a
  compact European civil-zone rule set plus the EU DST rule, or a solar
  `round(lon/15)` zone elsewhere. `TO <n>` keeps the manual mode. The mode
  is saved to EEPROM (byte 142, now 143 bytes total) and restored at boot.

---

## [v0.32-rtos]

### Fixed
- **Hardware detection report.** Added a robust dual-verification I2C probe
  (address ACK + 1-byte read-back). OLED and HT16K33 were previously
  reported `OK` unconditionally / on an unreliable ACK; they now report
  real presence. TM1637 and TFT are marked `enabled (write-only — not
  verifiable)`.
- **TFT frequency colour.** The green "locked" colour is now derived from
  the actual deviation from 10 MHz (≤1 mHz on the 10000 s window or ≤10 mHz
  on 1000 s), independent of the algorithm — so a locked algo 8 turns green
  too, rather than only on the rarely-emitted `hit` trend.

---

## [v0.31-rtos]

### Added
- **HT16K33 4-digit clock support** (I2C 0x70): a self-contained driver
  (HH:MM with blinking colon, `oooo` when searching), shareable with the
  LCD on the same bus — no extra pins. TM1637 retained.
- Unified startup hardware report: every optional device reports `OK` or
  `not found` in a consistent `HW:` format.
- New hardware architecture diagram in both READMEs (TFT + HT16K33).

---

## [v0.30-rtos]

### Added
- **TFT 240×320 support (ILI9341 / ST7789)** via TFT_eSPI on hardware SPI1
  (SCK PA5, MOSI PA7, RES PB15, DC PB12, CS PB13). Landscape layout: header
  bar, large colour-coded frequency, two-column info grid, sensor row, and
  a colour-coded status bar. Selective per-cell redraw keeps SPI traffic
  low. DisplayTask stack raised to 768 words when the TFT is enabled.
  Both controllers tested on hardware.

---

## [v0.29-rtos]

### Fixed
- **picDIV synchronisation.** Arming is now deferred until a GPS fix is
  present (a stopped divider with no 1PPS on Sync would otherwise hang
  dead); a dedicated flag replaces the millis-timestamp guard (wrap-safe);
  auto-arm after calibration was removed (the loop hasn't converged yet).
  Added clear serial feedback. README documents FLL phase random-walk vs
  PLL phase-lock for long-term 1PPS alignment.

---

## [v0.28-rtos]

### Fixed
- **PWM range with 3.3 V DAC.** The STM32 PWM reaches only 0–3.3 V of the
  0–4 V EFC input (82.5 %), so the accessible tuning is −10…+14.75 Hz (CTI)
  and −20…+13 Hz (Vectron). Default PWM corrected per-OCXO: 32767 (CTI,
  1.65 V midpoint) and 39718 (Vectron, 2.0 V nominal).

---

## [v0.27-rtos]

### Fixed
- **Vectron C4550A1-0213 parameters.** Corrected to its real operating
  point: 5 V supply, 0–4 V EFC, Kv = 10 Hz/V (0.504 mHz/LSB), scale factor
  1.333 vs CTI (gains × 0.75), shared default PWM.

### Changed
- `README_EN.md` renamed to `README.md` (GitHub default); `README_PL.md`
  unchanged.

---

## [v0.26-rtos]

### Added
- **OCXO selection** in `gpsdo_config.h` (`GPSDO_OCXO_CTI_OSC5A2B02` /
  `GPSDO_OCXO_VECTRON_C4550`), with per-OCXO compile-time PID defaults and
  default PWM. Falls back to CTI values if none is selected.
- `SP`, `F`, `C`, `T` documented in the help text and READMEs.

---

## [v0.25-rtos]

### Added
- `g_pressure_offset` (`PO`) and `g_altitude_offset` (`AO`) now saved to and
  restored from EEPROM (bytes 134–141, 142 bytes total).
- `V` command expanded with full author/credit information and GitHub links.

---

## [v0.24-rtos]

### Fixed
- **Bluetooth output.** All runtime messages route through an `OUT_SERIAL`
  macro (Serial2 when `GPSDO_BLUETOOTH` is defined, else USB Serial).

### Added
- Report pause/resume (`RP` / `RR`) to quiet the data stream during
  configuration.
- Algorithm PID parameters saved to EEPROM (signature `GPSD2`).
- Professional file-header documentation across all source files; README
  rewritten from scratch (project description, hardware principle, software
  architecture) in Polish and English; GitHub URL added to every file and
  to the serial banner.

---

## [v0.23-rtos]

### Added
- **Runtime PID tuning over CLI** — `LP`, `KP`, `KI`, `KD`, `IL` for
  algorithms 3–7, `BC` / `BS` for the algo 8 blend, `NS` for the algo 9 NN
  step. Coefficients moved to a global `g_pid[10]` array.

---

## [v0.22-rtos]

### Added
- Yellow LED 4-state machine (off / on / slow pulse = manual holdover /
  fast pulse = auto-holdover) and automatic holdover on GPS fix loss with
  `H` / `A` indicators on OLED and LCD.

---

## [v0.21-rtos]

### Added
- OLED row-0 clock (local time + day of week) after the version splash;
  LCD line-2 date/day rotating view. Day-of-week (Zeller) and local-time
  offset helpers.

---

## [v0.20-rtos]

### Changed
- Unified 4-character trend strings; corrected OLED/LCD frequency
  formatting; build-time guard against LCD + TM1637 together; fixed the
  André Balsa source URL.

---

## [v0.19-rtos]

- First tracked FreeRTOS port baseline: STM32F411CE BlackPill, frequency
  measurement via TIM2 ETR + TIM3 1PPS capture, ring-buffer averaging,
  PWM-DAC discipline loop, GPS/NMEA parsing, OLED / LCD / TM1637 displays,
  optional AHT/BMP/INA sensors, and the initial control algorithms.
