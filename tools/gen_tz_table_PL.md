# Generator tabeli stref czasowych (tz_table.h)

Skrypt `gen_tz_table.py` służy do generowania pliku nagłówkowego `tz_table.h` z bazą stref czasowych IANA. Został zaprojektowany z myślą o systemach wbudowanych (np. projekty GPSDO z FreeRTOS), gdzie potrzebna jest tylko aktualna reguła POSIX (przesunięcie czasu i zasady czasu letniego), a nie cała historyczna baza danych.

## Wymagania

* **Python 3.x**
* **Baza stref czasowych IANA (pliki TZif)**:
  * **Linux / macOS**: Baza jest zazwyczaj zainstalowana domyślnie w `/usr/share/zoneinfo`.
  * **Windows**: Wymaga doinstalowania paczki `tzdata` przez `pip` (Windows nie posiada natywnego odpowiednika tego katalogu).

## Jak używać

Skrypt automatycznie wykrywa system operacyjny i dobiera odpowiednią ścieżkę do bazy stref czasowych.

### Linux / macOS
Wystarczy uruchomić skrypt i przekierować standardowe wyjście do pliku nagłówkowego:
```bash
python3 gen_tz_table.py > tz_table.h