# GPSDO FreeRTOS v0.24

Firmware czasu rzeczywistego (FreeRTOS) dla oscylatora sterowanego GPS (GPSDO)
na platformie STM32 BlackPill (WeAct F411CE / F401CCU6).

## Autorzy i podziękowania

| Rola | Osoba / źródło |
|------|----------------|
| Autor portu FreeRTOS i algorytmów 3–9 | **J. M. Niewiński** — [repozytorium](https://github.com/jmnlabs/GPSDO_FreeRTOS) |
| Asystent programowania (Anthropic) | **Claude AI** |
| Autor oryginalnego firmware v0.06c | **André Balsa** — [repozytorium](https://github.com/AndrewBCN/STM32-GPSDO/tree/main/software/GPSDO_V006c) |
| Projekt PCB (prototyp) | **Scrachi** (forum EEVBlog) — [post z plikami](https://www.eevblog.com/forum/projects/yet-another-diy-gpsdo-yes-another-one/825/) · [profil](https://www.eevblog.com/forum/profile/?u=762266) |
| Wątek projektowy | [Yet another DIY GPSDO](https://www.eevblog.com/forum/projects/yet-another-diy-gpsdo-yes-another-one/) — EEVBlog Forum |

Firmware został napisany od podstaw jako port oryginalnego kodu André Balsa
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
               ┌─────────────┐      ┌──────────────┐
   Antena GPS ─┤  u-blox     │      │    OCXO      ├── TIM2 ETR (PA15) ──┐
               │  NEO-6M/7M  │      │  10 MHz      │                     │
               └──┬──────┬───┘      └──────▲───────┘                     │
                  │      │                 │                              │
        NMEA      │  1PPS (PB10)     PWM (PB9)                           │
     (Serial1)    │      │           + filtr RC                          │
                  │      │                 │                              │
               ┌──▼──────▼─────────────────┴──────┐                      │
               │          STM32 F411CE             │◄─────────────────────┘
               │          BlackPill                │
               └──────┬───────────┬───────┬───────┘
                      │           │       │
                    I2C bus    Serial    GPIO
                      │           │       │
                ┌─────┼─────┐     │    TM1637
                │     │     │     │    (zegar)
               OLED  Czuj- LCD   BT
              128x64 niki  20x4  HC-06
                     │
                ┌────┼────┐
               AHT  BMP  INA
               20   280  219
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
| Niski | `vDisplayTask` | 2 KB | OLED, LCD, TM1637, raport serial, LED |
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

Firmware oferuje dziesięć algorytmów przełączanych komendą `LA n`:

| Algo | Typ | Wejście | Okres | Opis |
|------|-----|---------|-------|------|
| 0 | Krokowy | avg100/1k | ~429 s | Domyślny — prosty, odporny |
| 1 | Drift | — | 1000 s | Tylko pomiar dryfu OCXO |
| 2 | Losowy | — | 5 s | Pomiar szumu — diagnostyczny |
| 3 | FLL PID | avg100 | 100 s | Ogólnego przeznaczenia, konserwatywny |
| 4 | PLL PI | phase10k | 10 s | Niski szum fazowy, wolne wciąganie |
| 5 | PLL PID | phase1k | 10 s | Zbalansowany: szybkość + szum |
| 6 | FLL PID (GA) | avg100 | 100 s | Współczynniki zoptymalizowane genetycznie |
| 7 | PLL PID (GA) | phase1k | 10 s | Współczynniki zoptymalizowane genetycznie |
| 8 | Hybrid | FLL+PLL | 100 s | Automatyczne przejście FLL↔PLL sigmoidą |
| 9 | Sieć neuronowa | e/∫e/de | 10 s | Eksperymentalny — jednowarstwowy perceptron |

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
Komendy zakończone `\r\n` lub `\n`.

### Ogólne

| Komenda | Opis |
|---------|------|
| `H` | Wyświetl pomoc |
| `RH` | Tryb raportowania: czytelny (domyślny) |
| `RD` | Tryb raportowania: rozdzielany tabulatorem |
| `RP` | Wstrzymaj raporty serial/BT |
| `RR` | Wznów raporty serial/BT |
| `SW` | Diagnostyka stosów FreeRTOS |

### Sterowanie

| Komenda | Opis |
|---------|------|
| `MH` | Włącz tryb holdover (ręczny) |
| `MD` | Włącz tryb dyscyplinowany |
| `LA [0-9]` | Wybierz / pokaż algorytm sterowania |
| `AP` | Włącz sekwencję picDIV |

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
| `TO [n]` | Pokaż / ustaw przesunięcie czasu lokalnego (h) |
| `PO [f]` | Pokaż / ustaw offset ciśnienia |
| `AO [f]` | Pokaż / ustaw offset wysokości |

### EEPROM

| Komenda | Opis |
|---------|------|
| `ES` | Zapisz parametry do EEPROM (PWM, algo, czas, PID) |
| `ER` | Odczytaj parametry z EEPROM |
| `EE` | Wymaż EEPROM (przywróć domyślne) |

---

## EEPROM

EEPROM (emulowane w pamięci Flash STM32) przechowuje 134 bajty:

| Adres | Rozmiar | Zawartość |
|-------|---------|-----------|
| 0–5 | 6 B | Sygnatura `"GPSD2"` |
| 6–7 | 2 B | PWM DAC (big-endian) |
| 8 | 1 B | Numer algorytmu (0–9) |
| 9 | 1 B | Przesunięcie czasu (±23 h) |
| 10–121 | 112 B | PID: g_pid[3..9] × {Kp, Ki, Kd, I_LIMIT} |
| 122–133 | 12 B | g_blend_crossover, g_blend_scale, g_nn_max_step |

---

## Konfiguracja kompilacji

Plik `gpsdo_config.h` steruje konfiguracją. Najważniejsze przełączniki:

```c
// Wyświetlacze — odkomentuj potrzebne:
#define GPSDO_OLED_SSD1309       // lub SH1106, SSD1306
#define GPSDO_LCD_20x4           // HD44780 20x4 I2C
#define GPSDO_TM1637_6           // 6-cyfrowy TM1637 (HH:MM:SS)

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

---

## Wymagania

- **Płytka**: WeAct BlackPill STM32F411CE lub F401CCU6
- **Środowisko**: Arduino IDE z rdzeniem STM32duino ≥ 2.2.0
- **Biblioteki**: STM32duino FreeRTOS, TinyGPS++, U8g2,
  Adafruit AHTX0, Adafruit BMP280, Adafruit INA219,
  hd44780 (dla LCD), EEPROM (STM32)
- **Ustawienia kompilacji**: Tools → C Runtime Library → Newlib Nano + Float Printf/Scanf
  USB support CDC → (generic Serial supersede U(S)ART)

---

## Licencja

Opublikowane na tych samych warunkach co oryginalny projekt André Balsa.
