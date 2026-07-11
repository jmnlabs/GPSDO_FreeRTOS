# GPSDO FreeRTOS v0.91

[English](README.md) | **Polski** | [Español](README_ES.md)

Firmware czasu rzeczywistego (FreeRTOS) dla oscylatora sterowanego GPS (GPSDO)
na platformie STM32 BlackPill (WeAct F411CE / F401CCU6).

📋 Historia wersji: [Lista zmian](CHANGELOG_PL.md)

## Autorzy i podziękowania

| Rola | Osoba / źródło |
|------|----------------|
| Autor portu FreeRTOS i algorytmów 3–9 | **J. M. Niewiński** — [repozytorium](https://github.com/jmnlabs/GPSDO_FreeRTOS) |
| Asystent programowania (Anthropic) | **Claude AI** |
| Autor v0.06c — inspiracja portu RTOS | **André Balsa** — [repozytorium](https://github.com/AndrewBCN/STM32-GPSDO) |
| Projekt PCB (prototyp) | **Scrachi** (forum EEVBlog) — [post z plikami](https://www.eevblog.com/forum/projects/yet-another-diy-gpsdo-yes-another-one/825/) · [profil](https://www.eevblog.com/forum/profile/?u=762266) |
| Wątek projektowy | [Yet another DIY GPSDO](https://www.eevblog.com/forum/projects/yet-another-diy-gpsdo-yes-another-one/) — EEVBlog Forum |

Firmware został napisany od podstaw jako port oryginalnego kodu André Balsy
na architekturę FreeRTOS, z pełnym przeprojektowaniem zadań, synchronizacji
i wyświetlania. Konstrukcja sprzętowa bazuje na schemacie z projektu v0.06c
z wykorzystaniem PCB udostępnionych przez użytkownika Scrachi na forum EEVBlog.

---

## Opis projektu

GPSDO (GPS-Disciplined Oscillator) to precyzyjne źródło częstotliwości 10 MHz,
w którym wolnobieżny oscylator kwarcowy (OCXO) jest dyscyplinowany sygnałem
1PPS z odbiornika GPS. Dzięki temu osiągana jest dokładność rzędu 10⁻¹⁰–10⁻¹²
w długim okresie, przy zachowaniu krótkookresowej stabilności OCXO.

### Zasada działania sprzętu

```
                                            10 MHz
               ┌─────────────┐       ┌──────────────┐
   Antena GPS ─┤  u-blox     │       │    OCXO      ├── TIM2 ETR (PA15) ──┐
               │  NEO-6M/7M  │       │  10 MHz      │                     │
               └──┬──────┬───┘       └──────▲───────┘                     │
                  │      │                  │                             │
        NMEA      │  1PPS (PB10)      PWM (PB9)                           │
     (Serial1)    │      │            + filtr RC                          │
                  │      │                  │                             │
               ┌──▼──────▼──────────────────┴───────┐                     │
               │           STM32 F411CE             │◄────────────────────┘
               │           BlackPill                │
               └───┬─────────┬─────────┬───────┬────┘
                   │         │         │       │
                I2C bus    SPI1     Serial2  GPIO
                   │         │         │       │
        ┌──────┬───┼───┬─────┤         │    TM1637
        │      │   │   │     │         │    (zegar,
      OLED   LCD  HT16K33  TFT        BT     PA8/PB4)
     128x64  20x4 (zegar)  │         HC-06
        │              ┌───┴────────┐
     Czujniki:         │ ILI9341 /  │  320x240
   ┌────┼────┐         │ ST7789     │
  AHT  BMP  INA        │ ILI9488    │  480x320 (nietestowany)
                       └────────────┘            eksperymentalny)
                       * wzajemnie wykluczający się z kolorowymi TFT
```

**Pętla regulacji** działa w następujący sposób:

1. OCXO generuje sygnał 10 MHz podawany na wejście TIM2 ETR (PA15).
   Licznik TIM2 (32-bitowy) zlicza takty OCXO w sposób ciągły.
2. Sygnał 1PPS z GPS wyzwala przerwanie capture na TIM3 (PB10).
   ISR odczytuje bieżącą wartość TIM2 — różnica dwóch kolejnych capture
   daje liczbę taktów OCXO w dokładnie jednej sekundzie GPS.
3. Pomiar jest uśredniany w oknie 10 s, 100 s, 1000 s i 10000 s za pomocą
   pierścieniowego bufora kołowego (20000 próbek).
4. Algorytm sterowania (PID, krokowy lub hybrydowy) oblicza korektę PWM.
5. 16-bitowy PWM DAC (PB9) steruje napięciem Vctl podawanym na wejście
   EFC oscylatora przez podwójny filtr RC (20 kΩ / 10 µF, τ ≈ 200 ms).

**Czujniki** (opcjonalne, I2C):

- **AHT10/20** — temperatura i wilgotność wewnątrz obudowy
- **BMP280** — temperatura i ciśnienie atmosferyczne
- **INA219** — napięcie zasilania i prąd pobierany przez OCXO

**Wyświetlacze** (opcjonalne):

- **OLED 128×64** I2C (SH1106 / SSD1306 / SSD1309)
- **LCD 20×4** I2C (HD44780 + PCF8574T)
- **TM1637** (4- lub 6-cyfrowy wyświetlacz zegarowy)
- **TFT 320×240** SPI (ILI9341 / ST7789, biblioteka TFT_eSPI)
- **TFT 480×320** SPI (ILI9488, biblioteka TFT_eSPI) — *nietestowany, brak
  panelu do testów; układ 320×240 jest automatycznie skalowany*
- **HT16K33** 4-cyfrowy zegar 7-seg z dwukropkiem, I2C 0x70 (HH:MM)

OLED i LCD mogą działać jednocześnie (różne adresy I2C).
LCD i TM1637 **nie mogą** działać jednocześnie (konflikt magistrali).

---

## Architektura oprogramowania

Firmware działa pod kontrolą FreeRTOS z siedmioma zadaniami o ściśle
określonych priorytetach:

| Priorytet | Zadanie | Stos | Rola |
|-----------|---------|------|------|
| Najwyższy | `vFreqRelayTask` | 768 B | Przetwarzanie PPS, bufor kołowy częstotliwości |
| Wysoki | `vControlTask` | 1,5 KB | Warmup OCXO, kalibracja, algorytm PID, ADC |
| Średni-wysoki | `vGpsTask` | 1,5 KB | Parsowanie NMEA (TinyGPS++), konfiguracja UBX |
| Średni | `vCliTask` | 1 KB | Parser komend szeregowych / Bluetooth |
| Średni-niski | `vSensorTask` | 1,5 KB | Odczyt AHT/BMP/INA co 2 s |
| Niski | `vDisplayTask` | 4 KB | OLED, LCD, TM1637, raport serial, LED |
| Najniższy | `vUptimeTask` | 768 B | Licznik czasu pracy (dd hh:mm:ss) |

**Współdzielony stan** chroniony jest muteksami FreeRTOS:

- `xFreqMutex` — dane częstotliwości (`gFreq`, `gFreqSnap`)
- `xGpsMutex` — dane GPS (`gGps`)
- `xCtrlMutex` — dane sterowania (`gCtrl`: PWM, algorytm, holdover, trend)
- `xUptimeMutex` — czas pracy (`gUptime`)
- `xWireMutex` — magistrala I2C (współdzielona przez czujniki i wyświetlacze)
- `xSerialMutex` — port szeregowy / Bluetooth

---

## Algorytmy sterowania

Firmware oferuje jedenaście algorytmów przełączanych komendą `LA n` (0–10):

| Algo | Typ | Wejście | Okres | Opis |
|------|-----|---------|-------|------|
| 0 | Krokowy | avg100/1k | ~429 s | Domyślny — prosty, odporny |
| 1 | Drift | — | 1000 s | Tylko pomiar dryfu OCXO |
| 2 | Losowy | — | 5 s | Pomiar szumu — diagnostyczny |
| 3 | FLL PID | avg100 | 100 s | Ogólnego przeznaczenia, konserwatywny |
| 4 | PLL PI+D | prawdziwa faza | 10 s | Niski szum; Kd = tłumienie częstotl. (wymagane) |
| 5 | PLL PID | prawdziwa faza | 10 s | Zbalansowany: szybkość + szum |
| 6 | FLL PID (GA) | avg100 | 100 s | Współczynniki zoptymalizowane genetycznie |
| 7 | PLL PID (GA) | prawdziwa faza | 10 s | Współczynniki zoptymalizowane genetycznie |
| 8 | Hybrid | FLL+PLL | 100 s | Automatyczne przejście FLL↔PLL sigmoidą |
| 9 | Sieć neuronowa | e/∫e/de + temp | 10 s | MLP 5-wejść; uczy się tempco oscylatora, termicznie kompensowany holdover |
| 10 | LTIC | faza TIC + częst. | etapowy | Trzy etapy ACQ→DPLL→LOCK; sprzętowy detektor fazy, samokalibrujący |

Algorytmy PLL (4, 5, 7 i gałąź PLL algo 8) używają architektury
**dwuczasowej**, strojonej pod „szybkie złapanie, łagodne pilnowanie fazy":

- człon dominujący działa na **błąd częstotliwości** (Kp ≈ 0,4/K), szybko
  i bez przeregulowania dociągając częstotliwość do celu;
- małe człony fazowe (Kd proporcjonalny, Ki całkujący na zakumulowanej
  fazie) usuwają powolny dryf drobnymi krokami.

Każda korekta przechodzi przez wspólny stopień wyjściowy z **ograniczeniem
szybkości narastania** (maks. ~12 LSB/krok dla PLL, 40 dla hybrydy) i
**strefą martwą** blisko locka. Slew-limit rozkłada duży nocny dryf fazy na
kilka okresów zamiast jednego wielkiego skoku PWM zaburzającego OCXO; strefa
martwa pozwala PWM stać nieruchomo w stanie ustalonym, by OCXO pracował na
własnej, doskonałej stabilności krótkoterminowej.

Algorytmy 3–9 mają parametry PID (`Kp`, `Ki`, `Kd`, `I_LIMIT`) konfigurowalne
w czasie pracy komendami CLI (`KP`, `KI`, `KD`, `IL`) — bez rekompilacji.
Parametry zapisywane są w EEPROM komendą `ES`.

---

## Układ wyświetlacza OLED (128×64 px, 16 znaków × 8 wierszy)

Przez 2 sekundy po starcie rząd 0 wyświetla wersję firmware.
Potem przechodzi na zegar czasu lokalnego. Dwie strony przełączają się
co `OLED_PAGE_SWITCH_SECS` sekund (domyślnie 10 s):

```
── Rząd 0 (wspólny): LMT:14:32:45 Mon  ← czas lokalny + dzień tygodnia
── Rząd 1 (wspólny): F 9999999.9999Hz   ← częstotliwość + Hz na poz. 14-15
──── STRONA A (GPS) ─────────────────────────────────────────
Rząd 2: La  52.12345             ← szerokość geograficzna
Rząd 3: Lo  23.12345             ← długość geograficzna
Rząd 4: Al  175m Sat: 9          ← wysokość + satelity
Rząd 5: Up 000d 00:00:00         ← czas pracy
Rząd 6: 12:34:56  23.4C          ← UTC + temperatura AHT
──── STRONA B (czujniki) ────────────────────────────────────
Rząd 2: BM:23.4C 1013hPa        ← BMP280
Rząd 3: AH:22.1C 45.3%rH        ← AHT20
Rząd 4: IN:12.05V  250mA        ← INA219
Rząd 5: Sat:09 HDOP:0.90        ← jakość GPS
Rząd 6: UTC:14:32:45 Mon        ← czas UTC + dzień
──── Oba ekrany ─────────────────────────────────────────────
Rząd 7: PWM:40908 hit H          ← PWM + trend + holdover (H/A blink)
```

Sygnalizacja holdovera na rzędzie 7: `H` (ręczny) lub `A` (automatyczny, utrata fixa).

---

## Układ wyświetlacza LCD 20×4

Splash wersji przez 2 sekundy, potem:

```
Linia 0: F:  10000000.0000 Hz     ← częstotliwość (20 znaków)
Linia 1: UTC:14:32:45 Up 000d     ← czas UTC + dni uptime
Linia 2: [widok rotacyjny]        ← patrz tabela poniżej
Linia 3: PWM:40908 V:1.65 hit     ← PWM + Vctl + trend/holdover
```

Linia 2 przełącza się co `LCD_LINE2_SWITCH_SECS` sekund:

| Tryb | Zawartość | Przykład |
|------|-----------|---------|
| 0 | Współrzędne GPS | `La:52.123 Lo:23.123 S: 9` |
| 1 | Satelity + HDOP | `Sats: 9  HDOP:0.90` |
| 2 | Data + dzień + czas lokalny | `02/06/2026 Mon 14:32` |
| 3 | AHT20 | `AHT:22.1C  45.3%rH` |
| 4 | INA219 | `INA:12.05V   250mA` |
| 5 | BMP280 | `BMP:23.4C 1013.2hPa` |

Holdover na linii 3: `[H]` (ręczny) lub `[A]` (automatyczny) — blink 500 ms.

---

## Układ wyświetlacza TFT (ILI9341 / ST7789 320×240, ILI9488 480×320, TFT_eSPI)

Obsługiwane są tanie moduły TFT SPI w orientacji poziomej, sterowane przez
sprzętowe SPI1: **ILI9341** i **ST7789** w 320×240 oraz **ILI9488** w 480×320.
Wszystkie trzy używają tego samego okablowania w `User_Setup.h` — zmiana
panelu wymaga tylko zmiany definicji drivera oraz szerokości/wysokości. Linie
`TFT_RGB_ORDER` / `TFT_INVERSION_OFF` są potrzebne dla prawidłowych kolorów na
modułach ST7789, a na pozostałych są nieszkodliwe. Niezależne od wyświetlaczy
I2C — OLED, LCD i TFT mogą działać jednocześnie.

> **ILI9488 jest nietestowany** — brak panelu do testów. Ekran roboczy 320×240
> i splash są automatycznie skalowane do 480×320 podczas kompilacji (szerokość
> ×1.5, wysokość ×1.33, fonty mapowane o rozmiar w górę). Kod się kompiluje, a
> geometria została zweryfikowana, że mieści się w panelu, ale nie był
> uruchomiony na realnym sprzęcie. Traktować jako eksperymentalny do
> potwierdzenia. ILI9488 po SPI jest zauważalnie wolniejszy (480×320, kolor
> 18-bit), więc przerysowania są bardziej widoczne niż na małych panelach.

**Okablowanie (sprzętowe SPI1):**

| Pin TFT | Pin STM32 |
|---------|-----------|
| SCK | PA5 (SPI1 SCLK) |
| SDI | PA7 (SPI1 MOSI) |
| RES | PB15 |
| D/C | PB12 |
| CS | PB13 |

**Układ ekranu:**

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

**Kodowanie kolorami:**

| Element | Kolor | Znaczenie |
|---------|-------|-----------|
| Częstotliwość | zielony | zablokowany — najlepsza średnia w granicach 1e-10 (10000s) lub 1e-9 (1000s) od 10 MHz |
| Częstotliwość | biały | korygowanie |
| Częstotliwość | pomarańczowy | holdover |
| Częstotliwość | czerwony | brak sygnału |
| Pasek statusu | zielony | dyscyplinowany, fix OK |
| Pasek statusu | pomarańczowy | holdover ręczny |
| Pasek statusu | czerwony | auto-holdover (utrata fixa) / oczekiwanie na fix |

Aktualizacje są selektywne — każda komórka wartości pamięta poprzedni
tekst i jest przerysowywana tylko przy zmianie, minimalizując ruch SPI
przy odświeżaniu 1 Hz.

**Konfiguracja biblioteki TFT_eSPI (wymagana):**

TFT_eSPI konfiguruje się w *bibliotece*, nie w szkicu. Edytuj
`Arduino/libraries/TFT_eSPI/User_Setup.h` aby zawierał:

```c
#define ST7789_DRIVER          // lub ILI9341_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 320
#define TFT_MISO PA6      // wymagany na STM32 nawet gdy wyświetlacz nie ma MISO
#define TFT_MOSI PA7
#define TFT_SCLK PA5
#define TFT_CS   PB13
#define TFT_DC   PB12
#define TFT_RST  PB15
#define TFT_RGB_ORDER TFT_BGR   // kolejność kolorów Blue-Green-Red
#define TFT_INVERSION_OFF       // naprawia odwrócone kolory w części modułów ST7789
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define SPI_FREQUENCY 27000000
```

Dla panelu **ILI9488 (480×320)** zmień driver i wymiary oraz dodaj
`LOAD_FONT6` (większy font częstotliwości używany przez skalowany układ):

```c
#define ILI9488_DRIVER
#define TFT_WIDTH  320
#define TFT_HEIGHT 480
// ...te same linie TFT_MISO/MOSI/SCLK/CS/DC/RST/RGB_ORDER co wyżej...
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6              // duży font częstotliwości w skalowanym układzie
#define SPI_FREQUENCY 27000000
```

**Rozwiązywanie problemów:** jeśli po włączeniu TFT firmware zawiesza się na
splashu wersji na OLED, sprawdź wyjście serial. Komunikat `TFT: init start ...`
jest drukowany bezpośrednio przed `TFT_eSPI::init()` — jeśli to ostatnia
linia, zawieszenie jest w bibliotece: zweryfikuj, że `User_Setup.h` zawiera
dokładnie powyższe piny (łącznie z `TFT_MISO PA6`) i właściwy driver. Stos
DisplayTask jest automatycznie zwiększany do 768 słów gdy `GPSDO_TFT` jest
włączone — jeśli modyfikowałeś rozmiary stosów ręcznie, przywróć tę wartość.

Następnie włącz `GPSDO_TFT_ST7789`, `GPSDO_TFT_ILI9341` lub `GPSDO_TFT_ILI9488` w `gpsdo_config.h`.

---

## Sygnalizacja LED

| LED | Pin | Funkcja |
|-----|-----|---------|
| Niebieska (wbudowana) | PC13 | Mruga co 1PPS — sygnał życia |
| Żółta | PB8 | Patrz tabela stanów poniżej |

**LED żółta — maszyna stanów:**

| Stan | Warunek | Sygnalizacja |
|------|---------|-------------|
| Brak fixa GPS | Po starcie, brak sygnału | OFF |
| Fix OK, tryb dyscyplinowany | Normalna praca | ON stale |
| Holdover ręczny (`MH`) | Użytkownik włączył holdover | Wolne pulsowanie 1000 ms |
| Auto-holdover | Fix utracony podczas pracy | Szybkie pulsowanie 200 ms |

---

## Synchronizacja picDIV

Opcjonalny picDIV (rodzina PD11/PD13/PD17 Toma Van Baaka, leapsecond.com)
dzieli 10 MHz z OCXO do czystego wyjścia 1PPS z jitterem <2 ps.
STM32 steruje pinem Arm (PB3); GPS 1PPS jest podłączony sprzętowo
bezpośrednio do pinu Sync.

**Sekwencja uzbrojenia** (komenda `AP`):

1. STM32 ściąga Arm do LOW — wyjście dividera zatrzymuje się
2. Arm trzymany LOW przez 1,0–1,2 s (specyfikacja wymaga >1 s)
3. STM32 zwalnia Arm (HIGH)
4. Divider startuje zsynchronizowany z najbliższym zboczem narastającym GPS 1PPS

Uzbrojenie jest odraczane gdy brak fixa GPS — bez zbocza 1PPS na Sync
divider pozostałby zatrzymany z martwym wyjściem.

**Synchronizacja długoterminowa — ważne:**

Wyjście picDIV jest spójne fazowo z **OCXO**, nie z GPS.
Zachowanie po uzbrojeniu zależy od aktywnego algorytmu:

| Typ algorytmu | Częstotliwość | Faza | Zachowanie picDIV |
|--------------|---------------|------|-------------------|
| FLL (0, 3, 6, 8*) | ograniczona | random walk | 1PPS powoli dryfuje od GPS |
| PLL (4, 5, 7) | ograniczona | ograniczona | 1PPS pozostaje zgrany z GPS |

*Algorytm 8 działa jak FLL przy dużych błędach, PLL blisko locka.

FLL zeruje tylko średni błąd częstotliwości; każda mała resztka całkuje
się do fazy, więc 1PPS picDIV wykonuje random walk względem GPS
(typowo µs/dzień przy średnim błędzie 1e-11). Jeśli długoterminowe
zgranie 1PPS ma znaczenie — używaj algorytmu PLL (`LA 4`, `LA 5` lub
`LA 7`) albo okresowo ponawiaj uzbrojenie (`AP`). Uzbrajaj dopiero gdy
pętla zgłosi lock (trend `hit`) — uzbrojenie w trakcie zbiegania
natychmiast rozpoczyna dryf fazy.

---

## Automatyczny holdover

Gdy GPS traci fix podczas normalnej pracy (np. odłączenie anteny):

1. `vControlTask` wykrywa przejście `pos_valid: true→false`
2. Automatycznie ustawia `holdover_mode=true`, `holdover_auto=true`
3. PWM zostaje zamrożony na ostatniej wartości — OCXO pracuje swobodnie
4. LED żółta pulsuje szybko (200 ms), wyświetlacze pokazują `A` (blink)
5. Po odzyskaniu fixa: automatyczne wyłączenie holdovera, powrót do `ON`

Ręczna komenda `MH` ustawia holdover niezależnie (sygnalizowany jako `H`).
`MD` wyłącza holdover (zarówno ręczny jak i automatyczny).

---

## Komendy CLI (Serial / Bluetooth)

Połączenie: 115200 Bd (USB) lub 57600 Bd (Bluetooth HC-06, `GPSDO_BLUETOOTH`).
Komendy zakończone `\r\n` lub `\n`. Nazwy komend są **niewrażliwe na wielkość
liter** (`LA`, `la` i `La` są równoważne), więc działa dowolna wielkość liter.

### Ogólne

| Komenda | Opis |
|---------|------|
| `H` | Wyświetl pomoc |
| `V` | Wersja, autorzy i linki GitHub |
| `F` | Wyczyść bufory kołowe częstotliwości (restart uśredniania) |
| `C` | Uruchom auto-kalibrację (tylko centrowanie PWM) |
| `CT` | Kalibracja + auto-strojenie: pomiar K, wyliczenie PID dla algo 3-9 |
| `T [baud]` | Tunel GPS na USB dla u-center — czysty dwukierunkowy NMEA/UBX (telemetria na Bluetooth jeśli jest, inaczej wyciszona); opcjonalny baud UART GPS, zachowany po wyjściu; wyłącza się po 300 s |
| `SP <n>` | Ustaw PWM DAC bezpośrednio (1–65535), omija algorytm |
| `RH` | Tryb raportowania: czytelny (domyślny) |
| `RD` | Tryb raportowania: rozdzielany tabulatorem |
| `RP` | Wstrzymaj strumień danych serial/BT |
| `RR` | Wznów strumień danych serial/BT |
| `SW` | Diagnostyka stosów zadań FreeRTOS |

### Sterowanie

| Komenda | Opis |
|---------|------|
| `MH` | Włącz tryb holdover (ręczny) |
| `MD` | Włącz tryb dyscyplinowany |
| `LA [0-10]` | Wybierz / pokaż algorytm sterowania |
| `AP` | Uzbrój picDIV — zatrzymuje wyjście na 1,0–1,2 s, resynchronizuje z GPS 1PPS |

### Dostrajanie algorytmów

| Komenda | Opis |
|---------|------|
| `LP [n]` | Pokaż parametry PID algorytmu `n` (lub bieżącego) |
| `KP n val` | Ustaw Kp dla algorytmu `n` (3–7) |
| `KI n val` | Ustaw Ki dla algorytmu `n` (3–7) |
| `KD n val` | Ustaw Kd dla algorytmu `n` (3–7) |
| `IL n val` | Ustaw I_LIMIT dla algorytmu `n` (3–9) |
| `BC [val]` | Algo 8: próg przejścia FLL↔PLL (Hz) |
| `BS [val]` | Algo 8: szerokość sigmoidy blend (Hz) |
| `NS [val]` | Algo 9: max krok NN (LSB) |

### Konfiguracja

| Komenda | Opis |
|---------|------|
| `TO [n]` | Pokaż / ustaw przesunięcie czasu ręcznie (h, −23..23) |
| `TO A` | Auto-strefa: strefa z pozycji GPS + reguła DST UE |
| `PO [f]` | Pokaż / ustaw offset ciśnienia |
| `AO [f]` | Pokaż / ustaw offset wysokości |
| `SV [0\|1]` | Survey-in / Time Mode na module czasowym (zapis przez `ES`, działa od następnego startu) |

### LTIC — algorytm 10 (trzy etapy ACQ/DPLL/LOCK)

Algorytm 10 dyscyplinuje OCXO ze sprzętowej fazy TIC (PA1), która rozdziela
fazę znacznie dokładniej niż licznik cykli TIM2. To projekt hybrydowy: etapy
zgrubne opierają się na odpornym błędzie **częstotliwości** z TIM2 (bez
niejednoznaczności zawinięcia), etapy dokładne — na wysokorozdzielczej **fazie**
TIC. Maszyna trójstanowa prowadzi pętlę od zimnego startu do ścisłego locku:

| Etap | Prowadzi na | Co robi | Wyjście gdy |
|------|-------------|---------|-------------|
| **ACQ** | częstotliwość (TIM2) | Wciąganie prowadzone częstotliwością — zbliż OCXO do 10 MHz, by faza narastała dość wolno, żeby ją złapać. picDIV uzbrajany przy wejściu. | \|faza\| mieści się w `acq_threshold` przez kilka cykli |
| **DPLL** | częst. + faza | Oba człony: `Kp·e_freq` (szybki, TIM2) plus PI fazy (TIC). Szybko centruje fazę. | \|faza\| mała **i** dryf niski (poniżej `dpll_threshold`) |
| **LOCK** | faza (TIC) | Prowadzony fazą, powolne wąskopasmowe aktualizacje co `lock_interval` s. | wraca do DPLL, jeśli \|faza\| trwale opuści pasmo histerezy |

Faza pochodzi z `g_ltic_voltage`. Po kalibracji (`ns_per_volt ≠ 0`) pętla pracuje
w nanosekundach względem `zero_offset`; bez kalibracji wraca do błędu napięciowego
wokół środka zakresu z jednorazowym ostrzeżeniem. Co istotne, pasmo pracy
detektora może leżeć daleko od środka ADC (np. 0–0,45 V), więc pętla nigdy nie
zakłada, że 1,65 V to środek — używa skalibrowanego `zero_offset`. Stan zapisany
w EEPROM, więc ciepły restart (`RB`) wznawia od miejsca przerwania, zamiast
startować na zimno od ACQ.

Wybierz go przez `LA 10`; picDIV uzbraja się automatycznie przy wejściu w ACQ.
Najpierw uruchom `LC`, by skalibrować (bez tego pętla wraca do fazy napięciowej
z ostrzeżeniem). `LC` można uruchomić w dowolnej chwili — na czas swojego
sweepu wycisza pętlę dyscypliny, więc działa nawet gdy algorytm 10 jest już
zablokowany. Udane `LC` **auto-zapisuje** swój wynik (ns/V, zero-offset,
zakres) do flash ringa jako dane żywe; **nie** trzeba potem robić `ES`.
Detektor, który nie zawija w oknie sweepu, i tak przechodzi, o ile slope,
centre i span są sensowne; tylko naprawdę słaby wynik jest odrzucany, z
podaniem powodu. Pozostałe komendy poniżej ustawiają/pokazują parametry, które
zapisuje `ES`.

| Komenda | Opis |
|---------|------|
| `LC` | **Autokalibracja** ns/V (lokalne nachylenie), zero-offset (zakotwiczony ~1,85 V) i zakresu (auto, ~7 min; drukuje diagnostykę `t=/V=/n=` co sekundę) |
| `LL` | Lista wszystkich parametrów LTIC + bieżący stan |
| `LNV [v]` | Kalibracja: ns na wolt (nachylenie napięcie TIC→czas) |
| `LZO [v]` | Kalibracja: napięcie TIC przy zerowej różnicy faz |
| `LRN [v]` | Zakres jednoznaczności detektora (ns, dla zawinięcia) |
| `AQP/AQI/AQD/AQL [v]` | PID etapu ACQ: Kp / Ki / Kd / I_LIMIT |
| `DPP/DPI/DPD/DPL [v]` | PID etapu DPLL: Kp / Ki / Kd / I_LIMIT |
| `LKP/LKI/LKD/LKL [v]` | PID etapu LOCK: Kp / Ki / Kd / I_LIMIT |
| `LAT [v]` | Próg ACQ→DPLL (faza w zakresie, ns) |
| `LDT [v]` | Próg DPLL→LOCK (błąd częstotliwości) |
| `LIV [v]` | Interwał aktualizacji LOCK (sekundy, domyślnie 300) |
| `LPOL [-1/0/1]` | Polaryzacja detektora fazy (0 = auto) |
| `LCV` | Pokaż bieżące napięcie TIC (pomoc przy kalibracji) |

#### Korekcja piły (qErr) — `SAW 0|1`

Odbiornik czasowy u-blox generuje swój 1PPS przez dzielenie wewnętrznego
oscylatora, więc każdy impuls pada na krawędź zegara — do jednego okresu
zegara od prawdziwego czasu GPS. Ten błąd kwantyzacji per impuls jest
dominującym krótkookresowym składnikiem fazy na starszych odbiornikach
(granularność LEA-6T to 21 ns). Odbiornik raportuje go z wyprzedzeniem jako
`qErr` w komunikacie UBX-TIM-TP.

Firmware włącza TIM-TP automatycznie przy inicjalizacji GPS, a pasywny sniffer
parsuje `qErr` z tego samego strumienia bajtów, który czyta parser NMEA. Przy
`SAW 1` tor fazy TIC go odejmuje, więc pętla dyscyplinuje względem własnego
błędu OCXO, zamiast gonić piłę granularności odbiornika. `qErr` to 32-bitowe
pole pikosekundowe na tym samym offsecie payloadu w **LEA-6T, LEA/NEO-M8T i
ZED-F9T**, więc jeden parser obsługuje wszystkie trzy. Korekcja wygasa, jeśli
TIM-TP przestanie napływać (reset odbiornika), więc przestarzała wartość nigdy
nie jest stosowana.

`SAW` bez argumentu pokazuje stan i qErr na żywo; `SAW 1`/`SAW 0` przełącza
(zapis przez `ES`, domyślnie wyłączone). Gdy włączone, linia telemetrii
`Learn:` pokazuje `qErr=…ns` dla algorytmu 10, a wartość jest odejmowana od
każdego odczytu fazy TIC. Ponieważ Vphase jest próbkowane na szczycie rampy
tuż po zboczu PPS (patrz uwagi o sprzęcie TIC niżej), każdy odczyt fazy paruje
się z qErr zgłoszonym dla impulsu tej samej sekundy.

---

## Uwagi o sprzęcie TIC — integrator rampy bramkowanej Kaashoeka

Detektor fazy to **1 ns TIC Erika Kaashoeka** (jak w STM32 GPSDO André Balsy,
schemat rev 0.4). Zrozumienie, jak dokładnie działa, kosztowało sporo czasu
przy stole — trzy przerzutniki (dwa 74HC74 przy 5 V, w końcu 74LVC74 przy
3,3 V), zła wartość filtra i długi objazd przez dwa błędne modele detektora.
Zapisane tutaj, żeby następna osoba tego nie powtórzyła.

### Jak to naprawdę działa (potwierdzone oscyloskopem)

Para **przerzutników D typu 74** (`xx74`) zamienia różnicę faz dwóch zboczy
1PPS na impuls: **ładowanie zaczyna się na narastającym zboczu 1PPS z GPS,
a kończy na narastającym zboczu 1PPS z picDIV**, więc szerokość impulsu
*równa się interwałowi fazowemu* między nimi. Impuls bramkuje diodę Schottky
(1N5711), która ładuje C13 przez R8 — **rampa czas-napięcie**, dokładnie jak
w oryginale Larsa Waleniusa, tylko z przerzutnikiem zamiast HC4046. MCU czyta
szczyt rampy raz na sekundę, a ładunek następnie spływa (~25 ms) przed
kolejnym impulsem.

Wynikają z tego dwie rzeczy, obie okupione czasem przy stole:

- **RC musi być małe.** R8×C13 = 1 kΩ × 1 nF, τ ≈ 1 µs — dopasowane do impulsu
  rzędu µs, żeby kondensator liniowo śledził szerokość impulsu. To wartość ze
  schematu Kaashoeka (notka „R8×C13 = 100 ns” na rev 0.4, 1000 ns na
  późniejszym arkuszu); to **nie** jest uśrednianie wypełnienia. Wcześniejsza
  wersja tych notatek twierdziła odwrotnie („detektor wypełnienia” wymagający
  dużego filtra 51 kΩ/1 µF) — to był błąd. Przy 51 kΩ/1 µF impuls µs ledwie
  ruszał kondensator (≈14 mV span w `LC`); przy 1 kΩ/1 nF rampa ma ~1,5–2 V
  i `LC` działa.
- **Odczyt musi trafiać w szczyt.** Rampa osiąga szczyt na końcu impulsu
  (≤ ~2 µs po zboczu GPS) i trzyma go poniżej ~1 ms, zanim opadnie.
  Próbkowanie z 2-sekundowej pętli czujników zawsze łapało rozładowany
  kondensator (~0,065 V, niezależnie od fazy — pierwotna przyczyna tygodni
  „nieudanych kalibracji”). Vphase jest teraz czytany ~50 µs po zboczu PPS, z
  budzonego zboczem PPS zadania przekaźnika, trafiając w szczyt. Bez aktywnego
  rozładowania: dioda odcina, a upływ ~25 ms czyści kondensator przed kolejnym
  impulsem 1 Hz.

### Rola picDIV

picDIV **nie** jest częścią wartości rampy — generuje zdyscyplinowane
**wyjście 1PPS** (zsynchronizowane z UTC, zdolne do holdoveru), a jego zbocze
wyznacza koniec impulsu ładowania. Krok `AP`/arm na początku `LC` tylko parkuje
fazę blisko zbocza GPS, żeby przebieg startował ze znanego punktu; detektor
porównuje 1PPS z GPS z 1PPS z picDIV (pochodzące, odpowiednio, z nieba i ze
zdyscyplinowanego OCXO) — dlatego minimalizacja Vphase wyrównuje wyjściowe PPS
do UTC.

### Kalibracja: zakotwiczony punkt pracy (Opcja D)

Rampa jest wykładnicza (τ ≈ 1 µs), więc ns/V **nie jest stałe** wzdłuż niej.
Średnia po całym przejściu (range/span) zależy więc od tego, gdzie arm
zaparkował fazę, i rozjeżdżała się o ~15–20 % między uruchomieniami. Dwa logi
`LC` z rozdzielczością 1 s pokazały, że **lokalne nachylenie** dV/dt jest
powtarzalne do ~0,3 % w wąskim paśmie wokół **1,85 V** i rozjeżdża się powyżej
oraz poniżej — to napięcie jest powtarzalnym „sweet spotem” detektora
(≈0,63·Vsat, środek zakresu użytecznego). `LC` zakotwicza tam `zero_offset`
i liczy ns/V z lokalnego nachylenia w oknie ±0.20 V, z dala od **stref
martwych** scharakteryzowanych przez Dana Wiering: spadek na diodzie Schottky
+ pull-down poniżej ~0,05 V oraz rail/zawinięcie ADC przy ~3,3 V (PA1 toleruje
5 V, ale czyta tylko do ~3,23 V). Jeśli przebieg nigdy nie przekroczy pasma
kotwicy, `LC` wraca do średniej range/span i to sygnalizuje.

### Rozdzielczość

Rampa 1 kΩ/1 nF pokrywa ~1,5–2 V 12-bitowego ADC w użytecznym oknie fazy, a
16× oversampling z medianą odrzuca glitche — porównywalnie lub lepiej niż
pojedynczy odczyt HC4046 Larsa przy ~1 ns. Zjazd ~25 ms jest bez znaczenia dla
pasma pętli: LOCK aktualizuje się co kilka sekund (znacznie poniżej 0,2 Hz),
więc stała czasowa detektora jest o rzędy wielkości z dala od pętli.

## EEPROM

| Komenda | Opis |
|---------|------|
| `ES` | Zapisz wszystkie parametry do EEPROM |
| `ER` | Odczytaj parametry z EEPROM |
| `EE` | Wymaż EEPROM (przywróć domyślne) |

---

## EEPROM

EEPROM (emulowane w pamięci Flash STM32) przechowuje 144 bajty:

| Adres | Rozmiar | Zawartość |
|-------|---------|-----------|
| 0–5 | 6 B | Sygnatura `"GPSD2"` |
| 6–7 | 2 B | PWM DAC (big-endian) |
| 8 | 1 B | Numer algorytmu (0–9) |
| 9 | 1 B | Przesunięcie czasu (±23 h) |
| 10–121 | 112 B | PID: g_pid[3..9] × {Kp, Ki, Kd, I_LIMIT} |
| 122–133 | 12 B | g_blend_crossover, g_blend_scale, g_nn_max_step |
| 134–137 | 4 B | g_pressure_offset (komenda PO) |
| 138–141 | 4 B | g_altitude_offset (komenda AO) |
| 142 | 1 B | tryb strefy czasowej (0 = ręczny, 1 = auto `TO A`) |
| 143 | 1 B | survey-in włączony (0 = wył., 1 = wł., komenda `SV`) |



---

## Moduły czasowe GPS (LEA-6T / LEA-M8T / NEO-M8T / ZED-F9T)

Moduły NEO-6M / NEO-8M działają od razu (domyślnie). Dla odbiornika
czasowego u-blox włącz opcję w `gpsdo_config.h`:

```c
#define GPSDO_GPS_TIMING            // odbiornik czasowy u-blox (patrz niżej)
#define GPSDO_SVIN_MIN_SECS   300   // min. czas survey-in [s]
#define GPSDO_SVIN_ACC_LIMIT  5000  // próg dokładności [mm] (5 m)
```

LEA-6T i LEA-M8T akceptują **różne** komendy Time Mode, więc firmware
próbuje każdej po kolei i zachowuje pierwszą zaakceptowaną (ACK):
`CFG-TMODE2` (0x06 0x3D, używana przez LEA-M8T) oraz starszą `CFG-TMODE`
(0x06 0x1D, używana przez u-blox 6 LEA-6T). Postęp odczytywany jest przez
`TIM-SVIN` (0x0D 0x04) na obu. (Nowsza para `CFG-TMODE3` / `NAV-SVIN`
istnieje tylko w firmware high-precision, jak NEO-M8P / ZED-F9P, a nie w tych
modułach czasowych — zweryfikowane w u-center na LEA-M8T-0 / TIM 1.10 i
LEA-6T.)

**NEO-M8T** jest w pełni zgodny z LEA-M8T — ten sam układ u-blox M8 i firmware
FW3, te same komendy `CFG-TMODE2` / `TIM-SVIN` — więc działa bez zmian w
kodzie poza włączeniem przełącznika. (Oba warianty M8T domyślnie używają
GPS + GLONASS + QZSS; przekonfiguruj na GPS + QZSS przez `CFG-GNSS` w u-center
i zapisz do flash, jeśli chcesz rozwiązanie jednokonstelacyjne.)

**ZED-F9T (Gen9)** jest również obsługiwany. Generacja F9 zastąpiła komendy
konfiguracyjne legacy (wycofane od firmware TIM 2.24) interfejsem kluczy
konfiguracyjnych i raportuje survey-in przez `NAV-SVIN` (0x01 0x3B), a nie
`TIM-SVIN`. Obsługa dodana jako trzecia ścieżka: `ubx_start_survey_in()`
wysyła też ramkę `CFG-VALSET` (0x06 0x8A) ustawiającą `CFG-TMODE-MODE` /
`CFG-TMODE-SVIN_MIN_DUR` / `CFG-TMODE-SVIN_ACC_LIMIT` (ten ostatni przeliczany
z mm na jednostkę 0.1 mm modułu F9T), a monitor survey-in przechodzi na
`NAV-SVIN`, gdy `TIM-SVIN` nie odpowiada. Ścieżka przetestowana na realnym
sprzęcie przez użytkownika EEVblog **danieljw**. Ramka legacy `CFG-NAV5` (tryb
stacjonarny) może zostać odrzucona (NAK) przez F9T; to nieszkodliwe (ścieżka
survey-in jest niezależna).


Przy każdym starcie odbiornik wykonuje **survey-in**: uśrednia pozycję
anteny, po czym przechodzi w tryb **time-only** o stałej pozycji. Daje to
wyraźnie czystszy 1PPS — timing jednosatelitarny bez szumu nawigacyjnego —
co bezpośrednio poprawia stabilność fazy. Survey-in kończy się, gdy
osiągnięty zostanie minimalny czas **lub** próg dokładności.

Postęp jest pokazywany na wszystkich wyświetlaczach jako
`SVIN <sekundy> <dokładność>m`. Pozycja jest nadal nadawana w NMEA przez
cały czas trybu Time Mode (zamrożony, uśredniony fix), więc wyświetlanie
lokalizacji i automatyczna strefa czasowa (`TO A`) działają dalej — wręcz
stabilniej, bo pozycja przestaje skakać.

> **Antena ma znaczenie.** Survey-in przeprowadzaj wyłącznie z dobrą anteną
> zewnętrzną o pełnym, nieprzesłoniętym widoku nieba. Survey-in uśrednia
> pozycję anteny i kończy się dopiero po osiągnięciu progu dokładności; przy
> antenie wewnętrznej lub przesłoniętej może zbiegać się wolno lub utknąć na
> słabej dokładności (dziesiątki metrów). Na właściwej antenie zewnętrznej /
> dachowej zarówno LEA-6T, jak i LEA-M8T kończą w zadanym czasie i czysto
> przechodzą w Time Mode. (W testach starszy LEA-6T okazał się zauważalnie
> czulszy w trudnych warunkach niż LEA-M8T.)

W trybie Time Mode odbiornik przestaje optymalizować pozycję, więc raportowany
HDOP staje się bezsensowny (~99,99). Wyświetlacze i czytelny raport serial
pokazują wtedy `HDOP:TIME` zamiast błędnej liczby; log z tabulatorami zachowuje
surową wartość do wykresów.

Gdy żadna opcja nie jest zdefiniowana, moduły NEO używają dotychczasowej
ścieżki stationary mode bez zmian.

---

## Równoważenie zużycia Flasha (dane żywe)

Dane “żywe” — nauczony dryf/tłumienie (`LRN`), kalibracja LC i ostatni PWM —
zmieniają się znacznie częściej niż ustawienia, więc są przechowywane osobno
od EEPROM ustawień, w **buforze pierścieniowym z równoważeniem zużycia**
zajmującym sektor 6 Flasha (0x08040000, 128 KB). Przełącznik `FR 0|1` (zapis
`ES`, domyślnie włączone); zużycie sprawdzisz komendą `EW`.

Każdy zapis używa kolejnego 32-bajtowego slotu; sektor kasowany jest dopiero
przy zawinięciu pierścienia (raz na 4095 zapisów). Przy 100 zapisach/dobę to
~9 kasowań/rok, więc wytrzymałość Flasha (~10 000 cykli) starczy na rzędu
tysiąca lat. Zapis następuje tylko gdy wartość ustabilizuje się na nowym
poziomie — dryf zmienił się o > 8 LSB lub tłumienie o > 0.03, i minęło ≥ 20 min
od ostatniego zapisu — a udana kalibracja `LC` zapisuje od razu. Każdy slot ma
CRC i numer sekwencji, więc zanik zasilania w trakcie zapisu jest wykrywany i
używany jest poprzedni dobry slot.

Gdy bufor jest **włączony**, `ES` nigdy nie nadpisuje kalibracji ani wartości
nauczonych — zapisuje tylko prawdziwe ustawienia (nastawy PID, progi, flagi).
Gdy bufor jest **wyłączony**, `ES` nadal zapisuje te dane żywe do EEPROM jako
fallback.

### Zachowanie danych żywych przy ponownym wgrywaniu firmware

- **Bootloader / DFU / Arduino IDE** dotyka tylko sektorów firmware (0–5);
  bufor (6) i EEPROM ustawień (7) przetrwają.
- **Pełne kasowanie układu J-Link/ST-Link** czyści wszystko. By zachować
  kalibrację i uczenie, kasuj tylko sektory 0–5:
  `erase 0x08000000 0x0803FFFF`, potem `loadbin firmware.bin 0x08000000`.
- Jeśli bufor zostanie wyczyszczony, firmware uczy się/kalibruje od nowa — nic
  się nie psuje, tracisz tylko nagromadzone dostrojenie.

---

## Auto-strojenie (komenda `CT`)

`CT` mierzy wzmocnienie obiektu (oscylatora) i wylicza z niego współczynniki
PID dla wszystkich algorytmów — bez ręcznego strojenia, bez ryzykownego
wymuszania oscylacji.

Procedura (~3 minuty, deterministyczna):

1. Ustawia PWM na trzy punkty (1,5 / 2,0 / 2,5 V), z czasem ustalania.
2. Regresja liniowa częstotliwości względem PWM → **K** [Hz/LSB], czyli
   wzmocnienie obiektu, oraz PWM dający dokładnie 10 MHz.
3. Wylicza współczynniki z K:
   - **PLL (4, 5, 7):** Kp = 0,40/K na częstotliwości; Kd = 2,0, Ki = 0,02 na fazie
   - **FLL (3, 6):** Kp = 0,35/K; Ki = Kp/300; Kd = Kp·73
   - **NN (9):** max krok = 0,05/K
4. Stosuje wycentrowany PWM i nowe współczynniki, drukuje przed/po.

Wynik jest kontrolowany (K musi mieścić się w 0,1–2 mHz/LSB, GPS musi mieć
fix); przy błędzie parametry pozostają bez zmian. Po zakończeniu uruchom
`ES`, by zapisać nastawy do EEPROM. W przeciwieństwie do auto-strojenia
metodą relay-feedback, `CT` nigdy nie destabilizuje pętli — stała czasowa
pętli to setki sekund, więc wymuszona oscylacja trwałaby godzinami i byłaby
zaburzona dryfem termicznym; wyliczenie wzmocnień wprost ze zmierzonego K
jest szybsze i bezpieczniejsze.

---

## Automatyczna strefa czasowa (`TO A`)

Czas lokalny może podążać automatycznie za pozycją GPS. W trybie auto
firmware na bieżąco wylicza przesunięcie UTC z szerokości/długości
geograficznej i daty:

- **W Europie** (lat 35–72, lon −11–42): kompaktowy zestaw reguł stref
  cywilnych (UTC+0 na zachód od −7,5°, UTC+1 dla pasa CET łącznie z całą
  Polską, UTC+2 dla krajów bałtyckich/Finlandii/Bałkanów), plus **reguła
  DST UE** — +1 h od ostatniej niedzieli marca 01:00 UTC do ostatniej
  niedzieli października 01:00 UTC.
- **Poza Europą**: strefa słoneczna `round(lon/15)`, bez DST (reguły na
  świecie są zbyt różne, by zgadywać bezpiecznie).

`TO <n>` wraca do stałego przesunięcia ręcznego. Tryb i offset zapisuje
`ES`, przywracane są przy starcie.

---

## Raport sprzętowy przy starcie

Każde opcjonalne urządzenie zgłasza wynik detekcji na serial/Bluetooth
przy starcie, dając pełny obraz wykrytego sprzętu:

```
HW: AHT10/AHT20 sensor    OK  (I2C 0x38)
HW: BMP280 sensor         OK  (I2C 0x77)
HW: INA219 sensor         not found
HW: OLED 128x64           OK  (I2C 0x3C)
HW: LCD 20x4              OK  (I2C expander)
HW: HT16K33 clock display OK  (I2C 0x70)
HW: TFT 320x240            enabled (SPI1, write-only - not verifiable)
HW: TM1637 clock display  enabled (GPIO PA8/PB4, write-only - not verifiable)
```

Brakujące urządzenie zgłasza `not found`, a firmware działa dalej bez niego.

---

## Wejście fazy LTIC (Lars' TIC) — podgląd

Przy włączonym `GPSDO_LTIC` firmware odczytuje sprzętowy licznik interwału
czasu (TIC Larsa Waleniusa): kondensator 1 nF jest ładowany stałym prądem w
interwale GPS-1PPS → OCXO-1PPS, a zatrzaśnięte napięcie na PA1 jest próbkowane
na szczycie rampy ~50 µs po zboczu PPS; aktywne rozładowanie nie jest potrzebne
— dioda odcina, a upływ ~25 ms czyści kondensator przed kolejnym impulsem 1 Hz.
Napięcie to bezpośrednia,
wysokorozdzielcza miara różnicy faz między oboma impulsami — znacznie
dokładniejsza niż licznik cykli TIM2, którego pętla używa dziś.

Obecnie to **tylko podgląd / telemetria**: wartość pojawia się w raporcie
serial (`Vphase:`), jako wiersz `Vph:` na TFT oraz jako pozycja `LTIC phase
(PA1)` w checkliście startowej. Pętla sterowania jeszcze **nie** dyscyplinuje
OCXO na jej podstawie. Stała `LTIC_NS_PER_VOLT` w `gpsdo_config.h` przelicza
wolty na nanosekundy po skalibrowaniu rampy TIC dla danej płytki (domyślnie 0
= nieskalibrowane, więc pokazywane są tylko wolty). Dyscyplinowanie pętli
bezpośrednio z TIC jest planowane jako osobny algorytm; logowanie Vphase
najpierw pozwala scharakteryzować zakres, szum i liniowość TIC na Twoim
sprzęcie, zanim zacznie sterować pętlą.

---

## Oscylator (OCXO)

Firmware współpracuje z dowolnym oscylatorem OCXO 10 MHz sterowanym napięciem,
którego wejście EFC mieści się w zakresie 0–3,3 V dostarczanym przez PWM DAC
STM32 (oscylator 0–4 V EFC też działa — dostępne jest ~82,5% jego zakresu).
Typ oscylatora **nie** musi być wybierany w czasie kompilacji.

Zamiast tego uruchom raz po rozgrzaniu komendę **`CT` (Calibrate & Tune)**:
mierzy ona rzeczywiste wzmocnienie sterowania *K* [Hz/LSB] z trzypunktowego
przemiatania PWM, znajduje wartość PWM dla dokładnie 10 MHz i wylicza wszystkie
współczynniki PID dla zamontowanego oscylatora. Zapisz przez `ES`. Przed pierwszym
`CT` pętla startuje od uniwersalnej wartości środkowej PWM (32767 ≈ 1,65 V),
bezpiecznej dla każdego egzemplarza 0–4 V EFC.

Zastępuje to wcześniejsze tabele współczynników per-oscylator — jedna kalibracja
dopasowuje pętlę do dowolnego zamontowanego kryształu, łącznie z różnicami między
dwoma nominalnie identycznymi egzemplarzami.

---

## Konfiguracja kompilacji

Plik `gpsdo_config.h` steruje konfiguracją. Najważniejsze przełączniki:

```c
// Wyświetlacze — odkomentuj potrzebne:
#define GPSDO_OLED_SSD1309       // lub SH1106, SSD1306
#define GPSDO_LCD_20x4           // HD44780 20x4 I2C
#define GPSDO_TM1637_6           // 6-cyfrowy TM1637 (HH:MM:SS)
#define GPSDO_TFT_ST7789         // ILI9341/ST7789 320x240, lub GPSDO_TFT_ILI9488 480x320
#define GPSDO_HT16K33            // 4-cyfrowy zegar HT16K33, I2C 0x70

// Czujniki:
#define GPSDO_AHT10              // AHT10/20 temperatura+wilgotność
#define GPSDO_BMP280_I2C         // BMP280 temperatura+ciśnienie
#define GPSDO_INA219             // INA219 napięcie+prąd

// Komunikacja:
#define GPSDO_BLUETOOTH          // HC-06 na Serial2 (57600 Bd)

// Inne:
#define GPSDO_EEPROM             // Zapis parametrów
#define GPSDO_PICDIV             // Wsparcie picDIV
#define GPSDO_UBX_CONFIG         // Konfiguracja UBX NEO-6M/7M
#define GPSDO_GEN_2kHz_PB5       // Generator 2 kHz na PB5
```

### Bufor szeregowy (`build_opt.h`)

W katalogu szkicu znajduje się też `build_opt.h`, który STM32duino przekazuje
do całej kompilacji (łącznie z rdzeniem) jako flagi kompilatora:

```
-DSERIAL_RX_BUFFER_SIZE=256 -DSERIAL_TX_BUFFER_SIZE=512
```

Powiększa to bufor RX portu GPS z domyślnych 64 bajtów, by zdania NMEA nie były
gubione ani sklejane przy 38400 baud, gdy zadanie GPS zostanie na chwilę
wywłaszczone. Zwykły `#define` w szkicu nie zadziała — rdzeniowy
`HardwareSerial.cpp` to osobna jednostka kompilacji widząca tylko flagi
kompilatora. Plik jest wykrywany automatycznie; nic nie trzeba włączać.

---

## Przypisanie pinów

| Pin | Funkcja |
|-----|---------|
| PA15 | TIM2 ETR — wejście 10 MHz z OCXO |
| PB10 | TIM3 CH3 — capture 1PPS z GPS |
| PB9 | PWM DAC — sterowanie Vctl (16-bit) |
| PB1 | ADC — pomiar Vctl |
| PA0 | ADC — pomiar Vcc/2 |
| PB8 | LED żółta — sygnalizacja fixa/holdovera |
| PC13 | LED niebieska — blink 1PPS |
| PB5 | Generator 2 kHz (opcjonalnie) |
| PB3 | picDIV ARM (opcjonalnie) |
| PA1 | LTIC Vphase (opcjonalnie) |
| PA9/PA10 | Serial1 TX/RX — GPS NMEA |
| PA2/PA3 | Serial2 TX/RX — Bluetooth HC-06 |
| PB6/PB7 | I2C1 SCL/SDA — OLED, LCD, czujniki |
| PA5/PA7 | SPI1 SCK/MOSI — wyświetlacz TFT |
| PB12/PB13/PB15 | TFT D/C, CS, RES |

---

## Wymagania

- **Płytka**: WeAct BlackPill STM32F411CE lub F401CCU6
- **Środowisko**: Arduino IDE z rdzeniem STM32duino ≥ 2.2.0
- **Biblioteki**: STM32duino FreeRTOS, TinyGPS++, U8g2,
  Adafruit AHTX0, Adafruit BMP280, Adafruit INA219,
  hd44780 (dla LCD), TFT_eSPI (dla TFT), EEPROM (STM32)
- **Ustawienia kompilacji**: Tools → C Runtime Library → Newlib Nano + Float Printf/Scanf

---

## Licencja

Opublikowane na tych samych warunkach co oryginalny projekt André Balsy.


## Trójstanowa dyscyplina LTIC (algorytm 10) — zmiany v0.5x–v0.88

Pętla LTIC (ACQ→DPLL→LOCK) jest w pełni samokonfigurująca:

- **`LC` — samokalibracja jedną komendą**: uzbraja picDIV, startuje z
  deterministycznego dolnego punktu detektora, zadaje tempo przemiotu ze
  zmierzonego K, próbkuje całe pasmo w jednym przebiegu dół→góra (górne
  nasycenie kończy pomiar), skaluje ns/V precyzyjnym avg100. Strażnicy
  odrzucają artefakty nasycenia i niemożliwe zakresy; werdykt
  `PASSED`/`MARGINAL` mówi, czy robić `ES`.
- **Automatyczne wzmocnienia** z dwóch zmierzonych stałych (K z `CT`,
  ns/V + zakres z `LC`) — bez ręcznego strojenia. LOCK: deadband, miękkie
  kolano, limit kroku ~4 mHz.
- **Odporny tor ADC**: mediana 16 odczytów na PPS + bramka outlierów.
- **Strażnik ucieczki**: faza na railu + |df| > 0,5 Hz zamraża pętlę.
- **Wiarygodny kolor locka**: przy algorytmie 10 zielony TYLKO w żywym LOCK.
- **Komendy**: `LC`, `LL`, `LPOL -1|0|1`, `LIV 1..30`, `WU 0|1` (wygrzewanie
  OCXO przy starcie, zapis EEPROM), `SPL 0|1` (animacja powitalna wł./wył.,
  zapis EEPROM), `FR 0|1` (bufor pierścieniowy Flasha dla danych żywych,
  zapis EEPROM), `EW` (statystyki zużycia Flasha), `SAW 0|1` (korekcja piły
  qErr dla LTIC, zapis EEPROM). Animacje LED: warmup = fala dolnego 'o',
  survey-in = fala górnego 'o', kalibracja = "CAL" + spinner.

## Obsługa kolorowych TFT (TFT_eSPI)

Powinien działać każdy wyświetlacz TFT_eSPI o rozdzielczości **320×240**
lub **480×320** — UI skaluje się przez `TFT_SX()/TFT_SY()`. Przetestowane:
ILI9341 (320×240), ST7789 (240×320), ILI9488 (480×320). Aby dołączyć swój:

1. włącz pasujący `GPSDO_TFT_*` w `gpsdo_config.h`,
2. ustaw sterownik i piny w `User_Setup.h` biblioteki TFT_eSPI (SPI1: SCK=PA5,
   MOSI=PA7; CS/DC/RST jak w `gpsdo_config.h`),
3. inny kontroler wybierz w `User_Setup.h` — każdy panel zgłaszający
   320×240 lub 480×320 pasuje bez zmian w kodzie.
