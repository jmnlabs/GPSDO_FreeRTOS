# Timezone Table Generator (`tz_table.h`)

The `gen_tz_table.py` script generates a C header file (`tz_table.h`) containing the IANA timezone database. It is designed for embedded systems (like GPSDO projects with FreeRTOS) where only the current POSIX rule (time offset and DST rules) is needed, rather than the full historical database.

## Requirements

* **Python 3.x**
* **IANA Timezone Database (TZif files)**:
  * **Linux / macOS**: The database is usually installed by default in `/usr/share/zoneinfo`.
  * **Windows**: Requires installing the `tzdata` package via `pip` (Windows lacks a native equivalent to this directory).

## How to Use

The script automatically detects the operating system and selects the correct path to the timezone database. Detection order is: `/usr/share/zoneinfo`, then the `pip`-installed `tzdata` package, then the `TZDATA_PATH` environment variable if set.

### Linux / macOS

Simply run the script and redirect standard output to the header file:

```bash
python3 gen_tz_table.py > tz_table.h
```

### Windows

First install the timezone database, then run the script:

```bash
pip install tzdata
python gen_tz_table.py > tz_table.h
```

### Custom path

If the database lives somewhere non-standard, point the script at it with an environment variable:

```bash
TZDATA_PATH=/path/to/zoneinfo python3 gen_tz_table.py > tz_table.h
```

## Output

The header is written to standard output; a short summary (zone, rule and region counts, plus the approximate flash cost) is written to standard error, so it does not end up in the header itself. A typical run reports around 440 zones, 88 rules and 10 regions, for roughly 7 KB of flash.

The generated `tz_table.h` contains:

* `tz_rule_str[]` — the POSIX TZ rule strings, deduplicated across zones.
* `tz_region_str[]` — the region names (`Europe`, `America`, …).
* `tz_city_blob[]` — the city names, NUL-separated, **sorted case-insensitively**.
* `tz_city_off[]`, `tz_zone_region[]`, `tz_zone_rule[]` — the per-zone index arrays.

## Important notes

* **The sort order is load-bearing.** The firmware's `tz_lookup()` finds a city by binary search over `tz_city_blob`, so the blob must stay sorted case-insensitively and contain no duplicate city names. The script enforces both and aborts if either is violated — do not hand-edit the generated file.
* **The walk order is deterministic.** Directories and files are sorted before processing, so the table is identical on Linux, macOS and Windows. This matters for the few cities that exist under two regions (for example `Asia/Istanbul` and `Europe/Istanbul`): the sorted walk always keeps the same one, rather than whichever the filesystem happened to return first.
* **Regenerate when tzdata changes.** The IANA database is updated several times a year. Re-run the script against a current `tzdata` to pick up new rules.
