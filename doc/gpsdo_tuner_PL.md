# GPSDO Tuner

📖 [Strona projektu](https://github.com/jmnlabs/GPSDO_FreeRTOS) · [Instrukcja](README_PL.md) · Języki: [EN](gpsdo_tuner_EN.md) · **PL** · [ES](gpsdo_tuner_ES.md)

GUI do strojenia na żywo i wizualizacji fazy dla firmware GPSDO_FreeRTOS.

Każdy OCXO ma inne wzmocnienie EFC, każdy detektor fazy inne Vsat i poziom szumu,
każdy wzorzec GPS/Rb własną charakterystykę. Zamiast gonić jeden zestaw
domyślnych wartości kompilacyjnych, który i tak nie zadowoli każdej płyty, to
narzędzie umieszcza komendy strojenia firmware za bezpośrednimi kontrolkami — tak
by każdy konstruktor dostroił pętlę do własnego sprzętu: odczytując każdy
parametr z urządzenia, zapisując go na żywo i zatwierdzając przez `ES` lub cofając
przez `ER`.

## Co robi

- **Wykresy na żywo** — faza (`dph`), napięcie detektora (`Vphase`, z prowadnicami
  kotwicy i pasma 15–85 % Vsat rysowanymi z kalibracji) oraz błąd częstotliwości.
- **Panel LTIC** — trzystopniowy PID ACQ / DPLL / LOCK, odczytywany z `LL` i
  zapisywany na żywo przez komendy `AQ*` / `DP*` / `LK*`.
- **Panel FA** — okna tłumienia DPLL i LOCK (`FAD` / `FAL`), rozdzielenie
  akwizycji i stanu ustalonego do tropienia cyklu granicznego.
- **Panel PID** — `KP/KI/KD/IL` dla klasycznych algorytmów 3–9.
- **Panel kalibracji** — `LNV/LZO/LRN/LCV/LAT/LIV` oraz `LPOL`.
- **Surowy monitor + linia komend** — dowolna komenda firmware plus szybkie
  przyciski.

Kontrolki są celowo bezpośrednie: to narzędzie warsztatowe dla osób, które znają
swój sprzęt. Zmiana PID na żywo może zaburzyć działający lock — najpierw odczytaj
bieżące wartości (panele robią to przy połączeniu), zmieniaj po jednej rzeczy i
trzymaj `ER` pod ręką, by przeładować ostatnio zapisany zestaw z EEPROM.

## Instalacja i uruchomienie

```
pip install pyserial pyqtgraph PySide6
python gpsdo_tuner.py
```

Wybierz port szeregowy, kliknij Connect, a panele wypełnią się z urządzenia.

## Podziękowania

Zainspirowane skryptem `GPSDO_log.py` autorstwa **lucido** — loggerem na żywo
Vphase/Vctl/dPh/qErr z PyQtGraph i linią TX portu szeregowego. To narzędzie wyrosło
z tamtego pomysłu i zachowuje jego ogólny kształt (wątek serial, konfigurowalne
wykresy na żywo, linia komend i surowy monitor); panele strojenia, odczyt
parametrów i wizualizator rampy fazy są tu nowe. Firmware: J. M. Niewiński
(jmnlabs), na bazie v0.06c André Balsy.
