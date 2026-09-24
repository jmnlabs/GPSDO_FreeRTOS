# GPSDO v1.07.57rt

[English](README_EN.md) | [Polski](README_PL.md) | **Español**

📖 [Inicio del proyecto](../README.md) · Manual: [MD](MANUAL_ES.md) · [PDF](MANUAL_ES.pdf)

Firmware en tiempo real (FreeRTOS) para un oscilador disciplinado por GPS
(GPSDO) sobre la plataforma STM32 BlackPill (WeAct F411CE / F401CCU6).

📋 Historial de versiones: [Registro de cambios](CHANGELOG_ES.md)

## Créditos

| Rol | Persona / fuente |
|-----|------------------|
| Autor del port a FreeRTOS, algoritmos 3–11 y 13 | **J. M. Niewiński** — [repositorio](https://github.com/jmnlabs/GPSDO_FreeRTOS) |
| Asistentes de programación | **Claude Opus 5 (Anthropic) · GLM-5.3 Max (Z.ai) · Qwen3.8-Max** |
| Medición y pruebas de campo, algoritmos 10–12 | **Dan Wiering** — series ADEV contra referencia de rubidio que hallaron el ciclo límite del algoritmo 10, resolvieron la cuestión de la amortiguación `FA`, informaron del bloqueo en ACQ y establecieron la comparación del algoritmo 11; ahora está probando el algoritmo 12 |
| Soporte de ILI9486 / ILI9488 — el empujón para implementarlo; y el sintonizador de PC nació de un script de monitorización que montó con Claude | **lucido** (foro EEVBlog) |
| Autor de v0.06c — inspiración del port RTOS | **André Balsa** — [repositorio](https://github.com/AndrewBCN/STM32-GPSDO) |
| Lazo PI continuo (algoritmo 11) — diseño original | **Lars Walenius** (in memoriam) — controlador GPSDO compartido con la comunidad [time-nuts](http://www.leapsecond.com/time-nuts.htm) y EEVBlog. Ampliado aquí con autocalibración por CT, una rama de adquisición guiada por frecuencia y un puente de captura de fase picDIV. |
| Acumulador multinivel (algoritmo 12) — diseño original | **Alan Cashin** (MIS42N, foro EEVBlog) — [perfil](https://www.eevblog.com/forum/profile/?u=121386) — su Budget GPSDO es el origen del algoritmo 12, la corrección por cruce por cero, el PWM con dithering que alcanza 24 bits desde uno corto y la idea de autoevaluación `CS`. Implementado aquí sobre el detector de fase LTIC, con los límites por nivel editables porque solo el de 128 s se dedujo alguna vez de una especificación. |
| Filtro de Kalman (algoritmo 13) — diseño original | **J. M. Niewiński** — tres estados sobre una medida escalar, así que la inversión de matrices de los libros es una división y todo el filtro cuesta alrededor de un microsegundo por segundo en el M4F. Cada constante que de otro modo necesitaría se deriva de `CT` y `LC`, así que no lleva ni un número medido en una placa concreta. |
| Diseño de PCB (prototipo) | **Scrachi** (foro EEVBlog) — [mensaje con archivos](https://www.eevblog.com/forum/projects/yet-another-diy-gpsdo-yes-another-one/825/) · [perfil](https://www.eevblog.com/forum/profile/?u=762266) |
| Hilo del proyecto | [Yet another DIY GPSDO](https://www.eevblog.com/forum/projects/yet-another-diy-gpsdo-yes-another-one/) — Foro EEVBlog |

Este firmware se escribió desde cero como un port del código original de
André Balsa a la arquitectura FreeRTOS, con un rediseño completo de tareas,
sincronización y manejo de pantallas. El diseño de hardware se basa en el
esquemático v0.06c, usando las PCB compartidas por el usuario Scrachi en el
foro EEVBlog.

---


> **Consola de ajuste en PC:** véase [README_TUNER_ES](README_TUNER_ES.md) —
> gráficas en vivo, pestañas de parámetros y el generador de `gpsdo_tz_table.h`.

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
               │ NEO-6M/8M   │       │  10 MHz      │                     │
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

La disposición de arriba es la misma en ambos paneles. No siempre lo fue — las
dos ramas se fueron separando campo a campo hasta v1.05, cuando el panel pequeño
volvió a la fila: qErr está en la fila de Alt junto a los datos de fix, los
sensores ambientales comparten la columna izquierda y los eléctricos la derecha,
y Vcc y Vdd comparten la fila inferior de sensores.

**Si editas la disposición del panel pequeño, mide — no cuentes caracteres.** La
fuente 2 es proporcional. `Vph:1.951V` son 70 px, no los 80 que sugieren diez
caracteres a 8 px, y la exageración llega a una quinta parte en el conjunto de
una fila. Dos campos ya habían sido recortados apoyándose en esa aritmética y
ambos cabían una vez medidos. `tft_text_w()` se lo pregunta a la librería, que es
lo que hace la rama del 480 en todas partes; donde se usa una constante en su
lugar, el comentario contiguo da la anchura medida de la que procede.

El relleno de cada campo es la anchura de su forma **más ancha**, no la de la
lectura de hoy: el argumento de anchura de `dtostrf()` es un mínimo, así que un
valor gana un carácter al cruzar una potencia de diez, y un relleno dimensionado
para el caso habitual deja que ese carácter caiga sobre el vecino. Los rellenos
de una fila la embaldosan exactamente — sin huecos ni solapes — de modo que
ningún fondo puede borrar el borde de un campo adyacente. En el panel pequeño
todos los campos de la columna derecha están anclados a x=314.

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

- **OLED 128×64** I2C (SH1106 / SSD1306 / SSD1309)
- **LCD 20×4** I2C (HD44780 + PCF8574T)
- **TM1637** (reloj de 4 o 6 dígitos)
- **TFT 320×240** SPI (ILI9341 / ST7789, biblioteca TFT_eSPI)
- **TFT 480×320** SPI (ILI9488, biblioteca TFT_eSPI) — probado en campo contra
  una referencia de rubidio; el diseño 320×240 se escala automáticamente
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
| Mínima | `vUptimeTask` | 768 B | Reloj de tiempo de actividad sólo en holdover — fuera de él lo mueve el PPS |

**El estado compartido** está protegido por mutexes de FreeRTOS:

- `xFreqMutex` — datos de frecuencia (`gFreq`, `gFreqSnap`)
- `xGpsMutex` — datos GPS (`gGps`)
- `xCtrlMutex` — datos de control (`gCtrl`: PWM, algoritmo, holdover, tendencia)
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
| 11 | LTIC-Lars | fase TIC | continuo | Un único lazo PI continuo, sin máquina de estados; ganancia derivada de `CT`. Según Lars Walenius |
| 12 | Acumulador multinivel | fase LTIC [ns] | adaptativo | No hay constante de tiempo que fijar: el error elige su ventana. Según Alan Cashin (MIS42N). **Sin ajustar.** |
| 13 | Filtro de Kalman | fase LTIC [ns] + frecuencia TIM2 | adaptativo | Tres estados — fase, frecuencia, envejecimiento — con covarianzas: el peso de cada lectura sale de las varianzas medidas y no de una constante, y cada escala se deriva de `CT` y `LC`. Original de este proyecto. |

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
`KI`, `KD`, `IL`) — sin recompilar. Los parámetros se guardan en el flash-ring con
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

### Los dos tamaños de panel

La pantalla de trabajo se compone una sola vez, a 320×240, y se escala a 480×320
en tiempo de compilación mediante `TFT_SX` / `TFT_SY` / `TFT_F`. La disposición
de los campos es por tanto **idéntica en ambos paneles** — nada se desplaza
respecto a lo demás. Lo que cambia es cómo se dibuja el texto, y esa diferencia
es lo bastante grande como para importar.

```
 ┌──────────────────────────────────────────────────┐
 │ GPSDO v1.07.57rt                LMT 14:32:45 Lun │   cabecera: versión + hora local
 │                                                  │
 │          1 0 0 0 0 0 0 0 . 0 0 0 0  H z          │   frecuencia, fuente grande
 │                                                  │
 │ UTC: 12:32:45 Lun        Sat: 11 HDOP: 0.77      │
 │ DATE: 28/07/2026         Lat:  52.229676         │
 │ Uptime: 000d 01:42:12    Lon:  21.012229         │
 │ Algo: 12 LOCK            Alt:118m     qEr:-4.2ns │   algoritmo + tendencia | datos de fix + qErr
 │ PWM:40849 Vct:1.799V     INA:4.920V     182.00mA │   salida de control     | monitor de alimentación
 │ BMP: 51.1C 1006.7hPa     Vph:2.083V dp:     -5ns │   sensores              | detector de fase
 │ AHT: 24.60C 41.20%rH     Vcc:5.012V    Vdd:3.29V │                         | raíles de alimentación
 │                                                  │
 │               DISCIPLINED  FIX OK                │   barra de estado (color = estado)
 └──────────────────────────────────────────────────┘
```

**320×240 (ILI9341, ST7789)** usa las fuentes numéricas GLCD clásicas para la
pantalla de trabajo. Ya tienen el tamaño adecuado a escala 1:1, se dibujan rápido
y no requieren `LOAD_GFXFF` en `User_Setup.h`. El razonamiento está en *Por qué
el panel pequeño conserva las fuentes clásicas*, más abajo.

**480×320 (ILI9488, ILI9486)** usa las fuentes libres de Adafruit GFX mediante
una capa de abstracción por rol (`GF_DATA`, `GF_HEAD`, `GF_STATUS`, etc. en
`gpsdo_config.h`). Escalar las fuentes de mapa de bits clásicas por 1,5 habría
dado dígitos visiblemente dentados; las fuentes libres se mantienen limpias en el
tamaño mayor. Esta compilación **sí** necesita `LOAD_GFXFF`.

El panel grande mueve además 2,4 veces más píxeles por redibujado, así que
`SPI_FREQUENCY` no debería bajar de 40 MHz en él — véanse las notas de conexión.

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
│ v1.03-rt      GPSDO      LMT 14:32:45 Thu   │ ← header bar (navy)
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

### Soporte de TFT a color (TFT_eSPI)

Cualquier pantalla TFT_eSPI de **320×240** o **480×320** debería funcionar
(`TFT_SX()/TFT_SY()`). Probadas: ILI9341, ST7789, ILI9488. Activa el
`GPSDO_TFT_*` adecuado en `gpsdo_config.h` y configura el controlador y los
pines en `User_Setup.h` (SPI1: SCK=PA5, MOSI=PA7); cualquier panel de
320×240 o 480×320 encaja sin cambios de código.

---

## Pantallas de reloj (HT16K33 y TM1637)

Se admiten dos módulos pequeños de 7 segmentos. Ambos muestran la hora, respetan
el ajuste `LT` (UTC o local) y parpadean los dos puntos, de modo que una pantalla
detenida salta a la vista. Ninguno muestra el estado del GPSDO — existen para que
la unidad sirva de reloj mientras disciplina.

### HT16K33 — 4 dígitos, I2C

```
 ┌────────────────┐
 │  1 4 : 3 2     │   HH:MM, los dos puntos parpadean cada segundo
 └────────────────┘
```

Dirección I2C 0x70, comparte el bus con los sensores y la OLED. Se activa con
`GPSDO_HT16K33`. Se informa al arrancar como
`HW: HT16K33 clock display OK (I2C 0x70)`, así que un módulo ausente o mal
direccionado se detecta de inmediato.

### TM1637 — 4 o 6 dígitos, dos hilos

```
 ┌────────────────┐
 │  1 4 : 3 2     │   versión de 4 dígitos: HH:MM
 └────────────────┘
 ┌────────────────────┐
 │  1 4 : 3 2 : 4 5   │   versión de 6 dígitos: HH:MM:SS
 └────────────────────┘
```

Usa sus propios pines CLK/DIO, no I2C. `GPSDO_TM1637` selecciona la variante de
4 dígitos y `GPSDO_TM1637_6` la de 6. Los dos puntos parpadean en los segundos
pares.

> **TM1637 y la LCD 20×4 no pueden usarse a la vez** — compiten por los mismos
> pines. Elige uno al compilar.

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
| `CT` | Calibrar + autoajustar: medir K, derivar PID para todos los algos 3-9 (+ LTIC 10 & 11; auto-save) |
| `T [baud]` | Túnel GPS por USB para u-center — NMEA/UBX bidireccional limpio (la telemetría pasa a Bluetooth si existe, si no se silencia); baud opcional de la UART GPS, conservado al salir; sale tras 300 s |
| `SP <n>` | Fijar el DAC PWM directamente (1–65535), omite el algoritmo |
| `DAC` | Salida de tensión de control: ruta, código de 24/16 bits y exacto, Vctl medido, tamaño de paso en µHz y df/f (las cifras en Hz necesitan `CT`) |
| `SPAN [CLR FULL\|REDUCED]` | Puente de span del EFC en PB14 (`GPSDO_SPAN_SENSE`): la posición en que está y la calibración `CT` de cada posición — una por span, conmutada y reasignada cuando el puente se mueve; `CLR` olvida una |
| `RH` | Modo de informe: legible por humanos (por defecto) |
| `RD` | Modo de informe: delimitado por tabuladores |
| `RP` | Pausar el flujo de datos serie/BT |
| `RR` | Reanudar el flujo de datos serie/BT |
| `SW` | Marcas de agua de pila de las tareas FreeRTOS, heap libre, fuente del uptime y el error medido del reloj del MCU frente al GPS (diagnóstico) |
| `CS` | Estadísticas de corrección: cuánto trabaja el lazo, y en df/f tras `CT` |

### Control

| Comando | Descripción |
|---------|-------------|
| `MH` | Activar modo holdover (manual) |
| `MD` | Activar modo disciplinado |
| `LA [0-12]` | Seleccionar / mostrar algoritmo de control |
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
mayúsculas. Hay 503 zonas integradas.

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
El estado persiste en el flash-ring, así que un reinicio en caliente (`RB`) reanuda
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
| `LIV [v]` | Intervalo de actualización LOCK (segundos, por defecto 300, 1..600 s) |
| `LPOL [-1/0/1]` | Polaridad del detector de fase (0 = auto) |
| `LCV` | Muestra el voltaje TIC actual (ayuda en la calibración) |

### Orden de calibración: primero `CT`, después `LC`

**Ejecuta `CT` antes que `LC`.** No son independientes: `LC` barre el PWM para
alcanzar una tasa de fase objetivo, y para saber cuánto desviarlo necesita K — la
pendiente en Hz por LSB de tu OCXO, que es justo lo que mide `CT`. Sin ella, `LC`
recurre a un valor genérico de 3000 LSB/Hz y la calibración sale escalada por lo
que tu oscilador se aparte de esa suposición.

La trampa es que **la respuesta equivocada no parece equivocada**. En una placa,
`LC` antes de `CT` dio ns_per_volt = 1592,8 y un resultado WEAK; tras `CT` la
misma placa dio 921,2 y PASSED — un factor de 1,7, sin nada en la primera
ejecución que hiciera sospechar.

El orden para una placa nueva:

1. `CT` — mide K, deriva los juegos de PID y los guarda automáticamente
2. `LC` — calibra el detector de fase, se guarda solo si pasa
3. `LPOL -1` o `LPOL 1` si el lazo indica que la polaridad no está fijada, luego `ES LTIC`
4. `LA 10` o `LA 11`, luego `ES ALGO`

`LC` avisa si no se ha ejecutado `CT`, pero continúa igualmente — repetirlo cuesta
tres minutos y hay motivos legítimos para barrer primero.

### Algoritmo 11 — LTIC-Lars (lazo PI continuo)

Un único lazo PI continuo sin máquina de estados ACQ/DPLL/LOCK, según el
controlador GPSDO original del difunto Lars Walenius. Comparte la calibración
del detector con el algoritmo 10 (`LC`), así que una sola calibración sirve a
ambos lazos. Las etiquetas de tendencia usan el mismo vocabulario que el
algoritmo 10: **ACQ** (guiado por frecuencia, detector de fase saturado),
**PLL** (guiado por fase) y **LOCK**.

| Comando | Descripción |
|---------|-------------|
| `LG [v]` | Ganancia. **0 = auto**, derivada de la calibración `CT`; un valor no nulo fija una escala manual |
| `LD [v]` | Amortiguación |
| `LTC [s]` | Constante de tiempo del lazo, 1..600 s |
| `LFD [n]` | Divisor del filtro — la constante del prefiltro es `LTC / n` |
| `LTO [adc]` | Offset del TIC: objetivo de fase en cuentas de ADC |
| `LPL [ns]` | Límite de fase de enganche — anchura de la ventana |
| `LPF [n]` | Factor de enganche: la ventana debe mantenerse `LPF × LTC` segundos |
| `LTK [v]` | Feed-forward del coeficiente de temperatura (0 = desactivado) |
| `LTR [adc]` | Referencia de temperatura, cuentas de ADC |

Ninguno se guarda automáticamente; `ES LTIC` los almacena junto con los
parámetros del algoritmo 10.

### Ventana de promediado del término de amortiguación — `FA` / `FAD` / `FAL`

| Comando | Descripción |
|---------|-------------|
| `FA [n]` | Fija la ventana de promediado del término de amortiguación en **ambas** etapas LTIC (10/100/1000 s) |
| `FAD [n]` | Solo la etapa DPLL |
| `FAL [n]` | Solo la etapa LOCK |

100 es el valor histórico y no cambia nada. Una ventana más corta es la
candidata para corregir un ciclo límite cuyo periodo sea un par de veces la
longitud de promediado — el retardo de grupo de una media larga puede quedar
cerca de la cuadratura con la respuesta del propio lazo.


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

### Entrada de fase LTIC (Lars' TIC)

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

## Almacenamiento de ajustes (flash-ring)

Los ajustes viven en un anillo con nivelado de desgaste en el **sector 7** del
flash (0x08060000, 128 KB) — el último, para que el firmware conserve el máximo
espacio contiguo por debajo. No hay EEPROM, ni emulada ni de ningún otro tipo.

Los registros llevan tipo, así que el bloque de ajustes y los datos aprendidos
comparten un mismo anillo sin colisionar. Cada slot lleva un CRC16, los slots van
numerados por secuencia y gana el más reciente válido, y cada escritura se relee y
verifica. Un corte de corriente a mitad de escritura deja por tanto intactos los
ajustes anteriores, en vez de un medio registro corrupto.

El bloque de ajustes guarda el PWM y el algoritmo, los juegos PID de los
algoritmos 3-9, la calibración LTIC y las ganancias por etapa, los parámetros
LTIC-Lars, las ventanas de amortiguación, los offsets de sensores, los
indicadores de arranque y la zona horaria. Está versionado: un bloque escrito por
un firmware anterior con distinto formato se rechaza en lugar de malinterpretarse,
y la placa arranca con los valores por defecto.

| Comando | Efecto |
|---------|--------|
| `ES [grupo]` | Guardar todo, o un grupo: `TZ` / `PID` / `LTIC` / `FLAGS` / `ALGO` / `PO` |
| `ER` | Recuperar los ajustes del anillo |
| `EE` | Borrar los ajustes — volver a los valores por defecto |
| `EW` | Desgaste del flash: ciclos de borrado y slots usados |
| `CR YES` | Reinicio en frío: borrar el anillo por completo |

Las preferencias se guardan solas; el ajuste del lazo requiere un `ES` explícito.
En ambos casos la respuesta indica qué se aplicó — véanse las notas de
persistencia en la sección del CLI.

### Nivelación de desgaste de Flash (datos vivos)

Los datos “vivos” — deriva/amortiguación aprendida (`LRN`), calibración LC y
el último PWM — cambian mucho más a menudo que los ajustes, por lo que se
almacenan junto al bloque de ajustes, en un **buffer circular con
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
Los datos vivos van al anillo junto con los ajustes; `ES` los guarda como
respaldo.

### Conservar datos vivos al re-flashear el firmware

- **Bootloader / DFU / Arduino IDE** toca solo los sectores de firmware (0–5);
  el anillo del sector 7 sobrevive.
- **Borrado total del chip con J-Link/ST-Link** borra todo. Para conservar
  calibración y aprendizaje, borra solo los sectores 0–5:
  `erase 0x08000000 0x0803FFFF`, luego `loadbin firmware.bin 0x08000000`.
- Si el anillo se borra, el firmware reaprende/recalibra desde los valores por
  defecto — nada se rompe, solo se pierde el ajuste acumulado.

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
después para guardar los valores ajustados en el flash-ring. A diferencia del
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

## Algoritmo 12 — acumulador multinivel

Según el diseño de Alan Cashin (MIS42N en EEVblog). Cualquier otro lazo aquí tiene
una única constante de tiempo, y esa constante es un compromiso que nadie gana:
medido contra una referencia de rubidio, `LTC 60` es hasta 1,58× mejor por encima
de 800 s mientras que `LTC 240` es hasta 1,44× mejor entre 10 y 400 s. Hay que
elegir uno.

Este algoritmo no elige. Las lecturas se acumulan en niveles — el nivel n abarca
2^n segundos — y la corrección se aplica en el nivel **más bajo** cuyo error supere
el límite de ese nivel. Un error grande actúa en dos segundos; uno pequeño espera a
un promedio más largo. No hay `LTC` que ajustar.

Los niveles salen del patrón de bits del contador de segundos, no de una matriz de
búferes: se inspecciona desde el bit menos significativo hacia arriba y se para en
el primer cero. Once niveles — de 2 s a 2048 s — cuestan 22 bytes.

### Qué mide

**Fase en nanosegundos, del detector LTIC.** La primera versión alimentaba el error
de cuenta de TIM2 en hercios enteros y estaba ciega: un oscilador disciplinado se
sitúa muy por debajo de 1 Hz, así que ese campo leía cero en el 83% y el 95% de las
muestras en dos ejecuciones y el algoritmo nunca corregía. La fase se integra donde
una cuenta de frecuencia de un segundo no lo hace — un error de 1e-11 es invisible
en la cuenta pero se convierte en 25 ns de fase en 2500 s.

**El detector es obligatorio.** Una versión anterior recurría a integrar el error
de cuenta en placas sin detector; eso no recuperaba la fase, acumulaba un paseo
aleatorio de ruido de cuantización que cruzaba un límite, disparaba una corrección,
saturaba el recorte y llevaba el detector contra el raíl — medido en 6000 cuentas
de oscilación del PWM en 148 correcciones, con el detector contra el raíl el 58%
del tiempo. `LA 12` ahora rechaza sin `GPSDO_LTIC`, y cuando el detector está
montado pero no lee, el algoritmo se detiene en lugar de adivinar.

Como los demás algoritmos LTIC, arma el picDIV. Sin eso la rampa del detector queda
contra el raíl, la lectura nunca es válida y el algoritmo vuelve en silencio a
estar ciego. El puente espera varias lecturas inservibles consecutivas antes de
tocar el divisor y luego hace una pausa mientras la fase se asienta. La tendencia
muestra `ARM` mientras tanto.

### Umbrales calculados a partir del ruido medido

Los límites por nivel se tomaron primero de la tabla de Alan escalada por la razón
entre pasos de contador. Esa es la magnitud equivocada: el umbral debe superar el
**ruido** de la medida de fase — detector, diente de sierra y el vagabundeo del
propio GPS juntos — y ese difiere entre montajes por razones que un tamaño de paso
no captura.

Medido en una placa: fase media −1 ns con una desviación típica de 462 ns. El
oscilador estaba bien ajustado y toda esa dispersión era ruido, mientras el umbral
del nivel 0 estaba en 462 ns y lo cruzaba el 41% de las muestras. Resultado: 620
correcciones en 1685 segundos, la jerarquía reiniciándose cada 2,7 s y sin alcanzar
nunca el nivel 2.

El firmware estima ahora el ruido de forma continua y fija con él el umbral de cada
nivel. El umbral se aplica a la expresión de prueba `|3b − a|`, cuya desviación es
`σ·√(2^L)·√10` — no a la fase media, para la que sería `σ/√N`. Seis sigma sobre la
magnitud correcta deja el intervalo entre correcciones en torno a un minuto.

`ML` informa del ruido medido y de si los límites lo siguen; la telemetría lo lleva
como `sig=`. Poner `MG` por encima de cero detiene el autoajuste y conserva la
tabla fijada a mano.

### Comandos

| Comando | Efecto |
|---------|--------|
| `LA 12` | Seleccionar el algoritmo |
| `MG [v]` | Ganancia en LSB por ns. 0 = derivarla de `CT` y autoajustar los umbrales |
| `MR [n]` | Forzar una corrección al alcanzar el nivel n, digan lo que digan los límites |
| `MLP <n> [ns]` | Límite de fase del nivel n; sin valor imprime el actual |
| `MF [0-3]` | De dónde salen los límites, con independencia de `MG`: 0 seguir a `MG`, 1 tabla guardada, 2 fórmula sigma, 3 medidos |
| `MFT [s]` | Sólo `MF 3`: segundos objetivo entre correcciones disparadas sólo por ruido (por defecto 3600) |
| `ML` | Lista ganancia, nivel de forzado, ruido medido y los once límites |
| `ES ALGO12` | Guardar el bloque en el anillo de flash |

### Sin ajustar, y por qué

Solo **un** límite se dedujo alguna vez. La especificación de Alan era 10 MHz
±0,01 Hz, que son 125 ns de fase en 128 segundos — eso fija la entrada de 128 s.
Podría haber usado 64 ns a 64 s, pero un módulo barato con mala señal vagabundea
±100 ns y generaría errores falsos. Del resto dice llanamente: *«funcionan la mayor
parte del tiempo pero no se optimizaron en modo alguno»*.

No hay pues nada que reproducir fielmente más allá de ese único anclaje — de ahí el
autoajuste, y de ahí que toda la tabla siga siendo editable y se guarde por placa.

---

## Autoevaluación sin referencia — `CS`

El algoritmo 11 se validó contra un patrón de rubidio en el banco de otra persona.
Casi nadie que construya esto tendrá uno, y sin él quedan la palabra del autor y
un rectángulo verde. `CS` ofrece algo mejor.

La corrección que aplica el lazo es el error que acaba de observar, así que el
tamaño de esas correcciones dice si la disciplina funciona — y la referencia es el
GPS, de modo que no existe nada mejor con lo que comparar la frecuencia. El
firmware ya calculaba todos estos números y los descartaba. La idea es de Alan
(MIS42N en EEVblog), cuyo propio diseño se apoya justamente en esto y por eso no
necesita un patrón secundario.

`CS` informa del RMS de la corrección sobre las últimas 100, 1 000, 10 000 y
100 000 correcciones, en cuentas de DAC y — una vez que `CT` ha medido la
pendiente del oscilador — en frecuencia fraccional, directamente comparable con
una cifra de ADEV. También informa del sesgo constante, no nulo cuando el lazo
sigue una deriva real en lugar de ruido.

**Las ventanas cuentan correcciones, no segundos**, porque el ritmo de corrección
depende del algoritmo: el algoritmo 11 corrige una vez por segundo, el 10 una vez
por `LIV`. Etiquetarlas en minutos habría significado una cosa con un algoritmo y
sesenta veces eso con el otro. `CS` mide el intervalo real e imprime lo que
abarcan las ventanas en tiempo de reloj, para que el lector no tenga que
calcularlo — a una corrección por segundo, 100 000 cubre unas 28 horas.

Son pesos exponenciales, no ventanas duras: alrededor del 63% del peso cae dentro
de N correcciones y el 95% dentro de 3N, así que los datos antiguos se desvanecen
en vez de desaparecer de golpe. Eso cuesta cuatro multiplicaciones-suma por
corrección y nada de memoria, mientras que un búfer de 100 000 muestras ocuparía
la mayor parte de la RAM disponible para responder lo mismo sin mejorarlo.

**Se cuenta solo con el lazo enganchado y sin calibración en curso.** La rampa de
adquisición, los tres saltos que `CT` da al medir la pendiente del VCO y el
barrido de `LC` son órdenes, no correcciones; uno solo dominaría la media horaria
mucho después de haber terminado. Los algoritmos 0-9 no tienen estado de enganche
sobre el que basar la compuerta, así que quedan excluidos y `CS` lo dice en lugar
de informar de un número sin significado definido.

> **Lo que no te dice.** Mide si el LAZO ESTÁ ASENTADO, no si la SALIDA ES BUENA,
> y ambas cosas coinciden únicamente mientras el detector de fase sea fiable. Un
> detector ruidoso hace que el lazo persiga ruido: las correcciones crecen, `CS`
> las informa fielmente, y el oscilador estaba bien hasta que el lazo lo empeoró.
> Nada medido desde dentro del lazo puede ver eso. Lee una cifra pequeña como «no
> está peleando con nada» — necesario, no suficiente. La señal útil es una cifra
> creciente.

---

## Tensión de control de 24 bits — PWM con dithering (`GPSDO_PWM_DITHER`)

La idea es de Alan Cashin (MIS42N en EEVBlog), de su Budget GPSDO: haz correr el
PWM con menos bits de los que necesitas y varía el ciclo de trabajo de un periodo
al siguiente, de modo que sea la **media** la que lleve los bits que faltan.

**La ganancia es la portadora, no los bits de más.** El rizado hay que filtrarlo
por debajo de un paso de salida, y lo difícil que eso resulte depende de cuánto
se separe la portadora del codo del filtro:

| | portadora | codo del filtro | constante de tiempo |
|---|---|---|---|
| PWM de 16 bits | 2 kHz | 0,7 Hz | 230 ms |
| dithering de 13 bits (por defecto) | 12,2 kHz | 4,2 Hz | 38 ms |
| dithering de 12 bits | 24,4 kHz | 8,4 Hz | 19 ms |

El retardo del filtro entra en el lazo directamente como desfase, así que un
filtro seis veces más corto vale más que la resolución. La resolución viene de
regalo.

**Cómo funciona aquí.** Alan hace el dithering en una interrupción de temporizador
porque un PIC no tiene DMA. Aquí serían 12 000 interrupciones por segundo
compitiendo con la captura del 1PPS — la única interrupción de este firmware que
no puede retrasarse. Pero el patrón de dithering para un valor constante es
periódico: se repite cada 2^(24−N) periodos. Así que se calcula una vez en una
tabla y se reproduce por DMA hacia el registro de comparación, sin que la CPU
toque nada entre cambios de valor.

La media es **exacta**, no aproximada: la tabla contiene exactamente Y entradas de
valor X+1 entre 2^(24−N), de modo que promedia X + Y/2^(24−N), que es el valor de
24 bits por construcción. Reproducirla no puede derivar.

| `GPSDO_PWM_DITHER_BITS` | tabla | RAM (dos búferes) | portadora | CPU |
|---|---|---|---|---|
| 12 | 4096 entradas | 16 KB | 24,4 kHz | 0,025% |
| 13 (por defecto) | 2048 entradas | 8 KB | 12,2 kHz | 0,012% |

El mismo pin que el PWM sencillo — **PB9, TIM4 CH4** — así que el filtro y el
cableado existentes no cambian. TIM4_UP gobierna DMA1 Stream 6 Channel 2; el tic
de 2 Hz está en TIM9 y la cadena del 1PPS en TIM2/TIM3, de modo que no se
perturba nada más. Dos búferes en modo doble búfer por hardware hacen que un
cambio de valor nunca produzca un glitch en el pin, y ambos se rellenan en cada
escritura: rellenar sólo uno deja al otro reproduciendo el código anterior hasta
la escritura siguiente, lo que pone en la salida una onda cuadrada de unos 3 Hz.

**Activado** en el `gpsdo_config.h` que se distribuye desde v1.05 — en v1.04
estaba desactivado por defecto, mientras se comprobaba la ruta de salida.
Comentar `GPSDO_PWM_DITHER` vuelve al PWM sencillo de 16 bits; el pin, el filtro
y el cableado son los mismos en ambos casos, así que fuera de la placa no cambia
nada. Necesita `configUSE_MUTEXES` e `INCLUDE_xTaskGetSchedulerState`, que este
proyecto fija ahora explícitamente. Es mutuamente excluyente con
`GPSDO_DAC_EXT` — activar ambos es un error de compilación, porque los dos
gobiernan la tensión de control.

### Qué hace el lazo con los 8 bits bajos

Desde v1.05 el lazo de control conserva la fracción de su corrección en vez de
truncarla. Sobre la planta medida aquí, un paso de 16 bits son unos 320 µHz —
3,2e-11 de 10 MHz, más grueso que los 4e-12 que el lazo mantiene a lo largo de
10 000 s, a los que llegaba cazando entre códigos contiguos. Con la fracción
conservada el paso es **1,25e-13** y la caza desaparece.

La fracción pertenece a la capa DAC, no al lazo. El valor de control se escribe
desde 21 sitios y veinte de ellos — los barridos `CT` y `LC`, las rampas de
adquisición, el gobierno en holdover, `SP` — son gruesos a propósito, y todos
ellos borran la fracción con sólo llamar a `gpsdo_dac_write16()`. Por encima de
esa capa no cambió nada: las pantallas, la línea de telemetría, el anillo en
flash y el bloque de ajustes siguen viendo un código liso de 16 bits.

Ejecuta `DAC` para ver qué ruta está compilada, el código en las tres vistas y el
tamaño de paso en µHz y en partes fraccionarias. Un código de 24 bits que no es
múltiplo de 256 es la prueba de que quien gobierna el pin es la ruta fina. Las
cifras de paso necesitan la ganancia de planta de `CT`; sin ella la orden lo dice
en lugar de imprimir un número por defecto.

---

## DAC SPI externo — planeado, no implementado

`GPSDO_DAC_EXT` cambia la tensión de control del PWM de 16 bits a un DAC SPI
externo. Activarlo hoy da un error de compilación deliberado: `gpsdo_dac_ext.cpp` es un
esqueleto sin dispositivo elegido.

El PWM da unos 50 µV por paso a 3,3 V, cerca de 2,7e-11 fraccional en un oscilador
de 5,3 Hz/V. Un integrado de 18 bits con una referencia diseñada para el trabajo
alcanza unos 17 µV, cerca de 9e-12, sin retardo de filtro dentro del lazo.

No hace falta SPI por hardware, ni está disponible — SPI1 pertenece al TFT y todos
los pines de SPI2 de este encapsulado están ocupados. No importa: el DAC se escribe
una vez por segundo, así que moverlo por software cuesta del orden de un
microsegundo. Los pines sugeridos son PB0, PB2 y PB4, elegidos para evitar PB6/PB7,
que parecen libres pero son los pines por defecto de I2C1 que reclama
`Wire.begin()`.

Los 23 sitios que antes escribían el PWM pasan ahora por `gpsdo_dac_write16()`, de
modo que añadir un dispositivo significa rellenar una función en lugar de editar 23
llamadas.

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
#define GPSDO_PWM_DITHER         // tension de control de 24 bits (PWM con dithering, PB9)
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

## Hoja de ruta

### Planeado

**Un DAC SPI externo para la tensión de control.** El PWM de 16 bits da unos 50 µV
por paso a 3,3 V, cerca de 2,7e-11 fraccional en un oscilador de 5,3 Hz/V. Un
integrado de 18 bits con una referencia diseñada para el trabajo alcanza unos
17 µV, cerca de 9e-12, sin retardo de filtro dentro del lazo.

El integrado ya está elegido y escrito: el **AD5680**, 18 bits, tras una
referencia externa REF5045, movido por software en CS/SCK/MOSI = PB4/PB0/PB2 en
el PCB de Dan Wiering. No hace falta SPI por hardware ni está disponible — el DAC
se escribe una vez por segundo, así que moverlo por software cuesta
microsegundos. `GPSDO_DAC_EXT` lo activa y es mutuamente excluyente con
`GPSDO_PWM_DITHER`, porque ambos gobiernan la tensión de control. El driver
compila y enlaza para cortex-m4 en `tools/hostcheck`; la prueba en hardware le
toca a Dan. Queda abierto: el rango de salida frente al EFC del oscilador y la
selección de ruta en tiempo de ejecución `DAC [PWM|DITH|EXT]` que acompaña al PCB
con puentes.

Conviene saber dónde está el límite útil. A 24 bits el paso es de 197 nV; el ruido
térmico en 10 kΩ sobre 1 Hz ronda los 13 nV, así que el ruido Johnson no es el
obstáculo — pero un buen amplificador zero-drift aporta unos 180 nV de pico a pico
entre 0,1 y 10 Hz, y una referencia de precisión alrededor de 1 µV. **18 bits es el
punto óptimo y 20 el techo razonable**: más allá, la cadena analógica no puede
entregar lo que promete el convertidor.

### Intentado y abandonado

Ambos se probaron en serio, y ambos quedan registrados aquí porque las razones son
más útiles que las ideas.

**Un DAC delta-sigma de 24 bits en un pin libre, alimentado por DMA.** Construido,
probado en anfitrión y luego medido — momento en el que dejó de ser atractivo. El
comando tiene 24 bits, pero la resolución que llega al oscilador la fija cuánto
promedia el filtro analógico: 13,6 bits efectivos sobre 4 096 bits de promediado,
19,0 sobre 262 144, y 24,0 solo tras 16,7 millones, o sea 43 segundos a 390 kHz. Un
filtro lo bastante rápido como para no retrasar el lazo rinde unos 18 bits, a costa
de un 6% de la CPU funcionando sin parar más una referencia de precisión, una
puerta CMOS y un filtro activo de cuarto orden. Subir la frecuencia de bit no lo
salva: 24 bits dentro de un filtro de 32 ms exigirían 524 MHz.

Una versión anterior de este documento prometía una mejora de 320 veces. Era falsa
— comparaba la anchura del comando con el tamaño de paso del PWM, lo que no compara
nada. El código sigue en el árbol, marcado como callejón sin salida: el núcleo del
modulador es correcto y está probado, y un callejón sin salida medido vale más que
una idea sin probar.

**Dos puertos USB CDC**, uno para el CLI y otro como túnel GPS transparente para
u-center. Imposible sin bifurcar la capa USB del core: la pila de STM32duino es de
clase única, la petición de dispositivos compuestos lleva años abierta y el core
3.0.0 no cambió nada al respecto. Descriptores escritos a mano serían unos cientos
de líneas que se romperían con cada actualización del core — exactamente lo que le
pasó a TFT_eSPI con 3.0.0. El modo paralelo con Bluetooth ya consigue el mismo
resultado: `T` tuneliza el GPS por USB mientras el CLI se queda en Bluetooth, sin
molestias.

### No planeado

**Los algoritmos 0–9 están congelados.** Permanecen en el firmware, siguen
funcionando y se respetan los ajustes ya afinados para ellos. Pero el desarrollo se
ha detenido: el algoritmo 11 midió entre 2 y 3 veces mejor que un algoritmo 10
correctamente ajustado en los tiempos de promediado donde un GPSDO se gana su
sitio, sobre hardware independiente y contra una referencia de rubidio. El esfuerzo
va a los algoritmos 10, 11 y 12.

Se conservan en vez de borrarse porque doce enfoques documentados del mismo problema
de control valen más como referencia que un árbol de fuentes más limpio, y porque
la configuración existente de nadie debería romperse. No habrá funciones nuevas
ahí, y conviene tratar el feed-forward autoaprendido (`LRN`) como parte de ese mismo
grupo congelado: sirve solo a los algoritmos 3–9.

---

## Requisitos

- **Placa**: WeAct BlackPill STM32F411CE o F401CCU6
- **IDE**: Arduino IDE con core STM32duino **2.2.0 – 2.12.0**
  (véase la nota sobre el core 3.0.0 más abajo)
- **Bibliotecas**: STM32duino FreeRTOS, TinyGPS++, U8g2,
  Adafruit AHTX0, Adafruit BMP280, Adafruit INA219,
  hd44780 (para LCD), TFT_eSPI (para TFT) (los ajustes viven en el flash-ring del chip, así que no hace falta la biblioteca EEPROM)
- **Ajustes de compilación**: Tools → C Runtime Library → Newlib Nano + Float Printf/Scanf

### Core STM32duino 3.0.0 — todavía no soportado

**Compila con el core 2.12.0 o anterior.** El core 3.0.0 (publicado el 23 de
julio de 2026) trae dos cambios que sus propias notas de versión marcan como
mayores: el **despliegue de ArduinoCore-API** y HALv2 para la serie STM32C5xx.
Tres consecuencias importan aquí:

1. **`ltoa()` ha desaparecido.** Es una extensión no estándar que suministraba
   el core anterior; el firmware la usa en cuatro sitios. En 3.0.0 no compilan y
   tendrían que pasar a `snprintf(buf, n, "%ld", …)`.
2. **`HardwareSerial` deja de ser la clase concreta.** Bajo ArduinoCore-API es
   una interfaz abstracta, así que `HardwareSerial Serial2(PA3, PA2)` ya no
   instancia. Lo habitual es un typedef que elija la clase correcta según la
   versión del core.
3. **TFT_eSPI deja de controlar el panel.** Este es el bloqueante. La biblioteca
   usa `SPIClass` solo para configurar los pines y levantar el periférico, y
   luego habla con el controlador mediante `HAL_SPI_Transmit()` en bruto sobre
   su propio manejador. Cuando la capa SPI subyacente cambia, el panel puede
   quedarse sin una secuencia de inicialización válida — el síntoma observado es
   una **pantalla en blanco**, con el CLI y la telemetría funcionando con normalidad.

Los puntos 1 y 2 son menores y podrían condicionarse por versión en cualquier
momento. El punto 3 reside en TFT_eSPI, no en este firmware, así que 3.0.0 debe
esperar a la biblioteca. Dado que 2.12.0 es una versión completa y nada de aquí
necesita 3.0.0, quedarse en 2.12.0 no cuesta nada.

> Conviene saberlo al buscar: la mayoría del material sobre «Arduino core 3.0.0»
> en la red trata del core **ESP32**, un proyecto distinto cuya guía de migración
> a 3.0 no aplica a STM32.

---

## Licencia

Publicado bajo los mismos términos que el proyecto original de André Balsa.

