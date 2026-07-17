# Timezone Table Generator (`tz_table.h`)

The `gen_tz_table.py` script generates a C header file (`tz_table.h`) containing the IANA timezone database. It is designed for embedded systems (like GPSDO projects with FreeRTOS) where only the current POSIX rule (time offset and DST rules) is needed, rather than the full historical database.

## Requirements

* **Python 3.x**
* **IANA Timezone Database (TZif files)**:
  * **Linux / macOS**: The database is usually installed by default in `/usr/share/zoneinfo`.
  * **Windows**: Requires installing the `tzdata` package via `pip` (Windows lacks a native equivalent to this directory).

## How to Use

The script automatically detects the operating system and selects the correct path to the timezone database.

### Linux / macOS
Simply run the script and redirect the standard output to the header file: