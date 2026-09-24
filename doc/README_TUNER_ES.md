# GPSDO Tuner

[English](README_TUNER_EN.md) | [Polski](README_TUNER_PL.md) | **Español**

📖 [Inicio del proyecto](../README.md) · [README](README_ES.md) · Manual: [MD](MANUAL_ES.md) · [PDF](MANUAL_ES.pdf)

Una consola de escritorio para ajustar el lazo en vivo y observar lo que hace:
tres gráficas que se desplazan, una pestaña por grupo de parámetros y un cuadro
de comandos manuales para todo lo que las pestañas no cubren.

Es una **ayuda al ajuste**, no un instrumento de medida — lee *Limitaciones*
más abajo antes de sacar conclusiones de lo que muestra.

---

## Requisitos

**Python** 3.9 o posterior, más cuatro paquetes:

```
pip install PySide6 pyqtgraph pyserial tzdata
```

| Paquete | Para qué |
|---------|----------|
| PySide6 | la interfaz de usuario Qt |
| pyqtgraph | las gráficas en vivo |
| pyserial | la comunicación con la placa |
| tzdata | solo el botón **Generate gpsdo_tz_table.h** |

`tzdata` es opcional si nunca pulsas ese botón, y en Linux o macOS el sistema ya
proporciona los mismos datos de zonas. En Windows es la única fuente, ya que el
sistema no incluye base IANA. Actualízalo después con `pip install -U tzdata`.

Ejecuta con `python gpsdo_tuner.py`, o haz doble clic en Windows: la ventana de
consola que se abre se minimiza automáticamente a la barra de tareas y sigue
disponible por si hay que leer un traceback.

---

## Correspondencia de versiones

El tuner lleva un `TOOL_VERSION` que sigue la versión de firmware para la que se
escribió. Al conectar lee la versión de la propia placa y compara:

- **coinciden** — la barra de estado muestra toda la identidad de la placa, en
  la medida en que la haya ofrecido:

  ```
  connected — firmware v1.07.57rt  2026-09-23 11:02  CRC 5A41C90B
  ```

  Los tres datos responden a preguntas distintas. La **versión** dice con qué
  protocolo habla el afinador. El **build** — dentro del nombre desde el build
  56, `.57rt` (el propio build 56 escribía `-rt56`); un firmware anterior
  (`v1.07-rtos`) lo envía aparte y la línea
  muestra `build N` — y la hora de compilación dicen de qué árbol de fuentes
  salió. El **CRC** dice qué binario está corriendo realmente, y
  es el único que no puede quedarse obsoleto: la placa lo calcula de su propia
  flash al arrancar, así que sigue siendo honesto aunque el compilador de Arduino
  reutilice un objeto y la marca de tiempo no lo refleje. Todo lo que va después
  de la versión es opcional: un firmware antiguo responde a `V` solo con el
  nombre y la línea simplemente dice menos.
- **discrepan** — la barra de estado y el monitor en bruto lo indican

Una discrepancia no es fatal y el tuner seguirá hablando con la placa, pero es
de esperar que algunos campos se lean raro o que se rechacen comandos: un tuner
antiguo no conoce la telemetría nueva, y uno nuevo puede enviar verbos que la
placa nunca ha oído. Usa el par que se distribuyó junto.

---

## Pestañas

| Pestaña | Función |
|---------|---------|
| **PID algo 3-9** | Kp / Ki / Kd / I_LIMIT para los algoritmos en el dominio de la frecuencia |
| **LTIC (algo 10)** | PID por etapa del lazo de fase de tres etapas, más la calibración del detector y la ventana de promediado de amortiguación (`FAD` / `FAL`, por etapa) |
| **LTIC-Lars (algo 11)** | Los parámetros del PI continuo (`LG`, `LD`, `LTC`, …) |
| **LTIC-MLA (algo 12)** | Los dos escalares (`MG`, `MR`) y los once límites de fase por nivel |
| **Calibration** | `LC`, `CT` y las constantes del detector |
| **Raw monitor** | Todo lo que envía la placa, sin analizar |
| **Help** | La referencia completa de comandos del firmware |

Todos los grupos de parámetros se leen al conectar, así que los paneles arrancan
poblados en vez de vacíos. Las pestañas siguen los números de los algoritmos.
Los comandos que describen la placa y no al lazo — las escalas del camino de
salida (`DV`, `AV`, `VS`), el puente de span del EFC (`SPAN`), la
retroiluminación (`BL`) — no tienen pestaña propia: van por la caja de
comandos, y la pestaña **Help** los documenta con el resto.

---

## Gráficas

Tres paneles, actualizados una vez por segundo. Lo que muestran los dos
superiores depende del algoritmo que reporte la placa:

| | Algoritmos 10 / 11 (LTIC) | Algoritmo 12 | Algoritmo 13 | Algoritmos 0-9 |
|---|---|---|---|---|
| Superior | Fase `dph` (ns) | Error de fase `ph` (ns) | **Estimación** de fase `ph` (ns) | Deriva aprendida (LSB) |
| Central | `Vphase` del detector (V), con guías de banda | Tensión de control `Vctl` (V) | `Vphase` del detector (V), con guías de banda | Tensión de control `Vctl` (V) |
| Inferior | Error de frecuencia (Hz) | Error de frecuencia (Hz) | Error de frecuencia (Hz) | Error de frecuencia (Hz) |

Solo los lazos LTIC tienen detector de fase, así que bajo cualquier otro
algoritmo esos dos paneles quedarían vacíos toda la sesión. En su lugar se
reorientan a otras magnitudes y los títulos se ajustan solos — no hay nada que
configurar.

El algoritmo 12 recibe su propia pareja en lugar de tomar prestada ninguna de las
otras: no usa la realimentación anticipativa autoaprendida, así que la traza de
deriva saldría plana, y su fase viene directamente del detector y no a través de
un filtro de lazo, de modo que no es la misma magnitud que dibuja `dph`. Las
guías de banda del detector desaparecen siempre que el panel central muestra en
su lugar una tensión de control.

El algoritmo 13 dibuja lo que el filtro de Kalman CREE que es la fase, no la
lectura de este segundo — esa estimación es justamente el sentido de tener un
filtro — y pone `Vphase` debajo, porque la pregunta que este lazo plantea más a
menudo es si el detector está vivo. Al principio caía en la pareja 0-9, así que
el panel superior se titulaba "Deriva aprendida" sobre una serie que el
algoritmo 13 nunca envía.

### Span y Follow

**Span** fija cuánta historia se ve: 1 min, 5 min, 15 min, 1 h o *all*. Con un
span seleccionado la traza se desplaza hacia la izquierda a escala constante, en
lugar de que el eje se estire para cubrir todo el búfer.

**Follow live** mantiene la ventana anclada a la muestra más reciente. Arrastra
o usa la rueda sobre cualquier gráfica y se desmarca sola, cediendo el eje al
ratón para poder recorrer todo el búfer; vuelve a marcarla — o cambia el Span —
para saltar de nuevo al modo en vivo.

**Clear plots** descarta todas las muestras almacenadas y reinicia el eje de
tiempo a cero — útil tras un arranque fallido y más rápido que reiniciar la
herramienta, lo que además cortaría la conexión. Borra los búferes además de las
trazas, así que después no vuelve a aparecer nada.

**About** reproduce la animación de arranque, sin más motivo que el placer.

---

## Limitaciones

**El historial está limitado a una semana.** El tuner guarda 604 800 muestras al
ritmo de telemetría de 1 Hz. Todo lo más antiguo se descarta a medida que llegan
datos nuevos y no se puede recuperar; nada de las gráficas se escribe en disco.
Los búferes son arrays de dobles y no listas de floats de Python, así que una
semana completa de todas las series cuesta unos 82 MB de RAM en vez de 406 — y
nada se reserva por adelantado, de modo que una sesión de cinco minutos sigue
costando kilobytes.

**Las gráficas no son un registrador.** Los datos graficados viven solo en
memoria y se pierden al cerrar la ventana. Para lo que quieras conservar usa
**Start logging** (pestaña Raw monitor) — véase más abajo.

### Qué escribe el registro

El desplegable junto a **Start logging** elige el formato, y queda fijado
mientras el archivo esté abierto:

| Ajuste | Escribe | Aprox. una semana |
|---|---|---|
| **Full log** | cada línea recibida, tal cual se imprimió, en `gpsdo_AAAA-MM-DD_HH-MM-SS.log` | ~217 MB |
| **CSV only** | una fila por segundo de telemetría, sólo columnas de análisis, en `…​.csv` | ~65 MB |
| **Both** | la misma captura escrita en ambos archivos | ~282 MB |

Ambos se abren junto al script con búfer por líneas, de modo que una ejecución
que termine mal deja datos utilizables y no un archivo vacío de búferes sin
volcar.

El **log completo** es el texto crudo de telemetría — todo lo que dijo la placa,
incluidas las respuestas de la CLI y los banners de arranque. Es el formato para
quien va a *mirar* el ensayo, no a calcular sobre él, y el único que conserva
cualquier cosa para la que el CSV no tenga columna.

El **CSV** es para calcular: `pandas.read_csv` y `numpy.loadtxt` lo leen con sus
ajustes por defecto, porque las dos líneas de procedencia empiezan por `#`. Las
columnas son lo que todo análisis de estos registros ha necesitado realmente, no
todo lo que imprime el firmware:

```
utc, up_s, algo, state, dph_ns, qerr_ns, vphase_v, pwm, f10, f100,
ph_ns, level, corr, sig_ns, zc, bmp_c, sat, hdop
```

`ph_ns`, `level`, `corr`, `sig_ns` y `zc` son los diagnósticos del algoritmo 12 y
quedan vacíos con cualquier otro algoritmo; `f100` está vacío hasta que se llena
la ventana de 100 s. Una celda vacía significa siempre *ese campo no estaba en la
telemetría de ese segundo*, nunca cero.

Tres columnas merecen una nota:

- **`up_s` sólo es fiable a partir del firmware v1.06.** Medido sobre 75 055
  bloques capturados con v1.05: UTC avanzó exactamente un segundo todas y cada
  una de las veces, mientras que el contador de uptime repitió o saltó un
  segundo 118 veces (0,16 %) y ganó 12 s en 20,8 h. Se contaba con un
  temporizador libre del MCU que va unos 159 ppm rápido; v1.06 lo cuenta con el
  PPS. El sintonizador escribe ambas columnas tal como llegaron y no repara
  ninguna — un registrador que arregla su entrada en silencio no sirve para
  encontrar cosas así —, así que en una captura de v1.05 o anterior usa `utc`.
- **`hdop` no siempre es un número.** Un LEA-T que ha completado el survey-in
  imprime `HDOP:TIME`, y esa bandera es el más útil de los dos datos — es el modo
  en que el 1PPS merece confianza. Analiza la columna con `errors="coerce"` si la
  quieres numérica.
- **`vphase_v`** es la tensión cruda de la rampa del detector, y la única columna
  que revela un detector contra el tope. Un `dph_ns` calculado desde una rampa
  topada parece un número corriente.

Omitido a propósito: `Vctl` (es `pwm` a través de una red RC, y `pwm` es la cifra
exacta), humedad, presión y los raíles del INA (en todas las capturas hasta ahora
nunca se han movido lo bastante como para explicar nada). **No hay columnas de
posición en absoluto**, así que un CSV está redactado por construcción diga lo
que diga la casilla.

**Redact position** (junto al botón de registro, activado por defecto) se aplica
al **log completo** — el CSV no tiene columnas de posición que redactar.
Sustituye `Lat` / `Lon` / `Alt` del receptor por marcadores **sólo en el archivo
guardado** — el Raw monitor y las gráficas siguen mostrando tu fix real. El
número de satélites, el HDOP y el indicador TIME se conservan: son diagnósticos
y no dicen nada sobre dónde estás.

Un registro de telemetría es justo lo que acaba en un foro o en manos de quien
se ofreció a medir tu placa, y cada segundo lleva una posición con seis
decimales — unos diez centímetros. Limpiarlo después funciona, pero depende de
acordarse, y la única vez que se olvida es la vez en que el archivo ya se envió.
El ajuste queda fijado al abrir el archivo y la casilla se deshabilita hasta que
el registro termina, de modo que un log está entero redactado o entero sin
redactar; uno redactado a medias parece seguro de un vistazo y no lo es. En
cualquier caso el propio log lo dice, en su segunda línea.

**Generate gpsdo_tz_table.h** reconstruye la tabla de zonas horarias del firmware a
partir de los datos IANA de esta máquina y escribe `gpsdo_tz_table.h` junto al script.
Sustituye al antiguo `gen_tz_table.py`, así que el tuner es ya el único script
que mantener.

Los datos de zonas provienen de la base del sistema en Linux/macOS, o del
paquete `tzdata` de Python — que es como funciona en Windows, donde el sistema
no incluye base IANA alguna. Si el botón informa de que no hay datos, ejecuta
`pip install tzdata`; para actualizarlos después, `pip install -U tzdata`. La
cabecera generada anota de qué versión de IANA procede, cuando puede
determinarse.

> La propia IANA publica *fuentes* que requieren el compilador `zic`, así que
> descargar directamente de ellos no ayudaría — el paquete `tzdata` son los
> mismos datos ya compilados.

**El panel Raw monitor guarda solo las últimas ~2500 líneas** (menos de cinco
minutos al ritmo de telemetría). Es un límite de visualización, no de registro:
una vez iniciado el registro el archivo recibe todo, independientemente de lo que
el panel siga mostrando.

**La resolución es el ritmo de telemetría.** Una muestra por segundo, así que
cualquier cosa más rápida que unos 2 s es invisible: un ciclo límite rápido o el
jitter pulso a pulso no aparecerán, y lo que ves ya viene promediado desde el
firmware.

**Una conexión cada vez.** El puerto serie es exclusivo. Cierra antes cualquier
otro terminal sobre el mismo puerto, y recuerda que el tuner lo retiene mientras
está abierto.

**Las gráficas se fían de la placa.** Los valores se extraen del texto de
telemetría tal como llega. Si el firmware reporta una cifra obsoleta o errónea,
el tuner la dibuja fielmente — no verifica nada de forma cruzada.

**Las escrituras no son persistentes.** Fijar un parámetro lo cambia solo en
RAM. Los parámetros de ajuste del lazo requieren un `ES` explícito (la respuesta
nombra el comando exacto); las preferencias se guardan solas y lo indican.

---

*Parte de GPSDO FreeRTOS — [manual del firmware](README_ES.md) · [changelog](CHANGELOG_ES.md) · [repositorio](https://github.com/jmnlabs/GPSDO_FreeRTOS)*
