
***

### 🇪🇸 Spanish Version

```markdown
# Generador de Tabla de Zonas Horarias (`tz_table.h`)

El script `gen_tz_table.py` genera un archivo de cabecera C (`tz_table.h`) con la base de datos de zonas horarias IANA. Está diseñado para sistemas embebidos (como proyectos GPSDO con FreeRTOS) donde solo se necesita la regla POSIX actual (desfase horario y reglas de horario de verano), y no la base de datos histórica completa.

## Requisitos

* **Python 3.x**
* **Base de datos de zonas horarias IANA (archivos TZif)**:
  * **Linux / macOS**: La base de datos suele estar instalada por defecto en `/usr/share/zoneinfo`.
  * **Windows**: Requiere instalar el paquete `tzdata` a través de `pip` (Windows carece de un equivalente nativo a este directorio).

## Cómo Usar

El script detecta automáticamente el sistema operativo y selecciona la ruta correcta a la base de datos de zonas horarias.

### Linux / macOS
Simplemente ejecuta el script y redirige la salida estándar al archivo de cabecera:
```bash
python3 gen_tz_table.py > tz_table.h