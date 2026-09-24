# GPSDO v1.07.57rt

[English](README_EN.md) | **Polski** | [Español](README_ES.md)

📖 [Strona projektu](../README.md) · Instrukcja: [MD](MANUAL_PL.md) · [PDF](MANUAL_PL.pdf)

Firmware czasu rzeczywistego (FreeRTOS) dla oscylatora sterowanego GPS (GPSDO)
na platformie STM32 BlackPill (WeAct F411CE / F401CCU6).

📋 Historia wersji: [Lista zmian](CHANGELOG_PL.md)

## Autorzy i podziękowania

| Rola | Osoba / źródło |
|------|----------------|
| Autor portu FreeRTOS oraz algorytmów 3–11 i 13 | **J. M. Niewiński** — [repozytorium](https://github.com/jmnlabs/GPSDO_FreeRTOS) |
| Asystenci programowania | **Claude Opus 5 (Anthropic) · GLM-5.3 Max (Z.ai) · Qwen3.8-Max** |
| Pomiary i testy terenowe, algorytmy 10–12 | **Dan Wiering** — przebiegi ADEV względem wzorca rubidowego, które wykryły cykl graniczny algorytmu 10, rozstrzygnęły kwestię tłumienia `FA`, zgłosiły zablokowanie ACQ i ustaliły porównanie dla algorytmu 11; teraz dociska algorytm 12 |
| Obsługa ILI9486 / ILI9488 — impuls do implementacji; a tuner na PC wyrósł ze skryptu monitorującego, który skleił z Claude | **lucido** (forum EEVBlog) |
| Autor v0.06c — inspiracja portu RTOS | **André Balsa** — [repozytorium](https://github.com/AndrewBCN/STM32-GPSDO) |
| Ciągła pętla PI (algorytm 11) — projekt oryginalny | **Lars Walenius** (pamięci) — kontroler GPSDO udostępniony społeczności [time-nuts](http://www.leapsecond.com/time-nuts.htm) i EEVBlog. Rozwinięty tutaj o auto-kalibrację z CT, gałąź akwizycji prowadzoną częstotliwością i pomost przechwytywania fazy picDIV. |
| Akumulator wielopoziomowy (algorytm 12) — projekt oryginalny | **Alan Cashin** (MIS42N, forum EEVBlog) — [profil](https://www.eevblog.com/forum/profile/?u=121386) — jego Budget GPSDO jest źródłem algorytmu 12, korekcji przejścia przez zero, ditherowanego PWM osiągającego 24 bity z krótkiego przebiegu oraz pomysłu na samoocenę `CS`. Zaimplementowane tutaj na detektorze fazy LTIC, z granicami poziomów pozostawionymi do edycji, bo tylko ta dla 128 s została kiedykolwiek wyprowadzona ze specyfikacji. |
| Filtr Kalmana (algorytm 13) — projekt autorski | **J. M. Niewiński** — trzy stany przy pomiarze skalarnym, więc podręcznikowe odwracanie macierzy to jedno dzielenie, a cały filtr kosztuje jakąś mikrosekundę na sekundę na M4F. Każda stała, której inaczej by potrzebował, jest wyprowadzona z `CT` i `LC`, więc nie niesie ani jednej liczby zmierzonej na jednej konkretnej płytce. |
| Projekt PCB (prototyp) | **Scrachi** (forum EEVBlog) — [post z plikami](https://www.eevblog.com/forum/projects/yet-another-diy-gpsdo-yes-another-one/825/) · [profil](https://www.eevblog.com/forum/profile/?u=762266) |
| Wątek projektowy | [Yet another DIY GPSDO](https://www.eevblog.com/forum/projects/yet-another-diy-gpsdo-yes-another-one/) — EEVBlog Forum |

Firmware został napisany od podstaw jako port oryginalnego kodu André Balsy
na architekturę FreeRTOS, z pełnym przeprojektowaniem zadań, synchronizacji
i wyświetlania. Konstrukcja sprzętowa bazuje na schemacie z projektu v0.06c
z wykorzystaniem PCB udostępnionych przez użytkownika Scrachi na forum EEVBlog.

---


> **Konsola strojenia na PC:** zobacz [README_TUNER_PL](README_TUNER_PL.md) —
> wykresy na żywo, zakładki parametrów i generator `gpsdo_tz_table.h`.

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
               │ NEO-6M/8M   │       │  10 MHz      │                     │
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
  AHT  BMP  INA        │ ILI9488    │  480x320
                       └────────────┘
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
- **TFT 480×320** SPI (ILI9488, biblioteka TFT_eSPI) — przetestowany w terenie
  względem wzorca rubidowego; układ 320×240 jest automatycznie skalowany
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
| Najniższy | `vUptimeTask` | 768 B | Zegar czasu pracy tylko w holdoverze — poza nim napędza go PPS |

**Współdzielony stan** chroniony jest muteksami FreeRTOS:

- `xFreqMutex` — dane częstotliwości (`gFreq`, `gFreqSnap`)
- `xGpsMutex` — dane GPS (`gGps`)
- `xCtrlMutex` — dane sterowania (`gCtrl`: PWM, algorytm, holdover, trend)
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
| 11 | LTIC-Lars | faza TIC | ciągła | Pojedyncza ciągła pętla PI, bez maszyny stanów; wzmocnienie wyliczane z `CT`. Wg Larsa Waleniusa |
| 12 | Akumulator wielopoziomowy | faza LTIC [ns] | adaptacyjna | Brak stałej czasowej do ustawienia: błąd sam wybiera okno uśredniania. Wg Alana Cashina (MIS42N). **Niedostrojony.** |
| 13 | Filtr Kalmana | faza LTIC [ns] + częstotliwość TIM2 | adaptacyjna | Trzy stany — faza, częstotliwość, starzenie — z kowariancjami: waga każdego odczytu bierze się ze zmierzonych wariancji, a nie ze stałej, a każda skala jest wyprowadzona z `CT` i `LC`. Autorski. |

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
Parametry zapisywane są we flash ringu komendą `ES`.

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

### Dwa rozmiary paneli

Ekran roboczy jest rysowany raz, w 320×240, i skalowany do 480×320 przy
kompilacji przez `TFT_SX` / `TFT_SY` / `TFT_F`. Rozmieszczenie pól jest więc
**identyczne na obu panelach** — nic nie przesuwa się względem niczego. Różni się
sposób renderowania tekstu, a ta różnica jest na tyle duża, że ma znaczenie.

```
 ┌──────────────────────────────────────────────────┐
 │ GPSDO v1.07.57rt                LMT 14:32:45 Pon │   nagłówek: wersja + czas lokalny
 │                                                  │
 │          1 0 0 0 0 0 0 0 . 0 0 0 0  H z          │   częstotliwość, duży font
 │                                                  │
 │ UTC: 12:32:45 Pon        Sat: 11 HDOP: 0.77      │
 │ DATE: 28/07/2026         Lat:  52.229676         │
 │ Uptime: 000d 01:42:12    Lon:  21.012229         │
 │ Algo: 12 LOCK            Alt:118m     qEr:-4.2ns │   algorytm + trend   |  dane fiksu + qErr
 │ PWM:40849 Vct:1.799V     INA:4.920V     182.00mA │   wyjście sterujące  |  monitor zasilania
 │ BMP: 51.1C 1006.7hPa     Vph:2.083V dp:     -5ns │   czujniki           |  detektor fazy
 │ AHT: 24.60C 41.20%rH     Vcc:5.012V    Vdd:3.29V │                      |  szyny zasilania
 │                                                  │
 │               DISCIPLINED  FIX OK                │   pasek stanu (kolor = stan)
 └──────────────────────────────────────────────────┘
```

Układ powyżej jest taki sam na obu panelach. Nie zawsze taki był — obie gałęzie
rozjeżdżały się pole po polu aż do v1.05, kiedy mały panel wrócił do szeregu:
qErr siedzi w wierszu Alt obok danych fiksu, czujniki środowiskowe dzielą lewą
kolumnę, elektryczne prawą, a Vcc i Vdd dzielą dolny wiersz czujników.

**Jeśli zmieniasz układ małego panelu, mierz — nie licz znaków.** Font 2 jest
proporcjonalny. `Vph:1.951V` to 70 px, a nie 80, które sugeruje dziesięć znaków
po 8 px; w skali wiersza zawyżenie sięga jednej piątej. Dwa pola zostały już na
podstawie tej arytmetyki skasowane i oba się mieściły, gdy je zmierzono.
`tft_text_w()` pyta bibliotekę — tak robi cała gałąź 480; tam, gdzie zamiast tego
użyto stałej, komentarz obok podaje zmierzoną szerokość, z której pochodzi.

Padding każdego pola to szerokość jego **najszerszej** formy, a nie dzisiejszego
odczytu: argument szerokości w `dtostrf()` jest minimum, więc wartość zyskuje
znak przy przekroczeniu potęgi dziesiątki, a padding wymierzony na typowy
przypadek pozwala temu znakowi wylądować na sąsiedzie. Paddingi w wierszu
kafelkują go dokładnie — bez dziur i bez nakładek — więc żadne tło nie zetrze
krawędzi sąsiedniego pola. Na małym panelu wszystkie pola prawej kolumny są
zakotwiczone na x=314.

**320×240 (ILI9341, ST7789)** używa klasycznych fontów numerycznych GLCD do
ekranu roboczego. Mają już właściwy rozmiar w skali 1:1, renderują się szybko i
nie wymagają `LOAD_GFXFF` w `User_Setup.h`. Uzasadnienie w podsekcji *Dlaczego
mały panel zachowuje klasyczne fonty* poniżej.

**480×320 (ILI9488, ILI9486)** używa wolnych fontów Adafruit GFX przez warstwę
abstrakcji per rola (`GF_DATA`, `GF_HEAD`, `GF_STATUS` itd. w `gpsdo_config.h`).
Przeskalowanie klasycznych fontów bitmapowych o 1,5 dałoby widocznie
poszarpane cyfry; wolne fonty pozostają czyste w większym rozmiarze. Ta wersja
**wymaga** `LOAD_GFXFF`.

Większy panel przenosi też 2,4× więcej pikseli na przerysowanie, więc
`SPI_FREQUENCY` nie powinno na nim schodzić poniżej 40 MHz — patrz uwagi o
połączeniach poniżej.

> **Wsparcie ILI9488 / ILI9486 480×320 zweryfikowane na panelu (v0.93).** Ekran
> roboczy 320×240 i splash są automatycznie skalowane do 480×320 podczas
> kompilacji (szerokość ×1.5, wysokość ×1.33). Duży panel rysuje cały swój tekst
> fontami Adafruit GFX, co naprawiło objawy widoczne na zdjęciach pierwszych
> użytkowników (Dan Wiering, lucido) — podtytuł splashu zwijający się do samego
> „p" i pusty pasek statusu wynikały z braku liter w numerycznych fontach GLCD,
> nie z problemu ze skalowaniem. Geometria pasów (częstotliwość, siatka,
> sensory, status) została przeliczona pod wyższe wiersze fontów
> proporcjonalnych i sprawdzona na żywym panelu ILI9488, tak aby żaden wiersz
> nie przecinał separatora na obu rozmiarach.
>
> ILI9488/ILI9486 po SPI przenosi 2,4× więcej pikseli niż panel 320×240 przy
> kolorze 18-bit, co dawniej sprawiało, że każde przerysowanie było widoczne. Od
> v0.93 żywe obszary są podwójnie buforowane jako sprity i wypychane po jednym
> transferze każdy, więc przerysowanie już nie migocze — patrz [Sprity: dlaczego
> wyświetlacz przestał migotać](#sprity-dlaczego-wyświetlacz-przestał-migotać).
> Na tym panelu ustaw SPI na 40 MHz.
>
> Panele 320×240 zostają przy klasycznych fontach numerycznych na ekranie
> roboczym — kroje GFX są za szerokie dla tego layoutu. Patrz [Dlaczego mały
> panel zostaje przy klasycznych
> fontach](#dlaczego-mały-panel-zostaje-przy-klasycznych-fontach).

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
│ v1.03-rt      GPSDO      LMT 14:32:45 Thu   │ ← header bar (navy)
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
│ BMP: 23.40C 1013.2hPa│ AHT: 22.10C 45.3%rH │
│ Vph: 2.615V +830ns   │ Vdd: 3.30V          │
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
#define LOAD_GLCD               // klasyczny font 1 — częstotliwość + napisy autorskie splashu
#define LOAD_FONT2              // klasyczny font 2 — nagłówek + siatka danych
#define LOAD_FONT4              // klasyczny font 4 — pasek statusu, komunikaty, podtytuł splashu
#define SPI_FREQUENCY 40000000  // SPI1 w F411 kończy się na 50 MHz; 40 zostawia zapas
```

> **Wersja 320×240 nie potrzebuje `LOAD_GFXFF`.** Wszystko na tym panelu — ze
> splashem włącznie — rysowane jest klasycznymi fontami numerycznymi, więc
> powyższe trzy linie `LOAD_` to komplet. To celowe: patrz [Dlaczego mały panel
> zostaje przy klasycznych
> fontach](#dlaczego-mały-panel-zostaje-przy-klasycznych-fontach). Jeśli
> przechodzisz ze starszej wersji, Twój `User_Setup.h` niemal na pewno już je
> ma.

Dla panelu **ILI9488 / ILI9486 (480×320)** zmień driver i wymiary oraz dodaj
`LOAD_GFXFF` — duży panel rysuje nagłówek, siatkę, pasek statusu i
częstotliwość fontami Adafruit GFX. Firmware sam dobiera rozmiary punktowe w
czasie kompilacji (patrz makra `GF_*` w `gpsdo_config.h`):

```c
#define ILI9488_DRIVER          // działa dla paneli ILI9488 i ILI9486
#define TFT_WIDTH  320
#define TFT_HEIGHT 480
// ...te same linie TFT_MISO/MOSI/SCLK/CS/DC/RST/RGB_ORDER co wyżej...
#define LOAD_GLCD               // font 1 — napisy autorskie splashu
#define LOAD_FONT4              // font 4 — zostaje dla ścieżki podtytułu splashu
#define LOAD_GFXFF              // WYMAGANE na tym panelu — fonty GFX
#define SPI_FREQUENCY 40000000  // 480×320 przepycha 2,4× więcej pikseli — nie oszczędzaj
```

> **Zegar SPI.** 40 MHz to przetestowane ustawienie na F411 (SPI1 szczytuje na
> 50 MHz, więc zostaje zapas na okablowanie, które nie jest idealne). Ma to
> największe znaczenie na panelu 480×320: pełne przerysowanie przenosi 2,4×
> więcej pikseli niż na małym panelu, a pushe sprite'ów (niżej) to pojedyncze
> ciągłe transfery, których czas skaluje się wprost z zegarem. Jeśli panel
> pokazuje artefakty, zejdź do 27 MHz — długie przewody mogą nie znieść 40.

**Rozwiązywanie problemów:** jeśli po włączeniu TFT firmware zawiesza się na
splashu wersji na OLED, sprawdź wyjście serial. Komunikat `TFT: init start ...`
jest drukowany bezpośrednio przed `TFT_eSPI::init()` — jeśli to ostatnia
linia, zawieszenie jest w bibliotece: zweryfikuj, że `User_Setup.h` zawiera
dokładnie powyższe piny (łącznie z `TFT_MISO PA6`) i właściwy driver. Stos
DisplayTask jest automatycznie zwiększany do 768 słów gdy `GPSDO_TFT` jest
włączone — jeśli modyfikowałeś rozmiary stosów ręcznie, przywróć tę wartość.

Następnie włącz `GPSDO_TFT_ST7789`, `GPSDO_TFT_ILI9341` lub `GPSDO_TFT_ILI9488` w `gpsdo_config.h`.

### Dlaczego mały panel zostaje przy klasycznych fontach

v0.92 przeniosło każdy ekran na fonty Adafruit GFX. Na panelu 480×320 to była
wyraźna wygrana: litery są porządnie ukształtowane, layout ma czym oddychać, a
`FreeMonoBold` trzyma cyfry częstotliwości w stałych kolumnach.

Na 320×240 tę samą zmianę wypróbowano i **cofnięto w v0.93**. Kroje GFX są
proporcjonalne i zauważalnie szersze niż fonty numeryczne, wokół których ułożono
layout, a 320 px po prostu nie ma miejsca na tę różnicę: wartości uciekały poza
swoje kolumny do sąsiedniej (`Uptime: 000d 00:01:03n: ---`,
`PWM:44778 Vct:1.9INA: 4.888V 224.5`), a środkowy separator przecinał to, co
wystawało. Zmniejszenie fontu też nie wchodziło w grę — 9 pt to *najmniejszy*
FreeSans dostarczany z TFT_eSPI, a jedyne co jest niżej to TomThumb (3×5 px),
nieczytelny z odległości ręki.

Mały panel zostaje więc przy tym, co się mieści: klasyczny font 2 na nagłówek i
siatkę, font 4 na pasek statusu, font 1 ×3 (18×24, stała szerokość) na
częstotliwość. Splash idzie tym samym tropem — podtytuł używa fontu 4, który ma
pełny alfabet (to fonty 6/8 są bez liter i to one zamieniały kiedyś „GPS
Disciplined OCXO" w samo „p"), a kredyty fontu 1. To był ostatni bastion GFX, a
jego usunięcie oznacza, że **wersja 320×240 nie potrzebuje `LOAD_GFXFF` w
ogóle** — przechodząc ze starszej wersji można zostawić `User_Setup.h` w
spokoju. Makra `TFT_FONT_*` w `gpsdo_config.h` dokonują wyboru w czasie
kompilacji; jest jeden layout, nie dwa.

**Środkowy separator kolumn** jest podobnie tylko na 480: przy 320 px kolumny
dochodzą do samego środka i linia nie miała gdzie pójść, żeby nie przeciąć
tekstu.

### Sprity: dlaczego wyświetlacz przestał migotać

Panel jest zapisywany przez SPI, więc wszystko rysowane wprost na niego jest
*widziane* w trakcie rysowania. Stary kod kasował, zanim zapisał:
`setTextPadding` wypełniał około 480×34 px tła, a potem nowy tekst lądował na
wierzchu. Przy jednej aktualizacji na sekundę ten cykl kasuj-potem-rysuj był
wyraźnie widoczny jako migotanie w pasmie częstotliwości — gorzej na panelu
480×320, gdzie kasowanie obejmuje 2,4× więcej pikseli.

v0.93 buforuje zamiast tego trzy żywe obszary w RAM — nagłówek, pasmo
częstotliwości i obszar danych — jako obiekty `TFT_eSprite`. Każde przerysowanie
czyści i maluje swój sprite niewidocznie w RAM, a potem wypycha gotowe pasmo na
panel **jednym ciągłym transferem SPI**. Nie ma stanu pośredniego na szkle, więc
nie ma czemu migotać. Ramka i separatory leżą poza granicami sprite'ów (albo,
gdy separator przecina pasmo, są rysowane w sprite i wypychane razem z nim),
więc nic ich nie tyka.

Pamięć jest skromna dzięki paletom — 4-bit na pasmo nagłówka i częstotliwości,
1-bit na obszar danych, ~25 KB łącznie na panelu 480, spokojnie w 128 KB F411.
Ramka jedzie w środku sprite'ów, zamiast być rysowana na panelu — i dlatego jest
biała na obu rozmiarach. Sprite danych jest 1-bitowy, więc ma tylko dwa kolory:
biel i tło. Granatowej ramki nie dałoby się w nim narysować, trzeba by ją
domalowywać na panelu po każdym pushu, co niweczyłoby cały sens. Biel trzyma
ramkę i tekst w tym samym atomowym transferze.

Jeśli `createSprite()` kiedyś zawiedzie (pofragmentowana sterta), każde pasmo
wraca do rysowania bezpośrednio na panel: dostajesz stare migotanie, ale nic się
nie psuje. Log startowy mówi, która ścieżka działa:

```
TFT: freq-band sprite (4-bit) created
TFT: header sprite (4-bit) created
TFT: data sprite (1-bit) created
```

---

### Obsługa kolorowych TFT (TFT_eSPI)

Powinien działać każdy wyświetlacz TFT_eSPI o rozdzielczości **320×240**
lub **480×320** — UI skaluje się przez `TFT_SX()/TFT_SY()`. Przetestowane:
ILI9341 (320×240), ST7789 (240×320), ILI9488 (480×320). Aby dołączyć swój:

1. włącz pasujący `GPSDO_TFT_*` w `gpsdo_config.h`,
2. ustaw sterownik i piny w `User_Setup.h` biblioteki TFT_eSPI (SPI1: SCK=PA5,
   MOSI=PA7; CS/DC/RST jak w `gpsdo_config.h`),
3. inny kontroler wybierz w `User_Setup.h` — każdy panel zgłaszający
   320×240 lub 480×320 pasuje bez zmian w kodzie.

---

## Wyświetlacze zegarowe (HT16K33 i TM1637)

Obsługiwane są dwa małe moduły 7-segmentowe. Oba pokazują czas, respektują
ustawienie `LT` (UTC albo czas lokalny) i migają dwukropkiem, żeby zatrzymany
wyświetlacz rzucał się w oczy. Żaden nie pokazuje stanu GPSDO — istnieją po to,
by urządzenie było użytecznym zegarem w trakcie dyscyplinowania.

### HT16K33 — 4 cyfry, I2C

```
 ┌────────────────┐
 │  1 4 : 3 2     │   HH:MM, dwukropek miga raz na sekundę
 └────────────────┘
```

Adres I2C 0x70, dzieli magistralę z czujnikami i OLED-em. Włączany przez
`GPSDO_HT16K33`. Przy starcie raportowany jako
`HW: HT16K33 clock display OK (I2C 0x70)`, więc brak modułu albo zły adres
wychodzi od razu.

### TM1637 — 4 albo 6 cyfr, dwuprzewodowy

```
 ┌────────────────┐
 │  1 4 : 3 2     │   wersja 4-cyfrowa: HH:MM
 └────────────────┘
 ┌────────────────────┐
 │  1 4 : 3 2 : 4 5   │   wersja 6-cyfrowa: HH:MM:SS
 └────────────────────┘
```

Używa własnych pinów CLK/DIO, nie I2C. `GPSDO_TM1637` wybiera wariant
4-cyfrowy, `GPSDO_TM1637_6` sześciocyfrowy. Dwukropek miga na parzystych
sekundach.

> **TM1637 i LCD 20×4 nie mogą pracować jednocześnie** — rywalizują o te same
> piny. Wybierz jeden przy kompilacji.

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
| `CT` | Kalibracja + auto-strojenie: pomiar K, wyliczenie PID dla algo 3-9 (+ LTIC 10 & 11; auto-save) |
| `T [baud]` | Tunel GPS na USB dla u-center — czysty dwukierunkowy NMEA/UBX (telemetria na Bluetooth jeśli jest, inaczej wyciszona); opcjonalny baud UART GPS, zachowany po wyjściu; wyłącza się po 300 s |
| `SP <n>` | Ustaw PWM DAC bezpośrednio (1–65535), omija algorytm |
| `DAC` | Wyjście napięcia sterującego: ścieżka, kod 24/16-bitowy i dokładny, zmierzone Vctl, wielkość kroku w µHz i df/f (liczby w Hz wymagają `CT`) |
| `SPAN [CLR FULL\|REDUCED]` | Zworka zakresu EFC na PB14 (`GPSDO_SPAN_SENSE`): pozycja, w której jest, i kalibracja `CT` każdej pozycji — jedna na zakres, przełączana i przeliczana, gdy zworka się przestawi; `CLR` zapomina jedną |
| `RH` | Tryb raportowania: czytelny (domyślny) |
| `RD` | Tryb raportowania: rozdzielany tabulatorem |
| `RP` | Wstrzymaj strumień danych serial/BT |
| `RR` | Wznów strumień danych serial/BT |
| `SW` | Znaczniki zużycia stosów zadań FreeRTOS, wolna sterta, źródło uptime'u i zmierzony błąd zegara MCU wobec GPS (diagnostyka) |
| `CS` | Statystyki korekcji: jak ciężko pracuje pętla, a po `CT` także w df/f |

### Sterowanie

| Komenda | Opis |
|---------|------|
| `MH` | Włącz tryb holdover (ręczny) |
| `MD` | Włącz tryb dyscyplinowany |
| `LA [0-12]` | Wybierz / pokaż algorytm sterowania |
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
| `TO [n]` | Pokaż / ustaw stały offset — godziny lub `h:mm` (`TO 9:30`, `TO -5`) |
| `TO A` | Auto-strefa: strefa z pozycji GPS + reguła DST UE (tylko Europa) |
| `TZ [strefa]` | Strefa z DST — `TZ Adelaide` lub reguła POSIX. Zob. `H TZ` |
| `PO [f]` | Pokaż / ustaw offset ciśnienia |
| `AO [f]` | Pokaż / ustaw offset wysokości |
| `SV [0\|1]` | Survey-in / Time Mode na module czasowym (zapis przez `ES`, działa od następnego startu) |

### Strefy czasowe

`TZ <miasto>` zwykle wystarcza:

```
TZ Adelaide          → UTC+9:30, a UTC+10:30 gdy działa DST
TZ Warsaw            → UTC+1 / UTC+2
TZ Kolkata           → UTC+5:30, bez DST
```

Nazwy miast są unikalne w całej bazie IANA, więc region jest opcjonalny —
`TZ Australia/Adelaide` też działa — a wielkość liter nie ma znaczenia.
Wbudowanych jest 503 strefy.

Regułę można też podać w całości, co ma znaczenie, gdy rząd zmieni przepisy,
zanim firmware to nadgoni:

```
TZ ACST-9:30ACDT,M10.1.0,M4.1.0/3
```

`H TZ` wyjaśnia format. `ES` zapisuje ustawienie.

**Dlaczego nie baza IANA?** Ma ~2 MB — czterokrotność całego flasha tego MCU —
a jej wartość polega na aktualizacjach kilka razy w roku, z czego GPSDO bez
internetu nie skorzysta. String POSIX TZ, do którego sprowadza się każda
strefa, ma 4–44 bajty i niesie to samo zachowanie na dziś: offset
standardowy, offset letni i oba przejścia. Półkula południowa nie wymaga
osobnego przypadku — miesiąc startu późniejszy niż miesiąc końca po prostu
oznacza, że DST przechodzi przez Nowy Rok.

`TO A` (auto z pozycji GPS) zostaje bez zmian. Jest poprawne w większości
Europy i nie wymaga konfiguracji, ale nie zna DST poza nią i zwraca wyłącznie
całe godziny — więc nie wyrazi +9:30 z Adelaide ani +5:30 z Indii. Poza
Europą używaj `TZ`.


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
we flash ringu, więc ciepły restart (`RB`) wznawia od miejsca przerwania, zamiast
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
| `LIV [v]` | Interwał aktualizacji LOCK (sekundy, domyślnie 300, 1..600 s) |
| `LPOL [-1/0/1]` | Polaryzacja detektora fazy (0 = auto) |
| `LCV` | Pokaż bieżące napięcie TIC (pomoc przy kalibracji) |

### Kolejność kalibracji: najpierw `CT`, potem `LC`

**Uruchom `CT` przed `LC`.** Te dwie komendy nie są niezależne: `LC` przemiata
PWM, żeby trafić w zadane tempo zmian fazy, a do wyliczenia, jak daleko sterować,
potrzebuje K — nachylenia Hz na LSB Twojego OCXO, które mierzy właśnie `CT`. Bez
tego `LC` przyjmuje ogólne 3000 LSB/Hz i kalibracja wychodzi przeskalowana o tyle,
o ile Twój oscylator odbiega od tego przybliżenia.

Pułapka polega na tym, że **zła odpowiedź nie wygląda na złą**. Na jednej płytce
`LC` przed `CT` dało ns_per_volt = 1592,8 i wynik WEAK; po `CT` ta sama płytka
dała 921,2 i PASSED — różnica 1,7×, przy czym nic w pierwszym przebiegu nie
sugerowało, że coś jest nie tak.

Kolejność dla nowej płytki:

1. `CT` — mierzy K, wylicza zestawy PID, zapisuje je automatycznie
2. `LC` — kalibruje detektor fazy, zapisuje automatycznie przy PASS
3. `LPOL -1` albo `LPOL 1`, jeśli pętla zgłosi nieustawioną polaryzację, potem `ES LTIC`
4. `LA 10` albo `LA 11`, potem `ES ALGO`

`LC` ostrzega, gdy `CT` nie zostało wykonane, ale i tak kontynuuje — powtórzenie
kosztuje trzy minuty, a bywają uzasadnione powody, by przemiatać najpierw.

### Algorytm 11 — LTIC-Lars (ciągła pętla PI)

Pojedyncza ciągła pętla PI bez maszyny stanów ACQ/DPLL/LOCK, na wzór
oryginalnego kontrolera GPSDO śp. Larsa Waleniusa. Dzieli kalibrację detektora
z algorytmem 10 (`LC`), więc jedna kalibracja służy obu pętlom. Etykiety trendu
używają tego samego słownictwa co algorytm 10: **ACQ** (prowadzenie
częstotliwością, detektor fazy ślepy), **PLL** (prowadzenie fazą) i **LOCK**.

| Komenda | Opis |
|---------|------|
| `LG [v]` | Wzmocnienie. **0 = auto**, wyliczane z kalibracji `CT`; wartość niezerowa ustawia skalę ręczną |
| `LD [v]` | Tłumienie |
| `LTC [s]` | Stała czasowa pętli, 1..600 s |
| `LFD [n]` | Dzielnik filtra — stała filtra wstępnego to `LTC / n` |
| `LTO [adc]` | Offset TIC: cel fazowy w jednostkach ADC |
| `LPL [ns]` | Granica fazy locka — szerokość okna |
| `LPF [n]` | Współczynnik locka: okno musi się utrzymać przez `LPF × LTC` sekund |
| `LTK [v]` | Feed-forward współczynnika temperaturowego (0 = wyłączony) |
| `LTR [adc]` | Odniesienie temperatury, jednostki ADC |

Żadna z nich nie zapisuje się automatycznie; `ES LTIC` zapisuje je razem z
parametrami algorytmu 10.

### Okno uśredniania członu tłumiącego — `FA` / `FAD` / `FAL`

| Komenda | Opis |
|---------|------|
| `FA [n]` | Ustawia okno uśredniania członu tłumiącego w **obu** stanach LTIC (10/100/1000 s) |
| `FAD [n]` | Tylko stan DPLL |
| `FAL [n]` | Tylko stan LOCK |

100 to wartość historyczna i niczego nie zmienia. Krótsze okno jest kandydatem
na naprawę cyklu granicznego, którego okres wynosi kilka długości uśredniania —
opóźnienie grupowe długiej średniej może wypaść blisko kwadratury z odpowiedzią
pętli.


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

### Wejście fazy LTIC (Lars' TIC)

Przy włączonym `GPSDO_LTIC` firmware odczytuje sprzętowy licznik interwału
czasu (TIC Larsa Waleniusa): kondensator 1 nF jest ładowany stałym prądem w
interwale GPS-1PPS → OCXO-1PPS, a zatrzaśnięte napięcie na PA1 jest próbkowane
na szczycie rampy ~50 µs po zboczu PPS; aktywne rozładowanie nie jest potrzebne
— dioda odcina, a upływ ~25 ms czyści kondensator przed kolejnym impulsem 1 Hz.
Napięcie to bezpośrednia,
wysokorozdzielcza miara różnicy faz między oboma impulsami — znacznie
dokładniejsza niż licznik cykli TIM2 używany przez algorytmy częstotliwościowe
(3–9).

Pętla sterowania **dyscyplinuje OCXO bezpośrednio z tej fazy** przez algorytm 10
(`LA 10`) — trójstopniowa pętla ACQ → DPLL → LOCK opisana niżej. Faza pojawia
się w raporcie serial (`Vphase:` i `dph:` w ns), jako wiersz `Vph:`/`dph:` na
TFT oraz jako pozycja `LTIC phase (PA1)` w checkliście startowej. Po kalibracji
przez `LC` faza jest raportowana w nanosekundach względem skalibrowanego
`zero_offset`, przy użyciu zmierzonego `ns_per_volt`; przed kalibracją
pokazywane są tylko wolty. (Stała kompilacyjna `LTIC_NS_PER_VOLT` w
`gpsdo_config.h` to przestarzały fallback i normalnie zostaje 0 — `LC` mierzy
realne nachylenie per płytka i zapisuje je w parametrach żywych.)

---

## Zapis ustawień (flash ring)

Ustawienia mieszkają w pierścieniu z wyrównywaniem zużycia we flashu, w
**sektorze 7** (0x08060000, 128 KB) — ostatnim, żeby firmware zachowało
maksymalną ciągłą przestrzeń poniżej. Nie ma żadnego EEPROM-u, emulowanego ani
innego.

Rekordy są typowane, więc blok ustawień i dane wyuczone dzielą jeden pierścień
bez kolizji. Każdy slot niesie CRC16, sloty są numerowane sekwencyjnie i wygrywa
najnowszy poprawny, a każdy zapis jest odczytywany z powrotem i weryfikowany.
Zanik zasilania w trakcie zapisu zostawia więc poprzednie ustawienia nietknięte,
a nie uszkodzony półrekord.

Blok ustawień trzyma PWM i numer algorytmu, zestawy PID dla algorytmów 3-9,
kalibrację LTIC i wzmocnienia stanów, parametry LTIC-Lars, okna tłumienia,
offsety czujników, flagi startu oraz strefę czasową. Jest wersjonowany: blok
zapisany przez starsze firmware o innym układzie zostaje odrzucony zamiast
błędnie odczytany, a płytka wstaje na wartościach domyślnych.

| Komenda | Działanie |
|---------|-----------|
| `ES [grupa]` | Zapisz wszystko albo jedną grupę: `TZ` / `PID` / `LTIC` / `FLAGS` / `ALGO` / `PO` |
| `ER` | Odczytaj ustawienia z pierścienia |
| `EE` | Skasuj ustawienia — powrót do domyślnych |
| `EW` | Zużycie flasha: cykle kasowania i użyte sloty |
| `CR YES` | Zimny restart: wyczyszczenie całego pierścienia |

Preferencje zapisują się same; strojenie pętli wymaga jawnego `ES`. W obu
przypadkach odpowiedź mówi, co zastosowano — patrz uwagi o trwałości przy sekcji CLI.

### Równoważenie zużycia Flasha (dane żywe)

Dane “żywe” — nauczony dryf/tłumienie (`LRN`), kalibracja LC i ostatni PWM —
zmieniają się znacznie częściej niż ustawienia, więc są przechowywane osobno
razem z blokiem ustawień, w **buforze pierścieniowym z równoważeniem zużycia**
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
Dane żywe trafiają do pierścienia razem z ustawieniami; `ES` zapisuje je jako
fallback.

### Zachowanie danych żywych przy ponownym wgrywaniu firmware

- **Bootloader / DFU / Arduino IDE** dotyka tylko sektorów firmware (0–5);
  pierścień w sektorze 7 przetrwa.
- **Pełne kasowanie układu J-Link/ST-Link** czyści wszystko. By zachować
  kalibrację i uczenie, kasuj tylko sektory 0–5:
  `erase 0x08000000 0x0803FFFF`, potem `loadbin firmware.bin 0x08000000`.
- Jeśli bufor zostanie wyczyszczony, firmware uczy się/kalibruje od nowa — nic
  się nie psuje, tracisz tylko nagromadzone dostrojenie.

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
`ES`, by zapisać nastawy do flash ringu. W przeciwieństwie do auto-strojenia
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

## Algorytm 12 — akumulator wielopoziomowy

Wg konstrukcji Alana Cashina (MIS42N z forum EEVblog). Każda inna pętla tutaj ma
jedną stałą czasową, a ta jest kompromisem, którego nikt nie wygrywa: zmierzone
względem wzorca rubidowego, `LTC 60` jest do 1,58× lepsze powyżej 800 s, a
`LTC 240` do 1,44× lepsze między 10 a 400 s. Trzeba wybrać jedno.

Ten algorytm nie wybiera. Odczyty gromadzą się w poziomach — poziom n obejmuje
2^n sekund — a korekcja następuje na **najniższym** poziomie, którego błąd
przekracza granicę tego poziomu. Duży błąd działa w ciągu dwóch sekund, mały
czeka na dłuższe uśrednienie. Nie ma `LTC` do ustawienia.

Poziomy wynikają z układu bitów licznika sekund, a nie z tablicy buforów: przegląd
od najmłodszego bitu w górę, zatrzymanie na pierwszym zerze. Jedenaście poziomów —
od 2 s do 2048 s — kosztuje 22 bajty.

### Co mierzy

**Fazę w nanosekundach, z detektora LTIC.** Pierwsza wersja karmiła algorytm
błędem zliczeń TIM2 w całych hercach i była ślepa: zdyscyplinowany oscylator siedzi
daleko poniżej 1 Hz, więc pole czytało zero w 83% i 95% próbek w dwóch przebiegach,
a algorytm nigdy nie korygował. Faza się całkuje tam, gdzie sekundowy pomiar
częstotliwości nie — błąd 10⁻¹¹ jest niewidoczny w zliczeniach, ale po 2500 s daje
25 ns fazy.

**Detektor jest wymagany.** Wcześniejsza wersja miała gałąź awaryjną całkującą
błąd zliczeń na płytkach bez detektora; nie odtwarzała ona fazy, tylko gromadziła
błądzenie losowe szumu kwantyzacji, które przekraczało granicę poziomu, wyzwalało
korekcję, uderzało w ogranicznik i wyrzucało detektor na szynę — zmierzone jako
6000 kroków wahnięcia PWM w 148 korekcjach, z detektorem na szynie przez 58% czasu.
`LA 12` odmawia teraz bez `GPSDO_LTIC`, a gdy detektor jest, ale nie czyta,
algorytm wstrzymuje się zamiast zgadywać.

Tak jak pozostałe algorytmy LTIC, zbroi picDIV. Bez tego rampa detektora stoi na szynie,
odczyt nigdy nie staje się poprawny, a algorytm po cichu wraca do bycia ślepym.
Pomost czeka na kilka kolejnych nieudanych odczytów, zanim ruszy dzielnik, potem
robi przerwę na ustalenie fazy. Trend pokazuje wtedy `ARM`.

### Progi wyliczane z pomiaru szumu

Granice poziomów były najpierw wzięte z tablicy Alana i przeskalowane stosunkiem
kroków licznika. To zła wielkość do skalowania: próg musi przekraczać **szum**
pomiaru fazy — detektora, sawtootha i wahania GPS razem — a ten różni się między
konstrukcjami z powodów, których rozmiar kroku nie oddaje.

Zmierzone na jednej płytce: średnia fazy −1 ns przy odchyleniu standardowym
462 ns. Oscylator był ustawiony poprawnie, a całe to rozrzucenie to szum — podczas
gdy próg poziomu 0 wynosił 462 ns i przekraczało go 41% próbek. Efekt: 620 korekcji
w 1685 sekundach, hierarchia resetowana co 2,7 s i nigdy nieosiągająca poziomu 2.

Firmware szacuje więc szum na bieżąco i wylicza z niego próg każdego poziomu. Próg
dotyczy wyrażenia testowego `|3b − a|`, którego odchylenie wynosi `σ·√(2^L)·√10` —
nie średniej fazy, dla której byłoby to `σ/√N`. Sześć sigma na właściwej wielkości
daje odstęp między korekcjami rzędu minuty.

`ML` raportuje zmierzony szum i to, czy granice za nim podążają; telemetria niesie
go jako `sig=`. Ustawienie `MG` powyżej zera zatrzymuje autotuning i zachowuje
tablicę zapisaną ręcznie.

### Komendy

| Komenda | Działanie |
|---------|-----------|
| `LA 12` | Wybór algorytmu |
| `MG [v]` | Wzmocnienie w LSB na ns. 0 = wylicz z kalibracji `CT` i strój progi automatycznie |
| `MR [n]` | Wymuś korekcję po osiągnięciu poziomu n, niezależnie od granic |
| `MLP <n> [ns]` | Granica fazy dla poziomu n; bez wartości wypisuje bieżącą |
| `MF [0-3]` | Skąd biorą się limity, niezależnie od `MG`: 0 śledź `MG`, 1 tablica zapisana, 2 formuła sigma, 3 mierzone |
| `MFT [s]` | Tylko `MF 3`: docelowy odstęp w sekundach między korekcjami wywołanymi samym szumem (domyślnie 3600) |
| `ML` | Lista wzmocnienia, poziomu wymuszenia, zmierzonego szumu i wszystkich granic |
| `ES ALGO12` | Zapis bloku do flash ringu |

### Niedostrojony — i dlaczego

Tylko **jedna** granica została kiedykolwiek wyprowadzona. Specyfikacja Alana
mówiła 10 MHz ±0,01 Hz, co daje 125 ns fazy w 128 sekundach — i to ustala wpis dla
128 s. Mógł użyć 64 ns przy 64 s, ale tani moduł ze słabym sygnałem błądzi ±100 ns
i generowałby fałszywe błędy. O reszcie mówi wprost: *„działają przez większość
czasu, ale nie były w żaden sposób optymalizowane"*.

Nie ma więc czego wiernie odtwarzać poza tą jedną kotwicą — stąd autotuning i stąd
cała tablica pozostaje edytowalna i zapisywana per płytka.

---

## Samoocena bez wzorca — `CS`

Algorytm 11 został zweryfikowany względem wzorca rubidowego na cudzym stanowisku.
Prawie nikt, kto zbuduje to urządzenie, takiego nie ma — a bez niego zostaje słowo
autora i zielony prostokąt. `CS` daje coś lepszego.

Korekcja, którą stosuje pętla, jest błędem, który przed chwilą zaobserwowała, więc
wielkość tych korekcji mówi, czy dyscyplinowanie działa — a odniesieniem jest GPS,
więc nie ma nic lepszego do porównania częstotliwości. Firmware i tak liczyło
wszystkie te wartości i je wyrzucało. Pomysł jest Alana (MIS42N z forum EEVblog),
którego własna konstrukcja opiera się dokładnie na tym i dlatego nie potrzebuje
wzorca wtórnego.

`CS` podaje RMS korekcji na oknach ostatnich 100, 1 000, 10 000 i 100 000
korekcji — w jednostkach DAC oraz, gdy `CT` zmierzyło nachylenie oscylatora, we
względnej częstotliwości, co da się wprost porównać z liczbą z ADEV. Podaje też
stałe odchylenie, niezerowe wtedy, gdy pętla nadąża za rzeczywistym dryfem, a nie
za szumem.

**Okna liczą korekcje, nie sekundy**, bo tempo korekcji zależy od algorytmu:
algorytm 11 steruje raz na sekundę, algorytm 10 raz na `LIV`. Oznaczenie ich w
minutach znaczyłoby co innego przy jednym algorytmie i sześćdziesiąt razy tyle
przy drugim. `CS` mierzy rzeczywisty odstęp i wypisuje, ile okna obejmują w
czasie rzeczywistym, żeby czytelnik nie musiał tego przeliczać — przy jednej
korekcji na sekundę 100 000 pokrywa około 28 godzin.

To wagi wykładnicze, nie ostre okna: około 63% wagi mieści się w N korekcjach, a
95% w 3N, więc stare dane blakną, zamiast wypadać. Kosztuje to cztery mnożenia z
dodawaniem na korekcję i zero pamięci, podczas gdy bufor na 100 000 próbek zająłby
większość dostępnego RAM-u, odpowiadając na to samo pytanie nie lepiej.

**Liczone wyłącznie, gdy pętla jest zalockowana i nie trwa kalibracja.** Rampa
akwizycji, trzy skoki, które `CT` wykonuje mierząc nachylenie VCO, oraz
przemiatanie `LC` to komendy, nie korekcje; jedna taka zdominowałaby średnią
godzinną długo po tym, jak się skończyła. Algorytmy 0-9 nie mają stanu locka, na
którym dałoby się oprzeć bramkę, więc są wykluczone, a `CS` mówi o tym wprost
zamiast podawać liczbę bez określonego znaczenia.

> **Czego to nie mówi.** Mierzy, czy PĘTLA JEST USTALONA, a nie czy WYJŚCIE JEST
> DOBRE — a to jest to samo tylko wtedy, gdy detektor fazy jest wiarygodny.
> Zaszumiony detektor sprawia, że pętla goni szum: korekcje rosną, `CS` wiernie je
> raportuje, a oscylator był w porządku, dopóki pętla go nie popsuła. Nic
> mierzonego wewnątrz pętli tego nie zobaczy. Małą wartość czytaj jako „pętla z
> niczym nie walczy" — konieczne, ale niewystarczające. Sygnałem użytecznym jest
> wartość rosnąca.

---

## 24-bitowe napięcie sterujące — PWM z ditherem (`GPSDO_PWM_DITHER`)

Pomysł jest Alana Cashina (MIS42N na EEVBlog), z jego Budget GPSDO: puść PWM na
mniejszej liczbie bitów, niż potrzebujesz, i zmieniaj wypełnienie z okresu na
okres tak, by brakujące bity niosła **średnia**.

**Zyskiem jest nośna, a nie dodatkowe bity.** Tętnienie trzeba odfiltrować poniżej
jednego kroku wyjścia, a jak trudne to jest, zależy od tego, jak wysoko nośna
siedzi nad zakresem filtru:

| | nośna | zakres filtru | stała czasowa |
|---|---|---|---|
| PWM 16-bitowy | 2 kHz | 0,7 Hz | 230 ms |
| dither 13-bitowy (domyślny) | 12,2 kHz | 4,2 Hz | 38 ms |
| dither 12-bitowy | 24,4 kHz | 8,4 Hz | 19 ms |

Opóźnienie filtru wchodzi do pętli wprost jako przesunięcie fazy, więc
sześciokrotnie krótszy filtr jest wart więcej niż sama rozdzielczość. Ta dochodzi
za darmo.

**Jak to działa tutaj.** Alan ditheruje w przerwaniu timera, bo PIC nie ma DMA. Na
tym układzie byłoby to 12 000 przerwań na sekundę konkurujących z przechwytem
1PPS — jedynym przerwaniem w tym firmware, którego nie wolno opóźniać. Ale wzór
ditheru dla stałej wartości jest okresowy: powtarza się co 2^(24−N) okresów. Jest
więc liczony raz do tablicy i odtwarzany przez DMA do rejestru porównania, a CPU
między zmianami wartości nie dotyka niczego.

Średnia jest **dokładna**, nie przybliżona: tablica trzyma dokładnie Y wpisów o
wartości X+1 wśród 2^(24−N), więc uśrednia się do X + Y/2^(24−N), czyli z
konstrukcji do wartości 24-bitowej. Odtwarzanie jej nie może dryfować.

| `GPSDO_PWM_DITHER_BITS` | tablica | RAM (dwa bufory) | nośna | CPU |
|---|---|---|---|---|
| 12 | 4096 wpisów | 16 KB | 24,4 kHz | 0,025% |
| 13 (domyślnie) | 2048 wpisów | 8 KB | 12,2 kHz | 0,012% |

Ten sam pin co zwykły PWM — **PB9, TIM4 CH4** — więc dotychczasowy filtr i
okablowanie zostają bez zmian. TIM4_UP steruje DMA1 Stream 6 Channel 2; takt 2 Hz
jest na TIM9, a łańcuch 1PPS na TIM2/TIM3, więc nic innego nie jest ruszane. Dwa
bufory w sprzętowym trybie double-buffer sprawiają, że zmiana wartości nigdy nie
daje glitcha na pinie — i oba są przepisywane przy każdym zapisie: wypełnienie
tylko jednego zostawia drugi odtwarzający poprzedni kod aż do następnego zapisu,
co daje na wyjściu falę prostokątną o częstotliwości około 3 Hz.

**Włączone** w wysyłanym `gpsdo_config.h` od v1.05 — w v1.04 było domyślnie
wyłączone, dopóki ścieżka wyjściowa nie została sprawdzona. Zakomentowanie
`GPSDO_PWM_DITHER` wraca do zwykłego PWM 16-bitowego; pin, filtr i okablowanie są
w obu przypadkach te same, więc poza płytką nic się nie zmienia. Wymaga
`configUSE_MUTEXES` i `INCLUDE_xTaskGetSchedulerState`, które ten projekt ustawia
teraz jawnie. Wzajemnie wyklucza się z `GPSDO_DAC_EXT` — włączenie obu jest
błędem kompilacji, bo oba sterują napięciem sterującym.

### Co pętla robi z dolnymi 8 bitami

Od v1.05 pętla sterowania zachowuje ułamek swojej korekcji, zamiast go obcinać.
Na zmierzonym tutaj obiekcie jeden krok 16-bitowy to około 320 µHz — 3,2e-11 z
10 MHz, grubiej niż 4e-12, które pętla trzyma przez 10 000 s, a dochodziła tam
polując między sąsiednimi kodami. Z zachowanym ułamkiem krok wynosi **1,25e-13**
i polowanie znika.

Ułamek należy do warstwy DAC, nie do pętli. Wartość sterująca zapisywana jest z
21 miejsc, a dwadzieścia z nich — przemiatania `CT` i `LC`, rampy akwizycji,
sterowanie w holdoverze, `SP` — jest zgrubnych z rozmysłem i każde z nich kasuje
ułamek już przez samo wywołanie `gpsdo_dac_write16()`. Powyżej tej warstwy nie
zmieniło się nic: wyświetlacze, linia telemetrii, flash ring i blok ustawień
nadal widzą zwykły kod 16-bitowy.

Komenda `DAC` pokazuje, która ścieżka jest wkompilowana, kod we wszystkich trzech
ujęciach oraz wielkość kroku w µHz i w częściach ułamkowych. Kod 24-bitowy
niebędący wielokrotnością 256 jest dowodem na to, że pinem steruje ścieżka
precyzyjna. Liczby kroku wymagają wzmocnienia obiektu z `CT`; bez niego komenda to
mówi, zamiast drukować liczbę z wartości domyślnej.

---

## Zewnętrzny przetwornik SPI — planowany, niezaimplementowany

`GPSDO_DAC_EXT` przełącza napięcie sterujące z 16-bitowego PWM na zewnętrzny
przetwornik SPI. Włączenie tego dzisiaj daje celowo błąd kompilacji:
`gpsdo_dac_ext.cpp` jest zaślepką bez wybranego układu.

PWM daje około 50 µV na krok przy 3,3 V, blisko 2,7×10⁻¹¹ względnie na
oscylatorze 5,3 Hz/V. Układ 18-bitowy z odniesieniem zaprojektowanym do tego
zadania osiąga mniej więcej 17 µV, blisko 9×10⁻¹², bez opóźnienia filtru w pętli.

Sprzętowe SPI nie jest potrzebne ani dostępne — SPI1 należy do TFT, a wszystkie
piny SPI2 w tej obudowie są zajęte. To nie przeszkadza: DAC zapisuje się raz na
sekundę, więc programowe kluczowanie kosztuje rzędu mikrosekundy. Proponowane piny
to PB0, PB2 i PB4, dobrane tak, by ominąć PB6/PB7 — te wyglądają na wolne, ale są
domyślnymi pinami I2C1, które zajmuje `Wire.begin()`.

Wszystkie 23 miejsca, które dawniej zapisywały PWM, przechodzą teraz przez
`gpsdo_dac_write16()`, więc dodanie układu oznacza wypełnienie jednej funkcji, a
nie edycję 23 wywołań.

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
#define GPSDO_PWM_DITHER         // 24-bitowe napiecie sterujace (PWM z ditherem, PB9)
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

## Plany rozwoju

### Planowane

**Zewnętrzny przetwornik SPI na napięcie sterujące.** 16-bitowe PWM daje około
50 µV na krok przy 3,3 V, blisko 2,7×10⁻¹¹ względnie na oscylatorze 5,3 Hz/V.
Układ 18-bitowy z odniesieniem zaprojektowanym do tego zadania osiąga mniej
więcej 17 µV, blisko 9×10⁻¹², bez opóźnienia filtru w pętli.

Układ jest już wybrany i napisany: **AD5680**, 18 bitów, za zewnętrznym
REF5045, kluczowany programowo na CS/SCK/MOSI = PB4/PB0/PB2 na PCB Dana
Wieringa. Sprzętowe SPI nie jest potrzebne ani dostępne — przetwornik zapisuje
się raz na sekundę, więc programowe kluczowanie kosztuje mikrosekundy.
`GPSDO_DAC_EXT` go włącza i wyklucza się z `GPSDO_PWM_DITHER`, bo oba sterują
napięciem kontrolnym. Sterownik kompiluje się i linkuje dla cortex-m4 w
`tools/hostcheck`; test na sprzęcie należy do Dana. Otwarte zostaje: zakres
wyjścia względem EFC oscylatora i runtime'owy wybór ścieżki
`DAC [PWM|DITH|EXT]`, który idzie w parze z płytką ze zworkami.

Warto wiedzieć, gdzie leży użyteczna granica. Przy 24 bitach krok wynosi 197 nV;
szum termiczny 10 kΩ w paśmie 1 Hz to około 13 nV, więc szum Johnsona nie jest
przeszkodą — ale dobry wzmacniacz zero-drift wnosi około 180 nV międzyszczytowo w
paśmie 0,1–10 Hz, a precyzyjne odniesienie około 1 µV. **18 bitów to punkt
optymalny, 20 rozsądny sufit** — powyżej łańcuch analogowy nie dostarczy tego, co
obiecuje przetwornik.

### Próbowane i porzucone

Oba zostały porządnie sprawdzone i oba są tu zapisane, bo powody są bardziej
użyteczne niż same pomysły.

**24-bitowy przetwornik delta-sigma na wolnym pinie, zasilany przez DMA.**
Zbudowany, przetestowany na hoście, potem zmierzony — i w tym momencie przestał
być atrakcyjny. Komenda ma 24 bity, ale rozdzielczość docierającą do oscylatora
wyznacza to, jak długo uśrednia filtr analogowy: 13,6 bita efektywnego przy 4 096
bitach uśredniania, 19,0 przy 262 144 i 24,0 dopiero po 16,7 miliona — czyli
43 sekundach przy 390 kHz. Filtr dostatecznie szybki, by nie opóźniać pętli, daje
około 18 bitów, za cenę 6% CPU pracującego bez przerwy plus precyzyjne
odniesienie, bramka CMOS i aktywny filtr czwartego rzędu. Podniesienie
częstotliwości bitowej tego nie ratuje: 24 bity w filtrze 32 ms wymagałyby
524 MHz.

Wcześniejsza wersja tego dokumentu obiecywała poprawę 320-krotną. To było błędne
— porównywało szerokość komendy z krokiem PWM, co nie porównuje niczego. Kod
zostaje w drzewie, oznaczony jako ślepy zaułek: rdzeń modulatora jest poprawny i
przetestowany, a zmierzony ślepy zaułek jest wart więcej niż nieprzetestowany
pomysł.

**Dwa porty USB CDC**, jeden na CLI i jeden jako przezroczysty tunel GPS dla
u-center. Niemożliwe bez forka warstwy USB rdzenia: stos STM32duino jest
jednoklasowy, zgłoszenie o obsługę urządzeń złożonych jest otwarte od lat, a
rdzeń 3.0.0 nic w tej sprawie nie zmienił. Ręcznie pisane deskryptory to kilkaset
linii, które psułyby się przy każdej aktualizacji rdzenia — dokładnie to, co
spotkało TFT_eSPI na 3.0.0. Istniejący tryb równoległy z Bluetooth osiąga zresztą
ten sam skutek: `T` tuneluje GPS przez USB, a CLI zostaje na Bluetooth,
nietknięte.

### Nieplanowane

**Algorytmy 0–9 są zamrożone.** Zostają w firmware, nadal działają, a już
dostrojone dla nich ustawienia są respektowane. Ale rozwój się zatrzymał:
algorytm 11 wypadł 2–3× lepiej niż poprawnie dostrojony algorytm 10 przy czasach
uśredniania, dla których GPSDO w ogóle ma sens — na niezależnym sprzęcie,
względem wzorca rubidowego. Wysiłek idzie w algorytmy LTIC 10–12.

Zostają, a nie znikają, bo jedenaście udokumentowanych podejść do tego samego
problemu regulacji jest warte więcej jako materiał poznawczy niż schludniejsze
drzewo źródeł — i bo niczyja istniejąca konfiguracja nie powinna przestać
działać. Nowych funkcji tam nie będzie, a samouczący feed-forward (`LRN`) należy
traktować jako część tej samej zamrożonej grupy: służy wyłącznie algorytmom 3–9.

---

## Wymagania

- **Płytka**: WeAct BlackPill STM32F411CE lub F401CCU6
- **Środowisko**: Arduino IDE z rdzeniem STM32duino ≥ 2.2.0
- **Biblioteki**: STM32duino FreeRTOS, TinyGPS++, U8g2,
  Adafruit AHTX0, Adafruit BMP280, Adafruit INA219,
  hd44780 (dla LCD), TFT_eSPI (dla TFT) (ustawienia mieszkają we flash ringu układu, więc biblioteka EEPROM nie jest potrzebna)
- **Ustawienia kompilacji**: Tools → C Runtime Library → Newlib Nano + Float Printf/Scanf

### Rdzeń STM32duino 3.0.0 — jeszcze nieobsługiwany

**Buduj na rdzeniu 2.12.0 lub starszym.** Rdzeń 3.0.0 (wydany 23 lipca 2026)
niesie dwie zmiany, które jego własne notatki wydania oznaczają jako główne:
**wdrożenie ArduinoCore-API** oraz HALv2 dla serii STM32C5xx. Trzy skutki mają
tu znaczenie:

1. **Zniknęło `ltoa()`.** To rozszerzenie niestandardowe, które dostarczał
   starszy rdzeń; firmware używa go w czterech miejscach. Na 3.0.0 nie
   skompilują się i musiałyby przejść na `snprintf(buf, n, "%ld", …)`.
2. **`HardwareSerial` przestaje być klasą konkretną.** W ArduinoCore-API jest
   interfejsem abstrakcyjnym, więc `HardwareSerial Serial2(PA3, PA2)` już nie
   tworzy obiektu. Zwykłym rozwiązaniem jest typedef wybierający właściwą klasę
   zależnie od wersji rdzenia.
3. **TFT_eSPI przestaje sterować panelem.** To jest blokada. Biblioteka używa
   `SPIClass` wyłącznie do konfiguracji pinów i uruchomienia peryferium, a potem
   rozmawia ze sterownikiem przez surowe `HAL_SPI_Transmit()` na własnym
   uchwycie. Gdy warstwa SPI pod spodem się zmienia, panel może zostać bez
   poprawnej sekwencji inicjalizacji — obserwowanym objawem jest **biały ekran**,
   przy normalnie działającym CLI i telemetrii.

Punkty 1 i 2 są drobne i można je w każdej chwili uwarunkować wersją. Punkt 3
leży w TFT_eSPI, nie w tym firmware, więc 3.0.0 musi poczekać na bibliotekę.
Ponieważ 2.12.0 jest pełnym wydaniem, a nic tutaj nie wymaga 3.0.0, pozostanie
przy 2.12.0 nic nie kosztuje.

> Warto wiedzieć przy wyszukiwaniu: większość materiałów o „Arduino core 3.0.0"
> w sieci dotyczy rdzenia **ESP32** — innego projektu, którego przewodnik
> migracji 3.0 nie ma zastosowania do STM32.

---

## Licencja

Opublikowane na tych samych warunkach co oryginalny projekt André Balsy.
