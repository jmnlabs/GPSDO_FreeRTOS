# Generador de Tabla de Zonas Horarias (`tz_table.h`)

El script `gen_tz_table.py` genera un archivo de cabecera C (`tz_table.h`) con la base de datos de zonas horarias IANA. Está diseñado para sistemas embebidos (como proyectos GPSDO con FreeRTOS) donde solo se necesita la regla POSIX actual (desfase horario y reglas de horario de verano), y no la base de datos histórica completa.

## Requisitos

* **Python 3.x**
* **Base de datos de zonas horarias IANA (archivos TZif)**:
  * **Linux / macOS**: La base de datos suele estar instalada por defecto en `/usr/share/zoneinfo`.
  * **Windows**: Requiere instalar el paquete `tzdata` a través de `pip` (Windows carece de un equivalente nativo a este directorio).

## Cómo Usar

El script detecta automáticamente el sistema operativo y selecciona la ruta correcta a la base de datos de zonas horarias. El orden de detección es: `/usr/share/zoneinfo`, luego el paquete `tzdata` instalado por `pip`, y por último la variable de entorno `TZDATA_PATH` si está definida.

### Linux / macOS

Simplemente ejecuta el script y redirige la salida estándar al archivo de cabecera:

```bash
python3 gen_tz_table.py > tz_table.h
```

### Windows

Primero instala la base de datos de zonas horarias, luego ejecuta el script:

```bash
pip install tzdata
python gen_tz_table.py > tz_table.h
```

### Ruta personalizada

Si la base de datos está en una ubicación no estándar, indícasela al script con una variable de entorno:

```bash
TZDATA_PATH=/ruta/a/zoneinfo python3 gen_tz_table.py > tz_table.h
```

## Salida

La cabecera se escribe en la salida estándar; un breve resumen (número de zonas, reglas y regiones, más el coste aproximado en flash) se escribe en la salida de error, de modo que no acaba dentro de la propia cabecera. Una ejecución típica informa de unas 440 zonas, 88 reglas y 10 regiones, es decir, alrededor de 7 KB de flash.

El `tz_table.h` generado contiene:

* `tz_rule_str[]` — las cadenas de reglas POSIX TZ, deduplicadas entre zonas.
* `tz_region_str[]` — los nombres de región (`Europe`, `America`, …).
* `tz_city_blob[]` — los nombres de ciudad, separados por NUL, **ordenados sin distinguir mayúsculas**.
* `tz_city_off[]`, `tz_zone_region[]`, `tz_zone_rule[]` — los arrays de índices por zona.

## Notas importantes

* **El orden de clasificación es esencial.** La función `tz_lookup()` del firmware busca una ciudad mediante búsqueda binaria sobre `tz_city_blob`, por lo que el blob debe permanecer ordenado sin distinguir mayúsculas y no contener nombres de ciudad duplicados. El script comprueba ambas condiciones y aborta si alguna se incumple — no edites el archivo generado a mano.
* **El orden de recorrido es determinista.** Los directorios y archivos se ordenan antes de procesarse, así que la tabla es idéntica en Linux, macOS y Windows. Esto importa para las pocas ciudades que existen bajo dos regiones (por ejemplo `Asia/Istanbul` y `Europe/Istanbul`): el recorrido ordenado conserva siempre la misma, en vez de la que el sistema de archivos devolviera primero.
* **Regenera cuando cambie tzdata.** La base IANA se actualiza varias veces al año. Vuelve a ejecutar el script con un `tzdata` actual para incorporar las nuevas reglas.
