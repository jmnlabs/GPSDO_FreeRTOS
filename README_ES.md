# GPSDO FreeRTOS v0.51

[English](README.md) | [Polski](README_PL.md) | **Español**

Firmware en tiempo real (FreeRTOS) para un oscilador disciplinado por GPS
(GPSDO) sobre la plataforma STM32 BlackPill (WeAct F411CE / F401CCU6).

## Créditos

| Rol | Persona / fuente |
|-----|------------------|
| Autor del port a FreeRTOS, algoritmos 3–9 | **J. M. Niewiński** — [repositorio](https://github.com/jmnlabs/GPSDO_FreeRTOS) |
| Asistente de programación (Anthropic) | **Claude AI** |
| Autor del firmware original v0.06c | **André Balsa** — [repositorio](https://github.com/AndrewBCN/STM32-GPSDO/tree/main/software/GPSDO_V006c) |
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
  20   280  219        │ T6963C *   │  240x128 mono (vía puente SPI,
                       └────────────┘            experimental)
                       * mutuamente excluyente con los TFT en color
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
- **T6963C 240×128** LCD mono mediante puente externo SPI→T6963C (PG240128);
  comparte los pines SPI1 del TFT, mutuamente excluyente con el TFT.
  ⚠️ **Experimental / sin probar** — el backend está completo pero el enlace
  solo se ha probado en banco con cables largos y problemas de integridad de
  señal (oscilaciones, flancos espurios de CS). Necesita validación en
  hardware con cableado corto y limpio antes de usarse; déjelo desactivado
  por ahora.
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

Diez algoritmos seleccionables mediante el comando `LA n`:

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
| 9 | Red neuronal | e/∫e/de | 10 s | Experimental — perceptrón de una capa |

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
│ GPSDO v0.51-rtos        LMT 14:32:45 Thu   │ ← barra de cabecera (azul marino)
├────────────────────────────────────────────┤
│                                            │
│        10000000.0000 Hz                    │ ← frecuencia (grande, por color)
│                                            │
├────────────────────────────────────────────┤
│ UTC:12:32:45 Thu     │ Sat: 9 HDOP:0.90    │
│ 11/06/2026           │ Lat: 52.123456      │
│ Up 000d 02:15:33     │ Lon: 23.123456      │
│ Algo:5  hit          │ Alt:  175m          │
│ PWM:44653 V:1.97     │ IN:12.05V  250mA    │
├────────────────────────────────────────────┤
│ BMP:23.4C 1013.2hPa  │ AHT:22.1C 45.3%rH   │
├────────────────────────────────────────────┤
│          DISCIPLINED  FIX OK               │ ← barra de estado (por color)
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
| `T` | Modo túnel GPS — paso directo a la UART del GPS (sale tras 300 s) |
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
| `LA [0-9]` | Seleccionar / mostrar algoritmo de control |
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

### EEPROM

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
