# Generator tabeli stref czasowych (`tz_table.h`)

Skrypt `gen_tz_table.py` służy do generowania pliku nagłówkowego `tz_table.h` z bazą stref czasowych IANA. Został zaprojektowany z myślą o systemach wbudowanych (np. projekty GPSDO z FreeRTOS), gdzie potrzebna jest tylko aktualna reguła POSIX (przesunięcie czasu i zasady czasu letniego), a nie cała historyczna baza danych.

## Wymagania

* **Python 3.x**
* **Baza stref czasowych IANA (pliki TZif)**:
  * **Linux / macOS**: Baza jest zazwyczaj zainstalowana domyślnie w `/usr/share/zoneinfo`.
  * **Windows**: Wymaga doinstalowania paczki `tzdata` przez `pip` (Windows nie posiada natywnego odpowiednika tego katalogu).

## Jak używać

Skrypt automatycznie wykrywa system operacyjny i dobiera odpowiednią ścieżkę do bazy stref czasowych. Kolejność wykrywania: `/usr/share/zoneinfo`, następnie paczka `tzdata` zainstalowana przez `pip`, a na końcu zmienna środowiskowa `TZDATA_PATH`, jeśli jest ustawiona.

### Linux / macOS

Wystarczy uruchomić skrypt i przekierować standardowe wyjście do pliku nagłówkowego:

```bash
python3 gen_tz_table.py > tz_table.h
```

### Windows

Najpierw zainstaluj bazę stref, potem uruchom skrypt:

```bash
pip install tzdata
python gen_tz_table.py > tz_table.h
```

### Niestandardowa ścieżka

Jeśli baza znajduje się w nietypowym miejscu, wskaż ją zmienną środowiskową:

```bash
TZDATA_PATH=/sciezka/do/zoneinfo python3 gen_tz_table.py > tz_table.h
```

## Wynik

Nagłówek trafia na standardowe wyjście, a krótkie podsumowanie (liczba stref, reguł i regionów oraz przybliżony koszt we flashu) — na standardowe wyjście błędów, żeby nie znalazło się w samym pliku nagłówkowym. Typowy przebieg raportuje około 440 stref, 88 reguł i 10 regionów, czyli mniej więcej 7 KB flasha.

Wygenerowany `tz_table.h` zawiera:

* `tz_rule_str[]` — łańcuchy reguł POSIX TZ, zdeduplikowane między strefami.
* `tz_region_str[]` — nazwy regionów (`Europe`, `America`, …).
* `tz_city_blob[]` — nazwy miast, rozdzielone znakiem NUL, **posortowane bez uwzględniania wielkości liter**.
* `tz_city_off[]`, `tz_zone_region[]`, `tz_zone_rule[]` — tablice indeksów dla poszczególnych stref.

## Ważne uwagi

* **Kolejność sortowania jest krytyczna.** Firmware'owa funkcja `tz_lookup()` znajduje miasto przez wyszukiwanie binarne po `tz_city_blob`, więc blob musi pozostać posortowany bez względu na wielkość liter i nie może zawierać powtórzonych nazw miast. Skrypt pilnuje obu warunków i przerywa pracę, jeśli któryś zostanie naruszony — nie edytuj wygenerowanego pliku ręcznie.
* **Kolejność przechodzenia katalogów jest deterministyczna.** Katalogi i pliki są sortowane przed przetworzeniem, więc tabela jest identyczna na Linuksie, macOS i Windowsie. Ma to znaczenie dla nielicznych miast występujących w dwóch regionach (na przykład `Asia/Istanbul` i `Europe/Istanbul`): posortowane przejście zawsze zachowuje to samo, zamiast tego, które akurat zwrócił system plików.
* **Regeneruj po zmianie tzdata.** Baza IANA jest aktualizowana kilka razy w roku. Uruchom skrypt ponownie na bieżącej paczce `tzdata`, aby uwzględnić nowe reguły.
