# Lista zmian — GPSDO FreeRTOS

Wszystkie istotne zmiany w projekcie są udokumentowane poniżej.

Projekt: **J. M. Niewiński** — <https://github.com/jmnlabs/GPSDO_FreeRTOS>
Na podstawie **GPSDO v0.06c** autorstwa André Balsy
(<https://github.com/AndrewBCN/STM32-GPSDO>); port na FreeRTOS oraz
algorytmy 3–9 autorstwa autora, Claude AI jako asystent programowania,
projekt PCB — Scrachi (forum EEVBlog).

Sufiks `-rtos` oznacza linię portu na FreeRTOS.

---

## [v0.48-rtos]

### Dodane
- **Obsługa TFT ILI9488 480×320 SPI (`GPSDO_TFT_ILI9488`).** ⚠️ Nietestowany —
  brak panelu do testów. Istniejący ekran roboczy 320×240 ILI9341/ST7789 oraz
  animowany splash są współdzielone i automatycznie skalowane do 480×320
  podczas kompilacji: szerokość ×1.5 i wysokość ×1.33 przez niezależne makra
  `TFT_SX`/`TFT_SY` (proporcje panelu różnią się od czystego 1.5×), a fonty
  TFT_eSPI mapowane o rozmiar w górę przez `TFT_F`. Geometria zweryfikowana,
  że mieści się w panelu; jeszcze nieuruchomiony na realnym sprzęcie. Ustaw
  `ILI9488_DRIVER` + `TFT_WIDTH 320`/`TFT_HEIGHT 480` (+ `LOAD_FONT6`) w
  `User_Setup.h` biblioteki TFT_eSPI.
- **Mostek SPI→T6963C jako nowy backend wyświetlacza (`GPSDO_T6963C`).**
  ⚠️ Eksperymentalny / niesprawdzony — backend jest kompletny i kompiluje
  się, ale połączenie nie zostało jeszcze zweryfikowane na czystym sprzęcie
  (uruchamianie na długich przewodach pokazało dzwonienie i fałszywe zbocza
  CS; to samo na masterze referencyjnym → problem integralności sygnału, nie
  firmware). Domyślnie wyłączony; zostaw wyłączony do testu na krótkim
  okablowaniu point-to-point.
  Obsługuje panel PowerTip PG240128 (240×128 mono) przez zewnętrzny
  `T6963C_SPI_bridge` po SPI1, używając wysokopoziomowych komend rysowania
  (`T6963C_Bridge.h`). Wybierany w konfiguracji jak pozostałe wyświetlacze;
  wzajemnie wykluczający się z TFT (wspólne piny SPI1 / slot wyświetlacza).
  - Reużywa pinów SPI1 TFT: `SCK PA5`, `MOSI PA7`, `CS PB13`, `READY PB12`;
    zwalnia `PB15` (był TFT_RST).
  - Skondensowany układ 240×128 odzwierciedlający ekran TFT: nagłówek
    (tytuł + czas LMT), duża częstotliwość (fonty LOGISOSO), wiersz statusu,
    wiersze wartości (PWM/Vctl, INA219, czujniki) i pasek postępu survey-in.
  - Panel monochromatyczny → wskazanie koloru lock/holdover staje się
    odwróconym (wypełnionym) prostokątem wokół słowa statusu (`LOCK` /
    `HOLD` / `H-LOST` / `NOFIX`).
  - Jedna wsadowa transakcja SPI na odświeżenie (jedno oczekiwanie na READY),
    z auto-podziałem biblioteki mostka jako zabezpieczeniem; cache zmian
    per-pole pomija zbędne przerysowania.
  - Statyczny splash startowy (logo + podtytuł + checklista sprzętu); bez
    animacji fali, bo renderowanie wsadowe przez SPI byłoby kosztowne na
    małym panelu mono.

---

## [v0.47-rtos]

### Dodane
- **Komenda CLI `SV`** — włącza/wyłącza survey-in (Time Mode) na module
  czasowym w czasie pracy, zapisywana w EEPROM (bajt 143). `SV` pokazuje
  stan, `SV 0` wyłącza (pozostań w trybie nawigacji — przydatne do testów na
  biurku), `SV 1` włącza; `ES` zapisuje, stosowane przy następnym starcie.
  Domyślnie włączone na świeżym EEPROM.

### Naprawione
- **Polling survey-in nie blokuje już wyświetlaczy.** `ubx_poll_svin()`
  czekał do 1000 ms aktywnym `delay()`, głodząc rodzeństwo wysokopriorytetowego
  zadania GPS — wyświetlacze wyraźnie się opóźniały (najgorzej przy wolniej
  odpowiadającym LEA-6T). Poll używa teraz ~500 ms okna, które ustępuje przez
  `vTaskDelay()` między odczytami, więc zadanie wyświetlacza działa normalnie,
  a okno wciąż niezawodnie łapie odpowiedź TIM-SVIN modułu (latencja
  100-200 ms). Bajty NMEA widziane podczas skanowania są przekazywane do
  TinyGPS++, więc fix nie jest zakłócany. Gdy survey już odpowiedział,
  sporadyczne nieudane odczyty nie przerywają monitora; dziury w sekwencji
  `svin dur=` zniknęły.
- **Survey-in wychodzi teraz niezawodnie po spełnieniu warunków.**
  Zakończenie jest ogłaszane, gdy ALBO odbiornik oznaczy średnią pozycję
  jako ważną, ALBO spełnione są kryteria użytkownika (dokładność ≤ limit
  ORAZ czas ≥ minimum) — niektóre odbiorniki (zwłaszcza LEA-6T) osiągały
  ~0,45 m długo po minimum, ale pozostawiały survey „aktywny", więc stary
  test `valid && !active` nigdy się nie wyzwalał. Bezpiecznik wynosi teraz
  `3 × SVIN_MIN` (min. 600 s), więc wolno zbiegający się survey na słabej
  antenie ma uczciwą szansę.
- Dokładność TIM-SVIN we wczesnej fazie survey (`0xFFFFFFFF` = „brak
  oszacowania") jest ograniczana do 65535 mm zamiast się przepełniać.

### Zmienione
- **Dokładność TFT**: INA219 pokazuje teraz napięcie szyny z 3 miejscami i
  prąd z 2 miejscami; napięcie sterujące PWM (Vctl) z 3 miejscami.

### Dokumentacja
- README (EN/PL) zaznacza, że survey-in wymaga dobrej anteny zewnętrznej z
  pełnym widokiem nieba, i odnotowuje obserwację z testów, że LEA-6T jest
  czulszy niż LEA-M8T w trudnych warunkach. Oba moduły zweryfikowano —
  kończą survey-in i przechodzą w Time Mode na profesjonalnej antenie
  zewnętrznej (geodezyjnej). Poprawiono kilka nieaktualnych komentarzy w
  kodzie (rozmiar EEPROM 144 B, TIM-SVIN zamiast NAV-SVIN).

---

## [v0.46-rtos]

### Usunięte
- **Całkowicie usunięto wybór OCXO w czasie kompilacji (CTI / Vectron).**
  Komenda `CT` mierzy wzmocnienie obiektu i wylicza wszystkie współczynniki
  dla dowolnego zamontowanego oscylatora, więc definicje per-OCXO, tabele
  PID i przełącznik `DEFAULT_PWM` nie są już potrzebne. Pętla startuje od
  uniwersalnej wartości środkowej PWM (32767 ≈ 1,65 V) przed pierwszym `CT`.

### Dodane
- **Wielowariantowy start survey-in.** LEA-6T i LEA-M8T akceptują różne
  komendy Time Mode (obie zweryfikowane w u-center), więc firmware próbuje
  każdej po kolei i zatrzymuje się na pierwszym ACK: `CFG-TMODE2` 0x06 0x3D
  (LEA-M8T), a następnie klasyczny `CFG-TMODE` 0x06 0x1D (LEA-6T, u-blox 6).
  Samo dostosowuje się do obu modułów. Jeśli żaden nie zostanie
  zaakceptowany, moduł jest uznawany za już timujący i mimo to monitorowany.

### Naprawione
- **Dokładność TIM-SVIN była bezsensowna (pokazywała ~467 km).** Pole
  `meanV` to *wariancja* pozycji w mm², nie odległość — firmware bierze
  teraz jej pierwiastek, by raportować dokładność 1-sigma w mm (zweryfikowane
  względem u-center: 18113534 mm² → ~4,3 m). Czas/dokładność survey-in mają
  teraz sensowne wartości.
- **Zawieszenie startu, gdy survey-in faktycznie ruszył (LEA-M8T).** Pętla
  postępu survey-in działała wewnątrz `gpsdo_gps_init()` — przed startem
  schedulera — i używała `vTaskDelay()`, co zawiesza system, gdy wywołane
  przed `vTaskStartScheduler()`. Nie ujawniało się na LEA-6T, bo ten NAK-uje
  CFG-TMODE2 i pomijał pętlę; M8T ACK-uje, wchodził w pętlę i zamarzał
  (niebieski LED zatrzymany). Survey-in teraz tylko *startuje* w init;
  postęp jest pollowany nieblokująco z `vGpsTask` po starcie schedulera.
- **Sporadyczne zawieszenie startu / czarne wyświetlacze** — `STACK_DISPLAY`
  zwiększony z 768 do 1024 słów. Skalowanie fontów i pętla czyszczenia OLED
  sprawiły, że 768 było na granicy; bez haka wykrywającego przepełnienie
  stosu objawiało się to cichym, niedeterministycznym zawisem.
- **Moduł czasowy LEA-M8T teraz działa.** Tkwił w 3D fix nawigacyjnym
  (HDOP ≈ 1), bo firmware wysyłał mu `CFG-TMODE3`, którego jego firmware
  (TIM 1.10, PROTVER 22) nie obsługuje. u-center potwierdził, że LEA-M8T
  używa **tych samych** komunikatów `CFG-TMODE2` / `TIM-SVIN` co LEA-6T.
  Ścieżka czasowa została ujednolicona do jednej implementacji TMODE2;
  osobne opcje `GPSDO_GPS_LEA6T` / `GPSDO_GPS_LEA8T` zastąpiono jedną
  `GPSDO_GPS_TIMING`, a gałąź TMODE3 / NAV-SVIN usunięto.
- **OLED**: dolna połowa dużego napisu `GPSDO` ze splasha (rysowanego
  czcionką dwurzędową) pozostawała za zegarem LMT — przy końcu splasha ekran
  jest czyszczony, każdy rząd wymazany, czcionka 2x2 zresetowana, a cache
  rzędów unieważniony. Napisy `GPSDO` i wersja są wycentrowane; stopka
  używa `jmnlabs+Claude`.
- **LCD 20x4**: linia tytułu/wersji przesunięta w prawo (dwie spacje), by
  sufiks `-rtos` nie był ucinany.
- Poprawiono komentarz nagłówka układu EEPROM (143 bajty, było błędnie 134).

### Zmienione
- **TFT**: biała wartość częstotliwości używa czcionki o stałej szerokości
  (font 1, rozmiar 3), więc jej cyfry zachowują stałą pozycję kolumn;
  podtytuł powiększony i zmieniony na `GPS-Disciplined OCXO`; logo, podtytuł
  i animacja zbiegających się fal podniesione; checklista sprzętu pojawia
  się wolniej z pauzą wstępną, aby nie umknęły pierwsze pozycje; stopka z
  `+`. Wartości czujników (temperatura BMP/AHT, ciśnienie, wilgotność)
  pokazują teraz dwa miejsca po przecinku.

---

## [v0.45-rtos]

### Zmienione
- **Ponownie przebudowany splash TFT** jako metafora złapania fazy: napis o
  twórcach rysowany jest najpierw i pozostaje; dwie sinusoidy 2px (niebieska
  u góry, bursztynowa u dołu) startują z widocznym przesunięciem fazy i małym
  odstępem pionowym, po czym powoli się zbiegają aż się pokrywają i łączą w
  jedną zieloną falę 4px, utrzymaną ~1,8 s. Następnie pojawia się checklista
  sprzętu.
- Czytelny raport serial pokazuje teraz `HDOP:TIME` w trybie czasowym (format
  maszynowy z tabulatorami zachowuje wartość liczbową do wykresów).

### Usunięte
- Zbędne definicje `SERIAL_*_BUFFER_SIZE` w `gpsdo_config.h` (i tak nigdy nie
  docierały do rdzenia). Rozmiary buforów są wyłącznie w `build_opt.h`
  (`RX=256, TX=512`).

---

## [v0.44-rtos]

### Dodane
- **`build_opt.h`** powiększający bufory szeregowe RX/TX do 256 bajtów
  (`-DSERIAL_RX_BUFFER_SIZE=256 -DSERIAL_TX_BUFFER_SIZE=256`). STM32duino
  stosuje te flagi do całej kompilacji łącznie z rdzeniem, czego `#define`
  w szkicu nie potrafi osiągnąć. Zapobiega to gubieniu i sklejaniu zdań
  NMEA przy 38400 baud, gdy zadanie GPS zostanie na chwilę wywłaszczone
  (przyczyna zniekształconego NMEA z LEA-6T).

### Zmienione
- **Przebudowany splash powitalny TFT**: dwie sinusoidy w różnych kolorach
  (niebieska z lewej, bursztynowa z prawej) zbiegają się do środka i łączą
  w jedną zieloną falę 10 MHz — metafora synchronizmu — z logo GPSDO i
  checklistą sprzętu poniżej. Wydłużone czasy dla czytelności.

### Uwagi
- Zachowane są tylko zdania GGA + RMC (GLL/GSA/GSV/VTG wyłączone), co wraz
  z większym buforem utrzymuje ruch na magistrali z dużym zapasem.

---

## [v0.43-rtos]

### Dodane
- **Wykrywanie Time Mode / `HDOP:TIME`.** Odbiornik czasowy w trybie
  time-only utrzymuje zamrożoną, ważną pozycję, ale raportuje HDOP ≈ 99,99.
  Zamiast pokazywać tę bezsensowną liczbę, wyświetlacze pokazują teraz
  `HDOP:TIME`, gdy ważna pozycja zbiega się z nieistotnym HDOP (≥ 50,00).
  Nowa flaga `gGps.time_mode`.

### Zmienione
- **NAK survey-in obsługiwany łagodnie.** Niektóre moduły czasowe (np.
  egzemplarze z odzysku z zapisaną konfiguracją Time Mode) NAK-ują
  `CFG-TMODE2/3`. Firmware nie traktuje tego już jako błędu — zapisuje, że
  moduł może już pracować w trybie czasowym, i kontynuuje; wykrywanie Time
  Mode w czasie pracy raportuje rzeczywisty stan.
- Wydłużono czasy splasha powitalnego (TFT ~7 s, OLED/LCD ~4,5 s), aby
  ekran powitalny dało się przeczytać.

### Naprawione
- Stopka splasha OLED nie ucina już ostatniego znaku (`jmnlabs/Claude`,
  usunięto spacje wokół ukośnika, by zmieścić się w 16 kolumnach).

---

## [v0.42-rtos]

### Naprawione
- **Błąd kompilacji w kodzie survey-in** (`get_ubx_ack` wywoływane z
  class/id/timeout zamiast wskaźnika na bufor komunikatu, którego oczekuje).
  Obie gałęzie `ubx_start_survey_in` przekazują teraz bufor ramki, zgodnie
  z sygnaturą funkcji. Buildy z modułami LEA znów się kompilują.

### Uwagi
- Moduł czasowy u-blox M8 (**LEA-M8T**) to ta sama generacja co 8T i używa
  CFG-TMODE3 / NAV-SVIN — włącz dla niego `GPSDO_GPS_LEA8T`.

---

## [v0.41-rtos]

### Dodane
- **Animowany splash powitalny na TFT**: przebiegająca sinusoida 10 MHz,
  logo GPSDO oraz checklista sprzętu odtworzona z rzeczywistych flag
  detekcji (moduły pokazują `[x]` / `[ ]`), z dyskretną stopką
  `jmnlabs · with Claude (Anthropic)`. Odtwarzany raz, potem rysowany jest
  ekran roboczy.
- **Splash powitalny na OLED** (tryb znakowy, U8x8): podwójnej wielkości
  `GPSDO`, wersja, linia akcentu i stopka.
- **Splash powitalny na LCD 20x4**: czterowierszowe powitanie z tytułem,
  podtytułem i stopką.

### Naprawione
- **TFT nie aktualizował PWM / Vctl podczas kalibracji.** Wyświetlacz
  kończył działanie zaraz po narysowaniu odliczania, zamrażając siatkę
  informacji. Teraz przechodzi dalej, więc komórka PWM/Vctl aktualizuje się
  na żywo podczas `C` / `CT` — zgodnie z zachowaniem OLED.

---

## [v0.40-rtos]

### Dodane
- **Obsługa odbiorników czasowych LEA-6T / LEA-8T** (`GPSDO_GPS_LEA6T` /
  `GPSDO_GPS_LEA8T`). Na tych modułach firmware wykonuje survey-in przy
  każdym uruchomieniu (CFG-TMODE2 na 6T, CFG-TMODE3 na 8T), po czym
  odbiornik przechodzi w tryb time-only o stałej pozycji z wyraźnie
  czystszym 1PPS. Survey-in kończy się, gdy osiągnięty zostanie minimalny
  czas (`GPSDO_SVIN_MIN_SECS`, domyślnie 120 s) lub próg dokładności
  (`GPSDO_SVIN_ACC_LIMIT`, domyślnie 2000 mm).
- Postęp survey-in jest pokazywany na każdym wyświetlaczu (`SVIN nnns nnm`
  na OLED/LCD/TFT, kreski na zegarach LED), za pomocą nowego stanu
  `g_svin_*`.
- Pozycja jest nadal nadawana w NMEA przez cały czas trybu Time Mode, więc
  wyświetlanie lokalizacji i automatyczna strefa czasowa (`TO A`) działają
  dalej — w oparciu o uśrednioną, zamrożoną pozycję z survey-in.
- `CHANGELOG.md` (i ta wersja PL) są teraz dołączane do archiwum projektu.

### Uwagi
- Zachowanie modułów NEO-6M / NEO-8M jest niezmienione (gdy żadna opcja LEA
  nie jest zdefiniowana).

---

## [v0.39-rtos]

### Dodane
- Rozgrzewanie OCXO jest teraz pokazywane na każdym wyświetlaczu z odliczaniem
  na żywo (`WARMUP nnn s` na OLED/LCD/TFT, kreski na TM1637/HT16K33), w oparciu
  o nowy stan `g_warmup_active` / `g_warmup_remaining`.

---

## [v0.38-rtos]

### Naprawione
- **Dłubanie PWM w stanie ustalonym na algorytmach fazowych (4, 5, 7, 8).**
  Strefa martwa sprawdza teraz także zakumulowaną fazę, nie tylko błąd
  częstotliwości: gdy `|e| < 1 mHz` i `|faza| < 5 Hz·s` (≈500 ns), pętla
  utrzymuje PWM i pokazuje `hit`, więc zalockowany oscylator przestaje być
  szarpany szumem GPS co okres. Mały szum fazy jest trzymany; prawdziwy dryf
  nadal korygowany.
- Wszystkie algorytmy fazowe faktycznie pokazują trend `hit` po zlockowaniu;
  algorytmy FLL (3, 6) otrzymały odpowiednik trzymania locka tylko na
  częstotliwości.
- Odczyty PWM i Vctl na wyświetlaczach aktualizują się teraz na żywo
  **podczas** kalibracji `C` / `CT` (nowa funkcja `wait_secs_pwm` publikuje
  PWM i sampluje ADC Vctl co sekundę, gdy główna pętla jest zajęta).

---

## [v0.37-rtos]

### Zmienione
- `LP 8` i `LP 9` pokazują teraz, skąd te algorytmy faktycznie czytają swoje
  wzmocnienia: algo 8 (hybryda) używa `g_pid[6]` (gałąź FLL) + `g_pid[7]`
  (gałąź PLL); algo 9 (NN) używa stałych wag sieci, więc liczą się tylko
  `NS` / `IL`. Zapobiega to myleniu pustego `g_pid[8]/[9]` z „nienastrojonym"
  po `CT`.

---

## [v0.36-rtos]

### Dodane
- Postęp kalibracji pokazywany na wszystkich wyświetlaczach: odliczanie
  `CAL nnn s` w polu częstotliwości (OLED/LCD/TFT) i `CAL` na zegarach LED
  (TM1637 / HT16K33), za pomocą `g_calib_active` / `g_calib_remaining`.

---

## [v0.35-rtos]

### Dodane
- **Komenda `CT` (Calibrate & Tune).** Mierzy wzmocnienie obiektu `K` z
  trzypunktowego przemiatania PWM (1,5 / 2,0 / 2,5 V) regresją liniową,
  znajduje PWM dla dokładnie 10 MHz i wylicza współczynniki PID dla
  wszystkich algorytmów z `K` (PLL: `Kp = 0,40/K`; FLL: `Kp = 0,35/K`,
  `Ki = Kp/300`, `Kd = Kp·73`; NN: `max_step = 0,05/K`). Z kontrolą
  poprawności, nieniszcząca; `ES` zapisuje wynik.

---

## [v0.34-rtos]

### Zmienione
- **Dwuczasowe strojenie PLL pod „szybkie złapanie, łagodne pilnowanie
  fazy".** Człon dominujący działa na błąd częstotliwości (`Kp ≈ 0,4/K`) dla
  szybkiego wejścia bez przeregulowania; małe człony fazowe usuwają powolny
  dryf. Wspólny stopień wyjściowy dodaje ograniczenie szybkości narastania
  (≈12 LSB/krok dla PLL, 40 dla hybrydy) i strefę martwą blisko locka, więc
  duży nocny dryf fazy jest rozkładany na kilka okresów zamiast jednego
  wielkiego skoku PWM.

---

## [v0.33-rtos]

### Naprawione
- **Algorytm 9 (NN) uciekał w górę.** Poprzednie „wytrenowane" wagi miały duży
  bias wyjścia (≈ −0,96 przy zerowym błędzie → stałe narastanie PWM). Zastąpione
  analitycznie skonstruowaną, pozbawioną biasu, nieparzyście symetryczną siecią:
  zerowe wejście daje dokładnie zerowe wyjście.
- **Algorytmy 4 / 5 / 7 oraz gałąź PLL algo 8 dryfowały.** Używały kroczącej
  średniej okna jako namiastki fazy, która opóźniała aktualizację 10 s o
  500–1000 s i nakręcała integrator. Zastąpione prawdziwą akumulacją fazy
  (`faza += (avg10 − 10 MHz)·10 s`, dokładna liczba cykli), ze sprzężeniem
  o opóźnieniu 10 s.
- Komunikat `GPS fix acquired` odróżnia teraz pierwszy fix po starcie od
  prawdziwego odzyskania po utracie fixa.

### Dodane
- **Automatyczna strefa czasowa (`TO A`).** Czas lokalny podąża za pozycją GPS:
  kompaktowy zestaw reguł stref cywilnych Europy plus reguła DST UE, albo
  strefa słoneczna `round(lon/15)` poza Europą. `TO <n>` zachowuje tryb ręczny.
  Tryb zapisywany do EEPROM (bajt 142, razem 143 bajty) i przywracany przy
  starcie.

---

## [v0.32-rtos]

### Naprawione
- **Raport detekcji sprzętu.** Dodano odporną sondę I2C z podwójną
  weryfikacją (ACK adresu + odczyt 1 bajtu). OLED i HT16K33 były wcześniej
  raportowane jako `OK` bezwarunkowo / na zawodnym ACK; teraz zgłaszają
  rzeczywistą obecność. TM1637 i TFT oznaczone jako `enabled (write-only —
  not verifiable)`.
- **Kolor częstotliwości TFT.** Zielony kolor „zlockowany" wynika teraz z
  rzeczywistego odchylenia od 10 MHz (≤1 mHz na oknie 10000 s lub ≤10 mHz na
  1000 s), niezależnie od algorytmu — więc zalockowany algo 8 też zmienia kolor
  na zielony, a nie tylko przy rzadko emitowanym trendzie `hit`.

---

## [v0.31-rtos]

### Dodane
- **Obsługa 4-cyfrowego zegara HT16K33** (I2C 0x70): samodzielny sterownik
  (HH:MM z migającym dwukropkiem, `oooo` podczas szukania), współdzielący
  magistralę z LCD — bez dodatkowych pinów. TM1637 zachowany.
- Ujednolicony raport sprzętowy przy starcie: każde opcjonalne urządzenie
  zgłasza `OK` lub `not found` w spójnym formacie `HW:`.
- Nowy diagram architektury sprzętu w obu plikach README (TFT + HT16K33).

---

## [v0.30-rtos]

### Dodane
- **Obsługa TFT 240×320 (ILI9341 / ST7789)** przez TFT_eSPI na sprzętowym SPI1
  (SCK PA5, MOSI PA7, RES PB15, DC PB12, CS PB13). Układ poziomy: pasek
  nagłówka, duża częstotliwość z kodowaniem kolorem, dwukolumnowa siatka
  informacji, wiersz czujników i pasek statusu z kodowaniem kolorem.
  Selektywne przerysowywanie komórek minimalizuje ruch SPI. Stos DisplayTask
  podniesiony do 768 słów gdy TFT włączony. Oba sterowniki przetestowane na
  sprzęcie.

---

## [v0.29-rtos]

### Naprawione
- **Synchronizacja picDIV.** Uzbrojenie jest teraz odraczane do pojawienia się
  fixa GPS (zatrzymany divider bez 1PPS na Sync zawiesiłby się z martwym
  wyjściem); dedykowana flaga zastępuje wartownika opartego na znaczniku millis
  (odporna na przepełnienie); usunięto auto-uzbrojenie po kalibracji (pętla nie
  zbiegła jeszcze). Dodano czytelny feedback na serialu. README dokumentuje
  random-walk fazy FLL vs lock fazy PLL dla długoterminowego wyrównania 1PPS.

---

## [v0.28-rtos]

### Naprawione
- **Zakres PWM przy DAC 3,3 V.** PWM STM32 osiąga tylko 0–3,3 V z zakresu EFC
  0–4 V (82,5%), więc dostępne dostrajanie to −10…+14,75 Hz (CTI) i
  −20…+13 Hz (Vectron). Domyślny PWM skorygowany per-OCXO: 32767 (CTI, środek
  1,65 V) i 39718 (Vectron, nominał 2,0 V).

---

## [v0.27-rtos]

### Naprawione
- **Parametry Vectron C4550A1-0213.** Skorygowane do rzeczywistego punktu
  pracy: zasilanie 5 V, EFC 0–4 V, Kv = 10 Hz/V (0,504 mHz/LSB), współczynnik
  skali 1,333 vs CTI (wzmocnienia × 0,75), wspólny domyślny PWM.

### Zmienione
- `README_EN.md` przemianowany na `README.md` (domyślny dla GitHub);
  `README_PL.md` bez zmian.

---

## [v0.26-rtos]

### Dodane
- **Wybór OCXO** w `gpsdo_config.h` (`GPSDO_OCXO_CTI_OSC5A2B02` /
  `GPSDO_OCXO_VECTRON_C4550`), z domyślnymi parametrami PID i domyślnym PWM
  per-OCXO ustalanymi w czasie kompilacji. Awaryjnie używa wartości CTI, gdy
  żaden nie jest wybrany.
- `SP`, `F`, `C`, `T` udokumentowane w tekście pomocy i plikach README.

---

## [v0.25-rtos]

### Dodane
- `g_pressure_offset` (`PO`) i `g_altitude_offset` (`AO`) są teraz zapisywane
  do i przywracane z EEPROM (bajty 134–141, razem 142 bajty).
- Komenda `V` rozszerzona o pełne informacje o autorach/podziękowaniach i
  linki do GitHub.

---

## [v0.24-rtos]

### Naprawione
- **Wyjście Bluetooth.** Wszystkie komunikaty runtime przechodzą przez makro
  `OUT_SERIAL` (Serial2 gdy zdefiniowane `GPSDO_BLUETOOTH`, inaczej USB Serial).

### Dodane
- Pauza/wznowienie raportów (`RP` / `RR`) do wyciszenia strumienia danych
  podczas konfiguracji.
- Parametry PID algorytmów zapisywane do EEPROM (sygnatura `GPSD2`).
- Profesjonalna dokumentacja nagłówków we wszystkich plikach źródłowych;
  README napisane od zera (opis projektu, zasada działania sprzętu,
  architektura oprogramowania) po polsku i angielsku; URL GitHub dodany do
  każdego pliku i do banera serial.

---

## [v0.23-rtos]

### Dodane
- **Strojenie PID w czasie pracy przez CLI** — `LP`, `KP`, `KI`, `KD`, `IL`
  dla algorytmów 3–7, `BC` / `BS` dla mieszania algo 8, `NS` dla kroku sieci
  NN algo 9. Współczynniki przeniesione do globalnej tablicy `g_pid[10]`.

---

## [v0.22-rtos]

### Dodane
- Maszyna 4-stanowa żółtej LED (off / on / wolny puls = holdover ręczny /
  szybki puls = auto-holdover) oraz automatyczny holdover przy utracie fixa GPS
  ze wskaźnikami `H` / `A` na OLED i LCD.

---

## [v0.21-rtos]

### Dodane
- Zegar w wierszu 0 OLED (czas lokalny + dzień tygodnia) po splashu wersji;
  rotujący widok data/dzień w wierszu 2 LCD. Funkcje pomocnicze dnia tygodnia
  (Zeller) i przesunięcia czasu lokalnego.

---

## [v0.20-rtos]

### Zmienione
- Ujednolicone 4-znakowe ciągi trendu; skorygowane formatowanie częstotliwości
  OLED/LCD; zabezpieczenie kompilacji przed jednoczesnym LCD + TM1637;
  poprawiony URL źródła André Balsy.

---

## [v0.19-rtos]

- Pierwsza śledzona baza portu FreeRTOS: STM32F411CE BlackPill, pomiar
  częstotliwości przez TIM2 ETR + przechwytywanie 1PPS TIM3, uśrednianie w
  buforze pierścieniowym, pętla dyscyplinująca PWM-DAC, parsowanie GPS/NMEA,
  wyświetlacze OLED / LCD / TM1637, opcjonalne czujniki AHT/BMP/INA oraz
  początkowe algorytmy sterowania.
