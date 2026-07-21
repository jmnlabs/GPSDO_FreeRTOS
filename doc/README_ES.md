# GPSDO FreeRTOS v0.95

[English](README_EN.md) | [Polski](README_PL.md) | **Español**

📖 [Inicio del proyecto](../README.md)

Firmware en tiempo real (FreeRTOS) para un oscilador disciplinado por GPS
(GPSDO) sobre la plataforma STM32 BlackPill (WeAct F411CE / F401CCU6).

📋 Historial de versiones: [Registro de cambios](CHANGELOG_ES.md)

## Créditos

| Rol | Persona / fuente |
|-----|------------------|
| Autor del port a FreeRTOS, algoritmos 3–10 | **J. M. Niewiński** — [repositorio](https://github.com/jmnlabs/GPSDO_FreeRTOS) |
| Asistente de programación (Anthropic) | **Claude AI** |
| Autor de v0.06c — inspiración del port RTOS | **André Balsa** — [repositorio](https://github.com/AndrewBCN/STM32-GPSDO) |
| Diseño de PCB (prototipo) | **Scrachi** (foro EEVBlog) — [mensaje con archivos](https://www.eevblog.com/forum/projects/yet-another-diy-gpsdo-yes-another-one/825/) · [perfil](https://www.eevblog.com/forum/profile/?u=762266) |
| Hilo del proyecto | [Yet another DIY GPSDO](https://www.eevblog.com/forum/projects/yet-another-diy-gpsdo-yes-another-one/) — Foro EEVBlog |

Este firmware se escribió desde cero como un port del código original de
André Balsa a la arquitectura FreeRTOS, con un rediseño completo de tareas,
sincronización y manejo de pantallas. El diseño de hardware se basa en el
esquemático v0.06c, usando las PCB compartidas por el usuario Scrachi en el
foro EEVBlog.

---

## Descripción del proyecto

Un GPSDO (oscilador disciplinado por GPS) es una fuente de frecuencia de
precisión de 10 MHz en la que un oscilador de cristal con horno (OCXO) se
disciplina mediante la señal 1PPS de un receptor GPS. Esto logra una
exactitud a largo plazo del orden de 10⁻¹⁰–10⁻¹², preservando a la vez la
estabilidad a corto plazo del OCXO.

### Principio de funcionamiento del hardware

```
                                            10 MHz
               ┌─────────────┐       ┌──────────────┐
   Antena GPS  ┤  u-blox     │       │    OCXO      ├── TIM2 ETR (PA15) ──┐
               │  NEO-6M/7M  │       │  10 MHz      │                     │
               └──┬──────┬───┘       └──────▲───────┘                     │
                  │      │                  │                             │
        NMEA      │  1PPS (PB10)      PWM (PB9)                           │
     (Serial1)    │      │            + filtro RC                         │
                  │      │                  │                             │
               ┌──▼──────▼──────────────────┴───────┐                     │
               │           STM32 F411CE             │◄────────────────────┘
               │           BlackPill                │
               └───┬─────────┬─────────┬───────┬────┘
                   │         │         │       │
                bus I2C    SPI1     Serial2  GPIO
                   │         │         │       │
        ┌──────┬───┼───┬─────┤         │    TM1637
        │      │   │   │     │         │    (reloj,
      OLED   LCD  HT16K33  TFT        BT     PA8/PB4)
     128x64  20x4 (reloj)  │         HC-06
        │              ┌───┴────────┐
     Sensores:         │ ILI9341 /  │  320x240
   ┌────┼────┐         │ ST7789     │
  AHT  BMP  INA        │ ILI9488    │  480x320
                       └────────────┘
```

**El lazo de control** funciona de la siguiente manera:

1. El OCXO genera una señal de 10 MHz que se inyecta en TIM2 ETR (PA15).
   El contador de 32 bits de TIM2 cuenta ciclos del OCXO de forma continua.
2. La señal 1PPS del GPS dispara una interrupción de captura en TIM3 (PB10).
   La ISR lee el valor actual de TIM2 — la diferencia entre dos capturas
   consecutivas da el número de ciclos del OCXO en exactamente un segundo GPS.
3. Las mediciones se promedian en ventanas de 10 s, 100 s, 1000 s y 10000 s
   mediante un búfer circular (20000 muestras).
4. El algoritmo de control (PID, escalón o híbrido) calcula una corrección PWM.
5. Un DAC PWM de 16 bits (PB9) controla la tensión Vctl aplicada a la entrada
   EFC del OCXO a través de un filtro RC doble (20 kΩ / 10 µF, τ ≈ 200 ms).

**Sensores** (opcionales, I2C):

- **AHT10/20** — temperatura y humedad del recinto
- **BMP280** — temperatura y presión atmosférica
- **INA219** — tensión de alimentación y consumo de corriente del OCXO

**Pantallas** (opcionales):

- **OLED 128×64** I2C (SH1106 / SSD1306 / SSD1309) — páginas rotativas; una
  tercera página muestra el estado del lazo algo-10, la fase y el aviso LPOL?
  cuando LTIC está activado
- **LCD 20×4** I2C (HD44780 + PCF8574T) — la línea alternante añade un modo con
  estado algo-10 / fase / LPOL? cuando LTIC está activado
- **TM1637** (reloj de 4 o 6 dígitos)
- **TFT 320×240** SPI (ILI9341 / ST7789, biblioteca TFT_eSPI)
- **TFT 480×320** SPI (ILI9488, biblioteca TFT_eSPI) — el panel principal en
  uso actual, con disciplinado a LOCK confirmado en el hardware de varios
  constructores. El diseño se dibuja de forma nativa a 480×320 (no solo
  escalado): una rejilla de seis campos de sensores con unidades alineadas a la
  derecha, la lectura de fase de algo-10 (Vph / dph con guardián de banda `ovf`
  / qErr) y una barra de estado con SURVEY y el aviso de polaridad LPOL?
- **HT16K33** reloj de 7 segmentos de 4 dígitos con dos puntos, dirección
  I2C 0x70 (HH:MM)

OLED y LCD pueden funcionar simultáneamente (direcciones I2C distintas).
LCD y TM1637 **no pueden** funcionar simultáneamente (conflicto de bus).

---

## Arquitectura de software

El firmware se ejecuta bajo FreeRTOS con siete tareas en niveles de prioridad
estrictamente definidos:

| Prioridad | Tarea | Pila | Función |
|-----------|-------|------|---------|
| Máxima | `vFreqRelayTask` | 768 B | Procesado de PPS, búfer circular de frecuencia |
| Alta | `vControlTask` | 1.5 KB | Calentamiento del OCXO, calibración, algoritmo PID, ADC |
| Media-alta | `vGpsTask` | 1.5 KB | Análisis NMEA (TinyGPS++), configuración UBX |
| Media | `vCliTask` | 1 KB | Analizador de comandos Serial / Bluetooth |
| Media-baja | `vSensorTask` | 1.5 KB | Lectura AHT/BMP/INA cada 2 s |
| Baja | `vDisplayTask` | 4 KB | OLED, LCD, TM1637, informe serie, LEDs |
| Mínima | `vUptimeTask` | 768 B | Contador de tiempo de actividad (dd hh:mm:ss) |

**El estado compartido** está protegido por mutexes de FreeRTOS:

- `xFreqMutex` — datos de frecuencia (`gFreq`, `gFreqSnap`)
- `xGpsMutex` — datos GPS (`gGps`)
- `xCtrlMutex` — datos de control (`gCtrl`: PWM, algoritmo, holdover, tendencia)
- `xUptimeMutex` — tiempo de actividad (`gUptime`)
- `xWireMutex` — bus I2C (compartido por sensores y pantallas)
- `xSerialMutex` — puerto serie / Bluetooth

---

## Algoritmos de control

Once algoritmos seleccionables mediante el comando `LA n` (0–10):

| Algo | Tipo | Entrada | Periodo | Descripción |
|------|------|---------|---------|-------------|
| 0 | Escalón | avg100/1k | ~429 s | Por defecto — simple, robusto |
| 1 | Deriva | — | 1000 s | Solo medición de deriva del OCXO |
| 2 | Aleatorio | — | 5 s | Medición del piso de ruido — diagnóstico |
| 3 | FLL PID | avg100 | 100 s | Uso general, conservador |
| 4 | PLL PI+D | fase real | 10 s | Bajo ruido; Kd = amortiguación de frecuencia (requerido) |
| 5 | PLL PID | fase real | 10 s | Equilibrado: velocidad + ruido |
| 6 | FLL PID (GA) | avg100 | 100 s | Coeficientes optimizados genéticamente |
| 7 | PLL PID (GA) | fase real | 10 s | Coeficientes optimizados genéticamente |
| 8 | Híbrido | FLL+PLL | 100 s | Mezcla sigmoide automática FLL↔PLL |
| 9 | Red neuronal | e/∫e/de + temp | 10 s | MLP de 5 entradas; aprende el tempco del oscilador, holdover compensado térmicamente |
| 10 | LTIC | fase TIC + frec. | por etapas | Tres etapas ACQ→DPLL→LOCK; detector de fase por hardware, autocalibrante |

Los algoritmos PLL (4, 5, 7 y la rama PLL del 8) usan un diseño de **dos
escalas temporales** ajustado para "captura rápida, mantenimiento de fase
suave":

- el término dominante actúa sobre el **error de frecuencia** (Kp ≈ 0.4/K),
  llevando la frecuencia al objetivo con rapidez y sin sobreoscilación;
- pequeños términos de fase (Kd proporcional, Ki integral sobre la fase
  acumulada) eliminan la deriva lenta con pasos diminutos.

Cada corrección pasa por una etapa de salida común que aplica un **límite de
velocidad de cambio** (máx. ~12 LSB/paso para los PLL, 40 para el híbrido) y
una **zona muerta** cerca del enganche. El límite de velocidad reparte una
gran deriva de fase nocturna en varios periodos en lugar de un salto PWM
grande que perturbaría el OCXO; la zona muerta permite que el PWM permanezca
quieto en régimen permanente para que el OCXO funcione libremente con su
propia estabilidad a corto plazo.

Los algoritmos 3–9 tienen parámetros PID ajustables en tiempo de ejecución
(`Kp`, `Ki`, `Kd`, `I_LIMIT`) configurables mediante comandos CLI (`KP`,
`KI`, `KD`, `IL`) — sin recompilar. Los parámetros se guardan en EEPROM con
`ES`.

---

## Diseño de la pantalla OLED (128×64 px, 16 caracteres × 8 filas)

Durante 2 segundos tras el arranque, la fila 0 muestra la versión del
firmware. Luego cambia a un reloj de hora local. Dos páginas se alternan cada
`OLED_PAGE_SWITCH_SECS` segundos (por defecto 10 s):

```
── Fila 0 (común): LMT:14:32:45 Mon  ← hora local + día de la semana
── Fila 1 (común): F 9999999.9999Hz   ← frecuencia + Hz en posiciones 14-15
──── PÁGINA A (GPS) ─────────────────────────────────────────
Fila 2: La  52.12345             ← latitud
Fila 3: Lo  23.12345             ← longitud
Fila 4: Al  175m Sat: 9          ← altitud + satélites
Fila 5: Up 000d 00:00:00         ← tiempo de actividad
Fila 6: 12:34:56  23.4C          ← UTC + temperatura AHT
──── PÁGINA B (sensores) ────────────────────────────────────
Fila 2: BM:23.4C 1013hPa        ← BMP280
Fila 3: AH:22.1C 45.3%rH        ← AHT20
Fila 4: IN:12.05V  250mA        ← INA219
Fila 5: Sat:09 HDOP:0.90        ← calidad GPS
Fila 6: UTC:14:32:45 Mon        ← hora UTC + día
──── Ambas páginas ──────────────────────────────────────────
Fila 7: PWM:40908 hit H          ← PWM + tendencia + holdover (parpadeo H/A)
```

Indicación de holdover en la fila 7: `H` (manual) o `A` (automático — señal
perdida).

---

## Diseño de la pantalla LCD 20×4

Pantalla de versión durante 2 segundos, luego:

```
Línea 0: F:  10000000.0000 Hz     ← frecuencia (20 car., alineada a la derecha)
Línea 1: UTC:14:32:45 Up 000d     ← hora UTC + días de actividad
Línea 2: [vista rotativa]         ← ver tabla abajo
Línea 3: PWM:40908 V:1.65 hit     ← PWM + Vctl + tendencia / holdover
```

La línea 2 rota cada `LCD_LINE2_SWITCH_SECS` segundos:

| Modo | Contenido | Ejemplo |
|------|-----------|---------|
| 0 | Coordenadas GPS + satélites | `La:52.123 Lo:23.123 S: 9` |
| 1 | Satélites + HDOP | `Sats: 9  HDOP:0.90` |
| 2 | Fecha + día + hora local | `02/06/2026 Mon 14:32` |
| 3 | AHT20 | `AHT:22.1C  45.3%rH` |
| 4 | INA219 | `INA:12.05V   250mA` |
| 5 | BMP280 | `BMP:23.4C 1013.2hPa` |

Holdover en la línea 3: `[H]` (manual) o `[A]` (automático) — parpadeo 500 ms.

---

## Diseño de la pantalla TFT (ILI9341 / ST7789 320×240, ILI9488 480×320, TFT_eSPI)

Se admiten módulos TFT SPI económicos en orientación horizontal, controlados
por SPI1 por hardware: **ILI9341** y **ST7789** a 320×240, e **ILI9488** a
480×320. Los tres comparten el mismo cableado en `User_Setup.h` — cambiar de
panel solo requiere cambiar la definición del driver y el ancho/alto. Las
líneas `TFT_RGB_ORDER` / `TFT_INVERSION_OFF` son necesarias para colores
correctos en módulos ST7789 e inofensivas en los demás. Independiente de las
pantallas I2C — OLED, LCD y TFT pueden funcionar a la vez.

> **El soporte de ILI9488 / ILI9486 480×320 está verificado en panel (v0.93).**
> La pantalla de operación 320×240 y el splash se escalan automáticamente a
> 480×320 en tiempo de compilación (ancho ×1.5, alto ×1.33). El panel grande
> dibuja todo su texto con las fuentes libres Adafruit GFX, lo que corrigió los
> síntomas vistos en las fotos de los primeros usuarios (Dan Wiering, lucido) —
> el subtítulo del splash reduciéndose a una sola «p» y la barra de estado
> apareciendo en blanco eran ambos por la falta de letras en las fuentes GLCD
> numéricas, no un problema de escalado. La geometría de las bandas (frecuencia,
> rejilla, sensores, estado) se recalculó frente a las filas más altas de las
> fuentes proporcionales y se comprobó en un panel ILI9488 real, para que ninguna
> fila cruce un separador en ninguno de los tamaños.
>
> El ILI9488/ILI9486 por SPI mueve 2,4× los píxeles de un panel 320×240 con color
> de 18 bits, lo que antes hacía visible cada redibujado. Desde la v0.93 las
> regiones vivas usan doble búfer con sprites y se envían en una transferencia
> cada una, así que el redibujado ya no parpadea — vea [Sprites: por qué la
> pantalla dejó de parpadear](#sprites-por-qué-la-pantalla-dejó-de-parpadear).
> Use SPI a 40 MHz en este panel.
>
> Los paneles 320×240 mantienen las fuentes numéricas clásicas en la pantalla de
> trabajo — los tipos GFX son demasiado anchos para esa maquetación. Vea [Por qué
> el panel pequeño mantiene las fuentes
> clásicas](#por-qué-el-panel-pequeño-mantiene-las-fuentes-clásicas).

**Cableado (SPI1 por hardware):**

| Pin TFT | Pin STM32 |
|---------|-----------|
| SCK | PA5 (SPI1 SCLK) |
| SDI | PA7 (SPI1 MOSI) |
| RES | PB15 |
| D/C | PB12 |
| CS | PB13 |

**Disposición de la pantalla:**

```
┌────────────────────────────────────────────┐
│ v0.95-rt      GPSDO      LMT 14:32:45 Thu   │ ← header bar (navy)
├────────────────────────────────────────────┤
│                                            │
│        10000000.0000 Hz                    │ ← frequency (large, colour-coded)
│                                            │
├────────────────────────────────────────────┤
│ UTC: 12:32:45 Thu    │ Sat:  9 HDOP: 0.90  │
│ DATE: 11/06/2026     │ Lat: 52.123456      │
│ Uptime: 000d 02:15:33│ Lon: 23.123456      │
│ Algo: 5 hit          │ Alt:  175m          │
│ PWM:44653 Vct:1.970V │ INA: 12.050V 250mA  │
├──────────────────────┼─────────────────────┤
│ BMP: 23.40C 1013.2hPa│ AHT: 22.10C 45.3%rH │
│ Vph: 2.615V +830ns   │ Vdd: 3.30V          │
├────────────────────────────────────────────┤
│          DISCIPLINED  FIX OK               │ ← status bar (colour-coded)
└────────────────────────────────────────────┘
```

**Código de colores:**

| Elemento | Color | Significado |
|----------|-------|-------------|
| Frecuencia | verde | enganchado — mejor promedio dentro de 1e-10 (10000s) o 1e-9 (1000s) de 10 MHz |
| Frecuencia | blanco | ajustando |
| Frecuencia | naranja | holdover |
| Frecuencia | rojo | sin señal |
| Barra de estado | verde | disciplinado, señal OK |
| Barra de estado | naranja | holdover manual |
| Barra de estado | rojo | holdover automático (señal perdida) / esperando señal |

Las actualizaciones son selectivas — cada celda de valor almacena su cadena
anterior y solo se redibuja al cambiar, manteniendo mínimo el tráfico SPI al
refresco de 1 Hz.

**Configuración de la biblioteca TFT_eSPI (obligatoria):**

TFT_eSPI se configura en la *biblioteca*, no en el sketch. Edite
`Arduino/libraries/TFT_eSPI/User_Setup.h` para que contenga:

```c
#define ST7789_DRIVER          // o ILI9341_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 320
#define TFT_MISO PA6      // requerido en STM32 aunque la pantalla no tenga MISO
#define TFT_MOSI PA7
#define TFT_SCLK PA5
#define TFT_CS   PB13
#define TFT_DC   PB12
#define TFT_RST  PB15
#define TFT_RGB_ORDER TFT_BGR   // orden de color Azul-Verde-Rojo
#define TFT_INVERSION_OFF       // corrige colores invertidos en algunos ST7789
#define LOAD_GLCD               // fuente clásica 1 — frecuencia + créditos de la splash
#define LOAD_FONT2              // fuente clásica 2 — cabecera + rejilla de datos
#define LOAD_FONT4              // fuente clásica 4 — barra de estado, mensajes, subtítulo splash
#define SPI_FREQUENCY 40000000  // el SPI1 del F411 llega a 50 MHz; 40 deja margen
```

> **La compilación 320×240 no necesita `LOAD_GFXFF`.** Todo en este panel — la
> splash incluida — se dibuja con las fuentes numéricas clásicas, así que las
> tres líneas `LOAD_` de arriba son todo lo necesario. Es deliberado: vea [Por
> qué el panel pequeño mantiene las fuentes
> clásicas](#por-qué-el-panel-pequeño-mantiene-las-fuentes-clásicas). Si
> actualiza desde una versión anterior, su `User_Setup.h` casi seguro ya las
> tiene.

Para el panel **ILI9488 / ILI9486 (480×320)**, cambie el driver y las
dimensiones, y añada `LOAD_GFXFF` — el panel grande dibuja su cabecera,
rejilla, barra de estado y frecuencia con las fuentes libres Adafruit GFX. El
firmware elige los tamaños de punto automáticamente en tiempo de compilación
(ver las macros `GF_*` en `gpsdo_config.h`):

```c
#define ILI9488_DRIVER          // funciona para paneles ILI9488 e ILI9486
#define TFT_WIDTH  320
#define TFT_HEIGHT 480
// ...las mismas líneas TFT_MISO/MOSI/SCLK/CS/DC/RST/RGB_ORDER de arriba...
#define LOAD_GLCD               // fuente 1 — créditos de la splash
#define LOAD_FONT4              // fuente 4 — se mantiene para la ruta del subtítulo
#define LOAD_GFXFF              // REQUERIDO en este panel — fuentes GFX
#define SPI_FREQUENCY 40000000  // 480×320 mueve 2,4× los píxeles — no escatime aquí
```

> **Reloj SPI.** 40 MHz es el ajuste probado en el F411 (el SPI1 llega a
> 50 MHz, así que esto deja margen para un cableado que no sea ideal). Importa
> sobre todo en el panel 480×320: un redibujado a pantalla completa mueve 2,4×
> los píxeles del panel pequeño, y los envíos de sprite (más abajo) son
> transferencias continuas únicas cuya duración escala directamente con el
> reloj. Si su panel muestra artefactos, baje a 27 MHz — los cables largos
> pueden no soportar 40.

Luego active `GPSDO_TFT_ST7789`, `GPSDO_TFT_ILI9341` o `GPSDO_TFT_ILI9488` en
`gpsdo_config.h`.

### Por qué el panel pequeño mantiene las fuentes clásicas

La v0.92 pasó todas las pantallas a las fuentes libres Adafruit GFX. En el panel
480×320 fue una mejora clara: las letras están bien formadas, la maquetación
tiene aire, y `FreeMonoBold` mantiene los dígitos de la frecuencia en columnas
fijas.

En 320×240 se probó el mismo cambio y se **revirtió en la v0.93**. Los tipos GFX
son proporcionales y bastante más anchos que las fuentes numéricas para las que
se diseñó la maquetación, y 320 px sencillamente no tiene sitio para esa
diferencia: los valores se salían de sus columnas hacia la vecina
(`Uptime: 000d 00:01:03n: ---`, `PWM:44778 Vct:1.9INA: 4.888V 224.5`), y el
divisor central cortaba lo que desbordaba. Reducir la fuente tampoco era opción
— 9 pt es la FreeSans *más pequeña* que trae TFT_eSPI, y lo único por debajo es
TomThumb (3×5 px), ilegible a distancia de brazo.

Así que el panel pequeño se queda con lo que cabe: fuente clásica 2 para la
cabecera y la rejilla, fuente 4 para la barra de estado, fuente 1 a ×3 (18×24,
ancho fijo) para la frecuencia. La splash sigue el mismo camino — su subtítulo
usa la fuente 4, que lleva el alfabeto completo (las fuentes 6/8 son las que no
tienen letras, y eran las que convertían «GPS Disciplined OCXO» en una sola
«p»), y los créditos la fuente 1. Ese era el último reducto de GFX, y
eliminarlo significa que **una compilación 320×240 no necesita `LOAD_GFXFF` en
absoluto** — quien actualice desde una versión anterior puede dejar
`User_Setup.h` como está. Las macros `TFT_FONT_*` en `gpsdo_config.h` hacen la
elección en tiempo de compilación; hay una maquetación, no dos.

El **divisor central de columnas** es igualmente solo de 480: a 320 px las
columnas llegan hasta el medio y la línea no tenía por dónde pasar sin cruzar
texto.

### Sprites: por qué la pantalla dejó de parpadear

El panel se escribe por SPI, así que todo lo que se dibuja directamente en él se
*ve* dibujarse. El código antiguo borraba antes de escribir: `setTextPadding`
rellenaba unos 480×34 px de fondo, y luego el texto nuevo caía encima. A una
actualización por segundo, ese ciclo borrar-y-dibujar se veía claramente como un
parpadeo en la banda de frecuencia — peor en el panel 480×320, donde el borrado
cubre 2,4× los píxeles.

La v0.93 almacena en su lugar las tres regiones vivas en RAM — cabecera, banda
de frecuencia y área de datos — como objetos `TFT_eSprite`. Cada redibujado
limpia y repinta su sprite invisiblemente en RAM, y luego envía la banda
terminada al panel en **una sola transferencia SPI continua**. No hay estado
intermedio en el cristal, así que no hay nada que parpadee. El marco y los
separadores quedan fuera de los límites del sprite (o, cuando un separador cruza
una banda, se dibujan dentro del sprite y se envían con él), así que nunca se
tocan.

La memoria es modesta gracias a las paletas — 4 bits para las bandas de cabecera
y frecuencia, 1 bit para el área de datos, ~25 KB en total en el panel de 480,
holgadamente dentro de los 128 KB del F411. El marco viaja dentro de los sprites
en vez de dibujarse en el panel — y por eso es blanco en ambos tamaños. El
sprite de datos es de 1 bit, así que sus únicos dos colores son el blanco y el
fondo: un marco azul marino no podría dibujarse dentro de él y habría que
repintarlo en el panel tras cada envío, echando por tierra todo el propósito. El
blanco mantiene marco y texto en la misma transferencia atómica.

Si `createSprite()` llegara a fallar (montón fragmentado), cada banda recurre a
dibujar directamente en el panel: vuelve el parpadeo antiguo, pero nada se
rompe. El registro de arranque indica qué ruta está activa:

```
TFT: freq-band sprite (4-bit) created
TFT: header sprite (4-bit) created
TFT: data sprite (1-bit) created
```

---

## Señalización por LED

| LED | Pin | Función |
|-----|-----|---------|
| Azul (integrado) | PC13 | Parpadea cada 1PPS — latido |
| Amarillo | PB8 | Ver tabla de estados abajo |

**Máquina de estados del LED amarillo:**

| Estado | Condición | Señalización |
|--------|-----------|--------------|
| Sin señal GPS | Tras arranque o falta persistente de señal | APAGADO |
| Señal OK, modo disciplinado | Operación normal | ENCENDIDO fijo |
| Holdover manual (`MH`) | Usuario activó holdover | Pulso lento 1000 ms |
| Holdover automático | Señal perdida durante operación | Pulso rápido 200 ms |

---

## Sincronización picDIV

El picDIV opcional (familia PD11/PD13/PD17 de Tom Van Baak, leapsecond.com)
divide los 10 MHz del OCXO hasta una salida 1PPS limpia con <2 ps de jitter.
El STM32 controla su pin Arm (PB3); el 1PPS del GPS gobierna su pin Sync
directamente por hardware.

**Secuencia de armado** (comando `AP`):

1. El STM32 pone Arm a BAJO — la salida del divisor se detiene
2. Arm se mantiene BAJO durante 1.0–1.2 s (la especificación exige >1 s)
3. El STM32 libera Arm (ALTO)
4. El divisor reinicia sincronizado con el siguiente flanco de subida 1PPS del GPS

El armado se rechaza (se aplaza) cuando no hay señal GPS — sin un flanco
1PPS en Sync el divisor quedaría detenido con la salida muerta.

**Sincronización a largo plazo — importante:**

La salida del picDIV es coherente en fase con el **OCXO**, no con el GPS.
Lo que ocurre tras el armado depende del algoritmo de control activo:

| Tipo de algoritmo | Frecuencia | Fase | Comportamiento del picDIV |
|-------------------|------------|------|---------------------------|
| FLL (0, 3, 6, 8*) | acotada | paseo aleatorio | el 1PPS deriva lentamente del GPS |
| PLL (4, 5, 7) | acotada | acotada | el 1PPS se mantiene alineado con el GPS |

*El algoritmo 8 se comporta como FLL para errores grandes y como PLL cerca del enganche.

Un FLL solo anula el error medio de frecuencia; cada pequeño residuo se
integra en fase, así que el 1PPS del picDIV realiza un paseo aleatorio
respecto al GPS (típicamente µs/día a 1e-11 de error medio). Si importa la
alineación 1PPS a largo plazo, use un algoritmo PLL (`LA 4`, `LA 5` o `LA 7`)
o rearme (`AP`) periódicamente. Arme solo después de que el lazo informe
enganche (tendencia `hit`) — armar durante la convergencia inicia la deriva
de fase de inmediato.

---

## Holdover automático

Cuando el GPS pierde la señal durante la operación normal (p. ej. antena
desconectada):

1. `vControlTask` detecta la transición de `pos_valid` de `true` a `false`
2. Establece automáticamente `holdover_mode=true`, `holdover_auto=true`
3. El PWM se congela en el último valor — el OCXO funciona libremente
4. El LED amarillo pulsa rápido (200 ms), las pantallas muestran `A` (parpadeo)
5. Al recuperar la señal: se cancela el holdover automático, vuelve a ENCENDIDO fijo

El comando manual `MH` activa el holdover de forma independiente (indicado
como `H`). `MD` desactiva el holdover (tanto manual como automático).

---

## Comandos CLI (Serial / Bluetooth)

Conexión: 115200 Bd (USB) o 57600 Bd (Bluetooth HC-06, `GPSDO_BLUETOOTH`).
Comandos terminados con `\r\n` o `\n`. Los nombres de comando **no distinguen
mayúsculas de minúsculas** (`LA`, `la` y `La` son equivalentes), así que
funciona cualquier combinación de mayúsculas/minúsculas.

### Generales

| Comando | Descripción |
|---------|-------------|
| `H` | Mostrar ayuda |
| `V` | Versión, autores y enlaces de GitHub |
| `F` | Vaciar búferes de frecuencia (reiniciar el promediado) |
| `C` | Iniciar autocalibración (solo centrado del PWM) |
| `CT` | Calibrar + autoajustar: medir K, derivar PID para todos los algos 3-9 |
| `T [baud]` | Túnel GPS por USB para u-center — NMEA/UBX bidireccional limpio (la telemetría pasa a Bluetooth si existe, si no se silencia); baud opcional de la UART GPS, conservado al salir; sale tras 300 s |
| `SP <n>` | Fijar el DAC PWM directamente (1–65535), omite el algoritmo |
| `RH` | Modo de informe: legible por humanos (por defecto) |
| `RD` | Modo de informe: delimitado por tabuladores |
| `RP` | Pausar el flujo de datos serie/BT |
| `RR` | Reanudar el flujo de datos serie/BT |
| `SW` | Marcas de agua de pila de las tareas FreeRTOS (diagnóstico) |

### Control

| Comando | Descripción |
|---------|-------------|
| `MH` | Activar modo holdover (manual) |
| `MD` | Activar modo disciplinado |
| `LA [0-10]` | Seleccionar / mostrar algoritmo de control |
| `AP` | Armar picDIV — detiene la salida 1.0–1.2 s, resincroniza con el 1PPS del GPS |

### Ajuste de algoritmos

| Comando | Descripción |
|---------|-------------|
| `LP [n]` | Listar parámetros PID del algo `n` (o el actual) |
| `KP n val` | Fijar Kp para el algo `n` (3–7) |
| `KI n val` | Fijar Ki para el algo `n` (3–7) |
| `KD n val` | Fijar Kd para el algo `n` (3–7) |
| `IL n val` | Fijar I_LIMIT para el algo `n` (3–9) |
| `BC [val]` | Cruce de mezcla del algo 8 (Hz) |
| `BS [val]` | Ancho del sigmoide de mezcla del algo 8 (Hz) |
| `NS [val]` | Paso máx. de la red neuronal del algo 9 (LSB) |

### Configuración

| Comando | Descripción |
|---------|-------------|
| `TO [n]` | Mostrar / fijar un desfase fijo — horas o `h:mm` (`TO 9:30`, `TO -5`) |
| `TO A` | Zona automática: zona desde la posición GPS + regla DST de la UE (solo Europa) |
| `TZ [zona]` | Zona horaria con DST — `TZ Adelaide`, o una regla POSIX. Ver `H TZ` |
| `PO [f]` | Mostrar / fijar el offset de presión |
| `AO [f]` | Mostrar / fijar el offset de altitud |
| `SV [0\|1]` | Survey-in / Time Mode en el receptor de temporización (guardado por `ES`, aplicado en el siguiente arranque) |

### Zonas horarias

`TZ <ciudad>` suele bastar:

```
TZ Adelaide          → UTC+9:30, y UTC+10:30 con DST activo
TZ Warsaw            → UTC+1 / UTC+2
TZ Kolkata           → UTC+5:30, sin DST
```

Los nombres de ciudad son únicos en toda la base IANA, así que la región es
opcional (`TZ Australia/Adelaide` también funciona) y no importan las
mayúsculas. Hay 407 zonas integradas.

La regla también puede darse completa, lo que importa si un gobierno cambia
las reglas antes de que el firmware lo recoja:

```
TZ ACST-9:30ACDT,M10.1.0,M4.1.0/3
```

`H TZ` explica el formato. `ES` guarda el ajuste.

**¿Por qué no la base IANA?** Ocupa ~2 MB, cuatro veces toda la flash de este
MCU, y su valor está en actualizarse varias veces al año, algo que un GPSDO sin
internet no puede aprovechar. La cadena POSIX TZ a la que se reduce cada zona
ocupa 4–44 bytes y recoge el mismo comportamiento actual.

`TO A` (automático desde la posición GPS) sigue igual: es correcto en casi toda
Europa, pero no conoce el DST fuera de ella y solo devuelve horas enteras.
Fuera de Europa, usa `TZ`.


### LTIC — algoritmo 10 (tres etapas ACQ/DPLL/LOCK)

El algoritmo 10 disciplina el OCXO desde la fase TIC por hardware (PA1), que
resuelve la fase mucho más fino que el contador de ciclos TIM2. Es un diseño
híbrido: las etapas gruesas se apoyan en el robusto error de **frecuencia** de
TIM2 (sin ambigüedad de wrap), las etapas finas en la **fase** TIC de alta
resolución. Una máquina de tres estados lleva el lazo del arranque en frío al
enganche estrecho:

| Etapa | Se guía por | Qué hace | Sale cuando |
|-------|-------------|----------|-------------|
| **ACQ** | frecuencia (TIM2) | Captura guiada por frecuencia — acerca el OCXO a 10 MHz para que la fase suba lo bastante despacio como para atraparla. picDIV se arma al entrar. | \|fase\| se mantiene dentro de `acq_threshold` unos ciclos |
| **DPLL** | frec. + fase | Ambos términos: `Kp·e_freq` (rápido, TIM2) más un PI de fase (TIC). Centra la fase rápido. | \|fase\| pequeña **y** deriva baja (bajo `dpll_threshold`) |
| **LOCK** | fase (TIC) | Guiado por fase, actualizaciones lentas de banda estrecha cada `lock_interval` s. | vuelve a DPLL si \|fase\| deja la banda de histéresis de forma persistente |

La fase viene de `g_ltic_voltage`. Calibrado (`ns_per_volt ≠ 0`) el lazo trabaja
en nanosegundos respecto a `zero_offset`; sin calibrar, recurre a un error en
voltios en torno al medio de escala con un aviso único. Es clave que la banda de
trabajo del detector puede quedar lejos del medio del ADC (p. ej. 0–0,45 V), así
que el lazo nunca asume que 1,65 V es el centro — usa el `zero_offset` calibrado.
El estado persiste en EEPROM, así que un reinicio en caliente (`RB`) reanuda
donde se quedó en vez de arrancar en frío desde ACQ.

Selecciónalo con `LA 10`; el picDIV se arma automáticamente al entrar en ACQ.
Ejecuta `LC` primero para calibrar (sin ello, el lazo recurre a la fase en
voltios con un aviso). `LC` puede ejecutarse en cualquier momento — suprime el
lazo de disciplina durante su barrido, así que funciona incluso con el
algoritmo 10 ya enganchado. Un `LC` que pasa **guarda automáticamente** su
resultado (ns/V, zero-offset, rango) en el anillo Flash como datos vivos;
**no** hace falta ejecutar `ES` después. Un detector que no envuelve dentro
del barrido igualmente pasa, siempre que pendiente, centro y span sean
razonables; solo un resultado genuinamente débil se rechaza, con el motivo.
Los demás comandos de abajo fijan/muestran parámetros, que `ES` guarda.

| Comando | Descripción |
|---------|-------------|
| `LC` | **Autocalibración** de ns/V (pendiente local), zero-offset (anclado ~1,85 V) y rango (auto, ~7 min; imprime diagnóstico `t=/V=/n=` por segundo) |
| `LL` | Lista todos los parámetros LTIC + estado actual |
| `LNV [v]` | Calibración: ns por voltio (pendiente voltaje TIC→tiempo) |
| `LZO [v]` | Calibración: voltios TIC en diferencia de fase cero |
| `LRN [v]` | Rango no ambiguo del detector (ns, para el wrap) |
| `AQP/AQI/AQD/AQL [v]` | PID de etapa ACQ: Kp / Ki / Kd / I_LIMIT |
| `DPP/DPI/DPD/DPL [v]` | PID de etapa DPLL: Kp / Ki / Kd / I_LIMIT |
| `LKP/LKI/LKD/LKL [v]` | PID de etapa LOCK: Kp / Ki / Kd / I_LIMIT |
| `LAT [v]` | Umbral ACQ→DPLL (fase en rango, ns) |
| `LDT [v]` | Umbral DPLL→LOCK (error de frecuencia) |
| `LIV [v]` | Intervalo de actualización LOCK (segundos, por defecto 300) |
| `LPOL [-1/0/1]` | Polaridad del detector de fase (0 = auto) |
| `LCV` | Muestra el voltaje TIC actual (ayuda en la calibración) |

#### Corrección de diente de sierra (qErr) — `SAW 0|1`

Un receptor de tiempo u-blox genera su 1PPS dividiendo un oscilador interno,
así que cada pulso cae en un flanco de reloj — hasta un periodo de reloj lejos
del tiempo GPS real. Este error de cuantización por pulso es el término de fase
de corto plazo dominante en receptores antiguos (la granularidad del LEA-6T es
21 ns). El receptor lo informa por adelantado como `qErr` en el mensaje
UBX-TIM-TP.

El firmware activa TIM-TP automáticamente al inicializar el GPS y un sniffer
pasivo parsea `qErr` del mismo flujo de bytes que lee el parser NMEA. Con
`SAW 1` la ruta de fase TIC lo resta, de modo que el lazo disciplina contra el
error propio del OCXO en vez de perseguir el diente de sierra de granularidad
del receptor. `qErr` es un campo de picosegundos de 32 bits con signo en el
mismo offset del payload en **LEA-6T, LEA/NEO-M8T y ZED-F9T**, así que un solo
parser cubre los tres. La corrección caduca si TIM-TP deja de llegar (reinicio
del receptor), por lo que nunca se aplica un valor obsoleto.

`SAW` sin argumento muestra el estado y el qErr en vivo; `SAW 1`/`SAW 0` lo
conmuta (guardado con `ES`, desactivado por defecto). Cuando está activo, la
línea de telemetría `Learn:` muestra `qErr=…ns` para el algoritmo 10, y el
valor se resta de cada lectura de fase del TIC. Como Vphase se muestrea en el
pico de la rampa justo tras el flanco PPS (véanse las notas de hardware TIC más
abajo), cada lectura de fase se empareja con el qErr reportado para el pulso de
ese mismo segundo.

#### Ventana de promediado del término de amortiguación — `FA` / `FAD` / `FAL`

El lazo del algoritmo 10 alimenta su término de frecuencia (amortiguación) desde
un promedio de frecuencia móvil. Medidas contra referencia de rubidio (Dan
Wiering, tinyPFA contra un S250) mostraron un ciclo límite de ~220 s en algo 10
cuyo mecanismo es el retardo de grupo del promedio largo de 100 s cayendo cerca
de la cuadratura — mientras que el algoritmo 7 en la misma referencia era limpio,
lo que apunta a la estructura de segundo orden del DPLL y no al promedio en sí.

`FA` selecciona qué ventana lee ese término, y los estados DPLL (adquisición) y
LOCK (estacionario) pueden ajustarse de forma independiente:

| Comando | Efecto |
|---------|--------|
| `FAD [n]` | Ventana solo para el estado **DPLL** |
| `FAL [n]` | Ventana solo para el estado **LOCK** |
| `FA [n]` | Ambos a la vez |
| `FA` | Muestra ambas ventanas actuales |

`n` es 10, 100 o 1000 segundos; **100 es el valor por defecto en cada uno y
reproduce el comportamiento anterior bit a bit**. Solo se afecta el término de
frecuencia — la detección de fuga, el autoaprendizaje, la máquina de estados y el
phase PI se mantienen en el promedio suave de 100 s. Ambas ventanas se guardan
con `ES` (grupo LTIC).

La separación es tanto diagnóstico como solución: si `FAD` cambia el ciclo, vive
en la adquisición; si solo `FAL`, en el estado estacionario; si ninguno lo toca,
el ciclo está en la rama de fase, no en el término de frecuencia. Es
deliberadamente un conmutador, no un nuevo valor por defecto — la ventana corta
es una candidata, a validar contra una referencia antes de cambiar nada.

---

## Notas del hardware TIC — integrador de rampa con puerta (Kaashoek)

El detector de fase es el **TIC de 1 ns de Erik Kaashoek** (como en el STM32
GPSDO de André Balsa, esquema rev 0.4). Entender exactamente cómo funciona
costó tiempo real de banco — tres flip-flops (dos 74HC74 a 5 V, finalmente un
74LVC74 a 3,3 V), un valor de filtro equivocado y un largo desvío por dos
modelos de detector incorrectos. Se documenta aquí para que el siguiente no lo
repita.

### Cómo funciona en realidad (confirmado con el osciloscopio)

Un **par de flip-flops D tipo 74** (`xx74`) convierte la diferencia de fase
entre dos señales 1PPS en un pulso: **la carga empieza en el flanco de subida
del 1PPS del GPS y termina en el del 1PPS del picDIV**, así que el ancho del
pulso *es igual al intervalo de fase* entre ellos. Ese pulso abre un diodo
Schottky (1N5711) que carga C13 a través de R8 — una **rampa tiempo-tensión**,
igual que el original de Lars Walenius, solo que con un flip-flop en lugar de
un HC4046. El MCU lee el pico de la rampa una vez por segundo y la carga se
disipa después (~25 ms) antes del siguiente pulso.

Dos consecuencias, ambas aprendidas por las malas:

- **El RC debe ser pequeño.** R8×C13 = 1 kΩ × 1 nF, τ ≈ 1 µs — ajustado al
  pulso de escala µs para que el condensador siga el ancho del pulso
  linealmente. Es el valor del esquema de Kaashoek (nota "R8×C13 = 100 ns" en
  rev 0.4, 1000 ns en la hoja posterior); **no** es un promedio paso-bajo de un
  ciclo de trabajo. Una revisión anterior de estas notas afirmaba lo contrario
  (un "detector de ciclo de trabajo" que necesitaba un filtro grande de
  51 kΩ/1 µF) — era **incorrecto**. Con 51 kΩ/1 µF el pulso de µs apenas movía
  el condensador (≈14 mV de span en `LC`); con 1 kΩ/1 nF la rampa abarca
  ~1,5–2 V y `LC` funciona.
- **La lectura debe caer en el pico.** La rampa llega al pico al final del pulso
  (≤ ~2 µs tras el flanco del GPS) y se mantiene menos de ~1 ms antes de
  decaer. Muestrearla desde el bucle de sensores de 2 s siempre capturaba el
  condensador descargado (~0,065 V, independiente de la fase — la causa raíz de
  semanas de "calibraciones fallidas"). Ahora Vphase se lee ~50 µs tras el
  flanco PPS, desde la tarea de relé notificada por PPS, cayendo en el pico. Sin
  descarga activa: el diodo bloquea y la fuga de ~25 ms limpia el condensador
  antes del siguiente pulso de 1 Hz.

### El papel del picDIV

El picDIV **no** forma parte del valor de la rampa — genera la **salida 1PPS**
disciplinada (sincronizada con UTC, capaz de holdover), y su flanco marca el
final del pulso de carga. El paso `AP`/arm al principio de `LC` solo aparca la
fase cerca del flanco del GPS para que el barrido empiece desde un punto
conocido; el detector compara el 1PPS del GPS con el del picDIV (derivados,
respectivamente, del cielo y del OCXO disciplinado), por lo que minimizar
Vphase alinea el PPS de salida con UTC.

### Calibración: punto de trabajo anclado (Opción D)

La rampa es exponencial (τ ≈ 1 µs), así que ns/V **no es constante** a lo largo
de ella. Un promedio de todo el tránsito (range/span) depende de dónde el arm
aparcó la fase, y variaba ~15–20 % entre ejecuciones. Registros `LC` con
resolución de 1 s mostraron que la **pendiente local** dV/dt es repetible en una
banda estrecha cerca de **1,85 V** y diverge por encima y por debajo — esa
tensión es el punto óptimo repetible de este detector (≈0,63·Vsat, el centro
del rango útil). `LC` ancla ahí `zero_offset` y lee ns/V de la pendiente local
en una ventana de ±0,20 V, lejos de las **zonas muertas** que caracterizó Dan
Wiering: la caída del Schottky + pull-down por debajo de ~0,05 V, y el riel/
wraparound del ADC cerca de 3,3 V (PA1 tolera 5 V pero solo lee hasta ~3,23 V).
Si un barrido nunca cruza la banda de anclaje, `LC` recurre al promedio
range/span y lo indica.

### Resolución

La rampa de 1 kΩ/1 nF abarca ~1,5–2 V del ADC de 12 bits en la ventana de fase
útil, y el sobremuestreo 16× con mediana rechaza glitches — comparable o mejor
que la lectura única del HC4046 de Lars a ~1 ns. La caída de ~25 ms es
irrelevante para el ancho de banda del lazo: LOCK actualiza cada pocos segundos
(muy por debajo de 0,2 Hz), así que la constante de tiempo del detector está
órdenes de magnitud por encima del lazo.

## EEPROM

| Comando | Descripción |
|---------|-------------|
| `ES [grupo]` | Guardar en EEPROM — todo, o un solo grupo (ver abajo) |
| `ER` | Recuperar parámetros desde EEPROM |
| `EE` | Borrar EEPROM (restaurar valores por defecto) |

`ES` por sí solo guarda toda la página de parámetros, como antes. `ES <grupo>`
guarda solo un bloque y deja el resto de ajustes en su valor almacenado — así,
confirmar un cambio de zona horaria no puede fijar de paso un PID con el que aún
estabas experimentando. Los grupos son `CORE` (PWM, algoritmo activo), `PID`
(ganancias algo 3-9, mezcla, paso NN), `TZ` (zona horaria), `LTIC` (ajuste del
lazo algo-10, umbrales y las ventanas FA), `LCAL` (calibración de rampa algo-10),
`CAL` (offsets de presión/altitud) y `MISC` (survey, warmup, splash, ring, saw,
learn). `ES` con un nombre desconocido los lista en vez de adivinar. Cada guardado
rellena el búfer de emulación desde flash, escribe la sección pedida y hace flush;
los bytes intactos conservan sus valores en flash.

---

## EEPROM

La EEPROM (emulada en la Flash del STM32) almacena 144 bytes:

| Dirección | Tamaño | Contenido |
|-----------|--------|-----------|
| 0–5 | 6 B | Firma `"GPSD2"` |
| 6–7 | 2 B | Valor del DAC PWM (big-endian) |
| 8 | 1 B | Número de algoritmo (0–9) |
| 9 | 1 B | Desfase heredado en horas enteras (sustituido por 234) |
| 10–121 | 112 B | PID: g_pid[3..9] × {Kp, Ki, Kd, I_LIMIT} |
| 122–133 | 12 B | g_blend_crossover, g_blend_scale, g_nn_max_step |
| 134–137 | 4 B | g_pressure_offset (comando PO) |
| 138–141 | 4 B | g_altitude_offset (comando AO) |
| 142 | 1 B | Flag tz_auto heredado (sustituido por 234) |
| 234 | 1 B | Modo de zona (0 = manual, 1 = auto-EU, 2 = regla POSIX) |
| 235 | 2 B | Desfase manual, minutos (int16) |
| 237 | 48 B | Regla POSIX TZ, como texto |
| 143 | 1 B | habilitación de survey-in (0 = off, 1 = on, comando `SV`) |

---

## Receptores de temporización GPS (LEA-6T / LEA-M8T / NEO-M8T / ZED-F9T)

Los módulos NEO-6M / NEO-8M funcionan sin configuración (por defecto). Para un
receptor de temporización u-blox, active la opción en `gpsdo_config.h`:

```c
#define GPSDO_GPS_TIMING            // receptor de temporización u-blox (ver abajo)
#define GPSDO_SVIN_MIN_SECS   300   // duración mínima de survey-in [s]
#define GPSDO_SVIN_ACC_LIMIT  5000  // límite de exactitud [mm] (5 m)
```

El LEA-6T y el LEA-M8T aceptan comandos de Time Mode **distintos**, así que el
firmware prueba cada uno por turno y conserva el primero que reciba ACK:
`CFG-TMODE2` (0x06 0x3D, usado por el LEA-M8T) y el más antiguo `CFG-TMODE`
(0x06 0x1D, usado por el LEA-6T de u-blox 6). El progreso se lee con
`TIM-SVIN` (0x0D 0x04) en ambos. (El par más nuevo `CFG-TMODE3` / `NAV-SVIN`
solo existe en firmware de alta precisión como NEO-M8P / ZED-F9P, no en estas
unidades de temporización — verificado en u-center contra un LEA-M8T-0 /
TIM 1.10 y un LEA-6T.)

**El NEO-M8T** es totalmente compatible con el LEA-M8T — mismo silicio u-blox
M8 y firmware FW3, mismos mensajes `CFG-TMODE2` / `TIM-SVIN` — así que funciona
sin cambios de código más allá de activar la opción. (Ambas variantes M8T usan
por defecto GPS + GLONASS + QZSS; reconfigure a GPS + QZSS mediante `CFG-GNSS`
en u-center y guarde en flash si desea una solución de una sola constelación.)

**ZED-F9T (Gen9)** también es compatible. La generación F9 reemplazó los
mensajes de configuración heredados (obsoletos desde el firmware TIM 2.24) por
la interfaz de claves de configuración, e informa el survey-in mediante
`NAV-SVIN` (0x01 0x3B) en lugar de `TIM-SVIN`. El soporte se añade como una
tercera vía: `ubx_start_survey_in()` también envía una trama `CFG-VALSET`
(0x06 0x8A) que fija `CFG-TMODE-MODE` / `CFG-TMODE-SVIN_MIN_DUR` /
`CFG-TMODE-SVIN_ACC_LIMIT` (este último convertido de mm a la unidad de
0,1 mm del F9T), y el monitor de survey-in recurre a `NAV-SVIN` cuando
`TIM-SVIN` no responde. Esta vía fue probada en hardware real por el usuario de
EEVblog **danieljw**. La trama heredada `CFG-NAV5` (modo estacionario) puede
ser rechazada (NAK) por un F9T; eso es inofensivo (la vía de survey-in es
independiente).

En cada encendido el receptor ejecuta un **survey-in**: promedia la posición
de la antena y luego cambia a una solución de **solo tiempo** con posición
fija. Esto da un 1PPS notablemente más limpio — temporización de un solo
satélite sin jitter de navegación — lo que mejora directamente la estabilidad
de fase. El survey-in termina cuando se alcanza la duración mínima **o** el
límite de exactitud.

El progreso se muestra en todas las pantallas como `SVIN <segundos>
<exactitud>m`. La posición sigue transmitiéndose por NMEA durante todo el Time
Mode (la solución congelada y promediada), así que la pantalla de ubicación y
la zona horaria automática (`TO A`) siguen funcionando — de hecho con más
estabilidad, ya que la posición ya no deambula.

> **La antena importa.** Ejecute el survey-in solo con una buena antena
> exterior con vista clara y completa del cielo. El survey-in promedia la
> posición de la antena y solo se completa al alcanzar el límite de exactitud;
> con una antena interior u obstruida puede converger lentamente o estancarse
> en una exactitud pobre (decenas de metros). Con una antena exterior/de techo
> adecuada, tanto el LEA-6T como el LEA-M8T se completan dentro del tiempo
> configurado y cambian limpiamente a Time Mode. (En las pruebas, el LEA-6T
> más antiguo resultó notablemente más sensible en condiciones marginales que
> el LEA-M8T.)

En Time Mode el receptor deja de optimizar la posición, así que el HDOP
informado pierde sentido (~99.99). Las pantallas y el informe serie legible
muestran `HDOP:TIME` en ese estado en lugar del número falso; el registro
delimitado por tabuladores conserva el valor bruto para graficar.

Sin ninguna de estas opciones definidas, los módulos NEO usan la ruta de modo
estacionario existente sin cambios.

---

## Nivelación de desgaste de Flash (datos vivos)

Los datos “vivos” — deriva/amortiguación aprendida (`LRN`), calibración LC y
el último PWM — cambian mucho más a menudo que los ajustes, por lo que se
almacenan aparte de la EEPROM de ajustes, en un **buffer circular con
nivelación de desgaste** que ocupa el sector 6 de Flash (0x08040000, 128 KB).
Actívalo con `FR 0|1` (guardado con `ES`, activo por defecto); consulta el
desgaste con `EW`.

Cada guardado escribe el siguiente slot de 32 bytes; el sector se borra solo
cuando el anillo da la vuelta (una vez cada 4095 guardados). A 100
guardados/día son ~9 borrados/año, así que la resistencia del Flash (~10 000
ciclos) dura del orden de mil años. Un guardado ocurre solo cuando un valor se
ha asentado en un nuevo nivel — la deriva cambió > 8 LSB o la amortiguación
> 0.03, y ≥ 20 min desde el último guardado — mientras que una calibración
`LC` exitosa guarda de inmediato. Cada slot lleva un CRC y un número de
secuencia, así que un corte de energía a mitad de escritura se detecta y se usa
el slot bueno anterior.

Con el anillo **activo**, `ES` nunca sobrescribe la calibración ni los valores
aprendidos — solo guarda ajustes genuinos (ganancias PID, umbrales, flags).
Con el anillo **inactivo**, `ES` sí guarda esos valores vivos en EEPROM como
respaldo.

### Conservar datos vivos al re-flashear el firmware

- **Bootloader / DFU / Arduino IDE** toca solo los sectores de firmware (0–5);
  el anillo (6) y la EEPROM de ajustes (7) sobreviven.
- **Borrado total del chip con J-Link/ST-Link** borra todo. Para conservar
  calibración y aprendizaje, borra solo los sectores 0–5:
  `erase 0x08000000 0x0803FFFF`, luego `loadbin firmware.bin 0x08000000`.
- Si el anillo se borra, el firmware reaprende/recalibra desde los valores por
  defecto — nada se rompe, solo se pierde el ajuste acumulado.

---

## Autoajuste (comando `CT`)

`CT` mide la ganancia de planta del oscilador y deriva de ella los
coeficientes PID para todos los algoritmos — sin ajuste manual, sin
oscilación forzada arriesgada.

Procedimiento (~3 minutos, determinista):

1. Lleva el PWM a tres puntos (1.5 / 2.0 / 2.5 V), estabilizándose en cada uno.
2. Ajuste por mínimos cuadrados de frecuencia vs PWM → **K** [Hz/LSB], la
   ganancia de planta, más el PWM que da exactamente 10 MHz.
3. Calcula los coeficientes a partir de K:
   - **PLL (4, 5, 7):** Kp = 0.40/K sobre frecuencia; Kd = 2.0, Ki = 0.02 sobre fase
   - **FLL (3, 6):** Kp = 0.35/K; Ki = Kp/300; Kd = Kp·73
   - **NN (9):** paso máx. = 0.05/K
4. Aplica el PWM centrado y los nuevos coeficientes, imprime antes/después.

El resultado se verifica (K debe estar entre 0.1–2 mHz/LSB y el GPS debe
mantener señal); si falla, los parámetros quedan sin cambios. Ejecute `ES`
después para guardar los valores ajustados en EEPROM. A diferencia del
autoajuste por realimentación de relé, `CT` nunca desestabiliza el lazo — la
constante de tiempo del lazo aquí es de cientos de segundos, así que la
oscilación forzada tardaría horas y se corrompería por la deriva térmica;
derivar las ganancias directamente de una K medida es más rápido y más seguro.

---

## Zona horaria automática (`TO A`)

La hora local puede seguir la posición GPS automáticamente. En modo automático
el firmware recalcula el desfase UTC de forma continua a partir de la
latitud/longitud y la fecha:

- **Dentro de Europa** (lat 35–72, lon −11–42): un conjunto compacto de reglas
  de zona civil (UTC+0 al oeste de −7.5°, UTC+1 para la franja CET incluida
  toda Polonia, UTC+2 para los Bálticos/Finlandia/Balcanes), más la **regla
  DST de la UE** — +1 h desde el último domingo de marzo a las 01:00 UTC hasta
  el último domingo de octubre a las 01:00 UTC.
- **Fuera de Europa**: zona solar `round(lon/15)`, sin DST (las reglas varían
  demasiado en el mundo para adivinarlas con seguridad).

`TO <n>` vuelve a un desfase manual fijo. El modo y el desfase se guardan con
`ES` y se restauran en el arranque.

---

## Informe de hardware al arranque

Cada dispositivo opcional informa de su resultado de detección por
serie/Bluetooth al arrancar, dando un inventario completo de lo encontrado:

```
HW: AHT10/AHT20 sensor    OK  (I2C 0x38)
HW: BMP280 sensor         OK  (I2C 0x77)
HW: INA219 sensor         not found
HW: OLED 128x64           OK  (I2C 0x3C)
HW: LCD 20x4              OK  (I2C expander)
HW: HT16K33 clock display OK  (I2C 0x70)
HW: TFT 320x240           enabled (SPI1, write-only - not verifiable)
HW: TM1637 clock display  enabled (GPIO PA8/PB4, write-only - not verifiable)
```

Un dispositivo ausente informa `not found` y el firmware continúa sin él.

---

## Entrada de fase LTIC (Lars' TIC)

Con `GPSDO_LTIC` activado, el firmware lee un contador de intervalo de tiempo
por hardware (el TIC de Lars Walenius): un condensador de 1 nF se carga con una
corriente constante durante el intervalo GPS-1PPS → OCXO-1PPS, y la tensión
retenida en PA1 se muestrea en el pico de la rampa ~50 µs tras el flanco PPS;
no hace falta descarga activa: el diodo bloquea y la fuga de ~25 ms limpia el
condensador antes del siguiente pulso de 1 Hz. La tensión
es una medida directa y de alta resolución de la diferencia de fase entre ambos
pulsos — mucho más fina que el contador de ciclos TIM2 que usan los algoritmos
de frecuencia (3–9).

El lazo de control **disciplina el OCXO directamente desde esta fase** mediante
el algoritmo 10 (`LA 10`) — el lazo de tres etapas ACQ → DPLL → LOCK descrito
abajo. La fase aparece en el informe serie (`Vphase:` y `dph:` en ns), como una
fila `Vph:`/`dph:` en el TFT y como una línea `LTIC phase (PA1)` en la lista de
verificación de arranque. Una vez que `LC` ha calibrado la rampa, la fase se
reporta en nanosegundos relativa al `zero_offset` calibrado, usando el
`ns_per_volt` medido; antes de calibrar solo se muestran voltios. (La constante
de compilación `LTIC_NS_PER_VOLT` en `gpsdo_config.h` es un fallback heredado y
normalmente queda en 0 — `LC` mide la pendiente real por placa y la guarda en
los parámetros vivos.)

---

## Oscilador (OCXO)

El firmware funciona con cualquier OCXO de 10 MHz controlado por tensión cuya
entrada EFC esté dentro del rango de 0–3.3 V que entrega el DAC PWM del STM32
(un oscilador con EFC de 0–4 V también funciona — se alcanza alrededor del
82.5% de su rango). El tipo de oscilador **no** necesita seleccionarse en
tiempo de compilación.

En su lugar, ejecute el comando **`CT` (Calibrate & Tune)** una vez tras el
calentamiento: mide la ganancia de control real *K* [Hz/LSB] de un barrido PWM
de tres puntos, encuentra el valor PWM para exactamente 10 MHz y deriva cada
coeficiente PID para el oscilador ajustado. Guarde con `ES`. Antes del primer
`CT`, el lazo arranca desde un PWM universal de rango medio (32767 ≈ 1.65 V),
seguro para cualquier unidad con EFC de 0–4 V.

Esto reemplaza las tablas de coeficientes por oscilador anteriores — una sola
calibración adapta el lazo a cualquier cristal instalado, incluida la
variación entre unidades de dos piezas nominalmente idénticas.

---

## Configuración de compilación

El archivo `gpsdo_config.h` controla la compilación. Interruptores clave:

```c
// Pantallas — descomente según necesidad:
#define GPSDO_OLED_SSD1309       // o SH1106, SSD1306
#define GPSDO_LCD_20x4           // HD44780 20x4 I2C
#define GPSDO_TM1637_6           // TM1637 de 6 dígitos (HH:MM:SS)
#define GPSDO_TFT_ST7789         // o GPSDO_TFT_ILI9341 (320x240) / GPSDO_TFT_ILI9488 (480x320)
#define GPSDO_HT16K33            // reloj HT16K33 de 4 dígitos, I2C 0x70

// Sensores:
#define GPSDO_AHT10              // temperatura + humedad AHT10/20
#define GPSDO_BMP280_I2C         // temperatura + presión BMP280
#define GPSDO_INA219             // tensión + corriente INA219

// Comunicación:
#define GPSDO_BLUETOOTH          // HC-06 en Serial2 (57600 Bd)

// Otros:
#define GPSDO_EEPROM             // Persistencia de parámetros
#define GPSDO_PICDIV             // Soporte de picDIV
#define GPSDO_UBX_CONFIG         // Configuración UBX de NEO-6M/7M
#define GPSDO_GEN_2kHz_PB5       // Generador de 2 kHz en PB5
```

### Búfer serie (`build_opt.h`)

La carpeta del sketch también contiene `build_opt.h`, que STM32duino pasa a
toda la compilación (incluido el core) como flags del compilador:

```
-DSERIAL_RX_BUFFER_SIZE=256 -DSERIAL_TX_BUFFER_SIZE=512
```

Esto agranda el búfer RX serie del GPS desde el valor por defecto de 64 bytes
para que las sentencias NMEA no se pierdan o se fusionen a 38400 baudios
cuando la tarea GPS se interrumpe brevemente. Un `#define` normal en el sketch
no funcionaría — el `HardwareSerial.cpp` del core es una unidad de traducción
separada que solo ve flags del compilador. El archivo se detecta
automáticamente; no hay nada que activar.

---

## Asignación de pines

| Pin | Función |
|-----|---------|
| PA15 | TIM2 ETR — entrada del OCXO de 10 MHz |
| PB10 | TIM3 CH3 — captura del 1PPS del GPS |
| PB9 | DAC PWM — control de Vctl (16 bits) |
| PB1 | ADC — medición de Vctl |
| PA0 | ADC — medición de Vcc/2 |
| PB8 | LED amarillo — indicación de señal / holdover |
| PC13 | LED azul — latido del 1PPS |
| PB5 | Generador de 2 kHz (opcional) |
| PB3 | ARM del picDIV (opcional) |
| PA1 | LTIC Vphase (opcional) |
| PA9/PA10 | Serial1 TX/RX — NMEA del GPS |
| PA2/PA3 | Serial2 TX/RX — Bluetooth HC-06 |
| PB6/PB7 | I2C1 SCL/SDA — OLED, LCD, sensores |
| PA5/PA7 | SPI1 SCK/MOSI — pantalla TFT |
| PB12/PB13/PB15 | TFT D/C, CS, RES |

---

## Requisitos

- **Placa**: WeAct BlackPill STM32F411CE o F401CCU6
- **IDE**: Arduino IDE con core STM32duino ≥ 2.2.0
- **Bibliotecas**: STM32duino FreeRTOS, TinyGPS++, U8g2,
  Adafruit AHTX0, Adafruit BMP280, Adafruit INA219,
  hd44780 (para LCD), TFT_eSPI (para TFT), EEPROM (STM32)
- **Ajustes de compilación**: Tools → C Runtime Library → Newlib Nano + Float Printf/Scanf

---

## Licencia

Publicado bajo los mismos términos que el proyecto original de André Balsa.


## Disciplina LTIC de tres etapas (algoritmo 10) — novedades v0.5x–v0.88

- **`LC` — autocalibración con un comando**: arma el picDIV, parte del punto
  inferior determinista, ordena una velocidad de barrido derivada de K,
  muestrea toda la banda en una pasada abajo→arriba y escala ns/V con
  avg100. Veredicto `PASSED`/`MARGINAL`.
- **Ganancias auto-ajustadas** desde K (`CT`) y ns/V + rango (`LC`).
  LOCK: banda muerta, codo suave, tope ~4 mHz.
- **ADC robusto**: mediana de 16 lecturas por PPS + puerta de outliers.
- **Guardián de fuga**: fase saturada + |df| > 0,5 Hz congela el lazo.
- **Color de lock fiable**: verde SOLO en LOCK vivo (algoritmo 10).
- **Comandos**: `LC`, `LL`, `LPOL -1|0|1`, `LIV 1..30`, `WU 0|1`, `SPL 0|1` (animación de arranque, EEPROM), `FR 0|1` (buffer circular en Flash para datos vivos, EEPROM), `EW` (estadísticas de desgaste de Flash), `SAW 0|1` (corrección de diente de sierra qErr para LTIC, EEPROM).
  Animaciones LED: warmup = onda 'o' inferior, survey-in = 'o' superior,
  calibración = "CAL" + spinner.

## Soporte de TFT a color (TFT_eSPI)

Cualquier pantalla TFT_eSPI de **320×240** o **480×320** debería funcionar
(`TFT_SX()/TFT_SY()`). Probadas: ILI9341, ST7789, ILI9488. Activa el
`GPSDO_TFT_*` adecuado en `gpsdo_config.h` y configura el controlador y los
pines en `User_Setup.h` (SPI1: SCK=PA5, MOSI=PA7); cualquier panel de
320×240 o 480×320 encaja sin cambios de código.
