# GPSDO FreeRTOS v0.91

[English](README.md) | [Polski](README_PL.md) | **Español**

Firmware en tiempo real (FreeRTOS) para un oscilador disciplinado por GPS
(GPSDO) sobre la plataforma STM32 BlackPill (WeAct F411CE / F401CCU6).

## Créditos

| Rol | Persona / fuente |
|-----|------------------|
| Autor del port a FreeRTOS, algoritmos 3–9 | **J. M. Niewiński** — [repositorio](https://github.com/jmnlabs/GPSDO_FreeRTOS) |
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
  AHT  BMP  INA        │ ILI9488    │  480x320 (sin probar)
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

- **OLED 128×64** I2C (SH1106 / SSD1306 / SSD1309)
- **LCD 20×4** I2C (HD44780 + PCF8574T)
- **TM1637** (reloj de 4 o 6 dígitos)
- **TFT 320×240** SPI (ILI9341 / ST7789, biblioteca TFT_eSPI)
- **TFT 480×320** SPI (ILI9488, biblioteca TFT_eSPI) — *sin probar, todavía
  sin panel disponible; el diseño 320×240 se escala automáticamente*
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

> **ILI9488 está sin probar** — todavía no hay panel disponible. La pantalla
> de operación 320×240 y el splash se escalan automáticamente a 480×320 en
> tiempo de compilación (ancho ×1.5, alto ×1.33, con las fuentes mapeadas un
> tamaño hacia arriba). El código compila y se ha verificado que la geometría
> cabe en el panel, pero no se ha ejecutado en hardware real. Trátelo como
> experimental hasta confirmarlo. El ILI9488 por SPI es apreciablemente más
> lento (480×320, color de 18 bits), así que los redibujados son más visibles
> que en los paneles pequeños.

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
│ GPSDO v0.91-rtos        LMT 14:32:45 Thu   │ ← header bar (navy)
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
│ BMP: 23.40C 1013hPa  │ AHT: 22.10C 45.3%rH │
│ Vph: 2.615V 652ns    │ Vdd: 3.30V          │
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
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define SPI_FREQUENCY 27000000
```

Para el panel **ILI9488 (480×320)**, cambie el driver y las dimensiones y
añada `LOAD_FONT6` (la fuente de frecuencia más grande que usa el diseño
escalado):

```c
#define ILI9488_DRIVER
#define TFT_WIDTH  320
#define TFT_HEIGHT 480
// ...las mismas líneas TFT_MISO/MOSI/SCLK/CS/DC/RST/RGB_ORDER de arriba...
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6              // fuente grande de frecuencia en el diseño escalado
#define SPI_FREQUENCY 27000000
```

Luego active `GPSDO_TFT_ST7789`, `GPSDO_TFT_ILI9341` o `GPSDO_TFT_ILI9488` en
`gpsdo_config.h`.

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
| `TO [n]` | Mostrar / fijar el desfase de hora local manualmente (horas, −23..23) |
| `TO A` | Zona horaria automática: zona desde la posición GPS + regla DST de la UE |
| `PO [f]` | Mostrar / fijar el offset de presión |
| `AO [f]` | Mostrar / fijar el offset de altitud |
| `SV [0\|1]` | Survey-in / Time Mode en el receptor de temporización (guardado por `ES`, aplicado en el siguiente arranque) |

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
| `LC` | **Autocalibración** de ns/V, zero-offset y rango (auto, ~70 s) |
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
línea de telemetría `Learn:` muestra `qErr=…ns` para el algoritmo 10. El filtro
RC de la ruta TIC debe ser lo bastante lento como para asentarse entre pulsos
de 1 Hz (p. ej. 51 kΩ/1 µF, τ≈51 ms) para que cada lectura de fase se empareje
limpiamente con el qErr de su pulso.

---

## Notas del hardware TIC — integrador de rampa con puerta (Kaashoek)

> **Errata (v0.90, corrige la sección de abajo).** El análisis siguiente
> describe el detector como un "detector de ciclo de trabajo" que necesita un
> RC grande (51 kΩ/1 µF). Eso resultó **incorrecto**. Confirmado con el post de
> André Balsa (EEVblog) y capturas de osciloscopio: es el **integrador de rampa
> con puerta de Kaashoek** (esquema rev 0.4). Un par de flip-flops 74 convierte
> la diferencia de fase entre dos señales 1PPS en un pulso — **la carga empieza
> en el flanco de subida del 1PPS del GPS y termina en el del picDIV** — que a
> través de un diodo Schottky carga C13; el ancho del pulso es la fase. Por eso
> el RC correcto es el **pequeño** (1 kΩ/1 nF, τ ≈ 1 µs), igual que el original
> de Lars, y **no** un filtro grande. La lectura del ADC debe caer en el pico de
> la rampa (~50 µs tras el flanco PPS), no 2 s después. La calibración `LC`
> ancla `zero_offset` en ~1,85 V (punto repetible del detector, lejos de las
> zonas muertas medidas por Dan Wiering) y lee ns/V de la pendiente local. Para
> la versión en inglés/polaco, consúltese la sección corregida equivalente. La
> traducción completa de esta sección al español está pendiente.

## Notas del hardware TIC — por qué el filtro RC *no* es el 1 nF de Lars

> **Errata (hallazgos v0.90).** El análisis siguiente describía una
> construcción en la que el segundo flip-flop NO estaba reloj­ado por 10 MHz
> (ambos FF veían solo las dos señales 1PPS — tal como está dibujado en el
> esquema base, cuyo bloque «Kaashoek 1 ns» parece carecer de la conexión de
> 10 MHz al CLK de U3B). Tal detector es un comparador BINARIO promediado por
> jitter (una sigmoide, sin diente de sierra), y solo entonces tiene sentido
> un RC grande (51 kΩ/1 µF). Con el detector completado según el original de
> Kaashoek — 10 MHz en el segundo FF, carga por diodo — la ventana es un
> periodo de 10 MHz (100 ns) y el RC pequeño ORIGINAL (1 kΩ/1 nF, τ = 1 µs,
> nota «R8×C13 = 1000 ns») es correcto, exactamente como en el integrador de
> rampa de Lars descrito abajo.


Este diseño desciende del Arduino GPSDO de Lars Walenius (vía la v0.06c de
André Balsa), pero el **front-end del detector de fase es fundamentalmente
distinto**, y esa diferencia costó tiempo real de banco — tres flip-flops (dos
74HC74 a 5 V, finalmente un 74LVC74 a 3,3 V) y un largo desvío — antes de
entenderla. Se documenta aquí para que la próxima persona que porte esto no lo
repita.

### El original de Lars: un integrador de rampa (el RC pequeño es esencial)

El TIC de Lars usa un **detector de fase-frecuencia HC4046 alimentado a
1 MHz**, un diodo Schottky (1N5711) y un RC pequeño (≈3,7 kΩ / 1 nF, τ≈3,7 µs)
hacia el ADC, con una fuga de 10 MΩ a masa. El 4046 emite un pulso cuyo *ancho
es igual a la diferencia de fase*; el diodo carga el condensador **solo durante
ese pulso**, así que el voltaje capturado es proporcional al ancho del pulso —
una rampa directa tiempo-a-voltaje. El ADC la lee una vez por segundo, luego
los 10 MΩ descargan lentamente el condensador antes del siguiente pulso.

Aquí el RC **debe** ser pequeño: el 4046 corre a 1 MHz (periodo 1 µs) y el
pulso de fase es 0–1000 ns, así que la constante de tiempo de carga ha de ser
del orden de un microsegundo para que el condensador siga linealmente el ancho
del pulso. Lars midió 500 ns → ADC ≈530, 1000 ns → ≈1000: excelente
linealidad, resolución ~1 ns. Un RC *grande* apenas movería el condensador
durante un pulso de 1 µs y destruiría la resolución. Así que 1 nF no era
arbitrario — estaba ajustado a una ventana de carga temporizada a escala de µs.

### Este diseño: un detector de ciclo de trabajo (el RC grande es necesario)

Nuestro front-end es un **flip-flop tipo D de la familia 74** (`xx74`), no un
4046. Produce una salida cuyo **ciclo de trabajo** — no un único pulso
temporizado — codifica la fase, a baja frecuencia de repetición (10 MHz
dividido por picDIV). Para convertir eso en un voltaje DC de fase, el RC debe
**promediar el ciclo de trabajo**, es decir actuar como un verdadero filtro
paso-bajo con una frecuencia de corte *muy por debajo* de la tasa de
conmutación del detector.

Poner el 1 nF de Lars sin cambios (empezamos con 1 kΩ/1 nF, τ=1 µs, f_c≈159
kHz) hace lo contrario de filtrar: el corte queda muy *por encima* de la tasa
del detector, así que el ADC muestrea el rizado crudo en vez de un promedio
asentado. En el banco esto apareció como un **span de 14 mV** durante `LC` — la
calibración lo rechazó correctamente como físicamente imposible. Solo tras
cambiar a **51 kΩ / 1 µF (τ≈51 ms, f_c≈3,1 Hz)** el condensador promedió el
ciclo de trabajo en un voltaje DC de fase limpio, y `LC`/lock empezaron a
funcionar.

**Por qué no se pueden intercambiar 1:1:** resuelven problemas opuestos. El
nodo de Lars debe *seguir* un pulso de 1 µs (τ pequeño); el nuestro debe
*promediar* una forma de onda de conmutación (τ grande). Los mismos
componentes, la misma posición en el esquema, requisito opuesto — la trampa
clásica de portar un valor cuyo significado vivía por completo en la
temporización del circuito original.

### ¿El RC grande costó resolución de lock? No.

Una duda legítima, ya que un filtro lento suele significar pérdida de ancho de
banda. Separando las tres cosas que se confunden:

- **Cuantización:** nuestra ventana de 333 ns en un ADC de 12 bits sobre los
  3,3 V completos es ≈81 ps/LSB, frente a los 1000 ns de Lars en 10 bits y
  rango de 1,1 V ≈977 ps/LSB — unas **12× más fino**, porque una ventana de
  tiempo más estrecha se mapea sobre más códigos ADC.
- **Ruido de medida:** el sobremuestreo 16× con mediana rechaza glitches y
  reduce el jitter, algo que la lectura de rampa única de Lars no tiene.
- **Ancho de banda:** el único coste real del RC grande. τ≈51 ms añade ~51 ms
  de retardo de grupo — pero LOCK actualiza cada pocos segundos (ancho de banda
  del lazo muy por debajo de 0,2 Hz), así que f_c≈3,1 Hz está órdenes de
  magnitud por encima del lazo y el retardo es ~1 % de un ciclo. Nunca limita
  DPLL ni LOCK.

Así que el cambio a 51 kΩ/1 µF **mejoró** la medida (cuantización más fina,
menos ruido) y el coste de ancho de banda es despreciable en la escala temporal
del lazo. El diente de sierra residual de ±21 ns en lock está dominado por el
propio qErr del receptor LEA-6T (ver `SAW`), no por el filtro TIC.

---

## EEPROM

| Comando | Descripción |
|---------|-------------|
| `ES` | Guardar todos los parámetros en EEPROM |
| `ER` | Recuperar parámetros desde EEPROM |
| `EE` | Borrar EEPROM (restaurar valores por defecto) |

---

## EEPROM

La EEPROM (emulada en la Flash del STM32) almacena 144 bytes:

| Dirección | Tamaño | Contenido |
|-----------|--------|-----------|
| 0–5 | 6 B | Firma `"GPSD2"` |
| 6–7 | 2 B | Valor del DAC PWM (big-endian) |
| 8 | 1 B | Número de algoritmo (0–9) |
| 9 | 1 B | Desfase horario (±23 h) |
| 10–121 | 112 B | PID: g_pid[3..9] × {Kp, Ki, Kd, I_LIMIT} |
| 122–133 | 12 B | g_blend_crossover, g_blend_scale, g_nn_max_step |
| 134–137 | 4 B | g_pressure_offset (comando PO) |
| 138–141 | 4 B | g_altitude_offset (comando AO) |
| 142 | 1 B | modo de zona horaria (0 = manual, 1 = auto `TO A`) |
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

## Entrada de fase LTIC (Lars' TIC) — vista previa

Con `GPSDO_LTIC` activado, el firmware lee un contador de intervalo de tiempo
por hardware (el TIC de Lars Walenius): un condensador de 1 nF se carga con una
corriente constante durante el intervalo GPS-1PPS → OCXO-1PPS, y la tensión
retenida en PA1 se muestrea (media móvil) y se descarga en cada PPS. La tensión
es una medida directa y de alta resolución de la diferencia de fase entre ambos
pulsos — mucho más fina que el contador de ciclos TIM2 que usa el lazo hoy.

Actualmente es **solo vista previa / telemetría**: el valor aparece en el
informe serie (`Vphase:`), como una fila `Vph:` en el TFT y como una línea
`LTIC phase (PA1)` en la lista de verificación de arranque. El lazo de control
**todavía no** disciplina el OCXO a partir de ello. La constante
`LTIC_NS_PER_VOLT` en `gpsdo_config.h` convierte voltios a nanosegundos una vez
calibrada la rampa del TIC por placa (por defecto 0 = sin calibrar, así que
solo se muestran voltios). Disciplinar el lazo directamente desde el TIC está
previsto como un algoritmo aparte; registrar Vphase primero permite
caracterizar el rango, el ruido y la linealidad del TIC en tu hardware antes de
que controle el lazo.

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
