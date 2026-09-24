# Registro de cambios — GPSDO FreeRTOS

[English](CHANGELOG_EN.md) | [Polski](CHANGELOG_PL.md) | **Español**

📖 [Inicio del proyecto](../README.md) · Volver al [README](README_ES.md) · Manual: [MD](MANUAL_ES.md) · [PDF](MANUAL_ES.pdf)

Todos los cambios notables de este proyecto se documentan aquí.

Proyecto de **J. M. Niewiński** — <https://github.com/jmnlabs/GPSDO_FreeRTOS>
Basado en **GPSDO v0.06c** de André Balsa
(<https://github.com/AndrewBCN/STM32-GPSDO>), port a FreeRTOS y algoritmos
3–10 del autor, con **Claude Opus 5** (Anthropic), **GLM-5.3 Max** (Z.ai) y
**Qwen3.8-Max** como asistentes de programación, y diseño de PCB por Scrachi
(foro EEVBlog).

Desde el build 57 cada build se llama `GPSDO vX.YY.NNrt`: la versión, `NN` el
número de build (`BUILD_SERIAL` en `gpsdo_build_id.h`) y `rt` por el linaje
del port a FreeRTOS. El build 56, el primero con su número en el nombre, lo
escribía `GPSDO v1.07-rt56`. Las versiones anteriores llevaban el sufijo
`-rtos` — `v1.06-rtos` fue el build 42 — y conservan el nombre con que se
publicaron.

> **Nota sobre la traducción.** Las entradas desde v0.95 en adelante están
> íntegramente en español y se mantienen al día. Las anteriores están traducidas
> en su mayor parte, pero conservan pasajes en inglés: son párrafos técnicos
> detallados sobre defectos ya corregidos, y traducirlos mecánicamente produciría
> un texto peor que el original. Se dejan como están antes que estropearlos.
>
> Si algún pasaje antiguo resulta poco claro, la versión inglesa
> (`CHANGELOG_EN.md`) es la referencia completa.

---

## [v1.07] — publicado 2026-09-24 (build 57)

Publicado como build 57, la primera versión que lleva su build en el nombre:
`GPSDO v1.07.57rt`. Como antes, las entradas llegaron aquí cuando estaban
medidas, no cuando estaban escritas.

### Añadido
- **`SPAN` — dos calibraciones `CT`, una por cada span del EFC, elegidas por
  el puente de span en PB14 (build 55).** Una placa con el desplazador de nivel
  del EFC gobierna el oscilador con el span de control completo o con uno
  reducido, y son dos plantas distintas: en el prototipo V3 de Dan Wiering `CT`
  midió 8209 LSB/Hz con el span completo y 40873–46711 con el reducido, entre
  5,0 y 5,7 veces más — aunque la cifra del span completo es anterior al cambio
  de una referencia de 5 V por una de 4,096 V, y con una misma referencia las
  dos posiciones difieren en el divisor, 4,1–4,7 veces. De ese número sale cada
  coeficiente del lazo, así que
  hasta ahora una placa cuyo puente cambiaba de posición seguía con la
  calibración de la otra hasta que alguien repetía `CT`. El segundo polo del
  puente va ahora a **PB14** (puesto = a masa = REDUCED; quitado, o sin cablear
  = FULL, de modo que una placa sin ese cable conserva su única calibración
  como antes) y el firmware guarda una K por posición. Cuando el puente se
  mueve — con antirrebote, medio segundo:

  - cada coeficiente se vuelve a derivar de la K de esa posición — el mismo
    conjunto que deriva `CT`, ahora en una sola función, `algo_coeffs_from_k()`,
    que llaman ambos — y el estado aprendido en LSB (feed-forward de LRN,
    tempco del algoritmo 9) se reescala con la razón entre las dos;
  - **el código de control se reasigna para que el pin EFC conserve su
    tensión**, y la frecuencia antes del cambio es la frecuencia después:
    `c_B = p_B + (K_A/K_B)·(c_A − p_A)`, donde `(p_A, p_B)` es un par de
    códigos que se sabe que dan la misma tensión en ambas posiciones. Los
    offsets de las dos rutas los fijan la referencia, el divisor y el trimmer,
    y ninguno es visible para el firmware, así que el par se mide en lugar de
    modelarse — el código de la posición vieja en un cambio hecho con el lazo
    enganchado, emparejado con el de la nueva en cuanto su propio lazo se
    gana el enganche, o el nulo que encuentra `CT` — y se reancla en cada
    cambio, para que un error de K pivote sobre el punto donde el lazo está de
    verdad;
  - el lazo se reinicia, como en un cambio de algoritmo.

  Una posición sin calibración propia funciona con los coeficientes de la otra,
  como siempre hizo una placa de una sola calibración, mientras `CT` arranca por
  sí mismo — con fix de GPS, nunca en holdover, y hasta tres veces más, 10, 20 y
  40 minutos después de un fallo; después se detiene y lo dice. Un puente
  movido con la placa apagada se atiende al arrancar, antes de que arranque el
  lazo. El primer arranque de este build asigna la calibración
  existente a la posición que lee el pin; si luego `CT` en la otra posición mide
  la misma planta (dentro de 1,5×), la más antigua se da por medida en el sitio
  equivocado — el caso típico es una placa calibrada en REDUCED antes de
  cablear PB14 — y se olvida. `SPAN` muestra el estado, `SPAN CLR FULL|REDUCED`
  olvida una posición y el informe `DAC` lo imprime bajo la línea de la planta.
  Las ganancias manuales en LSB (`LG`, `MG`, `LTK`) son del operador y no se
  reescalan; un cambio de posición avisa de cada una que esté fijada.

  **Simulado antes de publicarse.** `tools/spansim` (nuevo) compila sin
  modificar este módulo, el almacén de ajustes, los algoritmos y la unidad de
  salud, y los gobierna con un modelo de la placa V3 — divisor 4,083:1,
  1,5967 Hz/V, envejecimiento 1,7e-10/día — bajo el algoritmo 11 con LTC 60,
  cada ciclo de alimentación como un proceso aparte que comparte una imagen de
  flash. Cada escenario se repite en un build sin detección de span, que es el
  firmware tal como era:

  | | con detección de span | una calibración |
  |---|---|---|
  | cambio entre dos posiciones calibradas: aterriza en | 3e-12 … 1,8e-11 | 3e-8 … 1,2e-7 |
  | excursión de fase / reenganche | 13–49 ns / 399 s | 3,5–11,5 µs / 665–877 s |
  | puente movido con la placa apagada: arranca en | 8,1e-12 | 1,2e-7 |
  | arranque en frío, 3e-8 de retrace, luego REDUCED: aterriza en | 5,6e-10 | 3,3e-8 |

  La cifra del arranque en frío es el error propio de la reasignación: los dos
  `CT` se equivocaron un 2 % y un 10 % en sentidos opuestos, y el lazo estaba a
  417 códigos del par.

  Se guarda en 12 bytes añadidos al final del bloque de ajustes (380 → 392
  bytes, cada campo anterior en su offset de siempre, comprobado con
  `offsetof` bajo arm-none-eabi). Un registro más antiguo devuelve los campos
  nuevos a 0, que significa «nunca registrado»; el build 54 leyendo un registro
  del build 55 toma los primeros 380 bytes, así que volver atrás también es
  seguro.
- **`VS` — qué está midiendo el divisor de PA0 (build 49).** `VS VCC` o
  `VS VREF`, se guarda solo junto al grupo de la ruta de salida. Desde la placa
  V3 un puente en la parte alta de ese divisor de 4k7 + 4k7 selecciona la
  **referencia de tensión** en lugar del raíl de 5 V: mismo pin, mismo divisor,
  mismo escalado, objeto distinto. El firmware no ve un puente, así que se le
  dice — el mismo arreglo que con `DV`, `AV` y la ruta del DAC, y por la misma
  razón: la única huella que deja un puente es una tensión que no está donde el
  firmware la esperaba, y eso solo sirve si al firmware se le dijo qué esperar.

  Decírselo compra la comprobación. En `VREF` el informe `DAC` imprime la
  lectura junto a lo que afirma `DV` y protesta pasado el 5 %, lo que caza una
  referencia ausente, caída o sencillamente distinta de la que se montó. Esto
  último no es un fallo sutil: una pieza de 5,000 V donde `DV` dice 4,096 deja
  toda cifra de frecuencia que imprime la placa una quinta parte corta, en
  silencio. La etiqueta del TFT sigue también al puente — llamar «Vcc» a una
  referencia de 4,1 V se leería como una alimentación medio hundida.

  Por qué un puente y no un pin propio: **no hay ningún canal de ADC libre**.
  El convertidor alcanza PA0–PA7, PB0 y PB1 en este encapsulado, y con SPI1
  llevando la pantalla los diez están ocupados. Liberar uno obligaría a mover el
  reloj del AD5680 fuera de PB0: un pin comprado al precio de romper las placas
  ya construidas.
- **`BL` — atenuación de la retroiluminación del TFT (build 48).** 30..100 %,
  se guarda solo junto con las banderas de pantalla. Gobierna un MOSFET de
  canal P en **PB5** (TIM3 CH2, 20 kHz) entre el raíl de 3,3 V y el ánodo de
  los LED del panel: puerta a través de ~47 Ω, 100 kΩ a masa para que el estado
  esté definido mientras el pin está en alta impedancia tras el reset, y
  10-47 µF de capacidad local en el drenador.

  La etapa **invierte** — puerta baja es brillo máximo — así que el firmware
  escribe el complemento, lo que convierte `BL 100` en una comparación igual a
  cero: el pin queda estáticamente bajo y la etapa no conmuta en absoluto. Ese
  es justamente el objetivo. El raíl de 3,3 V alimenta VDDA y con él la
  referencia del ADC, la lectura de fase y el monitor de Vctl, de modo que el
  brillo máximo es el ajuste *más silencioso*, no el más ruidoso; atenuar
  cambia un poco de ruido en el raíl por carga y por calor en una caja acoplada
  térmicamente al OCXO.

  El suelo del 30 % existe por una razón que no es eléctrica: por debajo de un
  tercio aproximadamente el panel deja de ser legible en vez de atenuarse de
  forma útil, así que un `3` tecleado por error dejaría al operador sin poder
  distinguir una pantalla tenue de una placa muerta. Se compila con cualquier
  TFT y **cede PB5 al generador de prueba de 2 kHz** cuando este se ha
  habilitado explícitamente: un ajuste por defecto no debe quitarle un pin a
  una decisión deliberada. `TIM3` estaba libre: varios comentarios afirmaban
  que allí vivía la captura de 1 PPS, pero esa es TIM2 canal 3 en PB10.

  Se persiste en el **último byte de relleno del bloque de ajustes**, offset
  319, junto a `dac_path` en 318 — `sizeof(SettingsBlock_t)` es 376 antes y
  después y cada campo posterior conserva su offset, verificado con `offsetof`
  bajo arm-none-eabi y no a ojo. Sin subir `SETTINGS_VER`, porque la recarga
  exige coincidencia exacta de versión *y* tamaño, y subirla tiraría el PID, el
  LC y la zona horaria de todo el mundo por un byte. Cero significa «sin
  fijar», así que un registro escrito antes de que el campo existiera pide el
  valor por defecto y no una pantalla apagada.
- `DV` — voltios a código completo para el «ordenado» del informe `DAC`
  (2,50..5,50 V, por defecto 3,30 = el modelo PWM). En un DAC externo de 5 V
  el viejo modelo fijo de 3,3 V rebajaba el ordenado 1,5× y el informe
  marcaba MISMATCH en cada lectura. Se guarda solo.
- `AV` — ratio del divisor antes del pin ADC (1,00..10,00, por defecto
  directo). Escala el «medido» y toda visualización de Vctl (línea de
  telemetría, CSV, TFT). Se guarda solo. Con un divisor de medida en la
  salida del DAC (10k+10k en las placas AD5680), `DV 5.00` + `AV 2.00`
  hacen exacto el check de MISMATCH.
- `SETTINGS_VER` 6: ambas escalas persisten con el bloque ALGO; los
  registros v5 migran automáticamente con los campos nuevos por defecto.

### Cambiado
- **El número de build pasa dentro de la versión: `GPSDO v1.07.57rt` (build
  57).** La versión, el build como tercer número y `rt` por el linaje
  FreeRTOS, en lugar del `GPSDO v1.07-rt56` del build 56. Solo cambia la
  escritura: el nombre sigue siendo `g_fw_version`, se compone solo en el
  sketch, lo imprimen los mismos sitios y mide exactamente lo mismo, así que
  toda pantalla que tenía sitio para `-rt56` lo tiene para `.57rt`. Las
  cabeceras de los fuentes lo siguen (`Part of GPSDO v1.07.57rt`) — cambiarlas
  fue una edición, así que por ahora todas dicen 57 — y también los títulos de
  los documentos, incluido el README de v1.06 en la carpeta del sketch
  (`v1.06.42rt`). El tuner lee las tres escrituras, `v1.06-rtos`,
  `v1.07-rt56` y `v1.07.57rt`, compara solo la versión y omite el `build N`
  aparte en la línea de estado cuando el nombre ya lo lleva.
- **El nombre del firmware lleva su build: `GPSDO v1.07-rt56` (build 56).**
  Sustituye a `GPSDO v1.07-rtos`. `rt` sigue marcando el linaje del port a
  FreeRTOS; el número es `BUILD_SERIAL`, así que subir el build renombra el
  firmware por sí solo, y una captura, una foto de la pantalla o una línea del
  tuner dicen de qué build salieron. Se compone una sola vez en el sketch como
  `g_fw_version` — el sketch es la única unidad que incluye
  `gpsdo_build_id.h`, así que subir el build sigue recompilando solo el
  sketch — y lo imprimen desde esa misma cadena el rótulo de arranque, `V`, la
  cabecera de `H`, la cabecera del TFT y las pantallas de inicio del OLED y del
  LCD, que tienen sitio para él hasta el build 999. La marca de compilación
  sigue terminando en `build 56`, que es lo que leen los tuners anteriores a
  este. `PROGRAM_VERSION` es ahora solo la versión, `v1.07`. La cabecera de
  cada fichero fuente dice `Part of GPSDO v1.07-rt56`, y el número es el build
  que cambió ese fichero por última vez (`gpsdo_flash_ring_core.c` todavía
  decía v1.06). El build 57 pasó el número dentro de la versión — ver arriba.
- **El tuner acepta ambos nombres y dice el build una sola vez (build 56).**
  Su patrón de versión ya aceptaba cualquier sufijo tras la versión, así que
  `v1.07-rt56` y `v1.06-rtos` se analizan los dos y solo se compara `1.07`; la
  línea de estado omite el `build N` aparte cuando el nombre ya lo lleva.
- **Tabla de zonas regenerada desde IANA 2026d, y el generador comprueba ahora
  su propio trabajo (build 53).** 503 zonas, 88 reglas, ~3,34 KB de flash — la
  misma huella que la tabla 2026c a la que sustituye.

  **Las tres «filas rotas» reportadas contra la tabla anterior eran una falsa
  alarma, y la razón merece quedar escrita.** La regla de cada zona es la cadena
  POSIX del final de su fichero TZif, que es la regla que aplica *después de la
  última transición que el fichero almacena* — no necesariamente la vigente el
  día en que se genera la tabla. Para una zona con un cambio legislado por
  delante ambas difieren, y si eso es un fallo depende por completo de la
  pregunta que se haga.

  Preguntado como *«¿es correcta esta regla para el año 2026?»*, seis zonas
  parecen rotas: Columbia Británica, Alberta y los Territorios del Noroeste
  dejan de aplicar horario de verano el **1 de noviembre de 2026**, así que
  `America/Vancouver` lleva `MST7` y `America/Edmonton` lleva `CST6`, mientras
  que los primeros diez meses de 2026 todavía tuvieron PDT y MDT. Preguntado
  como *«¿es correcta para los años que esta tabla va a pasar en la flash?»*,
  las seis son correctas, y el pie es exactamente la elección adecuada: una
  tabla generada hoy debe llevar la era a la que *entra*, no la que deja.
  Africa/Casablanca es la misma historia con ocho días de antelación: IANA
  modela Marruecos como UTC+0 permanente desde el 20 de septiembre de 2026, que
  es justo lo que dice `<+00>0`.

  **Medido con el propio evaluador del firmware, no por argumento.** Cada
  transición de cada zona de la tabla, desde hoy hasta finales de 2028, sondeada
  dos minutos antes y un minuto después: **778 transiciones en 484 zonas, 1556
  sondeos, y el firmware coincide en todos** salvo en los dos últimos minutos de
  la era actual de Marruecos. Es además la prueba más dura que ha pasado la
  corrección de DST del build 52.

  El generador ya no depende de que alguien haga la pregunta correcta. Tras
  escribir la tabla verifica cada regla contra el tzdata de la propia máquina
  **dos años por delante** — la era en la que la tabla realmente vivirá — e
  imprime o bien «todas coinciden» o las zonas que no. La comprobación compara
  desfases en vez de reimplementar el motor de reglas del firmware, porque una
  segunda implementación puede equivocarse en el mismo sitio y coincidir
  consigo misma. Lee AMBOS desfases de una regla, no sólo el estándar, porque
  Irlanda escribe su zona como `IST-1GMT0` — IST es el estándar y GMT un horario
  de verano negativo, al revés que en todas partes y perfectamente legal. Una
  comprobación que lee sólo el primer desfase declara Dublín roto en cada
  ejecución, y una comprobación que grita con una zona correcta es peor que
  ninguna: así es como se pasa de largo una queja real. Verificada en ambos
  sentidos — callada sobre la tabla tal como se genera, y detecta un desfase
  equivocado o una regla de DST ausente cuando se plantan.
- **Nombres de archivo `gpsdo_` uniformes y una estructura de repositorio
  legible (build 48).** Diecisiete archivos renombrados — `dac_ext`,
  `flash_ring`, `flash_ring_core`, `live_store`, `settings_store`, `tz_table`,
  `ubx_timtp`, `TeeSerial`, `build_id` y `GPSDO_algorithms` — de modo que todo
  fuente propio del proyecto lleva el prefijo `gpsdo_` en minúsculas y un
  listado de directorio separa el proyecto de aquello sobre lo que se apoya.
  Los guardas de los encabezados siguen a los nombres.

  **Cuatro archivos conservan su nombre a propósito**, cada uno porque el coste
  de cambiarlo es silencio y no un error de compilación: `GPSDO_FreeRTOS.ino`
  (Arduino exige que el archivo del sketch se llame como su carpeta),
  `build_opt.h` (el compilador de Arduino busca ese nombre para recoger flags
  adicionales: renombrado, las flags desaparecen y sigue compilando, solo que
  distinto), `STM32FreeRTOSConfig.h` (la biblioteca lo incluye por ese nombre)
  y `TM1637Display.*` (copia de una biblioteca ajena; el nombre de upstream es
  lo que mantiene legible un futuro diff).

  `doc/`, `tools/` y `README.md` salen de la carpeta del sketch al raíz del
  repositorio, así que la carpeta del sketch contiene código y nada más, y con
  ellos llega un `.gitignore`. Excluye la salida de compilación, las cachés de
  Python y el directorio de trabajo de los PDF — y, deliberadamente, el
  material de trabajo de `doc/`: la correspondencia, las auditorías y las
  listas de tareas nombran personas y citan correo privado, y un `git add .`
  descuidado no debería publicarlos.

  De todo ello se encarga `tools/gpsdo_restructure.py`. Simulación por
  defecto, idempotente, y **se niega a mover `tools/` mientras algún banco de
  pruebas localice las fuentes contando directorios** — `hostcheck.sh`,
  `loopsim/run.sh` y `algoswitch/run.sh` decían «dos niveles arriba», cierto
  solo mientras `tools/` vive dentro de la carpeta del sketch; después apunta
  al raíz, y hostcheck habría compilado un árbol vacío informando de éxito.
  Los tres suben ahora hasta el `.ino`, que es correcto en ambas disposiciones.

  Verificado ejecutando el script sobre un árbol v1.06 y luego sobre el árbol
  v1.07 que hostcheck ya pasa 14/14 — donde informa de que no queda nada por
  hacer, que es la forma de afirmar que su resultado y el árbol verificado son
  lo mismo. Un fallo encontrado por el camino: el script recorre `tools/`, vive
  en `tools/` y lleva todos los nombres antiguos en su propia tabla de cambios,
  así que la primera ejecución convirtió esa tabla en una lista de identidades.
  Ahora se excluye a sí mismo.
- **La tensión de control ya no pasa por `analogWrite()` (build 48).** Ambas
  rutas de salida son ahora dueñas de TIM4 mediante registros — `pwm24_begin()`
  con la reproducción por DMA, el nuevo `pwm16_begin()` sin ella. La portadora
  y la resolución quedan deliberadamente **sin cambios**: 100 MHz / 50 000 =
  2,000 kHz, la misma cifra que producía `analogWriteFrequency(2000)`, de modo
  que a ninguna placa se le mueve por esto el CT, el filtro ni la ganancia de
  la planta.

  Con ello desaparecen dos cosas. El `analogWrite()` del núcleo llama a
  `pwm_start()`, que recalcula el preescalador y el auto-reload a partir de la
  frecuencia pedida **en cada llamada** — la tensión de control se escribe una
  vez por segundo durante toda la vida de la placa, así que eso era el
  temporizador reconstruido una vez por segundo para cambiar un solo valor de
  comparación: una oportunidad de perturbar la salida cada segundo, comprada a
  cambio de nada. Y `analogWriteFrequency()` es una **global** del núcleo: se
  aplica al pin que se escriba a continuación, así que en cuanto existe un
  segundo PWM — la retroiluminación de arriba — ambos se pelean en silencio por
  un único ajuste.

  La ruta simple mapea además ahora **fondo de escala a fondo de escala**, la
  misma regla que ya siguen la tabla de dither y el DAC externo: el código
  65535 es una comparación igual al periodo. El núcleo dividía por 65 536, con
  lo que el LSB superior quedaba inalcanzable; la diferencia de 15 ppm en mitad
  del rango queda muy por debajo de lo que el CT resuelve.
- CT en dos pasadas con plantas suaves: cuando la K de la primera pasada
  baja de 0,12 mHz/LSB, una segunda tura de tres puntos vuelve a medir sobre
  un reparto de códigos más ancho (objetivo ~0,8 Hz de excursión, centrado en
  el código de 10 MHz deducido, nunca más estrecho que la primera y nunca a
  menos de 1000 LSB de cualquiera de los dos extremos de escala). En esas
  placas el barrido fijo de 20 480 LSB mueve el oscilador bastante menos de
  un hercio y el ajuste de pendiente se desviaba un 10-50% hacia arriba
  frente a la verdad del DMM. El suelo de plausibilidad de la K final pasa
  de 0,02 → 0,01 mHz/LSB — defendible ahora que una puerta gruesa caza la
  basura sin señal antes de creer cualquier K.

### Corregido
- **`CT` y `C` reinician el lazo en todos los builds (build 57).** El build
  55 hizo que `CT` reiniciara el lazo tras centrarlo, pero puso el reinicio en
  el gancho de `CT` del módulo de span, que un build sin `GPSDO_SPAN_SENSE`
  compila vacío — así que allí la copia del código que guarda el lazo, todavía
  la de antes del barrido, seguía tirando hacia ella. El reinicio está ahora en
  el propio `CT`, y `C`, la calibración de dos puntos, recibe la misma línea,
  porque también escribe un código nuevo y deja al lazo la misma copia
  obsoleta. En el build de una sola calibración de `spansim`
  (`GPSDO_SPAN_SENSE` desactivado), un `CT` justo después de mover el puente
  de span: sin el reinicio el lazo tiró de vuelta hacia el código que `CT`
  acababa de dejar, 3,1e-8 en el peor momento, volvió a enganchar 1416 s
  después del cambio y anduvo a 1,1e-9 RMS en las dos horas siguientes; con
  él, 5,0e-9, 1190 s y 2,2e-10. Un build con la detección de span activada se
  comporta exactamente igual que antes: la mitad de span de la salida de
  `spansim` es idéntica byte a byte. `tools/spansim` hace la llamada donde
  ahora la hace `CT`.
- **`DV` perdía su tercer decimal en cada reinicio (build 57).** Se guardaba
  en centivoltios, así que `DV 4.096` — el ADR4540 de la V3 de Dan Wiering —
  volvía de un reinicio como 4.10, un sesgo del 0,1 % en la columna
  «ordenado» del informe `DAC`; y la consulta y el eco imprimían dos
  decimales, así que `DV` mostraba 4.10 tanto si guardaba 4.096 como 4.10.
  `DV` se guarda ahora también en milivoltios, en un campo añadido al final
  del bloque de ajustes (392 → 396 bytes, sin cambio de versión), y `DV` y el
  informe `DAC` lo imprimen con tres decimales. El campo en centivoltios se
  sigue escribiendo con el mismo valor, así que un build anterior que lea un
  registro nuevo conserva el `DV` de dos decimales de siempre, y un registro
  de un build anterior se recupera exactamente como antes. Probado en el host
  contra el almacén de ajustes real: todo `DV` de 2.500 a 5.500 en pasos de
  0,1 mV vuelve al milivoltio; un registro de 392 bytes cae al valor en
  centivoltios; el almacén del build 56 lee el registro de 396 bytes (4.10) y
  el build 57 relee lo que el build 56 guardó después.
- **`CT` reinicia el lazo después de centrarlo (build 55).** `CT` pone un
  código nuevo en el pin, pero cada lazo guarda su propia copia de dónde
  debería estar el pin — el integrador del algoritmo 11, el objetivo absoluto
  del algoritmo 10 — y esa copia seguía teniendo el código de antes del
  barrido, así que los primeros pasos del lazo tras `CT` volvían hacia él y
  deshacían el centrado. `CT` pide ahora el mismo reinicio que un cambio de
  algoritmo. Medido en `spansim` con un `CT` que acierta mientras el lazo ya
  estaba trabajando: sin el reinicio el código quedaba veinte minutos después a
  42 LSB del nulo de `CT`, en 1,0e-10, y el siguiente cambio de span,
  reasignado desde ahí, aterrizaba a 1,1e-10; con él, 1,2e-11 y 3,7e-12. El
  reinicio vive en el gancho de `CT` del módulo de span, así que un build
  compilado sin `GPSDO_SPAN_SENSE` — desactivado solo por elección, la
  configuración que se entrega lo trae activo — conserva el comportamiento
  antiguo. El build 57 lo movió al propio `CT`, para todos los builds — ver
  arriba.
- **`tools/loopsim/run.sh` imprimía la sd de seguimiento bajo un encabezado de
  sd de fase (build 55).** La línea del informe lleva un segundo `sd` desde que
  se añadieron las estadísticas de seguimiento (`track sd 0.71 LSB`), y el
  `.*sd` voraz de la tabla cogía ese: cada tabla que el script ha impreso desde
  entonces era la sd de seguimiento en LSB bajo un encabezado que promete sd de
  fase en ns, y nada parecía mal porque ambos son números pequeños y
  positivos. Anclado en el campo anterior, la tabla del README vuelve a
  reproducirse (ventana algo-12, `MG 0`: 3,70 / 2,93 ns frente a los 3,57 /
  2,95 registrados, con el árbol cambiado desde entonces). Las conclusiones
  sacadas de tablas de `run.sh` desde que aparecieron las cifras de seguimiento
  merecen una segunda mirada; el programa loopsim en sí siempre acertó.
- **`hostcheck` no veía una función `span_`, `algo_` o `health_` que faltara
  (build 55).** Su comprobación de símbolos sin definir mira solo los nombres
  con los prefijos del propio firmware, y esos tres no estaban en la lista: con
  la definición de `span_poll` renombrada, cada fila seguía diciendo «clean».
  Añadidos — junto con una lista `DEFAULT_ON` para los interruptores que la
  configuración entregada trae activos (`GPSDO_SPAN_SENSE`), para que las filas
  existentes sigan compilando las placas cuyo nombre llevan, y dos filas nuevas
  que compilan sin él. 18 filas, todas limpias bajo arm-none-eabi.
- **El algoritmo 11 martilleaba el EFC durante cinco minutos después de estar ya
  en casa (build 54).** `s_locked` controlaba el prefiltro de fase — `if
  (!s_locked) filt = 1u;` — y `s_locked` no es una afirmación sobre la fase. Es
  un cronómetro: la prueba de enganche exige que fase Y frecuencia estén dentro
  de sus ventanas *de forma continua* durante `LPF × LTC` segundos, cinco
  minutos con los valores por defecto, así que un lazo que ya está en casa sigue
  formalmente desenganchado cinco minutos más y pasaba cada uno de esos segundos
  en modo rápido, sin suavizado, respondiendo a cada muestra de ruido del
  detector con ganancia plena.

  Encontrado en la captura de banco del 11.09, en el momento en que el algoritmo
  13 cedió el paso al 11 con la fase en 2,4 ns: **299 segundos de `PLL` durante
  los cuales la fase filtrada nunca salió de ±6,7 ns frente a una ventana de
  100 ns — y el PWM se movió una media de 4,5 LSB por segundo, hasta 17 en un
  segundo, y más de 4 en 145 de esos 298 segundos.** Diecisiete LSB son 5,4e-10
  en esta placa. El mismo lazo, enganchado, durante 14,6 horas: 0 o 1 LSB cada
  segundo, 2 LSB cuarenta veces, nunca más. Un orden de magnitud de ruido de
  salida salido de una bandera que sólo significaba «cuánto tiempo lleva esto
  bien».

  El prefiltro sigue ahora a la fase y no al cronómetro: rápido mientras la fase
  está fuera de la ventana, suavizado en cuanto está dentro **y lleva ahí tanto
  tiempo como el que el filtro va a promediar**. Esa segunda mitad no sobra — la
  prueba de ventana sola empeoraba las cosas, porque una fase que va *de salida*
  también pasa por la ventana, y suavizarla ahí es como un lazo se entera tarde
  de una perturbación (el banco de conmutación midió una excursión de 600 s
  acabando en 340 ns en vez de 241). La permanencia es el propio `filt`, que es
  la única elección coherente consigo misma: promediar N segundos tiene sentido
  exactamente cuando los últimos N segundos describían lo mismo.

  **Reproducido sobre la planta medida del 26.08 con los ajustes de la propia
  placa** (LTC 60, LFD 3, LPL 100, LPF 5, LG 2,130), sobre los 300 s que el lazo
  pasa formalmente desenganchado con la fase ya en casa:

  | | paso medio | peor segundo | segundos > 4 LSB |
  |---|---|---|---|
  | antes | 5,88 LSB | 22 LSB | 159 de 300 |
  | después, ventana entera | 0,49 LSB | 13 LSB | 7 de 300 |
  | después, cumplida la permanencia | **0,21 LSB** | **1 LSB** | **0** |

  Los primeros veinte segundos conservan por construcción el comportamiento
  rápido anterior: el lazo sigue siendo rápido hasta que tiene la evidencia para
  permitirse suavizar. Todo lo demás queda igual y se comprobó en vez de
  suponerse — tiempos de adquisición idénticos en los cuatro escenarios
  (estabilizado, 800 ns fuera, 1500 ns fuera, detector contra el tope), sd de
  fase idéntica en cinco semillas de ruido, algoritmo 12 bit a bit, y el banco de
  conmutación de vuelta a las cifras que imprimía antes del cambio.

- **El algoritmo 13 daba una patada al DAC un segundo después de cada reinicio
  (build 54).** Medido en el banco en el relevo del algoritmo 11 en la captura
  del 11.09 — PWM 40835 → 40978 → 40871, un **paso de 143 LSB en un segundo,
  4,6e-9 de salida**, en una placa que estaba enganchada al nanosegundo un
  segundo antes. Rastreado en el simulador hasta su causa real, que no era la
  evidente:

  La primera medida de frecuencia del filtro tras un reinicio es una EMA del
  **contador de un segundo cuantizado a hercios enteros**, y `P[1][1]` sigue en
  el prior ancho de arranque en frío, así que el estado de frecuencia se mueve
  un 61 % del camino hacia un número que es sobre todo rizado de la EMA —
  3,79 ns/s, que son 119 LSB de corrección en esta placa. El control la aplicó
  entera; al segundo siguiente se llevó casi toda de vuelta. `lim_lsb` es toda
  la banda del detector y nunca actúa en un lazo estabilizado, así que el
  limitador se quedó mirando.

  Dos cambios, y la medida dice que ambos se ganan su sitio. **El horizonte
  corto de control ya no se engancha con la primera lectura en banda** — tras
  una muestra la estimación *es* esa muestra — sino que espera a que el filtro
  haya promediado un horizonte de medidas, que es la escala de tiempo propia del
  lazo y no una constante nueva. Y **la corrección está ahora limitada en
  velocidad de cambio** además de acotada: el límite dice hasta dónde puede ir,
  éste dice con qué rapidez puede cambiar — la banda del detector repartida en
  un horizonte de control. En una placa estabilizada `du` cambia mucho menos de
  un LSB por segundo, así que nunca actúa; el resto va al mismo acarreo que usa
  la vía sub-LSB, de modo que un segundo limitado se retrasa, no se pierde.

  Peor paso de un segundo en el primer minuto, planta nocturna del 03.09, cinco
  semillas de ruido:

  | | s1 | s2 | s3 | s4 | s5 |
  |---|---|---|---|---|---|
  | antes | 48 | 78 | **175** | 120 | 16 |
  | sólo límite de velocidad | 33 | 52 | 28 | 63 | 16 |
  | ambos | **13** | **27** | **9** | **19** | **10** |

  La sd de fase, dPWM, el peor paso de toda la tirada y el ADEV en cada tau
  quedan sin cambios.

- **La nota de `AP` recomendaba algoritmos que no pueden hacer lo que pide
  (build 54).** Decía «use a PLL algorithm (LA 4/5/7) to keep phase locked
  long-term» — un consejo anterior a los algoritmos 10–13. Esos tres actúan
  desde el contador y no tienen detector de fase alguno, así que no pueden
  sostener el divisor donde `AP` lo deja; los lazos LTIC sí pueden, y además se
  rearman solos cuando el detector dice que el divisor ha perdido sincronismo.
  Dan Wiering eligió el algoritmo 7 para una tirada nocturna y luego se preguntó
  por qué veía eventos de rearme — los algoritmos 0–9 no rearman nunca, y esta
  línea es la razón más probable de que estuviera ahí.

- **`loopsim` aprendió a informar de lo que el lazo le hizo al pin (build 54).**
  El estadístico del actuador era un RMS de toda la tirada, que promedia hasta
  desaparecer un transitorio de un segundo — y un paso de un segundo es
  exactamente lo que un analizador de fase dibuja como pico. Ahora imprime
  además el peor segundo aislado y el peor segundo del primer minuto, donde
  viven los transitorios de reinicio. Los mandos del algoritmo 11 — `LTC`,
  `LFD`, `LPL`, `LPF` y `LG` — están expuestos como variables de entorno para
  poder reproducir una captura con los ajustes que la placa tenía de verdad, y
  el volcado lleva el control aplicado junto a la fase. Todos los números de las
  dos entradas anteriores salieron de estas adiciones.
- **Regenerar la tabla de zonas devolvía el guard de cabecera anterior al
  renombrado (build 53).** El fichero pasó a ser `gpsdo_tz_table.h` en la
  reestructuración de v1.07 y su guard pasó a `GPSDO_TZ_TABLE_H`, pero el
  generador seguía escribiendo `TZ_TABLE_H` — de modo que cada pulsación del
  botón lo deshacía en silencio, y `TZ_TABLE_H` es justo la clase de nombre
  genérico que también elige otra biblioteca. Corregido en el generador y en la
  tabla entregada.
- **Cada cambio de horario de verano ocurría en el momento equivocado, por el
  tamaño del propio desfase (build 52).** `tz_offset_now()` comparaba las horas
  de transición de la zona — que POSIX escribe en hora LOCAL — contra UTC, y un
  comentario llamaba a esa diferencia irrelevante para un reloj de pared. No es
  irrelevante, y no es «una hora más o menos» como afirmaba el comentario: el
  error es exactamente el desfase vigente en la frontera. Medido contra los
  instantes reales de 2026, antes del cambio:

  | zona | primavera | otoño |
  |---|---|---|
  | Europe/London | exacto | **1 h tarde** |
  | Europe/Berlin, Warsaw | 1 h tarde | 2 h tarde |
  | Europe/Athens | 2 h tarde | 3 h tarde |
  | America/New_York | **5 h ANTES** | 4 h antes |
  | America/Denver | 7 h antes | 6 h antes |

  El cambio de primavera de Londres salía exacto sólo porque GMT *es* UTC, y esa
  coincidencia es lo que mantuvo oculto el fallo: la zona que el autor habría
  probado primero es la única en la que media falta se cancela. Nueva York es el
  caso que nadie podría haber pasado por alto — el reloj saltaba a las 21:00 del
  sábado por la noche, cinco horas antes que el país.

  Nunca hubo el problema del huevo y la gallina que temía el comentario
  antiguo. Una regla de inicio POSIX se escribe en hora local ESTÁNDAR y una de
  fin en hora local de verano; el desfase de cada una se conoce antes de la
  comparación, así que cada frontera se convierte a UTC restando el suyo. Sin
  iteración y sin conjeturas. La comparación corre ahora sobre un ordinal de día
  del año en vez de una fecha empaquetada, porque la resta puede cruzar la
  medianoche — Australia/Sydney empieza a las 02:00 AEST, que son las 16:00 UTC
  del día *anterior* — y las fronteras se envuelven dentro del año.

  **Verificado contra los datos IANA de esta máquina, no por argumento.** Cada
  zona de la tabla integrada se recorrió minuto a minuto durante 2026 y sus
  transiciones se compararon con `zoneinfo` de Python: **416 zonas coinciden
  ahora al minuto**, mientras que antes del cambio la misma prueba fallaba en
  los instantes de frontera en todo lo que no fuera UTC. Las cinco restantes son
  las tres filas malas de la tabla descritas en la entrada siguiente, más
  Casablanca y El Aaiún, cuyo horario de verano sigue el Ramadán y no puede
  escribirse como regla POSIX — algo que el firmware ya dice en voz alta al
  seleccionarlas.

  Encontrado porque Dave (Solder_Junkie) en EEVblog preguntó si `TZ London`
  cambiaría solo a finales de octubre. Lo hacía, una hora tarde, y la pregunta
  bastó para que alguien por fin lo midiera.

- **El brillo del TM1637 estaba puesto en 1 de 7 bajo un comentario que decía
  5/7 (build 52).** Un `setBrightness(1)` pelado enterrado en la tarea de
  pantalla, sin manera de que quien monta la placa sepa si un módulo apagado es
  el firmware o la pieza. Ahora es `TM1637_BRIGHTNESS` en `gpsdo_config.h`, por
  defecto 4, junto a `HT16K33_BRIGHTNESS`, donde uno lo buscaría, y documentado
  en la sección del reloj LED del manual.

  **Las dos configuraciones TM1637 las compila ahora `hostcheck`, y nunca lo
  hizo.** Así sobrevivió esto, y no es lo único que sobrevivió en ese bloque: la
  máscara de dos puntos de seis dígitos sigue llevando al lado un comentario que
  dice que el valor que usa el código no enciende ningún dos puntos. Una
  configuración que nadie compila es una configuración que nadie lee. Dieciséis
  configuraciones, frente a catorce. (El LCD 20x4 sigue sin fila — necesita
  stubs de `hd44780` que aún no existen. Un hueco declarado, no silencioso.)

- **El tuner dibujaba una estimación de fase sólo para el algoritmo 13; ahora la
  dibuja cada lazo LTIC que tiene una (build 52).** El algoritmo 11 mantiene una
  fase filtrada (`s_phase_filt`, un suavizador exponencial cuya constante es
  `time_const/filter_div` una vez enganchado y 1 mientras adquiere) y la ha
  impreso como `phase=` desde siempre — el tuner simplemente nunca la dibujó,
  porque los algoritmos 10 y 11 compartían una familia de gráficas y es la
  familia la que elige la superposición. Ahora son familias distintas.

  El panel del algoritmo 12 estaba peor que vacío: mostraba `ph`, que *parece*
  el campo correcto y es la lectura cruda del detector convertida a `int16_t` —
  la medida cuantizada a nanosegundos enteros, etiquetada «error de fase». El
  panel superior muestra ahora `dph`, la misma medida con su decimal, y la
  estimación va encima.

  Esa estimación hubo que exponerla, porque el algoritmo nunca publicó ninguna:
  `mlacc_stats_t` gana `est_ns`, la propia respuesta del acumulador —
  `last_phase` normalizada como la normaliza la corrección misma — impresa en la
  línea Learn como `est=`, añadida al FINAL de los campos del algoritmo 12 para
  que ninguna expresión regular existente se desplace. Comprobado sobre la
  reproducción del 26.08 en vez de supuesto: en 35 correcciones la estimación
  sigue a la fase verdadera con correlación 0,992 y una pendiente de ajuste de
  **1,048** (un error de nivel habría dado 0,5 o 2,0), y su desviación RMS
  respecto de la verdad es 0,70 ns frente a 2,45 ns de ruido del detector. Esa
  razón es toda la tesis del algoritmo, y es lo que mostrará la separación entre
  las dos trazas.

  **El algoritmo 10 no recibe superposición, a propósito.** El lazo de tres
  etapas trabaja sobre la lectura cruda y no guarda fase filtrada en ninguna
  parte; su suavizado vive en el integrador del PID, que es un estado de control
  y no una estimación de nada. Dibujarlo sería inventar un estimador que el
  algoritmo no tiene.

  El retardo de la superposición es de 1 muestra en los tres. Para el algoritmo
  13 se midió por correlación cruzada sobre una captura de 7,5 h; para el 11 y
  el 12 se toma por estructura — el mismo productor, el mismo consumidor, la
  misma carrera — y así lo dice el código. Unos minutos de telemetría `RH` bajo
  cada uno lo confirmarían.
- **Dos indicadores leían memoria sin inicializar, y un tercero pasaba un
  puntero nulo a `strncpy` (build 51).** Ambos fallos eran alcanzables en una
  placa corriente; ninguno requería una combinación rara de opciones.

  `set_trend(0)` — el algoritmo 11 y el algoritmo 13 se niegan a funcionar sin
  la calibración TIC, y ambos lo decían llamando a `set_trend(0)`, es decir
  `strncpy(dest, NULL, 4)`. Eso es comportamiento indefinido, no un indicador
  en blanco, y el camino que llega ahí es el ordinario: es lo que hace toda
  placa nueva en su primer arranque, antes de haber ejecutado `LC` alguna vez.
  Ahora ambos ponen `NoCT`, la palabra que el algoritmo 13 ya usaba una línea
  más abajo para el otro coeficiente que falta — de modo que el estado tiene
  nombre y al operador se le dice *por qué* el lazo espera, en lugar de mirar
  un indicador que puede ser el residuo de lo último que se escribió ahí.

  `snap_c` — la tarea de pantalla toma una instantánea de tres estructuras
  compartidas bajo un tiempo de espera de 5 ms del mutex. Dos se limpiaban
  primero; la de control no. Agotar ese tiempo no es un error — ocurre siempre
  que la tarea de control está a mitad de su propia actualización — y cuando
  ocurría, todos los consumidores de abajo leían un marco de pila sin
  inicializar: el LED amarillo tomaba de ahí su estado de holdover, la barra de
  estado el número de algoritmo y el veredicto de enganche, y `trendstr`
  llegaba al informe serie y al LCD **sin terminador alguno**, de modo que la
  función que concatena cadenas copiaba lo que siguiera en la pila hasta topar
  con un byte cero. Ahora se conserva la última copia buena y se recurre a
  ella, que es la única respuesta a la vez segura y verdadera: poner todo a
  cero sería seguro y seguiría afirmando algoritmo 0, PWM 0, sin holdover y
  tendencia vacía, al azar, que es la clase de error que se acaba creyendo.

- **El algoritmo 13 no tenía veredicto de enganche, así que la pantalla juzgaba
  un filtro de Kalman por una media de frecuencia (build 51).**
  `tft_loop_locked()` enumera los algoritmos que publican un estado de enganche
  vivo y les pregunta; el 13 no estaba en la lista, así que caía en la rama
  escrita para los algoritmos 0–9, que prueba la media de 10 000 s (o de
  1000 s) — exactamente lo que esa función se extrajo para que la barra dejara
  de hacer. Añadirlo a la lista tampoco habría servido: **el lazo de Kalman
  nunca emite `LOCK`.** Todo su vocabulario es `KAL` / `REJ` / `NOPH` / `ARM` /
  `WAIT` / `NoCT` / `NoPL`, de modo que una prueba sobre la cadena de tendencia
  no podía coincidir.

  El veredicto viene ahora del filtro, calculado donde el filtro lo sabe, a
  partir de tres términos que ningún otro algoritmo aquí puede ofrecer:

  - **el detector habló en este segundo** (`s_kf_holdover == 0`, que también
    cumple una lectura rechazada — un `REJ` es la puerta de innovación haciendo
    su trabajo, no el lazo perdiendo su entrada). Sin este término, un filtro
    que vuela perfectamente sobre su propio modelo se lee como enganchado
    indefinidamente, que es la diferencia entre gobernar y planear;
  - **la fase estimada está dentro de la banda** — la estimación, no la lectura
    de este segundo, que es justamente para lo que sirve tener un filtro. Un
    lazo que se acerca desde 400 ns dice `KAL` cada segundo mientras lo hace;
  - **y el filtro lo sabe** — `sqrt(P[0][0])` dentro de la misma banda. Esto es
    lo que vuelve honesto al veredicto en los dos momentos que importan y no
    cuesta nada en ningún otro: en un arranque en frío `P[0][0]` parte de
    `(range/2)²`, y tras un armado del picDIV se reinicia a propósito al mismo
    valor, porque el cero al que se refería la estimación ya no existe.

  La banda es `LAT` (`g_ltic.acq_threshold_ns`), que mide `LC` y que este
  algoritmo ya usa para `s_kf_ctl_fast`, para su prueba de seguimiento y para
  la puerta de paciencia previa al armado. Nada se inventa y nada queda fijado
  en tiempo de compilación: una placa cuyo detector resuelva mejor recibe un
  `LAT` menor de `LC` y el veredicto se estrecha con él.

  **Reproducido, seis escenarios, la planta de la noche del 03.09 (28 680 s,
  `LAT` 200 ns, ruido de detector 2,45 ns).** El nuevo veredicto no declaró
  enganche ni una sola vez con la fase verdadera fuera de la banda, en ningún
  escenario. Lo que cambia:

  | escenario | la regla antigua dice enganche | el nuevo veredicto dice enganche | verde falso |
  |---|---|---|---|
  | estabilizado desde el inicio | t+1000 s | **t+10 s** | 0 s / 0 s |
  | arranque en frío, 800 ns fuera | t+1000 s | t+193 s (hasta ahí sigue acercándose) | 0 s / 0 s |
  | detector contra el tope | t+1000 s | t+201 s | 0 s / 0 s |
  | detector congelado en +1295 ns | t+1189 s | **nunca** | **14 459 s** / 0 s |

  La última fila es el fallo por el que valía la pena hacer esto, y no es
  hipotético — un detector congelado en +1295 ns es la avería para la que
  existe la lógica de armado, tomada de una captura real. La media de
  frecuencia no puede verlo, porque la *frecuencia* del oscilador está bien: lo
  que murió es la referencia de fase. Durante cuatro horas de una sola noche la
  barra habría mostrado `DISCIPLINED  FIX OK` en verde de enganche con el
  detector de fase clavado 1,3 µs fuera. El nuevo veredicto no se enciende ni
  una vez.

  `LOOPSIM_TRACE13` imprime ahora ambos veredictos en paralelo (`lock` y
  `ofrq`), para que el próximo cambio en cualquiera de ellos se pueda contar en
  vez de discutir.

- **El LED amarillo se apagaba en holdover manual al perderse el fix (build
  51).** La prueba de OFF decía `(!fix && !hold_auto)`, que atrapa la única
  combinación que nunca debe estar a oscuras: el operador congeló la salida con
  `MH`, el LED parpadeaba despacio para decirlo, y en cuanto cayó el fix se
  apagó — indistinguible de una placa que jamás vio un satélite, justo en el
  momento en que la salida congelada es lo único que sostiene el oscilador. El
  holdover manual manda aquí sobre el fix, como ya manda en la barra de estado.
  Cambia exactamente una de las ocho combinaciones de entrada; las otras siete
  se comprobaron y no se mueven.

- **`LPOL 0` se describía como «auto» en tres sitios, y es lo contrario (build
  51).** Los tres lazos LTIC tratan la polaridad 0 como *me niego a actuar y
  espero*, e imprimen una línea pidiendo que se fije. Llamar a eso auto le
  decía al operador que el firmware lo deduciría — lo único que no va a hacer.
  El «auto» que existe es `LC`, y hay que ejecutarlo. `LPOL`, `LL` y el texto
  de ayuda dicen ahora `not set - loop holds`.

- **Las correcciones nunca se contaron en los algoritmos 12 y 13, y `CS` daba
  un motivo falso (build 51).** `counting_now()` sabía preguntar a los
  algoritmos 10 y 11 si estaban enganchados; el 12 y el 13 caían por el
  `default:` junto con los 0–9, así que en los dos lazos más nuevos — los dos
  con más probabilidad de estar corriendo en una placa cuyo dueño se interesa
  por ese número — la estadística no contaba nada, y `CS` explicaba el hueco
  diciendo que la placa estaba «corriendo un algoritmo por debajo de 10», lo
  que para el 12 y el 13 no es cierto.

  Ambos algoritmos siempre supieron responder; a ninguno se le preguntaba.
  Ahora publican el veredicto donde lo deciden (`mlacc_locked()`,
  `kf_locked()`), y `CS` nombra los algoritmos que de verdad no tienen estado
  de enganche: 0–9. La bandera del algoritmo 12 sobrevive a propósito a un
  segundo `CORR` (una corrección hecha por un lazo estabilizado es justo lo que
  mide esa estadística) y cae a propósito durante el único segundo de un salto
  `ZC`, que cancela una deriva que el propio algoritmo aplicó y es una orden,
  no una corrección.

- **`LL` imprimía el estado del algoritmo 10 bajo todos los algoritmos (build
  51).** El `state=ACQ|DPLL|LOCK` de tres etapas se guarda, así que bajo los
  algoritmos 11, 12 o 13 la línea informaba de dónde se detuvo el lazo de tres
  etapas la última vez que corrió — posiblemente en una sesión anterior, ya que
  el valor se recupera de la flash — impreso sin matices entre los parámetros
  LTIC vivos. Ahora se imprime solo bajo el algoritmo 10, y `state=- (algo N
  running…)` en los demás casos. El operador ternario detrás era la segunda
  mitad del fallo: todo valor que no fuera `ACQ` ni `DPLL` se imprimía como
  `LOCK`, de modo que un byte jamás escrito reclamaba el más tranquilizador de
  los tres estados en lugar del menos.

- **El consejo impreso tras `CT` nombraba una tendencia que no puede aparecer
  (build 51).** «arm picDIV (AP) after the loop locks (trend `hit`)» — `hit` lo
  emiten solo los algoritmos 0 y 3–8, nunca los 10–13, y quien acaba de
  ejecutar `CT` está en uno de estos últimos. Quien lo siguió al pie de la
  letra esperó una palabra que no podía llegar. El consejo dice ahora *cuando
  el lazo informe de enganche* y deja la indicación a la pantalla y a `CS`, que
  saben por algoritmo qué significa.

- **La tendencia de holdover del algoritmo 13 pasa de `HOLD` a `NOPH` (build
  51).** Tres estados sin relación compartían una palabra en una misma
  pantalla: este (el detector no dijo nada *en este segundo*), el modo de
  holdover manual o automático impreso al lado como `[HOLDOVER]`, y el
  «holdover — MCU crystal» de `SW` para un reloj de sistema corriendo sin PPS.
  El algoritmo 12 ya lo llamaba `NOPH`; no había razón para que el 13 lo
  llamara de otro modo, y todas las razones para no hacerlo. La lista de
  palabras de tendencia del manual nunca contuvo `HOLD`, así que la
  documentación queda más exacta, no menos.

- **El informe con tabuladores imprimía 0.0 para medias que aún no existían
  (build 51).** `gpsdo_calc_averages()` calcula cada ventana solo cuando se ha
  llenado; antes de eso los campos conservan su 0.0 inicial. El informe legible
  siempre los filtró con esas mismas banderas — el de tabuladores no, así que
  los primeros 10, 100, 1000, 10 000 y 20 000 segundos de cada registro
  llevaban un `0.0` duro en la columna correspondiente. Al graficarlo eso no es
  un hueco: es un oscilador marcando cero hercios, y reescala el eje de modo
  que todo lo posterior queda como una línea plana.

  Esos campos se dejan ahora **vacíos** hasta que su ventana se llena. Los
  separadores se siguen escribiendo, así que el número de columnas no cambia
  (22 campos, verificado) y cualquier cosa que ya analice el fichero sigue
  funcionando; gnuplot, pandas y cualquier hoja de cálculo leen un campo vacío
  entre dos tabuladores como dato ausente, que es lo que es. Un valor centinela
  — `nan`, `-1`, `99999` — habría sido un número más que explicar a quien lo
  grafique después.
- **La barra de estado afirmaba un enganche que no tenía forma de conocer
  (build 50).** La regla era: hay fix de posición y no hay holdover, luego
  `DISCIPLINED  FIX OK`, en verde de enganche. Eso es una afirmación sobre el
  receptor GPS vestida con los colores de una afirmación sobre el lazo. Una
  placa calentando, una placa ejecutando `CT` con su salida barrida a
  propósito y una placa treinta segundos después de empezar la adquisición con
  microsegundos de error de fase mostraban la misma barra verde que una que
  llevaba una hora dentro de un nanosegundo: el elemento más grande y visible
  de la pantalla era lo único que no podía equivocarse, y se equivocaba.

  El veredicto que necesitaba ya existía. `tft_loop_locked()` — extraído de las
  cifras de frecuencia, donde sus umbrales se midieron sobre una tirada de tres
  horas en lugar de adivinarse — lo consultan ahora las dos, así que la barra y
  las cifras no pueden contradecirse: verde aquí significa verde allí, por
  construcción y no porque la misma regla esté escrita dos veces.

  Cuatro estados que la barra antes no sabía expresar: `ACQUIRING  FIX OK`
  (fix bueno, lazo aún no convergido), `OCXO WARMUP`, `CALIBRATING`, y el verde
  de siempre significando ahora lo que dice. El holdover sigue por encima de
  todo, porque una salida congelada es el hecho más importante.

  **Ambas direcciones están amortiguadas, de forma asimétrica.** El veredicto
  del enganche es cosa de un segundo y puede parpadear por una sola cuenta del
  contador — el mismo motivo por el que el umbral de las propias cifras se
  amplió a 0,15 Hz. La respuesta instantánea se probó primero y aquí es un
  error por la misma razón: una cuenta de jitter no es una avería, y una barra
  que alterna entre verde y naranja es peor que cualquiera de los dos colores.
  Cinco segundos consecutivos de enganche antes de ponerse verde, dos de
  pérdida antes de soltarlo. `tools/gpsdo_statusbar.py` reproduce la máquina
  sobre un escenario que va del arranque en frío a la antena desconectada; en
  él la regla antigua afirmaba `DISCIPLINED` durante 18 de 33 segundos sin que
  fuera cierto.
- **Un campo nuevo en los ajustes ya no cuesta a todos sus ajustes
  (build 49).** `settings_recall()` exigía que el registro guardado tuviera
  exactamente el tamaño actual, así que añadir un byte obligaba a subir
  `SETTINGS_VER`, y subirla significa rechazar el registro entero — PID, LC,
  zona horaria, todo, a cambio de un byte. El archivo lo sorteó dos veces
  tallando campos en bytes de relleno, y dos más con un `else if` escrito a mano
  por versión, cada uno atado a su propio `offsetof`.

  Ahora acepta cualquier registro desde `SETTINGS_V6_BYTES` (376, el tamaño al
  que se congeló la disposición) hasta el `sizeof` actual. La estructura se pone
  a cero antes de leer, así que todo campo añadido desde entonces se lee como 0
  — que cada uno de ellos ya define como «sin fijar». `VS` es el primer campo
  que lo usa, y detrás quedan tres bytes de alineación para los dos siguientes.
  La ruta de guardado parcial recibe la misma regla, porque sembrar desde un
  registro corto es ahora seguro por el mismo motivo.

  Verificado con el compilador de destino y no a ojo: `dac_path` sigue en 318,
  `bl_pct` 319, `a12_gain` 320, `dac_vref_cv` 372, `adc_vdiv_h` 374,
  `vsense_src` en 376, `sizeof` 380. Un registro escrito por el build 48 carga,
  y su byte ausente se lee como el raíl de 5 V — que es lo que el build 48
  hacía.
- **`DV` y `AV` no se guardaban solos, aunque todo lo escrito sobre ellos decía
  que sí (build 48).** El changelog de v1.07, los tres manuales y las notas
  enviadas a quienes montan placas con AD5680 lo prometían; el código llamaba a
  `cli_manual_save("ES ALGO")`. Un `DV 5.00` tecleado y no seguido de un `ES`
  volvía a 3,30 tras un reset, y con él volvía el aviso MISMATCH — lo que se
  lee como un fallo de hardware y no como un ajuste perdido. Estos dos
  describen la etapa de salida de la placa y se teclean una sola vez, cuando se
  construye, así que la promesa era la correcta y el código la ha alcanzado.

- **El informe `DAC` exageraba en un tercio la ruta de PWM simple (build 48).**
  `gpsdo_dac_output_bits()` devolvía 16 para esa ruta, pero en una compilación
  sin motor de dither el temporizador resuelve **50 000** valores de ciclo de
  trabajo, no 65 536 — la portadora es de 2 kHz a partir de un reloj de
  100 MHz, y 50 000 no es potencia de dos. La función es ahora
  `gpsdo_dac_output_steps()` y devuelve un recuento; el informe imprime
  `N-bit` donde eso es exactamente cierto y `N steps` donde no lo es. La misma
  clase de defecto que los 0,094 µHz que el informe llegó a ofrecer en una
  pieza de 18 bits, y la misma corrección: decir lo que hace el hardware.
- **La segunda pasada de CT podía llegar al código completo (build 47,
  corrigiendo la segunda pasada añadida antes en esta misma versión).** La
  parte baja de ese barrido siempre guardó un margen de 1000 LSB; la alta no
  guardaba ninguno — el tope superior impedía que el centro pasara *más allá*
  de `65535 - half`, no que lo alcanzara, así que un cero deducido alto ponía
  el punto de medida superior justo en el extremo de escala. La placa AD5680
  de Dan Wiering lo hizo el 10-09-2026: cero en 45 577, medio reparto 20 654,
  centro arrastrado a `65535 - half`, tercer punto en 65 535. Allí midió
  limpiamente — sus tres puntos eran lineales hasta el dígito — pero el búfer
  de salida de un conversor es menos lineal justo contra su propio extremo, y
  la calibración que fija la ganancia de la planta para todos los algoritmos
  es el último sitio donde gastar el último LSB de rango.

  `CT_RAIL_GUARD` (1000 LSB) se aplica ahora en **ambos** extremos; en esa
  placa el barrido pasa a ser 23 227 / 43 881 / 64 535. No cuesta nada en
  excursión — el reparto sigue siendo `CT_TARGET_SWING / K` y solo se mueve
  el centrado — y el tope de 60 000 en el reparto es lo que garantiza que los
  dos límites nunca choquen (solo se encontrarían pasado
  `65535 - 2·CT_RAIL_GUARD` = 63 535). La línea de la segunda pasada dice
  ahora cuál de los dos casos ocurrió, en vez de afirmar «centrado en el
  código de 10 MHz deducido» precisamente cuando el tope acababa de moverlo.
  Verificado sobre 271 076 combinaciones de K y cero deducido: acercamiento
  mínimo al extremo de 0 LSB antes, 1000 después, reparto sin cambios en
  todos los casos.

- **El informe `DAC` citaba un paso que el conversor no puede dar (build 46).**
  Imprimía una cifra de 16 bits y otra de 24 en todas las rutas, y detrás de ese
  puente hay tres conversores de tres anchuras distintas. En la placa AD5680 de
  Dan Wiering ambas líneas estaban mal a la vez y en sentidos opuestos: ofrecía
  **0,094 µHz** como paso, cuando 64 cuentas del valor de control hacen un
  movimiento en un pin de 18 bits y el menor real es **5,99 µHz** — mientras la
  otra línea citaba una cifra de 16 bits, cuatro veces más gruesa de lo que la
  pieza puede.

  El valor de control es de 24 bits en todas las rutas; lo que la SALIDA mueve
  en una escritura es la anchura del driver vivo, y `gpsdo_dac_output_bits()`
  dice ahora cuál: **24** en DITH (la tabla de dither promedia el valor de 24
  bits exactamente, por construcción), **18** en el AD5680, **16** en el PWM
  simple, que es la misma propiedad que ya reportaba
  `gpsdo_dac_fine_available()`. El informe imprime ambos números y los nombra:

  ```
    step: control 24-bit 1 LSB = 0.094 uHz = 9.36e-15
          output 18-bit 1 LSB = 5.987 uHz = 5.99e-13
          (EXT resolves 18 bits; finer requests reach the pin as a
           time average, not as one step)
  ```

  La columna de frecuencia fraccional tuvo que aprender un exponente para ello.
  Un `e-15` fijo servía mientras el informe citaba dos anchuras codificadas; a
  través de 16, 18 y 24 bits la misma línea lleva desde 2,4e-12 hasta 9,4e-15, y
  «2394.9e-15» no es un número que nadie lea. `cli_frac_exp()` normaliza la
  mantisa — este fichero no imprime ningún float por `printf`, porque Float
  printf hay que habilitarlo en el IDE y sin él emite «?».

- **Una pendiente de calibración que la rampa no puede tener, y los ocho
  armados que costó (build 45).** `LC` produce dos números ns/V y uno acota al
  otro, cosa que nada comprobaba. `range_ns/span` es el dφ/dV **medio** sobre la
  banda barrida; el ajuste del ancla mide el dφ/dV **local** en 0,632·Vsat. En la
  rampa que este detector realmente es — `V = Vsat(1 − e^(−φ/τ))` —
  `dφ/dV = (τ/Vsat)·e^(φ/τ)` crece con φ, así que una media tomada sobre un
  tránsito que llega por encima del ancla se evalúa por encima de ella y es por
  tanto **mayor**. Para la geometría de esta placa (Vsat 3,29 V, tránsito
  0,80…3,22 V) el valor en el ancla debería ser en torno al **0,55×** de la
  media. Una pendiente local por encima de la media no es una pendiente que la
  rampa pueda tener.

  Dos calibraciones del mismo detector, con tres días de diferencia:

  | | LNV | LZO | LRN | LNV ÷ media del tránsito |
  |---|---|---|---|---|
  | builds 16–41 | 1252,0 | 2,0809 | 3000,00 | **1,01** |
  | build 42 | 1837,7 | 2,0797 | 2958,75 | **1,50** |

  LZO coincide en 1,2 mV y LRN en un 1,4 %, así que la rampa no se movió — sólo
  la pendiente, y es el único número ajustado a partir del puñado de puntos
  dentro de `±LTIC_ANCHOR_WIN_V`. Se midió con la fase inestable.

  **Lo que se rompió no fue la pendiente.** Fue la guardia de saturación de
  `ltic_phase_error_ns()`, que dimensiona la banda útil como
  `range_ns / ns_per_volt`, mezclando un numerador de tránsito completo con un
  denominador local. La banda se encogió de **±1,318 V a ±0,886 V** en torno al
  cero. El picDIV aterriza la fase de esta placa **1,06…1,28 V por debajo del
  cero**, un desfase físico fijo que no se movió — pero ahora quedaba fuera de
  la banda: cada aterrizaje se leía como raíl. El puente de captura de fase del
  algoritmo 11 rearmaba entonces el divisor cada 20 s — 15 s de espera más 5 s
  de encallamiento — ocho veces, en t = 122, 142, 162, 178, 198, 218, 238, 258 s,
  hasta que un aterrizaje cayó a sólo 0,67 V y fue aceptado. El lazo pasó a PLL
  de inmediato y treinta segundos después estaba enganchado. **Ocho
  interrupciones de la salida de 1 PPS por un artefacto de calibración.**

  LC compara ahora ambas antes de guardar ninguna: una pendiente de ancla por
  encima de la media del tránsito se reporta y se sustituye por la media. El
  ancla se conserva — el ajuste de Vsat recorre todo el tránsito y es robusto,
  que es justo lo que muestra la coincidencia de 1,2 mV en LZO. En la
  calibración buena la guardia mueve LNV un 1 % (1252,0 → 1239); en la mala, un
  33 %, que es la falta entera.

  No arreglado aquí: la guardia de `ltic_phase_error_ns()` sigue dimensionando
  su banda a partir de `range_ns / ns_per_volt`, dos magnitudes medidas con
  definiciones distintas. La banda pertenece al ancla — `Vsat = LZO / 0,63212`
  dio 3,290 V y 3,292 V en las dos calibraciones, es decir el mismo número antes
  y después — y llevarla ahí exige antes enseñar al simulador que la rampa es
  exponencial y no lineal, para poder elegir cualquier constante.

- **El registro imprimía una fase que el panel se negaba a mostrar (build 44).**
  Ambas rutas de visualización derivan dph del mismo voltaje enclavado, pero cada
  una lo hacía con su propia copia de la aritmética, y ya se habían separado una
  vez por el diente de sierra. Esta vez fue por la **banda**.

  `ns_per_volt` es una pendiente *local*: LC la mide en una ventana estrecha
  alrededor del ancla que sitúa en 0,632·Vsat, porque la rampa es
  `V = Vsat(1 − e^(−t/τ))` y una exponencial no tiene una sola pendiente. Fuera
  de la ventana 15–85 % la curva ya se ha aplanado y una lectura lineal es
  errónea. El panel lleva tiempo negándose a imprimir ahí — muestra `ovf` — y el
  informe serie seguía imprimiendo un número.

  **Medido en la captura del 04.09 11:41**, tomada para una pregunta muy otra.
  Al pasar del algoritmo 13 al 7, la fase se aparcó en lo que el registro llamó
  **+1085 ns** con Vphase en **2,946 V**, por encima de los 2,798 V del techo de
  la banda de este detector. El panel llevaba casi una hora diciendo `ovf`
  mientras el registro decía +1085 ns — y no era meramente «cerca de un raíl».
  Primeras diferencias de la misma placa en las dos posiciones:

  | dónde estaba la lectura | Vphase | suelo blanco | p99 de \|Δ\| |
  |---|---|---|---|
  | mitad de banda (algoritmo 13) | 2,08 V | **2,6 ns** | 5,2 ns |
  | aparcada cerca del techo (algoritmo 7) | 2,94 V | **7,7 ns** | 20,6 ns |

  **Tres veces el ruido**, sólo por la posición en la rampa — y el sesgo va en la
  dirección que halaga, porque la compresión significa que la fase verdadera era
  *mayor* que el número impreso. Nada en el registro lo decía.

  Ambas rutas llaman ahora a una sola función, `ltic_display_phase()`, que lleva
  juntos el cero, la pendiente medida, el diente de sierra enclavado y la banda.
  Fuera de banda la línea serie imprime `dph:ovf`, la misma palabra que usa el
  panel. Todo script que lee estos registros busca `dph:` seguido de dígitos, así
  que fuera de banda ya no encuentran lectura — que es la verdad — en lugar de un
  número plausible y equivocado en una dirección conocida. El `Vphase:` en bruto
  queda justo a su izquierda y dice por qué extremo se salió.

  El mismo fallo consta dos veces desde el otro extremo: un «+1561 ns» inmóvil y
  un «+1295 ns» inmóvil, ambos tomados por buenos, ambos costando una medida
  antes de que nadie lo notara. **Una lectura equivocada es recuperable; una
  lectura equivocada que parece tranquila no lo es.**

  No arreglado aquí, y merece su propia revisión: la guardia del propio lazo
  (`railed_now`) prueba unos 3,28 V codificados, de modo que en un detector que
  satura cerca de 2,9 V no dispara nunca y el filtro sigue actuando sobre
  lecturas que las pantallas ya declaran fuera de banda.

- Manual: la fila de `GPSDO_DAC_EXT` en la tabla de opciones de compilación
  seguía diciendo que el define era mutuamente excluyente con
  `GPSDO_PWM_DITHER` — redacción anticuada de antes de la selección de ruta en
  tiempo de ejecución. Se compilan juntos; el comando `DAC` elige la ruta
  activa y el puente conduce la señal.

### Rechazado
- **Retener la salida del lazo mientras una posición de span no tiene
  calibración propia.** Lo hacía la primera versión del módulo de span, con la
  teoría de que la ganancia de la otra posición — 5× desviada — era el mal
  mayor. Medido en lugar de supuesto (`loopsim` con el nuevo `LOOPSIM_KALL`,
  tres plantas, cinco semillas cada una, algoritmos 10–13, con LTC 100 y 60):
  con la quinta parte de la ganancia correcta cada lazo fue 2,2–6,4× peor en sd
  de fase; con 4–5,7× la ganancia correcta el algoritmo 11 salió 3,6–6×
  *mejor*, el 13 entre 0,6× y 2,2× con picos de 100–250 ns, el 12 de tres a
  siete veces peor, y el 10 bien a 4× y con excursiones de 500–870 ns en dos
  pasadas de quince a 5,7×. Ninguno divergió. Y `spansim` mostró lo que cuesta
  la retención cuando `CT` no puede acertar: movida a REDUCED al arrancar, con
  `CT` fallando durante tres horas, la placa retenida se quedó en 3e-8 más de
  cinco horas y recorrió 570 µs de fase, mientras la misma placa dejada con los
  coeficientes de FULL se enganchó en 58 minutos. En el caso normal ambas
  variantes son idénticas — `CT` arranca enseguida y el lazo no trabaja
  mientras barre —, así que la retención solo importaba justo donde hacía daño.
  Los algoritmos 3–7 no los puede mover loopsim y no se midieron; la cabecera
  del módulo lo dice.

- **Subir la portadora del PWM simple de 16 bits a los 12,2 kHz del dither.**
  Permitiría subir con ella la frecuencia de corte del filtro — seis veces, con
  dos polos — y en una placa de rango reducido eso merece la pena. Solo que
  donde importa ya ocurre: con `GPSDO_PWM_DITHER` compilado, `dac_emit()` lleva
  la ruta simple por `pwm24_write(code24 & 0x00FFFF00)`, de modo que ambas
  rutas ya comparten una sola portadora de TIM4 a 12,2 kHz y conmutar entre
  ellas cambia únicamente la tabla. El `analogWrite()` a 2 kHz solo sobrevivía
  en la compilación sin dither — y esa es precisamente la configuración en la
  que el cambio sale mal, porque allí la anchura del propio PWM **es** toda la
  salida: 15,6 → 13 bits lleva el paso de 5,0e-11 a 3,0e-10 en una placa de
  3,3 V. Dirección equivocada. Anotado en el propio código, en
  `gpsdo_dac.cpp`, para que no se vuelva a proponer; `tools/carrier.py` tiene
  las cifras de ambas placas y de las tres portadoras, incluidas las líneas de
  6-18 Hz de la propia tabla de dither, que pasan a ser el caso limitante por
  encima de un corte de ~16 Hz y no se mueven cuando se mueve la portadora.

- **Armar el picDIV bajo los algoritmos 3–9 para mostrar dph ahí.** Casi todo
  funciona ya — `ltic_read_fast()` corre en cada pulso sea cual sea el
  algoritmo, y ambas rutas de visualización dependen de LC y no del lazo — así
  que la pregunta era sólo si armar el divisor y rearmarlo según la fase se
  escapa. La captura del 04.09 responde, y la respuesta es no.

  Enganchado en el algoritmo 13 y conmutado luego al 7: la fase salió de ±100 ns
  en diez minutos, llegó a **2,2 ns/s (2,2e-9)** mientras el LRN aún recogía y el
  PWM osciló 353 LSB, y después **se aparcó en +1085 ns y ahí se quedó**. En los
  últimos 27 minutos su deriva fue `+3,1e-13 ± 1,5e-12`: el algoritmo 7 mantiene
  la frecuencia magníficamente y no tiene mecanismo alguno para el desfase que un
  transitorio deja atrás.

  La cadencia no la fija, pues, la deriva: desde un aterrizaje fresco a 3e-13 la
  rampa duraría semanas. La fijan los **sucesos** — cada conmutación, reaprendizaje
  o perturbación puede gastar un tercio de la banda en minutos — y cada rearmado
  detiene la salida picPPS durante `PICDIV_ARM_MS` y la devuelve desplazada por el
  desfase de aterrizaje, que en esta placa es de −900…−1650 ns.

  Ése es el argumento que lo zanja: **la fase en la que se aparca un lazo
  puramente de frecuencia es un recuerdo del último sobresalto, no una propiedad
  del oscilador, y un monitor que se rearmara para mantenerla en pantalla la
  convertiría en un recuerdo del último armado.** Estaría cambiando la misma
  salida cuya fase dice informar. Bajo 10/11/13 eso se paga porque el lazo posee
  la fase; bajo 3–9 no la posee nadie.

  El valor diagnóstico es real y se obtiene sin nada de esto: engancha en 13,
  conmuta, registra y lee la pendiente. Costó 52 minutos y midió lo que ningún
  contador de esta placa puede — el error en régimen del algoritmo 7 está tres
  décadas por debajo del propio cuanto de la media de 1 ks, y al final de esa
  captura la media de 10 ks aún marcaba −0,0041 Hz porque arrastraba un
  transitorio de cuarenta minutos antes.

### Documentación
- Manual: nuevo bloque «Tres rutas, un nodo» en la sección Salida — cómo
  coexisten `PWM`/`DITH`/`EXT`, el comando único de 24 bits con la vista de
  16 bits, y la relación de los 18 bits del AD5680 con la trama SPI de 24 bits
  (`code18 = code24 × 262143 / 16777215`, escalado para que los coeficientes
  pasen entre rutas). A raíz de preguntas de compilación de Dan Wiering.
- Manual: `SPAN` en la referencia de comandos, `GPSDO_SPAN_SENSE` en la tabla
  de interruptores y un bloque «Dos spans, dos calibraciones» en la sección
  Salida. `tools/loopsim`: `LOOPSIM_KALL`, todo el conjunto derivado por `CT`
  a partir de una K errónea.
- Pestaña **Help** del tuner: `SPAN` y `SPAN CLR` (build 56), y en
  README_TUNER `SPAN` entre los comandos que describen la placa. El nombre nuevo
  en el ejemplo del rótulo del manual, en el esquema de la cabecera del TFT y en
  el ejemplo de la línea de estado del tuner.
- Manual y pestaña **Help** del tuner: `DV` conserva tres decimales (build
  57). La escritura `v1.07.57rt` en el ejemplo del rótulo del manual, en el
  esquema de la cabecera del TFT, en el ejemplo de la línea de estado del
  tuner y en los títulos de los documentos.

## [v1.06-rtos] — publicado 2026-09-05 (build 42)

Publicado como build 42. Las entradas llegaron aquí cuando estaban
medidas, no cuando estaban escritas.

### Añadido

- **Manuales: Apéndice D — el filtro de Kalman en palabras llanas; KC
  documentado (los tres idiomas).** Un apéndice nuevo sin fórmulas explica
  el algoritmo 13 por intuición: las tres creencias y sus lápices de
  incertidumbre, los dos testigos (detector y TIM2), KT-frente-a-KC como
  entendimiento-frente-a-manos, la maquinaria de escepticismo (puerta 4σ,
  prueba de confianza, silencio tras el arm, silencio del TIM2 tras
  reinicio), cómo se ve una buena noche y cuándo no tocar las perillas. La
  sección 4.6a describe ahora la ley de control separada (`fase/KC`,
  vigente tras el primer bloqueo), la R medida (~2,9 ns) con la estructura
  aparte de deriva del cero de ~2,6 ns/45 s, el ratio de adaptación y las
  marcas de agua de Q en KL, y los cuatro segundos deliberados de HOLD
  tras un arm.
- **`KC` — el horizonte del controlador, separado del del estimador.** Un solo
  número hacía dos trabajos en el algoritmo 13: el techo de Q `R/T³` fija a qué
  velocidad puede correr el **estimador**, y `x0/T` en la ley de control fija a
  qué velocidad el **controlador** anula un error de fase que ya conoce. La nota
  junto al comando `KT` decía con todas las letras que son cosas distintas, y a
  continuación afirmaba que separarlas «necesita Sg medida en el oscilador, lo
  que necesita una referencia que esta placa no tiene». Era falso. Necesita una
  segunda variable.

  **Lo que dijo la medida antes de que hubiera idea.** En la captura del 03.09
  14:17, estabilizada y en modo tiempo, la estimación del filtro correlacionaba
  con el detector con **r = 0,822 a un retardo de −1 s** — la cadencia de medida,
  es decir, sin retardo alguno — y al restar la estimación quedaban **2,52 ns**,
  que es el suelo blanco del detector con un 1 % de margen. Es decir,
  `dph = ph + ruido blanco`: el filtro **ve** todo el error de fase, 3,27 ns. Y
  ese error es lento — la media de `dph` en 100 s todavía tiene sd de **3,06 ns**,
  el 69 % de la amplitud sobrevive a un horizonte completo de promediado. Nada se
  estimaba mal. El controlador simplemente decidía no corregir lo que el
  estimador ya había encontrado.

  **Por qué la separación es gratis.** `x0` es una estimación, no una medida. Su
  propio error es `sqrt(P00)` ≈ 1,05 ns frente a una señal de 3,3 ns en esta
  placa, así que anularlo rápido **no amplifica el ruido blanco** — el filtro ya
  lo quitó. Solo `Q/R` decide cuánta de la mentira lenta del detector se cree, y
  `KC` no toca `Q/R`. Hay un segundo efecto en la misma dirección: la ley de
  control **apunta su propia corrección en el estado de frecuencia**, así que un
  horizonte que tolera un error de fase permanente durante 100 s sesga `x1`
  durante 100 s. Anular más rápido elimina ese sesgo, y por eso el seguimiento de
  *frecuencia* mejora tanto como la fase.

  **Medido**, planta nocturna reconstruida de la captura de 8 h del 03.09, ocho
  semillas, `KT = 100` en todo, `KC = 100` (el comportamiento anterior) frente a
  `KC = auto = KT/3 = 33 s`:

  | condición | phase sd | sd error de control | r | movimiento DAC | Q |
  |---|---|---|---|---|---|
  | detector limpio | 24,72 → **4,46** | 2,20 → **0,78** | 0,637 → 0,913 | 0,619 → 0,675 | sin cambio |
  | deriva 2,8 ns / 60 s *(esta placa)* | 15,06 → **4,01** | 1,56 → **0,59** | 0,759 → 0,945 | 0,680 → 0,862 | 9,4e-6 → 8,5e-6 |
  | deriva 8 ns / 60 s | 18,98 → **8,14** | 1,89 → **1,14** | 0,688 → 0,833 | 0,817 → 1,193 | sin cambio |
  | deriva 12 ns / 300 s | 23,35 → **12,24** | 2,15 → **1,47** | 0,655 → 0,764 | 0,756 → 1,042 | sin cambio |
  | arranque en frío desde el raíl | estabiliza 266 → **108 s** | mismos arms | | | |

  Mejor en ambas plantas, con cualquier nivel de deriva del detector y en la
  adquisición — incluido el caso de 12 ns, donde acortar `KT` medía *peor*, porque
  esa vía acelera también el estimador y es el estimador el que copia la mentira.
  Frente a acortar `KT` a 40 s, que alcanza la misma phase sd con el mismo
  movimiento del DAC, `KC = 33` deja **Q en 8,5e-06 en lugar de 1,64e-05** — la
  mitad de ruido de proceso, es decir, la mitad de disposición a seguir al
  detector — y deja intactos el promediado largo y el estado de envejecimiento
  que pagan el holdover.

  El valor por defecto es `KT/3` y no una constante, para que escale con el
  horizonte y no sea un número ajustado a un solo oscilador. La rodilla medida
  está en 20–30 s en una placa cuya deriva del detector tiene `tau ≈ 60 s`;
  `KT/3 = 33 s` cae ahí. `KC` acepta 10–10000 s, o 0 para automático, y advierte
  en ambos sentidos: por encima de ~60 s dice que el lazo tolera un error de fase
  permanente y lo apunta en el estado de frecuencia; por debajo de ~15 s, que la
  fase deja de mejorar mientras el DAC se mueve más. `KL` imprime el valor en
  vigor.

  El registro en flash crece de 12 a 14 bytes y su versión de 1 a 2. **Los
  registros de la versión 1 siguen cargándose** — rechazarlos habría sido dos
  líneas más corto y habría reiniciado en silencio el `KR`/`KQ`/`KT` del operador
  en la única actualización que no tenía por qué tocarlos.

### Corregido

- **La supresión era un ciclo demasiado corta, porque la cuenta empieza en la
  petición y la petición no es el pin (build 42).** Tres es el número de lecturas
  de raíl que una captura *muestra*. El lazo ve una más. `ltic_arm_picdiv()` sólo
  activa un bit de evento; la tarea de control lo recoge en su siguiente despertar
  y baja el pin, lo mantiene durante `PICDIV_ARM_MS` = 1001 ms — deliberadamente
  algo más de un segundo, para que la liberación caiga después del flanco en vez
  de competir con él — y el divisor se sincroniza entonces en el 1PPS siguiente.
  Sólo la rampa posterior a eso es una fase.

  El build 41 dejó pasar la cuarta lectura en sus dos armados:

  | armado en el ciclo | raíl en | el lazo consumió |
  |---|---|---|
  | A = 101 | A+2, A+3, A+4 | **1377,0 ns** |
  | A = 661 | A+3, A+4 | **1361,5 ns** |

  Fíjese dónde *empieza* el raíl: A+2 en uno, A+3 en el otro, porque el jitter
  está en el despertar de la tarea. Y dónde *termina*: **A+4 en ambos**, porque el
  final lo fijan la retención de 1001 ms y la resincronización, que son
  deterministas. Cuatro no es, por tanto, tres más un margen de seguridad: es el
  ciclo en el que el raíl realmente termina, dos veces.

  **Medido.** El simulador tampoco podía verlo, y por una razón que merece
  anotarse: su modelo de armado decrementaba el contador y aterrizaba la fase en
  la misma iteración, de modo que `ARMSETTLE=n` producía `n−1` ciclos de raíl y un
  modelo que pedía cuatro daba tres — exactamente lo que la supresión del build 41
  ya cubría, así que la fuga se reproducía como nada. Corregido ese desfase de uno
  y puesto el transitorio en los cuatro ciclos que el lazo del hardware ve de
  verdad, veinticuatro semillas sobre la planta nocturna:

  | | asentado mediana | asentado peor | armados peor | rechazos mediana |
  |---|---|---|---|---|
  | build 41 | 240 s | **2154 s** | 6 | 12 |
  | build 42 | **220 s** | **347 s** | 3 | **2** |

  La única lectura filtrada vale **+454 LSB** de corrección ordenada en el
  simulador — unos 430 en el hardware — y el lazo rechaza después las lecturas
  reales todo el tiempo que tarde la puerta en reabrirse alrededor de una
  estimación de fase a 2570 ns de la verdad.

  **Y el coste de pasarse por uno es ninguno.** Ejecutado contra un transitorio de
  tres ciclos, donde el build 42 suprime una lectura que no tenía por qué
  suprimir: asentado mediana 220 s en ambos casos, peor caso 300 s frente a
  297 s. Tres segundos en la peor de doce semillas. Quedarse corto por uno cuesta
  una orden de 454 LSB y, en una semilla de veinticuatro, la adquisición entera.

- **Una lectura tomada con el divisor parado no es una fase (build 41).**
  Armar el picDIV detiene su salida y espera al siguiente flanco de 1PPS. La
  rampa LTIC se sigue muestreando todo ese tiempo y, sin nada que la detenga,
  lee cerca de su parte alta. Esa lectura está en banda, cuantizada y lleva un
  nanosegundo de jitter: no hay en ella nada que la puerta de innovación o el
  filtro puedan objetar. Simplemente no es una fase. Seis armados en la captura
  nocturna del 03/04.09, los tres segundos posteriores a cada uno y el cuarto:

  | armado en | +1 | +2 | +3 | +4 |
  |---|---|---|---|---|
  | t+102 | 1425,4 | 1378,0 | 1378,0 | **−1453,4** |
  | t+445 | 1437,5 | 1381,0 | 1381,0 | **−947,1** |
  | t+510 | 1437,5 | 1381,0 | 1381,0 | **−1106,4** |
  | t+575 | 1437,5 | 1381,0 | 1381,0 | **−1740,9** |
  | t+1597 | 1444,7 | 1394,9 | 1397,0 | **−1326,9** |
  | t+2202 | −1357,5 | 1378,1 | 1380,3 | **−1320,8** |

  Los mismos tres números cada vez, porque es el raíl y no una medida — y
  después el aterrizaje, donde el modelo del armado dice que debe estar.

  **Lo que costó.** En el primer armado de esa captura el filtro acababa de
  reiniciarse, así que la puerta estaba abierta con `P00 = (range/2)²` y tomó
  las tres lecturas del raíl como fase. Ordenó **+1653 LSB en 105 s**. TIM2
  decía que la placa estaba dentro de **0,01 Hz** cuando el lazo empezó; estaba
  a **0,50 Hz** cuando el lazo terminó con ella — cincuenta veces la banda de la
  propia puerta de armado, puesta ahí por el lazo mismo. La fase cruzó entonces
  el detector a unos 100 ns/s, la rampa se fue al raíl y la prueba de confianza
  la condenó, con razón: no estaba siguiendo. Los tres armados siguientes no
  pudieron ayudar, porque un lazo condenado no dirige y por tanto no puede
  deshacer el error de frecuencia que sigue mandando el detector al raíl. Salió
  únicamente por la expiración de treinta minutos: **2373 s de holdover en una
  placa que estaba enganchada cuando se encendió.**

  La reparación es la que GLM-5.3 Max escribió un build antes para TIM2,
  aplicada a la otra medida por la misma razón: **ninguna lectura de fase
  durante tres segundos después de un armado.** `raw` falso es la descripción
  honesta — el detector no dijo nada, lo cual es cierto, y todo consumidor aguas
  abajo ya sabe qué hacer con eso. La captura del aterrizaje coge entonces la
  cuarta lectura, que es la que la puerta de armado quería desde el principio y
  no recibió ni una sola vez.

  **Medido.** El simulador no podía ver nada de esto, porque su armado
  aterrizaba al instante — el cuarto modelo halagador encontrado en ese fichero,
  tras el TIM2 perfecto, el voltaje de raíl equivocado y el aterrizaje en cero.
  Con el transitorio modelado a partir de la tabla anterior, veinticuatro
  semillas sobre la planta nocturna y la fase arrancando donde arrancó la
  captura:

  | | asentado mediana | asentado peor | armados peor | sd de fase peor | rechazos mediana |
  |---|---|---|---|---|---|
  | build 40 | 514 s | **nunca (28680 s)** | 51 | **9495 ns** | 89 |
  | build 41 | **218 s** | **297 s** | 2 | **0,55 ns** | 2 |

  Diez de las veinticuatro semillas no llegaron a adquirir antes del cambio;
  ninguna después. Fuera de la adquisición el cambio no es sólo pequeño sino
  **idéntico bit a bit** — las mismas cifras en todos sus dígitos en cinco
  semillas sin armado y en la planta del 26.08 — y el caso del detector
  congelado sigue terminando en condena (50 armados en ambos casos).

- **Un cuanto no es todo el ruido de la referencia (build 40).** El umbral
  cuántico añadido un build antes es correcto en su clase y corto en su
  magnitud. Medido sobre el mismo registro para el que se escribió: las
  ventanas inmediatamente anteriores al veredicto falso llevaban `|aexp|`
  hasta **83 ns** — dos cuantos y medio, porque el promedio que pasa la puerta
  bajó a −0,04 Hz. Con un umbral de 32 ns esa ventana sigue condenando: `amov`
  20,0 ns frente a `0,25·aexp` = 20,7. **El veredicto que ese umbral existe
  para impedir es precisamente el que deja pasar.**

  El umbral sale ahora de la propia dispersión medida de la referencia y no
  del paso con que se muestra. `Rf` es la varianza de `z_f` y el filtro ya la
  calcula para la actualización de TIM2; las treinta y dos lecturas de una
  ventana vienen de un boxcar de cien segundos y comparten casi todo su
  contenido, de modo que el ruido acumulado es `W·σ` y no `sqrt(W)·σ`. En esta
  placa son 32 × 2,9 = **93 ns**, que superan el evento de 83 ns en un doce
  por ciento y siguen a la antena en lugar de ser una constante.

  Es una prueba de una sigma, y a propósito: dos sigmas serían 186 ns y
  cegarían la comprobación del detector congelado, que es la razón de existir
  de toda esta prueba. El coste queda declarado — un detector congelado
  necesita ahora un desfase real de unos 0,03 Hz antes de que se le pueda
  condenar — y por debajo de eso no hay movimiento de fase que perder. El
  simulador calla sobre este cambio (estado estacionario 3,94 ns y dPWM 1,305
  en ambos casos), porque su TIM2 está limpio y `aexp` nunca se acerca a
  ninguno de los umbrales; es un fallo exclusivamente de hardware y la medida
  de arriba es su prueba.

- **La prueba de confianza condenaba detectores sanos por la cuantización
  del propio TIM2 (build 39).** El movimiento esperado de la ventana viene
  de `z_f = -100 x avg100`, cuyo promedio de 100 s avanza en cuantos de
  0,01 Hz: un cuanto de sesgo son 32 ns de "movimiento esperado" en una
  ventana de 32 s, por encima del umbral antiguo `4*sqrt(2R) ~ 16,5 ns`.
  Un oscilador aparcado junto a un borde de cuantización con GPS tranquilo
  abría la prueba con un fantasma, el lazo bloqueado no se movía y tres
  ventanas condenaban el detector: 30 minutos de HOLD con lecturas sanas
  dentro de banda (03.09 20:11; ya constaba el 02.09 10:59). El umbral
  cubre ahora un paso de la resolución de la propia referencia:
  `max(4*sqrt(2R), trust_ns, 100 * 0,01 Hz * KF_TRUST_W)`. El coste queda
  declarado: un detector congelado necesita un desfase real superior a un
  cuanto antes de que la prueba pueda verlo.
- **KC ya no actúa durante la adquisición (build 39).** Con el horizonte del
  controlador separado (KC = KT/3), un aterrizaje en frío a media banda
  ordenaba 1278/33 = 39 ns/s de anulado y el arranque recorría el limitador
  con seis rebotes contra los raíles y 116 rechazos. KC espera ahora un
  enganche unidireccional - la fase dentro de la banda de adquisición en
  una lectura que el filtro usa - y luego lo conserva; un reinicio `LA n`
  vuelve a ganárselo. El guardia del EMA de R y la paciencia del detector
  congelado siguen el horizonte vigente, así que ambos vuelven al
  comportamiento previo a la separación (KT) durante la adquisición.
- **La primera actualización de TIM2 tras un reset ya no se cree el
  transitorio de arranque (build 39).** `kf_reset()` siembra P11 muy abierto,
  así que la primera actualización daba a un promedio de 100 s caduco (aún
  con la corrección del oscilador hacia su frecuencia) una ganancia de ~0,9
  y escribía una frecuencia que la placa ya no tenía: f = -29513 ps/s un
  segundo después del arm del arranque del 03.09, deshecho por el limitador
  durante nueve minutos. Las actualizaciones de TIM2 se silencian durante
  el primer boxcar (100 s) tras un reset; la puerta de arm y la prueba de
  confianza leen `z_f` directamente y no se ven afectadas. Los arm solo
  ensanchan P00 y no disparan el silencio.
- **La media de innovación arrastró el enganche durante horas, y la adaptación de
  Q actuaba sobre ella.** Un armado deja la fase a mil nanosegundos, así que las
  innovaciones durante la adquisición son de ese orden y sus *cuadrados* un
  millón de veces el valor en régimen. `s_kf_ms_innov` es una EMA con constante
  0,001, así que necesita unas tres horas para olvidarlo. Reconstruida de la
  captura del 03.09 18:35 alcanzó **3,0e+05** y, 5100 s después en una ejecución
  tranquila desde t+446, seguía marcando **1,6e+03** frente a un valor real de
  **9,1**. El propio `KL` del firmware informó `ratio 31.55` en una ejecución
  cuyos últimos mil segundos miden **1,7**.

  No es un fallo de visualización. La adaptación *actúa* sobre esa razón, así que
  **Q era empujada a su techo durante horas tras cada arranque por innovaciones
  que pertenecían al enganche** — por eso casi todas las capturas de la historia
  de este proyecto mostraron `[at ceiling]`, y la única que no lo hizo fue la de
  ocho horas. Es la misma forma que el fallo de las marcas de agua dos builds
  antes, y se ocultó más tiempo porque el número que corrompe resulta verosímil.

  La EMA arranca ahora con `tracking`, en el mismo pestillo que las marcas de
  agua, y se **siembra con `S`** en vez de ponerse a cero: `S` es lo que un filtro
  consistente espera de `y²`, así que la adaptación abre en razón 1 y se mueve
  solo con evidencia recogida durante el seguimiento. Medido en todo el banco —
  régimen permanente, detector limpio, deriva de 8 ns, arranque en frío desde el
  raíl, arranque a −1300 ns — sin cambios más allá del ruido de semilla, porque
  el modelo de armado del simulador es más suave que el del hardware y su
  transitorio de adquisición nunca fue el problema.

- **El guarda que congela el estimador de R dividía por `KT` mientras el control
  pedía `x0/KC`.** El comentario del guarda lo ata a «exactamente lo que pedirá
  `u` más abajo»; desde la build 36 eso es `x0/KC`, así que dejado en `KT`
  subestimaba la velocidad de anulación ordenada en `KT/KC` = 3 con la división
  por defecto — un movimiento que puntuaba como 0,4 ns/s era en realidad 1,2 y
  debía congelar las EMA. El régimen permanente no se ve afectado porque `x0` es
  pequeño, pero cada transitorio de anulación alimentaba R y `ms_diff1` al triple
  del ritmo previsto, y `KC` hace esos transitorios tres veces más empinados.
  Encontrado por GLM-5.3 Max leyendo la build 36 contra ese comentario.

  Enviado sobre el argumento: el banco apenas lo ve (solo se mueve un arranque
  con error de frecuencia, rechazos 6 → 4), porque estas plantas contienen
  arranques en frío y no las recuperaciones de episodio que produce el GPS real.
  `tools/episode_r.py` mide la diferencia en hardware — cuánto sube R sobre su
  media previa al episodio — y la referencia de la build 35 es **mediana
  +0,074 ns, percentil 90 +0,143, peor +0,306** sobre dieciséis episodios.

  `patience` pasó con él a `KC`, porque su comentario nombra el control
  explícitamente. El `horiz` de la puerta de armado se queda en `KT` a propósito:
  pregunta cuánto arrastra la deriva a un aterrizaje antes de que el lazo tenga
  autoridad, que no es una pregunta de velocidad de anulación, y `KC` allí
  admitiría *más* armados.

- **La medida de fase y el voltímetro de servicio compartían un único ADC sin
  ningún enclavamiento, y el banco lo cazó primero por el lado inofensivo.**
  `PA1` (la rampa LTIC) es `ADC1_IN1` y `PIN_VCTL_ADC` (`PB1`) es `ADC1_IN9` —
  hay un solo ADC en esta pieza — y el `analogRead()` del core reconfigura el
  canal sobre un manejador compartido y no es reentrante. `ltic_read_fast()`
  corre desde la tarea despertada por el PPS; `ControlTask` lee Vctl/Vcc/Vdd cada
  200 ms. No había nada entre ambas.

  El síntoma visible fue cosmético y exacto. En la captura del 03.09 10:08 el
  Vctl mostrado cayó de 1,800 V a **1,620 V** siete veces, unos dos segundos cada
  vez, con el PWM sin cambios. Vctl es una **media móvil de diez muestras**, así
  que una conversión que devuelve cero la baja exactamente una décima:
  1,800 × 0,9 = 1,620, coincidiendo en cuatro cifras, siete veces. Ese valor
  alimenta solo las pantallas, de modo que nada se gobernó con él — pero es una
  medida directa de una conversión destruida por la otra tarea, y la misma
  colisión por el otro lado destruye una **fase**.

  Había una segunda razón, independiente, para que la lectura fuera atómica: los
  **50 µs son un plazo, no un retardo.** La rampa decae con una constante de fuga
  de ~5 ms, así que un cambio de tarea que retrase la lectura 1 ms la sitúa un
  20 % más abajo — un 20 % de error de fase sin ningún signo externo.

  Ambos quedan cerrados suspendiendo el planificador alrededor de cada acceso al
  ADC: todo el bloque de asentamiento más dieciséis conversiones de
  `ltic_read_fast()` (~350 µs) y las tres conversiones de `ControlTask` (~60 µs),
  más los dos refrescos de calibración y la lectura desechable del arranque.
  **Las interrupciones siguen habilitadas** — la captura del PPS, los
  temporizadores y SysTick quedan intactos — así que nada de la ruta temporal
  cambia; solo se excluyen otras *tareas*.

  Se consideró un mutex y se descartó. El único timeout correcto del lado de la
  fase es cero, porque no puede esperar; y una toma con timeout cero que falla
  deja a elegir entre competir igualmente o descartar una medida de fase, y
  ninguna de las dos es una mejora. Suspender el planificador hace que ceda el
  lado barato ante el plazo, que es el sentido correcto.

  La misma captura traía también el aspecto que tiene la colisión por el lado de
  la fase: un segundo perdido en el registro (03:18:24 → 03:18:26), CPU al 38 %
  en la muestra siguiente y una única lectura del detector de **+1369,1 ns** —
  una rampa completa, es decir, una lectura servida contra el flanco de
  referencia equivocado. La puerta del LTIC debería haberla retenido (el salto es
  de 1364 cuentas frente a un umbral de 743) y no lo hizo, porque su rama de
  segunda oportunidad acepta una lectura que coincide con la previamente
  rechazada — y una tarea hambrienta más allá de un límite de PPS produce
  exactamente ese par coincidente. La puerta de innovación del algoritmo 13 la
  atrapó igualmente: `rej` pasó de 135 a 136 y la estimación de fase no se movió
  de −3,7 ns. La defensa en profundidad funcionó; la capa que debía detenerlo, no.

  No hace falta telemetría nueva para confirmar la reparación. Si funcionó, los
  hundimientos de Vctl ×0,9 dejan de aparecer.

- **El `KL` de una noche informó `Q since start: 5.022e-06 .. 4.982e-05` y
  ninguno de los dos números significaba lo que las condiciones de aprobación
  necesitaban.** Las marcas de agua añadidas una build antes — para que un solo
  volcado al final de una ejecución desatendida respondiera «¿superó Q su techo?»
  y «¿descendió en los tramos tranquilos?» — abarcaban toda la ejecución, y
  durante el enganche `tracking` es falso, el techo no está en vigor y se aplica
  en su lugar el raíl ancho `q_seed·1e3`. La marca alta era por tanto seis veces
  el techo de seguimiento, del todo legal, e ilegible tanto como excursión como
  como ausencia de ella. Una repetición offline de las horas estabilizadas situó
  el rango real en `6,4e-06 .. 1,2e-05`. Las marcas empiezan ahora en el primer
  segundo de seguimiento y no se reinician después: una pérdida momentánea de
  seguimiento es parte de la noche, no una noche nueva. `KL` dice en consecuencia
  `Q while tracking`.

  Vale la pena nombrar lo ocurrido y no solo corregirlo: el diagnóstico añadido
  para que un raíl no quedara sin informar resultó él mismo ilegible en su primera
  noche, del mismo modo y por la misma razón. Dos builds antes, la línea
  `[at ceiling]` ocultaba el suelo; aquí las marcas de agua ocultaron el régimen.

- **La adaptación de Q restaba la R equivocada, y las dos EMA de ruido tenían
  nombres tan parecidos que un análisis escrito de este filtro las leyó al
  revés.** `R` es deliberadamente la media cuadrática del **retardo 16** — lleva
  tanto el ruido blanco del detector como su deriva lenta, de modo que la puerta
  de innovación y la ganancia de Kalman traten al detector según lo que vale en el
  horizonte sobre el que gobierna el lazo. Esa derivación está documentada en su
  sitio y se midió: bajar allí al suelo blanco es lo que descartó el 11% de las
  lecturas en el banco del 29.08 y, en la corrida del 27/28.08, metió la deriva
  del detector dentro del oscilador.

  Es correcta para la puerta y correcta para la ganancia. Era incorrecta para la
  tercera tarea que a R se le había dado sin decirlo. Una innovación con horizonte
  de predicción de **un segundo** solo puede llevar el suelo blanco más lo que el
  filtro no haya seguido — unos 6,4 ns² en esta placa — así que nunca alcanza los
  8,45 ns² que informa el estimador de retardo 16. `Pobs = ms_innov - R` es por
  tanto negativa con GPS tranquilo por pura aritmética, y la adaptación estaba
  privada de información por construcción, no por accidente. La retención añadida
  en la build 30 lo hizo sobrevivible; no lo hizo informativo.

  Referida en cambio al suelo del retardo 1, la aritmética cierra:
  `E[y²] = P00 + σ_white²`, así que `Pobs` estima la `P00` verdadera y `Ppred` es
  la del propio filtro — una prueba de consistencia de covarianza que Q sí puede
  mover, porque `P00` es exactamente la palanca de Q. La puerta y las ganancias
  conservan la `R` del retardo 16 y todas las protecciones allí documentadas.

  Con ello se fue también la ley bang-bang. `×1,02` por encima de razón 1,2 y
  `×0,98` por debajo de 0,8 no tiene punto fijo, solo dos bordes de banda muerta
  entre los que oscilar, y es violentamente asimétrica en el tiempo: con razón
  1,25 compone **×2,7 por minuto**, mientras que volver abajo exige razón inferior
  a 0,8, lo que no puede ocurrir hasta que P ya haya crecido. Ahora es una
  aproximación estocástica multiplicativa, `Q *= 1 + κ(razón − 1)` con `κ = 0,001`
  igualado a la EMA de innovación y el paso recortado a ±0,02 para que el peor
  caso no sea más rápido que antes. Punto fijo exactamente en razón 1, simétrico,
  y esa misma razón 1,25 hace que Q crezca un factor e en **una hora en lugar de
  un minuto** — un episodio de GPS no puede trinquetearla. La adaptación además se
  aparta diez minutos tras cada armado del picDIV y mientras la puerta rechaza por
  encima del 5%, con el mismo criterio que el guarda `moving` que ya tenía el
  estimador de R.

  Como la EMA del retardo 1 fija ahora la referencia de la adaptación y no solo
  alimenta `Sf`, su entrada se **winsoriza**: se recorta el cuadrado de la
  diferencia en lugar de descartar la muestra, de modo que una cola gaussiana
  apenas se toca mientras un salto de GPS de 20 ns queda acotado. El recorte es
  **9×** la media cuadrática corriente, no el 3× que parece natural — la EMA
  guarda `0,5·d1²`, cuya media es `σ²`, mientras que `d1` tiene desviación
  `√2·σ`, así que recortar en `m·σ²` recorta `|d1|` en `√m` desviaciones. Con
  `m = 3` eso es 1,73 σ: actúa sobre el **8,4%** de las muestras y sesga el suelo
  **un 14% a la baja** (medido sobre 400 000 sorteos gaussianos) — justo sobre la
  magnitud que este cambio existe para medir bien. Con `m = 9` es el 3 σ
  pretendido: 0,27% de las muestras, 0,5% de sesgo.

  **Medido**, antes contra después, doce semillas de ruido por condición. Con una
  deriva del detector ajustada a la de esta placa (5 ns / 300 s), phase sd
  **27,4 -> 19,3 ns** de media y **44,6 -> 25,3** en el peor caso, sd del error de
  control 2,13 -> 1,58 LSB, movimiento del DAC sin cambios. Con 8 ns de deriva,
  **21,2 -> 17,5** de media y **35,4 -> 21,3** el peor. Con un error de frecuencia
  de 200 LSB: media 29,8 -> 28,0, peor **51,3 -> 34,9**. La adquisición — ocho
  arranques en frío desde el raíl en dos horizontes y un arranque a -1300 ns — no
  cambia. Dos condiciones salen peor: un detector perfectamente limpio (media
  23,1 -> 24,6, aunque el peor caso mejora 33,7 -> 31,3) y una deriva de 12 ns, vez
  y media lo que esta placa muestra (17,0 -> 18,6). Es la forma esperada del
  compromiso: el cambio deja subir a Q para cubrir la deriva que la `R` del
  retardo 16 mantenía fuera de la ganancia, lo cual es correcto hasta el punto en
  que la deriva es tan grande que pide un estado propio.

  Y las EMA se renombran. `s_kf_ms_diff` es ahora `s_kf_ms_diff16`, con el retardo
  en el nombre y un comentario en la declaración, porque leer el par al revés es
  lo que convirtió una decisión de diseño documentada en un fantasmal «error del
  30% en R» y costó un día.

  El diagnóstico y ambas reparaciones son de **GLM-5.3 Max**, a partir de una
  lectura independiente de la captura de cuatro horas del 02.09; las dos
  constantes de arriba son las correcciones de este proyecto a ellas.

- **El ruido de proceso del algoritmo 13 no se adaptó ni una sola vez — siempre
  estuvo apoyado contra un raíl, y uno de los dos raíles era invisible en
  `KL`.** Tres capturas de una misma placa, 02.09, en tres horizontes:

  | KT | Q en uso | qué raíl |
  |---|---|---|
  | 100 s | 7,777e-06 | `R/T^3` — el techo |
  | 40 s | 9,936e-08 | `q_seed/1000` — el suelo |
  | 20 s | 7,949e-07 | `q_seed/1000` — el suelo |

  Cada valor coincide con su raíl en cuatro cifras significativas. `KL` solo
  nombraba el techo, así que dos de las tres ejecuciones parecían una adaptación
  sana.

  Dos fallos que se ocultaban mutuamente. Primero, el suelo era `q_seed/1000` y
  `q_seed` es `r_seed/T^3` — **la misma T que el techo**. Ambos raíles se movían
  juntos, así que cambiar KT deslizaba una ventana fija de mil veces arriba y
  abajo en lugar de dar más margen a la adaptación. El barrido de KT que esas
  capturas debían medir estaba midiendo dónde caía un raíl: KT 40 salió *peor*
  que KT 100 (constante de tiempo ajustada 91 s frente a 40–65 s) porque Q cayó
  78x cuando el suelo se movió bajo ella y la propia `(R/Q)^(1/3)` del estimador
  se fue a 462 s. Un horizonte más corto produjo un lazo más lento.

  Segundo, la adaptación comparaba la media cuadrática de la innovación con `S`,
  y `S = HPH' + R`. `S` nunca puede bajar de `R`, así que cuando las innovaciones
  salen menores que R sola la razón queda atascada por debajo de 0,8 haga lo que
  haga Q, y el decaimiento de 0,98 por segundo corre hasta chocar con algo. **No
  hay punto fijo inferior.** Se le pedía a la adaptación reparar un error de R
  encogiendo Q, cosa que Q no puede hacer — y en esta placa, desde que se
  corrigió el emparejamiento del diente de sierra, las innovaciones *son* menores
  que R: la R medida es 2,9–3,0 ns mientras un ajuste de la función de estructura
  de esas mismas capturas sitúa la parte blanca en 2,35–2,65. La razón se queda
  cerca de 0,77. Con KT 100 midió 0,83 y Q se congeló en el techo donde la había
  dejado una subida anterior; con KT 40 midió 0,77 y Q bajó hasta el suelo. Un
  cambio del cuatro por ciento en R volcaba el lazo entre dos fallos opuestos.

  El suelo es ahora un raíl **numérico** sin T — el valor que da la misma fórmula
  con el horizonte más largo que el firmware acepta y un detector en su límite de
  cuantización; unos 1e-12 en esta placa, seis décadas por debajo de la semilla —
  de modo que mantiene la recursión lejos de cero sin participar en la respuesta.
  El techo conserva su T, porque «no corras más rápido que el horizonte que te
  dieron» es justo lo que significa KT. Y la adaptación resta ahora R a ambos
  lados y compara `HPH'` predicho contra `HPH'` implícito: cuando el implícito es
  negativo, las innovaciones no dicen nada sobre Q, así que Q se **mantiene**,
  `KL` lo indica y señala a R. `KL` nombra además el suelo y el estado retenido.

  **Medido**, antes contra después, mismo árbol, una condición cada vez. Régimen
  permanente con una deriva del detector ajustada a la medida: con KT 100 phase
  sd **42,1 -> 31,6 ns**, sd del error de control **3,17 -> 2,56 LSB**,
  correlación con el control verdadero requerido **0,046 -> 0,319**, movimiento
  del DAC sin cambios; con KT 40 Q abandona el suelo (**1,2e-07 -> 2,9e-05**) y
  phase sd pasa de 6,00 a 5,06. Con detector limpio y KT 100 sobre doce semillas,
  media **24,26 -> 23,12** y peor caso **53,42 -> 33,73**. Con 12 ns de deriva
  lenta del detector, **20,8/29,2 -> 16,5/20,4**. Todos los casos de adquisición
  — ocho arranques en frío desde el raíl en cada uno de los tres horizontes, un
  arranque a -1300 ns, un detector congelado dentro y fuera de la banda — salen
  **idénticos bit a bit**, que es lo que debe ocurrir: la adaptación se aparta
  durante el enganche y este cambio queda por completo en la parte que sigue.

  La reparación que proponía el comentario anterior de este archivo — sembrar Q a
  partir de una medida en TIM2 de la deriva de frecuencia del oscilador — se
  comprobó y no es posible: un contador entero de un segundo cuantiza a 29 ns/s y
  una media de cien segundos a 2,9, frente a una deriva del orden de 1e-2 ns/s.
  Es el punto 4 de `doc/AUDIT_algo13_model_gaps.md`, ya cerrado allí como no
  medible. La semilla nunca fue el problema; el problema era el suelo.

- **El lazo podía resincronizar el divisor mientras el detector informaba una
  fase perfectamente buena, perdiendo con ello casi toda la banda de
  adquisición.** La prueba de arm preguntaba por `!have`, y `have` es
  `raw && trust` — dos fallos distintos reducidos a una sola pregunta. Un
  detector EN EL RAÍL (`raw` falso) no dice nada y resincronizar el picDIV es la
  única reparación que existe. Uno DESCONFIADO (`raw` cierto, `trust` falso)
  sigue informando una fase, y si esa fase está dentro de la banda, armar no
  repara nada: descarta una lectura utilizable y aterriza entre -900 y -1650 ns.

  En la captura del 02.09 10:59 ocurrió exactamente eso. En t+593 s el detector
  leía **-53 ns** con `Vphase 2.041 V` — sano por cualquier medida — pero la
  prueba de confianza lo había descartado ocho segundos antes y el lazo estaba en
  holdover. La rama contó sus cinco segundos y armó. La fase se fue a
  **-1222 ns** y la vuelta costó otros setecientos segundos: tres arms y
  **1317 s** hasta estabilizar, frente a un arm y **309 s** la noche anterior en
  la misma placa.

  La rama ahora arma solo cuando no hay nada que perder: ninguna lectura válida,
  o una válida ya fuera de la banda, que es el caso congelado donde
  resincronizar *sí* es la reparación. **Medido** contra el mismo árbol con esa
  única condición retirada: ocho arranques en frío desde el raíl, régimen
  permanente, una deriva del detector de 12 ns/300 s, un desplazamiento inicial
  de -1300 ns y un error de frecuencia de 200 LSB salen idénticos bit a bit, y un
  detector congelado fuera de la banda sigue armando catorce veces. Solo cambia
  el detector congelado *dentro* de la banda — catorce arms pasan a cero — que es
  el caso para el que la corrección existe.

- **La medida de TIM2 estaba retrasada y sobrevalorada; corregir ambas cosas
  redujo a la mitad el tiempo de adquisición.** Dos fallos en el mismo sitio, y
  `loopsim.cpp` venía describiendo el segundo — sobre su propio modelo de planta —
  desde el 26.08 sin que nadie se lo contara al filtro.

  `Rf` se estimaba de las diferencias entre lecturas contiguas de `avg100`. Son
  medias móviles que comparten 99 de sus 100 muestras, así que su diferencia es
  una centésima del ruido de una muestra y el estimador salía dos órdenes de
  magnitud demasiado pequeño: el suelo `Rf >= 1` hacía todo el trabajo, y 1
  (ns/s)² es de por sí unas ocho veces demasiado optimista. Ahora se construye a
  partir de la dispersión medida del contador de UN SEGUNDO dividida por el número
  de muestras que contiene la media en uso: sin constantes, y un PPS más ruidoso o
  una antena peor aparecen ahí directamente.

  Y una media móvil de cien segundos no es una medida de la frecuencia AHORA:
  queda unos cincuenta segundos por detrás, que es justo cuando la frecuencia era
  distinta si el lazo estaba corrigiendo. El lazo contabiliza cada corrección que
  hace, así que conoce el cambio ordenado en esa ventana: la medida se predice
  ahora como `x1 - du/2` en vez de `x1`. Una corrección, no una inflación, porque
  el número se conoce y no solo se acota.

  **Medido** sobre dieciséis arranques en frío con el detector contra el raíl
  (cuatro desplazamientos de frecuencia, cuatro semillas): tiempo medio hasta
  asentarse **1802 -> 923 s**, peor caso **4615 -> 2727 s**, con muchos menos
  armados del picDIV. El seguimiento con detector congelado mejora seis veces
  (track sd 978 -> 169 LSB). El régimen permanente queda igual. Es el punto 3 de
  `doc/AUDIT_algo13_model_gaps.md`, que iba tercero de cinco y resultó ser la
  mayor mejora individual de todos.

- **El algoritmo 13 podía no engancharse en absoluto, y el simulador no podía
  verlo porque su modelo del picDIV era complaciente.** La captura del 01.09
  21:00 nunca enganchó: once minutos, cuatro armados, 460 s de holdover, 94 s de
  rechazos en la compuerta, once segundos de operación normal. `Sf` no tuvo nada
  que ver: `R` nunca salió de su semilla, así que `Sf` fue idénticamente cero
  toda la ejecución.

  **Dónde aterriza realmente un armado.** Siete armados en las tres capturas del
  01.09, fase leída en el segundo siguiente a cada uno: `-1554 -1431 -899`
  (21:00), `-1641 -1441 -943` (17:12), `-927` (10:49, la única ejecución que
  luego funcionó). Todos negativos, ninguno cerca de cero, de -900 a -1650 ns
  frente a una banda de ±1500. El simulador modelaba el aterrizaje como
  `gauss(300)` — unos cientos de nanosegundos a cada lado de cero — así que todo
  armado simulado se recuperaba y toda prueba de adquisición pasaba. Es el tercer
  modelo complaciente hallado en `loopsim.cpp`, tras el TIM2 perfecto y la
  tensión de raíl equivocada. Ahora aterriza donde aterriza el hardware, y puede
  volver a irse al raíl después, algo que el banco tampoco modelaba nunca.
  `LOOPSIM_ARMOFS` / `LOOPSIM_ARMSD` sobrescriben ambos números.

  **Por qué la compuerta antigua no podía funcionar.** Solo pedía que el error de
  frecuencia no llevara la fase a través de media banda durante los 60 s de
  espera — `arm_hz = range/(2·100·60)` = 0,25 Hz — lo que gasta todo el
  presupuesto en deriva y no deja nada para el desplazamiento del aterrizaje, y
  ese desplazamiento resulta ser la mayor parte de la banda. Los armados de las
  21:00 pasaron esa compuerta a +0,24 y +0,17 Hz: 24 y 17 ns de fase por segundo,
  bastante para sacar de la rampa un aterrizaje de -1450 en segundos. La
  compuerta pregunta ahora lo que importa — *dado dónde aterriza este divisor,
  ¿seguirá siendo legible la fase dentro de un horizonte?* — con el aterrizaje
  medido del último armado y la deriva de TIM2, de modo que ninguno de los dos
  términos es una constante que haya que adivinar. También deja de rechazar un
  error de frecuencia grande que casualmente empuja la fase de vuelta al centro.
  Un escape tras diez horizontes a ciegas evita que una compuerta capaz de
  rechazar para siempre lo haga.

  **Y el filtro trataba su propio actuador como exacto.** `s_kf_x1 +=
  polarity*applied/lsb_per_ns` entrega la corrección al filtro como un hecho, sin
  covarianza asociada, y `lsb_per_ns` viene de `CT`, que es una medida como
  cualquier otra. Un error de unos pocos por ciento se acumula ahí en `x1` y nada
  lo devuelve nunca — y ese sesgo tiene un hogar estable, porque
  `u = -(x1 + x0/T)` no manda exactamente nada siempre que `x1 = -x0/T`. El lazo
  aparca en un desplazamiento de fase constante, las innovaciones se van a cero y
  ninguna medida contradice nada. Visto en el simulador aparcado en -100 ns
  durante 1500 s con `x1` = +1,0 ns/s — un error de 0,01 Hz, exactamente la
  resolución de TIM2, así que la segunda medida tampoco lo ve — y en hardware
  como la ejecución del 01.09 que tardó 3600 s en bajar de 21 a 7,5 ns mientras
  informaba "ya anulando" (TODO 80), y como todo desplazamiento permanente que
  este lazo ha mostrado. Un cinco por ciento de la corrección aplicada, al
  cuadrado, va ahora a `P11`: el filtro conserva bastante duda sobre su propia
  frecuencia como para que la medida de fase pueda tirar de ella.

  **Medido**, dieciséis arranques en frío con el detector contra el raíl (cuatro
  desplazamientos de frecuencia, cuatro semillas), sobre el modelo honesto de
  armado: tiempo medio hasta asentarse **2316 -> 1802 s**, peor caso **7127 ->
  4615 s**, y muchos menos armados en todos los casos que cambiaron. El lazo ya
  enganchado queda igual hasta dos decimales en todas las cifras de régimen
  permanente, en dos plantas, cinco semillas y tres niveles de deriva del
  detector: ambas reparaciones son inertes una vez que el lazo entra.

  `loopsim` informa ahora del **tiempo hasta asentarse**, que es la métrica que
  los cambios de adquisición mueven realmente; la sd de fase de una ejecución
  entera puntúa igual a un lazo que arma pronto y mal que a uno que espera y arma
  bien.


- **El algoritmo 13 corría con medio modelo de reloj, y esa mitad era el
  trinquete.** El modelo de reloj de dos estados que usa todo texto de metrología
  de tiempo lleva *dos* densidades de ruido de proceso: `Sf`, el ruido blanco de
  frecuencia (`h0`), que aparece como un paseo aleatorio en fase, y `Sg`, el
  paseo aleatorio de frecuencia (`h_-2`):

  ```
         | Sf*t + Sg*t^3/3   Sg*t^2/2 |
    Q =  |                            |
         |    Sg*t^2/2        Sg*t    |
  ```

  Este filtro inyectaba `Q/3`, `Q/2`, `Q`, que con `t` = 1 s son exactamente los
  tres términos de `Sg`, con **`Sf` idénticamente cero** — no un ajuste puesto a
  cero: no tenía nombre y nada lo medía. La consecuencia es toda la historia de
  la última semana. Sin `Sf`, la única manera de explicar *"la fase se movió este
  segundo más de lo que predije"* es subir `Sg`, es decir, concluir que la
  FRECUENCIA DEL OSCILADOR está derivando rápido. Ruido de fase de corto plazo,
  un cero de detector que deriva, una lectura de contador retrasada: todo se
  contabilizaba como paseo aleatorio de frecuencia, lo que sube `K1`, la ganancia
  que escribe el estado de frecuencia, que es lo que el DAC sigue. El trinquete
  de `Q` no era un fallo de la adaptación. Era la adaptación haciendo lo único
  que el modelo le dejaba.

  **Medir `Sf` requiere dos retardos, y el segundo es casi gratis.** Con un
  retardo de `k` segundos las diferencias de fase llevan
  `0,5*E[dp^2] = sigma_R^2 + Sf*k/2`: ruido blanco, que no crece con `k`, más un
  paseo, que sí. El historial del estimador de `R` a retardo 16 ya estaba ahí; un
  estimador a retardo 1 junto a él separa ambos. Es la misma división
  `floor` / `slow` que `tools/logab.py` lleva imprimiendo desde el principio y
  que al filtro nunca se le dio.

  **`Sf` se mide, no se adapta**, así que a diferencia de `Sg` no puede
  atrincherarse: ese era el objetivo.

  La primera versión recortaba la diferencia en cero y estaba mal, de un modo que
  merece registrarse: ambos estimadores son EMAs con `alpha` = 0,002 de una
  gaussiana al cuadrado, así que cada uno lleva un 4,5% de error estándar y su
  diferencia un 6,3%, y recortar en cero una cantidad con signo y ruido la
  rectifica en un sesgo positivo de unas 0,4 sigma. Con un detector simulado
  perfectamente blanco, donde la respuesta honesta es cero, `Sf` salió 1,5e-2
  ns²/s — casi exactamente el sesgo predicho — y costó un 60% en sd de fase y un
  factor dos en ADEV a tau 1024, porque una `Sf` ficticia explica las
  innovaciones, la adaptación entonces mata de hambre a `Sg`, y `Sg` es lo que
  permite al filtro seguir un oscilador que deriva. Ahora la diferencia debe
  superar dos sigmas de su propio ruido (0,126 de la estimación) antes de contar
  como medida, y ese umbral se deriva de la constante de la EMA, no se elige.

  **Medido**, dos plantas, cinco semillas. Con detector limpio, donde `Sf` lee
  correctamente cero, todo queda igual; los casos de detector contra el raíl,
  congelado, arranque en frío y barrido de `KT` quedan idénticos hasta el último
  dígito. Con 12 ns de deriva del cero del detector — la condición en que
  realmente están ambas placas, `R` 6,3 ns de los cuales unos 5,9 ns son deriva —
  el ADEV a tau corto mejora **1,7x** (3,11e-11 -> 1,81e-11 en tau 16) y el lazo
  sigue la trayectoria real del oscilador bastante mejor (track sd 1,26 -> 1,14
  LSB, correlación aplicado-requerido 0,913 -> 0,928; en la segunda planta 1,29
  -> 1,15 y 0,461 -> 0,529). Los costes: un 33% más de movimiento del DAC — aún
  un orden de magnitud por debajo del punto de partida, y es movimiento útil,
  ya que el ADEV a tau corto mejoró con él — y alrededor de un 8% en ADEV a tau
  1024.

  `KL` imprime `Sf` junto a `R` y `Q`. Cero ahí significa que los dos retardos
  coinciden, que con un detector limpio es la respuesta correcta.

  Este es el punto 1 de `doc/AUDIT_algo13_model_gaps.md`. Los puntos 2 a 4 — el
  cero del detector como estado, la medida de TIM2 retrasada y sobrevalorada, y
  un `KT` que los datos referidos a rubidio dicen que es de cinco a diez veces
  demasiado corto — siguen abiertos, y `KT` todavía no puede subirse mientras la
  semilla de `Sg` escale como `1/KT^3`.


- **El techo de `Q` se aparta durante la adquisición, porque si no era más
  estrecho justo cuando el filtro necesitaba margen.** `R` arranca *en* su
  semilla (`kf_reset` siembra el estimador de diferencias con `r_seed`) y
  `q_seed` es `r_seed/KT³`, así que en el primer segundo `R/KT³` **es** `q_seed`:
  el techo nuevo caía sobre la semilla y no dejaba a la adaptación ningún margen
  hacia arriba hasta que `R` estuviera medida. Y la adquisición es justo donde
  una `Q` amplia se gana el sueldo: una fase a mil nanosegundos necesita un
  filtro que pueda moverse.

  Lo sacó a la luz la captura del 01.09 17:12: un armado del picDIV dejó la fase
  en -2124 ns, fuera de la banda del detector, y la ejecución necesitó cinco
  armados, 897 segundos contra el raíl y 3300 s para asentarse, descartando por
  el camino hasta el **70% de las lecturas** en la compuerta de innovación,
  frente a un armado y 900 s en la ejecución anterior. El simulador no reproduce
  ese arranque (allí el armado cae limpio), así que cuánto fue esto y cuánto la
  tirada de dados del propio armado queda sin resolver; un techo que se derrumba
  sobre la semilla en el arranque está mal de todos modos.

  La cota del horizonte se aplica ahora solo mientras el lazo sigue: la fase
  dentro de la banda de adquisición **y** una `R` con al menos una constante de
  tiempo de EMA de medida real detrás. Hasta que se cumplan ambas, la adaptación
  corre contra el raíl de seguridad amplio, como antes. No se pierde nada: el
  trinquete que esta cota existe para frenar es un fallo de régimen permanente y
  necesita horas de seguimiento tranquilo para desarrollarse. El régimen
  permanente queda igual hasta el último dígito en dos plantas, cinco semillas y
  tres niveles de deriva del detector; el caso del detector congelado mejora
  mucho (sd de fase 347k -> 55k ns, frecuencia final -0,50 -> +0,31 Hz, rechazos
  del 11,2% al 0,8%).

  Apartarse no es soltar, y la primera versión de esto se equivocó: con el techo
  estrecho suspendido y ninguno incondicional detrás, un trinquete de 1,02 por
  segundo alcanzó `Q` = 5,4e+18 en menos de dos horas en la planta del detector
  congelado — el lazo nunca lee "siguiendo", así que la única cota que queda debe
  ser incondicional. El raíl amplio se aplica ahora en ambas ramas.


- **La lectura de CPU en la cabecera del TFT vuelve al eje central de la barra.**
  La arreglo del `%` recortado la había desplazado: el campo se centraba en el
  hueco entre el nombre del programa y el *relleno* que reserva el reloj LMT, y
  `HDR_LMT_PAD` es mucho más ancho que los glifos del reloj, así que toda la
  cadena quedaba unos 23 px a la izquierda del centro en el panel de 480. El
  relleno nunca fue el obstáculo: ambas rutas de dibujo pintan la CPU en último
  lugar, así que nada puede borrarla después, y lo único que no debe tocar son
  los glifos reales del reloj. Medido contra ellos, el espacio libre es simétrico
  (el nombre del programa y el reloj tienen dieciséis caracteres cada uno), de
  modo que el eje central cabe con unos 30 px de margen a cada lado y ahí vuelve.
  La banda de borrado se dimensiona ahora según la lectura más ancha posible
  (`CPU 100%`) y no según todo el hueco — dimensionarla al hueco fue lo que sacó
  el texto del centro. El centro se usa solo cuando las anchuras medidas dicen
  que cabe; si no, queda el centro del hueco como respaldo, y si ni eso alcanza
  no se dibuja nada, así que ninguna combinación de fuentes y paneles puede
  devolver el recorte.


- **`KL` ahora lo dice cuando `KQ` está fijada.** La línea de cabecera ya las
  distinguía omitiendo `(adapt)` tras el valor, y no bastó. La captura del 01.09
  mostró el lazo moviendo el DAC seis veces menos que la semana anterior, lo que
  se leía como el nuevo techo de la adaptación de `Q` haciendo su trabajo — y no
  lo era: `KQ` había quedado fijada en la semilla en un experimento anterior y
  se recuperaba del anillo de flash en cada arranque, así que la adaptación no
  había corrido en absoluto. Un ajuste que sobrevive a los reinicios y cambia el
  significado de todas las demás cifras del informe necesita una línea propia, y
  ya la tiene.

- **El algoritmo 13 sobreactuaba: `Q` subía como un trinquete con el ruido
  correlacionado del detector hasta que el filtro era cinco veces más rápido que
  su propio horizonte.** El banco venía mostrando el lazo moviendo el DAC 1,80
  LSB por segundo frente a los 0,49 del algoritmo 11 esa misma noche, y sin
  mejor fase a cambio — y separado en componente rápida y lenta resultó ser
  temblor segundo a segundo, 3,7x, no deriva lenta.

  La causa está en la adaptación de `Q`, y es un trinquete de un solo sentido.
  El filtro sube `Q` siempre que sus innovaciones salen mayores de lo que la
  covarianza predijo. Cuando el error del detector está *correlacionado* — un
  cero que deriva en minutos — salen mayores por una razón ajena al oscilador,
  así que `Q` trepa a 1,02 por segundo hasta que `P` y `S` crecen lo bastante
  para explicarlas. Se detiene, pero alto: `KL` el 30.08 leía `Q=1.238e-03`
  frente a una semilla de `6.36e-06`. 195x en `Q` son `sqrt(195)` = 14x en la
  ganancia que escribe el estado de frecuencia, y esa ganancia es lo que el DAC
  sigue.

  LO ZANJÓ UNA SEGUNDA PLACA. Dan Wiering corrió el mismo firmware en la suya sin
  tocar nada más allá de `CT`, `LC`, `SAW 1` y `ES LTIC`, y fue hasta el final:
  `Q` estaba contra el viejo raíl de 1000x en `4.468e-03` a las 2h37m del
  arranque y seguía allí nueve horas después, con el DAC moviéndose **11,05
  LSB/s** y aplicando un **recorrido de 249 LSB donde el oscilador necesitaba
  19,8**. Medido contra un patrón de rubidio — la referencia independiente de la
  que este proyecto carece — el ADEV a 20 s fue de **8,6e-11 frente a 3,1e-12**
  del algoritmo 11 en la misma placa y la misma referencia: una joroba con
  máximo, como debe ser, en la propia constante de tiempo del filtro. El mismo
  defecto, cuatro veces mayor, en hardware jamás ajustado a mano.

  `(R/Q)^(1/3)` tiene unidades de tiempo y es la constante de tiempo del propio
  filtro, así que `Q = R/KT³` dice exactamente *corre tan rápido como el
  horizonte que te dieron*. Ahí queda ahora acotada la adaptación: **el filtro no
  puede correr más rápido que `KT`**, con la `R` medida y no la sembrada. Eso
  último no es un detalle. La primera versión de esta cota era ocho veces la
  semilla, y funcionaba, pero por suerte: `q_seed` sale de la conjetura a priori
  de 2,5 cuantos, y cuánto se aparta de ella un detector real es propiedad de la
  placa — esta mide 2,5x su semilla y la de Dan 6,8x, de modo que el mismo
  multiplicador significaba tau >= 92 s aquí y >= 95 s allí, y habría significado
  >= 50 s en una placa cuyo detector coincidiera con su semilla. Tomada de la `R`
  en uso dice lo mismo en todas partes, y sigue a un nuevo `LC` en un segundo.
  Medido sobre dos plantas, cinco semillas de ruido y tres niveles de deriva del
  detector: ADEV a tau corto el doble de bueno (7,99e-12 frente a 1,54e-11 en tau
  16), movimiento del DAC 2,4x menor, a cambio de un 7% en la sd de fase y un 25%
  en el ADEV a tau 1024 — y la sd de fase se mide contra el detector, que es el
  instrumento que aquí miente, mientras que un rubidio no. Fijar `Q = 1.238e-03`
  a mano reproduce la cifra del banco en el simulador, 1,07 LSB/s, que es la
  confirmación que el registro por sí solo no podía dar.

  Nada se pierde al negar que `Q` absorba el error correlacionado del detector:
  `R` ya lo lleva, al medirse de diferencias tomadas cerca del horizonte. Un
  valor fijado con `KQ` sigue pasando tal cual; la cota es sobre la adaptación.
  Un lazo más rápido se pide ahora por las buenas: acortando `KT`.

  Y ESO CORTA EN LOS DOS SENTIDOS, conviene decirlo claro. Todo lo anterior es
  con el `KT` por defecto de 100 s. Con `KT` 300 y 1000 el lazo choca con este
  techo y se queda apoyado en él (sd de fase 0,64 -> 2,82 ns a 300, 3,04 -> 47,6
  ns a 1000), porque `Q_seed = R/KT³` cae con el cubo del horizonte mientras que
  la deriva real del oscilador no se mueve en absoluto: pasados unos cientos de
  segundos la semilla deja de estimar nada, y el viejo raíl de 1000x lo estaba
  corrigiendo en silencio. Ahora eso se ve en lugar de esconderse: `KL` imprime
  `[at ceiling]` junto a `Q`, y un lazo que se queda ahí todo un turno está
  diciendo *tu `KT` es más largo de lo que este oscilador soporta*. La reparación,
  cuando llegue, es sembrar `Q` desde el oscilador y no desde el horizonte: TIM2
  mide la deriva de frecuencia directamente y su ruido es independiente del cero
  del detector. Hasta entonces, `KT 100` es la configuración medida. La marca
  `[at ceiling]` de `KL` existe porque este fallo se encontró rastreando una
  captura en busca de ese número, y ese no es un diagnóstico que nadie deba hacer
  dos veces.

- **La compuerta de frecuencia del algoritmo 13 podía quedarse cerrada, y se
  llevaba el holdover con ella.** Encontrado al comprobar lo anterior con el
  detector congelado: la prueba de confianza hace su trabajo y descarta el
  detector, y desde ahí TIM2 es la única medida que queda — pero para entonces el
  oscilador está a un hercio, la innovación es de 100 ns/s y el límite de la
  compuerta de 4 ns/s, porque `P11` no tiene de qué crecer salvo `Q`. Toda
  lectura rechazada, el estado de frecuencia clavado en cero, y el lazo
  cabalgando un modelo que dice que todo va bien mientras la fase se escapa. El
  único caso para el que esta medida existe era el único en el que no podía
  actuar.

  El escape de la compuerta de fase — ensanchar la covarianza con la innovación
  rechazada y dejar entrar la siguiente — se probó aquí primero y midió mucho
  peor: pone la ganancia casi en uno, así que el estado salta a una medida que es
  una media móvil de 100 s y por tanto va cincuenta segundos por detrás, y el
  lazo persigue su propio retardo (+10,2 Hz y 7795 LSB/s, frente a -1,04 Hz y
  0,06 LSB/s rechazando sin más). Para la fase funciona porque la fase es
  instantánea; esta no lo es. Así que la compuerta ahora **recorta** en vez de
  abrirse: tras diez rechazos seguidos la lectura se acepta, pero solo cuatro
  sigmas de ella, y el estado camina hacia la verdad a ritmo acotado. Detector
  congelado: -0,49 Hz y 1,9 LSB/s, el mejor de los tres. Todo caso sano —
  detector contra el raíl, arranque en frío de 400 LSB, ejecución limpia, ambas
  plantas, todas las semillas — queda idéntico bit a bit.

### Cambiado

- **La barra de estado del afinador muestra ahora toda la identidad del firmware,
  y `V` responde con ella.** Antes decía `connected — firmware v1.06` y ahí se
  quedaba, lo que nombra el protocolo pero no el binario. Ahora se lee:

  ```
  connected — firmware v1.06-rtos  build 27  2026-09-02 09:46  CRC 78B08D26
  ```

  La marca de compilación y el número de build ya existían, pero solo en el
  banner de arranque, que para cuando alguien se conecta suele haberse ido de la
  pantalla. El sketch compone ahora esa marca una vez en una cadena compartida
  — tiene que ser el sketch, porque `__DATE__` queda grabado en la unidad de
  compilación que lo menciona y el sketch es la única que `build_id.h` obliga a
  recompilar — y el banner y `V` imprimen los mismos caracteres en vez de dos que
  puedan separarse.

  Los tres datos están porque responden a preguntas distintas: la versión dice
  con qué protocolo habla el afinador, el build y la marca de tiempo de qué árbol
  de fuentes salió, y el CRC qué binario está corriendo realmente. Solo el último
  no puede quedarse obsoleto, que es justo por lo que existe: el 26.08 dos
  capturas de dos builds distintos llevaban la misma marca de tiempo y costaron
  una hora de discutir con un registro que tenía razón.

  Todo lo que sigue a la versión es opcional en el analizador, así que un
  firmware antiguo que responde a `V` solo con el nombre sigue conectando y la
  línea simplemente dice menos.


- **`KT` por encima de 200 s ahora avisa, porque el lazo no puede servir a los dos
  extremos.** `Sg` se siembra y se acota en `R/KT³`, así que un horizonte largo
  obliga al *estimador* a ser tan lento como el *controlador*, y son cosas
  distintas. Quitar la cota arregla `KT` 1000 por completo (sd de fase 50,2 ->
  2,92 ns, asentándose de inmediato en vez de tras 11120 s) y devuelve el
  trinquete de golpe en `KT` 100 (`Q` a 1,12e-3, ADEV en tau 16 de 8,05e-12 a
  4,08e-11). Congelar la adaptación mientras el lazo manda — la guarda que
  funciona para `R` — tampoco ayuda: el trinquete se desarrolla también en reposo.
  Separar ambas cosas exige medir `Sg` desde el oscilador, y `Sg` son 6,4e-6
  (ns/s)²/s frente a un suelo de ruido de TIM2 de 8 (ns/s)²: seis órdenes de
  magnitud por debajo de lo que esta placa ve. Así que la cota se queda, `KT 100`
  sigue siendo la configuración medida, y el firmware lo dice cuando se pone algo
  más largo.

- **El cero del detector como cuarto estado se construyó, se midió y se dejó
  fuera del árbol**, conservado entero en `doc/algo13-zero-state.patch`. Hace lo
  que se diseñó que hiciera: con 12 ns de deriva del cero el DAC se mueve un 32%
  menos y el ADEV a tau corto mejora 1,6x. También mantiene peor la fase real
  (sd 12,80 -> 13,90, track sd 1,13 -> 1,24, `r` 0,929 -> 0,917, ADEV a 1024
  +17%), porque `x0` y el cero son casi degenerados: la medida de fase solo ve su
  suma, y lo único que los separa es TIM2 con unos 8 (ns/s)². Se probaron
  constantes de tiempo de 1x, 2x, 5x y 10x `KT`. Ambas métricas perdedoras se
  miden contra la lectura de fase, que es el instrumento del que trata el cambio,
  así que este simulador no puede zanjarlo; una referencia independiente que mida
  la salida lo haría en una noche.



- **Algoritmo 10, etapa LOCK: la banda muerta desaparece, sustituida por la
  construcción del propio algoritmo 12.** El LOCK anterior trataba como cero
  cualquier error de fase por debajo de `range_ns/40` — 47 ns en esta placa —
  con un codo suave por encima, y el lazo se estacionó fielmente en unos +76 ns
  permanentes durante hora y media. Eso no era un fallo del lazo; era su
  especificación. Quitarla y actuar sobre la media simple del intervalo resultó
  peor (la fase barrió ±500 ns, ambos topes del detector), porque una media de H
  segundos es la fase de hace H/2 y esta etapa ya sólo corrige cada H.

  El algoritmo 12 no promedia. Su prueba `(a+b) + 2*(b-a)` conserva dos
  semiventanas contiguas y EXTRAPOLA al final del par, de modo que el promediado
  y el retardo se cancelan por construcción — y el mismo par da la pendiente,
  que es una medida del error de frecuencia que LOCK de otro modo no ve en
  absoluto (`Kp` es 0 aquí, así que el término de TIM2 es idénticamente cero).
  LOCK conserva ahora ese par, condiciona la fase al error estándar de la media
  más reciente y la pendiente al error estándar de una diferencia de dos medias,
  e inyecta la pendiente en el integrador como corrección absoluta de PWM. No se
  supone ningún umbral en ninguna parte: sigma sale de las primeras diferencias
  del detector, el mismo estimador que usa el algoritmo 12.

  Medido el 21.08 con `LIV 30`, 25 min en LOCK sin pérdida: RMS de fase
  **6,9 ns** tras el asentamiento (media **+2,2 ns**, −14,3…+19,1 ns), 31
  correcciones, paso mediano 2 LSB, el mayor 9. El ADEV solapado coincide con el
  registro de 23 h del algoritmo 12 dentro de un pequeño porcentaje en todo tau
  hasta 128 s (3,5e-9 a 1 s, 1,0e-10 a 128 s) — que es el objetivo del cambio:
  el lazo de tres etapas mantiene ahora la fase tan bien como el acumulador, en
  el mismo hardware y con menos esfuerzo de control.

- **La cadencia de LOCK queda acotada por la deriva que el lazo acaba de
  medir.** Nunca dejar que la fase recorra más de dos sigmas de su propio ruido
  entre correcciones, usando la pendiente que el par ya proporciona. Esto sólo
  puede ACORTAR el intervalo, y una placa sin deriva resoluble conserva el
  `lock_interval_s` completo que se le dio, porque la cota es una división por
  una pendiente que lee cero. Barrido en simulación: con `LIV 300` y la deriva
  medida en este hardware, el RMS de fase es 90 ns a ocho sigmas, 50 a cuatro y
  34 a dos, frente a 100 de la antigua banda muerta; el lazo que mira más a
  menudo tiene menos que deshacer cada vez, así que los pasos individuales de
  PWM son MENORES, no mayores (unos 10 LSB a dos sigmas frente a 17 a ocho).
  `LIV 30` sigue siendo el mejor ajuste medido y no necesita cota alguna.

- **Transferencia sin salto DPLL↔LOCK.** El integrador es el objetivo absoluto
  de PWM en este lazo (`u = integ - pwm`), así que al cambiar de etapa se
  resiembra ahora con lo que se acaba de escribir. Medido en la transición del
  21.08: 40853 → 40854, un paso de un LSB donde antes la fase recibía el tirón
  de donde hubiera acabado `integ`.

- **Vcc se muestra con tres decimales** y la fila `dph` se etiqueta `dp:` en el
  panel de 320×240; `qE:` se amplía a `qEr:`. Las tres filas tocadas cubren
  ahora exactamente el tramo 168..314 px.

- **El ACQ del algoritmo 10 era incondicionalmente inestable — la ganancia
  quintuplicaba el límite de estabilidad.** ACQ actúa cada 5 s pero gobierna
  con `avg100`, una media móvil de 100 s: una corrección no puede llegar a la
  medida hasta 100 s después, y el lazo actúa veinte veces dentro de esa
  ventana. La ganancia era la mitad de la planta (`acq.Kp = 0.5 * lsb_per_hz`),
  así que aplicaba unas diez veces lo necesario antes de que la medida pudiera
  responder.

  Medido el 25.08 a las 21:06, una entrada en ACQ en frío con el OCXO ya dentro
  de 0,02 Hz: el PWM recorrió **31229..51512** — veinte mil LSB — Vctl
  1,38..2,15 V, el guardián de runaway saltó dos veces y la sesión terminó
  clavada en **+2,53 Hz** con el PWM congelado sus últimos 527 s. El simulador
  lo reproduce solo con las constantes publicadas (PWM ±13319, terminando en
  2,79 Hz) y diverge incluso partiendo de un LSB, que es lo que significa aquí
  «incondicionalmente».

  El límite es `Kp * K < 2 * periodo / ventana` = 0,10. Barrido en simulación
  desde 0,02 Hz: 0,50 diverge, 0,25 se arrastra, 0,10 es el borde (334 s), 0,05
  se asienta en 167 s. `acq.Kp` es ahora **0,05 × lsb_per_hz**, con un factor
  dos de margen, y converge monótonamente desde 1 LSB, 0,02 Hz, 1 Hz y 3 Hz sin
  sobreoscilación alguna (pico 0,95× del desvío inicial, 603 s en el peor
  caso). Escala con la K medida de la placa, así que vale para cualquier OCXO.

  Esto solo mordía en un ACQ EN FRÍO. `g_ltic.state` se guarda, de modo que un
  arranque en caliente reanuda en DPLL o LOCK y nunca recorre esa vía — por eso
  un algoritmo publicado en v1.04 ha tardado hasta ahora en enseñarlo.

- **El centrado en ACQ se condiciona a «no al riel», no a «dentro de banda», y
  el error se recorta a la banda en vez de descartarse con ella.** Un término,
  dos fallos opuestos. Condicionarlo a la prueba completa de banda detuvo el
  empujón del 20.08 (Vphase 3,187 V frente a una banda de 0,818..2,865 V, err_v
  +1,35 V, 690 LSB barridos), pero también apaga la captura siempre que la
  banda que registró LC es más estrecha que el detector real — y en este
  hardware es una quinta parte. Medido el 25.08 con la ganancia de ACQ ya
  corregida: tras armar el picDIV la fase quedó aparcada en −1320 ns, o sea
  1,583 V frente a una banda registrada de 1,729..2,433 V. Fuera de banda, así
  que sin centrado; la frecuencia ya en el objetivo, así que sin término de
  frecuencia; `u = 0`, PWM congelado, y ACQ→DPLL exige |fase| ≤ 200 ns. Un
  bloqueo permanente con todos los guardianes callados, porque no había nada
  mal salvo que el lazo se había apagado a sí mismo.

  Lo que carece de sentido fuera de banda es la MAGNITUD de `V - centre`, no su
  signo: la rampa es monótona hasta los rieles, así que la lectura sigue
  diciendo hacia dónde está casa, y a ACQ no le hace falta más — es un empujón
  proporcional acotado que no integra nada. Ahora gobierna siempre que la
  lectura esté fuera de los rieles, con el error recortado al borde de la
  banda: sin cambios dentro, un tirón de borde de banda fuera en lugar de un
  empujón de 1,35 V, y en el riel se detiene como antes. DPLL y LOCK conservan
  la prueba estricta: ellos integran la fase, que es para lo que existe.

- **El sintonizador mostraba un estado de lazo caduco en los algoritmos con
  vocabulario propio.** `parse_state()` rastreaba la línea entera buscando una
  lista fija de palabras, y la lista no daba abasto: el firmware emite `SYNC`,
  `FLL`, `ZC`, `HYB`, `NoPL`, `hit` y diez palabras de dirección como `uf+` que
  nunca estuvieron en ella. En esos segundos no devolvía nada y la etiqueta
  seguía mostrando lo último que había reconocido — con el algoritmo 12 el panel
  leía **LOCK durante cada segundo de CORR y de ZC**. `[HOLDOVER]` tampoco
  encajaba, así que el holdover era invisible. La búsqueda no estaba anclada, de
  modo que cualquiera de esas palabras en cualquier línea podía fijar el estado.

  La tendencia es un CAMPO, no un vocabulario: es el último token de la línea
  `PWM:`, y en holdover el firmware lo sustituye entero. Tomado por posición,
  una palabra que el firmware invente mañana se muestra literal en vez de
  perderse, y nada más en el enlace puede confundirse con ella. `___` limpia
  ahora la etiqueta en lugar de dejar en pie la palabra anterior — quien la
  llamaba comprobaba veracidad donde debía comprobar `is not None`, que era el
  mismo fallo de pantalla caduca un piso más arriba. Quince casos verificados
  contra la salida real del firmware.

- **El `dph` del panel ganó el decimal que el registro siempre tuvo.** El informe
  serie imprime un decimal y el panel imprimía nanosegundos enteros: invisible
  mientras las lecturas eran de cientos de ns, clamoroso al asentarse el lazo en
  una cifra — el registro decía −5,2 y el panel −5. El decimal no se podía añadir
  sin más: ambos campos de fase están dimensionados para la cadena `+0000ns`, y
  el propio comentario del panel de 320 avisa de que una lectura de cinco
  dígitos desbordaría la etiqueta. Así que un decimal por debajo de 100 ns,
  donde la forma más ancha `-99.9ns` son exactamente los siete caracteres para
  los que se midió el campo, y números enteros por encima. Comprobado en todo el
  rango del detector: la cadena más ancha que cualquiera de los dos formatos
  puede producir tiene siete caracteres, así que no hubo que recortar ningún
  relleno. `dtostrf`, no `%.1f` — este fichero no usa conversiones en coma
  flotante dentro de `snprintf` a propósito, porque imprimen `?` sin Float
  printf activado en el IDE.

- **La pestaña Help del sintonizador se puso al día.** `DAC` documenta ya el
  argumento de ruta; `LTO`/`LTR` hablan de voltios; el valor por defecto de `MR`
  es 7, no 9; `AQI`/`AQD` aparecen como guardados-pero-inertes con un puntero a
  `ACG`; la lista de tendencias del algoritmo 12 cubre todo el vocabulario y no
  tres palabras de él; `SAW` describe los contadores de emparejamiento; y
  `FA`/`FAD`/`FAL` están documentados siquiera, tras faltar desde que se
  añadieron. Verificado mecánicamente contra la lista de verbos extraída de
  `gpsdo_cli.cpp` — ya no falta en la pestaña nada a lo que el firmware
  responda.

- **`DAC PWM|DITH|EXT` — la ruta de la tensión de control se elige en tiempo de
  ejecución.** Las tres rutas de salida se compilan ahora juntas y el comando
  elige cuál gobierna el firmware. La SEÑAL la conmutan los puentes de la placa;
  no hay multiplexor por software ni debe haberlo, porque dos controladores
  peleando por la tensión de control es un fallo de hardware, no un modo. Lo
  que el firmware necesita saber es qué ruta gobierna, para que el tamaño del
  paso, la telemetría y la aritmética de la ruta fina describan lo que está
  realmente conectado.

  `GPSDO_PWM_DITHER` y `GPSDO_DAC_EXT` eran mutuamente excluyentes al compilar y
  ya no lo son — los pines nunca chocaron (PB9/TIM4 frente a PB4/PB0/PB2), solo
  chocaba la suposición de que un binario gobernaba una salida. Un binario sirve
  ahora para cualquiera de los dos cableados, y comparar con y sin dither es un
  comando en vez de una regrabación.

  PWM y DITH comparten PB9/TIM4 CH4, así que elegir PWM en una placa con motor
  de dither **no** desmonta el DMA ni devuelve el pin a `analogWrite`. Escribe
  el mismo código de 24 bits con los ocho bits bajos a cero: todas las entradas
  de la tabla idénticas, ciclo de trabajo constante, bit a bit la tensión que
  daba el PWM simple — la misma salida sin una reconfiguración que pudiera
  fallar a medias. `gpsdo_dac_fine_available()` pasa a ser propiedad de la ruta
  ACTIVA y no del build, de modo que un lazo que gobierna en fracciones se
  entera cuando la ruta de LSB entero va a tirarlas.

  Se guarda en un byte tallado del relleno de alineación entre `tz_str` y
  `a12_gain` — verificado con el compilador, no a ojo — así que la disposición,
  el tamaño y `SETTINGS_VER` quedan intactos y un bloque escrito por un build
  anterior sigue cargando. Cero significa SIN FIJAR y pide el valor por defecto,
  por eso la codificación empieza en 1: un registro antiguo se lee como «usa el
  valor por defecto» y no como «ruta 0». **El valor por defecto es DITH**,
  resuelto al arrancar contra lo que de verdad está compilado, y nunca resuelto
  a una ruta incapaz de gobernar el pin. En un build con DAC externo pero sin
  motor de dither, un valor sin fijar da PWM y no EXT — el integrado externo es
  una elección de hardware deliberada y hay que pedirla.

  El informe imprime ahora la tensión ordenada junto a la medida y avisa cuando
  difieren en más de medio voltio. El firmware no ve el puente; la única prueba
  de que el ajuste y el cableado no concuerdan es que la tensión de control no
  está donde se le dijo. Medio voltio es deliberadamente holgado — el divisor
  del ADC y la referencia son buenos a unos pocos por ciento como mucho —
  porque lo que tiene que cazar es «ordenado 1,80 V, medido 0,00 V», no un
  error de escala.

- **`LTO` y `LTR` toman voltios, como todo lo demás en este detector.** El
  mismo punto físico — el cero de fase del detector — se guardaba en dos
  unidades que no coincidían: `LZO` = 2,0809 V para el algoritmo 10, `LTO` =
  2620 cuentas de ADC para el 11, o sea 2,1104 V. Treinta y siete cuentas de
  diferencia, unos 37 ns una vez corregida la escala, e invisible porque nadie
  compara 2,0809 con 2620 a ojo. Ambos comandos toman e imprimen voltios ahora;
  el bloque de ajustes sigue guardando cuentas, exactamente el reparto que usa
  `MLP` con los nanosegundos, así que sin subir `SETTINGS_VER` ni migrar nada.
  Los dos imprimen también la cuenta, que es lo que aparece en un volcado de
  flash. Un valor entre 3,3 y 4095 se rechaza con la conversión ya hecha —
  «2620 counts = 2,1104 V — type that» — porque es el único error que alguien
  va a cometer de verdad. En el sintonizador `LTO` y `LTR` pasan a ser casillas
  en voltios, TODAS las etiquetas llevan su unidad (o un «(x)» explícito si es
  adimensional), y el patrón de lectura del algo 11 dejó de anclarse al final
  de la línea, cosa que si no habría descartado en silencio cada respuesta
  `tic_offset=2.1104 V (2620 counts)` dejando la casilla vacía mientras la
  placa contestaba perfectamente.

- **`AQI` y `AQD` no hacían nada, y no lo decían.** ACQ lee `pid->Kp` y nada
  más; el tirón de centrado sale de `g_ltic_acq_centre_gain`, que fija `ACG`,
  un global aparte con sus propias unidades. `acq.I_LIMIT` SÍ está vivo — es el
  limitador de paso — así que el par inerte es exactamente Ki y Kd. Los ponía
  autotune, los imprimía `LL`, los fijaban `AQI`/`AQD`, se guardaban y el
  sintonizador los ofrecía como casillas editables: cinco maneras de decir que
  un mando funciona cuando girarlo no cambia nada. Ahora cada lectura y cada
  escritura lo dice, `LL` lleva la misma nota en la fila ACQ, y el
  sintonizador atenúa ambas casillas — la lectura sigue mostrando lo que tiene
  la placa, simplemente no se puede girar. Los verbos siguen aceptando valor,
  porque el sintonizador manda los cuatro juntos al pulsar Apply y un rechazo
  ahí parecería un fallo suyo. Quitar los campos implica tocar el bloque de
  ajustes, que es otro trabajo.

- **La fila de fase del TFT restaba el diente de sierra del pulso SIGUIENTE.**
  Ambas rutas de visualización pedían la corrección a
  `ubx_timtp_correction_ns()` en el instante de dibujarse, y esa función
  devuelve el qErr decodificado más recientemente. El informe serie está
  condicionado a un cambio de `ppscount`, así que se ejecuta en el primer
  despertar tras el pulso y recibe el correcto. El TFT se redibuja en CADA
  despertar de `vDisplayTask` — y uno de ellos viene del parser de GPS, ya
  consumida la ráfaga serie del receptor. TIM-TP va dentro de esa ráfaga, así
  que para entonces `g_qerr_ns` ha pasado al pulso siguiente mientras
  `g_ltic_voltage` sigue siendo la rampa de este. El panel restaba qErr(N+1) de
  la fase(N).

  Se detectó como una lectura del panel visiblemente mayor que la línea del
  registro impresa en el mismo segundo, y «mayor» es exactamente lo esperable:
  en este receptor los qErr sucesivos están ANTI-correlados (corr −0,30, periodo
  2–3 s), de modo que la corrección del pulso vecino añade el diente de sierra
  en vez de quitarlo. Medido sobre el registro del 25.08: restar el vecino lleva
  el residuo de 11,46 ns a 13,6 ns — peor que no corregir. Alan Cashin planteó
  justo este modo de fallo para el lazo ese mismo día; estaba en la pantalla.

  `ltic_read_fast()` corre ~50 µs tras el flanco del PPS, antes de la ráfaga que
  trae la trama siguiente, así que el qErr visible ahí todavía pertenece a este
  pulso. Ahora se enclava una vez y todos los lectores usan el enclave, con lo
  que informe, panel y lazo coinciden por construcción se dibujen cuando se
  dibujen.

- **Algoritmo 10 durante la noche, 26.08: 9,7 h en LOCK, sin caídas.** ACQ tardó
  225 s, DPLL 47 s, y a partir de t = 410 s la máquina de estados no volvió a
  moverse. Asentado durante 9,29 h: media de fase **+0,09 ns**, sd 7,42 ns,
  deriva **−0,10 ns/h**, PWM dentro de una banda de 32 LSB con una corrección
  cada 53 s. ADEV solapado: 4,4e-9 a 1 s, 1,0e-10 a 128 s, 1,2e-11 a 1024 s,
  1,7e-12 a 8192 s. La correlación residual contra qErr se mantuvo en +0,021
  toda la noche, así que la cancelación del diente de sierra no es un artefacto
  de una prueba corta.

  La sesión también enseña dónde vive ahora el error restante. Separando la fase
  en ruido por muestra (de las primeras diferencias) y todo lo más lento:

  ```
                            sd      suelo de ruido   estructura lenta
    algo 11, 5,2 h        3,11 ns      2,56 ns           1,76 ns
    algo 10, misma ventana 7,76 ns     2,53 ns           7,34 ns
  ```

  El suelo del detector es idéntico — misma placa, mismo receptor — así que la
  diferencia está entera en el lazo. El algoritmo 11 corrige cada segundo con
  una constante de 60 s; el LOCK del algoritmo 10 corrige una vez cada 53 s con
  una integral de fase cuya constante de recuperación se acerca a 800 s, y la
  fase vaga ±20 ns en periodos de 250–2400 s porque el lazo es más lento que lo
  que la mueve. El límite ahora es LOCK, no el detector.

- **`ltic_autotune()` recalcula solo cuando cambian sus entradas.** Cada
  ganancia que deriva es función pura de `lsb_per_hz` (de CT) y `range_ns` (de
  LC), y sin embargo se ejecutaba en cada transición a ACQ y tiraba en silencio
  todo lo que hubiera escrito el operador. Ocurrió dos veces en una noche
  probando a mano la ganancia de ACQ: se fija `AQP`, el lazo cae a ACQ,
  autotune repone el valor viejo, y lo que se observa después es el ajuste que
  uno creía haber sustituido — sin una sola línea en el registro. Ahora se
  ejecuta una vez por arranque y de nuevo cuando CT o LC mueven las constantes
  medidas, que es lo que de verdad necesita el propósito de «no hace falta
  ajuste manual por placa».

- **El guardián de runaway no podía soltarse nunca.** Congelar ponía `u = 0`,
  lo que deja el OCXO donde lo dejó la fuga; la fase entonces recorre el
  detector sin fin, `railed_now` no se limpia, `|e_freq|` no baja de 0,25 y la
  condición de liberación no puede cumplirse. La sesión del 25.08 estuvo justo
  en ese estado sus últimos 527 s. El guardián ahora devuelve el PWM hacia
  `start_pwm` — el último código mantenido con el lazo sano — a lo sumo 50 LSB
  por ciclo, en vez de pararse en seco: una fuga de 20 000 LSB se deshace en
  media hora y el guardián suelta en cuanto la frecuencia vuelve por debajo de
  0,25 Hz por el camino.

- **`LNV` / `LZO` / `LRN` puestos a mano no sobrevivían a un reset.** La
  calibración del detector vive en dos sitios y al arrancar gana el otro: `LC`
  la escribe en la ranura del live-store, `ES LTIC` en el bloque de ajustes, y
  `setup()` aplica primero el bloque y DESPUÉS la ranura. Medido el 25.08: se
  fijó `LNV 1252`, se guardó, y la placa arrancó con 2649,3914 sin decir nada.
  `ES LTIC` refresca ahora también la ranura live, así que ambos coinciden y el
  orden de carga deja de importar. La otra opción era reordenar las cargas,
  pero `LC` guarda SOLO en la ranura live, así que hacer autoritativo el bloque
  de ajustes habría impedido que un `LC` normal sobreviviera a un reinicio.

- **La prueba de cruce por cero la arma solo una corrección POR LÍMITE (regla de
  Alan).** Una sola bandera hacía dos trabajos: impedir una segunda corrección
  mientras el empuje deliberado de la primera todavía lleva la fase a casa —
  añadido nuestro, tras la sobreoscilación de +3800 LSB del 14.08 — y armar la
  cancelación en el cruce por cero. Solo el segundo es de Alan, y su regla es
  más estrecha de lo que hizo el port: una corrección programada (el nivel `MR`)
  salta por reloj, con la fase donde esté, así que no hay empuje conocido que
  cancelar ni motivo para esperar cruce alguno; y un ZC no se rearma, porque el
  ZC *es* la cancelación. Armar en cualquiera de esos casos dejaba que un cruce
  ajeno, minutos más tarde, sacara un paso de una pendiente caduca. Los dos
  trabajos son ahora dos banderas: la supresión durante el asentamiento sigue a
  toda corrección; el armado, solo a la vía del límite.

- **El valor por defecto de `MR` es 7 (256 s), no 9 (1024 s)** — el valor del
  propio Alan, y el que convierte la corrección programada en el caballo de
  batalla que debe ser en lugar de una red de seguridad cada 17 minutos. El
  código cambió con el trabajo del algoritmo 12; los tres manuales decían 9 en
  dos sitios cada uno hasta ahora.

- **`MLP` y `ML` hablan en nanosegundos.** La tabla de límites se guarda en
  unidades del acumulador — un nivel contiene 2^(nivel+1) muestras de
  `2*fase + 1`, de modo que el número guardado es la fase desplazada a la
  izquierda `nivel+2` — y ambos comandos imprimían ese número crudo llamándolo
  «ns». Ajustar una magnitud que nadie puede relacionar con un osciloscopio es
  ajustar a ciegas. `MLP <n>` imprime ahora las dos (`lim[6]=126ns (32350 units
  over 128s)`), `MLP <n> <ns>` toma nanosegundos, `ML` tabula ns junto a
  unidades y ventana, y las casillas de límites del sintonizador van en
  nanosegundos. El formato de almacenamiento no cambia, así que el bloque de
  ajustes y lo ya guardado quedan intactos.

- **Nada afirma que el detector de fase esté presente cuando no puede saberlo.**
  Dave Solder_Junkie montó su placa sin detector y todas las capas le dijeron
  que iba bien: el banner de arranque imprimía
  `HW: LTIC phase input OK (PA1 analog)`, `LA 12` fue aceptado, y el lazo
  disciplinaba el OCXO con ruido de ADC en un pin flotante. Tres cambios —
  ninguno detecta el hardware, porque nada puede — pero dejan de fingir. El
  banner dice ahora `enabled (PA1) - needs the ramp detector hw`. El comentario
  de `GPSDO_LTIC` en `gpsdo_config.h` enuncia el requisito en vez de describir
  el circuito. Y `LA 10`, `LA 11` y `LA 12` comprueban si `LC` ha corrido ALGUNA
  vez: si la pendiente del TIC *y* el rango del detector siguen en sus valores
  de compilación, el aviso ya no es el suave «uncalibrated» sino «no detector
  calibrated, phase may be floating — is the hardware really there?».

- **El centrado en ACQ se detiene ante una lectura inválida del detector**, como
  ya hacía el algoritmo 12. El término de centrado gobierna sobre la tensión
  cruda, y un detector saturado informa de una tensión que ya no sigue a la
  fase; la puerta de deriva no lo cazaba, porque una lectura al riel es plana y
  su deriva sale cero. Medido el 20.08 a las 19:42, tres segundos después de
  pasar al algoritmo 10: Vphase 3,187 V frente a una banda de 0,818..2,865 V, el
  término de centrado saturando su propio tope y empujando mientras el detector
  siguió fuera de banda, 690 LSB de PWM barridos antes de asentarse. Detenerse
  no cuesta nada: la vía de frecuencia trabaja desde TIM2, que ve el offset haga
  lo que haga el detector, y es exactamente aquello a lo que recurre el
  algoritmo 12.

### Añadido
- **DAC externo AD5680: el driver existe.** `dac_ext.cpp` sale del estadio
  de stub — bit-bang por GPIO en CS/SCK/MOSI = PB4/PB0/PB2 (rutas del PCB de
  Dan Wiering; PB2 es a la vez BOOT1, así que la pista MOSI va sin pull-up),
  palabra de 24 bits MSB-first (`code << 2`, bits de comando a cero =
  escritura y modo normal), DIN muestreado en el flanco de bajada de SCLK,
  registro enganchado en el flanco de subida de SYNC, envío bajo ~20 µs de
  interrupciones enmascaradas. Se activa con `GPSDO_DAC_EXT`. De paso:
  TM1637 y el generador de 2 kHz quedan APAGADOS por omisión (política de
  v1.06, opciones históricas) y ceden sus pines automáticamente cuando el
  DAC externo está activo — en vez de #error, el build simplemente los omite.

  Ya compila en una cadena de herramientas real: `hostcheck` ganó dos filas
  AD5680 (con y sin detector de fase) y ambas compilan y enlazan para cortex-m4
  con `arm-none-eabi-g++`. Eso cierra la reserva de «solo revisado» con la que
  salió el driver — no cierra la prueba en hardware, que le toca a Dan.
### Añadido
- **Algoritmo 13 — un filtro de Kalman de tres estados (fase, frecuencia,
  envejecimiento).** Hasta ahora cada lazo de este firmware tenía un ancho de
  banda elegido una vez y asumido: el algoritmo 10 conmuta entre tres, el 11
  tiene una constante de tiempo, el 12 escoge un nivel de una tabla de umbrales.
  Los tres responden a la misma pregunta — cuánto creerse la lectura de este
  segundo — con un número decidido de antemano. Este la responde a partir de las
  varianzas, y la vuelve a responder cada segundo.

  **No cuesta nada que merezca la pena contar.** Tres estados y una medida
  ESCALAR, así que la inversión de matrices que a todos preocupa es una división:
  unos 140 productos-acumulación y 36 bytes de estado, una vez por segundo, cosa
  de un microsegundo en un M4F a 100 MHz. La idea recibida de que un Kalman
  necesita un Cortex-A o una FPGA se refiere a filtros GNSS de veinte estados, no
  a esto.

  **Ambas cifras de ruido se miden, no se fijan.** R sale de las primeras
  diferencias del propio detector, el mismo estimador que el algoritmo 12 usa
  para su sigma. Q se adapta a partir de la secuencia de innovaciones: el filtro
  predice cómo de grandes deberían ser sus propias sorpresas, y cuando son
  sistemáticamente mayores el ruido de proceso es demasiado pequeño. `KR` y `KQ`
  fijan cualquiera de las dos para un experimento; cero significa medir.
  Sembradas con lo que dio la noche del 26/27.08 — 2,64 ns y 2e-6 (ns/s)²/s, esta
  última consistente en ventanas de 600, 1800 y 3600 s, que es el aspecto de un
  random-walk FM — así el filtro es sensato desde su primer segundo.

  **El holdover no necesita código propio.** El estado lleva frecuencia Y
  envejecimiento con sus covarianzas, así que perder la fase no es un caso
  especial: deja de actualizar, sigue prediciendo, sigue gobernando. La tendencia
  muestra `HOLD` y `KL` indica cuánto lleva funcionando solo con el modelo.

  Medido con `tools/loopsim`, reproduciendo el oscilador reconstruido de los
  registros del 26/27.08, cinco semillas de ruido:

  ```
                       sd de fase [ns]      ADEV @ 1024 s
    algoritmo 11      3,62 / 7,39           7,7e-12 / 1,5e-11
    algoritmo 12      3,07 / 4,20           4,1e-12 / 7,6e-12
    algoritmo 13      1,20 / 1,25           1,4e-12 / 1,8e-12
  ```

  Lo interesante es la segunda columna: los otros dos lazos pierden terreno en la
  planta que más deriva y este no, que es el ancho de banda adaptativo haciendo
  su trabajo y no una constante mejor elegida.

  Dos salvaguardas, ambas puestas por el simulador y no por gusto. **Una puerta
  que nunca se abre es un filtro roto**: la puerta de innovación rechaza una
  lectura a más de cuatro sigmas de la predicción — aquella noche hubo dos picos
  de ±40 ns — pero diez rechazos seguidos significan que lo equivocado es el
  ESTADO, no el dato, así que el filtro ensancha su propia creencia. Y **una fase
  todavía fuera de la ventana ACQ tras cinco horizontes es una lectura que no se
  mueve con el oscilador**: se arma el picDIV una vez y se reinicia el filtro.

  Nuevos comandos `KR` / `KQ` / `KT` / `KL`, guardados al momento en su propio
  registro del anillo flash (`REC_A13`) y no en el bloque de ajustes, que ya no
  tiene relleno libre.

- **TAB o ESC pausa y reanuda la telemetría con una tecla.** Sugerencia de Alan
  Cashin, y acertada: `RP` y `RR` ya hacen esto, pero teclear una orden mientras
  los informes pasan a toda velocidad es justo lo difícil, y el remedio no
  debería necesitar él mismo un hueco para escribir. Un ESC solo conmuta como
  TAB; un ESC seguido de `[` u `O` es una flecha o una tecla de función y se
  descarta, así que buscar en el historial ya no detiene los informes. Una línea
  a medio escribir se abandona al conmutar en vez de unirse a la siguiente.

- **Carga de CPU en la línea de telemetría.** `CPU:7%`, añadido a la línea de
  sensores. Medida, no modelada, y sin temporizador propio:
  `vApplicationIdleHook()` incrementa un contador y una vez por segundo la cuenta
  se convierte en porcentaje contra la mayor cuenta por segundo jamás vista, que
  por definición es un segundo ocioso.

  Lo que ve: todo lo que la tarea ociosa no recibió, tiempo de interrupción
  incluido. Lo que no ve: una placa que nunca ha estado cerca de ociosa, donde la
  referencia se queda corta y la cifra resulta optimista. La referencia se fuga
  un 0,02% por segundo para seguir a la placa en lugar de quedar clavada por un
  segundo afortunado en el arranque. Va al FINAL de la línea a propósito.

### Medido
- **La cifra de "27% peor que el algoritmo 11" era errónea y queda retirada.**
  Venía de comparar dos noches distintas, y las dos sesiones de algoritmo 13 que
  la sostenían estaban mal configuradas: una tenía `KR` fijado en 2,5 ns, lo que
  le costó el once por ciento de sus lecturas en la compuerta de innovación, y la
  sesión del 29.08 a las 12:31 tenía el estimador de R disparándose hasta
  **47,70** durante los enganches — justo el fallo para el que se escribió el
  seguro de "congelar mientras se mueve", en una compilación anterior a él.
  Ninguna de las dos debería haberse usado para juzgar el lazo.

  La captura del 28.08 cambia de algoritmo a mitad de sesión, lo que resuelve en
  una tarde lo que la comparación entre noches no resuelve en absoluto. El
  algoritmo 13 corrió 4,24 h y el 11 las 3,53 h siguientes, seguidos, en la misma
  placa:

  ```
    algo   sd fase  suelo   lento  track sd    r    requerido aplicado
     13     3,67    2,72    2,46     0,54    0,996    20,7      59,0
     11     3,22    2,70    1,76     0,48    0,959    10,0      23,0
     12     6,18    2,67    5,57     0,87    0,959    20,0      28,0
  ```

  Trece por ciento de diferencia en error de seguimiento, catorce en sd de fase
  — y al algoritmo 13 le tocó la mitad más difícil, con un rango de control
  requerido de 20,7 LSB frente a los 10,0 del 11. Ambos van muy por delante del
  algoritmo 12 en la misma sesión. Ése es un lazo distinto del que describía la
  cifra entre noches.

- **Y con ella se retira el veredicto sobre el estimador de R.** La reproducción
  decía que el retardo de 16 s medía marginalmente peor que el de 1 s; el banco
  dice que R llegó a 47,70 en la compilación sin el seguro, contra un suelo
  blanco de 6,4. `loopsim` no lo reproduce porque sus plantas no llevan un
  transitorio de enganche bastante grande: la fuga ocurre mientras el lazo mueve
  la fase, que es exactamente el estado en el que el seguro congela el estimador.
  El cambio se queda, y el resultado de la reproducción queda como lo que es: la
  medida de una situación que la reproducción no contiene.

- **Una diferencia de comportamiento sobrevive a todas las sesiones: el algoritmo
  13 castiga el actuador mucho más de lo necesario.** 59 LSB aplicados para 20,7
  requeridos el 28.08, y 111 para 8,0 en la mala sesión del 29.08, frente a los
  23 para 10,0 y 17 para 6,1 del algoritmo 11. No sub-corrige — esa sospecha
  queda cerrada — sobre-actúa, de forma consistente, y eso es lo que queda por
  atacar.

### Añadido
- **`tools/logab.py` — comparar los algoritmos entre sí dentro de una misma
  captura.** Por cada tramo contiguo de un algoritmo imprime la sd de fase, el
  suelo del detector, la estructura lenta de la que responde el lazo, y el error
  de seguimiento contra el control que el oscilador realmente necesitó
  (reconstruido como `loopsim` construye sus plantas). Dos algoritmos en dos
  noches son dos experimentos; una captura que alterna entre ellos es una
  comparación.

  Arregla además una trampa que costó un día: la línea Learn tiene una forma
  distinta por familia de algoritmo, así que una expresión regular escrita para
  una de ellas conserva en silencio el valor anterior para las demás — lo que en
  el primer intento convirtió una sesión de cuatro algoritmos en un único tramo
  de 20 h de "algoritmo 13". De la línea Learn sólo se toma `algo=`; todo lo
  demás viene de líneas que imprime cualquier algoritmo.

### Medido
- **Tres sesiones de banco del algoritmo 13, y el único hallazgo seguro es que
  un `KR` fijado costó el once por ciento de las lecturas.** La sesión del
  29/30.08 descartó en la compuerta de innovación **4904 de 45275** muestras
  — `rej` pasó de 98 a 5021 — y nada lo dijo: la línea Learn lleva un contador
  acumulado que nadie deriva mientras pasa, y la sd de fase parecía normal en
  6,07 ns. `KR` estaba fijado en 2,5 ns, lo que sitúa R por debajo de las
  innovaciones que el detector realmente produce y deja la compuerta de 4 sigma
  demasiado estrecha. Las dos sesiones cortas del 30.08, con R medida, no
  rechazaron **ninguna** — y el simulador tampoco. Así que: deje `KR` en 0 salvo
  que fijarlo sea el experimento.

- **El lazo no sub-corrige**, que era la sospecha tras la observación de 57
  frente a 93 LSB. Reconstruyendo de cada registro lo que el oscilador necesitó
  de verdad: el control requerido abarcó **17,6 LSB** la noche del algoritmo 13 y
  **18,6 LSB** la del 11 — las dos noches fueron todo lo comparables que se puede
  pedir — mientras los lazos aplicaron 61 y 93 LSB. Ambos mueven de tres a cinco
  veces más de lo necesario; el 11 mueve MÁS de los dos y aun así sostiene mejor
  la fase.

- **Una métrica que discrimina, y una formulación más nítida de dónde falla el
  simulador.** Suavizada sobre 300 s, la correlación entre el control aplicado y
  el requerido es **0,992 para el algoritmo 11 y 0,942 para el 13**, con errores
  de seguimiento de 0,70 y 0,81 LSB. `loopsim` calcula ahora esos dos números, y
  sobre el oscilador reproducido del 26.08 da para el algoritmo 11 **0,71 LSB**
  frente a los 0,70 del banco: la reconstrucción de la planta es sólida y la
  métrica es la correcta. Para el algoritmo 13 da 0,20 LSB frente a 0,81. La
  divergencia es específica de ese lazo, no de la planta.

  Calibrar el error correlado del detector del simulador contra el banco no lo
  cierra: el algoritmo 13 encaja con unos 12 ns de deriva del cero (0,96 LSB,
  r 0,944) y el 11 encaja con cero (0,71, r 0,971). No hay ajuste con el que
  ambos sean ciertos. El mismo detector se comporta como si fuera 12 ns más
  ruidoso para un lazo que para el otro.

### Rechazado
- **Aumentar R no lo arregla.** Se barrió el retardo del estimador de R en el
  punto que coincide con el banco: 1 s da 0,96 LSB / r 0,945, 16 s da
  1,01 / 0,939, y 64, 128 y 256 s empeoran monótonamente hasta 1,49 / 0,886.
  Fijar Q más bajo — la otra forma de filtrar más — es aún peor (1,30 LSB con
  1e-7 frente a 1,01 adaptativo); fijarlo más alto es marginalmente mejor. Todas
  las palancas probadas hasta ahora van en la dirección equivocada o no hacen
  nada: el retardo de R, la Q, el horizonte de 50 a 800 s, realimentar el estado
  de envejecimiento y una prueba de blancura sobre las innovaciones. El mecanismo
  es real — el error correlado del detector degrada este lazo cinco veces más
  rápido que al algoritmo 11 — pero inflar R no es su cura, porque un filtro más
  lento sigue peor la deriva real.

  El A/B en UNA noche sigue siendo el experimento que lo resuelve, y hasta que se
  haga, cada cambio más en este lazo es una conjetura.

### Corregido
- **La lectura de CPU en la cabecera del TFT perdía la cola de su `%`, y la
  compilación de 320x240 no funcionaba en absoluto.** Dos cosas distintas,
  encontradas juntas porque ahora existe la misma comprobación para ambas.

  El campo de CPU se centraba en `TFT_W / 2`. El reloj LMT a su lado se ancla a
  la derecha con un relleno de `TFT_S(130)`, y el borrado por relleno de
  TFT_eSPI es un rectángulo de ese ancho que termina en el ancla: 276..471 en el
  panel de 480. `CPU 66%` en FreeSans9pt mide unos 79 px, así que centrado en
  240 su borde derecho cae cerca de 279, tres píxeles dentro de esa banda, y el
  reloj (dibujado después) borra el último glifo. En el panel de 320 la misma
  suma deja alrededor de un píxel, que no es un margen sino una casualidad: una
  lectura de tres cifras también se corta allí. El campo se centra ahora en el
  hueco que los otros dos dejan realmente, con ambos anchos tomados de
  `textWidth()` en vez de supuestos — los paneles ni siquiera usan la misma
  fuente — y se dibuja después del reloj, así que ningún borrado puede
  alcanzarlo sean cuales sean esos anchos. Si el hueco no admite la cadena no
  dibuja nada: un número cortado es peor que ningún número. La constante del
  relleno tiene ahora un solo nombre, `HDR_LMT_PAD`, porque dos copias de un
  número del que depende un margen de un píxel es exactamente cómo ocurrió esto.

- **La compilación del TFT de 320x240 tenía una variable declarada en la rama de
  480 y usada en la de 320** (`phs`, la fase formateada). Probablemente desde que
  esa fila se dividió, y nadie pudo notarlo: **ninguna comprobación compilaba una
  sola línea del código de pantalla.** Todos los interruptores de panel estaban
  apagados en `tools/hostcheck` por falta de la librería, y el bloque de pantalla
  es lo más grande de `gpsdo_tasks.cpp`. Una configuración que nadie puede
  construir no es una configuración, es un rumor.

  `tools/hostcheck/stub/TFT_eSPI.h` es ahora bastante de esa API como para
  compilar contra ella, y hostcheck creció en tres filas: ambos paneles con
  detector, y el pequeño sin él. Catorce configuraciones. No dibuja nada y no
  puede detectar un glifo cortado — eso sólo lo hace el panel — pero sí detecta
  la errata, el tipo equivocado, el datum que no existe y la variable fuera de
  alcance, que es la clase de error que aquí ocurre de verdad.

### Medido
- **El algoritmo 13 en el banco, 11,8 h de noche: el desplazamiento fijo ha
  desaparecido y el lazo es un 27% peor que el algoritmo 11.** Ambos hechos
  importan y el segundo todavía no está explicado.

  Lo que funcionó. El desplazamiento fijo de +18,7 ns de la sesión anterior es
  ahora **+0,06 ns** — la corrección de la contabilidad de la etapa de salida
  hizo exactamente lo que debía. La derivación desde CT/LC imprimió `res 1.01ns
  R0 2.52ns Q0 6.36e-6 P0 1500ns lim 750LSB arm<0.25Hz`, y la R medida se asentó
  en 6,28 frente a una semilla de 6,35: acertada dentro del ruido. **163 rechazos
  de la compuerta de innovación en doce horas** frente a 281 en los cincuenta y
  tres minutos anteriores, y **cero** rearmes del picDIV. La estimación de
  frecuencia promedió +0,0007 ns/s: sin sesgo.

  Lo que no. Sd de fase **5,94 ns frente a los 4,68 ns del algoritmo 11** en una
  noche comparable de 11,0 h en la misma placa — mismo suelo del detector (2,74
  frente a 2,64 ns), misma excursión térmica (2,4 frente a 2,7 °C), así que es el
  lazo y no la habitación. ADEV 1,0e-11 a 1024 s frente a 7,8e-12, y 2,7e-12 a
  4096 s frente a 2,0e-12; idénticos a 1 s y 16 s, de modo que el extremo corto
  está limitado por el detector en ambos y toda la diferencia está en tau medios.

  **El simulador dice lo contrario, por un factor de tres.** Cuatro intentos de
  hacerle decir otra cosa fracasaron: ruido correlado del detector, un TIM2
  realista, el horizonte de 50 a 800 s, y realimentar el estado de
  envejecimiento hacia el control. La pista que nadie ha explicado: durante la
  noche este lazo movió el PWM **57 LSB donde el algoritmo 11 movió 93** en una
  noche comparable. Ésa es la firma de una SUB-corrección, no de perseguir ruido.
  Las dos sesiones fueron noches distintas, así que el siguiente paso honesto es
  A/B en UNA noche — un par de horas de cada uno, alternando — lo que saca el
  entorno de la comparación por completo.

### Corregido
- **Dos modelos del simulador halagaban, y uno era directamente un error.**
  `loopsim` entregaba al firmware la frecuencia exacta, instantánea y sin ruido
  cada segundo. El TIM2 real cuenta ciclos enteros durante un segundo, así que la
  cifra de 1 s es un número ENTERO de hercios, la media de 100 s es la media de
  cien de ésas — de ahí su resolución de 0,01 Hz — y es un promediado rectangular
  que se retrasa cincuenta segundos. Eso no importaba mientras la frecuencia era
  un término menor; importó en cuanto el algoritmo 13 la tomó como medida de
  Kalman. La planta ahora cuenta ciclos enteros y los promedia como lo hace el
  contador.

  El ruido del detector era blanco, y un TIC de rampa leído por un ADC de 12 bits
  no lo es. No es un detalle: cualquier lazo que estime su ruido de medida a
  partir de PRIMERAS DIFERENCIAS — la R del algoritmo 13, la sigma del 12 — mide
  sólo la parte blanca y es ciego al resto por construcción. `LOOPSIM_DNOISE=<ns>`
  añade ahora un error lento estacionario, de modo que la pregunta tiene un
  número por respuesta. Degrada al algoritmo 13 cinco veces más rápido que al 11
  (1,22 → 7,23 ns frente a 7,45 → 10,07 con 8 ns de deriva del cero), que es la
  forma correcta — y todavía no basta para invertir el resultado del banco.

### Rechazado
- **Una prueba de blancura sobre las innovaciones**, escrita para explicar el
  resultado del banco y medida peor en todos los niveles, incluido un detector
  limpio (1,22 → 1,72 ns) y 8 ns de ruido correlado (7,23 → 8,28). El
  razonamiento era de manual: las innovaciones de un filtro óptimo son blancas,
  así que una correlación lag-1 positiva significa que el ruido de MEDIDA está
  correlado y la respuesta correcta es inflar R en vez de ensanchar Q. El fallo:
  las innovaciones de este filtro nunca iban a ser blancas — su control escribe
  su propio estado de frecuencia cada segundo — así que la prueba dispara por
  razones ajenas al detector y la inflación sólo vuelve lento el lazo. No está en
  el árbol; documentada en su sitio para que no se vuelva a proponer.

### Añadido
- **Carga de CPU por tarea, medida con el contador de ciclos y con una ventana
  real de 100 s.** `SW` la imprime una vez, de mayor a menor, junto a las marcas
  de pila; `TL 1` pone los mismos números en la línea de telemetría y `TL 0` los
  quita. No se guarda, y queda apagada tras cada reinicio: es un diagnóstico de
  banco que cuesta una línea de telemetría por segundo, y un ajuste que
  sobrevive a un reinicio es uno que nadie recuerda haber encendido.

  No se muestrea. FreeRTOS llama a `traceTASK_SWITCHED_IN()` en cada cambio de
  contexto y el Cortex-M4 tiene un contador de ciclos libre en el bloque DWT, de
  modo que el intervalo entre dos cambios se conoce exactamente y pertenece,
  exactamente, a la tarea que estaba corriendo — unos pocos ciclos por cambio y
  ningún temporizador consumido. Un perfilador por muestreo desde el tick era la
  alternativa obvia y habría sido ciego a cualquier tarea que empiece en un
  límite de tick y acabe antes del siguiente, que en este firmware son casi
  todas.

  La ventana son cien cubos de un segundo, no una media exponencial: pedida una
  media de 100 s, una EWMA con constante de 100 s todavía arrastra una quinta
  parte de su peso de hace cinco minutos. Las cuotas son sobre el total conmutado
  de cada segundo, así que en la aritmética no entra ninguna frecuencia de reloj
  y las columnas suman 100. Cada cubo se cierra en una sección crítica que
  además carga el trozo de la tarea que corre en ese instante — sin eso la tarea
  ociosa, que suele retener el procesador la mayor parte de un segundo sin
  interrupción, no aportaría nada al cubo que dominaba.

  Conviene decir qué se atribuye a qué: el tiempo de interrupción recae sobre la
  tarea interrumpida, porque una ISR no cambia de contexto. La lectura es "el
  procesador pasó este tiempo con esta tarea como actual", que es honesto y no
  es exactamente "esta tarea consumió esto".

### Cambiado
- **La cifra global de CPU sale ahora de la misma contabilidad: 100% menos la
  cuota de la tarea ociosa.** Antes era un contador de vueltas en el gancho de
  reposo, tomando como 0% de carga la cuenta más alta jamás vista — lo que
  funciona y tiene un defecto que no puede medirse desde dentro: una placa que
  nunca ha estado cerca del reposo tiene una referencia infravalorada y por tanto
  una lectura optimista, para siempre. El contador de ciclos no tiene ninguna
  referencia en la que equivocarse. El gancho de reposo y `configUSE_IDLE_HOOK`
  desaparecen con él.

  Tampoco se cadencia ya desde la línea de telemetría, lo que significaba que TAB
  (pausar telemetría) pausaba también, en silencio, la medida de carga. Ahora la
  mueve la tarea de uptime, y avanza por milisegundos transcurridos en vez de por
  ser llamada exactamente una vez por segundo, así que una llamada omitida o
  duplicada no acorta ni alarga un cubo.

### Cambiado
- **El algoritmo 13 toma su escala de CT y LC en vez de una sola placa.** Salió
  con tres números medidos en un banco — R sembrado en (2,64 ns)^2, Q en 2e-6 y
  una covarianza inicial de fase de (100 ns)^2 — y en cualquier otro OCXO o
  detector son sencillamente falsos. Una placa con un detector de 300 ns
  empezaría con una previa cuatro veces más ancha que toda su banda; una de
  10 000 ns, mucho más estrecha que la verdad, rechazando lecturas buenas durante
  sus primeros diez minutos. La sesión del 27.08 muestra el segundo fallo en la
  misma placa de la que salieron las constantes: **281 rechazos y 500 s** para
  enganchar desde 1300 ns.

  En `kf_scale()` ya no hay ninguna constante. CT da las cuentas que anulan un
  nanosegundo en un segundo; LC da los ns por voltio y el rango útil; la pieza
  fija el ADC en 12 bits sobre 3,3 V, así que el cuanto propio del detector es
  `ns_per_volt * 3,3/4096` — 1,01 ns en esta placa, frente a un ruido medido de
  2,6 ns, que son dos cuantos y medio y de ahí sale la semilla de R. A partir de
  ahí:

  | era | ahora | en esta placa |
  |---|---|---|
  | semilla R (2,64 ns)^2 | (2,5 cuantos)^2 | 6,4 ns^2 (medido 6,6) |
  | suelo R 0,25 ns^2 | un cuanto al cuadrado | 1,02 ns^2 |
  | semilla Q 2e-6 | R / KT^3 | 6,6e-6 (medido 2e-6) |
  | P00 (100 ns)^2 | (media banda)^2 | (1500 ns)^2 |
  | P11 (1 ns/s)^2 | (banda cruzada en un horizonte)^2 | (15 ns/s)^2 |
  | Q de envejecimiento 1e-12 | Q / (100 KT^2) | 6,6e-12 |
  | compuerta de rearme 0,5 Hz | banda / (2 x 100 x espera) | 0,25 Hz |
  | suelo de confianza 8 ns | dos cuantos | 2,0 ns |

  Se recalcula cada segundo en vez de guardarse, así que volver a ejecutar CT o
  LC surte efecto sin reiniciar el lazo, y se imprime una vez como una línea
  `KAL: from CT/LC ...` al arrancar el filtro: una constante derivada que nadie
  puede leer es una constante que nadie puede comprobar. La semilla de Q merece
  una nota: no hay medida del paseo aleatorio de un oscilador en CT ni en LC,
  pero el filtro adapta Q de sus propias innovaciones en minutos, así que la
  semilla sólo tiene que fijar un ancho de banda sensato para esos minutos.
  `Q = R/KT^3` tiene unidades de (ns/s)^2 por segundo exactamente y cae dentro de
  un factor tres de lo que midió la sesión nocturna — dos caminos sin relación al
  mismo número, que es todo lo que se le puede pedir a una semilla.

  Medido, cinco semillas: sd de fase **1,19 -> 0,81 ns** en la planta algo-12 del
  26.08 y 1,24 -> 1,20 en la del algo-11.

### Corregido
- **El lazo apuntaba correcciones que el pin nunca recibió, y eso es el
  desplazamiento fijo de fase del registro del 27.08.** +18,7 ns mantenidos
  durante treinta y ocho minutos con el PWM inmóvil y el filtro informando de una
  rampa de -0,19 ns/s que no estaba aplicando. El recorte estaba contabilizado;
  el REDONDEO no.

  Una vez la fase está en casa, las correcciones son una fracción de LSB.
  Apuntar el `du` pedido en el estado de frecuencia le dice al filtro que ya está
  rampando a `-x0/T`; al segundo siguiente el control calcula `-(x1 + x0/T) = 0`
  y no pide nada. Si esa fracción nunca llegó al pin, el filtro está ahora seguro
  de que corrige un error de fase que nada corrige — y el lazo se aparca, en
  cualquier desplazamiento, para siempre, porque el estado que lo notaría lo
  escribe el control en vez de estimarlo de los datos.

  El arreglo no es redondear con más cuidado. Es apuntar la **diferencia entre
  los valores del DAC**, que cubre el recorte, el redondeo y la vía sub-LSB a la
  vez y que tampoco puede engañar una etapa de salida futura, y arrastrar el
  resto al segundo siguiente para que una petición sub-LSB se retrase en lugar de
  perderse — un sigma-delta de un segundo, de modo que una placa sin la vía fina
  vuelve a pedir hasta que se mueve una cuenta entera. La misma lección que el
  cruce por cero del algoritmo 12, una capa más abajo: un estado actualizado con
  un control no aplicado es un estado que miente.

  Simulado en una placa de 16 bits (`LOOPSIM_FINE=0`), desplazamiento fijo de
  fase: +0,29 -> +0,02 ns en una planta tranquila, +0,16 -> +0,04 en la del
  26.08, con sd 0,65 -> 0,55.

- **El veredicto sobre la confianza en el detector podía quedarse trabado, y se
  trabó.** Dos fallos, ambos hallados midiendo y no leyendo. Primero, la prueba
  se ejecutaba contra la EMA de frecuencia de 1 s cuando la media de 100 s aún no
  estaba; esa EMA se retrasa cincuenta segundos, así que durante un enganche dice
  que la fase debería moverse a un ritmo que era cierto hace un minuto y condena
  a un detector perfectamente sano. Ahora sólo corre con `have100`. Segundo, el
  veredicto no podía revisarse: un detector sin confianza deja el lazo sobre
  TIM2, que mantiene bien la frecuencia, así que no se espera que nada se mueva,
  ninguna ventana concluye y la condena queda para siempre sobre pruebas
  caducadas. Treinta minutos sin una sola ventana concluyente devuelven ahora el
  beneficio de la duda — y el puente de rearme ya no borra ese reloj, lo que
  recreaba el mismo bloqueo a través de la espera de 600 s. Un detector ya
  cazado se condena con una ventana fallida en vez de tres, lo que reduce a la
  mitad el coste de la revisión periódica en uno realmente congelado.

  Con el detector contra el raíl modelado como es debido (`LOOPSIM_RAIL` estaba
  en 3,27 V, que está DENTRO de la banda para un LRN de 3000 — era una prueba de
  detector congelado con el nombre equivocado), la recuperación es ahora un
  rearme en t+106 s seguido de una sesión indistinguible de una sana: sd de fase
  0,55 ns, frecuencia 0,0001 Hz. Un detector congelado de forma permanente:
  frecuencia mantenida a 0,0002 Hz.

### Añadido
- **Una identidad de compilación que no puede quedar obsoleta: un CRC-32 de la
  imagen de flash, calculado al arrancar desde la propia flash.** El banner y el
  comando `V` lo imprimen junto a la hora de compilación.

  La marca de compilación se creía, y el 26.08 mintió: dos capturas de dos
  compilaciones distintas llevaban la misma, y se fue una hora discutiendo con
  un registro que tenía razón. Nadie hizo nada mal. `__DATE__` queda grabado en
  la unidad de compilación que lo menciona, que es el sketch, y el compilador de
  Arduino no recompila una unidad cuyas fuentes no han cambiado. Editas
  `GPSDO_algorithms.cpp`, subes, y el objeto del sketch se reutiliza con la marca
  de la semana pasada dentro. Esa marca es honesta sobre cuándo se compiló el
  SKETCH y no dice nada del resto del firmware.

  Un CRC de la imagen no tiene ese problema: nada lo calcula hasta que la placa
  arranca, así que no puede salir de una caché, y cambia si cambia un solo byte
  de cualquier unidad de compilación. Dos placas con el mismo binario imprimen el
  mismo número. Cuando un registro y un recuerdo no coinciden, ése es el que vale.

  Cubre la tabla de vectores, el código, los datos de sólo lectura y los
  inicializadores de `.data` — cada byte que escribió el programador — acotado
  por `_sidata`, `_sdata` y `_edata` del enlazador. Son referencias **débiles**:
  un toolchain que los llame de otro modo informa "unavailable" en vez de fallar
  al enlazar, porque una identidad ausente es una molestia y un firmware que no
  enlaza es una avería. Unos 2,5 ms una vez al arrancar y 64 bytes de tabla.

- **`build_id.h` y `tools/bumpbuild.py`, para que la marca de tiempo también sea
  fresca.** El sketch incluye `build_id.h` sólo por su existencia: tocarlo es lo
  que obliga al compilador a rehacer el sketch, y sólo el sketch — una fracción
  de segundo, frente a la reconstrucción completa que el mismo truco costaría vía
  `build_opt.h`, donde cambiar una bandera invalida todo. `bumpbuild.py`
  incrementa el número; cualquier edición hace lo mismo, porque el compilador se
  fija en el archivo, no en su contenido. El número sale en el banner. Si nunca
  se incrementa no se rompe nada ni miente nada: la garantía es el CRC, esto es
  la comodidad.

### Cambiado
- **El sintonizador dibuja para el algoritmo 13 la estimación de fase en lugar de
  una serie que nunca envía.** El algoritmo 13 caía en la familia PID, así que el
  panel superior se titulaba "Learned drift (LSB) — LRN feed-forward" sobre una
  gráfica vacía. Ahora muestra la estimación de fase del filtro, con Vphase y sus
  guías de banda debajo, porque la pregunta que este lazo plantea más a menudo es
  si el detector está vivo. Las guías siguen ahora al panel que muestra Vphase en
  lugar de a una familia fijada en el código. `ARM` se une a las palabras de
  tendencia explicadas, y la línea Learn lleva `arm=` una vez rearmado el divisor.

### Corregido
- **El algoritmo 13 no tenía ningún puente hacia LTIC, y con el detector contra
  el raíl dejaba sencillamente que el oscilador se fuera.** Reportado desde el
  banco como la frecuencia subiendo sin parar bajo el algoritmo 13. La causa es
  estructural, no sutil: el filtro tenía UNA medida, la fase. Con el picDIV sin
  sincronizar no hay fase válida, así que no tenía medida alguna — predecía desde
  un estado todavía en cero, no aplicaba nada, y reportaba HOLD mientras el OCXO
  derivaba. Los algoritmos 11 y 12 tienen la frecuencia de TIM2 exactamente por
  esto, y el 12 lo dice con todas las letras: mientras el detector de fase está
  ciego, la frecuencia sigue leyendo verdad. Peor: lo único que podía repararlo
  —la vigilancia que rearma el picDIV— borraba su propio contador siempre que la
  fase era inválida, que es el caso para el que existía.

  **TIM2 es ahora la segunda medida del filtro**, como una actualización escalar
  más sobre un estado que ya lleva: sin inversión de matrices, sin conceptos
  nuevos, unas treinta multiplicaciones-acumulaciones adicionales. El holdover,
  el enganche desde muy lejos en frecuencia y un detector muerto dejan de ser
  casos especiales.

  **Y es lo que hace verificable al detector.** Fase y frecuencia son la misma
  magnitud derivada, así que en una ventana la fase TIENE que moverse lo que suma
  el error de frecuencia. Un detector que no se mueve cuando TIM2 dice que debe
  no está midiendo nada — que es el fallo del 26.08 a las 21:47, donde 3,116 V
  clavados se leían como un +1295 ns perfectamente válido que nunca cambiaba.
  Un filtro solo no tiene defensa: una lectura constante es una lectura
  consistente, las innovaciones se van a cero y le cree MÁS cuanto más miente.
  Esta prueba es la vigilancia del algoritmo 12 sin la conjetura — aquella
  predecía el movimiento a partir de la pendiente que ella misma acababa de
  ordenar, que es el lazo corrigiéndose los deberes; ésta compara contra un
  segundo instrumento. Un detector sin confianza se trata igual que uno contra el
  raíl, y la confianza NO se devuelve al rearmar: hacerlo readmitía un detector
  ya probado muerto y costaba 1,35 Hz en simulación donde seguir ciego cuesta
  0,01.

  El puente de rearme del picDIV es el del algoritmo 12, con su compuerta y su
  espera — rearmar sólo cuando la frecuencia está cerca, porque un rearme deja la
  fase en un desplazamiento cuantizado y con la frecuencia aún fuera vuelve al
  raíl en segundos. Al rearmar, el filtro ensancha su creencia sobre la FASE en
  vez de reiniciarse: nada de lo aprendido sobre frecuencia y envejecimiento pasó
  por el divisor, y eso es lo caro de reaprender. El algoritmo 12 tiene que tirar
  aquí todo su acumulador; esto es lo que compra llevar una covarianza.

  Medido en `tools/loopsim` contra el oscilador reproducido del registro del
  26.08, con el detector fallando como falla el hardware (el nuevo `LOOPSIM_RAIL`
  y el ya existente `LOOPSIM_STUCK`), antes y después:

  ```
                                       ANTES       DESPUÉS
    picDIV sin sincronizar            -5,06 Hz    -0,0002 Hz (1 rearme)
    congelado en +1295 ns, 300 LSB    -4,26 Hz    +0,0004 Hz
    congelado en +1295 ns, en frec.   -4,17 Hz    -0,0005 Hz
    detector sano, 1500 ns fuera      sd 209,80   sd 209,77 ns
    detector sano (5 semillas)        sd  1,23    sd  1,19 ns
  ```

  La calidad del lazo con un detector sano no cambia, que es justo el punto: nada
  de esto está en la ley de control.

- **El canal de frecuencia del simulador tenía el signo cambiado**, y pasó
  inadvertido porque nada usaba los dos canales a la vez hasta que este filtro
  tomó la frecuencia como medida. `loopsim` accionaba el detector y TIM2 desde el
  mismo error de control con la frecuencia BAJANDO al subir el PWM, lo que hace
  que la pendiente de fase y el error de frecuencia tengan el mismo signo; en esta
  placa son opuestos. Dos lazos que funcionan en el banco lo dicen de forma
  independiente — el algoritmo 11 baja el PWM cuando el oscilador se lee rápido, y
  el ajuste TIM2 del algoritmo 12 llevó `f100` a casa con pasos negativos en la
  tormenta del 16.08 — y ambos requieren que la frecuencia suba con el PWM,
  mientras que la reconstrucción de fase la tiene bajando. Ninguna medida anterior
  se mueve: el término TIM2 del algoritmo 12 está limitado muy por encima de los
  errores de frecuencia que producen estas plantas y nunca disparó.

- **El sketch no compilaba, y once configuraciones limpias no decían nada al
  respecto.** Dos errores llegaron juntos al IDE: `vApplicationIdleHook()`
  llevaba `extern "C"` en la línea anterior a la función en lugar de en la
  misma, y `setup()` llamaba a `kf_store_load()` sin la cabecera que lo declara.

  El primero merece explicación, porque no es evidente. El compilador de Arduino
  inserta un prototipo C++ para cada función que define un sketch, y su pasada
  de ctags va POR LÍNEAS: un `extern "C"` en la línea anterior no se ve, el
  prototipo se genera igualmente, y la definición de debajo — que sí tiene
  enlace C — entra en conflicto con él. FreeRTOS no declara el hook (lo llama
  desde C), así que el prototipo generado es la primera declaración y el
  compilador no tiene nada que anteponerle.

  `tools/hostcheck` no veía ninguno de los dos: compilaba todos los `.cpp` en
  once configuraciones y nunca el `.ino`. **Ahora sí**:
  `tools/hostcheck/ino2cpp.py` reproduce las dos cosas que el compilador de
  Arduino le hace a un sketch — anteponer `<Arduino.h>` e insertar los
  prototipos por líneas — y el sketch se compila y enlaza junto al resto en cada
  configuración. Deshacer cualquiera de las dos correcciones hace fallar la
  ejecución con el mismo texto que dio el IDE.

- **El algoritmo 12 era tres veces peor de lo necesario, y la causa fue una sola
  entrada en la CLI.** La sesión del 26.08: 2,25 h en el algoritmo 12 con una sd
  de fase de 41 ns, en un ciclo límite de ±70 ns y periodo de 2,3 h, frente a los
  3,0 ns del algoritmo 11 en la misma placa la misma tarde. El suelo de ruido del
  detector fue idéntico en ambos tramos — 2,47 contra 2,45 ns — así que la
  diferencia era enteramente del lazo, que es justo lo que la métrica de
  estructura lenta existe para decir.

  `MG` estaba puesto a **2,130 LSB/ns**. Ese es el `LG` del algoritmo 11, su
  propia ganancia de VCO; el `MG` del algoritmo 12 son las cuentas necesarias
  para anular un nanosegundo de fase en un segundo, y CT había medido **31,3**.
  Ambos se imprimen como «LSB per ns» y no son la misma magnitud. Cada corrección
  era por tanto **14,7 veces demasiado pequeña** — visible en el registro como
  trece correcciones que movieron el PWM cuatro cuentas en total mientras el
  control que el oscilador necesitaba se desplazaba 3,3.

  La misma entrada hizo además algo que nadie pidió. `MF 0` significaba «seguir a
  MG», así que teclear una ganancia cambió también la tabla de límites, de la
  fórmula de ruido a la tabla almacenada, cuyo límite en el nivel 6 es ~126 ns.
  El lazo corregía entonces débilmente Y demasiado tarde.

  Tres cambios, cada uno medido en vez de argumentado:

  - **La tabla de límites ya no sigue a la ganancia.** `MF 0` es ahora la fórmula
    de ruido, diga lo que diga `MG`; la tabla `MLP` editada a mano se pide con
    `MF 1`. Que la soldadura entre ambas era errónea ya se argumentó al
    introducir `MF` — la ganancia pertenece al oscilador, los límites al ruido de
    fase del emplazamiento — y solo la compatibilidad la mantenía. Esa
    compatibilidad es lo que ha costado esto.
  - **La prueba de cruce por cero se arma en ambas vías de corrección.** Se
    armaba solo en la vía del límite, con el argumento de que una corrección
    programada «no tiene un desplazamiento conocido que cancelar». Unas líneas
    antes ambas vías calculan el mismo `slew_lsb = -(p_ns/span)*lsb_per_ns` y
    ambas lo guardan, así que ese argumento describe el lazo de Alan, no este.
  - **La placa avisa cuando un `MG` puesto a mano no puede ser una decisión de
    ajuste.** Más allá de un factor cuatro respecto a lo que midió CT es un error
    de tecleo, no un ajuste. El valor se sigue usando — un experimento deliberado
    tiene que seguir siendo posible — pero tanto `MG` como el propio lazo, en su
    primer uso tras recuperar los ajustes, imprimen al lado la cifra medida. La
    vía de recuperación importa: es la que nadie prueba.

  Lo midió la nueva herramienta `tools/loopsim/`, la tercera de su especie:
  `hostcheck` pregunta si el árbol compila, `algoswitch` si un lazo arranca,
  `loopsim` si mantiene la fase. Reproduce un oscilador reconstruido de un
  registro en vez de uno inventado para la ocasión — dado el PWM aplicado y la
  fase reportada, el control que el oscilador necesitaba cada segundo sale por
  álgebra — y compila el algoritmo real dos veces, con y sin un cambio, de modo
  que comparar las columnas es comparar el cambio y nada más. Sd de fase, media
  de cinco semillas de ruido:

  ```
                          ANTES    DESPUÉS  algo 11
    ventana algo-12 26.08
      MG 0  (derivada)     3,57     2,95     3,64
      MG 2,130 (la puesta)53,81    21,06
      MG 31,3 (de CT)      6,63     2,95
    ventana algo-11 26.08
      MG 0  (derivada)     4,62     4,01     7,39
      MG 2,130 (la puesta)66,04    61,96
      MG 31,3 (de CT)     16,41     4,01
  ```

  Con la ganancia correcta el algoritmo 12 mantiene ahora **mejor que el
  algoritmo 11** en ambas ventanas — 2,95 contra 3,64 en la corta y 4,01 contra
  7,39 en la larga, que lleva cuatro veces más deriva.

  **Se escribieron dos reparaciones y se descartaron; ambas quedan anotadas en su
  sitio.** La prueba de cruce por cero no se disparó ni una vez en aquel registro
  porque la fase tardaba de 810 a 3283 segundos en cambiar de signo tras cada
  corrección, contra una ventana de renuncia de 300 s; todos los retornos la
  sobrevivieron. Sustituir esa ventana por «mantén el armado mientras la fase
  siga volviendo» es la reparación evidente y mide *peor* en la planta larga
  (4,01 → 6,76 ns), porque en una placa con deriva una fase que se acerca a cero
  es a menudo el oscilador y no el desplazamiento de esta corrección. Los
  retornos largos eran el síntoma del error de ganancia de 14,7×, no un fallo de
  la constante. Una segunda salvaguarda — armar solo cuando la fase extrapolada
  del acumulador coincide en signo con la fase en la salida — midió cero
  beneficio y tampoco está en el árbol. Una reparación descartada que no deja
  rastro se vuelve a proponer.

  Instalaciones existentes: si `MG` no es cero, compárelo con la cifra de `CT` —
  `ML` imprime ambas — y ponga `MG 0` en caso de duda. Si editó a mano los
  límites `MLP`, añada `MF 1` para conservarlos y luego `ES ALGO12`.
- **La prueba de cruce por cero devolvía un desplazamiento que nunca llegó a la
  salida.** Esta es la que hacía inservible el algoritmo 12 con la fase lejos de
  cero, y el registro del 26.08 22:41 la recoge limpiamente: después de que el
  vigilante de estancamiento resincronizara el divisor, la fase volvió
  honestamente, llegó a −50 ns, y el lazo lanzó entonces el PWM 1038 cuentas en
  un segundo. Pasó los diecisiete minutos siguientes recorriendo toda la banda
  del detector — PWM de 40348 a 41410, fase de −1600 a +20 ns, cinco
  resincronizaciones — sin entrar ni una vez en ±200 ns.

  Una corrección calcula un desplazamiento deliberado y luego limita el total
  contra la banda del detector. El cruce retira después ese desplazamiento, para
  que el oscilador quede con la frecuencia correcta Y sin error de fase. Pero el
  valor retirado era el desplazamiento CALCULADO, registrado antes del límite — y
  cuando el límite actúa, que es justo cuando la fase está lejos, ese es otro
  número. A −1500 ns en esta placa el desplazamiento calculado es de 734 LSB y
  solo 500 llegan a la salida; el cruce devuelve 734, y los 234 sobrantes son un
  error de frecuencia nuevo apuntando al otro lado. La fase sale en dirección
  contraria, choca con el límite en el raíl opuesto, y el lazo recorre la banda
  indefinidamente.

  Ahora se registra lo que sobrevivió al límite: el total limitado menos el
  término de frecuencia, que no es un desplazamiento deliberado y no se cancela.
  Entrada desde un desfase grande, reproducida sobre el oscilador de aquel día,
  media de cinco semillas de ruido:

  ```
     fase inicial       antes         después
        ±500 ns          0,6 ns       1,1 ns
       ±1000 ns          0,7 ns       1,1 ns
       ±1500 ns      4,6e6 ns         1,0 ns
  ```

  Por debajo de unos 1000 ns el límite rara vez actúa y ambas se comportan igual;
  a 1500 la versión antigua diverge siempre y la nueva se asienta por debajo de
  2 ns. El mantenimiento no cambia (3,07 ns frente a los 3,62 del algoritmo 11 en
  la misma planta) y el caso de ganancia puesta a mano también mejora:
  53,8 → 16,7 ns.

- **Un detector que ha dejado de seguir ya se detecta.** Segunda mitad de la
  tarde del 26.08. La placa se reinició directamente en el algoritmo 12 con el
  picDIV sin sincronizar: la rampa se quedó cerca de su raíl superior, con Vphase
  plana en 3,116 V a ±5 mV durante toda la captura de 5,5 minutos. Con `LRN` 3000
  la banda útil es de ±1650 ns, así que esa tensión cae cómodamente DENTRO — el
  detector informaba de unos perfectamente válidos +1295 ns que nunca cambiaban.
  El lazo creía tener fase, así que nunca armó (`arm=0` todo el rato), disparó una
  corrección de nivel 0 que saturó el límite de ±500 LSB, y ahí se quedó.

  **La prueba que se incorpora es una predicción, no un umbral.** El lazo conoce
  el desplazamiento que ordenó, así que sabe cuánto debería viajar la fase en una
  ventana: `|slew_lsb| / lsb_per_ns` nanosegundos por segundo. Eso se compara con
  lo que la fase hizo de verdad — la diferencia de las medias de dos
  semiventanas, cuyo ruido es `sigma*sqrt(2/W)` y no `sigma`, o sea 1,8 ns en vez
  de 7. Cuando la predicción es lo bastante grande para ser medible y la fase
  entrega menos de una cuarta parte, tres ventanas de 32 s seguidas, la lectura
  ya no está conectada al oscilador. Entonces se arma el picDIV y se dice por
  consola. Reproducido sobre el oscilador de aquel día con el detector congelado
  en +1320 ns, se dispara a los 229 s.

  **Se escribieron tres versiones y dos no están en el árbol.** La primera
  comparaba muestras sueltas contra una referencia y reiniciaba con cualquier
  excursión de cuatro sigmas — algo que el ruido por segundo hace casi cada
  segundo, así que nunca pasó de uno y nunca se disparó en la placa para la que
  se escribió. La segunda añadía una puerta que se negaba a actuar sobre una fase
  que aún no se había demostrado en movimiento, y se bloqueó: la puerta impedía
  la corrección cuyo trabajo era moverla. Diagnosticar, no restringir.

  **También se probó y se descartó armar en la entrada nueva**, siguiendo la
  pregunta de arranque del algoritmo 10. Se dispara cuando el detector está BIEN
  y la fase simplemente está lejos, y entonces el armado tira una medida buena:
  el divisor se resincroniza a un desfase cuantizado de unos cientos de
  nanosegundos. Además es innecesario: reproducido desde +1295 ns con detector
  sano y un error real de frecuencia, el algoritmo 12 entra solo todas las veces,
  de 17 a 19 cruces por cero, de vuelta dentro de ±17 ns, sin armar ni una vez.
  Armar no es gratis, «la fase está lejos» no es prueba de que el divisor se haya
  perdido, y el lazo no necesita ayuda mientras el detector diga la verdad.

- **El acumulador se descarta cada vez que se arma el picDIV.** Armar
  resincroniza el divisor con el flanco de 1PPS, así que toda fase ya presente en
  la jerarquía se midió contra una alineación que ya no existe; conservarlas
  mezcla dos ceros distintos. Simulado, la primera corrección tras un armado
  salió a −436 LSB desde una prueba de nivel 3 que era mitad anterior y mitad
  posterior al salto. El `s_mla_post_arm` existente no cubre esto — mantiene el
  transitorio de aterrizaje FUERA del acumulador, pero el acumulador ya estaba
  lleno.
- **Cambiar de algoritmo ya reinicia el lazo al que se cambia.** Ninguno lo
  hacía, y a ninguno se le había pedido: cada lazo guarda su estado en estáticas
  de función, y una estática no sabe que el operador ha escrito `LA 12`.
  Reportado en el banco — algoritmo 11 en LOCK, cambio a 12, y el 12 se quedaba
  en nivel 0 con cero correcciones y el picDIV sin armar.

  Tres mecanismos distintos, una sola causa. El **algoritmo 12** quedaba con
  `s_mla_returning` puesta, la bandera que una corrección levanta mientras su
  desplazamiento deliberado devuelve la fase a cero; esa bandera bloquea AMBAS
  vías de corrección (la prueba de límite por nivel y la planificación `MR`) y
  el temporizador de 300 s que la libera solo avanza mientras el 12 es el lazo
  en marcha — así que salir a mitad del desplazamiento y volver suprimía toda
  corrección hasta que ese temporizador expiraba, con `level=0 corr=0` en la
  telemetría y nada que lo explicara. Su jerarquía de acumuladores conservaba
  además sumas de fase anteriores al cambio, de modo que la primera corrección
  que sí disparaba actuaba sobre pruebas de hacía minutos. El **algoritmo 10**
  mantiene `integ` como objetivo ABSOLUTO de PWM, es decir una tensión de
  control elegida para condiciones quizá de hace horas, y con `prev_state`
  marcando todavía LOCK la transición de entrada nunca ocurre — así que
  `ltic_autotune()` no se ejecuta y el picDIV no se arma nunca. Los
  **algoritmos 3–9** arrastraban sus integrales PID, lo que en la primera
  actualización tras el cambio es un escalón de corrección que nadie pidió.

  El gancho vive en `adjustVctlPWM()` y no en el manejador de `LA`, porque es la
  única vía por la que pasa todo cambio: la CLI, la recuperación de ajustes al
  arrancar y cualquier otra cosa que escriba `gCtrl.active_algo`. Un gancho en
  el comando se habría perdido la recuperación — justo el caso que nadie prueba.
  Cada lazo toma la bandera una vez y limpia su propio estado; los lazos
  antiguos reutilizan el vaciado de búfer circular que ya tenían.

  Lo que SOBREVIVE deliberadamente a un reinicio es todo lo que los lazos han
  MEDIDO de la placa: los LSB por ns del algoritmo 12, su suelo de ruido del
  detector y sus umbrales medidos, y la estimación de ruido del algoritmo 10.
  Eso describe el hardware, no la ejecución anterior, y reconstruirlo costaría
  minutos a ciegas en cada cambio. Los contadores de sesión se borran por la
  razón contraria: «cero correcciones desde el cambio» solo es un hecho legible
  si la cuenta empieza en cero.

  Una cosa que NO era un fallo: que el algoritmo 12 no arme el picDIV al entrar
  desde un algoritmo 11 enganchado. Solo arma cuando el detector está ciego y la
  frecuencia está cerca (`!have_phase && |f| < 0,5 Hz`); llegando desde un
  enganche la fase es válida, así que no hay nada que armar y mover el divisor
  solo lanzaría la fase a un desfase cuantizado. Ahí `arm=0` es el lazo
  funcionando.

  Verificado antes de enviarlo, no después del siguiente registro. Nueva
  herramienta `tools/algoswitch/run.sh`: compila `GPSDO_algorithms.cpp` para el
  PC dos veces — tal como está y con `algo_take_restart()` forzado a false, que
  es el código de antes — y ejecuta ambas contra una placa simulada
  (319,5 µHz/LSB, detector a 1252 ns/V, LPOL −1). La misma secuencia de cambios,
  saliendo del 12 a mitad del desplazamiento: antes, 30 minutos después de
  volver seguía batiéndose en el nivel 2 con 308 ns de error de fase; después,
  se asienta en el nivel 7 con 4 ns. Entrando en el algoritmo 10 con la fase a
  1200 ns fuera de la rampa: antes, el LOCK persistido se creía y se mantenía;
  después, se vuelve a comprobar, se degrada a ACQ y se rearma el divisor. Un
  enganche centrado se conserva en ambos — se trata de comprobar la afirmación,
  no de estropear una buena.
- **Errata del manual + soporte de receptores clon.** La sección DFU cubre
  ahora las placas WeAct v3.1 actuales, con *botón* BOOT0 en vez de jumper
  (mantenga BOOT0 al conectar el USB y luego suéltelo — la receta
  «mantén y pulsa NRST» que circula por internet no funciona), y la Parte 3
  ganó un párrafo sobre elección de módulo GNSS: los clones chinos de u-blox
  ignoran la configuración binaria y algunos pierden el auto-baud con el
  chorro de tramas sin respuesta, hasta que la nueva sonda tras el túnel `T`
  los rescata (informe de campo: Solder Junkie). Nuevo conmutador de
  compilación `GPSDO_FAKE_UBLOX` (apagado por omisión): solo sonda de baud,
  cero configuración UBX — el lazo no pierde nada esencial, al PPS nunca le
  importó UBX; desaparecen el silenciado de NMEA, el modo estacionario, el
  survey-in/Time Mode y `qErr`.
- **Los algoritmos 11 y 12 ya no adivinan la polaridad del EFC.** Trataban un `LPOL` sin fijar como +1 y actuaban — en una placa con EFC invertido cada corrección empujaba al revés. Ahora esperan con un aviso de una línea hasta fijar `LPOL ±1` (instalaciones 11/12 existentes: fije `LPOL` una vez y `ES LTIC`).
- **La ayuda de `ES` ocultaba el grupo `ALGO12`.** Ambas líneas de ayuda (la
  lista principal de `H` y la página `H TZ`) decían `obj: TZ/PID/LTIC/FLAGS/ALGO/PO`,
  cuando el analizador acepta `ES ALGO12` desde que existe el bloque del
  algoritmo 12 — siguiendo la ayuda, el usuario no descubriría el grupo que
  persiste MG/MR/MF/MFT y la tabla de límites por nivel, mientras las pistas
  `[not saved — run 'ES ALGO12']` señalaban un comando que la propia ayuda no
  reconocía. Ambas líneas ya listan `ALGO12`. En la ayuda del sintonizador
  `ES` ya era correcta, pero `FR 0|1` seguía descrito como interruptor y la
  sección de PID arrastraba la entrada inalcanzable `LRN 0|1|R` — corregido
  para coincidir con el firmware.
- **La escritura no bloqueante del informe silenciaba la telemetría de 1 Hz
  por USB CDC.** El guard added para la falla «enchufas el USB y la pantalla se
  congela» preguntaba `availableForWrite()` una sola vez y descartaba el
  informe ENTERO cuando devolvía menos que su tamaño. Por USB CDC eso es
  todos los informes: la cola TX de `USBSerial` mide
  `USB_FS_MAX_PACKET_SIZE * CDC_TRANSMIT_QUEUE_BUFFER_PACKET_NUMBER` = 64 × 2 =
  **128 bytes** (valores por omisión de stm32duino 2.12.0) y el informe de
  lectura ocupa 400+. El arranque y la CLI seguían funcionando — líneas cortas,
  otra ruta — así que una placa que acababa de registrar sin problemas por
  UART (búfer TX de 512 B desde `build_opt.h`, donde el informe cabía) enmudecía
  al pasar el registro a USB.

  Ahora el informe se escribe POR TROZOS no mayores que el espacio que el
  puerto declara, con un presupuesto de 25 ms: un host que lee se lleva el
  informe entero en pocas iteraciones; un host que no lee pierde el resto
  tras el presupuesto y la pantalla sigue viva. `room == 0` se interpreta como
  «llena, espera», nunca como permiso para escribir a ciegas — con la cola CDC
  llena `USBSerial::write()` gira mientras el host siga conectado, que es
  exactamente la congelación que el guard debe impedir. La cola TX CDC se
  amplía además a 1 KB (`-DCDC_TRANSMIT_QUEUE_BUFFER_PACKET_NUMBER=16` en
  `build_opt.h`; la macro tiene `#ifndef` en la librería USBDevice, verificado
  en 2.12.0), de modo que un host sano tiene ~2,5 s de margen y no se descarta
  nada; la escritura por trozos queda como red de seguridad para un host
  conectado pero sin leer, al que ningún tamaño de búfer salva.
- **El enlazado fallaba con `GPSDO_LTIC` desactivado, incluso tras la corrección
  de cabecera anterior.** `g_freq_damp_win_dpll` y `g_freq_damp_win_lock` — las
  ventanas de amortiguación FA / FAD / FAL — se definían dentro del bloque
  `GPSDO_LTIC` de `gpsdo_tasks.cpp`, pero forman parte del bloque de ajustes
  persistido y la CLI las imprime y las fija incondicionalmente, así que
  `settings_store.cpp` y `gpsdo_cli.cpp` las referencian en cualquier
  configuración. Cuatro bytes de RAM frente a un firmware que no se puede
  compilar sin detector de fase no es un intercambio que merezca la pena; las
  definiciones salen de la guarda.
- **El firmware no compilaba con `GPSDO_LTIC` desactivado.**
  `GPSDO_algorithms.cpp` define el estado, los globales y los accesores del
  algoritmo 12 *fuera* de su propio bloque `#ifdef GPSDO_LTIC`, y
  `gpsdo_cli.cpp` los lee incondicionalmente — pero todas esas declaraciones
  vivían *dentro* del bloque `#ifdef GPSDO_LTIC` de `GPSDO_algorithms.h`.
  Desactivar el detector producía catorce errores «was not declared in this
  scope» desde `GPSDO_algorithms.cpp:1437` y todo el grupo `ML`/`MLP`/`MG`/`MF`
  de la CLI. Aparte, el `#endif` que cerraba la guarda alrededor de
  `multi_level_accum()` estaba una línea *por encima* de la llave de cierre de
  esa función, así que con la guarda desactivada la llave quedaba huérfana.
  Nadie se había topado con ello, porque todas las placas de este diseño llevan
  el detector — el primero en compilar sin uno fue Dave (Solder_Junkie) en
  EEVblog, cuya placa con M8N no tiene etapa de entrada TIC. Una declaración no
  cuesta nada cuando la definición no está, así que la guarda cubre ahora las
  dos funciones que realmente necesitan el hardware y nada más. Verificado en
  ambos sentidos: el árbol compila y enlaza limpio con `GPSDO_LTIC` activado y
  desactivado, y ningún símbolo definido sólo bajo la guarda se referencia
  desde fuera de ella.
- **Una escritura bloqueante en el CDC USB congelaba la pantalla.**
  `vDisplayTask` gobierna el OLED, el LCD, el TM1637 y el TFT *y además* escribe
  el informe de telemetría de 1 Hz, y `USBSerial::write()` del núcleo STM32duino
  gira en el sitio mientras el endpoint está ocupado durante todo el tiempo que
  el host siga conectado. Un host que había enumerado el puerto pero no lo
  vaciaba detenía por tanto esa tarea en seco, dentro de la escritura, al otro
  lado del tiempo límite de 30 ms del mutex de serie — así que el síntoma
  visible era una pantalla congelada, que se lee como una placa colgada y no lo
  era en absoluto: las tareas de frecuencia y control no tocan el puerto serie y
  siguieron disciplinando el oscilador todo el tiempo. No había ni una sola
  guarda `availableForWrite()` en todo el firmware. El informe pide ahora sitio
  primero y descarta la línea entera si no cabe; la telemetría es un flujo en
  vivo, no un registro, y el siguiente informe llega en un segundo. `TeeSerial`
  ha recibido su propio `availableForWrite()`, que devuelve el menor de sus dos
  puertos, porque el que hereda de `Stream` devuelve 0 y silenciaría por
  completo una compilación `GPSDO_BLUETOOTH_PARALLEL`. Informado por Dave
  (Solder_Junkie) en EEVblog.
- **El uptime se contaba con un temporizador libre del MCU, en un reloj
  disciplinado por GPS.** Medido sobre trece capturas de dos placas, de 1 a 21
  horas cada una: el uptime impreso ganaba sobre el UTC impreso entre +129 y
  +169 ppm, mejor estimación **+159 ppm** de los tres registros más largos
  (+12 s en 20,8 h, +13 s en 22,9 h, +7 s en 11,9 h). Ambas placas coinciden,
  así que no es tolerancia del cristal: el tic de 2 Hz que movía el contador
  simplemente no es de 2 Hz.

  Ese error de ritmo producía también el jitter de ±1 s, que es lo primero que
  se nota: el informe se imprime con el PPS y el contador avanzaba con TIM9, así
  que los dos flancos se deslizaban uno sobre otro cada ~6530 s (= 1/159 ppm) y
  cada cruce lanzaba una ráfaga de segundos repetidos y saltados. Las ráfagas del
  registro del 19.08 empiezan en 325, 6854, 13383 y 19907 s — separación 6529,
  6529, 6524.

  El uptime lo avanza ahora el PPS validado, en el mismo punto donde se
  incrementa `ppscount` — que es además el evento que dispara el informe, de
  modo que el valor impreso no puede ser un batido entre dos relojes.
  `vUptimeTask` mantiene el reloj sólo en holdover, tras más de 1,5 s de
  silencio del PPS. Simulado contra un cristal 159 ppm rápido: exactamente
  86 400 s en 24 h enganchado, exactamente 3600 en una hora de holdover puro, y
  error cero a lo largo de 20 ciclos de caída y de una hora con el 2 % de los
  pulsos ausentes (`tools/uptime_test.cpp`).

- **Dos pérdidas silenciosas en la ruta antigua del uptime.** Tomaba el mutex
  del uptime con un tiempo límite de 5 ms y hacía `continue` al fallar,
  descartando ese segundo sin dejar constancia de que se debía; y contaba los
  tics de medio segundo por paridad, que un `give` perdido de un semáforo
  binario invierte. Ninguna de las dos existe ya: no hay mutex, y lo que se
  cuenta son milisegundos transcurridos, de modo que un tic perdido, tardío o
  duplicado salen igual, y una tarea bloqueada un minuto recupera el minuto.

- **El tic de corrección de LOCK usaba `ppscount % period` con un periodo que
  podía cambiar.** La forma con módulo sólo es correcta con periodo fijo, y la
  cota de cadencia anterior lo convierte en cualquier cosa menos eso: con
  `LIV 300` y una cota de ~110 s el tic caía en múltiplos de lo que `period`
  fuese ese segundo, lo que en una simulación de 6 h dio intervalos de 7 s a
  551 s y una MEDIANA de 300 — la cota calculaba el número correcto cada segundo
  y casi nunca llegaba a usarlo. LOCK se condiciona ahora al tiempo transcurrido;
  ACQ y DPLL conservan el módulo, con sus periodos fijos. Confirmado en
  hardware: todos los intervalos entre correcciones del ensayo del 21.08 son
  múltiplos exactos del ajuste de 30 s, salvo un intervalo de 12 s durante el
  asentamiento en el que la cota actuó legítimamente.

- **El límite de paso de LOCK es por CORRECCIÓN, no por segundo.** El límite de
  4 mHz concedía al lazo, a una cadencia de 300 s, la trigésima parte de la
  autoridad que tenía a 30 s, con la misma deriva que cancelar; el integrador se
  quedaba contra el límite corrección tras corrección y sobrepasaba al alcanzarlo
  (RMS de fase simulado 174 ns con `LIV 300`, frente a 34 con esto corregido). El
  límite tiene ahora un suelo en cuatro veces el paso de frecuencia que la prueba
  de pendiente acaba de calcular, y no cambia en una placa sin pendiente
  resoluble.

- **Ambas longitudes de ventana se daban por supuestas iguales a
  `lock_interval_s`.** No lo son en cuanto la cota de cadencia puede acortar el
  intervalo: en una placa que pide 300 s y corre a 73, la pendiente salía cuatro
  veces pequeña y la extrapolación llegaba cuatro veces demasiado lejos. Ambas
  longitudes se leen ahora del contador de PPS.

- **El giro de ventana estaba dentro de la prueba del par, de modo que LOCK no
  habría hecho absolutamente nada.** Tras el reinicio de muestras en DPLL→LOCK
  la ventana antigua está vacía, así que la prueba no corría, así que el giro no
  ocurría, así que la ventana antigua seguía vacía. Sobrevivió a la simulación
  sólo porque el simulador sembraba la primera ventana a mano — una diferencia
  entre modelo y firmware justo donde el modelo existía para descartarla. El
  giro es ahora incondicional en cuanto hay una ventana nueva.

- **El rearme del picDIV entraba directo al PI.** El divisor deja la fase en un
  desplazamiento cuantizado — unos ±3 µs en esta construcción — y ese salto no es
  un error de fase que el lazo deba responder. El algoritmo 12 lo omite desde
  v1.05; el lazo de tres etapas armaba en tres sitios distintos y no omitía nada.
  Los tres apagan ahora el detector durante 5 s.

- **Los dígitos de frecuencia cambiaban de color sólo con `LOCK`**, así que el
  algoritmo 12 — que informa `CORR` y `ZC` — mostraba color de no enganchado
  estando enganchado. La guarda de eco obsoleto era además de ±0,050 Hz frente a
  una media de 10 s cuantizada a 0,1 Hz, así que una sola cuenta legítima se leía
  como eco viejo; ahora es ±0,150.

- **Todas las gráficas del sintonizador estaban comprimidas 4:1 en el eje
  temporal.** Al búfer de tiempo compartido se añadía cada vez que *cualquier*
  campo se extraía de una línea — cuatro de las seis líneas de un bloque de
  telemetría — mientras que cada serie individual recibía una muestra por
  segundo. Los dos se llenaban por tanto a ritmos distintos, y el código de
  dibujo emparejaba las N muestras más recientes de una serie con las N marcas
  de tiempo más recientes, que cubrían sólo el último cuarto del intervalo que
  ocupaban esas muestras. Medido sobre la captura del 21.08: 1725 segundos de
  telemetría, 6872 muestras de tiempo. El búfer de «30 h» eran 30 h de fase
  contra 7,5 h de reloj, y el indicador de «segundos guardados» mostraba el
  cuádruple de la verdad. El tic es ahora la línea `Up:`, impresa exactamente una
  vez por segundo por todos los algoritmos, y cada serie se rellena hasta él, de
  modo que las longitudes coinciden por construcción y no por casualidad.
- **`HDOP:TIME` se descartaba por no ser analizable.** Un LEA-T que ha
  completado el survey-in informa de su modo de fix en el campo HDOP, y esa
  bandera es el más útil de los dos datos — es el modo en que el 1PPS merece
  confianza. Ahora pasa tal como se escribió.

### Añadido
- **`tools/hostcheck/` — compila y enlaza todas las combinaciones de
  interruptores en un PC.** Tres fallos de compilación seguidos salieron de una
  sola persona compilando sin detector de fase, cada uno ocultando al siguiente,
  y el último era un error de ENLAZADO que ninguna lectura de un solo archivo
  habría atrapado. Esto pasa el compilador del anfitrión por siete
  configuraciones en unos treinta segundos e informa de cualquier símbolo que
  exista en una y no en otra. No sustituye a una compilación de Arduino —los
  stubs sólo llegan a satisfacer los `#include` y el objetivo ARM no se ejercita—
  pero la clase de fallo que atrapa es exactamente la que costó tres idas y
  vueltas.

- **Umbrales por nivel medidos para el algoritmo 12 (`MF`, `MFT`).** La tabla
  `auto` no era adaptativa: sigma se clava en su suelo de 5 ns en cualquier placa
  de este diseño, así que la tabla salía idéntica en todas partes, y su escalado
  por niveles supone ruido de fase blanco (exponente 0,5) donde ambas placas de
  aquí miden 0,95–1,03. `MF 3` sustituye la suposición por una EMA por nivel del
  estadístico de prueba, un ajuste por mínimos cuadrados del exponente y un
  cuantil en el intervalo objetivo que fija `MFT`. `ML` informa de qué fuente
  está en uso, del exponente ajustado y del número de niveles. Verificado contra
  la propia salida `ML` del firmware y contra dos registros de hardware fuera de
  línea; **todavía no en lazo cerrado** — esa medida está pendiente, y en la más
  silenciosa de las dos placas la tabla medida reprodujo PEOR que la supuesta
  (RMS mediano de 13 min de 11,6 ns frente a 5,5), porque las dos tablas hacen
  preguntas distintas.
- **Anonimización de la posición GPS en el sintonizador.** Una casilla, fijada al
  abrir el fichero de captura y deshabilitada mientras se registra, sustituye
  Lat/Lon/Alt en el log capturado y escribe una cabecera de procedencia de dos
  líneas. Se conservan el número de satélites y el HDOP.
- **`tools/lock_sim_algo10.py`** — el simulador de la etapa LOCK del que salen
  las cifras anteriores, para que puedan comprobarse de forma independiente.
- **El sintonizador escribe CSV además del log crudo, y guarda una semana.**
  Nació como herramienta de ajuste y así se presenta, pero se está usando como
  registrador, y un ensayo de estabilidad que termina porque el búfer dio la
  vuelta es un ensayo que hay que repetir. El historial de las gráficas pasa de
  30 h a 604 800 muestras — una semana al ritmo de telemetría de 1 Hz — y un
  desplegable junto a **Start logging** elige *Full log*, *CSV only* o *Both*,
  fijado mientras el archivo esté abierto igual que la casilla de redacción.

  El CSV es una fila por segundo de telemetría y lleva lo que todo análisis de
  estos registros ha necesitado realmente, no todo lo que imprime el firmware:
  `utc, up_s, algo, state, dph_ns, qerr_ns, vphase_v, pwm, f10, f100, ph_ns,
  level, corr, sig_ns, zc, bmp_c, sat, hdop`. Unos 65 MB por semana frente a 217
  del log completo. `Vctl` se omite porque es `pwm` a través de una red RC y
  `pwm` es la cifra exacta; humedad, presión y los raíles del INA se omiten
  porque en todas las capturas hasta ahora nunca se han movido lo bastante como
  para explicar nada. No hay columnas de posición en absoluto, así que un CSV
  está redactado por construcción diga lo que diga la casilla.

  El constructor de filas no supone nada sobre el orden de las seis líneas del
  bloque de telemetría, porque ese orden no es fijo — la línea de sensores la
  imprime una tarea distinta de la del lazo, y una captura del 20.08 la tiene al
  final donde una del 21.08 la tiene al principio. Una fila se cierra en cuanto
  una línea intenta escribir un campo ya presente, lo que sólo puede significar
  que ha empezado el segundo siguiente.

- **Tres secciones del manual que se ganó el responder dos veces a lo mismo.**
  *10.1 Cómo informar de un problema* pide las cuatro cosas que ha necesitado
  aquí todo diagnóstico — el registro de arranque como texto, el
  `gpsdo_config.h` compilado, qué módulo GNSS (de tiempo auténtico, de
  navegación auténtico o clon) y cuánto cielo ve la antena — más la única
  comprobación que conviene hacer primero: un banner sin línea
  `compiled <fecha>` significa un build anterior a v1.05, y varios informes
  resultaron ser fallos ya corregidos. *Cómo dejar el survey fijo* (Parte 3.2)
  explica hacer el survey una vez en u-center V8.29 y guardarlo en la
  configuración del módulo respaldada por batería, lo que supera el compromiso
  de 300 s / 5 m del firmware y sobrevive a los cortes de alimentación —
  recomendación de Alan Cashin y la precisión más barata del montaje. Y
  *Apéndice C — Glosario* define los veintiún términos que este proyecto usa en
  un sentido concreto, de ADEV a ZC, porque la mitad significan otra cosa en
  otros sitios.

---

## [v1.05-rtos] — 2026-08-20

El algoritmo 12, hecho funcionar. Salió en v1.04 con la aritmética correcta y
cinco fallos independientes en la maquinaria que la rodea, cada uno de los cuales
ocultaba al siguiente. El lazo mantiene ahora la fase en 5–8 ns RMS a lo largo de
23 horas con un único rearmado del picDIV, frente a los 10–23 ns de la mejor
referencia anterior — y cada corrección de abajo se simuló antes de grabarla,
porque los dos cambios de este proyecto que salieron solo con razonamiento
resultaron ambos equivocados.

### Corregido
- **El estimador de ruido solo podía descender.** La puerta de valores atípicos
  era `dp_lim = 5*sigma`, leída de la propia estimación que alimentaba: en cuanto
  sigma era pequeña, toda diferencia lo bastante grande como para elevarla se
  rechazaba por atípica. Medido el 14.08 — `sig` marcó exactamente 2 ns durante
  las 1020 muestras de una serie, y con los límites por nivel derivados de ella
  la jerarquía quedó clavada en el suelo de 100 unidades: 79 de 80 correcciones
  dispararon en el nivel 0. Un acumulador multinivel que nunca abandona el nivel
  cero no lo es. La puerta es ahora absoluta (300 ns), y los atípicos reales para
  los que existía — diferencias tomadas a través de un hueco NOPH/SYNC/rearmado
  — se excluyen estructuralmente con una bandera de contigüidad en lugar de
  estadísticamente. Sigma tiene un suelo de 5 ns, por debajo del cual este
  detector no resuelve honestamente.
- **El término de frecuencia tenía el signo invertido.** Llevaba `+polarity`,
  copiado de la rama de frecuencia del algoritmo 11 — pero esa rama lee TIM2, y
  el propio comentario del algo 11 en este firmware recoge el hallazgo de
  hardware de que TIM2 y el detector LTIC tienen orientación opuesta en este
  cableado. La pendiente `f_nss` no es una lectura de TIM2: es la derivada de los
  mismos valores del acumulador que producen el término de fase, del mismo
  sensor. Una magnitud y su propia derivada temporal, medidas por un solo sensor,
  no pueden requerir signos de realimentación opuestos. El `cvPWM` de Alan
  coincide: pasa fase y pendiente por una sola conversión y las suma. Con la
  planta medida en vez de supuesta (+319,5 µHz/LSB, de regresar la media de 100 s
  del PWM contra la media de frecuencia de 100 s impresa, correlación 0,999 a
  retardo cero), el signo antiguo daba `d(phase_rate) = +0,4·f_ns`. Eso es
  realimentación positiva.
- **`s_mla_wait` nunca se ponía a cero.** Aparecía exactamente dos veces en el
  fichero, en su declaración y en el `++` de la prueba de abandono, y nunca
  volvía a cero. Así que unos cinco minutos después de arrancar superaba 300 y la
  prueba de abandono disparaba en el mismo segundo en que se alzaba la bandera,
  lo que mataba las dos cosas que esa bandera controla: la corrección por cruce
  por cero y la supresión de nuevas correcciones mientras un deslizamiento aún
  lleva la fase a casa. La serie que lo encontró: `zc` = 7 en 76 minutos, todos
  dentro de los cinco primeros, y 1072 de 1174 correcciones exactamente a dos
  segundos, que es la cadencia desnuda del nivel 0 sin nada que la frene. El
  mecanismo de cruce por cero, por tanto, nunca había funcionado más allá de los
  primeros minutos de ninguna serie desde que se introdujo.
- **El rescate FLL era un lazo bang-bang y no podía ser otra cosa.** Su paso era
  `-f*lsb_per_hz*0,10` limitado a ±64, que satura en |f| = 0,256 Hz, mientras que
  la puerta que tenía debajo solo abría a 0,3 Hz — la parte proporcional no podía
  actuar nunca. Accionado una vez por segundo desde una media de 100 s, unos 50 s
  de retardo, eso da 64 LSB/s × 50 s = 3200 LSB de recorrido antes de que la
  medida responda: 1,0 Hz de sobrepasamiento. Ambas cifras están en los registros
  (462 de 655 pasos consecutivos exactamente ±64; PWM barriendo 12 845 LSB;
  `f100` oscilando de −1,29 a +1,55 Hz). Ahora aplica la corrección calculada
  entera una vez y espera lo que necesite refrescarse la media de la que
  provino, a dos velocidades: la media de 10 s mientras el error es grande, la de
  100 s cuando es pequeño, con la espera siempre igual a la ventana en uso. La
  puerta pasó de 0,3 Hz a 0,05 Hz, porque por encima de 0,147 Hz la fase recorre
  toda la banda del detector de ±940 ns dentro de un solo horizonte de 64 s — la
  puerta antigua dejaba una zona muerta de 0,147 a 0,3 Hz en la que el lazo de
  fase no conseguía una mirada lo bastante larga y el FLL daba su trabajo por
  hecho.
- **`instant_offset` desbordaba.** `FREQ_LOWER`/`FREQ_UPPER` admiten ±500 Hz y el
  campo era `int8_t`, así que todo lo que pasara de ±127 alimentaba basura a
  cualquier puerta que lo leyera. Ahora `int16_t`, en los tres ficheros que lo
  tocan — la estructura, la conversión que lo rellena y la instantánea que lo
  copia. Arreglar solo uno habría compilado limpiamente dejando el desbordamiento
  en su sitio.
- **Las ramas de frecuencia y de FLL tomaban el signo de un menos incrustado.**
  Correcto únicamente porque `LPOL` vale −1 en esta placa; realimentación
  positiva en una placa con `LPOL +1`. Ambas toman ahora la `polarity` de la
  placa, que aquí evalúa idéntico y en otras partes correcto.
- **El dithering escribía solo una de sus dos tablas DMA.** El doble búfer
  alterna en cada pasada, así que la otra tabla — todavía con el código anterior
  — se reproducía hasta la escritura siguiente, y la salida alternaba entre el
  código viejo y el nuevo a unos 3 Hz (una pasada son 2^(24−N) periodos de
  portadora = 167,8 ms sea cual sea N). Un filtro de dos polos a 0,8 Hz atenúa
  3 Hz solo 14×. Ahora se rellenan ambas tablas, bajo un mutex, porque
  `pwm24_write()` se alcanza tanto desde ControlTask como desde CliTask y dos
  rellenos concurrentes de una misma tabla se entrelazan en una reproducción rota
  de 168 ms. Escribir la tabla que el DMA está leyendo es seguro por
  adelantamiento: el relleno escribe una entrada cada pocos microsegundos donde
  el DMA consume una cada 81,9 µs.
- **`PO` y `AO` no admitían el valor cero.** La comprobación de rango era
  `v >= −3000 && v <= 3000 && v != 0.0f`, de modo que el único valor que un
  usuario querrá con más probabilidad era el único rechazado. Rangos corregidos a
  ±5000 Pa y ±3000 m, y ambos llevan ya sus unidades en la ayuda y en el eco.

- **La placa no siempre arrancaba en frío, y el raíl de 3,3 V nunca fue la
  causa.** `ubx_poll_svin_nav()` llamaba a `vTaskDelay()` incondicionalmente. Su
  gemela `ubx_poll_svin()` lleva la guarda y el comentario que la explica —
  *"before vTaskStartScheduler() this must not be vTaskDelay(): calling it with
  no scheduler hangs the system"* — y la corrección entró en una de las dos y no
  en la otra.

  Antes de que exista el planificador, `vTaskDelay()` escribe a través de
  `pxCurrentTCB`, que todavía es `NULL`, así que la placa entra en hard fault y
  el manejador por defecto gira con las interrupciones desactivadas: sin salida,
  sin watchdog, nada salvo el botón de reset. Los ganchos de fallo de FreeRTOS
  añadidos en v1.04 no pueden atraparlo — necesitan un núcleo en marcha.

  Se escondía detrás del orden de llamadas en `gpsdo_gps_init()`: NAV-SVIN sólo
  se consulta cuando TIM-SVIN no respondió dentro de su ventana de 500 ms. Un
  receptor que ya está funcionando responde y la placa arranca — es el caso tras
  un reset, porque el receptor conserva su propia alimentación. Uno que todavía
  está arrancando no responde, y la placa se queda muerta. Ese es el caso del
  encendido en frío, y esa asimetría es la razón por la que esto pareció durante
  mucho tiempo una alimentación que se hundía mientras subía el raíl.

  Encontrado en un registro con cuatro arranques consecutivos, cada uno
  terminando tras `UBX: CFG-NAV5 ACK` y antes de `LEA-T: starting survey-in`, con
  la causa de reset leyendo `PIN/NRST` todas las veces — el botón del operador.
  El decodificador imprime `POWER-ON/BROWN-OUT` y una línea sobre revisar el raíl
  de 3V3 cuando la culpa es de la alimentación, y no apareció ni una sola vez.
  Un fallo de alimentación no se detiene cuatro veces en la misma línea de
  código.
- **Los dígitos de frecuencia no seguían el lock del algoritmo 12.** La lógica
  del color tiene una rama autoritativa para los lazos que publican un estado en
  vivo, y el algoritmo 12 no estaba en la lista — así que caía en la rama de las
  medias de frecuencia, que es precisamente lo que el comentario sobre esa rama
  dice que el verde no debe ser.

  Medido sobre una serie de 2,99 h: el lazo y el color discreparon en el
  **15,0%** de las muestras, y en todos esos casos el lazo estaba LOCKED con los
  dígitos BLANCOS, nunca al revés. El lazo alcanzó LOCK a los 108 s y los dígitos
  se pusieron verdes a los 1041 s. Quince minutos de un oscilador disciplinado
  con aspecto de no estarlo, en cada arranque, porque hasta que se llena la media
  de 1000 s esa rama no tiene con qué juzgar y `locked` es falso por
  construcción.

  El algoritmo 12 toma ahora su color de su propia tendencia, como el 10 y el 11.
  `CORR` y `ZC` cuentan como lock: son estados de un segundo que significan que
  el lazo está haciendo su trabajo, el mismo razonamiento por el que no
  reinician `s_mla_quiet`. Sin eso los dígitos parpadearían en blanco una vez por
  corrección — dieciséis veces en las tres horas medidas. La concordancia es
  ahora del 100%.
- **La guarda de «eco rancio» estaba puesta en medio conteo.** Retira el lock
  basado en la media larga cuando la media de 10 s se ha desviado, y el umbral
  era ±50 mHz. Pero la media de 10 s es un conteo de ciclos durante diez
  segundos, así que su granularidad es 0,1 Hz: a lo largo de tres horas tomó
  exactamente tres valores — −100, 0 y +100 mHz — y nada intermedio. Un umbral de
  50 mHz no significaba entonces «dentro de 50 mHz», sino «el contador debe leer
  exactamente 10 000 000», y un conteo en cualquier dirección mataba el verde.
  Eso es el 8,3% de las muestras asentadas y el 46% de la discrepancia anterior.
  Ahora ±0,15 Hz: un conteo completo más medio de margen, de modo que un temblor
  de un conteo pasa y una pérdida real de disciplina — muchos conteos, que es
  para lo que existe la guarda — sigue sin pasar. Los algoritmos 0-9 llevan la
  misma guarda y reciben la misma corrección.

### Añadido
- **Una puerta por nivel sobre el término de frecuencia.** El ruido propio de la
  pendiente es `sd(f_nss) = sigma · 2^((1−3L)/2)`, así que en el nivel 0 es
  1,41·sigma de ruido puro escalado por 12,5 LSB por ns/s, frente a los 0,39 LSB
  por ns del término de fase — una ventaja de 32:1 a favor de la magnitud
  equivocada. El registro mostró lo que eso compraba: el 46% de las correcciones
  chocaba contra el límite de ±470, una de ellas con la fase leyendo exactamente
  0 ns y la corrección a fondo de escala. El término se usa ahora desde el nivel
  3 hacia arriba, donde esa misma estimación está promediada sobre pares de 16 s
  y vuelve a ser una medida.
- **Un ajuste fino de frecuencia desde TIM2.** Cuando la media de 100 s indica
  más de 0,03 Hz, la componente de frecuencia de la corrección se toma de esa
  medida en lugar de la pendiente del acumulador. En régimen permanente está
  dormido — en una simulación de 10 h no disparó ni una vez — y ese es
  precisamente el punto: atrapa las excursiones de frecuencia que de otro modo
  sacarían la fase de la banda del detector, de manera que el lazo nunca tiene
  que readquirir. La serie de 23 h que estableció las cifras de arriba registró
  **un solo** rearmado del picDIV, frente a 121 con los mismos ajustes sin él.
- **`configUSE_MUTEXES` e `INCLUDE_xTaskGetSchedulerState`**, fijados
  explícitamente. El cerrojo del dithering necesita ambos, este proyecto no
  fijaba ninguno, y si la configuración por defecto de la librería los habilita o
  no es algo que no se deja al azar: una macro ausente es un error de compilación
  y no una sorpresa en ejecución.

- **Los 8 bits bajos del dithering llegan por fin al lazo.** v1.04 sacó la salida
  de 24 bits y dijo, en este mismo registro, que todavía no daba al lazo pasos
  más finos: cada algoritmo llamaba a `gpsdo_dac_write16()`, que desplazaba el
  valor a los 16 bits altos para que los ajustes guardados conservaran su
  tensión, y el byte bajo era siempre cero. Ya no lo es.

  La fracción pertenece a `gpsdo_dac.cpp`, no al lazo de control, y ahí está todo
  el diseño. El valor de control se escribe desde 21 sitios — los barridos `CT` y
  `LC`, las rampas de adquisición, el gobierno en holdover, `SP` y el propio lazo
  — y veinte de ellos son gruesos a propósito: un barrido que acaba en 30720,4 en
  lugar de en 30720 no es un barrido mejor, es uno cuyo punto de referencia nadie
  sabe enunciar. Toda escritura gruesa borra la fracción como efecto secundario
  de pasar por `gpsdo_dac_write16()`, de modo que ningún llamante tiene que
  acordarse. Guardar la fracción en el lazo habría significado veinte sitios que
  debían saber ponerla a cero — que es exactamente la clase de fallo para la que
  se creó ese único punto de escritura.

  Lo que compra, sobre la planta medida aquí: un paso de 16 bits son unos 320 µHz,
  o sea 3,2e-11 de 10 MHz — más grueso que los 4e-12 que se midió al lazo
  manteniendo a lo largo de 10 000 s. Llegaba ahí ditherando entre códigos
  contiguos de una corrección a la siguiente, lo cual funciona pero deja la
  tensión de control cazando. Con la fracción conservada, una corrección menor
  que un paso se aplica en lugar de truncarse, y el paso pasa a 1,25e-13.

  El truncamiento que desaparece era además sesgado: `(int32_t)` redondea hacia
  cero, así que toda corrección perdía parte de sí misma en la misma dirección —
  lo que el lazo lee como un error de ganancia de hasta un sexto en las
  correcciones de 6 LSB que se ven en operación normal.

  Por encima de la capa DAC no cambió nada. `gpsdo_dac_last16()` sigue devolviendo
  un `uint16_t` liso, de modo que las pantallas, la línea de telemetría y el
  anillo en flash ven exactamente lo que veían antes, y el bloque de ajustes sigue
  guardando 16 bits: una restauración arranca con fracción cero y cede como mucho
  1,25e-13, por debajo de lo que este hardware puede mostrar.
- **`DAC` — una orden que dice qué es realmente la tensión de control.** La ruta
  de salida y, para el dithering, su frecuencia portadora y la RAM de las tablas;
  el código en tres vistas — 24 bits, los 16 bits redondeados que usan las
  pantallas y el anillo en flash, y el valor fraccionario exacto con su diferencia
  respecto al redondeado; el Vctl medido; y el tamaño de paso en ambas anchuras,
  en µHz y como fracción de 10 MHz. Un código de 24 bits que no es múltiplo de 256
  es la prueba de que quien gobierna el pin es la ruta fina, y por eso se imprimen
  las tres vistas y no una; la orden dice además con todas las letras si la ruta
  fina está activa o si la salida la está redondeando.

  Las cifras de paso necesitan la ganancia de la planta, que sólo `CT` puede
  aportar. Sin ella la orden lo dice, en vez de imprimir un número derivado de un
  valor por defecto. Figura también en la pestaña Help del tuner.

- **`MF` y `MFT` — los límites por nivel reciben su propia fuente, elegida con
  independencia de la ganancia.** Ambos compartían un solo `if`, así que `MG 0`
  significaba «ganancia de CT **y** límites de la fórmula de ruido» y `MG > 0`
  «ganancia a mano **y** límites a mano». No hay razón para que estén soldados:
  la ganancia pertenece al OSCILADOR — es LSB por ns, y otro OCXO tiene otra
  sensibilidad de Vctl — mientras que los límites pertenecen al RUIDO DE FASE que
  ve la placa, que es propiedad del emplazamiento y del receptor. «Ganancia
  medida, límites puestos a mano», que es lo que quiere una instalación ruidosa,
  no podía expresarse en absoluto.

  `MF 0` sigue a `MG` como hasta ahora y es el valor por defecto, así que nada se
  mueve mientras nadie lo pida. `MF 1` mantiene la tabla guardada, `MF 2` la
  fórmula de ruido, `MF 3` la tabla medida de abajo. Ambos ajustes caben en tres
  bytes de relleno que el bloque de algo-12 ya tenía, de modo que la disposición,
  el tamaño y `SETTINGS_VER` quedan intactos y un bloque guardado por una versión
  anterior sigue cargando — se lee como 0/0, que es exactamente el comportamiento
  que tenía aquella versión.
- **`MF 3` — límites por nivel medidos en lugar de extrapolados.** La fórmula es
  `thr[L] = 8·σ·√(2^L)·√10`, y `√(2^L)` afirma que la fase es BLANCA, de modo que
  promediar 2^L muestras reduce el test como 2^(L/2). Medido en dos placas de
  este diseño — mismo PCB, mismo OCXO, habitaciones distintas — el exponente es
  **0,95 y 1,03**, no 0,50. Promediar no compra casi nada aquí, porque lo que
  importa es una deriva lenta (autocorrelación 0,96 a 60 s, 0,64 a 300 s) y no el
  ruido muestra a muestra. El error se agrava con el nivel: la fórmula subestima
  la dispersión real unas 5 veces en el nivel 0 y más de 100 en el nivel 10, así
  que su tabla cae 32 veces a lo largo de la jerarquía donde la propia fase cae
  1,3.

  De modo que el exponente se mide. Cada nivel guarda el valor cuadrático medio
  de su propio estadístico de test, un ajuste por mínimos cuadrados de log2(sd)
  frente al nivel da amplitud y exponente a la vez, y la tabla se construye del
  ajuste. Ajustar A TRAVÉS de los niveles, en lugar de fiarse de cada uno por
  separado, es lo que lo hace utilizable pronto: el nivel 8 se evalúa una vez
  cada 512 s y necesitaría medio día para tener varianza propia, pero los niveles
  bajos se pueblan en minutos y el ajuste extrapola.

  Con ello desaparecen cinco números fijados a mano: el exponente 0,5, el
  multiplicador `8.0` (ahora el cuantil normal para la tasa de disparos por ruido
  que enuncia `MFT` — que es el trabajo que el 8 hacía a mano, porque la
  jerarquía prueba el nivel 0 mil veces más a menudo que el 10), la propagación
  de ruido blanco `√10`, el suelo de σ en 5 ns — propiedad de este detector, no
  de la aritmética — y el suelo de 100 unidades. Queda un número con significado
  físico: cuánto tiempo entre correcciones disparadas sólo por ruido.

  El exponente se limita a [0,5; 1,0] y eso es física, no gusto. Por debajo de
  0,5 el promediado quitaría más de lo que el ruido blanco permite; por encima de
  1,0 la dispersión crece más deprisa que plana-en-ns, lo que es una RAMPA de
  fase y no una placa más ruidosa — y dejar que una rampa suba el umbral es el
  fallo ya registrado en este archivo, donde sigma trepó de 165 a 746 ns y el
  lazo se congeló.

  Verificado reproduciendo la aritmética del propio firmware sobre los registros
  de ambas placas: la tabla del taller sale en 74 ns cayendo hasta 14, que es
  donde esa placa quedó ajustada a mano después de que el modo automático
  resultara inestable, y la placa de casa reproduce su propio comportamiento
  asentado. En una serie de tres horas la de casa ajustó **α = 1,00** y corrigió
  en los niveles 5 a 9 — la primera vez que esta jerarquía usa más de uno o dos
  de sus niveles.

  **No es automáticamente mejor.** En la placa de casa, donde la tabla mucho más
  ceñida de la fórmula casualmente encajaba con un emplazamiento tranquilo, la
  tabla medida duplica el RMS de fase (mediana de 11,6 ns frente a 5,5 ns sobre
  una ventana de la misma longitud) porque corrige un tercio de veces. Las dos
  tablas hacen preguntas distintas — la fórmula pregunta si una desviación supera
  el ruido de medida, la tabla medida si es inusual para esta placa — y cuál de
  las dos acierta depende del emplazamiento. Para eso está `MF`.

### Cambiado
- `LOCK` en el campo de tendencia significa ahora que la jerarquía está tranquila
  **y** que la frecuencia de TIM2 se mantiene dentro de 0,05 Hz, contado sobre
  segundos tranquilos consecutivos y no sobre `s_mla_count`, que se reinicia en
  cada corrección y era un mal indicador de cuánto hacía que había pasado algo.

- **`GPSDO_PWM_DITHER` está activado en la configuración que se distribuye.**
  Salió en v1.04 desactivado, mientras se comprobaba la ruta de salida; cerrada
  ya la ruta fina y con una serie de 23 horas detrás, «desactivado» deja de ser
  el valor por defecto honesto. Comentarlo sigue devolviendo al PWM sencillo de
  16 bits, y el pin, el filtro y el cableado son los mismos en ambos casos.
- **La disposición de campos del panel 320×240 coincide ya con la del 480×320.**
  Este manual viene diciendo desde v0.93 que la pantalla de trabajo se diseña una
  vez y se escala, y eso era cierto de la geometría y no del contenido: los dos
  paneles se habían ido separando campo a campo. qErr sube a la fila de Alt, junto
  a los datos de fix a los que pertenece; AHT y el campo de fase intercambian
  columnas, de modo que los sensores ambientales comparten la columna izquierda y
  los eléctricos la derecha; Vcc y Vdd ocupan juntos la fila que quedó libre. El
  panel pequeño muestra todo lo que muestra el grande.

  Todo campo que combinaba una etiqueta con un valor de anchura variable se partió
  en dos. Una sola cadena anclada a la derecha fija la unidad y arrastra la
  etiqueta de lado a medida que cambia la anchura de los dígitos — en qErr se veía
  como una etiqueta que se movía una vez por segundo. Etiqueta y valor son ahora
  ranuras distintas con relleno distinto: la etiqueta sujeta el borde izquierdo de
  la columna, el valor conserva el anclaje derecho y sólo cambia el hueco entre
  ambos. Igual para `dph` y para la corriente del INA.
- **La fuente 2 es proporcional, y esta disposición se había calculado a 8 px por
  carácter.** Contrastado con la propia tabla de anchuras de la librería, eso
  exagera las cadenas del panel pequeño en torno a una quinta parte — `Vph:1.951V`
  mide 70 px, no 80. El error no era académico: es lo que había costado la
  etiqueta `dph` y lo que mantenía Vcc en dos decimales donde el 480 muestra tres.
  Ambas cosas han vuelto. Los campos de la derecha comparten ahora una única línea
  de alineación en x=314 — aquella a la que Vdd ya estaba anclado — de modo que
  qErr, dph, la corriente del INA y Vdd forman una columna en lugar de cuatro
  casi-aciertos. El relleno de cada campo es ahora la anchura medida de su propia
  forma más ancha, y no la de la lectura de hoy, y los rellenos de una fila la
  embaldosan exactamente, así que ningún fondo puede borrar el borde del vecino.

### Créditos
- **Alan Cashin** (MIS42N en el foro EEVBlog) aparece ya acreditado donde el
  trabajo es suyo: en `V`, en la cabecera de la ayuda, en la pantalla About del
  sintonizador y en la tabla de créditos de los tres manuales. El algoritmo 12,
  la corrección por cruce por cero, el PWM con dithering y la idea de
  autoevaluación `CS` proceden todos de su Budget GPSDO. Hasta ahora figuraba
  como «dither / DAC discussion», lo que se quedaba bastante corto.

### Medido
Veintitrés horas, umbrales automáticos, `MR 9`, dithering a 13 bits:

| | esta serie | mejor anterior |
|---|---|---|
| fase RMS, ya asentada | **5–8 ns** | 10–23 ns |
| \|fase\| < 10 ns | **86,7%** de las muestras | — |
| rearmados del picDIV | **1** | 121 |
| niveles de corrección alcanzados | **5–6 típico, hasta 8** | 0 |
| frecuencia a 10 000 s | **4e-12** | 1,4e-11 |
| intervalo entre correcciones | 254 s | 130 s |

`NOPH` tres veces en 82 572 muestras; `FLL` una. La presión ambiente cayó 4 hPa a
lo largo de la serie y el lazo no reaccionó.

---

## [v1.04-rtos] — 2026-08-12

### Añadido
- **`GPSDO_PWM_DITHER` — tensión de control de 24 bits a partir de un PWM corto con dithering.**
  Idea de Alan Cashin (MIS42N): haz correr el PWM con menos bits de los que
  necesitas y varía el ciclo de trabajo de un periodo al siguiente, de modo que
  sea la media la que lleve el resto.

  La ganancia es la PORTADORA, no los bits de más. El rizado hay que filtrarlo por
  debajo de un paso de salida, y lo difícil que eso resulte depende de la
  separación entre la portadora y el codo del filtro: el PWM de 16 bits a 2 kHz
  admite un codo de 0,7 Hz y una constante de tiempo de 230 ms, mientras que el
  dithering de 13 bits a 12,2 kHz admite 4,2 Hz y 38 ms. El retardo del filtro
  entra en el lazo directamente como desfase, así que un filtro seis veces más
  corto vale más que la resolución.

  Alan hace el dithering en una interrupción de temporizador porque un PIC no
  tiene DMA. Aquí serían 12 000 interrupciones por segundo compitiendo con la
  captura del 1PPS — la única interrupción que no puede retrasarse. Pero el patrón
  para un valor constante es periódico, así que se calcula una vez en una tabla y
  se reproduce por DMA hacia el registro de comparación: 0,012% de CPU a 13 bits,
  y nada de ello dentro de una interrupción. La media es exacta por construcción
  — la tabla contiene exactamente Y entradas de valor X+1 entre 2^(24-N).

  El mismo pin que antes (PB9, TIM4 CH4), así que el filtro y el cableado
  existentes no cambian. TIM4_UP gobierna DMA1 Stream 6 Channel 2; el tic de 2 Hz
  está en TIM9 y la cadena del 1PPS en TIM2/TIM3, de modo que no se perturba nada
  más. Dos búferes en modo doble búfer por hardware hacen que un cambio de valor
  nunca produzca un glitch en el pin.

  Desactivado por defecto. Cuesta 8 KB de RAM a 13 bits, 16 KB a 12.

  **Lo que todavía no da** es un paso más fino para el lazo: cada algoritmo llama
  a `gpsdo_dac_write16()`, que desplaza el valor a los 16 bits altos para que los
  ajustes existentes conserven su tensión. Los 8 bits bajos esperan a un lazo que
  llame a `gpsdo_dac_write24()`.
- **Corrección en el paso por cero, del diagrama de flujo de Alan.** Tras una
  corrección por límite que cambia la frecuencia, la fase sigue moviéndose en la
  dirección que ya llevaba: barre a través de cero, sale por el otro lado y
  normalmente vuelve a incumplir el límite — así que el lazo corrige, se pasa,
  corrige de vuelta y se asienta despacio.

  El instante en que la fase cruza cero es especial. El error de fase es nulo,
  pero el error de frecuencia que la llevó hasta allí sigue presente; cancelar el
  error de frecuencia exactamente entonces deja al oscilador con la frecuencia
  correcta Y sin error de fase, en lugar de en un estado hacia el que el lazo
  tiene que iterar.

  Medido contra los propios registros de Alan del mismo diseño: su lazo corrige
  cada 506 segundos donde este corregía cada 130. La mayor parte de esa diferencia
  es precisamente esta prueba, que él describe como esencial y que aquí faltaba.

  Se informa como `zc=` en la telemetría; la tendencia muestra `ZC` en el momento
  en que actúa.
- **Algoritmo 12 — acumulador multinivel.** Según el diseño de Alan Cashin
  (MIS42N en EEVblog). Cualquier otro lazo aquí tiene una única constante de
  tiempo, y esa constante es un compromiso que nadie gana: medido contra una
  referencia de rubidio, `LTC 60` es hasta 1,58× mejor por encima de 800 s
  mientras que `LTC 240` es hasta 1,44× mejor entre 10 y 400 s. Hay que elegir.

  Este algoritmo no elige. Las lecturas se acumulan en niveles — el nivel n abarca
  2^n segundos — y la corrección se aplica en el nivel **más bajo** cuyo error
  supere su límite. Un error grande actúa en dos segundos; uno pequeño espera a un
  promedio más largo. No hay `LTC` que ajustar.

  Los niveles salen del patrón de bits del contador de segundos, no de una matriz
  de búferes: once niveles, de 2 s a 2048 s, por 22 bytes.

  **La entrada es la fase en nanosegundos del detector LTIC.** La primera versión
  alimentaba el error de cuenta de TIM2 en hercios enteros y estaba ciega: un
  oscilador disciplinado se sitúa muy por debajo de 1 Hz, así que ese campo leía
  cero en el 83% y el 95% de las muestras en dos ejecuciones. La fase se integra
  donde una cuenta de frecuencia de un segundo no lo hace. Alan preguntó por qué
  se habían citado 100 ns cuando un TIC resuelve 1 ns; tenía razón, era la
  resolución del contador y no la del detector.

  **La prueba de frecuencia se ha eliminado**, siguiendo el consejo del propio
  Alan: *«Fue un experimento... lo que queremos es un sistema estable donde las
  pruebas siempre pasen. Así que la prueba de frecuencia es innecesaria.»*

  Nuevos comandos `MG`, `MR`, `MLP` y `ML`, guardados con `ES ALGO12`. Los límites
  por nivel son editables y persistentes porque solo **uno** se dedujo alguna vez
  — 125 ns a 128 s, de la especificación original de 10 MHz ±0,01 Hz. Alan
  describe el resto como arbitrarios.
- **Informe de la causa del reinicio al arrancar.** `RCC->CSR` se lee y decodifica
  antes de que se ejecute cualquier otra cosa, de modo que un reinicio
  intermitente ya no parece idéntico venga de una caída de tensión, del pin de
  reset o de un reinicio por software. Añadido después de que una placa se
  reiniciara repetidamente en el mismo punto de la configuración del GPS sin forma
  de saber cuál era.
- **Ganchos de fallo de FreeRTOS y un `STM32FreeRTOSConfig.h` propio.**
  `configCHECK_FOR_STACK_OVERFLOW` y `configUSE_MALLOC_FAILED_HOOK` valen 0 por
  defecto, así que una pila desbordada corrompe en silencio a su vecina y
  `configASSERT` queda atrapado en un `for(;;)` con las interrupciones
  deshabilitadas — un panel blanco muerto y nada en la consola. Así se
  presentaron exactamente los tres últimos fallos: una pila de CLI demasiado
  pequeña para la escritura del anillo de flash, un grupo de eventos NULL leído
  antes de arrancar el planificador, y una estructura declarada en una rama muerta
  que aun así agrandó el marco de la tarea de pantalla.

  La anulación activa ambos ganchos y redefine `configASSERT` para imprimir
  fichero y línea antes de detenerse. Los ganchos nombran la tarea culpable en la
  consola USB y parpadean el LED, de modo que el siguiente se identifica solo en
  segundos. `Serial` a secas, no `OUT_SERIAL`: un gancho no debe tocar un mutex ni
  un flujo Bluetooth que podría ser lo que ha fallado.

  Autoría del fichero: GLM-5.2, adoptado aquí prácticamente tal cual.

### Corregido
- **Los umbrales del algoritmo 12 ahora se miden, no se heredan.** Se tomaron del
  diseño de Alan y se escalaron por la razón entre pasos de contador, que es la
  magnitud equivocada: lo que un umbral debe superar es el **ruido** de la medida
  de fase, y ese difiere entre montajes por razones que un tamaño de paso no
  captura. Medido en esta placa: fase media −1 ns con una desviación típica de
  462 ns — el oscilador estaba bien ajustado y toda esa dispersión era ruido,
  mientras el umbral del nivel 0 estaba en 462 ns. El 41% de las muestras lo
  cruzaba. 620 correcciones en 1685 segundos, la jerarquía reiniciándose cada
  2,7 s y sin alcanzar nunca el nivel 2.

  El firmware estima ahora el ruido de fase de forma continua y fija con él el
  umbral de cada nivel. Al hacerlo apareció un segundo error: el umbral se aplica
  a la expresión de prueba |3b − a|, cuya desviación es sigma·√(2^L)·√10, y no a
  la fase media, cuya desviación es sigma/√N. Usar la segunda dejaba el umbral 4,5
  veces demasiado bajo en el nivel 0 y peor arriba. Seis sigma sobre la magnitud
  correcta lleva el intervalo entre correcciones a alrededor de un minuto, frente
  a los 256 s en los que se asienta el diseño de Alan.

  `ML` informa del ruido medido y de si los límites lo siguen. La telemetría lo
  lleva como `sig=`. Poner `MG` por encima de cero detiene el autoajuste.
- **El algoritmo 12 ignoraba la polaridad del detector y confundía nanosegundos
  con hercios.** Dos fallos en la misma conversión, hallados juntos en un registro.

  `LPOL -1` no se aplicaba en absoluto — el algoritmo 11 multiplica su término de
  fase por `-polarity` y este no — así que en una placa así cada corrección iba en
  sentido contrario. Y la fase media, en nanosegundos, se multiplicaba por
  LSB-por-hercio como si fueran la misma magnitud: anular P ns en T segundos exige
  P/(100·T) Hz a 10 MHz, de modo que la corrección salía 100·T veces demasiado
  grande, de 200× en el nivel 0 a 102 400× en el nivel 9.

  Realimentación positiva cuatro órdenes de magnitud demasiado fuerte describe
  bien lo que mostró el registro: 6000 cuentas de oscilación del PWM en 148
  correcciones.
- **`MG` y `MR` se aceptaban y guardaban pero nunca se leían.** Los comandos
  funcionaban, el afinador los enviaba, `ML` los listaba, y el algoritmo no usaba
  ninguno. Ambos están ahora conectados.
- **El algoritmo 12 exige ahora el detector LTIC y se detiene en lugar de
  adivinar.** Estaba escrito para recurrir a integrar el error de cuenta de TIM2
  en placas sin detector. Esa vía era activamente destructiva: la cuenta está
  cuantizada a hercios enteros y lee cero en un oscilador disciplinado, así que
  integrarla producía un paseo aleatorio de ruido de cuantización en vez de fase.
  Medido: 6000 cuentas de oscilación del PWM en 148 correcciones, con el detector
  contra el raíl el 58% del tiempo y la fase informada clavada en 0.

  `LA 12` ahora rechaza sin `GPSDO_LTIC`, y cuando el detector está montado pero
  no lee, el algoritmo se detiene y deja trabajar al puente del picDIV.
- **El algoritmo 12 ahora arma el picDIV.** No lo hacía, y el fallo era silencioso:
  con la rampa contra el raíl el detector nunca devuelve una lectura válida, así
  que el código recurría a integrar la cuenta y volvía a estar ciego — justo lo
  que pasar al detector debía evitar. El mismo puente con espera que el algoritmo 11.
- **El afinador dejó de leer nada de la placa.** `STATE_HINT` se añadió a
  `TelemetryParser` pero se referenciaba como `self.STATE_HINT` desde
  `GpsdoTuner`, una clase distinta. Cada línea de telemetría lanzaba entonces
  `AttributeError`, así que ninguna respuesta llegaba a su absorbedor y ni los
  campos de calibración de `LL` ni la tabla de límites del algoritmo 12 se
  rellenaban. Dos síntomas, un solo fallo.

  El manejador va ahora envuelto: un error de análisis cuesta una línea, no la
  recepción entera.
- **`MG` y `LG` respondían igual, con `gain=`.** El absorbedor de Lars corre
  primero y capturaba la respuesta del algoritmo 12. El firmware responde ahora
  `m_gain=` y `m_run_level=`.
- **La tabla de límites del afinador mostraba ceros.** Nunca se leía: la consulta
  de parámetros pedía solo los escalares. Ahora se lee al conectar y el envío se
  niega mientras alguna fila siga a cero.
- **La placa no arrancaba: sin LED, sin consola, nada.** `setup()` escribe el DAC
  tres veces antes de que se ejecute `xEventGroupCreate()`, y la compuerta de las
  nuevas estadísticas leía `xSysEvents`, todavía NULL. Además el marco de pila se
  dimensiona al compilar, así que una estructura en una rama que nunca se ejecuta
  reserva su espacio igualmente; veinte bytes desbordaron la tarea de pantalla y
  murió antes de `tft.init()`.

### Cambiado
- **`SETTINGS_VER` 4 → 5** para el bloque del algoritmo 12, **con migración**. Un
  bloque v4 se acepta, sus campos se aplican y los valores del algoritmo 12 quedan
  por defecto. Rechazarlo habría descartado un PID, un LC y una zona horaria que
  funcionaban solo porque se añadió un algoritmo.

## [v1.03-rtos] — 2026-08-01

Construido sobre v1.01. Los experimentos de v1.02 — un DAC delta-sigma en PB5 y
el soporte del core STM32duino 3.0.0 — no se trasladan: el primero se midió y no
entregaba lo prometido, el segundo colgaba la placa en hardware real. v1.01 sigue
siendo la base probada, con dos añadidos.

### Corregido
- **Un reinicio en caliente ya no reinicia un survey-in terminado.** El receptor
  conserva su propia alimentación y su propio estado a través de `RB`, así que un
  survey completado antes del reinicio sigue siendo válido: la posición que
  estableció no se ha movido. El firmware ordenaba antes un survey nuevo de todos
  modos, descartando un resultado que costó minutos y sacando al módulo del Time
  Mode mientras repetía trabajo ya hecho. `gpsdo_gps_init()` consulta ahora primero
  TIM-SVIN y omite el arranque cuando el receptor informa valid=1 con active=0 —
  Time Mode con un survey terminado detrás. Se informa como *already in Time Mode
  from an earlier survey*.

  Esto exigió hacer `ubx_poll_svin()` seguro de llamar antes del planificador:
  cedía con `vTaskDelay()` sin condición, lo que cuelga el sistema cuando no hay
  planificador en marcha. Ahora usa `delay()` en ese caso, el mismo patrón que ya
  empleaba el lector de ACK.
- **La placa no arrancaba: sin LED, sin consola, nada.** `setup()` escribe el DAC
  tres veces — el 127 inicial, el PWM recuperado y el valor por defecto — antes de
  que se ejecute `xEventGroupCreate()`. Las nuevas estadísticas de corrección
  cuelgan de la ruta de escritura del DAC, y su compuerta leía `xSysEvents`, que
  en ese punto seguía siendo NULL. Pasar NULL a `xEventGroupGetBits()` dispara
  `configASSERT` y detiene el procesador, así que el fallo ocurría antes del primer
  parpadeo y no dejaba nada en la consola que lo explicara. La compuerta comprueba
  ahora NULL primero; esas escrituras tempranas son órdenes, no correcciones, así
  que excluirlas es correcto además de seguro.

### Añadido
- **`CS` — estadísticas de corrección, el lazo evaluándose a sí mismo.** El
  algoritmo 11 se validó contra un patrón de rubidio en el banco de otra persona;
  casi nadie que construya esto tiene uno, y sin él quedan la palabra del autor y
  un indicador de enganche. La corrección que aplica el lazo es el error que acaba
  de observar, así que el tamaño de esas correcciones dice si la disciplina
  funciona — y la referencia es el GPS, de modo que no existe nada mejor con lo que
  comparar la frecuencia. El firmware ya calculaba estos números y los descartaba.

  Informa del RMS de la corrección sobre las últimas **100, 1 000, 10 000 y
  100 000 correcciones**, en cuentas de DAC y — una vez que `CT` ha medido la
  pendiente del oscilador — en frecuencia fraccional, directamente comparable con
  una cifra de ADEV. También el sesgo constante, no nulo cuando el lazo sigue una
  deriva real y no ruido.

  Las ventanas cuentan correcciones y no segundos porque el ritmo de corrección
  depende del algoritmo: el algoritmo 11 corrige una vez por segundo, el 10 una vez
  por `LIV`. Etiquetarlas en minutos habría significado una cosa con un algoritmo y
  sesenta veces eso con el otro — el mismo número describiendo dos periodos
  distintos. `CS` mide el intervalo real e imprime lo que abarcan las ventanas en
  tiempo de reloj, para que el lector no tenga que calcularlo. A una corrección por
  segundo, 100 000 cubre unas 28 horas.

  Son pesos exponenciales, no ventanas duras: alrededor del 63% del peso cae dentro
  de N correcciones y el 95% dentro de 3N. Eso cuesta cuatro multiplicaciones-suma
  por corrección y nada de memoria, mientras que un búfer de 100 000 muestras
  ocuparía la mayor parte de la RAM para responder lo mismo sin mejorarlo.

  Se cuenta solo con el lazo enganchado y sin calibración en curso: la rampa de
  adquisición, los tres saltos de `CT` y el barrido de `LC` son órdenes, no
  correcciones, y uno solo dominaría la media horaria mucho después de terminar.
  Los algoritmos 0-9 no tienen estado de enganche sobre el que basar la compuerta y
  quedan excluidos, cosa que `CS` dice en lugar de informar de un número sin
  significado definido.

  **La advertencia está en la salida, en la cabecera y en el README:** mide si el
  LAZO ESTÁ ASENTADO, no si la SALIDA ES BUENA. Un detector ruidoso hace que el
  lazo persiga ruido; las correcciones crecen, `CS` las informa fielmente, y el
  oscilador estaba bien hasta que el lazo lo empeoró. Nada medido desde dentro del
  lazo puede ver eso.

  La idea es de Alan (MIS42N en EEVblog), cuyo propio diseño se apoya justamente en
  esto y por eso no necesita un patrón secundario.
- **`GPSDO_DAC_EXT` — DAC SPI externo, planeado, no implementado.** Activarlo da un
  error de compilación deliberado: `dac_ext.cpp` es un esqueleto sin dispositivo
  elegido. El PWM de 16 bits da unos 50 µV por paso a 3,3 V, cerca de 2,7e-11
  fraccional en un oscilador de 5,3 Hz/V; un integrado de 18 bits con una
  referencia diseñada para el trabajo alcanza unos 17 µV, cerca de 9e-12, sin
  retardo de filtro dentro del lazo.

  No hace falta SPI por hardware, ni está disponible — SPI1 pertenece al TFT y
  todos los pines de SPI2 de este encapsulado están ocupados — pero el DAC se
  escribe una vez por segundo, así que moverlo por software cuesta microsegundos.
  Pines propuestos: PB0, PB2 y PB4, elegidos para evitar PB6/PB7, que parecen
  libres pero son los pines por defecto de I2C1 que reclama `Wire.begin()`; un DAC
  ahí rompería los sensores y la pantalla de reloj.

### Cambiado
- **Las 23 llamadas a `analogWrite(PIN_VCTL_PWM, ...)` pasan ahora por
  `gpsdo_dac_write16()`.** Añadir una segunda ruta de salida editando cada una
  habría invitado a olvidar alguna, y una llamada olvidada es el peor tipo de fallo
  aquí: el lazo dirigiría bien casi siempre y daría un salto cada vez que se tomara
  la ruta antigua. Añadir un DAC significa ahora rellenar una función.

## [v1.01-rtos] — 2026-07-29

> **Compila con el core STM32duino 2.12.0 o anterior.** El core 3.0.0 (23 de
> julio de 2026) despliega ArduinoCore-API, lo que elimina `ltoa()` y convierte
> `HardwareSerial` en una interfaz abstracta — ambos se usan aquí — y, más
> importante, deja a TFT_eSPI sin poder inicializar el panel (pantalla en blanco,
> el CLI no se ve afectado). Los dos primeros son menores y podrían condicionarse
> por versión; el tercero reside en la biblioteca. Detalles en el README.

Versión hito: fusiona la rama de persistencia por flash-ring con la rama del
algoritmo 11 (LTIC-Lars). El algoritmo 11 se basa en el controlador GPSDO
original de lazo PI continuo del difunto **Lars Walenius**, compartido con la
comunidad time-nuts; se amplía aquí con la autocalibración y la adquisición
descritas abajo, en su memoria.

### Añadido
- **Algoritmo 11 "LTIC-Lars"** — un único lazo PI continuo (sin máquina de estados
  ACQ/DPLL/LOCK), que disciplina el OCXO desde la fase del TIC por hardware.
  Seleccionable con `LA 11`; tendencia LFQ (guiado por frecuencia) / LPH (fase) /
  LLK (enganchado). Ajustable en vivo con LG/LD/LTC/LFD/LTO/LPL/LPF/LTK/LTR.
- **Autocalibración por CT para el algoritmo 11.** gain vale por defecto 0 = auto:
  el lazo deriva su escala de frecuencia de la K medida por CT (Hz por LSB de PWM),
  la misma constante que usa el algo 10, así que una CT calibra también el lazo de
  Lars. Un LG no nulo lo sobrescribe con una escala manual.
- **Adquisición guiada por frecuencia** con un término proporcional dominante y
  autofrenante, un límite de paso y anti-windup, para que un arranque en frío
  enganche sin la fuga ni la oscilación de ±2 Hz vistas durante el desarrollo.
- **Puente de captura de fase picDIV**: cuando la frecuencia está asentada pero la
  fase sigue saturada, se rearma el picDIV una vez para llevar la fase a la ventana
  del detector, donde la rama de fase completa el enganche.
- **Tuner: versionado y emparejado con el firmware.** Las herramientas llevan
  ahora un TOOL_VERSION que sigue la versión del firmware, y el tuner lee la
  versión de la placa al conectar: una discrepancia se informa en la barra de
  estado y en el monitor, en vez de manifestarse como campos que se leen raro. La
  ventana principal se abre maximizada con el splash encima, y en Windows la
  consola que aparece al hacer doble clic en el script se minimiza a la barra de
  tareas (solo cuando pertenece al tuner — una terminal abierta por el operador se
  deja en paz).
- **Tuner: pestaña de ayuda y splash de inicio.** El tuner incorpora una pestaña
  Help con la referencia completa de comandos agrupada por tema y un splash de tres
  segundos que anima dos senoides desfasadas convergiendo en una — la misma metáfora
  de enganche que la pantalla de arranque del TFT (un clic lo omite). Además lee
  todos los grupos de parámetros al conectar (LTIC, FA, PID 3-9, Lars) en lugar de
  solo dos, y el algoritmo 11 tiene su propia pestaña junto al algoritmo 10.
- **Persistencia por flash-ring para el algoritmo 11.** Todos los parámetros
  g_lars se guardan en el flash-ring junto a los ajustes LTIC (SETTINGS_VER 2);
  `ES LTIC` guarda ambos. Sin EEPROM en ningún sitio — persistencia 100% flash-ring.

### Cambiado
- **`LC` avisa cuando se ejecuta antes que `CT`.** No son independientes: `LC`
  necesita la pendiente en Hz por LSB que mide `CT`, y sin ella recurre a un valor
  genérico. El fallo es silencioso, no evidente — una placa informó ns_per_volt
  1592,8 antes de `CT` y 921,2 después, un factor de 1,7, sin nada en la primera
  ejecución que lo insinuara. `LC` ahora lo indica de entrada y continúa igual, y
  el README expone el orden explícitamente.
- **Cada ajuste indica ahora si se ha guardado.** Las preferencias que no tocan el
  lazo de control — zona horaria (`TZ`/`TO`/`LT`), offsets de sensores (`PO`/`AO`) y
  los indicadores de arranque y survey-in (`WU`/`SPL`/`SV`) — se guardan solas, y la
  respuesta nombra el grupo escrito. El ajuste del lazo sigue siendo manual, y su
  respuesta nombra el comando exacto que lo conservaría, p. ej.
  `[not saved — run 'ES LTIC' to keep it]`, así que nunca hay que adivinar el grupo.
  `SET_FLAGS` lleva `SAW` y `LRN` junto a los indicadores de arranque, así que un
  guardado automático los confirma también; el mensaje enumera todo el grupo en
  lugar de ocultarlo. Un valor rechazado se informa como tal —
  `[not saved — value out of range; accepted range shown above]` — en vez de
  ofrecer un comando `ES` para un cambio que nunca ocurrió.
- **`LT` ahora es persistente.** El comando estaba implementado pero no tenía campo
  en el bloque de ajustes, así que la elección UTC/local no sobrevivía a un
  reinicio. Añadido al grupo de zona horaria (SETTINGS_VER 4).
- **`CT` guarda su resultado automáticamente.** Igual que `LC`, la calibración de
  tres minutos escribe ahora sus coeficientes en el flash-ring al terminar con
  éxito, en lugar de confiar en que el operador recuerde `ES PID`. Solo se escribe
  el grupo PID, así que el ajuste del lazo en curso queda intacto.
- **Etiquetas de tendencia del algoritmo 11 renombradas** a ACQ / PLL / LOCK, en
  línea con el vocabulario del algoritmo 10, para que pantallas, CLI y tuner se
  lean de forma coherente. El algo 11 muestra PLL donde el algo 10 muestra DPLL,
  lo que sigue distinguiéndolos en un registro.
- **La telemetría Learn informa de lo que realmente controla cada lazo**: el algo
  11 muestra modo de ganancia / escala / fase filtrada, el algo 10 su máquina de
  estados, los algos 3-9 conservan las cifras LRN. qErr permanece en cada línea
  (compartido por ambas ramas LTIC).
- **El mensaje de CT** indica ahora que ajusta los algos 3-9 más LTIC 10 y 11.
- **Wrappers de persistencia renombrados** eeprom_* → persist_*, para reflejar que
  el almacenamiento es el flash-ring, no EEPROM; los nombres dejan de confundir.

### Corregido
- **El algoritmo 10 podía congelarse durante un enganche sano.** La protección
  contra fugas se disparaba solo con el detector saturado más un error de
  frecuencia grande — pero ese es el estado normal de un OCXO frío o alejado al
  principio de la adquisición, y congelarlo ahí elimina el único camino de vuelta,
  ya que el término de frecuencia es precisamente lo que lleva al oscilador a la
  ventana del detector. Una ejecución observada recorrió 3855 LSB durante un
  enganche DPLL perfectamente sano y quedó congelada a medio camino. Ambas
  protecciones exigen ahora además que el error haya dejado de mejorar durante
  varios ciclos (LTIC_RUNAWAY_STALL), algo que una fuga real por polaridad
  invertida provoca y una adquisición sana nunca. El umbral de saturación vuelve a
  tomarse de la calibración LC en lugar de unos 3,28 V fijos válidos para una sola placa.
- **`LIV` estaba limitado a 30 s.** Tanto el CLI como el lazo recortaban el
  intervalo de corrección LOCK a 30, y el lazo saltaba a 5 s ante un valor fuera de
  rango — así que pedir un lazo más lento daba en silencio el más rápido. Restaurado
  a 1..600 s, recortando al límite más cercano. Importaba de inmediato: un probador
  comparando LIV 30 con LIV 60 habría visto rechazado el 60.
- **Los ajustes nunca se guardaban realmente.** La cabecera del slot guardaba la
  longitud de los datos en un solo byte, así que todo lo que superara 255 B se
  truncaba: un bloque de ajustes de 324 bytes se registraba como 68. Los datos en
  sí se escribían bien y el CRC los cubría, así que nada parecía mal — pero cada
  lectura devolvía una longitud truncada, dejando la cola del bloque recuperado
  con lo que hubiera en la pila. De ahí venía el extraño `temp_coeff=-1`, y en
  cuanto la longitud se comprobó de forma estricta la lectura rechazó el registro y
  la placa arrancaba con los valores por defecto. El campo de longitud es ahora de
  16 bits (cabecera de slot 4 B → 6 B, datos 506 B → 504 B) y el magic del anillo
  se incrementa para que un anillo antiguo se reformatee en vez de decodificarse
  como basura. Esto afectaba a la rama flash-ring desde el principio — el bloque
  del propio GML ya medía 292 B, también por encima del límite.
- **Desbordamiento de pila al escribir el flash-ring.** Guardar los ajustes
  necesita unos 1,4 KB de pila — `fr_write()` construye una imagen de slot de 512
  bytes más una copia de verificación de 512 bytes, y `settings_store` añade un
  bloque de ~324 B — pero la tarea CLI tenía 1 KB y la de control 1,5 KB. La tarea
  CLI se desbordaba sobre su vecina y la placa imprimía la confirmación de guardado
  y luego se colgaba con la pantalla congelada. Ambas pilas se amplían con margen
  (CLI 1 KB → 3 KB, control 1,5 KB → 3,25 KB; 4 KB más de RAM de 128 KB). El riesgo
  es anterior al trabajo de guardado automático — `ES` estaba igual de expuesto —
  pero el guardado automático lo hizo fácil de alcanzar.
- **`EW` informaba del sector de flash equivocado.** El anillo siempre ha estado en
  el sector 7 (0x08060000, el último sector, para que el firmware conserve el
  máximo espacio contiguo por debajo), pero el mensaje de `EW` llevaba fijo
  «sector 6, 0x08040000» — el único sitio que mira un operador era el único que
  mentía. El mensaje lee ahora la dirección de la implementación mediante las
  nuevas funciones `flash_ring_sector_no()` / `flash_ring_base_addr()`, así que ya
  no puede desviarse. Los documentos de puesta en marcha tenían las mismas cifras
  obsoletas y se han corregido en los tres idiomas: el techo del firmware es
  393216 B (384 KB), no 262144 B, y el rango de borrado con J-Link para limpiar el
  anillo es 0x08060000-0x0807FFFF, no el rango del sector 6, que habría dejado el
  anillo intacto.
- **LC ya no descarta su propia convergencia.** El bucle de anulación de tasa
  paraba tras tres intentos y, si aún no había entrado en la banda de aceptación,
  recurría a `saved_pwm + offset` — que asume que el PWM guardado está en el punto
  de enganche. Ejecutado antes de que el oscilador esté cerca de 10 MHz esa
  suposición es muy falsa: una ejecución observada convergió -244, -57, -16 ns/s
  (a un paso de la banda), lo descartó y muestreó a un PWM que corría a -244 ns/s,
  donde la fase cruza toda la ventana del detector entre publicaciones. Cada
  armado del picDIV caía en la saturación y la calibración abortaba. El bucle tiene
  ahora seis intentos y, al agotarlos, conserva el PWM dirigido en vez de volver
  al inicio.
- **FA / FAD / FAL restaurados.** La ventana de promediado del término de
  amortiguación por estado (y el término `damp_e_freq` que alimenta en el
  algoritmo 10) existía en v0.97 pero no en la rama flash-ring, así que se perdió
  en la fusión. Restaurada, y ahora guardada en el flash-ring en lugar de EEPROM.
- **Los registros de ajustes verifican su longitud.** `settings_recall` y
  `settings_save_partial` aceptaban cualquier registro de dos bytes o más en un
  bloque de pila, así que un registro más corto que la estructura actual dejaba la
  cola como basura de pila — y un guardado parcial la reescribía. Ambos ponen a
  cero el bloque primero y exigen el tamaño exacto.
- **settings_store.cpp ya compila.** Leía tres variables globales que no podía
  ver — g_pressure_offset, g_altitude_offset (definidas en gpsdo_control.cpp, sin
  cabecera propia) y g_qerr_enable (declarada en ubx_timtp.h, que no estaba
  incluido). Se añadieron el include y los dos extern locales, siguiendo el patrón
  que usa el resto del proyecto.
- **Comando LT implementado.** La ayuda siempre documentó `LT 0|1` y las rutas de
  pantalla e informes siempre leyeron g_show_local_time, pero el manejador del CLI
  nunca se escribió, así que el verbo no hacía nada en silencio. Ahora conmuta e
  informa UTC frente a hora local, tal como promete la ayuda.
- **El dph del puerto serie coincide ahora con el panel.** La fila del TFT restaba
  el sawtooth del receptor pero el informe serie no, así que el mismo instante se
  leía distinto en cada uno — todo un sawtooth de diferencia (~±10 ns en un LEA-6T,
  más en un M8T). La ruta serie también lo resta ahora, como su propio comentario
  ya afirmaba.
- **CR (reinicio en frío) ahora sí borra el ring.** persist_erase() llama al nuevo
  flash_ring_wipe(), que borra y reformatea físicamente el sector del ring, de modo
  que un reinicio en frío vuelve de verdad a los valores por defecto en lugar de
  solo marcar el estado como obsoleto.

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
