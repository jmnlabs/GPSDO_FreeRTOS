# Bufor pierścieniowy Flash — procedura uruchomienia na sprzęcie (v0.90)

Bufor pierścieniowy z równoważeniem zużycia przechowuje dane „żywe” (nauczony
dryf/tłumienie, kalibracja LC, ostatni PWM) w **sektorze 6** Flasha
(0x08040000, 128 KB), osobno od firmware i EEPROM ustawień (sektor 7).
Przełączany **w czasie pracy** komendą `FR 0|1` (zapis przez `ES`, domyślnie
włączony) — bez flagi kompilacji, więc bez zabawy z cache buildu.

Rdzeń logiki jest przetestowany (28 asercji na PC). Za pierwszym razem
uruchamiaj świadomie, z kopią zapasową.

## 0. Najpierw kopia zapasowa (zalecane)

J-Linkiem (lub ST-Linkiem) zrzuć cały Flash, by móc przywrócić w razie potrzeby:

```
JLinkExe -device STM32F411CE -if SWD -speed 4000
> savebin backup_full.bin 0x08000000 0x80000
> exit
```

Przywrócenie później: `loadbin backup_full.bin 0x08000000`.

## 1. Potwierdź, że firmware mieści się poniżej sektora 6

Sprawdź linię rozmiaru: `Sketch uses NNNNNN bytes ...`

`NNNNNN` musi pozostać **poniżej 262144** (0x08040000 − 0x08000000). W v0.90 to
~170 KB, ~89 KB zapasu. Jeśli przyszły build zbliży się do 256 KB, przesuń
pierścień lub odchudź firmware **przed** wgraniem.

## 2. Włącz pierścień, obserwuj `EW`

Pierścień domyślnie **włączony**. Dla pewności, przez port szeregowy:

```
FR 1
ES
```

Następnie `EW`. Na dziewiczym sektorze spodziewaj się:

```
Flash ring: erase cycles=1  slots used=0/4095  (sector 6, 0x08040000)
```

`erase cycles=1` jest normalne: pierwszy `begin` nie znajduje ważnego
nagłówka, kasuje raz i zakłada świeży. `slots used=0`, bo nic jeszcze nie
auto-zapisane.

## 3. Wymuś zapis, potwierdź trwałość

Pozwól, by LRN nazbierał dryf ponad próg histerezy (> 8 LSB), albo puść udaną
kalibrację `LC` (zapisuje od razu). Wtedy `EW` powinno pokazać `slots used=1`.
Zresetuj zasilanie. Przy starcie powinieneś zobaczyć:

```
Flash ring: live data recalled
Live store: LRN + LC applied from flash ring
```

Ponownie `EW` — nadal `slots used=1`, a wartości nauczone/kalibracji są
odtworzone. To dowód, że zapis przeżył restart — o to właśnie chodzi.

## 4. Bezpieczeństwo śmieci/kasowania (opcjonalne, dokładne)

Skasuj J-Linkiem tylko sektor 6 i zresetuj:

```
> erase 0x08040000 0x0805FFFF
```

Przy starcie firmware musi wykryć pusty sektor, re-inicjować pierścień
(licznik kasowań rośnie) i wystartować od domyślnych — bez zawieszenia, bez
śmieci. To weryfikuje ścieżkę odporności.

## 5. Wyłączanie

`FR 0` + `ES` zatrzymuje całą aktywność pierścienia: żadnych odczytów, zapisów
ani kasowań. Wartości nauczone/kalibracji żyją wtedy tylko w RAM i giną po
restarcie. Włącz ponownie w dowolnej chwili `FR 1` + `ES`.

## 6. Wgrywanie nowego firmware później (ważne)

- **Bootloader / DFU / Arduino IDE** dotyka tylko sektorów firmware (0–5);
  pierścień (6) i EEPROM ustawień (7) przetrwają.
- **Pełne kasowanie układu J-Link/ST-Link** czyści wszystko. By zachować
  kalibrację i uczenie, kasuj tylko sektory 0–5:
  ```
  > erase 0x08000000 0x0803FFFF
  > loadbin firmware.bin 0x08000000
  ```
- Jeśli pierścień zostanie wyczyszczony, firmware uczy się/kalibruje od nowa —
  nic się nie psuje, tracisz tylko nagromadzone dostrojenie.

## Wycofanie

Jeśli coś się psuje, `FR 0` + `ES` natychmiast wyłącza pierścień (bez ponownego
wgrywania). Przywróć `backup_full.bin`, jeśli zawartość Flasha została naruszona.
