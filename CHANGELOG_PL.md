# Lista zmian — GPSDO FreeRTOS

Wszystkie istotne zmiany w projekcie są udokumentowane poniżej.

Projekt: **J. M. Niewiński** — <https://github.com/jmnlabs/GPSDO_FreeRTOS>
Na podstawie **GPSDO v0.06c** autorstwa André Balsy
(<https://github.com/AndrewBCN/STM32-GPSDO>); port na FreeRTOS oraz
algorytmy 3–9 autorstwa autora, Claude AI jako asystent programowania,
projekt PCB — Scrachi (forum EEVBlog).

Sufiks `-rtos` oznacza linię portu na FreeRTOS.

---

## [v0.91-rtos] — 2026-07-11

### Dodane
- **Kalibracja LC — zakotwiczony punkt pracy + lokalne nachylenie ns/V (Opcja D).**
  Rampowy detektor fazy jest wykładniczy (1k/1n, τ≈1 µs), więc ns/V nie jest
  stałe wzdłuż rampy, a średnia po całym przejściu (range/span) rozjeżdżała się
  o ~15–20 % między uruchomieniami — zależnie od tego, gdzie arm picDIV zaparkował
  fazę. ns/V liczone jest teraz z LOKALNEGO nachylenia dV/dt w oknie ±0.20 V wokół
  stałego punktu pracy (LTIC_ZERO_ANCHOR_V = 1.85 V). zero_offset jest
  zakotwiczone w tym punkcie — powtarzalnym środku rampy, z dala od stref martwych
  detektora zmierzonych przez Dana Wiering (spadek na diodzie Schottky + pull-down
  poniżej ~0.05 V oraz rail/zawinięcie ADC przy ~3.3 V). Jeśli przebieg nigdy nie
  przekroczy pasma kotwicy, kod wraca do dawnej średniej range/span i to sygnalizuje.

  Ustalenia z kilku logów LC o rozdzielczości 1 s:
  * Kotwica jest dokładna — kolejne przebiegi za każdym razem lądują z
    zero_offset = 1,8500 V.
  * Rozrzut ns/V między przebiegami spadł z ~15–20 % (dawna średnia range/span)
    do kilku procent. Przy obu przebiegach z TYM SAMYM rate wynosi ~2,8 %;
    resztę zdominowała kwantyzacja rate przemiatania, nie dopasowanie nachylenia —
    avg100 rozróżnia rate z ziarnem 1 ns/s, więc etykieta „−5” vs „−6” niesie
    ±0,5 ns/s i pasma ufności obu ns/V się przekrywają. Nie szkodzi to LOCK-owi:
    pętla używa dokładnie tego ns/V, które zmierzyła, w napięciu, w którym
    faktycznie pracuje.
  * Okno dopasowania poszerzono do ±0,20 V (LTIC_ANCHOR_WIN_V): więcej punktów w
    paśmie (~70 vs ~35) uśrednia szum ADC, redukując rozrzut przy tym samym rate
    z ~5,9 % przy ±0,10 V do ~2,8 %.
- **Diagnostyka LC co sekundę.** Podczas przebiegu próbkującego LC drukuje teraz
  jedną linię `t=/V=/n=` na sekundę, uwidaczniając całą rampę w logu (posłużyło do
  wyprowadzenia Opcji D).

### Naprawione
- **Raport szeregowy drukowany dwa razy na sekundę w RD/RH przy aktywnym fiksie GPS.**
  vDisplayTask jest budzony przez dwa źródła ~1 Hz — tor częstotliwości (co PPS)
  i parser GPS (co zdanie czasu) — więc przy fiksie budził się dwa razy na sekundę
  i emitował dwie linie raportu. Linia szeregowa jest teraz bramkowana zmianą
  licznika PPS, więc drukuje się dokładnie jedna na sekundę; wyświetlacz nadal
  odświeża się przy każdej notyfikacji. Zgłoszone przez Dana Wiering.
- **Pisownia nazwiska w podziękowaniach** poprawiona na „Wiering” (na prośbę autora).
- **Odbicie LOCK przy ciepłym starcie (marnowało ~1 min z ~8 min boot-to-lock).**
  Zapisany LOCK/DPLL był wznawiany, dopóki odczyt fazy był ważny (na rampie),
  nawet gdy siedział daleko od zero_offset — np. Vphase ≈2,09 V przy kotwicy
  1,85 V (~260 ns od centrum). LOCK wtedy wchodził, DPLL po minucie uznawał fazę
  za zbyt odległą i schodził aż do ACQ, więc pełna akwizycja i tak się wykonywała
  po zbędnym objeździe. Guard startowy demotuje teraz zapisany LOCK/DPLL do ACQ,
  chyba że faza jest ważna I mieści się w oknie ACQ wokół zero_offset. Zimny start
  bez zmian (stan domyślnie ACQ); naprawdę wycentrowany ciepły start nadal wznawia
  LOCK natychmiast.

---

## [v0.90-rtos]

### Dodane
- **Bufor pierścieniowy w Flashu z równoważeniem zużycia dla danych “żywych”.**
  Nauczony dryf/tłumienie, kalibracja LC i ostatni PWM są teraz auto-zapisywane
  do dedykowanego sektora Flasha (sektor 6, 0x08040000, 128 KB) jako pierścień
  32-bajtowych slotów. Każdy zapis programuje kolejny pusty slot; sektor jest
  kasowany dopiero przy zawinięciu pierścienia (raz na 4095 zapisów), więc przy
  100 zapisach/dobę Flash starczy na rzędu tysiąca lat. Każdy slot ma CRC i numer
  sekwencji; slot zapisany połowicznie (zanik zasilania) nie przechodzi CRC i
  używany jest poprzedni dobry. Nagłówek z sygnaturą i wersją formatu czyni
  firmware odpornym na pełne kasowanie układu, programowanie sektorowe, pierwszy
  start i śmieci we Flashu (obcy lub pusty sektor jest wykrywany i re-inicjowany).
- **Auto-zapis z histerezą.** Dane żywe zapisywane tylko gdy ustabilizują się na
  nowej wartości: dryf zmienił się o > 8 LSB lub tłumienie o > 0.03, ORAZ minęło
  co najmniej 20 min od ostatniego zapisu. Udana kalibracja `LC` zapisuje od razu.
- **Komenda `FR 0|1`** (zapis `ES`, domyślnie włączone) przełącza bufor w czasie
  pracy — bez flagi kompilacji, więc bez niespodzianek z cache buildu. `FR 0`
  zatrzymuje całą aktywność bufora.
- **Komenda `EW`** pokazuje diagnostykę zużycia Flasha: cykle kasowania i użyte sloty.
- **Korekcja piły (qErr) dla LTIC (`SAW 0|1`).** Odbiorniki czasowe u-blox
  generują 1PPS przez dzielenie wewnętrznego zegara, więc każdy impuls pada do
  jednego okresu zegara obok prawdziwego czasu GPS — to błąd kwantyzacji per
  impuls, który odbiornik raportuje jako `qErr` w UBX-TIM-TP. Pasywny sniffer
  parsuje ten komunikat (qErr to 32-bitowe pole pikosekundowe na tym samym
  offsecie w LEA-6T, LEA/NEO-M8T i ZED-F9T, więc jeden parser obsługuje
  wszystkie), a tor fazy TIC go odejmuje, usuwając piłę granularności odbiornika
  i zostawiając własny błąd OCXO. Na LEA-6T (granularność 21 ns) to dominujący
  krótkookresowy składnik fazy. TIM-TP włączane automatycznie przy inicjalizacji
  GPS; `SAW` przełącza korekcję (zapis przez `ES`, domyślnie wyłączona) i
  pokazuje qErr na żywo.

### Zmienione
- **`ES` nie nadpisuje już nauczonych/skalibrowanych wartości gdy bufor jest włączony.**
  Przy `FR 1` kalibracja (ns_per_volt, zero_offset, range_ns, centre_v) oraz
  nauczony dryf/tłumienie należą wyłącznie do bufora; `ES` zapisuje tylko
  prawdziwe ustawienia (nastawy PID, progi, flagi). Przy `FR 0` `ES` nadal
  zapisuje te wartości żywe do EEPROM jako fallback, a `eeprom_recall()` ładuje
  je przy starcie, więc migracja starszego EEPROM zachowuje kalibrację.

### Naprawione
- **`LA 10`: brzydki rozruch — persystowany LOCK przy nasyceniu.** Po restarcie
  alg 10 czytał `g_ltic.state=LOCK` z EEPROM i startował prosto w LOCK, ale OCXO
  zdążył termicznie dryfnąć, więc detektor startował nasycony (Vphase 3,09 V).
  Saturation guard oznaczał fazę invalid → hold PWM → utknięcie w nasyceniu ~6 min
  aż DPLL→ACQ w końcu przejął. Naprawa: na PIERWSZYM wywołaniu po boocie
  (`prev_state == 0xFF`), jeśli persystowany stan to LOCK/DPLL ale faza invalid,
  demote do ACQ — pełny pull-in + re-arm picDIV od razu.
- **`LA 10`: limit-cycle ~370 s w LOCK — nasycenie detektora + windup
  integratora.** Vphase w LOCK oscylował 0,024↔0,963 V (≈pełen zakres detektora,
  23 % próbek blisko nasycenia), okres ~370–550 s — pętla sama generowała cykl,
  nie szum OCXO. Root cause: odczyt z nasycanego detektora dawał fałszywą fazę
  ~1000 ns (bo `ltic_phase_error_ns` akceptował V < 3,28 jako valid), którą
  integrator całkował → overshoot → ponowne nasycenie. Naprawa: `ltic_phase_error_ns`
  oznacza teraz odczyty poza kalibrowanym pasmem liniowym (±55 % span wokół
  zero_offset) jako invalid, a DPLL/LOCK dla `!ph_valid` freeze PHASE path
  (proporcja + całka) i zostawia tylko ścieżkę FREQUENCY (TIM2 widzi prawdziwy
  offset niezależnie od nasycenia detektora). TIM2 wciąga OCXO z powrotem w okno,
  faza staje się valid i PI wznawia.
- **`LA 10` self-learn: damp utknął na 0,5 + drift łowił cykl.** Obserwator
  limit-cycle miał stały próg amplitudy 5 ns — dla detektora HC74 (zakres 1650 ns,
  szum ADC ~50 ns) każdy cykl przekraczał próg, więc damping zawsze dekrementował
  aż do podłogi `LRN_DAMP_LO=0,5`, a feed-forward gonił oscylację zamiast ją
  gasić. Naprawa: próg skalowany do zmierzonego zakresu detektora (3 % range,
  clamp 5..150 ns), próg sign-crossing też skalowany, krok dekrementacji
  ograniczony (0,10 max), a `LRN_DAMP_LO` obniżony do 0,30. Teraz cicha pętla
  relaksuje damping z powrotem w górę, głośna — tłumi mocniej.
- **`LC` nie walczy już z pętlą dyscypliny.** Uruchomienie `LC`, gdy algorytm
  10 aktywnie dyscyplinował, pozwalało pętli ruszać PWM jednocześnie ze
  sweepem kalibracji, więc oba się nawzajem psuły — zmierzone tempo sweepu
  wychodziło ±1 ns/s, a zakres jako absurdalne wartości (1502 / 3518 ns),
  które bramka fizyki słusznie odrzucała. Pętla sterująca jest teraz wyciszana
  gdy trwa kalibracja (`g_calib_active`), więc `LC` można uruchomić w dowolnej
  chwili, także pod `LA 10`.
- **Ścieżki PWM bezpieczne podczas kalibracji.** Ten sam guard obejmuje teraz
  także sterowanie holdoverem termicznym algorytmu 9 oraz ręczne komendy PWM
  (`up1`/`up10`/`dp1`/`dp10`/`SP`), które są odrzucane z jasnym komunikatem gdy
  trwa `LC`/`CT`, więc żadna ścieżka nie zaburzy trwającego sweepu.
- **Brak zawinięcia w `LC` nie jest już traktowany jako porażka.** Detektor,
  który nie zawija w oknie sweepu, przechodzi teraz z dobrym slope/centre/span
  i jest auto-zapisywany; tylko naprawdę słaby wynik (mały span lub centre poza
  pasmem) jest sygnalizowany, z konkretnym powodem. Komunikaty nie każą już
  użytkownikowi robić `ES` po `LC` — udane `LC` auto-zapisuje do flash ringa
  (to dane żywe). `CT` nadal prosi o `ES`, bo stroi nastawy PID.

### Kredyty
- Doprecyzowano atrybucję: André Balsa jako autor v0.06c, inspiracji portu RTOS.
  Poprawiono link do repozytorium.

---

## [v0.89-rtos]

### Dodane
- **Samouczący się układ pomocniczy pętli (`LRN`), wspólny dla algorytmu 7 i
  LTIC.** Dwa wolne, pasywne uczniowie — na podstawie nocnych śladów Dana
  Wiering (wzorzec Rb): piła fazy ~9000 s ±80 ns, garb ADEV przy stałej
  czasowej pętli, dryf 8E-12/dobę: (1) **feed-forward dryfu** — estymuje
  średnie nachylenie fazy OCXO w oknach 30 s i dodaje człon PWM kasujący je,
  więc pętla przestaje gonić ruchomy cel, a faza się spłaszcza; (2)
  **adaptacja tłumienia** — obserwuje przejścia błędu fazy przez zero i
  obniża wzmocnienie przy przeregulowaniu, podnosi przy ospałości — zbijając
  garb ADEV przy stałej czasowej. Oba działają TYLKO w LOCK, aktualizują się
  co najwyżej raz na 30 s i są twardo ograniczone (feed-forward ±400 LSB,
  tłumienie 0,5–1,5), więc zła estymacja nie rozstroi pętli; żaden nie wtrąca
  pobudzenia. `LRN 1|0` włącza/wyłącza (domyślnie wł.), `LRN R` resetuje do
  teorii, samo `LRN` wypisuje stan; wartości zapisuje `ES` (EEPROM 222–230) i
  odtwarza przy starcie. Raport szeregowy pokazuje żywy wiersz `Learn:` (dryf,
  nachylenie, tłumienie, zaobserwowany okres/amplituda cyklu granicznego).
- **Uczenie obejmuje teraz każdy algorytm dyscypliny (3–10), nie tylko 7/8.**
  Jeden wrapper `lrn_apply()` podaje uczniom własny akumulator fazy i błąd
  częstotliwości każdej pętli; sieć NN (algo 9), nie mając jawnego
  akumulatora fazy, używa tylko tłumienia. Stan `LRN` jest wspólny dla
  wszystkich algorytmów.

### Interfejs / Wyświetlacz
- **Kolorowy TFT przerobiony dla czytelności i odrobiny życia.** Spójne
  formatowanie etykiet z pojedynczą spacją (`Alt: 144m`, `PWM:...`,
  `Uptime: ...`); wartości wyrównane optycznie w foncie proporcjonalnym.
  Granatowa ramka (jak nagłówek) obejmuje obszar danych, trzy separatory
  spięte bocznymi liniami. Częstotliwość zielenieje przy locku. Dodana
  etykieta `DATE:`.
- **Splash powitalny dopieszczony**: tytuł na wysokości częstotliwości, dwie
  fale oscylatorów wyłaniające się z przesunięciem fazy, schodzące się i
  zlewające w jedną zieloną falę z narastającą i zanikającą poświatą,
  a następnie przewijana lista detekcji sprzętu (okno stałej wysokości,
  kredyty nieruchome).
- **Komenda `SPL 0|1`** (zapis `ES`, domyślnie 1) przełącza animację
  powitalną. `SPL 0` pokazuje sam tytuł i kredyty przez dwie sekundy — dla
  obojętnych na sztukę.

---

## [v0.88-rtos]

### Naprawione
- **Pole częstotliwości TFT nie zostawia już fragmentów cyfr po
  komunikatach CAL/WARMUP/SVIN.** Komunikaty i duża częstotliwość mają
  różne wysokości tekstu, więc padding czyścił tylko pas bieżącego
  fontu; całe pole jest teraz czyszczone przy każdej zmianie trybu.

### Usunięte
- **Usunięto obsługę mostka SPI→T6963C** (eksperyment): `T6963C_Bridge.h`,
  jego sekcja w tasku wyświetlaczy, blok konfiguracyjny i odwołania.

### Dokumentacja
- README (EN/PL/ES) zaktualizowane o funkcje LTIC v0.5x–v0.88 (auto-
  kalibracja LC, automatyczne wzmocnienia, medianowy tor ADC, strażnik
  ucieczki, WU, animacje LED, wiarygodny kolor locka) oraz nową sekcję o
  obsłudze kolorowych TFT: dowolny panel TFT_eSPI 320×240 lub 480×320
  z opisem podłączenia.

---

## [v0.87-rtos]

### Naprawione
- **Zero martwego czasu przed próbkowaniem — przygotowania zjadały całe
  pasmo.** ADC nadąża spokojnie (1 próbka/s ≈ 8 mV/krok przy 9 ns/s); zawiodło
  ~60 s stabilizacji i pomiarów d1/d2 między zadaniem rampy a pierwszą
  próbką. Stały offset nakłada się na df, które zapisany PWM już ma
  (zmierzone +9 ns/s na żywo), więc faza przeleciała 0,061→2,62 V przez całe
  pasmo ZANIM próbkowanie ruszyło, a fit widział samo nasycenie. LC teraz
  re-armuje picDIV (deterministyczny start od dołu), zadaje offset i zaczyna
  próbkować w ~3 s; dokładne tempo czytane jest PO przebiegu z czystego
  avg100. Jeśli nasycenie przyjdzie przed 10 punktami fitu, offset jest
  połowiony, picDIV re-armowany i przebieg powtórzony raz. Przedprzemiotowy
  pomiar d1/d2 i maszyneria adaptacyjna reduce/increase zostały usunięte —
  bramka fizyki i precyzyjne tempo po przebiegu czynią je zbędnymi.

---

## [v0.86-rtos]

### Zmienione
- **LC przeprojektowany jako pojedynczy przebieg dół→góra — bez sondowania
  kierunku, bez flipów, bez potrzeby zawinięć.** Logi z anteny dowiodły, że
  uzbrojenie picDIV parkuje fazę DETERMINISTYCZNIE ~60 ns nad punktem
  synchronizacji (Vphase ≈0,061 V po każdym re-armie), że strona ujemna
  poniżej tego punktu jest MARTWA (kolejność zboczy się odwraca, impuls
  znika — avg100 pokazywało realny dryf −3 ns/s przy stojącym napięciu),
  a strona dodatnia prowadzi całe pasmo w miękkie nasycenie. LC teraz to
  wykorzystuje: po uzbrojeniu ZADAJE dodatni przemiot ~+4 ns/s (offset ze
  zmierzonego K), próbkuje całe pasmo w jednym przebiegu, a utrzymane górne
  nasycenie traktuje jako naturalny KONIEC pomiaru, nie usterkę. Precyzyjny
  odczyt avg100 (v0.85) dokładnie skaluje ns/V. Flip kierunku w przemiocie
  i jego maszyneria restartu zostały usunięte.

---

## [v0.85-rtos]

### Naprawione
- **Odwrócenie kierunku ZADAJE teraz tempo przemiotu zamiast ufać ślepemu
  odczytowi — i faza nie parkuje już przy krawędzi pasma.** Na żywo iteracja
  flip zatrzymała się na nominalnym „−1 ns/s", które realnie było ≈0: avg10
  kwantuje po 0,1 Hz (d1=0.1000, d2=0.0000 w logu), więc poniżej 0,1 Hz odczyt
  to szum. Przy df≈0 faza siedziała tam, gdzie zostawił ją re-arm picDIV
  (Vphase 0,061 V — dolna krawędź pasma, gdzie zbyt wąskie impulsy ledwo
  ładują RC), przemiot pokrył 5 mV, a bramka fizyki musiała przerwać. Teraz,
  gdy znak odwraca się między iteracjami, LC interpoluje punkt 10 MHz P0 z
  dwóch ostatnich offsetów i ustawia rampę na P0 − 0,06 Hz·(LSB/Hz) — ZADANE
  −6 ns/s wyprowadzone ze zmierzonego K, niezależne od skwantowanego odczytu.
  Na końcu przemiotu (PWM stały przez cały czas, więc avg100 jest czyste z
  rozdzielczością 0,01 Hz) prawdziwe tempo jest odczytywane i zastępuje
  zadane przed liczeniem ns/V, więc skala dopasowania jest dokładna.

---

## [v0.84-rtos]

### Naprawione
- **Odwrócenie kierunku w przemiocie prze-mierza teraz tempo i WYMUSZA zmianę
  znaku.** Obrony v0.83 zadziałały na żywo poprawnie (miękkie nasycenie → flip
  → czysty restart → zły wynik odrzucony), ale sam flip miał dwa defekty:
  (1) ns/V z dopasowania dzieli przez phase_rate, a po flipie używane było
  tempo sprzed flipu — gwarantowana zła skala (ns/V=9,09e6 odrzucone przez
  strażnika); (2) lustrzane odbicie offsetu wokół saved_pwm nie zmienia znaku
  dryfu, gdy saved_pwm leży daleko od prawdziwego punktu 10 MHz (+70 dawało
  +0,100 Hz, −70 wciąż +0,054 Hz — strona railująca, tylko wolniej). Po flipie
  LC mierzy df od nowa, a jeśli znak się nie odwrócił, dopycha offset o
  −2·df·(LSB/Hz) ze zmierzonego K i sprawdza ponownie (≤3 iteracje); okno
  odrzucania glitchy jest przeskalowywane do nowego tempa. Symulacja na
  dokładnie tych liczbach z anteny: jedno dopchnięcie ląduje na −0,054 Hz
  (−5,4 ns/s) — strona zawijająca, idealne tempo przemiotu.

---

## [v0.83-rtos]

### Naprawione
- **`LC` nie daje się już oszukać miękkiemu nasyceniu RC.** Przebieg z szybkim
  początkowym offsetem (10 ns/s) pozwolił fazie wjechać w rejon miękkiego
  nasycenia RC (2,9-3,27 V — poniżej progu railu 3,28 V, więc „żywe"):
  dopasowanie liniowe połknęło płaskie punkty nasycenia (ns/V ×74 za duże),
  późniejsze zejście z nasycenia („skok" 2,57 V) zostało przyjęte jako
  zawinięcie, a wynik (range=209204 ns, zero_offset=1,34 V — poza pasmem
  detektora) nawet PRZESZEDŁ samospójność wolt-wolt. Trzy bramki względne do
  pasma zamykają tę klasę: (1) **bramka fizyki** — zapisany zakres nie może
  przekraczać tego, co przemiot mógł fizycznie pokryć (~tempo × okno × 1,5),
  inaczej params unchanged; (2) **końce skoku zawinięcia** muszą leżeć w
  czystym paśmie dopasowania ±50%, więc zejście z nasycenia nie jest
  zawinięciem; (3) **pomijanie miękkiego nasycenia** — gdy dopasowanie ma już
  kształt, próbki daleko poza jego pasmem są traktowane jak railowane
  (pomijane; zasilają logikę odwracania kierunku w przemiocie). Wszystkie trzy
  skalują się z obserwacji bieżącego przebiegu — detektory pełnozakresowe
  3,3 V pozostają nietknięte.

### Dodane
- **Animacja survey-in na wyświetlaczach LED.** Spinner górnego 'o' (segmenty
  A→B→G→F krążące po górnym oczku cyfry), z przesunięciem fazy na cyfrę w
  falę — wizualnie odróżnialny od dolnego 'o' fali warmup.

---

## [v0.82-rtos]

### Naprawione
- **ACQ parkował fazę pół zakresu od punktu przekazania — wieczny ACQ (1401
  cykli na żywo przy Δf≈0).** Cel przyciągania ACQ liczony był jako
  `zero_offset + span/2` — relikt sprzed v0.66, gdy zero_offset był dołem
  pasma; od tamtej pory zero_offset JEST środkiem pasma, więc pętla trzymała
  fazę w swoim „środku", a próg ACQ→DPLL (mierzony względem zero_offset) nigdy
  nie mógł być spełniony. Teraz jeden punkt prawdy: ACQ ciągnie dokładnie do
  zero_offset. Świeże `LC` kasuje też stary override `LCV` (który mógł po
  cichu przywrócić ten sam pat z EEPROM).

### Dodane
- **Animacja warmup na wyświetlaczach LED.** Podczas wygrzewania OCXO każda
  cyfra TM1637/HT16K33 pokazuje spinner małej litery 'o' z przesunięciem fazy
  na cyfrę, więc wzór wędruje przez wyświetlacz jak fala (survey-in zachowuje
  kreski).

### Uwaga
- Po aktualizacji uruchom raz `LC`: poprzednia kalibracja powstała na starym,
  10-sekundowo uśrednianym torze ADC i jej zero_offset/range są rozmyte;
  przebudowany tor z medianą paczki (v0.79) daje ostrzejszy pomiar.

---

## [v0.81-rtos]

### Naprawione
- **Naprawa kompilacji:** `p_eff` był używany przez integrator DPLL/LOCK przed
  deklaracją (v0.79/v0.80 nie kompilowały się). Blok deadband/miękkie kolano
  jest teraz liczony najpierw, więc widzą go i integrator, i człon fazowy.
- **Odliczanie kalibracji pokazuje REALNY czas całości.** Licznik restartował
  się dla każdego wewnętrznego segmentu oczekiwania (30 s, 20 s…), więc
  wyświetlacz nigdy nie odzwierciedlał całej procedury. `LC`/`CT` ładują teraz
  realistyczny total, a fazy adaptacyjne (podbicie rampy, rail-backoff,
  odwrócenie kierunku, restart przemiotu) doładowują go w trakcie; każda
  ścieżka wyjścia go zeruje.
- **Warmup OCXO przywrócony i zapisywalny.** Warmup był po cichu pomijany przy
  ważnym EEPROM — „znikał" po zapisaniu konfiguracji, a zimny OCXO był
  dyscyplinowany jeszcze w dryfie termicznym. Teraz warmup działa domyślnie
  przy każdym starcie, a wyłącza go nowa komenda `WU 0` (`WU 1` włącza; stan
  zapisuje `ES` w bajcie 221 EEPROM, świeży flash: włączony).

### Dodane
- **LED „CAL" + animacja podczas każdej kalibracji.** TM1637 i HT16K33
  pokazują CAL na pierwszych trzech cyfrach, a na czwartej animację gonionego
  segmentu (G→C→D→E) kreślącą małą literę 'o' — czytelny sygnał „pracuję".

---

## [v0.80-rtos]

### Naprawione
- **Zielony kolor częstotliwości oznacza teraz wiarygodny, BIEŻĄCY lock.** Po
  wypadnięciu LTIC z LOCK do ACQ wyświetlacz zostawał zielony, bo średnia
  1000 s wciąż pokazywała ~10 MHz — echo przeszłości, nie teraźniejszość.
  Zasady teraz: dla algorytmu 10 zieleń pochodzi WYŁĄCZNIE z żywego stanu LOCK
  pętli (bez fallbacku na średnie); dla algorytmów 0-9 kryterium długiego okna
  zostaje, ale musi być potwierdzone szybką średnią 10 s wciąż w ±50 mHz od
  10 MHz, więc utrata dyscypliny gasi zieleń w ~10 s zamiast w minutach.

---

## [v0.79-rtos]

### Naprawione
- **Przebudowany tor ADC LTIC — 10-sekundowa średnia krocząca zatruwała
  pętlę.** Stary tor brał JEDEN surowy odczyt ADC na PPS przez 10-próbkową
  (=10 s) średnią kroczącą: ~5 s opóźnienia grupowego (pętla korygowała na
  nieświeżych danych), a co gorsza napięcia sprzed i po zawinięciu mieszały się
  w fantomowe poziomy pośrednie — pętla widziała gładki dryf ~30 ns/s, który
  fizycznie nie istniał, i kopała prawdziwą fazę (kroki LOCK do 152 LSB,
  odbijanie LOCK↔DPLL). Teraz każdy slot PPS bierze paczkę 16 odczytów (~1 ms)
  i jej MEDIANĘ — bez pamięci międzysekundowej, bez lagu, bez mieszania przez
  wrap, pojedyncze glitche wypadają — plus bramka outlierów: skok >25%
  skalibrowanego zakresu musi się powtórzyć w następnym odczycie, by być
  uznany (prawdziwe zawinięcia trwają; glitche nie). Uwaga: częstsze
  odczytywanie ADC nic by nie dało — detektor ładuje kondensator raz na PPS,
  więc informacja o fazie jest z natury 1 Hz; paczka maksymalizuje jakość tej
  jednej próbki.
- **LOCK łagodny z założenia: deadband + miękkie kolano + limit kroku.**
  Wewnątrz deadbandu (zakres/40, ≥6 ns — poziom szumu ADC) błąd fazy liczy się
  jako zero, a integrator stoi; poza nim błąd narasta od zera (miękkie kolano);
  końcowy krok LOCK jest twardo ścięty na ≈4 mHz (ze zmierzonego K). Małe
  odchyłki dostają teraz proporcjonalnie małe pchnięcia zamiast kopnięć pełnym
  wzmocnieniem.

---

## [v0.78-rtos]

### Naprawione
- **Pierwszy potwierdzony LOCK na żywo z trójstanową pętlą LTIC.** Dwa
  domknięcia: (1) odczyt częstotliwości na TFT zielenieje teraz przy LTIC LOCK
  — rozpoznawał tylko dawne „hit", więc kolor czekałby aż średnie 1000/10000 s
  dojdą do mHz; (2) strażnik odczytu EEPROM odrzucał algorytm 10
  (`algo > 9 → 0`), więc zapisana konfiguracja LTIC po restarcie po cichu
  wracała do algorytmu 0 — teraz `> 10`. Z tym `ES` w pełni utrwala zestaw
  LTIC: algorytm 10, kalibracja LC i polaryzacja są zapisane, a wzmocnienia
  pętli autotune wylicza na nowo z zapisanych pomiarów przy każdym wejściu,
  więc po restarcie urządzenie wraca gotowe do locka bez żadnych ręcznych
  kroków.

---

## [v0.77-rtos]

### Naprawione
- **Przejścia stanów nie odbijają już na schodkowym odczycie detektora.** Przy
  wreszcie utrzymanej częstotliwości (−0,02 Hz) pętla wciąż ping-pongowała
  ACQ↔DPLL: ADC aktualizuje napięcie fazy schodkami, a każdy schodek dawał
  fantomowe „nachylenie" 50-100 ns/s, które wyzwalało bramki nachylenia z
  napięcia (wejście do DPLL blokowane przez 183 cykle; DPLL degradowany po 6).
  Wszystkie bramki jakości częstotliwości w przejściach używają teraz Δf z TIM2
  (odpornego na schodki) — ACQ→DPLL przy |Δf|≤0,05 Hz, DPLL→LOCK przy ≤0,03 Hz,
  degradacje przy Δf>0,30 / 0,10 Hz — a napięcie służy wyłącznie POZYCJI fazy.
  Degradacja DPLL dostała też tę samą 3-krotną persistencję, którą LOCK już
  miał, więc pojedynczy schodkowy odczyt nie degraduje. Symulacja ze schodkami:
  zero fałszywych degradacji, czysty awans do LOCK.

---

## [v0.76-rtos]

### Dodane
- **Pełne auto-strojenie LTIC — bez ręcznych współczynników.** `ltic_autotune()`
  wyprowadza KAŻDE wzmocnienie pętli z dwóch zmierzonych stałych sprzętu: K
  (Hz/LSB z CT) oraz ns/V + zakres (z LC). Pętla częstotliwości kasuje ~50% Δf
  na krok; pętla fazy ściąga z τ≈20 s; LOCK jest 4× łagodniejszy; próg ACQ staje
  się ćwiartką zmierzonego zakresu detektora. Uruchamia się automatycznie po
  każdej udanej LC i przy wejściu w algorytm 10, wypisując wyliczone wartości.

### Naprawione
- **ACQ gasi teraz błąd częstotliwości z TIM2, nie napięciowy dryf.** Schodkowy
  odczyt detektora płaszczeje przy krawędzi pasma (na żywo: faza zaparkowana na
  0,336 V przy realnym utrzymującym się offsecie −0,3 Hz i odbijaniu ACQ↔DPLL)
  — nachylenie z napięcia jest tam ślepe; TIM2 nie.
- **Polaryzacja płytki nie odwraca już toru częstotliwości.** K jest dodatnie na
  każdej płytce (+PWM → +f), więc człony częstotliwościowe nie przechodzą przez
  `pol`; robi to tylko tor fazowy (Vphase). Prowadzenie e_freq przez pol=−1
  odwracało w DPLL poprawną korekcję częstotliwości — współprzyczyna odbijania
  stanów.

---

## [v0.75-rtos]

### Naprawione
- **ACQ oscylował (wahnięcia ±1 Hz, dwukrotnie mrożony przez strażnika), gdy
  kalibracja stała się wreszcie POPRAWNA.** Wzmocnienie dryfu używało
  zgadniętego stałego mnożnika (×60), niejawnie dostrojonego do starej, źle
  wyskalowanej kalibracji; z prawdziwym ns/V liczbowy dryf urósł ~2,3× i pętla
  przekorygowywała ~1,8× na krok — podręcznikowa oscylacja z przestrzałem.
  Wzmocnienie jest teraz wyprowadzane ze ZMIERZONEJ czułości OCXO (CT zapisuje
  0,40/K w g_pid[7].Kp, więc LSB-na-Hz odzyskuje się jako Kp7/0,40) z
  tłumieniem 0,5: ~60% błędu kasowane na krok, bezwarunkowo stabilne na każdym
  egzemplarzu, bez strojenia pod płytkę. Człon częstotliwościowy DPLL (stałe
  ×1000, ~6× za słaby na tym egzemplarzu) jest skalowany ze zmierzonego K tak
  samo.

---

## [v0.74-rtos]

### Naprawione
- **Bramka jakości skoku zawinięcia — zamyka ostatnią znaną drogę, którą LC
  mogło pójść źle.** Schodkowy ADC potrafi zaraportować zawinięcie w pół kroku,
  dając CZĘŚCIOWY skok; jeden taki został przyjęty jako pełny span (0,122 V na
  detektorze ~0,33 V), co posadziło zero_offset przy dnie (0,09 V) i wysłało
  pętlę w pogoń za fałszywym środkiem, aż częstotliwość uciekła o 3 Hz. Skok
  liczy się teraz tylko, jeśli zaczyna się od żywej (nie-railowanej) próbki
  ORAZ wynosi ≥80% faktycznie zaobserwowanego pasma min–max; częściowe skoki są
  nazwane w logu, a zamiast nich używane jest obserwowane pasmo (lub cross-check
  czasowy). `zero_offset` jest teraz ZAWSZE środkiem obserwowanego pasma, nigdy
  nie pochodzi z pozycji skoku.
- **Linia werdyktu dla operatora.** LC kończy się jawnym „PASSED checks —
  review LL, then 'ES'" albo „MARGINAL result — prefer re-running LC before
  'ES'", więc słabą kalibrację trudno zapisać przez przypadek.

---

## [v0.73-rtos]

### Naprawione
- **Strażnik ucieczki przebudowany po realnej ucieczce 3 Hz do PWM 63500 —
  stary miał trzy fałszywe założenia.** (1) Jego baza zakotwiczała się na nowo
  przy każdej nie-railującej próbce, ale podczas ucieczki faza okresowo się
  ZAWIJA (chwilowo nie-railed), więc baza goniła ucieczkę i próg 6000 LSB nigdy
  nie zadziałał. Teraz baza przesuwa się tylko przy faktycznie zdrowej pętli
  (nie-railed ORAZ |Δf| < 0,25 Hz). (2) Próg w LSB milcząco zakłada czułość
  Hz/LSB danego OCXO; głównym kryterium jest teraz sam zmierzony błąd
  częstotliwości: faza railed ORAZ |Δf| > 0,5 Hz → freeze (zapasowy próg
  2000 LSB zostaje). (3) Zamrożenie kroku zostawiało nakręcający się integrator
  DPLL/LOCK, gotowy walnąć PWM po odzyskaniu — podczas freeze jest re-seedowany
  do trzymanego PWM. Test behawioralny: stary strażnik pozwolił symulowanej
  ucieczce dojść do 6,15 Hz; nowy zamraża przy 0,51 Hz.

---

## [v0.72-rtos]

### Naprawione
- **Odwrócenie kierunku następuje teraz W TRAKCIE przemiotu, tam gdzie rail
  faktycznie się ujawnia.** 8-sekundowa sonda z v0.71 nie mogła złapać złego
  kierunku: w stronę railującą faza wychodzi z okna synchronizacji dopiero po
  ~pełnym zakresie dryfu — kilkadziesiąt sekund w głąb przemiotu (sonda
  przeszła, potem 137 próbek railowało). LC liczy teraz kolejne railowane
  próbki podczas samego przemiotu; utrzymana seria (≥15 s) jest werdyktem
  kierunku: odwraca znak offsetu (lustrzanie wokół zapisanego PWM), ponownie
  uzbraja picDIV, zeruje wszystkie akumulatory i restartuje przemiot raz.
  Zweryfikowane symulacją: zła strona railuje po 40 s → flip po ~54 s → czysty
  przemiot z dobrej strony z uchwyconym skokiem pełnego zakresu. Jeśli railują
  oba kierunki, istniejący abort mostly-railed nadal to zgłosi.

---

## [v0.71-rtos]

### Naprawione
- **`LC` auto-wykrywa KIERUNEK rampy — pierwotną przyczynę każdej railującej
  kalibracji.** Porównanie wszystkich przebiegów ujawniło wzorzec: każda
  nieudana kalibracja miała dodatnie df (rampa wypychała częstotliwość powyżej
  10 MHz), a jedyna czysta (range=318) miała df ujemne. W tej rodzinie
  detektorów faza zawija się piłokształtnie tylko przy dryfie w jedną stronę; w
  drugą impuls po prostu się poszerza, aż RC przyklei się do railu 3,3 V na
  stałe. Dobry kierunek zależy od płytki, więc LC teraz go sonduje: po
  ustabilizowaniu obserwuje fazę ~8 s i jeśli jest przyklejona do railu,
  odwraca znak offsetu, ponownie uzbraja picDIV i stabilizuje ponownie
  (przerywając czysto tylko, gdy railują OBA kierunki). Adaptacyjna rampa
  zachowuje wykryty kierunek. Zweryfikowano też: algorytm 7 NIE działa podczas
  LC (kalibracja blokuje control task), więc interferencja pętli jest
  wykluczona.

---

## [v0.70-rtos]

### Zmienione
- **`LC` jest w pełni samowystarczalny: ignoruje poprzednią kalibrację.** Zgodnie
  z dobrą zasadą operatorską — kalibrujesz ponownie właśnie dlatego, że zapisane
  wartości mogą być błędne — LC nie dziedziczy już niczego z EEPROM/g_ltic: cel
  rampy, próg zawinięcia, okno glitchy i kryterium prep startują z neutralnych
  założeń, a wszystko jest mierzone od nowa. To kończy kaskadę zatruwania, w
  której jedna zła kalibracja (range=6035) źle sterowała trzema kolejnymi.
- **Pomiar zakresu z pojedynczego zawinięcia.** SKOK napięcia przy zawinięciu
  (szczyt piły → dół w jednej próbce) JEST pełnym spanem detektora, więc jedno
  zawinięcie wystarcza: range = |skok| × ns/V. Cel rampy spada do jednego
  zawinięcia w oknie, czyli znacznie łagodniejszy przemiot, który nie wypycha
  już fazy poza okno synchronizacji picDIV na rail (awaria widziana przy
  9-22 ns/s). Dwa zawinięcia, gdy zdarzą się naturalnie, nadal umożliwiają
  niezależny cross-check czasowy.
- **Kryterium prep jest uniwersalne:** czeka na ważną, nie-railującą, stabilną
  fazę — bez zakładanego napięcia środka (pasma detektorów zasadnie różnią się
  między konstrukcjami).

---

## [v0.69-rtos]

### Naprawione
- **Adaptacyjna rampa `LC` jest teraz sprzętowo-agnostyczna i samoograniczająca.**
  Log v0.68 pokazał kaskadę: zatruta poprzednia kalibracja (range_ns=6035 z
  dopasowania na szumie) ustawiła absurdalny cel tempa rampy, adaptacyjne
  zwiększanie go goniło (offset do 1120, 15 ns/s), a szybka rampa wypchnęła fazę
  całkiem poza okno synchronizacji picDIV — impuls detektora zrobił się szeroki
  i napięcie przykleiło się do railu na cały pomiar („180 railed samples").
  Trzy sprzętowo-agnostyczne obrony (nie zakłada się żadnego pasma detektora;
  różne konstrukcje mają od ~0,3 V do pełnych 3,3 V): (1) zapamiętany zakres
  tylko *kieruje* celem rampy przez szeroki anty-śmieciowy clamp (20..5000 ns);
  (2) **rail-backoff** — po każdym zwiększeniu rampy LC obserwuje ~8 s i jeśli
  faza przykleja się do railu, cofa offset o połowę, ponownie uzbraja picDIV dla
  odzyskania synchronizacji i kontynuuje z tempem, na jakie pozwala sprzęt;
  (3) **bramka samospójności** — wyniki są zapisywane tylko, jeśli zakres ÷
  nachylenie implikuje fizycznie możliwą rozpiętość napięcia (≤3,3 V), inaczej
  poprzednia kalibracja zostaje nietknięta (zła LC nie zatruje już następnej).

---

## [v0.68-rtos]

### Naprawione
- **`LC` nie produkuje już bzdur, gdy rampa trafi blisko punktu 10 MHz OCXO.**
  Offset +70 LSB może ledwo rozstroić OCXO (df=0,01 Hz → 1 ns/s), więc w oknie
  nie mogło być prawdziwego zawinięcia — a mimo to skoki odczytu (napięcie fazy
  aktualizuje się schodkowo) przekraczały próg zawinięcia i dawały fałszywe
  „2 wraps", dopasowanie na samym szumie i absurdalne wyniki
  (ns_per_volt=38615, range_ns=6035). Dodano trzy obrony: (1) **adaptacyjne
  zwiększanie rampy** — jeśli dryf jest za wolny na dwa zawinięcia w oknie,
  offset jest podwajany (limit ±4000) i ponownie stabilizowany; (2) **walidacja
  zawinięć czasem** — skok wcześniej niż ~połowa oczekiwanego czasu przejścia od
  poprzedniego zawinięcia to glitch i jest ignorowany; (3) **cross-check zakresu
  wolt/czas** — czas między dwoma zawinięciami × tempo fazy daje niezależny
  pomiar zakresu; jeśli różni się >2× od pomiaru napięciowego, nachylenie jest
  podejrzane i wygrywa zakres CZASOWY (ns/V przeskalowane do zgodności).

---

## [v0.67-rtos]

### Dodane
- **`LC` sam się przygotowuje przed rampą (wygoda operatora).** Uruchomienie `LC`
  wymagało wcześniej ręcznej sekwencji `LA 7` / `AP` / „poczekaj aż faza dojdzie
  do środka"; start z fazą przy railu był główną przyczyną słabych kalibracji.
  `LC` teraz samodzielnie: (1) uzbraja picDIV do synchronizacji z 1PPS, jeśli
  jest fix GPS, potem (2) czeka do ~60 s, aż napięcie fazy ustali się w
  centralnym pasmie detektora (środek ± ¼ zakresu, utrzymane kilka sekund) przed
  rozpoczęciem rampy. Wypisuje każdy krok i kontynuuje z jasną adnotacją, jeśli
  fazy nie da się wycentrować w czasie. Wystarczy uruchomić `LC` — bez ręcznego
  przygotowania.

---

## [v0.66-rtos]

### Naprawione
- **`LC` mierzy teraz PEŁNY zakres detektora (był ułamek, np. <75 ns).** Dwa
  błędy zaniżały `range_ns` na wąskim detektorze: (1) próg zawinięcia był
  sztywne 0,5 V — większy niż cały ~0,33 V zakres detektora — więc zawinięcia
  nigdy nie były wykrywane; (2) `range_ns` brano z małego wycinka, który faza
  akurat przemiotła podczas rampy, nie z pełnego zakresu jednoznaczności
  detektora. `LC` przemiata teraz aż zobaczy **dwa zawinięcia** (jeden pełny
  cykl), śledzi prawdziwe min/max przez zawinięcia dla zakresu, i wciąż dopasowuje
  nachylenie (ns/V) na czystym segmencie przed zawinięciem. Próg zawinięcia jest
  teraz względny do zakresu detektora. Rampa/okno przestrojone (offset 70 LSB,
  180 s), żeby zmieścił się i długi czysty segment nachylenia, i dwa zawinięcia.
  `LC` raportuje, czy zobaczył 0/1/2 zawinięcia, żebyś wiedział, czy zakres jest
  dokładny, przybliżony, czy dolnym oszacowaniem.

---

## [v0.65-rtos]

### Naprawione
- **DPLL korygował za rzadko dla wąskiego detektora (wyglądał na „zamrożony").**
  DPLL zmieniał PWM tylko co 10 s, a LOCK co `lock_interval_s`; na wąskim
  detektorze faza przemiata cały zakres w ~10-15 s resztkowego dryfu, więc
  między korekcjami faza błądziła i zawijała się, a PWM stał (widoczne jako PWM
  przyklejony do jednej wartości przez 114 próbek). DPLL koryguje teraz co 2 s.
  To *nie* jest błąd schematu: w każdym stanie to PWM (przez filtr RC → EFC)
  steruje OCXO — Vphase jest tylko pomiarem sprzężenia zwrotnego do ADC, więc
  słusznie nie ma analogowego toru Vphase→EFC.
- **Interwał LOCK ograniczony do sensownego zakresu (1..30 s).** Uszkodzony
  `lock_interval_s` (np. 50373 widziane w logu) sprawiłby, że LOCK korygowałby
  mniej więcej raz na 14 godzin; jest teraz ograniczony w czasie działania i w
  komendzie `LIV`, żeby LOCK dalej śledził.

---

## [v0.64-rtos]

### Zmienione
- **Usunięto zawodny auto-probe polaryzacji; polaryzację ustawia się ręcznie.**
  Jednocyklowy probe nie potrafił oddzielić efektu PWM od własnego dryfu fazy na
  wąskim, dryfującym detektorze, więc wielokrotnie wykrywał zły znak (+1 tam,
  gdzie płytka jest −1). ACQ wstrzymuje się teraz i wypisuje przypomnienie o
  uruchomieniu `LPOL -1` (lub `+1`) i `ES`, gdy polaryzacja nieustawiona, a
  DPLL/LOCK i tak się wstrzymują przy nieznanej polaryzacji. Gdy `LPOL` jest
  ustawione i zapisane, wszystkie trzy stany używają go spójnie — to niezawodne,
  czego o probe nie dało się powiedzieć.

---

## [v0.63-rtos]

### Naprawione
- **Wykryta polaryzacja jest teraz współdzielona przez wszystkie trzy stany.**
  Auto-wykryty znak żył w statycznej zmiennej lokalnej wewnątrz ACQ, niewidocznej
  dla DPLL/LOCK, które wpadały w fallback +1 i — na płytce o odwrotnej
  polaryzacji z niezapisanym znakiem — pchały fazę na górny rail, z rosnącym PWM
  i częstotliwością oddalającą się od 10 MHz. ACQ zapisuje teraz wykryty znak do
  `g_ltic.polarity`, więc każdy stan go używa (i wypisuje przypomnienie o `ES`).
- **DPLL/LOCK wstrzymują się zamiast zgadywać, gdy polaryzacja nieznana.** Bez
  ustalonego znaku dają teraz zerową korekcję i pozwalają maszynie wrócić do ACQ
  (który sonduje), zamiast zakładać +1 i uciekać.
- **Strażnik ucieczki.** Jeśli faza jest przyklejona do railu, a PWM zostaje
  wypchnięty o więcej niż ~6000 LSB od punktu startu pętli, pętla zamraża się i
  ostrzega raz („check LPOL / re-centre") zamiast zjeżdżać PWM na skraj i
  rozstrajać OCXO.

### Uwaga
- Zapisz polaryzację: gdy pętla wypisze „detected …polarity -1", uruchom `ES`,
  żeby przetrwała restart (to była główna przyczyna ostatniej ucieczki — znak
  był ustawiony, ale nigdy zapisany, więc wracał do auto/jeden).

---

## [v0.62-rtos]

### Naprawione
- **DPLL i LOCK stosują teraz polaryzację płytki (wcześniej tylko ACQ).** ACQ
  używał wykrytego/wymuszonego znaku `LPOL`, ale DPLL i LOCK nie — więc na
  płytce o odwrotnej polaryzacji sterowały fazą w złą stronę, spychając Vphase
  na dolny rail i wracając od razu do ACQ (faza centrowała się w ACQ,
  przekazywała do DPLL, po czym była pchana do ~0 V i wracała). Wszystkie trzy
  stany dzielą teraz tę samą polaryzację, więc DPLL/LOCK ciągną fazę ku środkowi
  zamiast w rail. Przy działającym już przekazaniu ACQ (v0.61) to właśnie
  pozwala DPLL utrzymać się i przejść do LOCK.

---

## [v0.61-rtos]

### Naprawione
- **ACQ zeruje teraz dryf fazy zamiast gonić jej pozycję.** Przy poprawnej
  polaryzacji (`LPOL -1`) PWM przestał uciekać, ale faza wciąż przemiatała cały
  detektor i zawijała się, więc ACQ nigdy nie spełniał warunku wyjścia „w oknie
  + małe nachylenie". Resztkowy offset częstotliwości (~-0,26 Hz) napędzał fazę
  ~26 ns/s przez detektor 318 ns — dużo za szybko. Dominujący człon ACQ działa
  teraz na DRYFIE fazy (dFaza/dt), sprowadzając offset częstotliwości do zera,
  żeby faza przestała się ruszać; słaby człon centrujący parkuje ją w środku
  dopiero gdy dryf jest już mały. Skoki dryfu od zawinięć (faza skacze >½
  zakresu w kroku) są odrzucane, żeby nie psuły estymaty dryfu ani przejść
  bramkowanych nachyleniem.

---

## [v0.60-rtos]

### Naprawione
- **ACQ uciekał z PWM przy odwrotnej polaryzacji płytki.** ACQ przesuwał PWM w
  stałym kierunku ku `zero_offset`; na sprzęcie, gdzie zwiększanie PWM obniża
  napięcie fazy (odwrotny znak), pchało to PWM coraz niżej, a faza zawijała się
  chaotycznie, więc ACQ nigdy się nie ustabilizował (obserwowane jako długie
  utknięcie w ACQ z PWM zjeżdżającym z ~41000 do ~17000). ACQ **wykrywa teraz
  automatycznie polaryzację PWM→faza** małym krokiem próbnym, potem steruje ku
  celowi z właściwym znakiem. Nowa komenda `LPOL -1/0/1` wymusza znak (0 = auto).
- **ACQ centruje teraz na środku zakresu detektora, nie na `zero_offset`.** Na
  wąskim, niskim detektorze `zero_offset` może siedzieć blisko dna (np.
  0,097 V), więc celowanie w niego trzymało fazę przy railu (ryzyko latch-up /
  zawinięcia, zgodnie z uwagą Dana o wyborze środka skali). ACQ celuje teraz w
  środek zakresu, z możliwością nadpisania przez `LCV <wolty>`.

### Dodane
- Komendy CLI `LPOL` (polaryzacja PWM→faza) i `LCV` (cel centrowania ACQ), obie
  zapisywane w EEPROM i pokazywane przez `LL`.

---

## [v0.59-rtos]

### Zmienione
- **Bramkowanie nachyleniem fazy przy przejściach stanów (algorytm 10).** Za
  radą Dana (time-nuts) oba przejścia stanów LTIC sprawdzają teraz NACHYLENIE
  fazy (dFaza/dt), nie tylko wielkość fazy. Ponieważ częstotliwość to pierwsza
  pochodna fazy, małe nachylenie oznacza, że częstotliwość jest już blisko
  10 MHz — więc ACQ→DPLL wymaga teraz szerokiego okna nachylenia, a DPLL→LOCK
  ~5× węższego, co zapobiega przekazaniu, gdy faza jedynie przelatuje przez
  środek z dużą prędkością (co zalokowałoby złą częstotliwość). LOCK wraca też
  do DPLL, jeśli nachylenie rośnie. To sprawia, że częstotliwość trafia bardzo
  blisko nominału przy każdym przekazaniu.

---

## [v0.58-rtos]

### Naprawione
- **Rampa `LC` zdecydowanie za szybka dla wąskiego detektora.** Na sprzęcie,
  którego detektor obejmuje tylko ułamek zakresu ADC (np. ~0,33 V na okres
  jednoznaczności), stara rampa +2000 LSB przemiatała fazę przez cały detektor
  co ~1-2 s, więc każda próbka trafiała w rail albo zawinięcie i `LC` przerywał
  z "mostly railed". Domyślny offset rampy to teraz łagodne 60 LSB (≈4-5 ns/s
  na typowym OCXO), a `LC` adaptacyjnie zmniejsza offset dalej, jeśli zmierzony
  dryf przekroczyłby detektor w mniej niż ~15 s. Poprawka pomiaru
  częstotliwości z v0.56 potwierdzona (realne df raportowane, np. 1,4-2,0 Hz,
  nie stare zaszyte 0,6).

---

## [v0.57-rtos]

### Naprawione
- **ACQ aktywnie centruje teraz fazę (było tylko sterowanie częstotliwością).**
  Stan ACQ wcześniej korygował tylko błąd częstotliwości TIM2; gdy OCXO był już
  blisko 10 MHz, nic nie napędzało fazy, więc mogła utknąć przy krawędzi
  detektora na zawsze i nigdy nie spełnić warunku wyjścia ACQ→DPLL
  (obserwowane jako całonocne utknięcie z Vphase nisko). ACQ przesuwa teraz PWM
  w stronę środka detektora, gdy odczyt jest na krawędzi, i steruje
  proporcjonalnie do błędu fazy, gdy jest w oknie.
- **Środek fazy brany z kalibracji, nie z zaszytego 1,65 V.** Realny sprzęt
  może mieć wąskie pasmo detektora daleko od środka ADC (np. 0..0,45 V), więc
  pętla centruje teraz na skalibrowanym `zero_offset` (z zgrubnym fallbackiem
  0,22 V) zamiast zakładać 1,65 V. Uruchom `LC`, żeby `zero_offset`/`ns_per_volt`
  odzwierciedlały realne pasmo.

---

## [v0.56-rtos]

### Naprawione
- **Pomiar częstotliwości w `LC`.** Kalibracja czytała średnią częstotliwości
  z okna 10 s raz, zaraz po 10 s settlingu — na realnym sprzęcie to okno nie
  zdążyło jeszcze nadążyć za wymuszoną rampą, więc tempo rampy (a przez to
  `ns_per_volt`) wychodziło błędne. `LC` czeka teraz 30 s, potem próbkuje
  średnią ze 100 s (stabilniejsza, z fallbackiem na 10 s) dwukrotnie w odstępie
  ~5 s i uśrednia.
- **Obsługa sufitu w `LC`.** Próbki, w których napięcie TIC siedzi na suficie
  lub podłodze ADC (faza poza oknem detektora), są teraz pomijane zamiast
  spłaszczać dopasowanie najmniejszych kwadratów, a `LC` przerywa z jasnym
  komunikatem, jeśli rampa jest w większości na sufitach (każąc najpierw
  wycentrować Vphase blisko środka).
- **Poprawka kompilacji:** usunięto zduplikowany extern `g_ltic_voltage` w
  GPSDO_algorithms.cpp, który kolidował z deklaracją w `gpsdo_state.h`.

---

## [v0.55-rtos]

### Dodane
- **Algorytm 10 (trójstanowy PLL LTIC) — pętla jest już zaimplementowana.**
  `LA 10` dyscyplinuje OCXO z fazy sprzętowego TIC (PA1) przez hybrydową
  maszynę stanową ACQ → DPLL → LOCK. ACQ sterowany częstotliwością (TIM2),
  żeby ściągnąć OCXO blisko 10 MHz i faza dryfowała wolno; DPLL dodaje człon
  fazy LTIC do szybkiego centrowania; LOCK sterowany fazą, z wolną
  aktualizacją co `lock_interval_s` i pasmem histerezy do powrotu na DPLL.
  picDIV uzbraja się automatycznie przy wejściu w ACQ. Pętla pracuje w
  nanosekundach, gdy TIC jest skalibrowany (`LC`), a bez kalibracji przechodzi
  na fazę w woltach (nominalnie) z jednorazowym ostrzeżeniem. Stan jest
  zachowywany w `g_ltic.state`, więc ciepły restart (`RB`) wznawia w środku
  sekwencji zamiast zaczynać od ACQ. Pole trendu pokazuje `ACQ` / `DPLL` /
  `LOCK`.
- **Trzeci zestaw PID (ACQ).** `LticParams_t` zyskał PID `acq` obok `dpll` i
  `lock`, z własnymi komendami CLI `AQP` / `AQI` / `AQD` / `AQL` i zapisem w
  EEPROM. `LL` pokazuje teraz wszystkie trzy zestawy.

### Zmienione
- **Układ EEPROM rozszerzony do 216 bajtów (rezerwa do 224).** Blok PID ACQ
  [200..215] dodany pod tym samym podpisem `GPSD2` z guardami NaN/`0xFF`, więc
  starsze zapisy nadal wczytują się z domyślnymi nastawami ACQ.

---

## [v0.54-rtos]

### Dodane
- **`LC` — automatyczna kalibracja LTIC.** Samodzielnie mierzy nachylenie
  napięcie→czas detektora TIC, bez żadnego zewnętrznego wzorca. `LC` wymusza
  mały offset PWM, żeby faza narastała liniowo, wyznacza tempo rampy z błędu
  częstotliwości TIM2 (`phase_rate = df / BASE_FREQ × 1e9` ns/s), dopasowuje
  metodą najmniejszych kwadratów napięcie TIC do czasu (`dV/dt`) i liczy
  `ns_per_volt = phase_rate / (dV/dt)`. Zapisuje też przemiecioną rozpiętość
  napięcia jako `range_ns` oraz `zero_offset` w połowie skali, wykrywając jedno
  zawinięcie, by mieć czysty segment rampy. Działa w zadaniu sterującym jak
  `CT`, z tym samym wzorcem bezpieczeństwa (PWM zapamiętany i przywrócony,
  wyniki z guardami, przerwanie przy braku GPS / za mało punktów / osobliwym
  lub płaskim dopasowaniu — parametry niezmienione przy każdym błędzie). Wyniki
  trafiają do bieżących parametrów LTIC; przejrzyj `LL`, potem `ES` by zapisać.
  Nowe stałe configu `LTIC_CAL_PWM_OFFSET`, `LTIC_CAL_SECS`,
  `LTIC_CAL_MIN_POINTS`. To wypełnia pola kalibracyjne, których będzie
  potrzebować pętla fazy A; sama pętla nadal nie jest zaimplementowana.

---

## [v0.53-rtos]

### Dodane
- **Komendy restartu warm/cold `RB` i `CR`.** `RB` robi ciepły restart
  (`NVIC_SystemReset()`) z zachowaniem EEPROM, więc wciąż ciepły OCXO odtwarza
  swój zdyscyplinowany stan. `CR YES` robi zimny restart: kasuje EEPROM (powrót
  do fabrycznych domyślnych — PWM, model, kalibracja, parametry LTIC) i
  restartuje; potwierdzenie `YES` jest wymagane, bo kasuje wyuczony model OCXO.
- **Infrastruktura algorytmu 10 (LTIC) — parametry, CLI i EEPROM.** Pełny
  zestaw parametrów, edycja CLI i zapis w EEPROM dla planowanego 3-stanowego
  PLL na LTIC (ACQ→DPLL→LOCK), żeby konfiguracja była gotowa zanim powstanie
  sama pętla („faza A"). Nowa struktura `LticParams_t` zawiera kalibrację TIC
  (ns/V, offset zera, zakres), dwa zestawy PID (szerokopasmowy DPLL +
  wąskopasmowy LOCK), progi przejść stanów, interwał LOCK i wznawialny stan.
  Piętnaście komend CLI ustawia/pokazuje te pola (`LL`, `LNV/LZO/LRN`,
  `DPP/DPI/DPD/DPL`, `LKP/LKI/LKD/LKL`, `LAT/LDT/LIV`). `LA 10` jest
  rozpoznawane przez parser, ale zgłasza „not implemented yet" i odmawia
  wyboru, więc OCXO nigdy nie zostaje bez dyscyplinowania. Sama pętla nie jest
  zaimplementowana — to faza A, czeka na sprzęt LTIC.

### Zmienione
- **Układ EEPROM rozszerzony do 200 bajtów (rezerwa do 208).** Blok LTIC
  [144..207] dodany pod **tym samym podpisem `GPSD2`**; każde nowe pole jest
  zabezpieczone guardem NaN/`0xFF`, więc obrazy EEPROM zapisane starszym
  firmware wczytują się czysto, a parametry LTIC przyjmują wartości domyślne do
  czasu ustawienia. Bez migracji ani re-init.

---

## [v0.52-rtos]

### Dodane
- **Podgląd napięcia fazy LTIC (Lars' TIC).** Napięcie TIC na PA1 było już
  próbkowane i wysyłane w telemetrii serial, ale nie miało reprezentacji na
  ekranie. Dodano (wszystko pod `GPSDO_LTIC`, więc zero wpływu na buildy bez
  TIC):
  - **wiersz na TFT** pokazujący `Vph:x.xxxV` (oraz `… NNNns` po kalibracji);
  - **pozycję LTIC w checkliście sprzętu na splashu** (`[x] LTIC phase (PA1)`
    — pokazywana gdy wkompilowana, jak TM1637/TFT, bo TIC jest read-only i nie
    da się go wykryć);
  - **stałą kalibracyjną `LTIC_NS_PER_VOLT`** w configu (0 = nieskalibrowane →
    tylko wolty). Po ustawieniu zmierzonego nachylenia rampy, wyświetlacz i
    planowany algorytm dyscyplinowania fazą przeliczają wolty na ns.
  To **warstwa wyłącznie podglądowa/telemetryczna** — pętla sterowania jeszcze
  nie dyscyplinuje OCXO z TIC (planowane jako osobna faza, nowy algorytm
  oparty na LTIC). OLED/LCD świadomie pozostawiono bez zmian (ich układy są
  pełne); Vphase jest tam dostępne przez logowanie serial, co na tym etapie
  wystarcza do charakteryzacji TIC.

---

## [v0.52-rtos]

### Dodane
- **Warstwa podglądu napięcia fazy LTIC (Lars' TIC).** Gdy `GPSDO_LTIC` jest
  wkompilowane, zatrzaśnięte napięcie TIC (`g_ltic_voltage`, już próbkowane na
  PA1 i rozładowywane co PPS) jest teraz pokazywane jako podgląd: dedykowany
  wiersz `Vph:` na TFT (pod rzędem czujników, widoczny tylko z wkompilowanym
  LTIC) oraz pozycja `LTIC phase (PA1)` w checkliście startowej. Telemetria
  serial już wcześniej zawierała Vphase. Nowa stała kalibracyjna
  `LTIC_NS_PER_VOLT` pozwoli w przyszłym buildzie przeliczyć napięcie na fazę w
  nanosekundach: dopóki wynosi 0 (domyślnie, nieskalibrowane) wyświetlacze
  pokazują tylko wolty; po ustawieniu wiersz TFT pokazuje też `<n>ns`. To tylko
  podgląd/telemetria — pętla sterowania jeszcze nie dyscyplinuje na LTIC; to
  planowany osobny algorytm. Układy OLED/LCD bez zmian (oba pełne); Vphase
  zostanie tam dodane, gdy LTIC stanie się operacyjnym wejściem pętli.

---

## [v0.51-rtos]

### Dodane
- **Komendy CLI są teraz niewrażliwe na wielkość liter.** Dyspozytor komend
  porównywał je przez `strcmp()`, więc `LA` działało, ale `la` już nie.
  Dopasowanie komend używa teraz małej funkcji pomocniczej niewrażliwej na
  wielkość liter (`cli_ieq`), więc akceptowana jest dowolna wielkość liter
  (`LA` / `la` / `La` są równoważne), włącznie z komendami pisanymi małymi
  literami (`up1`, `dp10`, …) oraz rodziną `KP`/`KI`/`KD`/`IL` (gdzie litera
  parametru również jest dopasowywana niewrażliwie). Argumenty komend bez
  zmian; `TO A` już wcześniej akceptowało obie wielkości.

### Zmienione
- **Obsługa ZED-F9T (Gen9) nie jest już eksperymentalna.** Ścieżka survey-in
  CFG-VALSET oraz fallback monitora NAV-SVIN zostały przetestowane na realnym
  sprzęcie przez użytkownika EEVblog danieljw, więc oznaczenia
  „eksperymentalny / nietestowany" zostały usunięte z kodu, configu i plików
  README. Bez zmian w samej ścieżce F9T — tylko jej status.

---

## [v0.50-rtos]

### Dodane
- **Obsługa odbiornika czasowego ZED-F9T (Gen9) — eksperymentalna,
  nietestowana.** Dodano trzecią ścieżkę survey-in obok sprawdzonych LEA-6T /
  LEA-M8T. `ubx_start_survey_in()` wysyła teraz także ramkę `CFG-VALSET`
  (0x06 0x8A) ustawiającą klucze konfiguracyjne Gen9: `CFG-TMODE-MODE`
  (survey-in), `CFG-TMODE-SVIN_MIN_DUR` oraz `CFG-TMODE-SVIN_ACC_LIMIT` (ten
  ostatni przeliczany z mm na jednostkę 0.1 mm odbiornika F9T). Monitor
  survey-in zyskał równoległy parser `NAV-SVIN` (0x01 0x3B) i przechodzi na
  niego, gdy `TIM-SVIN` nie odpowiada, bo generacja F9 raportuje survey-in
  przez NAV-SVIN. ⚠️ Napisane na podstawie dokumentacji u-blox/ubxtool bez
  modułu F9T pod ręką — identyfikatory kluczy, jednostka 0.1 mm i offsety
  payloadu NAV-SVIN NIE są zweryfikowane na sprzęcie. Ramka legacy `CFG-NAV5`
  (tryb stacjonarny) może zostać odrzucona (NAK) przez F9T (nieszkodliwe;
  ścieżka survey-in jest niezależna). Dwa przetestowane odbiorniki bez zmian:
  TIM-SVIN jest nadal próbowany jako pierwszy, więc zachowanie LEA-6T /
  LEA-M8T / NEO-M8T pozostaje niezmienione. Udokumentowane jako eksperymentalne
  w README i config.

### Zmienione
- **Podtytuł splashu LCD 20×4** zmieniony z `GPS-Disciplined Osc.` na
  `GPS-Disciplined OCXO`, zgodnie ze splashem TFT (oba 20 znaków, pełna szerokość).

### Uwagi
- **NEO-M8T** potwierdzony (analizą datasheetu) jako w pełni zgodny z istniejącą
  ścieżką LEA-M8T — ten sam układ M8 + FW3, te same CFG-TMODE2 / TIM-SVIN — bez
  zmian w kodzie. Udokumentowane w sekcji odbiorników czasowych.

---

## [v0.49-rtos]

### Naprawione
- **Kolejność makr w config: `OUT_SERIAL` respektuje teraz `GPSDO_BLUETOOTH`.**
  Makro routingu `OUT_SERIAL` było ewaluowane blisko początku
  `gpsdo_config.h`, *przed* zdefiniowaniem `GPSDO_BLUETOOTH` (i kilku innych
  przełączników funkcji) niżej w pliku. W efekcie `OUT_SERIAL` zawsze
  rozwijało się do USB `Serial`, nawet gdy Bluetooth był włączony, a build z
  zakomentowanym `GPSDO_BLUETOOTH` mógł nie kompilować się zależnie od tego,
  co go używało. Wszystkie przełączniki funkcji są teraz zgrupowane razem
  blisko początku pliku, a makra od nich pochodne (`OUT_SERIAL`) ewaluowane
  później, w dedykowanej sekcji „Derived macros". Brak zmian funkcjonalnych
  poza tym, że wyjście Bluetooth faktycznie trafia teraz na Serial2. Skan
  pozostałych plików źródłowych nie wykrył innych problemów z kolejnością
  definicja-po-użyciu.

### Zmienione
- **Ujednolicono wzorzec startowy HT16K33 z TM1637.** Po starcie HT16K33
  pokazuje teraz `----` (kreski na segmencie G) zamiast `oooo`, zgodnie ze
  wzorcem startowym TM1637 — oba zegary LED sygnalizują „żywy, oczekiwanie na
  GPS" tak samo. Wskaźnik `oooo` pozostaje dla przypadku braku fixa podczas
  pracy (gdzie TM1637 też pokazuje `oooo`), więc oba wyświetlacze zachowują
  się teraz identycznie w każdym stanie.
- **Linia kredytów na splashu TFT** zmieniona z `jmnlabs + with Claude
  (Anthropic)` na `jmnlabs with Claude (Anthropic)` (usunięto `+`).

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
