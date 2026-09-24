# Lista zmian — GPSDO FreeRTOS

[English](CHANGELOG_EN.md) | **Polski** | [Español](CHANGELOG_ES.md)

📖 [Strona projektu](../README.md) · Powrót do [README](README_PL.md) · Instrukcja: [MD](MANUAL_PL.md) · [PDF](MANUAL_PL.pdf)

Wszystkie istotne zmiany w projekcie są udokumentowane poniżej.

Projekt: **J. M. Niewiński** — <https://github.com/jmnlabs/GPSDO_FreeRTOS>
Na podstawie **GPSDO v0.06c** autorstwa André Balsy
(<https://github.com/AndrewBCN/STM32-GPSDO>); port na FreeRTOS oraz
algorytmy sterowania autorstwa autora, **Claude Opus 5** (Anthropic),
**GLM-5.3 Max** (Z.ai) i **Qwen3.8-Max** jako asystenci programowania,
projekt PCB — Scrachi (forum EEVBlog).

Od builda 57 build nazywa się `GPSDO vX.YY.NNrt`: wydanie, `NN` — numer
builda (`BUILD_SERIAL` w `gpsdo_build_id.h`) — i `rt` dla linii portu na
FreeRTOS. Build 56, pierwszy z numerem w nazwie, zapisywał ją jako
`GPSDO v1.07-rt56`. Wydania sprzed tej zmiany miały sufiks `-rtos` —
`v1.06-rtos` to był build 42 — i zachowują nazwy, pod którymi wyszły.

---

## [v1.07] — wydane 2026-09-24 (build 57)

Wydane jako build 57, pierwsze wydanie z numerem builda w nazwie:
`GPSDO v1.07.57rt`. Jak poprzednio, wpisy trafiały tutaj wtedy, gdy zostały
zmierzone, a nie wtedy, gdy zostały napisane.

### Dodane
- **`SPAN` — dwie kalibracje `CT`, po jednej na każdy zakres EFC, wybierane
  zworką zakresu na PB14 (build 55).** Płytka z przesuwnikiem poziomu EFC
  steruje oscylatorem w pełnym zakresie napięcia albo w zawężonym, a to są dwa
  różne obiekty: na prototypie V3 Dana Wieringa `CT` zmierzyło 8209 LSB/Hz w
  pełnym zakresie i 40873–46711 w zawężonym, 5,0–5,7 razy więcej — choć wynik
  dla pełnego zakresu pochodzi sprzed wymiany referencji 5 V na 4,096 V, a przy
  jednej referencji obie pozycje różnią się o dzielnik, 4,1–4,7 razy. Z tej liczby
  wyprowadzany jest każdy współczynnik pętli, więc do tej pory płytka po
  przestawieniu zworki jechała na kalibracji z drugiej pozycji, dopóki ktoś
  nie powtórzył `CT`. Drugi biegun zworki idzie teraz na **PB14** (założona =
  do masy = REDUCED; zdjęta albo w ogóle niepodłączona = FULL, więc płytka bez
  tego przewodu zachowuje swoją jedną kalibrację jak dotąd), a firmware trzyma
  osobne K dla każdej pozycji. Gdy zworka się przestawi — po odfiltrowaniu
  drgań styku, pół sekundy:

  - każdy współczynnik jest wyliczany od nowa z K tej pozycji — ten sam
    zestaw, który wylicza `CT`, teraz w jednej funkcji `algo_coeffs_from_k()`,
    wołanej przez oba miejsca — a wyuczony stan liczony w LSB (feed-forward
    LRN, tempco algorytmu 9) jest przeskalowany stosunkiem obu K;
  - **kod sterujący jest przeliczany tak, żeby pin EFC zachował napięcie**, a
    częstotliwość przed przestawieniem była częstotliwością po nim:
    `c_B = p_B + (K_A/K_B)·(c_A − p_A)`, gdzie `(p_A, p_B)` to jedna para
    kodów, o której wiadomo, że daje to samo napięcie w obu pozycjach. Offsety
    obu torów ustawiają referencja, dzielnik i trymer, a żadnego z nich
    firmware nie widzi, więc para jest mierzona, a nie modelowana — kod starej
    pozycji przy przestawieniu z zablokowaną pętlą, sparowany z kodem nowej
    pozycji, gdy jej własna pętla wypracuje lock, albo zero znalezione przez
    `CT` — i przy każdym przestawieniu kotwiczona na nowo, żeby błąd K obracał
    się wokół punktu, w którym pętla faktycznie jest;
  - pętla startuje od nowa, jak przy zmianie algorytmu.

  Pozycja bez własnej kalibracji jedzie na współczynnikach drugiej — jak zawsze
  jechała płytka z jedną kalibracją — a `CT` startuje samo: przy fixie GPS,
  nigdy w holdoverze, i jeszcze do trzech razy, 10, 20 i 40 minut po nieudanej
  próbie; potem przestaje i mówi o tym. Zworka przestawiona przy wyłączonym
  zasilaniu jest obsłużona przy starcie, zanim ruszy pętla.
  Pierwszy start tego builda przypisuje istniejącą kalibrację pozycji, którą
  czyta pin; jeśli `CT` w drugiej pozycji zmierzy potem ten sam obiekt (w
  granicach 1,5×), starsza kalibracja zostaje uznana za zmierzoną w złym
  miejscu — typowo płytka skalibrowana na REDUCED, zanim podłączono PB14 — i
  zapomniana. `SPAN` pokazuje stan, `SPAN CLR FULL|REDUCED` zapomina jedną
  pozycję, a raport `DAC` wypisuje to pod linią obiektu. Ręczne wzmocnienia w
  LSB (`LG`, `MG`, `LTK`) należą do operatora i nie są przeskalowywane;
  przestawienie zworki ostrzega o każdym, które jest ustawione.

  **Zasymulowane przed wydaniem.** `tools/spansim` (nowe) kompiluje ten
  moduł, magazyn ustawień, algorytmy i moduł zdrowia bez zmian i steruje nimi
  modelem płytki V3 — dzielnik 4,083:1, 1,5967 Hz/V, starzenie 1,7e-10/dobę —
  pod algorytmem 11 przy LTC 60, każdy cykl zasilania jako osobny proces ze
  wspólnym obrazem flasha. Każdy scenariusz jest powtarzany na buildzie bez
  czujnika zworki, czyli na firmware sprzed zmiany:

  | | z czujnikiem zworki | jedna kalibracja |
  |---|---|---|
  | przestawienie między dwiema skalibrowanymi pozycjami: ląduje przy | 3e-12 … 1,8e-11 | 3e-8 … 1,2e-7 |
  | wychylenie fazy / ponowny lock | 13–49 ns / 399 s | 3,5–11,5 µs / 665–877 s |
  | zworka przestawiona przy wyłączonym zasilaniu: start przy | 8,1e-12 | 1,2e-7 |
  | zimny start, 3e-8 retrace'u, potem REDUCED: ląduje przy | 5,6e-10 | 3,3e-8 |

  Liczba dla zimnego startu to własny błąd przeliczenia: oba `CT` myliły się o
  2 % i 10 % w przeciwne strony, a pętla była 417 kodów od pary.

  Zapisane jako 12 bajtów dopisanych na końcu bloku ustawień (380 → 392
  bajty, każde wcześniejsze pole pod starym offsetem, sprawdzone `offsetof`
  pod arm-none-eabi). Starszy rekord czyta nowe pola jako 0, co znaczy „nigdy
  nie zapisane"; build 54 czytający rekord builda 55 bierze pierwsze 380
  bajtów, więc powrót też jest bezpieczny.
- **`VS` — co mierzy dzielnik na PA0 (build 49).** `VS VCC` albo `VS VREF`,
  zapisuje się samo razem z grupą ścieżki wyjściowej. Od płytki V3 zworka na
  górze tego dzielnika 4k7 + 4k7 wybiera **referencję napięcia** zamiast szyny
  5 V: ten sam pin, ten sam dzielnik, to samo skalowanie, inny obiekt pomiaru.
  Firmware nie widzi zworki, więc się mu ją podaje — tak samo jak przy `DV`,
  `AV` i ścieżce DAC, i z tego samego powodu: jedynym śladem, jaki zworka
  zostawia, jest napięcie nie tam, gdzie firmware się go spodziewał, a to
  pomaga wyłącznie wtedy, gdy firmware'owi powiedziano, czego ma się
  spodziewać.

  Podanie tego kupuje kontrolę. Przy `VREF` raport `DAC` drukuje odczyt obok
  tego, co twierdzi `DV`, i protestuje powyżej 5 % — co łapie referencję
  brakującą, zapadniętą albo po prostu nie tę, którą wlutowano. To ostatnie nie
  jest subtelną awarią: kość 5,000 V tam, gdzie `DV` mówi 4,096, sprawia, że
  każda liczba częstotliwości drukowana przez płytkę jest o piątą część za
  mała, w całkowitej ciszy. Etykieta na TFT też idzie za zworką — nazwanie
  referencji 4,1 V „Vcc" czytałoby się jak zasilanie, które w połowie siadło.

  Dlaczego w ogóle zworka, a nie własny pin: **nie ma wolnego kanału ADC**.
  Przetwornik sięga na tej obudowie PA0–PA7, PB0 i PB1, a przy SPI1 obsługującym
  wyświetlacz wszystkie dziesięć jest zajętych. Uwolnienie jednego oznaczałoby
  przeniesienie zegara AD5680 z PB0 — pin kupiony kosztem zepsucia płytek, które
  już istnieją.
- **`BL` — ściemnianie podświetlenia TFT (build 48).** 30..100 %, zapisuje się
  samo razem z flagami wyświetlania. Steruje tranzystorem MOSFET z kanałem P na
  **PB5** (TIM3 CH2, 20 kHz), włączonym między szynę 3,3 V a anodę LED panelu:
  bramka przez ~47 Ω, 100 kΩ do masy, żeby stan był określony, gdy pin jest w
  stanie wysokiej impedancji po resecie, i 10-47 µF pojemności przy drenie.

  Stopień **odwraca** — niski stan bramki to pełna jasność — więc firmware
  zapisuje dopełnienie, przez co `BL 100` wypada na porównaniu równym zeru: pin
  stoi statycznie nisko i stopień w ogóle nie przełącza. O to właśnie chodzi.
  Szyna 3,3 V zasila VDDA, a więc referencję ADC, odczyt fazy i monitor Vctl —
  pełna jasność jest zatem ustawieniem *najcichszym*, nie najgłośniejszym, a
  ściemnianie wymienia odrobinę szumu na szynie na obciążenie i ciepło w
  obudowie sprzężonej termicznie z OCXO.

  Dolna granica 30 % istnieje z powodu, który nie jest elektryczny: poniżej
  mniej więcej jednej trzeciej panel przestaje być czytelny, zamiast stawać się
  użytecznie przyciemniony, więc omyłkowe `3` zostawiłoby operatora bez
  możliwości odróżnienia przyciemnionego ekranu od martwej płytki. Kompiluje
  się z każdym TFT i **oddaje PB5 generatorowi testowemu 2 kHz**, jeśli ten
  został jawnie włączony — domyślne ustawienie nie może odbierać pinu świadomej
  decyzji. `TIM3` był wolny: kilka komentarzy twierdziło, że mieszka tam
  przechwytywanie 1 PPS, ale to TIM2 kanał 3 na PB10.

  Zapisane w **ostatnim bajcie wyrównania bloku ustawień**, offset 319, obok
  `dac_path` na 318 — `sizeof(SettingsBlock_t)` wynosi 376 przed i po, a każde
  późniejsze pole zachowuje swój offset; sprawdzone przez `offsetof` pod
  arm-none-eabi, a nie na oko. Bez podbicia `SETTINGS_VER`, bo odczyt wymaga
  dokładnej zgodności wersji *i* rozmiaru, a podbicie wyrzuciłoby wszystkim PID,
  LC i strefę czasową za jeden bajt. Zero znaczy „nieustawione", więc rekord
  zapisany zanim to pole istniało prosi o wartość domyślną, a nie o ciemny
  ekran.
- `DV` — napięcie przy pełnym kodzie dla „zadane" w raporcie `DAC`
  (2,50..5,50 V, domyślnie 3,30 = model PWM). Na zewnętrznym DAC-u 5 V stary
  zahardkodowany model 3,3 V zaniżał „zadane" 1,5× i raport flagował MISMATCH
  przy każdym odczycie. Zapisuje się samo.
- `AV` — stosunek dzielnika przed pinem ADC (1,00..10,00, domyślnie
  bezpośrednio). Skaluje „zmierzone" i każde wyświetlenie Vctl (linia
  telemetrii, CSV, TFT). Zapisuje się samo. Przy dzielniku pomiarowym na
  wyjściu DAC (10k+10k na płytkach AD5680) para `DV 5.00` + `AV 2.00` czyni
  check MISMATCH dokładnym.
- `SETTINGS_VER` 6: obie skale persystują z blokiem ALGO; rekordy v5
  migrują automatycznie, nowe pola startują z domyślnymi.

### Zmienione
- **Numer builda przechodzi do wersji: `GPSDO v1.07.57rt` (build 57).**
  Wydanie, build jako trzecia liczba i `rt` dla linii FreeRTOS, w miejsce
  `GPSDO v1.07-rt56` z builda 56. Zmienia się tylko zapis: nazwa to nadal
  `g_fw_version`, składana wyłącznie w szkicu, drukowana w tych samych
  miejscach i dokładnie tak samo długa, więc każdy wyświetlacz, który miał
  miejsce na `-rt56`, ma je na `.57rt`. Nagłówki źródeł idą za nią
  (`Part of GPSDO v1.07.57rt`) — zmiana ich zapisu była edycją, więc na razie
  każdy mówi 57 — podobnie tytuły dokumentów, łącznie z README v1.06 w
  folderze szkicu (`v1.06.42rt`). Tuner czyta wszystkie trzy zapisy,
  `v1.06-rtos`, `v1.07-rt56` i `v1.07.57rt`, porównuje tylko wydanie i
  pomija osobne `build N` w linii stanu, gdy nazwa już je zawiera.
- **Nazwa firmware niesie numer builda: `GPSDO v1.07-rt56` (build 56).**
  Zastępuje `GPSDO v1.07-rtos`. `rt` nadal oznacza linię portu na FreeRTOS;
  liczba to `BUILD_SERIAL`, więc podbicie builda samo zmienia nazwę firmware,
  a zapis, zdjęcie wyświetlacza czy linia z tunera mówią, z którego builda
  pochodzą. Składana raz w szkicu jako `g_fw_version` — szkic jest jedyną
  jednostką, która dołącza `gpsdo_build_id.h`, więc podbicie nadal
  przekompilowuje tylko szkic — i drukowana z tego jednego napisu przez baner,
  `V`, nagłówek `H`, nagłówek TFT oraz ekrany startowe OLED i LCD, które mają
  na nią miejsce aż do builda 999. Znacznik kompilacji nadal kończy się na
  `build 56`, bo czytają go tunery starsze od tego. `PROGRAM_VERSION` to
  teraz samo wydanie, `v1.07`. Nagłówek każdego pliku źródłowego brzmi
  `Part of GPSDO v1.07-rt56`, a liczba to build, który ostatnio zmienił dany
  plik (`gpsdo_flash_ring_core.c` wciąż mówił v1.06). Build 57 przeniósł
  numer do wersji — patrz wyżej.
- **Tuner przyjmuje obie nazwy i podaje build raz (build 56).** Jego wzorzec
  wersji już przyjmował dowolny przyrostek po numerze wydania, więc
  `v1.07-rt56` i `v1.06-rtos` parsują się oba, a porównywane jest tylko
  `1.07`; linia stanu pomija osobne `build N`, gdy nazwa już je zawiera.
- **Tablica stref przegenerowana z IANA 2026d, a generator sprawdza teraz to, co
  zapisał (build 53).** 503 strefy, 88 reguł, ~3,34 KB flasha — dokładnie tyle
  samo, co tablica 2026c, którą zastępuje.

  **Trzy „błędne wiersze", które zgłosiłem przy poprzedniej tablicy, były
  fałszywym alarmem, i powód warto zapisać.** Reguła każdej strefy to łańcuch
  POSIX z końca jej pliku TZif, czyli reguła obowiązująca *po ostatnim
  przejściu zapisanym w pliku* — niekoniecznie ta z dnia generowania. Dla strefy
  z ustawową zmianą przed sobą te dwie rzeczy się różnią, a to, czy jest to
  usterka, zależy wyłącznie od zadanego pytania.

  Zapytane jako *„czy ta reguła jest poprawna dla roku kalendarzowego 2026?"*,
  sześć stref wygląda na zepsute: Kolumbia Brytyjska, Alberta i Terytoria
  Północno-Zachodnie przestają stosować czas letni **1 listopada 2026**, więc
  `America/Vancouver` niesie `MST7`, a `America/Edmonton` `CST6`, podczas gdy
  pierwsze dziesięć miesięcy 2026 miało jeszcze PDT i MDT. Zapytane jako *„czy
  jest poprawna dla lat, przez które ta tablica będzie siedzieć we flashu?"* —
  wszystkie sześć jest poprawnych, a stopka jest dokładnie właściwym wyborem:
  tablica generowana dziś powinna nieść epokę, *w którą* wchodzi, a nie tę,
  którą opuszcza. Africa/Casablanca to ta sama historia z wyprzedzeniem ośmiu
  dni: IANA modeluje Maroko jako trwałe UTC+0 od 20 września 2026, czyli
  dokładnie to, co mówi `<+00>0`.

  **Zmierzone, ewaluatorem samego firmware'u, a nie wywodem.** Każde przejście
  każdej strefy z tablicy, od dziś do końca 2028, sondowane dwie minuty przed i
  minutę po: **778 przejść w 484 strefach, 1556 sond, i firmware zgadza się z
  każdą** poza dwiema ostatnimi minutami obecnej epoki Maroka. To zarazem
  najostrzejszy test, jaki przeszła poprawka DST z builda 52.

  Generator nie polega już na tym, że ktoś zada właściwe pytanie. Po zapisaniu
  tablicy sprawdza każdą regułę względem tzdata tej maszyny **dwa lata do
  przodu** — w epoce, w której tablica naprawdę będzie żyć — i drukuje albo
  „wszystkie się zgadzają", albo listę stref, które nie. Sprawdzenie porównuje
  offsety, zamiast powtarzać silnik reguł firmware'u, bo druga implementacja
  potrafi mylić się w tym samym miejscu i zgadzać się sama ze sobą. Czyta OBA
  offsety reguły, nie tylko standardowy, bo Irlandia zapisuje swoją strefę jako
  `IST-1GMT0` — IST jest standardem, a GMT ujemnym czasem letnim, odwrotnie niż
  wszędzie indziej i całkowicie zgodnie z POSIX. Sprawdzenie czytające tylko
  pierwszy offset uznaje Dublin za zepsuty przy każdym uruchomieniu, a
  sprawdzenie, które krzyczy na poprawną strefę, jest gorsze niż żadne: tak
  właśnie przewija się obok prawdziwego zgłoszenia. Zweryfikowane w obie strony
  — milczy na tablicy tak jak wygenerowana i łapie podłożony zły offset albo
  brakującą regułę DST.
- **Jednolite nazwy `gpsdo_` i struktura repozytorium, którą da się czytać
  (build 48).** Siedemnaście plików zmieniło nazwę — `dac_ext`, `flash_ring`,
  `flash_ring_core`, `live_store`, `settings_store`, `tz_table`, `ubx_timtp`,
  `TeeSerial`, `build_id` i `GPSDO_algorithms` — więc każdy plik źródłowy
  należący do tego projektu nosi prefiks `gpsdo_` małymi literami, a listing
  katalogu oddziela projekt od tego, na czym stoi. Strażniki nagłówków poszły
  za nazwami plików.

  **Cztery pliki celowo zachowują nazwy**, każdy dlatego, że kosztem zmiany
  byłaby cisza, a nie błąd kompilacji: `GPSDO_FreeRTOS.ino` (Arduino wymaga,
  by plik szkicu nazywał się jak folder), `build_opt.h` (builder szuka tej
  nazwy, żeby pobrać flagi kompilatora — po zmianie flagi znikają, a kod dalej
  się buduje, tylko inaczej), `STM32FreeRTOSConfig.h` (biblioteka włącza go po
  nazwie) i `TM1637Display.*` (kopia obcej biblioteki; nazwa z upstreamu jest
  tym, co utrzymuje czytelność przyszłego diffa).

  `doc/`, `tools/` i `README.md` wychodzą z folderu szkicu do korzenia
  repozytorium, więc folder szkicu trzyma kod i nic więcej, a razem z nimi
  pojawia się `.gitignore`. Wyklucza wyjście builda, cache Pythona i katalog
  roboczy PDF-ów — oraz, celowo, materiał roboczy w `doc/`: korespondencja,
  audyty i listy zadań wymieniają ludzi z nazwiska i cytują prywatną pocztę, a
  nieuważne `git add .` nie powinno ich opublikować.

  Robi to wszystko `tools/gpsdo_restructure.py`. Domyślnie sucha próba,
  idempotentny, i **odmawia przeniesienia `tools/`, dopóki którykolwiek
  harness lokalizuje źródła licząc katalogi** — `hostcheck.sh`,
  `loopsim/run.sh` i `algoswitch/run.sh` mówiły „dwa poziomy wyżej", co jest
  prawdą tylko dopóki `tools/` siedzi w folderze szkicu; potem wskazuje na
  korzeń, a hostcheck skompilowałby puste drzewo i zgłosił sukces. Wszystkie
  trzy idą teraz w górę do `.ino`, co jest poprawne w obu układach.

  Sprawdzone przez uruchomienie skryptu na drzewie v1.06, a potem na drzewie
  v1.07, które hostcheck już przechodzi 14/14 — gdzie raportuje, że nie ma nic
  do zrobienia, co jest stwierdzeniem, że jego wynik i zweryfikowane drzewo to
  to samo. Jeden błąd znaleziony przy okazji: skrypt chodzi po `tools/`, mieszka
  w `tools/` i nosi wszystkie stare nazwy we własnej tablicy zmian, więc
  pierwsze uruchomienie przepisało tę tablicę na listę tożsamości. Teraz
  wyklucza sam siebie.
- **Napięcie sterujące nie przechodzi już przez `analogWrite()` (build 48).**
  Obie ścieżki wyjściowe posiadają teraz TIM4 na własność, przez rejestry —
  `pwm24_begin()` z odtwarzaniem DMA, nowe `pwm16_begin()` bez. Nośna i
  rozdzielczość są celowo **bez zmian**: 100 MHz / 50 000 = 2,000 kHz, dokładnie
  to, co dawało `analogWriteFrequency(2000)`, więc żadnej płytce nie przesuwa
  się przez to CT, filtr ani wzmocnienie obiektu.

  Znikają przy tym dwie rzeczy. `analogWrite()` z rdzenia wywołuje
  `pwm_start()`, które przelicza preskaler i auto-reload z żądanej
  częstotliwości **przy każdym wywołaniu** — napięcie sterujące zapisywane jest
  raz na sekundę przez całe życie płytki, więc był to timer przebudowywany raz
  na sekundę po to, by zmienić jedną wartość porównania: okazja do zakłócenia
  wyjścia co sekundę, kupiona za nic. A `analogWriteFrequency()` jest w rdzeniu
  **globalne**: stosuje się do pinu zapisanego jako następny, więc w chwili, gdy
  pojawia się drugi PWM — powyższe podświetlenie — oba po cichu biją się o jedno
  ustawienie.

  Zwykła ścieżka mapuje teraz również **pełną skalę na pełną skalę**, zgodnie z
  tą samą regułą, którą stosują już tablica ditheru i zewnętrzny DAC: kod 65535
  to porównanie równe okresowi. Rdzeń dzielił przez 65 536, przez co najwyższy
  LSB był nieosiągalny; różnica 15 ppm w środku zakresu leży daleko poniżej
  tego, co CT jest w stanie rozróżnić.
- CT dwuprzebiegowy przy łagodnym obiekcie regulacji: gdy K z pierwszego
  przebiegu spada poniżej 0,12 mHz/LSB, druga trójpunktowa tura mierzy
  ponownie na szerszym rozstawie kodów (cel ~0,8 Hz wychylenia,
  wycentrowanym na wyprowadzonym kodzie 10 MHz, nigdy węższym niż przebieg
  pierwszy i nigdy bliżej niż 1000 LSB od któregokolwiek krańca zakresu). Na
  takich płytkach stały sweep 20 480 LSB wychyla oscylator wyraźnie poniżej
  herca, a dopasowanie nachylenia rozbiegało się o 10-50% w górę wobec
  prawdy z DMM. Podłoga wiarygodności finalnego K przesunięta
  0,02 → 0,01 mHz/LSB — obroniona, bo wstępna brama łapie śmieci bez
  sygnału zanim jakiekolwiek K zostanie uwierzone.

### Naprawione
- **`CT` i `C` restartują pętlę w każdym buildzie (build 57).** Build 55
  kazał `CT` restartować pętlę po wycentrowaniu, ale umieścił restart w haku
  `CT` modułu zakresu, który build bez `GPSDO_SPAN_SENSE` kompiluje pusty —
  tam więc własna kopia kodu w pętli, wciąż ta sprzed przemiatania, dalej
  ciągnęła z powrotem ku niemu. Restart jest teraz w samym `CT`, a `C`,
  kalibracja dwupunktowa, dostaje tę samą linię, bo także wpisuje nowy kod i
  zostawia pętli tę samą nieaktualną kopię. W buildzie `spansim` z jedną
  kalibracją (`GPSDO_SPAN_SENSE` wyłączony), `CT` zaraz po przestawieniu
  zworki zakresu: bez restartu pętla ciągnęła z powrotem ku kodowi, który
  `CT` właśnie opuścił, najgorzej o 3,1e-8, odzyskała lock 1416 s po
  przestawieniu i przez następne dwie godziny miała 1,1e-9 RMS; z restartem
  najgorzej 5,0e-9, 1190 s i 2,2e-10. Build z czujnikiem zakresu zachowuje
  się dokładnie jak poprzednio: połowa wyniku `spansim` dotycząca zakresu jest
  identyczna co do bajtu. `tools/spansim` robi to wywołanie tam, gdzie robi je
  teraz `CT`.
- **`DV` tracił trzecie miejsce po przecinku przy każdym restarcie (build
  57).** Był zapisywany w centywoltach, więc `DV 4.096` — ADR4540 na V3 Dana
  Wieringa — wracał po restarcie jako 4.10, co daje 0,1 % przesunięcia
  kolumny „zadane" w raporcie `DAC`; a zapytanie i echo drukowały dwa
  miejsca, więc `DV` pokazywał 4.10 niezależnie od tego, czy trzymał 4.096,
  czy 4.10. `DV` jest teraz zapisywany także w miliwoltach, w polu dopisanym
  na końcu bloku ustawień (392 → 396 bajtów, bez zmiany wersji), i drukowany
  z trzema miejscami przez `DV` i raport `DAC`. Pole w centywoltach jest
  nadal zapisywane z tej samej wartości, więc starszy build czytający nowszy
  rekord zachowuje dwumiejscowe `DV` jak zawsze, a rekord ze starszego builda
  wczytuje się dokładnie jak przedtem. Sprawdzone na hoście na prawdziwym
  magazynie ustawień: każde `DV` od 2.500 do 5.500 co 0,1 mV wraca z
  dokładnością do miliwolta; rekord 392-bajtowy wraca do wartości w
  centywoltach; magazyn builda 56 czyta rekord 396-bajtowy (4.10), a build 57
  odczytuje to, co build 56 potem zapisał.
- **`CT` restartuje pętlę po wycentrowaniu (build 55).** `CT` wystawia na pin
  nowy kod, ale każda pętla trzyma własną kopię tego, gdzie pin powinien być
  — integrator algorytmu 11, absolutny cel algorytmu 10 — i ta kopia wciąż
  miała kod sprzed przemiatania, więc pierwsze kroki pętli po `CT` sterowały z
  powrotem ku niemu i psuły wycentrowanie. `CT` prosi teraz o ten sam restart,
  co zmiana algorytmu. Zmierzone w `spansim` na `CT` udanym, gdy pętla już
  pracowała: bez restartu kod stał dwadzieścia minut później 42 LSB od zera
  `CT`, przy 1,0e-10, a następne przestawienie zworki, przeliczone od tego
  miejsca, wylądowało 1,1e-10 obok; z restartem 1,2e-11 i 3,7e-12. Restart
  siedzi w haku `CT` modułu zakresu, więc build skompilowany bez
  `GPSDO_SPAN_SENSE` — wyłączonego tylko z wyboru, w dostarczanej konfiguracji
  jest włączony — zachowuje stare zachowanie. Build 57 przeniósł restart do
  samego `CT`, dla każdego builda — patrz wyżej.
- **`tools/loopsim/run.sh` drukował odchylenie śledzenia pod nagłówkiem
  odchylenia fazy (build 55).** Linia raportu ma drugie `sd`, odkąd doszły
  statystyki śledzenia (`track sd 0.71 LSB`), a zachłanne `.*sd` w tabeli
  łapało właśnie je: każda tabela wydrukowana przez skrypt od tamtej pory
  pokazywała sd śledzenia w LSB pod nagłówkiem obiecującym sd fazy w ns, i nic
  nie wyglądało źle, bo obie liczby są małe i dodatnie. Po zakotwiczeniu na
  polu przed nim tabela z README znów się odtwarza (okno algo-12, `MG 0`:
  3,70 / 2,93 ns wobec zapisanych 3,57 / 2,95 — drzewo od tego czasu się
  zmieniło). Wnioski wyciągnięte z tabel `run.sh`, odkąd pojawiły się liczby
  śledzenia, warto sprawdzić jeszcze raz; sam program loopsim zawsze liczył
  dobrze.
- **`hostcheck` nie widział brakującej funkcji `span_`, `algo_` ani `health_`
  (build 55).** Kontrola niezdefiniowanych symboli patrzy tylko na nazwy z
  prefiksami firmware'u, a tych trzech nie było na liście: przy definicji
  `span_poll` przemianowanej na inną nazwę każdy wiersz nadal mówił „clean".
  Dodane — razem z listą `DEFAULT_ON` dla przełączników włączonych w
  dostarczanej konfiguracji (`GPSDO_SPAN_SENSE`), żeby istniejące wiersze dalej
  budowały płytki, od których mają nazwy, i dwoma nowymi wierszami, które
  budują bez niego. 18 wierszy, wszystkie czyste pod arm-none-eabi.
- **Algorytm 11 łomotał w EFC przez pięć minut po tym, jak już był w domu
  (build 54).** `s_locked` bramkował przedfiltr fazy — `if (!s_locked) filt =
  1u;` — a `s_locked` nie jest zdaniem o fazie. To stoper: test locka wymaga,
  żeby faza I częstotliwość były w swoich oknach *nieprzerwanie* przez
  `LPF × LTC` sekund, czyli pięć minut na wartościach domyślnych. Pętla, która
  już jest w domu, pozostaje formalnie niezablokowana przez kolejne pięć minut i
  spędzała każdą z tych sekund w trybie szybkim, bez wygładzania, odpowiadając
  pełnym wzmocnieniem na każdą próbkę szumu detektora.

  Znalezione w przechwycie z 11.09, w momencie przekazania z algorytmu 13 do 11
  przy fazie 2,4 ns: **299 sekund `PLL`, przez które filtrowana faza nie
  wyszła poza ±6,7 ns wobec okna 100 ns — a PWM ruszał się średnio o 4,5 LSB na
  sekundę, aż do 17 LSB w jednej sekundzie, i o więcej niż 4 LSB w 145 z tych
  298 sekund.** Siedemnaście LSB to 5,4e-10 na tej płytce. Ta sama pętla po
  zablokowaniu, przez 14,6 godziny: 0 albo 1 LSB w każdej sekundzie, 2 LSB
  czterdzieści razy, nigdy więcej. Rząd wielkości szumu wyjściowego z flagi,
  która znaczyła tylko „jak długo to już jest dobre".

  Przedfiltr idzie teraz za fazą, a nie za stoperem: szybko, dopóki faza jest
  poza oknem, wygładzanie, gdy jest w oknie **i utrzymała się w nim tak długo,
  jak długie jest okno uśredniania filtru**. Ta druga połowa nie jest zbędna —
  sam test okna dawał gorszy wynik, bo faza *wychodząca* też przez okno
  przechodzi, a wygładzanie jej tam to sposób, w jaki pętla dowiaduje się o
  zaburzeniu za późno (harness przełączania zmierzył ekskursję 600 s kończącą
  się na 340 ns zamiast 241). Czas trwania to samo `filt`, i to jedyny wybór
  spójny sam ze sobą: uśrednianie po N sekundach ma sens dokładnie wtedy, gdy
  ostatnie N sekund opisywało to samo.

  **Odtworzone na zmierzonym obiekcie z 26.08 z ustawieniami tej płytki**
  (LTC 60, LFD 3, LPL 100, LPF 5, LG 2,130), na tych 300 s, które pętla spędza
  formalnie niezablokowana z fazą już w domu:

  | | średni krok | najgorsza sekunda | sekundy > 4 LSB |
  |---|---|---|---|
  | przed | 5,88 LSB | 22 LSB | 159 z 300 |
  | po, całe okno | 0,49 LSB | 13 LSB | 7 z 300 |
  | po, gdy warunek czasu jest spełniony | **0,21 LSB** | **1 LSB** | **0** |

  Pierwsze dwadzieścia sekund z założenia zachowuje stare, szybkie zachowanie:
  pętla pozostaje szybka, dopóki nie ma dowodu, że stać ją na wygładzanie.
  Reszta jest bez zmian i została sprawdzona, a nie założona — czasy akwizycji
  identyczne we wszystkich czterech scenariuszach (ustabilizowany, 800 ns poza,
  1500 ns poza, detektor na ograniczniku), sd fazy identyczne na pięciu ziarnach
  szumu, algorytm 12 bit w bit, a harness przełączania wrócił do liczb, które
  drukował przed zmianą.

- **Algorytm 13 kopał DAC sekundę po każdym restarcie (build 54).** Zmierzone na
  stole przy przekazaniu z algorytmu 11 w przechwycie z 11.09 — PWM 40835 →
  40978 → 40871, czyli **krok 143 LSB w jednej sekundzie, 4,6e-9 na wyjściu**,
  na płytce zablokowanej z dokładnością do nanosekundy sekundę wcześniej.
  Prześledzone w symulatorze do prawdziwej przyczyny, która nie była tą
  oczywistą:

  Pierwszy pomiar częstotliwości po resecie to EMA **licznika
  jednosekundowego kwantowanego do pełnych herców**, a `P[1][1]` stoi jeszcze
  na szerokim priorze zimnego startu, więc stan częstotliwości przesuwa się o
  61% drogi do liczby, która jest głównie tętnieniem EMA — 3,79 ns/s, czyli
  119 LSB korekcji na tej płytce. Sterowanie zastosowało to w całości; w
  następnej sekundzie zabrało większość z powrotem. `lim_lsb` to całe pasmo
  detektora i na ustabilizowanej pętli nigdy nie ogranicza, więc ogranicznik
  tylko się temu przyglądał.

  Dwie zmiany, i pomiar mówi, że obie zarabiają na swoje miejsce. **Krótki
  horyzont sterowania nie zatrzaskuje się już na pierwszym odczycie w paśmie**
  — po jednej próbce estymata *jest* tą próbką — tylko czeka, aż filtr uśredni
  horyzont pomiarów, co jest własną skalą czasu pętli, a nie nową stałą. Oraz
  **korekcja jest teraz ograniczona co do szybkości zmian**, a nie tylko co do
  wartości: ogranicznik mówi, jak daleko wolno pójść, ten mówi, jak szybko wolno
  się zmienić — pasmo detektora rozłożone na jeden horyzont sterowania. Na
  ustabilizowanej płytce `du` zmienia się o dużo mniej niż LSB na sekundę, więc
  nigdy nie ogranicza; reszta idzie do tego samego carry, którego używa ścieżka
  sub-LSB, więc ograniczona sekunda jest opóźniona, a nie stracona.

  Najgorszy krok jednosekundowy w pierwszej minucie, obiekt z nocy 03.09, pięć
  ziaren szumu:

  | | z1 | z2 | z3 | z4 | z5 |
  |---|---|---|---|---|---|
  | przed | 48 | 78 | **175** | 120 | 16 |
  | sam limit szybkości | 33 | 52 | 28 | 63 | 16 |
  | obie | **13** | **27** | **9** | **19** | **10** |

  Sd fazy, dPWM, najgorszy krok w całym przebiegu i ADEV przy każdym tau — bez
  zmian.

- **Notka przy `AP` polecała algorytmy, które nie umieją tego, o co prosi
  (build 54).** Mówiła „use a PLL algorithm (LA 4/5/7) to keep phase locked
  long-term" — rada starsza niż algorytmy 10–13. Tamte trzy sterują z licznika i
  w ogóle nie mają detektora fazy, więc nie mogą utrzymać dzielnika tam, gdzie
  postawi go `AP`; pętle LTIC mogą, i same zbroją ponownie, gdy detektor
  powie, że dzielnik stracił synchronizację. Dan Wiering wybrał algorytm 7 na
  nocny przebieg i potem zastanawiał się, skąd te zdarzenia zbrojenia —
  algorytmy 0–9 nie zbroją wcale, a ta linia jest najbardziej prawdopodobnym
  powodem, dla którego tam trafił.

- **`loopsim` nauczył się raportować, co pętla zrobiła z pinem (build 54).**
  Statystyka aktuatora była RMS-em po całym przebiegu, który uśrednia do zera
  transjent trwający jedną sekundę — a jednosekundowy krok to dokładnie to, co
  analizator fazy rysuje jako pik. Teraz drukuje też najgorszą pojedynczą
  sekundę i najgorszą sekundę pierwszej minuty, gdzie mieszkają transjenty
  restartu. Pokrętła algorytmu 11 — `LTC`, `LFD`, `LPL`, `LPF` i `LG` — są
  wystawione jako nadpisania środowiskowe, żeby dało się odtworzyć przechwyt z
  ustawieniami, które płytka naprawdę miała, a zrzut niesie zastosowane
  sterowanie obok fazy. Każda liczba w dwóch powyższych wpisach wyszła z tych
  dodatków.
- **Przegenerowanie tablicy stref przywracało strażnik nagłówka sprzed zmiany
  nazwy (build 53).** Plik stał się `gpsdo_tz_table.h` w restrukturyzacji v1.07,
  a jego strażnik `GPSDO_TZ_TABLE_H`, ale generator nadal pisał `TZ_TABLE_H` —
  więc każde naciśnięcie przycisku cicho to cofało, a `TZ_TABLE_H` to dokładnie
  ten rodzaj ogólnej nazwy, którą bierze też druga biblioteka. Poprawione w
  generatorze i w dostarczonej tablicy.
- **Każde przejście czasu letniego wypadało w złym momencie, o wielkość samego
  offsetu (build 52).** `tz_offset_now()` porównywał godziny przejść strefy —
  które POSIX zapisuje w czasie LOKALNYM — z UTC, a komentarz nazywał tę
  różnicę nieistotną dla zegara ściennego. Nie jest nieistotna i nie jest to
  „mniej więcej godzina", jak twierdził komentarz: błąd równa się dokładnie
  offsetowi obowiązującemu na granicy. Zmierzone względem prawdziwych momentów
  2026, przed poprawką:

  | strefa | wiosna | jesień |
  |---|---|---|
  | Europe/London | dokładnie | **godzinę za późno** |
  | Europe/Berlin, Warsaw | godzinę za późno | dwie godziny za późno |
  | Europe/Athens | dwie za późno | trzy za późno |
  | America/New_York | **pięć godzin ZA WCZEŚNIE** | cztery za wcześnie |
  | America/Denver | siedem za wcześnie | sześć za wcześnie |

  Wiosenna zmiana w Londynie wyszła dokładnie tylko dlatego, że GMT *jest* UTC,
  i ta zbieżność trzymała usterkę w ukryciu: strefa, którą autor sprawdziłby
  jako pierwszą, jest jedyną, w której połowa błędu się skraca. Nowy Jork jest
  tym przypadkiem, którego nie dałoby się przeoczyć — zegar przeskakiwał o
  21:00 w sobotni wieczór, pięć godzin przed całym krajem.

  Nigdy nie było tu błędnego koła, którego bał się stary komentarz. Reguła
  początku jest w POSIX zapisana w lokalnym czasie STANDARDOWYM, a reguła końca
  w lokalnym czasie letnim; offset każdej z nich jest znany przed porównaniem,
  więc każda granica przelicza się na UTC przez odjęcie własnego offsetu. Bez
  iteracji, bez zgadywania. Porównanie idzie teraz po numerze dnia w roku
  zamiast po spakowanej dacie, bo odejmowanie potrafi przekroczyć północ —
  Australia/Sydney startuje o 02:00 AEST, czyli 16:00 UTC dnia *poprzedniego* —
  a granice zawijają się w obrębie roku.

  **Sprawdzone na danych IANA tej maszyny, nie wywodem.** Każda strefa z
  wbudowanej tablicy została przeskanowana minuta po minucie przez cały 2026, a
  jej przejścia porównane z `zoneinfo` Pythona: **416 stref zgadza się teraz co
  do minuty**, podczas gdy przed poprawką ten sam test przewracał się na
  momentach granicznych wszędzie poza UTC. Pięć pozostałych to trzy błędne
  wiersze tablicy opisane w następnym punkcie oraz Casablanca i El Aaiun,
  których czas letni idzie za ramadanem i nie da się go zapisać regułą POSIX —
  co firmware zresztą sam mówi przy wyborze tych stref.

  Znalezione, bo Dave (Solder_Junkie) z EEVbloga zapytał, czy `TZ London`
  przełączy się sam pod koniec października. Przełączał się — godzinę za
  późno — i samo pytanie wystarczyło, żeby ktoś to wreszcie zmierzył.

- **Jasność TM1637 ustawiona na 1 z 7 pod komentarzem mówiącym 5/7 (build
  52).** Gołe `setBrightness(1)` zakopane w zadaniu wyświetlacza, bez żadnego
  sposobu, żeby budujący odróżnił ciemny moduł od ciemnego firmware'u. Teraz
  jest to `TM1637_BRIGHTNESS` w `gpsdo_config.h`, domyślnie 4, obok
  `HT16K33_BRIGHTNESS`, czyli tam, gdzie ktoś będzie tego szukał, i opisane w
  instrukcji w rozdziale o zegarze LED.

  **Obie konfiguracje TM1637 są teraz też kompilowane przez `hostcheck`, a nie
  były.** Tak właśnie ta usterka przetrwała, i nie ona jedna w tym bloku:
  sześciocyfrowa maska dwukropka nadal ma przy sobie komentarz mówiący, że
  wartość używana przez kod nie zapala żadnego dwukropka. Konfiguracja, której
  nikt nie buduje, jest konfiguracją, której nikt nie czyta. Szesnaście
  konfiguracji zamiast czternastu. (LCD 20x4 dalej nie ma wiersza — potrzebuje
  zaślepek `hd44780`, których jeszcze nie ma. Luka jawna, a nie cicha.)

- **Tuner rysował estymatę fazy tylko dla algorytmu 13; teraz rysuje ją każda
  pętla LTIC, która ją ma (build 52).** Algorytm 11 prowadzi filtrowaną fazę
  (`s_phase_filt`, wykładniczy uśredniacz o stałej `time_const/filter_div` po
  zablokowaniu i 1 w akwizycji) i od zawsze drukował ją jako `phase=` — tuner
  po prostu nigdy jej nie rysował, bo algorytmy 10 i 11 dzieliły jedną rodzinę
  wykresów, a to rodzina wybiera nakładkę. Teraz są osobne.

  Panel algorytmu 12 był gorszy niż pusty: pokazywał `ph`, które *wygląda* na
  właściwe pole, a jest surowym odczytem detektora rzutowanym na `int16_t` —
  pomiarem skwantowanym do pełnych nanosekund, podpisanym „błąd fazy". Górny
  panel pokazuje teraz `dph`, ten sam pomiar z miejscem po przecinku, a estymata
  idzie na wierzch.

  Tę estymatę trzeba było najpierw wystawić, bo algorytm nigdy jej nie
  publikował: `mlacc_stats_t` dostaje `est_ns` — własną odpowiedź akumulatora,
  czyli `last_phase` znormalizowane tak, jak normalizuje je sama korekcja —
  drukowane w linii Learn jako `est=`, dopisane na KOŃCU pól algorytmu 12, żeby
  żaden istniejący regex się nie przesunął. Sprawdzone na odtworzeniu z 26.08, a
  nie założone: na 35 korekcjach estymata idzie za prawdziwą fazą z korelacją
  0,992 i nachyleniem dopasowania **1,048** (błąd poziomu dałby 0,5 albo 2,0), a
  jej odchylenie RMS od prawdy to 0,70 ns przy 2,45 ns szumu detektora. Ten
  stosunek jest całą tezą algorytmu i to właśnie pokaże odstęp między dwoma
  przebiegami.

  **Algorytm 10 nakładki nie dostaje, celowo.** Pętla trzystopniowa pracuje na
  surowym odczycie i nigdzie nie trzyma filtrowanej fazy; jej wygładzanie
  siedzi w integratorze PID, a to stan sterowania, nie estymata czegokolwiek.
  Narysowanie go byłoby wymyśleniem estymatora, którego ten algorytm nie ma.

  Opóźnienie nakładki to 1 próbka dla wszystkich trzech. Dla algorytmu 13 było
  zmierzone korelacją wzajemną na 7,5-godzinnym przechwycie; dla 11 i 12 wzięte
  ze struktury — ten sam producent, ten sam odbiorca, ten sam wyścig — i tak to
  jest w kodzie napisane. Kilka minut telemetrii `RH` pod każdym z nich by to
  potwierdziło.
- **Dwa wskaźniki czytały niezainicjalizowaną pamięć, a trzeci przekazywał
  wskaźnik pusty do `strncpy` (build 51).** Obie usterki były osiągalne na
  zwykłej płytce; żadna nie wymagała nietypowej kombinacji przełączników.

  `set_trend(0)` — algorytm 11 i algorytm 13 odmawiają pracy bez kalibracji
  TIC i obydwa mówiły to wywołaniem `set_trend(0)`, czyli
  `strncpy(dest, NULL, 4)`. To zachowanie niezdefiniowane, a nie pusty
  wskaźnik, i ścieżka do niego jest zwyczajna: robi to każda nowa płytka przy
  pierwszym uruchomieniu, zanim `LC` zostało kiedykolwiek wykonane. Obydwa
  ustawiają teraz `NoCT` — słowo, którego algorytm 13 używał już linijkę niżej
  dla drugiego brakującego współczynnika. Stan ma nazwę, a operator dowiaduje
  się, *dlaczego* pętla czeka, zamiast patrzeć na wskaźnik, który może być
  pozostałością po tym, co ostatnio tam zapisano.

  `snap_c` — zadanie wyświetlacza robi migawkę trzech współdzielonych struktur
  pod 5-milisekundowym timeoutem muteksu. Dwie były najpierw czyszczone,
  migawka sterowania nie. Przekroczenie timeoutu nie jest błędem — zdarza się
  zawsze, gdy zadanie sterujące jest w połowie własnej aktualizacji — a gdy się
  zdarzyło, każdy odbiorca poniżej czytał niezainicjalizowaną ramkę stosu:
  żółta dioda brała stąd stan holdoveru, pasek statusu numer algorytmu i
  werdykt lock, a `trendstr` trafiał do raportu szeregowego i na LCD **w ogóle
  bez terminatora**, więc funkcja doklejająca łańcuch kopiowała to, co leżało
  za nim na stosie, aż trafiła na bajt zerowy. Teraz trzymana jest ostatnia
  dobra kopia i to ona jest używana — jedyna odpowiedź zarazem bezpieczna i
  prawdziwa. Wyzerowanie byłoby bezpieczne i dalej twierdziłoby: algorytm 0,
  PWM 0, brak holdoveru, pusty trend — losowo, czyli dokładnie taki rodzaj
  nieprawdy, w który się wierzy.

- **Algorytm 13 nie miał w ogóle werdyktu lock, więc ekran oceniał filtr
  Kalmana po średniej częstotliwości (build 51).** `tft_loop_locked()` wymienia
  algorytmy publikujące żywy stan lock i pyta je; 13 nie było na liście, więc
  spadał do gałęzi pisanej dla algorytmów 0–9, która bada średnią 10 000 s
  (albo 1000 s) — dokładnie to, czego ta funkcja została wydzielona, żeby pasek
  przestał robić. Dopisanie do listy też by nie pomogło: **pętla Kalmana nigdy
  nie emituje `LOCK`.** Cały jej słownik to `KAL` / `REJ` / `NOPH` / `ARM` /
  `WAIT` / `NoCT` / `NoPL`, więc test na łańcuchu trendu nie mógł się dopasować.

  Werdykt pochodzi teraz z filtru i jest liczony tam, gdzie filtr go zna, z
  trzech składników, których żaden inny algorytm tutaj nie ma:

  - **detektor odezwał się w tej sekundzie** (`s_kf_holdover == 0`, co spełnia
    również odczyt odrzucony — `REJ` to bramka innowacji wykonująca swoją
    pracę, a nie utrata wejścia). Bez tego filtr bezbłędnie lecący na własnym
    modelu czyta się jako zablokowany w nieskończoność, a to jest różnica
    między sterowaniem a dryfowaniem;
  - **estymowana faza mieści się w paśmie** — estymata, nie odczyt z tej
    sekundy, bo po to właśnie jest filtr. Pętla ściągająca z 400 ns mówi `KAL`
    w każdej sekundzie, przez którą to robi;
  - **i filtr o tym wie** — `sqrt(P[0][0])` w tym samym paśmie. To jest
    składnik, który czyni werdykt uczciwym w dwóch momentach, w których to się
    liczy, i nie kosztuje nic w żadnym innym: przy zimnym starcie `P[0][0]`
    startuje z `(range/2)²`, a po zbrojeniu picDIV jest celowo resetowane do
    tej samej wartości, bo zero, do którego odnosiła się estymata, przestało
    istnieć.

  Pasmo to `LAT` (`g_ltic.acq_threshold_ns`), które mierzy `LC` i którego ten
  algorytm już używa dla `s_kf_ctl_fast`, dla testu podążania i dla bramki
  cierpliwości przed zbrojeniem. Nic nie jest wymyślone i nic nie jest
  zakodowane na sztywno: płytka o lepiej rozdzielczym detektorze dostaje z `LC`
  mniejsze `LAT` i werdykt zacieśnia się razem z nim.

  **Odtworzone, sześć scenariuszy, obiekt z nocy 03.09 (28 680 s, `LAT`
  200 ns, szum detektora 2,45 ns).** Nowy werdykt ani razu nie zgłosił locka,
  gdy prawdziwa faza była poza pasmem — w żadnym scenariuszu. Co się zmienia:

  | scenariusz | stara reguła mówi lock | nowy werdykt mówi lock | fałszywa zieleń |
  |---|---|---|---|
  | od początku ustabilizowany | t+1000 s | **t+10 s** | 0 s / 0 s |
  | zimny start, 800 ns poza | t+1000 s | t+193 s (do tego czasu wciąż ściąga) | 0 s / 0 s |
  | detektor na ograniczniku | t+1000 s | t+201 s | 0 s / 0 s |
  | detektor zamrożony na +1295 ns | t+1189 s | **nigdy** | **14 459 s** / 0 s |

  Ostatni wiersz jest tym, dla czego warto było to zrobić, i nie jest
  hipotetyczny — detektor zamrożony na +1295 ns to awaria, dla której istnieje
  logika zbrojenia, wzięta z prawdziwego przechwytu. Średnia częstotliwości nie
  może tego zobaczyć, bo *częstotliwość* oscylatora jest w porządku: umarło
  odniesienie fazy. Przez cztery godziny jednej nocy pasek pokazywałby
  `DISCIPLINED  FIX OK` w zieleni locka, z detektorem fazy stojącym 1,3 µs
  poza. Nowy werdykt nie zapala się ani razu.

  `LOOPSIM_TRACE13` drukuje teraz oba werdykty obok siebie (`lock` i `ofrq`),
  żeby następną zmianę któregokolwiek z nich dało się policzyć, a nie
  przedyskutować.

- **Żółta dioda gasła w ręcznym holdoverze po utracie fixa (build 51).** Test
  na OFF brzmiał `(!fix && !hold_auto)`, a to łapie jedyną kombinację, która
  nigdy nie może być ciemna: operator zamroził wyjście komendą `MH`, dioda
  pulsowała wolno, żeby to powiedzieć, i w chwili utraty fixa gasła — nie do
  odróżnienia od płytki, która nigdy nie widziała satelity, dokładnie w
  momencie, w którym zamrożone wyjście jest jedyną rzeczą trzymającą oscylator.
  Ręczny holdover ma tu pierwszeństwo przed fixem, tak jak ma je już w pasku
  statusu. Dokładnie jedna z ośmiu kombinacji wejść zmienia zachowanie;
  pozostałe siedem sprawdzono i nie ruszają się.

- **`LPOL 0` był w trzech miejscach opisany jako „auto", a jest odwrotnie
  (build 51).** Wszystkie trzy pętle LTIC traktują polaryzację 0 jako *odmawiam
  pracy i trzymam* i drukują prośbę o jej ustawienie. Nazwanie tego auto mówiło
  operatorowi, że firmware sam sobie to wyliczy — a to jedyna rzecz, której nie
  zrobi. „Auto", które istnieje, to `LC`, i trzeba je uruchomić. `LPOL`, `LL` i
  tekst pomocy mówią teraz `not set - loop holds`.

- **Korekcje nigdy nie były liczone na algorytmach 12 i 13, a `CS` podawał
  fałszywy powód (build 51).** `counting_now()` umiał zapytać algorytmy 10 i 11,
  czy są zablokowane; 12 i 13 spadały przez `default:` razem z 0–9, więc na
  dwóch najnowszych pętlach — dwóch najbardziej prawdopodobnych na płytce,
  której właściciel dba o tę liczbę — statystyka nie policzyła niczego, a `CS`
  tłumaczył pustkę zdaniem, że płytka „pracuje na algorytmie poniżej 10", co dla
  12 i 13 nie jest prawdą.

  Obydwa algorytmy zawsze umiały odpowiedzieć; żaden nie był pytany. Publikują
  teraz werdykt tam, gdzie go podejmują (`mlacc_locked()`, `kf_locked()`), a
  `CS` wymienia algorytmy, które naprawdę nie mają stanu lock — 0–9. Flaga
  algorytmu 12 celowo przeżywa sekundę `CORR` (korekcja wykonana przez
  ustabilizowaną pętlę to dokładnie to, co ta statystyka mierzy) i celowo
  opada na jedną sekundę skoku `ZC`, który kasuje narzut zadany przez sam
  algorytm i jest poleceniem, a nie korekcją.

- **`LL` drukował stan algorytmu 10 pod każdym algorytmem (build 51).**
  Trzystopniowy `state=ACQ|DPLL|LOCK` jest zapamiętywany, więc pod algorytmem
  11, 12 czy 13 linia raportowała, gdzie trzystopniowa pętla skończyła ostatnim
  razem — możliwe, że w poprzedniej sesji, bo wartość jest odczytywana z flasha
  — i to bez żadnego zastrzeżenia, pośród żywych parametrów LTIC. Teraz drukuje
  się tylko pod algorytmem 10, a poza nim `state=- (algo N running…)`. Stojący
  za tym operator warunkowy był drugą połową usterki: każda wartość, która nie
  była `ACQ` ani `DPLL`, drukowała się jako `LOCK`, więc bajt nigdy niezapisany
  przyjmował najbardziej uspokajający z trzech stanów zamiast najmniej.

- **Podpowiedź drukowana po `CT` wskazywała trend, który nie może się pojawić
  (build 51).** „arm picDIV (AP) after the loop locks (trend `hit`)" — `hit`
  emitują tylko algorytmy 0 i 3–8, nigdy 10–13, a kto właśnie wykonał `CT`,
  pracuje na jednym z tych drugich. Kto potraktował to dosłownie, czekał na
  słowo, które nie mogło nadejść. Podpowiedź mówi teraz *gdy pętla zgłosi lock*
  i zostawia wskazanie wyświetlaczowi oraz `CS`, które wiedzą per algorytm, co
  to znaczy.

- **Trend holdoveru algorytmu 13 przemianowany `HOLD` → `NOPH` (build 51).**
  Trzy niepowiązane stany dzieliły jedno słowo na jednym ekranie: ten (detektor
  nie powiedział nic *w tej sekundzie*), tryb holdoveru operatora albo
  automatyczny drukowany obok jako `[HOLDOVER]`, oraz „holdover — MCU crystal"
  z `SW` dla zegara systemowego pracującego bez PPS. Algorytm 12 nazywał to już
  `NOPH`; nie było powodu, żeby 13 nazywał to inaczej, i był każdy powód, żeby
  nie. Lista słów trendu w instrukcji nigdy nie zawierała `HOLD`, więc
  dokumentacja staje się dokładniejsza, a nie mniej dokładna.

- **Raport tabulatorowy drukował 0.0 dla średnich, które jeszcze nie istniały
  (build 51).** `gpsdo_calc_averages()` liczy każde okno dopiero, gdy się
  napełni; wcześniej pola trzymają początkowe 0.0. Raport czytelny zawsze
  bramkował je tymi samymi flagami — raport tabulatorowy nie, więc pierwsze 10,
  100, 1000, 10 000 i 20 000 sekund każdego logu niosło twarde `0.0` w
  odpowiedniej kolumnie. Na wykresie to nie jest przerwa: to oscylator
  pokazujący zero herców, przeskalowujący oś tak, że wszystko po nim jest płaską
  linią.

  Te pola są teraz **zostawiane puste**, dopóki ich okno się nie napełni.
  Separatory nadal są zapisywane, więc liczba kolumn się nie zmienia
  (22 pola, sprawdzone) i wszystko, co już parsuje ten plik, działa dalej;
  gnuplot, pandas i każdy arkusz czytają puste pole między dwoma tabulatorami
  jako brak danych, czyli to, czym ono jest. Wartość zastępcza — `nan`, `-1`,
  `99999` — byłaby jeszcze jedną liczbą do wytłumaczenia następnemu, kto to
  narysuje.
- **Pasek statusu twierdził, że jest lock, choć nie miał skąd o tym wiedzieć
  (build 50).** Reguła brzmiała: jest fix pozycyjny i nie ma holdoveru, zatem
  `DISCIPLINED  FIX OK`, w zieleni locka. To stwierdzenie o odbiorniku GPS
  ubrane w barwy stwierdzenia o pętli. Płytka w rozgrzewaniu, płytka z
  uruchomionym `CT`, której wyjście jest celowo przemiatane, i płytka trzydzieści
  sekund po starcie akwizycji z mikrosekundami błędu fazy pokazywały ten sam
  zielony pasek co płytka od godziny siedząca w nanosekundzie — największy i
  najbardziej rzucający się w oczy element ekranu był jedyną rzeczą, która nie
  mogła kłamać, i kłamał.

  Potrzebny werdykt już istniał. `tft_loop_locked()` — wyciągnięty z cyfr
  częstotliwości, gdzie jego progi zmierzono na trzygodzinnym przebiegu, a nie
  zgadnięto — jest teraz pytany przez oba, więc pasek i cyfry nie mogą się
  różnić: zielony tutaj znaczy zielony tam, z konstrukcji, a nie dlatego, że tę
  samą regułę napisano dwa razy.

  Cztery stany, których pasek wcześniej nie umiał wyrazić:
  `ACQUIRING  FIX OK` (fix dobry, pętla jeszcze nie zbieżna), `OCXO WARMUP`,
  `CALIBRATING` — oraz dotychczasowa zieleń, znacząca teraz to, co mówi.
  Holdover dalej przebija wszystko, bo zamrożone wyjście jest ważniejszym
  faktem.

  **Oba kierunki są tłumione, niesymetrycznie.** Werdykt locka jest
  jednosekundowy i potrafi mrugnąć na pojedynczym zliczeniu licznika — z tego
  samego powodu próg samych cyfr poszerzono do 0,15 Hz. Natychmiastowa reakcja
  była pierwszą próbą i jest tu błędem z tego samego powodu: jedno zliczenie
  drgnięcia to nie awaria, a pasek migający między zielonym a pomarańczowym
  jest gorszy od każdego z tych kolorów osobno. Pięć kolejnych sekund locka,
  zanim pasek zzielenieje, dwie sekundy straty, zanim go odda.
  `tools/gpsdo_statusbar.py` odtwarza automat na scenariuszu od zimnego startu
  po wyciągniętą antenę; na tym scenariuszu stara reguła twierdziła
  `DISCIPLINED` przez 18 z 33 sekund, kiedy to nieprawda.
- **Nowe pole w ustawieniach nie kosztuje już wszystkich ich ustawień
  (build 49).** `settings_recall()` wymagał, żeby zapisany rekord miał dokładnie
  bieżący rozmiar, więc dopisanie jednego bajtu oznaczało podbicie
  `SETTINGS_VER`, a podbicie oznacza odrzucenie rekordu w całości — PID, LC,
  strefa czasowa, wszystko, za jeden bajt. Plik obchodził to dwukrotnie,
  wykrawając pola z bajtów wyrównania, i jeszcze dwa razy ręcznie pisanym
  `else if` na wersję, każdym przywiązanym do własnego `offsetof`.

  Teraz przyjmuje każdy rekord od `SETTINGS_V6_BYTES` (376, rozmiar, przy którym
  układ zamrożono) do bieżącego `sizeof`. Struktura jest zerowana przed
  odczytem, więc każde pole dopisane od tamtej pory czyta się jako 0 — co każde
  z nich już definiuje jako „nieustawione". `VS` jest pierwszym polem, które z
  tego korzysta, a za nim zostają trzy bajty wyrównania na kolejne dwa. Ścieżka
  zapisu częściowego dostała tę samą regułę, bo zasiew z krótkiego rekordu jest
  teraz bezpieczny z tego samego powodu.

  Sprawdzone kompilatorem docelowym, a nie na oko: `dac_path` dalej na 318,
  `bl_pct` 319, `a12_gain` 320, `dac_vref_cv` 372, `adc_vdiv_h` 374,
  `vsense_src` na 376, `sizeof` 380. Rekord zapisany przez build 48 wczytuje
  się, a jego brakujący bajt czyta się jako szyna 5 V — czyli to, co build 48
  robił.
- **`DV` i `AV` nie zapisywały się same, choć wszystko, co o nich napisano,
  twierdziło inaczej (build 48).** Changelog v1.07, wszystkie trzy instrukcje i
  notatki wysłane osobom budującym płytki z AD5680 obiecywały autozapis; kod
  wołał `cli_manual_save("ES ALGO")`. Wpisane `DV 5.00`, po którym nie
  nastąpiło `ES`, wracało po resecie jako 3,30, a razem z nim wracało
  ostrzeżenie MISMATCH — co czyta się jak usterka sprzętowa, a nie jak
  zgubione ustawienie. Te dwie wartości opisują stopień wyjściowy płytki i
  wpisuje się je raz, przy jej budowie, więc obietnica była słuszna, a kod
  właśnie ją dogonił.

- **Raport `DAC` zawyżał zwykłą ścieżkę PWM o jedną trzecią (build 48).**
  `gpsdo_dac_output_bits()` zwracało dla niej 16, ale na buildzie bez silnika
  ditheru timer rozróżnia **50 000** wartości wypełnienia, a nie 65 536 —
  nośna to 2 kHz z zegara 100 MHz, a 50 000 nie jest potęgą dwójki. Funkcja
  nazywa się teraz `gpsdo_dac_output_steps()` i zwraca liczbę kroków; raport
  drukuje `N-bit` tam, gdzie to dokładna prawda, i `N steps` tam, gdzie nie
  jest. Ta sama klasa błędu co 0,094 µHz oferowane kiedyś na 18-bitowej kości
  i ta sama poprawka: mówić, co robi sprzęt.
- **Drugi przebieg CT mógł dojechać do pełnego kodu (build 47, poprawka do
  drugiego przebiegu dodanego wcześniej w tej wersji).** Dół tego sweepu
  zawsze miał zapas 1000 LSB, góra nie miała żadnego — górny clamp
  powstrzymywał środek przed wyjściem *poza* `65535 - half`, a nie przed
  dojściem do niego, więc wysoko wypadające zero stawiało górny punkt
  pomiarowy dokładnie na krańcu zakresu. Płytka Dana Wieringa z AD5680
  zrobiła to 10.09.2026: zero przy 45 577, połowa rozstawu 20 654, środek
  ściągnięty do `65535 - half`, trzeci punkt na 65 535. Zmierzyło się tam
  czysto — jego trzy punkty były liniowe co do cyfry — ale bufor wyjściowy
  przetwornika jest najmniej liniowy właśnie przy własnym krańcu, a
  kalibracja ustalająca wzmocnienie obiektu dla wszystkich algorytmów to
  ostatnie miejsce, gdzie warto wydawać ostatni LSB zakresu.

  `CT_RAIL_GUARD` (1000 LSB) obowiązuje teraz po **obu** stronach; na tamtej
  płytce sweep staje się 23 227 / 43 881 / 64 535. Nie kosztuje to nic w
  wychyleniu — rozstaw dalej wynosi `CT_TARGET_SWING / K`, przesuwa się samo
  wycentrowanie — a limit rozstawu 60 000 gwarantuje, że oba clampy nigdy się
  nie zderzą (spotkałyby się dopiero powyżej `65535 - 2·CT_RAIL_GUARD` =
  63 535). Linia drugiego przebiegu mówi teraz, który z dwóch przypadków
  zaszedł, zamiast twierdzić „centred on the fitted 10 MHz code" akurat
  wtedy, gdy clamp właśnie ten środek przesunął. Sprawdzone na 271 076
  kombinacjach K i wyprowadzonego zera: najmniejszy odstęp od krańca 0 LSB
  przed zmianą, 1000 po, rozstaw w każdym przypadku bez zmian.

- **Raport `DAC` podawał krok, którego przetwornik nie potrafi zrobić
  (build 46).** Drukował liczbę 16-bitową i 24-bitową na każdej ścieżce, a za tą
  zworką siedzą trzy przetworniki o trzech różnych szerokościach. Na płytce
  Dana Wieringa z AD5680 obie linie były błędne naraz i w przeciwne strony:
  oferowała **0,094 µHz** jako krok, podczas gdy 64 jednostki wartości sterującej
  dają jeden ruch na 18-bitowym pinie i najmniejszy realny to **5,99 µHz** — a
  druga linia podawała liczbę 16-bitową, czterokrotnie grubszą, niż ta kość
  potrafi.

  Wartość sterująca jest 24-bitowa na każdej ścieżce; to, co WYJŚCIE rusza
  jednym zapisem, to szerokość żywego sterownika, i `gpsdo_dac_output_bits()`
  mówi teraz która: **24** na DITH (tablica ditheru uśrednia wartość 24-bitową
  dokładnie, z konstrukcji), **18** na AD5680, **16** na czystym PWM — czyli ta
  sama własność, którą już raportowało `gpsdo_dac_fine_available()`. Raport
  drukuje obie liczby i nazywa je:

  ```
    step: control 24-bit 1 LSB = 0.094 uHz = 9.36e-15
          output 18-bit 1 LSB = 5.987 uHz = 5.99e-13
          (EXT resolves 18 bits; finer requests reach the pin as a
           time average, not as one step)
  ```

  Kolumna z częstotliwością ułamkową musiała się przy okazji nauczyć wykładnika.
  Stałe `e-15` wystarczało, dopóki raport podawał dwie zakodowane szerokości;
  przy 16, 18 i 24 bitach ta sama linia niesie wszystko od 2,4e-12 do 9,4e-15, a
  „2394.9e-15" to nie jest liczba, którą ktokolwiek czyta. `cli_frac_exp()`
  normalizuje mantysę — ten plik nie drukuje żadnych floatów przez `printf`, bo
  Float printf trzeba włączyć w IDE, a bez tego wypisuje „?".

- **Nachylenie kalibracji, którego rampa mieć nie może, i osiem armów, które
  to kosztowało (build 45).** `LC` produkuje dwie liczby ns/V i jedna ogranicza
  drugą, czego nic nie sprawdzało. `range_ns/span` to **średnie** dφ/dV po
  przemiecionym paśmie; dopasowanie w kotwicy mierzy **lokalne** dφ/dV w
  0,632·Vsat. Na rampie, którą ten detektor naprawdę jest —
  `V = Vsat(1 − e^(−φ/τ))` — `dφ/dV = (τ/Vsat)·e^(φ/τ)` rośnie z φ, więc średnia
  po tranzycie sięgającym powyżej kotwicy jest brana w punkcie nad nią i jest
  przez to **większa**. Dla geometrii tej płytki (Vsat 3,29 V, tranzyt
  0,80…3,22 V) wartość w kotwicy powinna wynosić około **0,55×** średniej.
  Nachylenie lokalne powyżej średniej to nie jest nachylenie, które ta rampa
  może mieć.

  Dwie kalibracje tego samego detektora, w odstępie trzech dni:

  | | LNV | LZO | LRN | LNV ÷ średnia z całego tranzytu |
  |---|---|---|---|---|
  | buildy 16–41 | 1252,0 | 2,0809 | 3000,00 | **1,01** |
  | build 42 | 1837,7 | 2,0797 | 2958,75 | **1,50** |

  LZO zgadza się co do 1,2 mV, LRN co do 1,4 %, więc rampa się nie ruszyła —
  ruszyło się tylko nachylenie, i to jedyna liczba dopasowywana z garstki
  punktów wewnątrz `±LTIC_ANCHOR_WIN_V`. Mierzona była przy niestabilnej fazie.

  **Zepsuło się nie nachylenie.** Zepsuła się gwardia nasycenia w
  `ltic_phase_error_ns()`, która wymiaruje użyteczne pasmo jako
  `range_ns / ns_per_volt` — mieszając licznik z całego tranzytu z lokalnym
  mianownikiem. Pasmo skurczyło się z **±1,318 V do ±0,886 V** wokół zera.
  picDIV ląduje fazę tej płytki **1,06…1,28 V poniżej zera**, to jest stały
  offset fizyczny i on się nie ruszył — ale teraz był poza pasmem: każde
  lądowanie czytało się jako railed. Mostek przechwytywania fazy algorytmu 11
  uzbrajał wtedy dzielnik co 20 s — 15 s hold-offu plus 5 s uwięzienia — osiem
  razy, w t = 122, 142, 162, 178, 198, 218, 238, 258 s, aż jedno lądowanie
  wypadło tylko 0,67 V od zera i zostało przyjęte. Pętla weszła w PLL
  natychmiast i po trzydziestu sekundach była zablokowana. **Osiem przerw w
  wyjściu 1 PPS za artefakt kalibracji.**

  LC porównuje teraz obie, zanim którąkolwiek zapisze: nachylenie z kotwicy
  powyżej średniej z całego tranzytu jest zgłaszane i zastępowane średnią. Sama
  kotwica zostaje — dopasowanie Vsat biegnie po całym tranzycie i jest odporne,
  co dokładnie pokazuje zgodność LZO co do 1,2 mV. Na dobrej kalibracji gwardia
  przesuwa LNV o 1 % (1252,0 → 1239), na złej o 33 %, czyli o całą usterkę.

  Nie naprawione tutaj: gwardia w `ltic_phase_error_ns()` nadal wymiaruje pasmo
  z `range_ns / ns_per_volt`, czyli z dwóch wielkości mierzonych różnymi
  definicjami. Pasmo należy do kotwicy — `Vsat = LZO / 0,63212` dało 3,290 V i
  3,292 V na obu kalibracjach, czyli tę samą liczbę przed i po — a przeniesienie
  go tam wymaga nauczenia symulatora, że rampa jest wykładnicza, a nie liniowa,
  zanim wybierze się jakąkolwiek stałą.

- **Log drukował fazę, której panel odmawiał pokazania (build 44).** Obie
  ścieżki wyświetlania wyprowadzają dph z tego samego zatrzaśniętego napięcia,
  ale każda robiła to własną kopią arytmetyki — i raz już się rozjechały, na
  pile. Tym razem poszło o **pasmo**.

  `ns_per_volt` to nachylenie *lokalne*: LC mierzy je w wąskim oknie wokół
  kotwicy, którą stawia na 0,632·Vsat, bo rampa to `V = Vsat(1 − e^(−t/τ))`, a
  wykładnicza nie ma jednego nachylenia. Poza oknem 15–85 % krzywa się już
  wypłaszczyła i odczyt liniowy jest błędny. Panel od pewnego czasu odmawia tam
  druku — pokazuje `ovf` — a raport szeregowy dalej drukował liczbę.

  **Zmierzone na przebiegu 04.09 11:41**, zrobionym w zupełnie innej sprawie.
  Po przełączeniu z algorytmu 13 na 7 faza zaparkowała na tym, co log nazwał
  **+1085 ns**, przy Vphase **2,946 V** — powyżej 2,798 V, czyli górnej granicy
  pasma tego detektora. Panel przez większość godziny pisał `ovf`, a log +1085
  ns. I to nie było „blisko szyny". Pierwsze różnice tej samej płytki w dwóch
  położeniach:

  | gdzie siedział odczyt | Vphase | podłoga biała | p99 z \|Δ\| |
  |---|---|---|---|
  | środek pasma (algo 13) | 2,08 V | **2,6 ns** | 5,2 ns |
  | zaparkowany przy górze (algo 7) | 2,94 V | **7,7 ns** | 20,6 ns |

  **Trzy razy więcej szumu**, wyłącznie z położenia na rampie — a bias idzie w
  stronę pochlebną, bo kompresja oznacza, że prawdziwa faza była *większa* niż
  wydrukowana liczba. W logu nie było o tym ani słowa.

  Obie ścieżki wołają teraz jedną funkcję, `ltic_display_phase()`, która niesie
  zero, zmierzone nachylenie, zatrzaśniętą piłę i pasmo razem. Poza pasmem
  linia szeregowa drukuje `dph:ovf`, tym samym słowem co panel. Każdy skrypt
  czytający te logi dopasowuje `dph:` plus cyfry, więc poza pasmem nie znajdzie
  odczytu — co jest prawdą — zamiast wiarygodnej liczby błędnej w znanym
  kierunku. Surowe `Vphase:` stoi tuż obok i mówi, którym końcem wyszło.

  Ta sama usterka jest w aktach dwa razy z drugiej strony: nieruchome
  „+1561 ns" i nieruchome „+1295 ns", oba wzięte za dobre odczyty, oba kosztowały
  pomiar, zanim ktokolwiek zauważył. **Odczyt błędny jest do odratowania; odczyt
  błędny i wyglądający spokojnie nie jest.**

  Nie naprawione tutaj i warte osobnego spojrzenia: własna gwardia pętli
  (`railed_now`) testuje zakodowane 3,28 V, więc na detektorze saturującym przy
  2,9 V nie odpala wcale i filtr nadal działa na odczytach, które wyświetlacze
  właśnie nazwały spoza pasma.

- Manual: wiersz `GPSDO_DAC_EXT` w tabeli opcji budowania nadal twierdził, że
  define wyklucza się z `GPSDO_PWM_DITHER` — nieaktualne sformułowanie sprzed
  wprowadzenia wyboru ścieżki w czasie działania. Kompilują się razem; żywą
  ścieżkę wybiera komenda `DAC`, a sygnał prowadzi zworka.

### Odrzucone
- **Wstrzymywanie wyjścia pętli, dopóki pozycja zakresu nie ma własnej
  kalibracji.** Tak robiła pierwsza wersja modułu zakresu, w przekonaniu, że
  wzmocnienie z drugiej pozycji — 5× obok — jest większym złem. Zmierzone
  zamiast założone (`loopsim` z nowym `LOOPSIM_KALL`, trzy obiekty, po pięć
  ziaren, algorytmy 10–13, przy LTC 100 i 60): przy piątej części właściwego
  wzmocnienia każda pętla była 2,2–6,4× gorsza w sd fazy; przy 4–5,7×
  właściwego wzmocnienia algorytm 11 wypadł 3,6–6× *lepiej*, 13 od 0,6× do
  2,2× z pikami 100–250 ns, 12 trzy do siedmiu razy gorzej, a 10 w porządku
  przy 4× i z wychyleniami 500–870 ns w dwóch przebiegach z piętnastu przy
  5,7×. Żaden się nie rozbiegł. A `spansim` pokazał, ile wstrzymanie kosztuje,
  gdy `CT` nie może się udać: przestawiona na REDUCED przy starcie, z `CT`
  zawodzącym przez trzy godziny, wstrzymana płytka stała przy 3e-8 ponad pięć
  godzin i przeszła 570 µs fazy, a ta sama płytka zostawiona na
  współczynnikach FULL złapała lock w 58 minut. W normalnym przypadku oba
  warianty są identyczne — `CT` startuje od razu, a pętla nie pracuje, gdy ono
  przemiata — więc wstrzymanie miało znaczenie tylko tam, gdzie szkodziło.
  Algorytmów 3–7 loopsim nie napędza i nie zostały zmierzone; nagłówek modułu
  mówi to wprost.

- **Podniesienie nośnej zwykłego 16-bitowego PWM do 12,2 kHz ditheru.**
  Pozwoliłoby to podnieść wraz z nią częstotliwość graniczną filtru — sześć
  razy, przy dwóch biegunach — i na płytce ze zwężonym zakresem jest to warte
  zachodu. Tyle że tam, gdzie ma to znaczenie, już tak jest: przy włączonym
  `GPSDO_PWM_DITHER` `dac_emit()` prowadzi zwykłą ścieżkę przez
  `pwm24_write(code24 & 0x00FFFF00)`, więc obie ścieżki dzielą już jedną nośną
  TIM4 przy 12,2 kHz, a przełączenie między nimi zmienia wyłącznie tablicę.
  `analogWrite()` przy 2 kHz przetrwało tylko w buildzie bez ditheru — i to
  właśnie ta konfiguracja, w której ten interes jest zły, bo tam szerokość
  samego PWM **jest** całym wyjściem: 15,6 → 13 bitów podnosi krok z 5,0e-11
  do 3,0e-10 na płytce 3,3 V. Zły kierunek. Zapisane w miejscu kodu w
  `gpsdo_dac.cpp`, żeby nie wracało; `tools/carrier.py` ma liczby dla obu
  płytek i trzech nośnych, wraz z własnymi liniami 6-18 Hz tablicy ditheru,
  które stają się przypadkiem wiążącym powyżej granicy ~16 Hz i nie przesuwają
  się wraz z nośną.

- **Uzbrajanie picDIV pod algorytmami 3–9, żeby pokazywać tam dph.** Większość
  już działa — `ltic_read_fast()` chodzi przy każdym impulsie niezależnie od
  algorytmu, a obie ścieżki wyświetlania są bramkowane na LC, nie na pętli —
  więc pytanie brzmiało tylko, czy uzbrajać dzielnik i uzbrajać go ponownie, gdy
  faza wychodzi. Przebieg 04.09 odpowiada, i odpowiedź brzmi nie.

  Zablokowane w algorytmie 13, potem przełączone na 7: faza opuściła ±100 ns w
  dziesięć minut, jechała do **2,2 ns/s (2,2e-9)**, kiedy LRN jeszcze zbierał, a
  PWM machnęło 353 LSB — po czym **zaparkowała na +1085 ns i tam została**. Przez
  ostatnie 27 minut jej dryf wynosił `+3,1e-13 ± 1,5e-12`: algorytm 7 trzyma
  częstotliwość znakomicie i nie ma żadnego mechanizmu na offset fazy, który
  zostawił po sobie transjent.

  Kadencji nie ustala więc dryf — od świeżego lądowania przy 3e-13 rampa
  starczyłaby na tygodnie. Ustalają ją **zdarzenia**: każde przełączenie,
  ponowna nauka czy zaburzenie potrafi wydać trzecią część pasma w kilka minut —
  a każdy re-arm zatrzymuje wyjście picPPS na `PICDIV_ARM_MS` i oddaje je
  przesunięte o offset lądowania, czyli −900…−1650 ns na tej płytce.

  To jest argument, który zamyka sprawę: **faza, na której parkuje pętla
  wyłącznie częstotliwościowa, jest pamięcią po ostatnim zaburzeniu, a nie
  własnością oscylatora — a monitor, który uzbraja się ponownie, żeby ją
  utrzymać na ekranie, zmieniłby ją w pamięć po ostatnim armie.** Zmieniałby to
  samo wyjście, o którego fazie twierdzi, że raportuje. Pod 10/11/13 płacimy to,
  bo pętla posiada fazę; pod 3–9 nikt jej nie posiada.

  Wartość diagnostyczna jest realna i da się ją mieć bez tego wszystkiego:
  zablokuj w 13, przełącz, loguj, policz nachylenie. Zajęło 52 minuty i zmierzyło
  to, czego żaden licznik na tej płytce nie umie — błąd algorytmu 7 w stanie
  ustalonym jest trzy dekady poniżej kwantu średniej 1 ks, a na końcu tamtego
  przebiegu średnia 10 ks czytała jeszcze −0,0041 Hz, bo niosła transjent sprzed
  czterdziestu minut.

### Dokumentacja
- Manual: nowy blok „Trzy ścieżki, jeden węzeł" w sekcji Wyjście — jak
  współistnieją `PWM`/`DITH`/`EXT`, jedna komenda 24-bit z widokiem 16-bit
  oraz relacja 18 bitów AD5680 do 24-bitowej ramki SPI
  (`code18 = code24 × 262143 / 16777215`, skalowanie, żeby współczynniki
  przenosiły się między ścieżkami). Po pytaniach budowlanych Dana Wieringa.
- Manual: `SPAN` w spisie komend, `GPSDO_SPAN_SENSE` w tabeli przełączników
  i blok „Dwa zakresy, dwie kalibracje" w sekcji Wyjście. `tools/loopsim`:
  `LOOPSIM_KALL` — cały zestaw wyliczany przez `CT` z błędnego K.
- Zakładka **Help** tunera: `SPAN` i `SPAN CLR` (build 56), a w README_TUNER
  `SPAN` wśród komend opisujących płytkę. Nowa nazwa w przykładzie baneru w
  manualu, na szkicu nagłówka TFT i w przykładzie linii stanu tunera.
- Manual i zakładka **Help** tunera: `DV` zachowuje trzy miejsca po przecinku
  (build 57). Zapis `v1.07.57rt` w przykładzie baneru w manualu, na szkicu
  nagłówka TFT, w przykładzie linii stanu tunera i w tytułach dokumentów.

## [v1.06-rtos] — wydane 2026-09-05 (build 42)

Wydane jako build 42. Wpisy trafiały tutaj wtedy, gdy zostały zmierzone, a
nie wtedy, gdy zostały napisane.

### Dodane

- **Manuale: Aneks D — filtr Kalmana prostymi słowami; KC udokumentowane
  (wszystkie trzy języki).** Nowy aneks bez wzorów tłumaczy algorytm 13 na
  intuicję: trzy przekonania i ołówki niepewności, dwaj świadkowie
  (detektor i TIM2), KT-vs-KC jako rozum-kontra-ręce, mechanizmy
  sceptycyzmu (bramka 4σ, test zaufania, cisza po armie, cisza TIM2 po
  restarcie), jak wygląda dobra noc i kiedy nie ruszać pokręteł. Sekcja
  4.6a opisuje teraz rozdzielone prawo sterowania (`faza/KC`, skuteczne po
  pierwszym zablokowaniu), zmierzone R (~2,9 ns) z osobną strukturą
  wędrówki zera ~2,6 ns/45 s, ratio adaptacji i znaki wodne Q w KL oraz
  cztery celowe sekundy HOLD po armie.
- **`KC` — horyzont regulatora, oddzielony od horyzontu estymatora.** Jedna
  liczba robiła w algorytmie 13 dwie rzeczy: sufit Q `R/T³` ustawia, jak szybko
  wolno biec **estymatorowi**, a `x0/T` w prawie sterowania ustawia, jak szybko
  **regulator** zeruje błąd fazy, o którym już wie. Notatka przy komendzie `KT`
  mówiła wprost, że to dwie różne rzeczy — i dodawała, że rozdzielenie ich
  „wymaga Sg zmierzonego na oscylatorze, co wymaga wzorca, którego ta płytka nie
  ma". To było nieprawdą. Wymaga drugiej zmiennej.

  **Co powiedział pomiar, zanim powstał pomysł.** W zapisie z 03.09 14:17, w
  stanie ustalonym i w Time Mode, estymata filtru korelowała z odczytem detektora
  na poziomie **r = 0,822 przy przesunięciu −1 s** — czyli kadencji pomiaru, czyli
  bez żadnego opóźnienia — a po odjęciu estymaty zostawało **2,52 ns**, co jest
  białą podłogą detektora z dokładnością do 1 %. Czyli `dph = ph + biały szum`:
  filtr **widzi** cały błąd fazy, 3,27 ns. A ten błąd jest wolny — średnia `dph`
  w oknie 100 s ma nadal sd **3,06 ns**, czyli 69 % amplitudy przeżywa pełny
  horyzont uśredniania. Nic nie było źle estymowane. Regulator po prostu
  postanawiał nie korygować tego, co estymator już znalazł.

  **Dlaczego rozdzielenie jest darmowe.** `x0` to estymata, nie pomiar. Jej
  własny błąd to `sqrt(P00)` ≈ 1,05 ns przy sygnale 3,3 ns na tej płytce, więc
  szybkie zerowanie **nie wzmacnia białego szumu** — filtr już go usunął. Ile
  wolnego kłamstwa detektora zostaje uznane, decyduje wyłącznie `Q/R`, a `KC`
  nie rusza `Q/R`. Jest jeszcze drugi efekt w tę samą stronę: prawo sterowania
  **wpisuje własną korektę do stanu częstotliwości**, więc horyzont tolerujący
  stały błąd fazy przez 100 s zaszumia `x1` przez 100 s. Szybsze zerowanie to
  usuwa — i dlatego śledzenie *częstotliwości* poprawia się tak samo jak faza.

  **Zmierzone**, plant nocny zrekonstruowany z ośmiogodzinnego zapisu z 03.09,
  osiem ziaren, `KT = 100` wszędzie, `KC = 100` (stare zachowanie) wobec
  `KC = auto = KT/3 = 33 s`:

  | warunek | phase sd | błąd sterowania | r | ruch DAC | Q |
  |---|---|---|---|---|---|
  | czysty detektor | 24,72 → **4,46** | 2,20 → **0,78** | 0,637 → 0,913 | 0,619 → 0,675 | bez zmian |
  | dryf 2,8 ns / 60 s *(ta płytka)* | 15,06 → **4,01** | 1,56 → **0,59** | 0,759 → 0,945 | 0,680 → 0,862 | 9,4e-6 → 8,5e-6 |
  | dryf 8 ns / 60 s | 18,98 → **8,14** | 1,89 → **1,14** | 0,688 → 0,833 | 0,817 → 1,193 | bez zmian |
  | dryf 12 ns / 300 s | 23,35 → **12,24** | 2,15 → **1,47** | 0,655 → 0,764 | 0,756 → 1,042 | bez zmian |
  | zimny start z szyny | settled 266 → **108 s** | tyle samo armów | | | |

  Lepiej na obu plantach, przy każdym poziomie dryfu detektora i w akwizycji —
  włącznie z przypadkiem 12 ns, gdzie skracanie samego `KT` wychodziło *gorzej*,
  bo tamta droga przyspiesza też estymator, a to estymator kopiuje kłamstwo.
  Wobec skrócenia `KT` do 40 s, które daje tę samą phase sd przy tym samym ruchu
  DAC-a, `KC = 33` zostawia **Q na 8,5e-06 zamiast 1,64e-05** — o połowę mniejszy
  szum procesu, czyli o połowę mniejszą skłonność do podążania za detektorem — i
  zostawia długie uśrednianie oraz stan starzenia, które płacą za holdover,
  dokładnie takimi, jakie były.

  Domyślna wartość to `KT/3`, a nie stała, żeby skalowała się z horyzontem i nie
  była liczbą dopasowaną do jednego oscylatora. Zmierzone kolano leży w 20–30 s
  na płytce, której dryf detektora ma `tau ≈ 60 s`; `KT/3 = 33 s` trafia w nie.
  `KC` przyjmuje 10–10000 s albo 0 dla auto, i ostrzega w obie strony: powyżej
  ~60 s mówi, że pętla toleruje stały błąd fazy i wpisuje go do stanu
  częstotliwości, poniżej ~15 s — że faza przestaje się poprawiać, a DAC rusza
  więcej. `KL` pokazuje wartość obowiązującą.

  Rekord we flashu rośnie z 12 do 14 bajtów, a jego wersja z 1 na 2. **Rekordy w
  wersji 1 nadal się wczytują** — odrzucenie ich byłoby o dwie linijki krótsze i
  po cichu zerowałoby operatorowi `KR`/`KQ`/`KT` przy jednej aktualizacji, która
  nie miała powodu ich dotykać.

### Naprawione

- **Blokada była o jeden cykl za krótka, bo liczenie zaczyna się od żądania, a
  żądanie to nie pin (build 42).** Trzy to liczba odczytów z szyny, które *widać*
  w logu. Pętla widzi o jeden więcej. `ltic_arm_picdiv()` ustawia tylko bit
  zdarzenia; task kontrolny łapie go przy następnym przebudzeniu i ściąga pin,
  trzyma go przez `PICDIV_ARM_MS` = 1001 ms — celowo tuż ponad sekundę, żeby
  zwolnienie wypadło po zboczu, a nie ścigało się z nim — a dzielnik synchronizuje
  się dopiero na następnym 1PPS. Fazą jest dopiero rampa po tym.

  Build 41 przepuścił czwarty odczyt przy obu swoich armach:

  | arm w cyklu | szyna w | pętla zjadła |
  |---|---|---|
  | A = 101 | A+2, A+3, A+4 | **1377,0 ns** |
  | A = 661 | A+3, A+4 | **1361,5 ns** |

  Zwróć uwagę, gdzie szyna się *zaczyna*: A+2 w jednym, A+3 w drugim, bo jitter
  siedzi w przebudzeniu taska. I gdzie się *kończy*: **A+4 w obu**, bo koniec
  wyznaczają przytrzymanie 1001 ms i resynchronizacja, a te są deterministyczne.
  Cztery to więc nie trzy plus margines — to cykl, na którym szyna realnie się
  kończy, dwa razy.

  **Zmierzone.** Symulator też tego nie widział, i z powodu, który warto zapisać:
  jego model arma dekrementował licznik i lądował fazę w tej samej iteracji, więc
  `ARMSETTLE=n` dawało `n−1` cykli szyny, a model proszony o cztery dawał trzy —
  dokładnie tyle, ile blokada buildu 41 już pokrywała, więc przeciek
  reprodukował się jako nic. Po naprawieniu tego przesunięcia o jeden i ustawieniu
  transjentu na cztery cykle, które realnie widzi pętla, dwadzieścia cztery
  ziarna na nocnym plancie:

  | | ustalenie mediana | ustalenie najgorsze | army najgorzej | odrzuty mediana |
  |---|---|---|---|---|
  | build 41 | 240 s | **2154 s** | 6 | 12 |
  | build 42 | **220 s** | **347 s** | 3 | **2** |

  Jeden przepuszczony odczyt jest wart **+454 LSB** skomenderowanej korekty w
  symulatorze — na sprzęcie około 430 — a potem pętla odrzuca prawdziwe odczyty
  tak długo, aż bramka otworzy się wokół estymaty fazy oddalonej o 2570 ns od
  prawdy.

  **A koszt bycia o jeden za długim to nic.** Puszczone na transjencie
  trzycyklowym, gdzie build 42 blokuje odczyt, którego nie musiał: ustalenie
  mediana 220 s tak czy inaczej, najgorsze 300 s wobec 297 s. Trzy sekundy w
  najgorszym z dwunastu ziaren. Bycie o jeden za krótkim kosztuje komendę
  454 LSB i, na jednym ziarnie z dwudziestu czterech, całą akwizycję.

- **Odczyt zrobiony przy zatrzymanym dzielniku nie jest fazą (build 41).**
  Uzbrojenie picDIV zatrzymuje jego wyjście i czeka na następne zbocze 1PPS.
  Rampa LTIC jest przez cały ten czas próbkowana i — nie mając czego zatrzymać —
  czyta przy swoim szczycie. Ten odczyt jest w paśmie, skwantowany, niesie
  nanosekundę jitteru: nie ma w nim niczego, do czego bramka innowacji albo filtr
  mogłyby się przyczepić. Po prostu nie jest fazą. Sześć armów w nocnym
  przebiegu 03/04.09, trzy sekundy po każdym i czwarta:

  | arm w | +1 | +2 | +3 | +4 |
  |---|---|---|---|---|
  | t+102 | 1425,4 | 1378,0 | 1378,0 | **−1453,4** |
  | t+445 | 1437,5 | 1381,0 | 1381,0 | **−947,1** |
  | t+510 | 1437,5 | 1381,0 | 1381,0 | **−1106,4** |
  | t+575 | 1437,5 | 1381,0 | 1381,0 | **−1740,9** |
  | t+1597 | 1444,7 | 1394,9 | 1397,0 | **−1326,9** |
  | t+2202 | −1357,5 | 1378,1 | 1380,3 | **−1320,8** |

  Te same trzy liczby za każdym razem, bo to szyna, a nie pomiar — a potem
  lądowanie, dokładnie tam, gdzie model arma mówi, że ma być.

  **Ile to kosztowało.** Przy pierwszym armie tego przebiegu filtr był świeżo po
  resecie, więc bramka stała otworem na `P00 = (range/2)²` i wzięła wszystkie
  trzy odczyty z szyny za fazę. Skomenderowała **+1653 LSB w 105 s**. TIM2
  mówił, że płytka jest w granicach **0,01 Hz**, kiedy pętla zaczynała; kiedy
  skończyła, płytka była **0,50 Hz** obok — pięćdziesiąt razy więcej niż własne
  pasmo bramki arma, wstawione tam przez samą pętlę. Faza przeszła przez
  detektor z prędkością około 100 ns/s, rampa poszła na szynę i test zaufania ją
  skazał — słusznie, bo nie podążała. Trzy kolejne army nie mogły pomóc, bo
  skazana pętla nie steruje, a więc nie może cofnąć błędu częstotliwości, który
  wciąż wystawia detektor na szynę. Wyjście dało dopiero wygaśnięcie
  trzydziestominutowe: **2373 s holdoveru na płytce, która była zablokowana w
  chwili włączenia.**

  Naprawa to ta sama, którą GLM-5.3 Max napisał build wcześniej dla TIM2,
  zastosowana do drugiego pomiaru z tego samego powodu: **żadnego odczytu fazy
  przez trzy sekundy po armie.** `raw` fałszywe jest uczciwym opisem — detektor
  nie powiedział nic, co jest prawdą, a każdy odbiorca w dole strumienia już wie,
  co z tym zrobić. Przechwytywanie lądowania łapie wtedy czwarty odczyt, czyli
  ten, którego bramka arma chciała od początku i nie dostała ani razu.

  **Zmierzone.** Symulator nie mógł tego zobaczyć, bo jego arm lądował
  natychmiast — czwarty pochlebny model znaleziony w tym pliku, po idealnym
  TIM2, złym napięciu szyny i lądowaniu w zerze. Z transjentem zamodelowanym
  z powyższej tabeli, dwadzieścia cztery ziarna na nocnym plancie, faza
  startująca tam, gdzie startował przebieg:

  | | ustalenie mediana | ustalenie najgorsze | army najgorzej | sd fazy najgorzej | odrzuty mediana |
  |---|---|---|---|---|---|
  | build 40 | 514 s | **nigdy (28680 s)** | 51 | **9495 ns** | 89 |
  | build 41 | **218 s** | **297 s** | 2 | **0,55 ns** | 2 |

  Dziesięć z dwudziestu czterech ziaren w ogóle nie osiągnęło akwizycji przed
  zmianą; po zmianie żadne. Poza akwizycją zmiana jest nie tyle mała, co
  **bitowo identyczna** — te same liczby co do cyfry na pięciu ziarnach bez arma
  i na plancie z 26.08 — a przypadek zamrożonego detektora nadal kończy się
  skazaniem (50 armów tak czy inaczej).

- **Jeden kwant to nie cały szum referencji (build 40).** Podłoga kwantowa
  dodana build wcześniej jest właściwa co do rodzaju i za mała co do wielkości.
  Zmierzone na tym samym przebiegu, dla którego powstała: okna bezpośrednio przed
  fałszywym wyrokiem niosły `|aexp|` do **83 ns** — dwa i pół kwantu, bo
  bramkowana średnia zeszła do −0,04 Hz. Przy podłodze 32 ns to okno nadal
  skazuje: `amov` 20,0 ns wobec `0,25·aexp` = 20,7. **Wyrok, któremu ta podłoga
  ma zapobiegać, jest tym, który przez nią przechodzi.**

  Podłoga bierze się teraz z własnego zmierzonego rozrzutu referencji, a nie z
  kroku jej wyświetlania. `Rf` to wariancja `z_f`, filtr już ją liczy dla
  aktualizacji TIM2; trzydzieści dwa odczyty w oknie pochodzą ze studniowego
  boxcara i dzielą prawie całą zawartość, więc skumulowany szum to `W·σ`, a nie
  `sqrt(W)·σ`. Na tej płytce to 32 × 2,9 = **93 ns**, co przewyższa zdarzenie
  83 ns o dwanaście procent i podąża za anteną zamiast być stałą.

  To test jednosigmowy i celowo: dwa sigma dałyby 186 ns i oślepiłyby kontrolę
  zamrożonego detektora, dla której cały ten test istnieje. Koszt jest nazwany —
  zamrożony detektor potrzebuje teraz realnego offsetu około 0,03 Hz, żeby dało
  się go skazać — a poniżej tego nie ma ruchu fazy do przeoczenia. Symulator o
  tej zmianie milczy (stan ustalony 3,94 ns i dPWM 1,305 tak czy inaczej), bo
  jego TIM2 jest czysty i `aexp` nigdy nie zbliża się do żadnej z podłóg; to
  usterka wyłącznie sprzętowa, a powyższy pomiar jest jej dowodem.

- **Test zaufania skazywał zdrowe detektory na kwantyzacji TIM2 (build 39).**
  Oczekiwany ruch fazy w oknie pochodzi z `z_f = -100 x avg100`, a średnia
  100 s chodzi kwantami 0,01 Hz - jeden kwant biasu to 32 ns „oczekiwanego
  ruchu" w oknie 32 s, ponad starym progiem `4*sqrt(2R) ~ 16,5 ns`.
  Oscylator parkujący przy granicy kwantyzacji przy spokojnym GPS otwierał
  test na fantomie, zablokowana pętla nie ruszała się i trzy okna skazywały
  detektor: 30 minut HOLD przy zdrowych odczytach w paśmie (03.09 20:11;
  to samo już 02.09 10:59). Próg pokrywa teraz jeden krok rozdzielczości
  samej referencji: `max(4*sqrt(2R), trust_ns, 100 * 0,01 Hz * KF_TRUST_W)`.
  Koszt powiedziany wprost: zamrożony detektor potrzebuje realnego offsetu
  powyżej jednego kwantu, zanim test go w ogóle zobaczy.
- **KC nie działa już podczas akwizycji (build 39).** Po rozdziale horyzontów
  (KC = KT/3) zimne lądowanie ramienia w połowie pasma komendowało
  1278/33 = 39 ns/s nullowania i boot jechał limiterem przez sześć odbić od
  szyn i 116 rejectów. KC czeka teraz na jednokierunkowy zatrzask - faza w
  paśmie akwizycji na odczycie, którego filtr używa - po czym zostaje na
  stałe; restart `LA n` zaczyna od nowa. Guard EMA R i patience dla
  zamrożonego detektora podążają za horyzontem w mocy, więc podczas
  akwizycji wracają do zachowania sprzed rozdziału (KT).
- **Pierwszy update TIM2 po resecie nie wierzy już w transient power-on
  (build 39).** `kf_reset()` obsadza P11 szeroko, więc pierwszy update dawał
  starej średniej 100 s (wciąż z zaciągiem oscylatora do częstotliwości)
  wzmocnienie ~0,9 i zapisywał częstotliwość, której płytka już nie miała -
  f = -29513 ps/s sekundę po armie z boota 03.09, odwoływane przez limiter
  przez dziewięć minut. Update'y TIM2 są wyciszone przez pierwszy boxcar
  (100 s) po resecie; bramka arm i test zaufania czytają `z_f` bezpośrednio
  i są nietknięte. Armie poszerzają tylko P00 i nie odpalają wyciszenia.
- **Średnia innowacji niosła dociąganie przez godziny, a adaptacja Q na niej
  działała.** Arm ląduje fazę tysiąc nanosekund od zera, więc innowacje w
  akwizycji są tego rzędu, a ich *kwadraty* milion razy większe od wartości w
  stanie ustalonym. `s_kf_ms_innov` to EMA ze stałą 0,001, więc potrzebuje około
  trzech godzin, żeby o tym zapomnieć. Odtworzone z zapisu 03.09 18:35 osiągnęło
  szczyt **3,0e+05** i 5100 s później, na przebiegu spokojnym od t+446, nadal
  pokazywało **1,6e+03** wobec prawdziwej wartości **9,1**. Własne `KL`
  firmware'u raportowało `ratio 31.55` na przebiegu, którego ostatni tysiąc
  sekund mierzy **1,7**.

  To nie jest usterka wyświetlania. Adaptacja *działa* na tym ilorazie, więc **Q
  było spychane na sufit przez godziny po każdym starcie przez innowacje należące
  do dociągania** — i dlatego prawie każdy zapis w historii tego projektu
  pokazywał `[at ceiling]`, a jedynym, który nie, był ten ośmiogodzinny. To ten
  sam kształt co błąd widełek dwa buildy wcześniej i ukrywał się dłużej, bo
  liczba, którą psuje, wygląda wiarygodnie.

  EMA startuje teraz razem z `tracking`, na tej samej zatrzasce co widełki, i
  jest **zasiana wartością `S`**, a nie zerowana: `S` to wartość, jakiej spójny
  filtr oczekuje po `y²`, więc adaptacja otwiera się przy ilorazie 1 i rusza
  wyłącznie na dowodach zebranych podczas śledzenia. Zmierzone na całym
  stanowisku — stan ustalony, czysty detektor, dryf 8 ns, zimny start z szyny,
  start z −1300 ns — bez zmian ponad szum ziaren, bo model armu w symulatorze
  jest łagodniejszy niż sprzętowy i jego transjent akwizycji nigdy nie był
  problemem.

- **Strażnik zamrażający estymator R dzielił przez `KT`, podczas gdy sterowanie
  prosiło o `x0/KC`.** Komentarz strażnika wiąże go z „dokładnie tym, o co
  poprosi `u` poniżej"; od build 36 jest to `x0/KC`, więc pozostawiony na `KT`
  zaniżał komendowaną prędkość zerowania o `KT/KC` = 3 przy domyślnym podziale —
  ruch, który liczył jako 0,4 ns/s, był naprawdę 1,2 i powinien był zamrozić EMA.
  Stan ustalony jest nietknięty, bo `x0` jest małe, ale każdy transjent zerowania
  karmił R i `ms_diff1` trzykrotnie szybciej, niż zamierzano, a `KC` czyni te
  transjenty trzykrotnie stromszymi. Znalezione przez GLM-5.3 Max przy czytaniu
  build 36 wobec tego komentarza.

  Wysłane na argumencie: stanowisko ledwo to widzi (rusza się tylko start z
  błędem częstotliwości, rejekcje 6 → 4), bo te planty zawierają zimne starty, a
  nie odbudowy po epizodach, jakie produkuje prawdziwy GPS. `tools/episode_r.py`
  mierzy różnicę na sprzęcie — o ile R rośnie ponad średnią sprzed epizodu — a
  punkt odniesienia z build 35 to **mediana +0,074 ns, 90. percentyl +0,143,
  najgorszy +0,306** na szesnastu epizodach.

  `patience` poszło z nim na `KC`, bo jego komentarz wprost mówi o sterowaniu.
  Bramka arm zachowuje `KT` celowo: pyta, jak daleko dryf zaniesie lądowanie,
  zanim pętla uzyska nad nim władzę, co nie jest pytaniem o prędkość zerowania, a
  `KC` wpuściłoby tam *więcej* armów.

- **Pomiar fazy i woltomierz serwisowy dzieliły jeden przetwornik ADC bez
  żadnej blokady, a stanowisko złapało to najpierw od nieszkodliwej strony.**
  `PA1` (rampa LTIC) to `ADC1_IN1`, a `PIN_VCTL_ADC` (`PB1`) to `ADC1_IN9` — na
  tej kości jest jeden ADC — a `analogRead()` z rdzenia rekonfiguruje kanał na
  współdzielonym uchwycie i nie jest reentrantne. `ltic_read_fast()` biegnie z
  zadania budzonego przez PPS; `ControlTask` czyta Vctl/Vcc/Vdd co 200 ms. Nic
  między nimi nie stało.

  Widoczny objaw był kosmetyczny i dokładny. W zapisie z 03.09 10:08 wyświetlane
  Vctl spadło z 1,800 V do **1,620 V** siedem razy, na około dwie sekundy, przy
  niezmienionym PWM. Vctl to **średnia krocząca z dziesięciu próbek**, więc jedna
  konwersja zwracająca zero obniża ją dokładnie o jedną dziesiątą:
  1,800 × 0,9 = 1,620, zgodność na cztery cyfry, siedem razy. Ta wartość zasila
  wyłącznie wyświetlanie, więc nic po niej nie sterowało — ale jest to
  bezpośredni pomiar konwersji zniszczonej przez drugie zadanie, a ta sama
  kolizja po drugiej stronie niszczy **fazę**.

  Był też drugi, niezależny powód, dla którego odczyt musiał być niepodzielny:
  **50 µs to termin, nie opóźnienie.** Rampa opada ze stałą wycieku ~5 ms, więc
  przełączenie zadania, które przesunie odczyt o 1 ms, ląduje 20 % w dół
  zbocza — czyli 20 % błędu fazy bez żadnego zewnętrznego objawu.

  Oba są teraz zamknięte przez zawieszenie szeregowania wokół każdego dostępu do
  ADC: całego bloku „50 µs plus szesnaście konwersji" w `ltic_read_fast()`
  (~350 µs) i trzech konwersji w `ControlTask` (~60 µs), plus dwa odświeżenia
  kalibracyjne i jeden odczyt wyrzucany przy starcie. **Przerwania pozostają
  włączone** — przechwyt PPS, timery i SysTick są nietknięte — więc nic w
  ścieżce czasowej się nie zmienia; wykluczone są wyłącznie inne *zadania*.

  Mutex był rozważony i odrzucony. Jedyny poprawny timeout po stronie fazy to
  zero, bo ona nie może czekać; a nieudane pobranie z zerowym timeoutem zostawia
  wybór między wyścigiem mimo wszystko a porzuceniem pomiaru fazy, i żadne z
  tego nie jest poprawą. Zawieszenie szeregowania sprawia, że to tania strona
  ustępuje terminowi, a nie odwrotnie.

  Ten sam zapis niósł również to, jak kolizja wygląda od strony fazy: zgubiona
  sekunda w logu (03:18:24 → 03:18:26), CPU 38 % na kolejnej próbce i jeden
  odczyt detektora **+1369,1 ns** — pełna rampa, czyli odczyt obsłużony
  względem złego zbocza odniesienia. Bramka LTIC powinna go zatrzymać (skok 1364
  zliczeń wobec progu 743) i tego nie zrobiła, bo jej gałąź drugiej szansy
  akceptuje odczyt zgodny z wcześniej odrzuconym — a zadanie zagłodzone poza
  granicę PPS produkuje dokładnie taką parę. Bramka innowacji algorytmu 13
  złapała to mimo wszystko: `rej` poszło 135 → 136, a estymata fazy nie ruszyła
  się z −3,7 ns. Obrona w głąb zadziałała; warstwa, która miała to zatrzymać, nie.

  Do potwierdzenia naprawy nie trzeba nowej telemetrii. Jeśli zadziałała, dipy
  Vctl ×0,9 przestaną się pojawiać.

- **Nocny `KL` pokazał `Q since start: 5.022e-06 .. 4.982e-05` i żadna z tych
  liczb nie znaczyła tego, czego wymagały warunki zaliczenia.** Widełki dodane
  jeden build wcześniej — po to, by jeden zrzut na koniec przebiegu bez nadzoru
  odpowiadał na pytania „czy Q przekroczyło sufit" i „czy zeszło w spokojnych
  odcinkach" — obejmowały cały przebieg, a podczas dociągania `tracking` jest
  fałszywe, sufit nie obowiązuje i stosuje się szeroka szyna `q_seed·1e3`. Górna
  wartość była więc sześć razy wyższa od sufitu śledzenia, całkowicie legalna i
  nieczytelna zarówno jako przekroczenie, jak i jako jego brak. Offline'owy
  replay godzin ustalonych dał rzeczywisty zakres `6,4e-06 .. 1,2e-05`. Widełki
  startują teraz od pierwszej sekundy śledzenia i nie są już zerowane —
  chwilowa utrata śledzenia to część nocy, a nie nowa noc. `KL` mówi
  odpowiednio `Q while tracking`.

  Warto nazwać, co się stało, a nie tylko to naprawić: diagnostyka dodana po to,
  żeby szyna nie została przemilczana, sama okazała się nieczytelna przy
  pierwszym nocnym użyciu, w ten sam sposób i z tego samego powodu. Dwa buildy
  wcześniej linia `[at ceiling]` zasłaniała podłogę; tutaj widełki zasłoniły
  reżim.

- **Adaptacja Q odejmowała niewłaściwe R, a obie EMA szumu nazywały się tak
  podobnie, że pisemna analiza tego filtru odczytała je na odwrót.** `R` jest
  celowo średnim kwadratem **na lagu 16** — niesie zarówno biały szum detektora,
  jak i jego wolny dryf, żeby bramka innowacji i wzmocnienie Kalmana traktowały
  detektor jako wart tyle, ile jest wart w skali horyzontu, po którym pętla
  steruje. To wyprowadzenie jest udokumentowane przy kodzie i zostało zmierzone:
  zejście tam do białej podłogi wyrzuciło 11 % odczytów na stanowisku 29.08, a w
  przebiegu 27/28.08 wsterowało dryf detektora w oscylator.

  Jest właściwe dla bramki i właściwe dla wzmocnienia. Było niewłaściwe dla
  trzeciego zadania, które R po cichu dostało. Innowacja przy horyzoncie predykcji
  **jednej sekundy** może nieść tylko białą podłogę plus to, czego filtr nie
  wyśledził — na tej płytce około 6,4 ns² — więc nigdy nie sięgnie 8,45 ns²
  raportowanego przez estymator lagu 16. `Pobs = ms_innov - R` jest zatem ujemne
  przy spokojnym GPS z samej arytmetyki, a adaptacja była zagłodzona informacyjnie
  z konstrukcji, nie przez przypadek. Wstrzymanie dodane w build 30 uczyniło to
  przeżywalnym; nie uczyniło tego informatywnym.

  Odniesiona do podłogi z lagu 1 arytmetyka się domyka: `E[y²] = P00 + σ_white²`,
  więc `Pobs` estymuje prawdziwe `P00`, a `Ppred` jest własnym filtru — to test
  spójności kowariancji, którym Q faktycznie może ruszyć, bo `P00` jest dokładnie
  tym, na co Q jest dźwignią. Bramka i wzmocnienia zachowują `R` z lagu 16 i
  wszystkie opisane tam zabezpieczenia.

  Poszła za tym również reguła bang-bang. `×1,02` powyżej ilorazu 1,2 i `×0,98`
  poniżej 0,8 nie ma punktu stałego, tylko dwie krawędzie martwej strefy do
  drgania między nimi, i jest brutalnie asymetryczna w czasie: przy ilorazie 1,25
  narasta do **×2,7 na minutę**, a powrót wymaga ilorazu poniżej 0,8, co nie może
  nastąpić, dopóki P już nie urośnie. Teraz jest to multiplikatywna aproksymacja
  stochastyczna, `Q *= 1 + κ(iloraz − 1)` z `κ = 0,001` dopasowanym do EMA
  innowacji i krokiem obciętym do ±0,02, żeby najgorszy przypadek nie był szybszy
  niż wcześniej. Punkt stały dokładnie przy ilorazie 1, symetryczny, a ten sam
  iloraz 1,25 e-składa Q teraz w **około godzinę zamiast minuty** — epizod GPS nie
  jest w stanie go zapadkować. Adaptacja stoi też z boku przez dziesięć minut po
  każdym armie picDIV i gdy bramka odrzuca powyżej 5 %, na tej samej zasadzie co
  strażnik `moving` w estymatorze R.

  Ponieważ EMA lagu 1 wyznacza teraz odniesienie adaptacji, a nie tylko zasila
  `Sf`, jej wejście jest **winsoryzowane**: obcinany jest kwadrat różnicy, a nie
  pomijana próbka, więc ogon gaussowski jest ledwo dotknięty, a skok GPS 20 ns
  ograniczony. Obcięcie wynosi **9×** bieżący średni kwadrat, a nie 3×, które
  wygląda naturalnie — EMA trzyma `0,5·d1²` o średniej `σ²`, podczas gdy samo `d1`
  ma odchylenie `√2·σ`, więc obcięcie przy `m·σ²` obcina `|d1|` na `√m`
  odchyleniach. Przy `m = 3` to 1,73 σ: działa na **8,4 %** próbek i zaniża
  podłogę o **14 %** (zmierzone na 400 000 losowań gaussowskich) — trafiając
  dokładnie w wielkość, dla której zmierzenia ta zmiana istnieje. Przy `m = 9`
  jest to zamierzone 3 σ: 0,27 % próbek, 0,5 % obciążenia.

  **Zmierzone**, przed wobec po, dwanaście ziaren szumu na warunek. Przy dryfie
  detektora dobranym do tej płytki (5 ns / 300 s) phase sd **27,4 -> 19,3 ns**
  średnio i **44,6 -> 25,3** w najgorszym przypadku, sd błędu sterowania
  2,13 -> 1,58 LSB, ruch DAC bez zmian. Przy 8 ns dryfu **21,2 -> 17,5** średnio i
  **35,4 -> 21,3** najgorzej. Błąd częstotliwości 200 LSB: średnia 29,8 -> 28,0,
  najgorszy **51,3 -> 34,9**. Akwizycja — osiem zimnych startów z szyny przy dwóch
  horyzontach i start z -1300 ns — bez zmian. Dwa warunki wychodzą gorzej:
  idealnie czysty detektor (średnia 23,1 -> 24,6, choć najgorszy przypadek
  poprawia się 33,7 -> 31,3) oraz dryf 12 ns, czyli półtora raza więcej, niż ta
  płytka pokazuje (17,0 -> 18,6). To oczekiwany kształt kompromisu: zmiana
  pozwala Q rosnąć, by pokryć dryf, który `R` z lagu 16 trzymało poza
  wzmocnieniem — co jest słuszne do momentu, w którym dryf jest na tyle duży, że
  chce własnego stanu.

  EMA zostały przemianowane. `s_kf_ms_diff` to teraz `s_kf_ms_diff16`, z lagiem w
  nazwie i komentarzem przy deklaracji, bo odczytanie tej pary na odwrót zamieniło
  udokumentowaną decyzję projektową w widmowy „30-procentowy błąd w R" i
  kosztowało dzień.

  Diagnoza i obie naprawy pochodzą od **GLM-5.3 Max**, z niezależnego odczytu
  czterogodzinnego zapisu z 02.09; obie stałe powyżej to poprawki tego projektu do
  nich.

- **Szum procesu algorytmu 13 ani razu się nie zaadaptował — zawsze był
  dociśnięty do szyny, a jedna z dwóch szyn była w `KL` niewidoczna.** Trzy
  zapisy z jednej płytki, 02.09, przy trzech horyzontach:

  | KT | Q w użyciu | która szyna |
  |---|---|---|
  | 100 s | 7,777e-06 | `R/T^3` — sufit |
  | 40 s | 9,936e-08 | `q_seed/1000` — podłoga |
  | 20 s | 7,949e-07 | `q_seed/1000` — podłoga |

  Każda wartość zgadza się ze swoją szyną na cztery cyfry znaczące. `KL`
  nazywało tylko sufit, więc dwa z trzech przebiegów wyglądały na zdrową
  adaptację.

  Dwie usterki, które się nawzajem zasłaniały. Po pierwsze, podłoga to było
  `q_seed/1000`, a `q_seed` to `r_seed/T^3` — **to samo T co sufit**. Obie szyny
  poruszały się razem, więc zmiana KT przesuwała stałe tysiąckrotne okno w górę i
  w dół, zamiast dać adaptacji jakiekolwiek miejsce. Sweep KT, który te zapisy
  miały mierzyć, mierzył położenie szyny: KT 40 wyszło *gorzej* niż KT 100
  (dopasowana stała czasowa pętli 91 s wobec 40–65 s), bo Q spadło 78x, gdy
  podłoga przesunęła się pod nim, a własne `(R/Q)^(1/3)` estymatora poszło na
  462 s. Krótszy horyzont dał wolniejszą pętlę.

  Po drugie, adaptacja porównywała średni kwadrat innowacji z `S`, a
  `S = HPH' + R`. `S` nigdy nie spadnie poniżej `R`, więc kiedy innowacje
  wychodzą mniejsze niż samo R, iloraz tkwi poniżej 0,8 niezależnie od Q, a
  spadek 0,98 na sekundę jedzie aż do czegoś twardego. **Nie ma dolnego punktu
  stałego.** Adaptacja miała naprawiać błąd w R przez zmniejszanie Q, czego Q
  zrobić nie potrafi — a na tej płytce, od naprawy parowania piły, innowacje
  *są* mniejsze niż R: mierzone R to 2,9–3,0 ns, a dopasowanie funkcji struktury
  z tych samych zapisów daje białą część 2,35–2,65. Iloraz siedzi koło 0,77. Przy
  KT 100 wyszedł 0,83 i Q zamarło na suficie, gdzie zostawił je wcześniejszy
  wzrost; przy KT 40 wyszedł 0,77 i Q zeszło na podłogę. Czteroprocentowa zmiana
  R przerzucała pętlę między dwiema przeciwnymi awariami.

  Podłoga jest teraz szyną **numeryczną**, bez T — to wartość, którą ta sama
  formuła daje przy najdłuższym horyzoncie akceptowanym przez firmware, z
  detektorem na granicy kwantyzacji; na tej płytce około 1e-12, sześć dekad pod
  ziarnem — więc trzyma rekurencję z dala od zera, nie biorąc udziału w wyniku.
  Sufit zachowuje swoje T, bo „nie biegnij szybciej niż dany ci horyzont" jest
  właśnie tym, co znaczy KT. A adaptacja odejmuje teraz R od obu stron i
  porównuje `HPH'` przewidziane z `HPH'` wynikającym z innowacji: gdy to drugie
  jest ujemne, innowacje nie niosą żadnej informacji o Q, więc Q jest
  **wstrzymane**, `KL` to mówi i wskazuje na R. `KL` nazywa też teraz podłogę i
  stan wstrzymania.

  **Zmierzone**, przed wobec po, to samo drzewo, po jednym warunku naraz. Stan
  ustalony ze spacerem detektora dobranym do zmierzonego: przy KT 100 phase sd
  **42,1 -> 31,6 ns**, sd błędu sterowania **3,17 -> 2,56 LSB**, korelacja z
  prawdziwym wymaganym sterowaniem **0,046 -> 0,319**, ruch DAC bez zmian; przy
  KT 40 Q schodzi z podłogi (**1,2e-07 -> 2,9e-05**), a phase sd 6,00 -> 5,06. Z
  czystym detektorem przy KT 100 na dwunastu ziarnach: średnia **24,26 -> 23,12**,
  najgorszy przypadek **53,42 -> 33,73**. Przy 12 ns wolnego dryfu detektora
  **20,8/29,2 -> 16,5/20,4**. Każdy przypadek akwizycji — osiem zimnych startów z
  szyny przy każdym z trzech horyzontów, start z -1300 ns, detektor zamrożony
  wewnątrz i na zewnątrz pasma — wychodzi **bit w bit identycznie**, i tak być
  powinno: adaptacja stoi z boku podczas dociągania, a ta zmiana leży w całości w
  części śledzącej.

  Naprawa proponowana przez wcześniejszy komentarz w tym pliku — zasianie Q z
  pomiaru spaceru częstotliwości na TIM2 — została sprawdzona i jest niemożliwa:
  całkowity licznik jednosekundowy kwantuje się co 29 ns/s, a średnia
  studniowa co 2,9, wobec spaceru rzędu 1e-2 ns/s. To jest pozycja 4 z
  `doc/AUDIT_algo13_model_gaps.md`, tam już zamknięta jako niemierzalna. Ziarno
  nigdy nie było problemem; problemem była podłoga.

- **Pętla mogła przesynchronizować dzielnik, gdy detektor podawał zupełnie
  poprawną fazę, i traciła przy tym większość pasma akwizycji.** Warunek arm
  pytał o `!have`, a `have` to `raw && trust` — dwie różne usterki sprowadzone do
  jednego pytania. Detektor na SZYNIE (`raw` fałszywe) nie mówi nic i wtedy
  przesynchronizowanie picDIV jest jedyną naprawą, jaka istnieje. Detektor
  NIEZAUFANY (`raw` prawdziwe, `trust` fałszywe) wciąż podaje fazę — a jeśli ta
  faza leży w paśmie, arm niczego nie naprawia: wyrzuca użyteczny odczyt i
  ląduje gdzieś między -900 a -1650 ns.

  W zapisie z 02.09 10:59 stało się dokładnie to. W t+593 s detektor pokazywał
  **-53 ns** przy `Vphase 2.041 V` — zdrowy pod każdym względem — ale test
  zaufania odrzucił go osiem sekund wcześniej i pętla była w holdover. Gałąź
  odliczyła swoje pięć sekund i wykonała arm. Faza poszła na **-1222 ns**, a
  powrót zajął kolejne siedemset sekund: trzy army i **1317 s** do ustalenia,
  wobec jednego armu i **309 s** poprzedniej nocy na tej samej płytce.

  Gałąź wykonuje teraz arm tylko wtedy, gdy nie ma czego stracić — brak
  jakiegokolwiek ważnego odczytu albo odczyt już poza pasmem, czyli przypadek
  zamrożonego detektora, gdzie przesynchronizowanie *jest* naprawą. **Zmierzone**
  względem tego samego drzewa z usuniętym tym jednym warunkiem: osiem zimnych
  startów z szyny, stan ustalony, dryf detektora 12 ns/300 s, przesunięcie
  startowe -1300 ns i błąd częstotliwości 200 LSB wychodzą bit w bit identycznie,
  a detektor zamrożony poza pasmem nadal wykonuje czternaście armów. Zmienia się
  wyłącznie detektor zamrożony *wewnątrz* pasma — czternaście armów staje się
  zerem — a to jest właśnie przypadek, dla którego ta zmiana powstała.

- **Pomiar TIM2 był opóźniony i przeceniany; naprawa obu rzeczy skróciła czas
  akwizycji o połowę.** Dwie usterki w jednym miejscu, a `loopsim.cpp` opisywał
  drugą z nich — o własnym modelu obiektu — od 26.08, nie mówiąc o tym filtrowi.

  `Rf` było liczone z różnic sąsiednich odczytów `avg100`. To okna prostokątne
  dzielące 99 ze 100 próbek, więc ich różnica jest ~setną szumu pojedynczej
  próbki i estymator wychodził o dwa rzędy wielkości za mały — całą robotę po
  cichu wykonywała podłoga `Rf >= 1`, a samo 1 (ns/s)² jest ~ośmiokrotnie zbyt
  optymistyczne. Teraz buduje się je ze zmierzonego rozrzutu licznika
  JEDNOSEKUNDOWEGO, podzielonego przez liczbę próbek w używanej średniej: bez
  stałej, a gorszy PPS czy gorsza antena widać w tym wprost.

  A stusekundowe okno prostokątne nie jest pomiarem częstotliwości teraz — siedzi
  jakieś pięćdziesiąt sekund do tyłu, czyli dokładnie tam, gdzie częstotliwość
  była inna, jeśli pętla korygowała. Pętla księguje każdą swoją korektę, więc zna
  zadaną zmianę w tym oknie: pomiar jest teraz przewidywany jako `x1 - du/2`, a
  nie `x1`. To korekta, nie inflacja, bo ta liczba jest znana, a nie tylko
  ograniczona.

  **Zmierzone** na szesnastu zimnych startach z detektorem na szynie (cztery
  offsety częstotliwości, cztery ziarna): średni czas do ustabilizowania
  **1802 -> 923 s**, najgorszy przypadek **4615 -> 2727 s**, przy znacznie
  mniejszej liczbie armów picDIV. Nadążanie przy zamrożonym detektorze poprawia
  się sześciokrotnie (track sd 978 -> 169 LSB). Stan ustalony bez zmian. To
  pozycja 3 z `doc/AUDIT_algo13_model_gaps.md` — trzecia w kolejności ważności, a
  okazała się największą pojedynczą poprawą z całej piątki.

- **Algorytm 13 potrafił w ogóle nie złapać, a symulator tego nie widział, bo
  jego model picDIV-a był pobłażliwy.** Zrzut z 01.09 21:00 nigdy nie zaskoczył:
  jedenaście minut, cztery army, 460 s holdoveru, 94 s odrzuceń na bramce,
  jedenaście sekund normalnej pracy. `Sf` nie ma z tym nic wspólnego — `R` ani
  razu nie ruszyło z zasiewu, więc `Sf` było przez cały przebieg zerem.

  **Gdzie arm naprawdę ląduje.** Siedem armów w trzech zrzutach z 01.09, faza
  odczytana w sekundzie po każdym: `-1554 -1431 -899` (21:00), `-1641 -1441 -943`
  (17:12), `-927` (10:49 — jedyny przebieg, który potem działał). Każdy ujemny,
  żaden blisko zera, od -900 do -1650 ns przy paśmie ±1500. Symulator modelował
  lądowanie jako `gauss(300)`, czyli kilkaset nanosekund wokół zera — więc każdy
  symulowany arm się udawał i każdy test akwizycji przechodził. To trzeci
  pobłażliwy model znaleziony w `loopsim.cpp`, po idealnym TIM2 i złym napięciu
  szyny. Teraz ląduje tam, gdzie sprzęt, i może potem znowu wejść na szynę, czego
  stanowisko również nigdy nie modelowało. `LOOPSIM_ARMOFS` / `LOOPSIM_ARMSD`
  nadpisują obie liczby.

  **Dlaczego stara bramka nie mogła działać.** Żądała tylko, by błąd
  częstotliwości nie przeniósł fazy przez pół pasma w ciągu 60 s przetrzymania —
  `arm_hz = range/(2·100·60)` = 0,25 Hz — czyli wydawała cały budżet na dryf i
  nie zostawiała nic na przesunięcie lądowania, a to przesunięcie okazało się
  większością pasma. Army z 21:00 przeszły przez tę bramkę przy +0,24 i +0,17 Hz:
  24 i 17 ns fazy na sekundę, dość, by w kilka sekund wyprowadzić lądowanie z
  -1450 poza rampę. Bramka pyta teraz o to, co istotne — *skoro ten dzielnik
  ląduje tutaj, czy faza będzie jeszcze czytelna za jeden horyzont?* — z
  lądowaniem zmierzonym z ostatniego armu i dryfem z TIM2, więc żaden składnik
  nie jest stałą do zgadnięcia. Przestaje też odrzucać duży błąd częstotliwości,
  który akurat spycha fazę z powrotem ku środkowi. Ucieczka po dziesięciu
  horyzontach bez odczytu pilnuje, by bramka mogąca odmawiać w nieskończoność
  tego nie robiła.

  **A filtr traktował własny aktuator jako dokładny.** `s_kf_x1 +=
  polarity*applied/lsb_per_ns` podaje filtrowi korektę jako fakt, bez żadnej
  kowariancji, a `lsb_per_ns` pochodzi z `CT`, czyli z pomiaru jak każdy inny.
  Kilka procent błędu tam kumuluje się w `x1` i nic już tego nie odbiera — a to
  obciążenie ma stabilny dom, bo `u = -(x1 + x0/T)` nie zadaje dokładnie nic,
  ilekroć `x1 = -x0/T`. Pętla parkuje na stałym offsecie fazy, innowacje idą do
  zera i żaden pomiar z niczym się nie kłóci. Widziane w symulatorze:
  zaparkowane na -100 ns przez 1500 s przy `x1` = +1,0 ns/s — błąd 0,01 Hz, czyli
  dokładnie rozdzielczość TIM2, więc drugi pomiar też tego nie widzi. I na
  sprzęcie: przebieg z 01.09, któremu zejście z 21 do 7,5 ns zajęło 3600 s przy
  stanie mówiącym „już się domykam" (TODO 80), oraz każdy stały offset, jaki ta
  pętla kiedykolwiek pokazała. Pięć procent zadanej korekty, do kwadratu, idzie
  teraz w `P11`: filtr zachowuje dość wątpliwości co do własnej częstotliwości,
  by pomiar fazy mógł go ściągnąć z powrotem.

  **Zmierzone**, szesnaście zimnych startów z detektorem na szynie (cztery
  offsety częstotliwości, cztery ziarna), na uczciwym modelu armu: średni czas do
  ustabilizowania **2316 -> 1802 s**, najgorszy przypadek **7127 -> 4615 s** i
  znacznie mniej armów w każdym przypadku, który się zmienił. Zamknięta pętla bez
  zmian do dwóch miejsc po przecinku we wszystkich wskaźnikach stanu ustalonego,
  na dwóch obiektach, pięciu ziarnach i trzech poziomach wędrówki detektora —
  obie naprawy są bezczynne, gdy pętla już trzyma.

  `loopsim` raportuje teraz **czas do ustabilizowania**, bo to jest miara, którą
  zmiany w akwizycji naprawdę ruszają; sd fazy z całego przebiegu ocenia pętlę
  armującą wcześnie i źle tak samo jak tę, która czeka i armuje dobrze.


- **Algorytm 13 chodził na połowie modelu zegara i ta połowa była zapadką.**
  Model zegara dwustanowego, którego używa każdy podręcznik metrologii czasu,
  niesie *dwie* gęstości szumu procesowego — `Sf`, biały szum częstotliwości
  (`h0`), objawiający się jako błądzenie przypadkowe fazy, oraz `Sg`, błądzenie
  przypadkowe częstotliwości (`h_-2`):

  ```
         | Sf*t + Sg*t^3/3   Sg*t^2/2 |
    Q =  |                            |
         |    Sg*t^2/2        Sg*t    |
  ```

  Ten filtr wstrzykiwał `Q/3`, `Q/2`, `Q`, co przy `t` = 1 s jest dokładnie
  trzema członami `Sg`, a **`Sf` było tożsamościowo zerem** — nie nastawą
  ustawioną na zero: nie miało nazwy i nic go nie mierzyło. Konsekwencja to cała
  historia minionego tygodnia. Bez `Sf` jedynym sposobem wyjaśnienia „faza
  ruszyła się w tej sekundzie bardziej, niż przewidziałem" jest podniesienie
  `Sg`, czyli orzeczenie, że CZĘSTOTLIWOŚĆ OSCYLATORA błądzi szybko. Szum
  krótkoterminowy, wędrujące zero detektora, opóźniony odczyt licznika: wszystko
  było księgowane jako błądzenie częstotliwości, co podnosi `K1`, wzmocnienie
  zapisujące stan częstotliwości, za którym jedzie DAC. Zapadka `Q` nie była
  błędem adaptacji. Była adaptacją robiącą jedyną rzecz, jaką model jej zostawił.

  **Zmierzenie `Sf` wymaga dwóch opóźnień, a drugie jest prawie darmowe.** Przy
  opóźnieniu `k` sekund różnice fazy niosą `0,5*E[dp^2] = sigma_R^2 + Sf*k/2` —
  biały szum, który nie rośnie z `k`, plus błądzenie, które rośnie. Historia dla
  estymatora `R` na opóźnieniu 16 już tam była; estymator na opóźnieniu 1 obok
  niej rozdziela jedno od drugiego. To ten sam podział `floor` / `slow`, który
  `tools/logab.py` drukuje od początku, a którego filtr nigdy nie dostał.

  **`Sf` jest mierzone, nie adaptowane**, więc w odróżnieniu od `Sg` nie może
  wpaść w zapadkę — o to właśnie chodziło.

  Pierwsza wersja obcinała różnicę na zerze i była błędna w sposób wart
  zapisania: oba estymatory to EMA z `alpha` = 0,002 z kwadratu zmiennej
  gaussowskiej, więc każdy niesie ok. 4,5% błędu standardowego, a ich różnica
  ok. 6,3%, a obcięcie zaszumionej wielkości ze znakiem na zerze prostuje ją w
  dodatnie obciążenie rzędu 0,4 sigma. Na idealnie białym detektorze
  symulacyjnym, gdzie uczciwą odpowiedzią jest zero, `Sf` wyszło 1,5e-2 ns²/s —
  niemal dokładnie przewidziane obciążenie — i kosztowało 60% na sd fazy oraz
  dwukrotność na ADEV dla tau 1024, bo fikcyjne `Sf` tłumaczy innowacje,
  adaptacja zagładza wtedy `Sg`, a to `Sg` pozwala filtrowi nadążać za dryfującym
  oscylatorem. Różnica musi teraz przekroczyć dwie sigmy własnego szumu (0,126
  estymaty), zanim zostanie uznana za pomiar, a próg wynika ze stałej EMA, a nie
  z wyboru.

  **Zmierzone**, dwa obiekty, pięć ziaren. Przy czystym detektorze, gdzie `Sf`
  poprawnie czyta zero, wszystko bez zmian; przypadki detektora na szynie,
  zamrożonego, zimnego startu i przemiatania `KT` identyczne do ostatniej cyfry.
  Przy 12 ns wędrówki zera detektora — czyli w warunkach, w jakich realnie są obie
  płytki, `R` 6,3 ns, z czego ok. 5,9 ns to wędrówka — ADEV dla krótkich tau
  poprawia się **1,7x** (3,11e-11 -> 1,81e-11 na tau 16), a pętla wyraźnie lepiej
  nadąża za prawdziwą trajektorią oscylatora (track sd 1,26 -> 1,14 LSB,
  korelacja zadane-wymagane 0,913 -> 0,928; na drugim obiekcie 1,29 -> 1,15 i
  0,461 -> 0,529). Koszty: 33% więcej ruchu DAC-a — nadal rząd wielkości poniżej
  punktu wyjścia, i jest to ruch użyteczny, skoro ADEV krótkotau poprawiło się
  razem z nim — oraz ok. 8% na ADEV dla tau 1024.

  `KL` pisze `Sf` obok `R` i `Q`. Zero oznacza, że oba opóźnienia się zgadzają,
  co przy czystym detektorze jest właściwą odpowiedzią.

  To pozycja 1 z `doc/AUDIT_algo13_model_gaps.md`. Pozycje 2-4 — zero detektora
  jako stan, opóźniony i przeceniany pomiar TIM2 oraz `KT`, o którym dane
  odniesione do rubidu mówią, że jest 5-10x za krótkie — pozostają otwarte, a
  `KT` nadal nie da się podnieść, dopóki zasiew `Sg` skaluje się jak `1/KT^3`.


- **Sufit `Q` odsuwa się na czas akwizycji, bo inaczej był najciaśniejszy
  dokładnie wtedy, gdy filtr potrzebował miejsca.** `R` startuje *na* swoim
  zasiewie (`kf_reset` zasiewa estymator różnic wartością `r_seed`), a `q_seed`
  to `r_seed/KT³` — więc w pierwszej sekundzie `R/KT³` **jest** `q_seed`: nowy
  sufit lądował na zasiewie i nie zostawiał adaptacji żadnego zapasu w górę,
  dopóki `R` nie zostało zmierzone. A akwizycja to właśnie miejsce, gdzie szerokie
  `Q` zarabia na siebie — faza tysiąc nanosekund obok potrzebuje filtru, który
  może się ruszyć.

  Podniósł to zrzut z 01.09 17:12: arm picDIV wylądował fazą na -2124 ns, poza
  pasmem detektora, i przebieg potrzebował pięciu armów, 897 sekund na szynie i
  3300 s na ustabilizowanie, wyrzucając po drodze do **70% odczytów** na bramce
  innowacji — wobec jednego armu i 900 s w przebiegu wcześniejszym. Symulator
  tego startu nie odtwarza (arm ląduje tam czysto), więc ile z tego to ta zmiana,
  a ile własny rzut kostką armu, pozostaje nierozstrzygnięte; sufit spadający na
  zasiew przy starcie jest błędem tak czy inaczej.

  Ograniczenie horyzontem obowiązuje więc teraz tylko, gdy pętla nadąża: faza
  wewnątrz pasma akwizycji **i** `R` mające za sobą co najmniej jedną stałą
  czasową EMA rzeczywistego pomiaru. Dopóki oba nie zachodzą, adaptacja pracuje
  przy szerokiej szynie bezpieczeństwa jak dawniej. Nic nie tracimy — zapadka, dla
  której to ograniczenie istnieje, jest usterką stanu ustalonego i potrzebuje
  godzin spokojnego nadążania, żeby się rozwinąć. Stan ustalony bez zmian do
  ostatniej cyfry na dwóch obiektach, pięciu ziarnach i trzech poziomach wędrówki
  detektora; przypadek zamrożonego detektora poprawia się wyraźnie (sd fazy
  347k -> 55k ns, częstotliwość końcowa -0,50 -> +0,31 Hz, odrzucenia 11,2% ->
  0,8%).

  Odsunąć się to nie to samo co puścić, i pierwsza wersja tego pomyliła: przy
  zawieszonym ciasnym suficie i bez bezwarunkowego za nim zapadka 1,02 na sekundę
  doszła do `Q` = 5,4e+18 w niecałe dwie godziny na obiekcie z zamrożonym
  detektorem — pętla nigdy nie czyta „nadąża", więc jedyne pozostałe ograniczenie
  musi być bezwarunkowe. Szeroka szyna jest teraz nakładana w obu gałęziach.


- **Odczyt CPU na belce TFT wrócił na oś środkową.** Przesunęła go naprawa
  ucinanego `%`: pole było centrowane w przerwie między nazwą programu a
  *paddingiem*, który rezerwuje zegar LMT, a `HDR_LMT_PAD` jest znacznie szerszy
  niż glify zegara — więc cały napis siedział jakieś 23 px na lewo od środka na
  panelu 480. Padding nigdy nie był przeszkodą: obie ścieżki rysowania kładą CPU
  jako ostatnie, więc nic go już potem nie wymaże, a jedyne, czego nie wolno mu
  dotknąć, to rzeczywiste glify zegara. Zmierzone względem nich, wolne miejsce
  jest symetryczne (nazwa programu i zegar mają po szesnaście znaków), więc oś
  środkowa mieści się z zapasem około 30 px z każdej strony i tam właśnie napis
  wraca. Pasmo kasowania jest teraz wymiarowane na najszerszy możliwy odczyt
  (`CPU 100%`), a nie na całą przerwę — to wymiarowanie na przerwę zepchnęło
  tekst z osi. Środek jest używany tylko wtedy, gdy zmierzone szerokości mówią,
  że się mieści; w przeciwnym razie zostaje środek przerwy jako zapas, a gdy i
  ten jest za mały, nie rysuje się nic — więc żadne zestawienie fontów i paneli
  nie przywróci ucinania.


- **`KL` mówi teraz wprost, gdy `KQ` jest przypięte.** Linia nagłówka już je
  rozróżniała, pomijając `(adapt)` po wartości, i to nie wystarczyło. Zrzut z
  01.09 pokazał pętlę ruszającą DAC-iem sześć razy mniej niż tydzień wcześniej,
  co czytało się jak działanie nowego sufitu na adaptacji `Q` — a nie było nim:
  `KQ` zostało przypięte na zasiewie w jakimś wcześniejszym eksperymencie i od
  tamtej pory wracało z flash ringu przy każdym starcie, więc adaptacja w ogóle
  nie działała. Ustawienie, które przeżywa restarty i zmienia znaczenie każdej
  innej liczby w raporcie, potrzebuje własnej linii — i ją dostało.

- **Algorytm 13 przesterowywał: `Q` narastało na skorelowanym szumie detektora,
  aż filtr był pięć razy szybszy niż jego własny horyzont.** Stół pokazywał
  pętlę ruszającą DAC-iem o 1,80 LSB na sekundę wobec 0,49 algorytmu 11 tej
  samej nocy, i to bez lepszej fazy w zamian — a po rozbiciu na składową szybką
  i wolną okazało się, że to drżenie sekunda po sekundzie, 3,7x, a nie wolna
  wędrówka.

  Przyczyna siedzi w adaptacji `Q` i jest zapadką działającą w jedną stronę.
  Filtr podnosi `Q` zawsze, gdy jego innowacje wychodzą większe, niż przewidziała
  kowariancja. Gdy błąd detektora jest *skorelowany* — zero wędrujące w skali
  minut — wychodzą, z powodu niemającego nic wspólnego z oscylatorem, więc `Q`
  rośnie o 1,02 na sekundę, aż `P` i `S` urosną na tyle, by je wytłumaczyć.
  Zatrzymuje się, ale wysoko: `KL` 30.08 pokazało `Q=1.238e-03` przy zasiewie
  `6.36e-06`. 195x w `Q` to `sqrt(195)` = 14x we wzmocnieniu, które zapisuje stan
  częstotliwości — a to właśnie za nim podąża DAC.

  ROZSTRZYGNĘŁA DRUGA PŁYTKA. Dan Wiering puścił ten sam firmware na własnej
  płytce, nie ruszając niczego poza `CT`, `LC`, `SAW 1` i `ES LTIC`, i poszło na
  całość: `Q` opierało się o starą szynę 1000x na `4.468e-03` po 2h37m od startu
  i siedziało tam jeszcze dziewięć godzin później, DAC ruszał się **11,05 LSB/s**
  i zadał **rozpiętość 249 LSB tam, gdzie oscylator potrzebował 19,8**. Zmierzone
  względem wzorca rubidowego — niezależnego odniesienia, którego ten projekt nie
  ma — ADEV na 20 s wyniósł **8,6e-11 wobec 3,1e-12** dla algorytmu 11 na tej
  samej płytce i tym samym odniesieniu; garb z maksimum, jak być musi, dokładnie
  na stałej czasowej filtru. Ta sama usterka, cztery razy większa, na sprzęcie
  nigdy nie strojonym ręcznie.

  `(R/Q)^(1/3)` ma wymiar czasu i jest własną stałą czasową filtru, więc
  `Q = R/KT³` mówi dokładnie *biegnij tak szybko jak zadany horyzont*. Tam
  właśnie adaptacja jest teraz ograniczona: **filtr nie może biec szybciej niż
  `KT`**, przy `R` branym z pomiaru, a nie z zasiewu. To ostatnie nie jest
  szczegółem. Pierwsza wersja tego ograniczenia brzmiała „osiem zasiewów" i
  działała — ale przez przypadek, bo `q_seed` bierze się z założonego z góry
  2,5-kwantowego strzału, a to, jak wysoko nad nim siedzi realny detektor, jest
  cechą płytki: ta mierzy 2,5x swój zasiew, a Dana 6,8x, więc ten sam mnożnik
  znaczył tu tau >= 92 s, tam >= 95 s, a na płytce z detektorem równym zasiewowi
  znaczyłby >= 50 s. Wzięte z `R` w użyciu mówi wszędzie to samo i nadąża za
  ponownym `LC` w ciągu sekundy. Zmierzone na dwóch obiektach, pięciu ziarnach
  szumu i trzech poziomach wędrówki detektora: ADEV dla krótkich tau dwa razy
  lepsze (7,99e-12 wobec 1,54e-11 na tau 16), ruch DAC-a 2,4x mniejszy, kosztem
  7% na sd fazy i 25% na ADEV dla tau 1024 — a sd fazy mierzy się względem
  detektora, czyli przyrządu, który tu właśnie kłamie, podczas gdy rubid nie.
  Przypięcie `Q = 1.238e-03` ręcznie odtwarza wynik ze stołu w symulatorze,
  1,07 LSB/s, i to jest potwierdzenie, którego sam log dać nie mógł.

  Nic nie tracimy, odmawiając `Q` pochłaniania skorelowanego błędu detektora:
  `R` już go niesie, bo jest mierzone z różnic branych blisko horyzontu. Wartość
  przypięta przez `KQ` nadal przechodzi bez zmian; ograniczenie dotyczy
  adaptacji. O szybszą pętlę prosi się teraz uczciwie — skracając `KT`.

  I TO DZIAŁA W OBIE STRONY, co trzeba powiedzieć wprost. Wszystko powyższe jest
  przy domyślnym `KT` 100 s. Przy `KT` 300 i 1000 pętla wchodzi w ten sufit i o
  niego się opiera (sd fazy 0,64 -> 2,82 ns przy 300, 3,04 -> 47,6 ns przy 1000),
  bo `Q_seed = R/KT³` spada z sześcianem horyzontu, a realna wędrówka oscylatora
  nie zmienia się wcale — powyżej kilkuset sekund zasiew przestaje cokolwiek
  szacować, a stara szyna 1000x po cichu to korygowała. Teraz jest to widoczne, a
  nie ukryte: `KL` pisze `[at ceiling]` obok `Q`, a pętla siedząca tam przez cały
  przebieg mówi *twoje `KT` jest dłuższe, niż ten oscylator udźwignie*. Naprawa,
  kiedy przyjdzie, polega na zasianiu `Q` z oscylatora zamiast z horyzontu — TIM2
  mierzy wędrówkę częstotliwości wprost, a jego szum jest niezależny od zera
  detektora. Do tego czasu `KT 100` jest konfiguracją zmierzoną. Flaga
  `[at ceiling]` w `KL` powstała dlatego, że tę usterkę znaleziono, przeszukując
  zrzut pod kątem tej liczby, a to nie jest diagnostyka, którą ktokolwiek
  powinien wykonywać dwa razy.

- **Bramka częstotliwości algorytmu 13 mogła zatrzasnąć się na stałe i zabierała
  ze sobą holdover.** Znalezione przy sprawdzaniu powyższego na zamrożonym
  detektorze: test zaufania robi swoje i odrzuca detektor, od tej chwili TIM2
  jest jedynym pozostałym pomiarem — ale oscylator jest już o herc obok,
  innowacja wynosi 100 ns/s, a limit bramki 4 ns/s, bo `P11` nie ma z czego
  rosnąć poza `Q`. Każdy odczyt odrzucony, stan częstotliwości utknięty na zerze,
  a pętla jedzie na modelu, który twierdzi, że wszystko gra, podczas gdy faza
  ucieka. Jedyny przypadek, dla którego ten pomiar istnieje, był jedynym, w
  którym nie mógł zadziałać.

  Ucieczkę z bramki fazy — poszerz kowariancję o odrzuconą innowację i wpuść
  następną — wypróbowano tu najpierw i zmierzono zdecydowanie gorzej: ustawia
  wzmocnienie blisko jedynki, więc stan skacze do pomiaru, który jest 100-
  sekundowym oknem prostokątnym i przez to opóźnionym o pięćdziesiąt sekund, a
  pętla goni własne opóźnienie (+10,2 Hz i 7795 LSB/s, wobec -1,04 Hz i 0,06
  LSB/s przy odrzucaniu wprost). Dla fazy to działa, bo faza jest natychmiastowa;
  tu nie jest. Dlatego bramka teraz **przycina**, zamiast się otwierać: po
  dziesięciu odrzuceniach z rzędu odczyt zostaje przyjęty, ale tylko cztery
  sigmy z niego, i stan idzie ku prawdzie z ograniczoną szybkością. Zamrożony
  detektor: -0,49 Hz i 1,9 LSB/s, najlepiej z całej trójki. Każdy zdrowy
  przypadek — detektor na szynie, zimny start 400 LSB, czysty przebieg, oba
  obiekty, wszystkie ziarna — pozostaje identyczny co do bitu.

### Zmienione

- **Pasek stanu tunera pokazuje teraz całą tożsamość firmware'u, a `V` nią
  odpowiada.** Wcześniej pisał `connected — firmware v1.06` i na tym kończył, co
  nazywa protokół, ale nie binarkę. Teraz czyta się to tak:

  ```
  connected — firmware v1.06-rtos  build 27  2026-09-02 09:46  CRC 78B08D26
  ```

  Znacznik kompilacji i numer builda istniały już wcześniej, ale wyłącznie w
  banerze startowym, który zwykle zdąży przewinąć się zanim ktokolwiek się
  podłączy. Szkic składa teraz ten znacznik raz do wspólnego napisu — musi to
  być szkic, bo `__DATE__` wpieka się w tę jednostkę kompilacji, która o nim
  wspomina, a szkic jest jedyną, którą `build_id.h` zmusza do rekompilacji — i
  baner oraz `V` drukują te same znaki zamiast dwóch, które mogą się rozjechać.

  Wszystkie trzy fakty są tam, bo odpowiadają na różne pytania: wersja mówi,
  jakim protokołem tuner rozmawia, build i znacznik czasu — z którego drzewa
  źródeł to pochodzi, a CRC — który obraz binarny naprawdę działa. Tylko to
  ostatnie nie może być nieaktualne, i po to właśnie powstało: 26.08 dwa zrzuty
  z dwóch różnych buildów niosły ten sam znacznik czasu i kosztowały godzinę
  kłótni z logiem, który miał rację.

  Wszystko poza wersją jest w parserze opcjonalne, więc starszy firmware
  odpowiadający na `V` samą nazwą nadal się łączy, a linia po prostu mówi mniej.


- **`KT` powyżej 200 s ostrzega, bo pętla nie potrafi obsłużyć obu końców.** `Sg`
  jest zasiewane i ograniczane przez `R/KT³`, więc długi horyzont zmusza
  *estymator*, by był tak wolny jak *sterownik* — a to dwie różne rzeczy.
  Usunięcie ograniczenia całkowicie naprawia `KT` 1000 (sd fazy 50,2 -> 2,92 ns,
  ustabilizowanie natychmiast zamiast po 11120 s) i natychmiast przywraca zapadkę
  przy `KT` 100 (`Q` do 1,12e-3, ADEV na tau 16 z 8,05e-12 na 4,08e-11).
  Zamrożenie adaptacji na czas, gdy pętla steruje — zabezpieczenie, które działa
  dla `R` — też nie pomaga: zapadka rozwija się i w stanie spokoju. Rozdzielenie
  tych dwóch rzeczy wymaga zmierzenia `Sg` z oscylatora, a `Sg` to 6,4e-6
  (ns/s)²/s wobec podłogi szumu TIM2 równej 8 (ns/s)² — sześć rzędów wielkości
  poniżej tego, co ta płytka widzi. Ograniczenie zostaje, `KT 100` pozostaje
  konfiguracją zmierzoną, a firmware mówi o tym przy ustawieniu czegoś dłuższego.

- **Zero detektora jako czwarty stan zostało zbudowane, zmierzone i nie weszło do
  drzewa** — leży w całości w `doc/algo13-zero-state.patch`. Robi to, do czego
  zostało zaprojektowane: przy 12 ns wędrówki zera DAC rusza się o 32% mniej, a
  ADEV dla krótkich tau poprawia się 1,6x. Trzyma też prawdziwą fazę gorzej
  (sd 12,80 -> 13,90, track sd 1,13 -> 1,24, `r` 0,929 -> 0,917, ADEV na 1024
  +17%), bo `x0` i zero są niemal zdegenerowane — pomiar fazy widzi tylko ich
  sumę, a jedyne, co je rozdziela, to TIM2 z szumem ok. 8 (ns/s)². Wypróbowano
  stałe czasowe 1x, 2x, 5x i 10x `KT`. Obie przegrywające metryki mierzy się
  względem odczytu fazy, czyli przyrządu, którego ta zmiana dotyczy — więc ten
  symulator tego nie rozstrzygnie; niezależne odniesienie mierzące wyjście
  rozstrzygnęłoby w jedną noc.



- **Algorytm 10, faza LOCK: strefa nieczułości zniknęła, zastąpiona konstrukcją
  z algorytmu 12.** Dawny LOCK traktował każdy błąd fazy poniżej `range_ns/40`
  jako zero — 47 ns na tej płytce — z miękkim kolanem powyżej, i pętla
  posłusznie parkowała na stałych +76 ns przez półtorej godziny. To nie była
  usterka pętli; to była jej specyfikacja. Usunięcie strefy i działanie na
  zwykłej średniej z interwału okazało się gorsze (faza przemiatała ±500 ns, oba
  ograniczniki detektora), bo średnia z H sekund to faza sprzed H/2, a ten etap
  i tak koryguje tylko co H.

  Algorytm 12 nie uśrednia. Jego test `(a+b) + 2*(b-a)` trzyma dwa sąsiednie
  półokna i EKSTRAPOLUJE na koniec pary, więc uśrednianie i opóźnienie znoszą
  się z konstrukcji — a ta sama para daje nachylenie, czyli pomiar błędu
  częstotliwości, którego LOCK inaczej w ogóle nie widzi (`Kp` jest tu zerem,
  więc człon z TIM2 jest tożsamościowo zerowy). LOCK trzyma teraz tę parę,
  bramkuje fazę na błędzie standardowym nowszej średniej, a nachylenie na
  błędzie standardowym różnicy dwóch średnich, i wprowadza nachylenie do
  integratora jako bezwzględną poprawkę PWM. Nigdzie nie ma założonego progu:
  sigma pochodzi z pierwszych różnic detektora, z tego samego estymatora,
  którego używa algorytm 12.

  Zmierzone 21.08 przy `LIV 30`, 25 min w LOCK bez utraty: RMS fazy **6,9 ns**
  po ustaleniu (średnia **+2,2 ns**, −14,3…+19,1 ns), 31 korekt, mediana kroku
  2 LSB, największy 9. Nakładający się ADEV zgadza się z 23-godzinnym zapisem
  algorytmu 12 z dokładnością do kilku procent na każdym tau aż do 128 s
  (3,5e-9 przy 1 s, 1,0e-10 przy 128 s) — i o to w tej zmianie chodziło:
  pętla trójstopniowa trzyma teraz fazę tak samo dobrze jak akumulator, na tym
  samym sprzęcie i mniejszym wysiłkiem sterowania.

- **Kadencja LOCK ograniczona dryfem, który pętla właśnie zmierzyła.** Nigdy nie
  pozwól fazie przejść więcej niż dwie sigmy własnego szumu między korektami,
  korzystając z nachylenia, które para i tak daje. To może tylko SKRÓCIĆ
  interwał, a płytka bez rozpoznawalnego dryfu zachowuje pełne
  `lock_interval_s`, bo ograniczenie jest dzieleniem przez nachylenie, które
  czyta zero. Przemiecione w symulacji: przy `LIV 300` i dryfie zmierzonym na
  tym sprzęcie RMS fazy wynosi 90 ns przy ośmiu sigmach, 50 przy czterech i 34
  przy dwóch, wobec 100 dla dawnej strefy nieczułości; pętla, która patrzy
  częściej, ma za każdym razem mniej do odrobienia, więc pojedyncze kroki PWM są
  MNIEJSZE, nie większe (około 10 LSB przy dwóch sigmach wobec 17 przy ośmiu).
  `LIV 30` pozostaje najlepszym zmierzonym ustawieniem i nie potrzebuje
  ograniczenia w ogóle.

- **Bezuderzeniowe przejście DPLL↔LOCK.** Integrator jest w tej pętli
  bezwzględnym celem PWM (`u = integ - pwm`), więc przy zmianie etapu jest teraz
  przesiewany na to, co właśnie zostało zapisane. Zmierzone na przejściu 21.08:
  40853 → 40854, krok jednego LSB tam, gdzie wcześniej fazą szarpało to, gdzie
  akurat zawinął się `integ`.

- **Vcc pokazywane z trzema miejscami po przecinku**, a wiersz `dph` opisany
  jako `dp:` na panelu 320×240; `qE:` rozwinięte do `qEr:`. Wszystkie trzy
  ruszone wiersze wypełniają teraz dokładnie zakres 168..314 px.

- **ACQ algorytmu 10 był bezwarunkowo niestabilny — wzmocnienie pięć razy poza
  granicą stabilności.** ACQ liczy co 5 s, ale steruje z `avg100`, czyli
  PRZESUWNEJ średniej stusekundowej: korekta nie może dotrzeć do pomiaru przez
  nawet 100 s, a pętla w tym czasie działa dwadzieścia razy. Wzmocnienie
  wynosiło połowę plantu (`acq.Kp = 0.5 * lsb_per_hz`), więc przykładało około
  dziesięciokrotności tego, co potrzebne, zanim pomiar zdążył odpowiedzieć.

  Zmierzone 25.08 21:06, zimne wejście w ACQ przy OCXO już w granicach 0,02 Hz:
  PWM przeszło **31229..51512** — dwadzieścia tysięcy LSB — Vctl 1,38..2,15 V,
  strażnik runaway zadziałał dwa razy, a bieg skończył się na **+2,53 Hz** z
  PWM zamrożonym przez ostatnie 527 s. Symulator odtwarza to z samych
  wysyłanych stałych (PWM ±13319, koniec na 2,79 Hz) i rozbiega się nawet ze
  startu jednego LSB — stąd „bezwarunkowo".

  Granica to `Kp * K < 2 * okres / okno` = 0,10. Przemiecione w symulacji od
  startu 0,02 Hz: 0,50 rozbiega się, 0,25 pełznie, 0,10 to krawędź (334 s),
  0,05 ustala się w 167 s. `acq.Kp` to teraz **0,05 × lsb_per_hz**, dwukrotny
  zapas do granicy, i zbiega monotonicznie ze startów 1 LSB, 0,02 Hz, 1 Hz i
  3 Hz bez żadnego przeregulowania (szczyt 0,95× offsetu startowego, najgorszy
  przypadek ustalony w 603 s). Skaluje się zmierzonym K płytki, więc przenosi
  się na dowolny OCXO.

  Gryzło to wyłącznie ZIMNE ACQ. `g_ltic.state` jest zapamiętywane, więc ciepły
  start wraca do DPLL albo LOCK i tej ścieżki nie uruchamia — dlatego algorytm
  wysłany w v1.04 pokazał to dopiero teraz.

- **Centrowanie w ACQ jest bramkowane „nie na ryglu", a nie „w paśmie" — a błąd
  jest ograniczany do pasma zamiast być razem z nim wyrzucany.** Jeden człon,
  dwie przeciwne usterki. Bramka na pełnym teście pasma zatrzymała szarpnięcie
  z 20.08 (Vphase 3,187 V przy paśmie 0,818..2,865 V, err_v +1,35 V, 690 LSB
  przemiecione), ale wyłącza też wciąganie zawsze, gdy pasmo zapisane przez LC
  jest węższe niż detektor naprawdę — a na tym sprzęcie jest piątą jego
  częścią. Zmierzone 25.08 przy poprawionym wzmocnieniu ACQ: po uzbrojeniu
  picDIV faza zaparkowała na −1320 ns, czyli 1,583 V przy zapisanym paśmie
  1,729..2,433 V. Poza pasmem, więc brak centrowania; częstotliwość już na
  celu, więc brak członu częstotliwościowego; `u = 0`, PWM zamrożone, a
  ACQ→DPLL wymaga |faza| ≤ 200 ns. Trwały zastój przy wszystkich strażnikach
  milczących, bo nic nie było nie tak poza tym, że pętla sama się wyłączyła.

  Poza pasmem bez sensu jest WARTOŚĆ `V - centre`, nie jej znak: rampa jest
  monotoniczna aż do rygli, więc odczyt nadal mówi, gdzie jest dom — a ACQ
  więcej nie potrzebuje, bo to ograniczone szturchnięcie proporcjonalne, które
  niczego nie całkuje. Steruje teraz zawsze, gdy odczyt jest poza ryglami, z
  błędem obciętym do krawędzi pasma: w paśmie bez zmian, poza nim ciąg z
  krawędzi pasma zamiast szarpnięcia 1,35 V, na ryglu wstrzymanie jak dotąd.
  DPLL i LOCK zachowują ostry test — one fazę całkują, i po to ta bramka jest.

- **Tuner pokazywał nieaktualny stan pętli przy algorytmach z własnym
  słownictwem.** `parse_state()` przeszukiwał całą linię pod kątem stałej listy
  słów, a lista nie nadążała: firmware wypisuje `SYNC`, `FLL`, `ZC`, `HYB`,
  `NoPL`, `hit` i dziesięć słów kierunkowych w rodzaju `uf+`, których tam nigdy
  nie było. W tych sekundach funkcja nie zwracała nic, a etykieta pokazywała
  dalej to, co rozpoznała ostatnio — przy algorytmie 12 panel czytał **LOCK
  przez każdą sekundę CORR i ZC**. `[HOLDOVER]` też nie pasował, więc holdover
  był niewidoczny. Wyszukiwanie nie było zakotwiczone, więc którekolwiek z tych
  słów gdziekolwiek w dowolnej linii mogło ustawić stan.

  Trend jest POLEM, a nie słownikiem: to ostatni token linii `PWM:`, a w
  holdoverze firmware zastępuje go w całości. Brany po pozycji, sprawia, że
  słowo wymyślone jutro pokaże się dosłownie zamiast zniknąć, i nic innego na
  łączu nie może zostać z nim pomylone. `___` czyści teraz etykietę, zamiast
  zostawiać poprzednie słowo — wywołujący sprawdzał prawdziwość tam, gdzie
  musiał sprawdzić `is not None`, co było tym samym błędem nieaktualnego
  wyświetlania piętro wyżej. Piętnaście przypadków sprawdzonych na faktycznym
  wyjściu firmware'u.

- **`dph` na panelu dostał miejsce dziesiętne, które log miał od zawsze.**
  Raport szeregowy drukuje jedno miejsce, panel drukował całe nanosekundy —
  niewidoczne przy odczytach rzędu setek ns, rażące gdy pętla zeszła do cyfr
  jednostkowych: log mówił −5,2, a panel −5. Miejsca nie dało się dopisać wprost:
  oba pola fazy są wymiarowane na napis `+0000ns`, a komentarz panelu 320
  ostrzega, że odczyt pięciocyfrowy wyszedłby poza etykietę. Więc jedno miejsce
  poniżej 100 ns, gdzie najszersza forma `-99.9ns` to dokładnie siedem znaków, na
  które pole zmierzono, i liczby całkowite powyżej. Sprawdzone przez cały zakres
  detektora: najszerszy napis, jaki którykolwiek format może dać, ma siedem
  znaków, więc żadnego wypełnienia nie trzeba przycinać. `dtostrf`, nie `%.1f` —
  ten plik celowo nie używa konwersji zmiennoprzecinkowych w `snprintf`, bo bez
  Float printf w IDE drukują `?`.

- **Zakładka Help w tunerze dogoniła firmware.** `DAC` opisuje argument ścieżki;
  `LTO`/`LTR` mówią o woltach; domyślne `MR` to 7, nie 9; `AQI`/`AQD` oznaczone
  jako zapisywane-ale-bezwładne ze wskazaniem na `ACG`; lista trendów algo 12
  obejmuje całe słownictwo, a nie trzy słowa z niego; `SAW` opisuje liczniki
  parowania; a `FA`/`FAD`/`FAL` są w ogóle udokumentowane — brakowało ich od
  czasu dodania. Sprawdzone mechanicznie względem listy verbów wyciągniętej
  z `gpsdo_cli.cpp` — nic, na co firmware odpowiada, nie brakuje już w zakładce.

- **`DAC PWM|DITH|EXT` — ścieżka napięcia sterującego wybierana w czasie
  pracy.** Wszystkie trzy ścieżki wyjścia kompilują się teraz razem, a komenda
  wybiera, którą firmware steruje. SYGNAŁ przełączają zworki na płytce; nie ma
  programowego multipleksera i być nie może, bo dwa sterowniki walczące o
  napięcie kontrolne to usterka sprzętowa, a nie tryb pracy. Firmware musi
  wiedzieć tylko, którą ścieżką steruje — po to, by rozmiar kroku, telemetria i
  arytmetyka ścieżki dokładnej opisywały to, co jest faktycznie podłączone.

  `GPSDO_PWM_DITHER` i `GPSDO_DAC_EXT` wykluczały się przy kompilacji i już się
  nie wykluczają — piny nigdy nie kolidowały (PB9/TIM4 kontra PB4/PB0/PB2),
  kolidowało tylko założenie, że jedna binarka steruje jednym wyjściem. Jedna
  binarka obsługuje teraz oba warianty okablowania, a porównanie dither
  włączony/wyłączony to komenda zamiast przegrywania firmware'u.

  PWM i DITH dzielą PB9/TIM4 CH4, więc wybranie PWM na płytce z silnikiem
  ditheru **nie** rozbiera DMA i nie oddaje pinu do `analogWrite`. Zapisuje ten
  sam kod 24-bitowy z wyzerowanymi ośmioma młodszymi bitami: każdy wpis tablicy
  identyczny, stałe wypełnienie, co do bitu to napięcie, które dawał zwykły PWM
  — to samo wyjście osiągnięte bez rekonfiguracji, która mogłaby się zawiesić w
  połowie. `gpsdo_dac_fine_available()` staje się cechą ścieżki AKTYWNEJ, a nie
  buildu, więc pętla sterująca ułamkami dowiaduje się, kiedy ścieżka
  pełnobitowa zaraz je wyrzuci.

  Zapis w jednym bajcie wyciętym z wyrównania między `tz_str` a `a12_gain` —
  sprawdzonym kompilatorem, nie okiem — więc układ, rozmiar i `SETTINGS_VER`
  pozostają bez zmian, a blok zapisany starszym buildem nadal się wczytuje.
  Zero znaczy NIEUSTAWIONE i prosi o domyślną, dlatego kodowanie zaczyna się od
  1: stary zapis czyta się jako „użyj domyślnej", a nie jako „ścieżka 0".
  **Domyślną jest DITH**, rozstrzygana przy starcie względem tego, co
  faktycznie skompilowano, i nigdy nie rozstrzygana na ścieżkę, która nie może
  sterować pinem. Na buildzie z zewnętrznym DAC-iem, ale bez silnika ditheru,
  nieustawiona wartość daje PWM, nie EXT — układ zewnętrzny to świadomy wybór
  sprzętowy i trzeba go zażądać.

  Raport drukuje teraz napięcie zadane obok zmierzonego i ostrzega, gdy różnią
  się o więcej niż pół wolta. Firmware nie widzi zworki; jedynym dowodem, że
  ustawienie i okablowanie się nie zgadzają, jest to, że napięcie sterujące nie
  jest tam, gdzie mu kazano. Pół wolta jest luźne celowo — dzielnik ADC i
  odniesienie są dobre w najlepszym razie do kilku procent — bo łapać ma
  „zadane 1,80 V, zmierzone 0,00 V", a nie błąd skali.

- **`LTO` i `LTR` przyjmują wolty, jak wszystko inne na tym detektorze.** Ten
  sam punkt fizyczny — zero fazy detektora — był trzymany w dwóch jednostkach,
  które się nie zgadzały: `LZO` = 2,0809 V dla algorytmu 10, `LTO` = 2620
  zliczeń ADC dla algorytmu 11, czyli 2,1104 V. Trzydzieści siedem zliczeń
  różnicy, około 37 ns po poprawce skali — i niewidoczne, bo nikt nie porównuje
  okiem 2,0809 z 2620. Obie komendy przyjmują i drukują teraz wolty; blok
  ustawień dalej trzyma zliczenia, dokładnie tak jak `MLP` robi to z
  nanosekundami, więc bez podbicia `SETTINGS_VER` i bez migracji. Obie drukują
  też liczbę zliczeń, bo to ona wyjdzie w zrzucie flasha. Wartość między 3,3 a
  4095 jest odrzucana z gotowym przeliczeniem — „2620 counts = 2,1104 V — type
  that" — bo to jedyna pomyłka, którą ktokolwiek naprawdę popełni. W tunerze
  `LTO` i `LTR` stają się polami w woltach, **każda** etykieta parametru niesie
  jednostkę (albo jawne „(x)" dla wielkości bezwymiarowej), a wzorzec odczytu
  parametrów algo 11 przestał być kotwiczony na końcu linii — inaczej po cichu
  wyrzucałby każdą odpowiedź `tic_offset=2.1104 V (2620 counts)` i zostawiał
  puste pole, podczas gdy płytka odpowiada bez zarzutu.

- **`AQI` i `AQD` nie robiły nic i nic o tym nie mówiły.** ACQ czyta `pid->Kp`
  i nic więcej; ciąg centrujący bierze się z `g_ltic_acq_centre_gain`, komenda
  `ACG`, osobny global z własnymi jednostkami. `acq.I_LIMIT` **jest** żywe —
  to ogranicznik kroku — więc bezwładna jest dokładnie para Ki/Kd. Były
  ustawiane przez autotune, drukowane przez `LL`, ustawialne przez `AQI`/`AQD`,
  zapisywane i oferowane jako edytowalne pola w tunerze: pięć sposobów na
  powiedzenie, że pokrętło działa, gdy kręcenie nim nie zmienia niczego. Każdy
  odczyt i zapis mówi to teraz wprost, `LL` ma tę samą notkę przy wierszu ACQ,
  a tuner wyszarza oba pola — odczyt nadal pokazuje, co trzyma płytka, tylko
  nie da się tym przekręcić. Verby nadal przyjmują wartość, bo tuner wysyła
  całą czwórkę przy „Apply" i odrzucenie wyglądałoby na usterkę tunera.
  Usunięcie pól to wejście w blok ustawień, czyli osobna robota.

- **Wiersz fazy na TFT odejmował piłę NASTĘPNEGO impulsu.** Obie ścieżki
  wyświetlania pytały `ubx_timtp_correction_ns()` o korektę w chwili rysowania,
  a ta zwraca ostatnio zdekodowany qErr. Raport szeregowy jest bramkowany
  zmianą `ppscount`, więc biegnie przy pierwszym obudzeniu po impulsie i dostaje
  właściwy. TFT przerysowuje się przy KAŻDYM obudzeniu `vDisplayTask` — a jedno
  z nich przychodzi od parsera GPS, już po przetrawieniu paczki szeregowej
  odbiornika. TIM-TP siedzi w tej paczce, więc do tego czasu `g_qerr_ns` przeszło
  na następny impuls, podczas gdy `g_ltic_voltage` to wciąż rampa tego impulsu.
  Panel odejmował qErr(N+1) od fazy(N).

  Zauważone jako odczyt na panelu wyraźnie większy niż linia logu wypisana w tej
  samej sekundzie — i „większy" jest tu dokładnie tym, czego należało oczekiwać:
  na tym odbiorniku kolejne wartości qErr są ANTYskorelowane (corr −0,30, okres
  2–3 s), więc korekta sąsiedniego impulsu dokłada piłę zamiast ją zdejmować.
  Zmierzone na zapisie z 25.08: odjęcie sąsiada zabiera resztę z 11,46 ns na
  13,6 ns — gorzej, niż nie korygować wcale. Alan Cashin podniósł dokładnie ten
  tryb awarii dla pętli tego samego dnia; siedział w wyświetlaczu.

  `ltic_read_fast()` biegnie ~50 µs po zboczu PPS, przed paczką niosącą następną
  ramkę, więc widoczny tam qErr wciąż należy do tego impulsu. Jest teraz
  zatrzaskiwany raz, a wszyscy czytelnicy używają zatrzasku — raport, panel i
  pętla zgadzają się z konstrukcji, niezależnie od tego, kiedy zostaną narysowane.

- **Algorytm 10 przez noc, 26.08: 9,7 h w LOCK, ani jednego wypadnięcia.** ACQ
  zajęło 225 s, DPLL 47 s, a po t = 410 s automat stanów już się nie ruszył.
  Ustalone przez 9,29 h: średnia fazy **+0,09 ns**, sd 7,42 ns, dryf
  **−0,10 ns/h**, PWM w paśmie 32 LSB przy jednej korekcie na 53 s. ADEV
  nakładany: 4,4e-9 przy 1 s, 1,0e-10 przy 128 s, 1,2e-11 przy 1024 s,
  1,7e-12 przy 8192 s. Resztkowa korelacja z qErr utrzymała +0,021 przez całą
  noc, więc zniesienie piły nie jest artefaktem krótkiego biegu.

  Bieg pokazuje też, gdzie mieszka teraz reszta błędu. Rozbicie fazy na szum
  próbki (z pierwszych różnic) i wszystko wolniejsze:

  ```
                            sd      podłoga szumu   struktura wolna
    algo 11, 5,2 h        3,11 ns      2,56 ns          1,76 ns
    algo 10, to samo okno 7,76 ns      2,53 ns          7,34 ns
  ```

  Podłoga detektora jest identyczna — ta sama płytka, ten sam odbiornik — więc
  cała różnica siedzi w pętli. Algorytm 11 koryguje co sekundę ze stałą czasową
  60 s; LOCK algorytmu 10 koryguje raz na 53 s całkowaniem fazy o stałej
  odbudowy bliższej 800 s, i faza błądzi ±20 ns w okresach 250–2400 s, bo pętla
  jest wolniejsza niż to, co ją rusza. Ograniczeniem jest teraz LOCK, nie
  detektor.

- **`ltic_autotune()` przelicza się tylko wtedy, gdy zmieniły się jego dane
  wejściowe.** Każdy wyprowadzany współczynnik jest czystą funkcją
  `lsb_per_hz` (z CT) i `range_ns` (z LC), a mimo to funkcja biegła przy każdym
  przejściu w ACQ i po cichu wyrzucała wszystko, co wpisał operator. Trafiło to
  dwa razy jednego wieczoru przy ręcznym testowaniu wzmocnienia ACQ: wpisujesz
  `AQP`, pętla spada do ACQ, autotune wstawia starą wartość z powrotem, i
  patrzysz na strojenie, które według ciebie już wymieniłeś — a w logu ani
  słowa. Teraz biegnie raz na start i ponownie, gdy `CT` albo `LC` ruszy
  zmierzone stałe. Tego właśnie wymaga zamiar „żadne strojenie per płytka nie
  jest potrzebne".

- **Strażnik runaway nie mógł się nigdy zwolnić.** Zamrożenie ustawiało
  `u = 0`, co zostawia OCXO tam, gdzie rzucił go wybieg; faza pędzi wtedy przez
  detektor bez końca, `railed_now` się nie czyści, `|e_freq|` nie spada poniżej
  0,25, a warunek zwolnienia nie może zostać spełniony. Bieg z 25.08 siedział
  w dokładnie tym stanie ostatnie 527 s. Strażnik cofa teraz PWM w stronę
  `start_pwm` — ostatniego kodu trzymanego przy zdrowej pętli — po najwyżej
  50 LSB na cykl, zamiast stawać w miejscu: wybieg 20 000 LSB rozwija się w
  jakieś pół godziny, a strażnik zwalnia w chwili, gdy częstotliwość po drodze
  wróci poniżej 0,25 Hz.

- **`LNV` / `LZO` / `LRN` ustawione ręcznie nie przeżywały resetu.** Kalibracja
  detektora mieszka w dwóch miejscach i przy starcie wygrywa to drugie: `LC`
  zapisuje ją do slotu live-store, `ES LTIC` do bloku ustawień, a `setup()`
  stosuje najpierw blok ustawień, a slot live PO nim. Zmierzone 25.08:
  `LNV 1252` ustawione, zapisane — i płytka wstała na 2649,3914 bez słowa.
  `ES LTIC` odświeża teraz także slot live, więc oba są zgodne i kolejność
  ładowania przestaje mieć znaczenie. Drugim kandydatem była zmiana kolejności,
  ale `LC` zapisuje WYŁĄCZNIE do slotu live, więc uczynienie bloku ustawień
  nadrzędnym odebrałoby zwykłemu `LC` przeżycie restartu.

- **Test przejścia przez zero uzbraja tylko korekta Z LIMITU (reguła Alana).**
  Jedna flaga robiła dwie rzeczy: blokowała drugą korektę, póki celowy slewing
  pierwszej sprowadza fazę do domu — to nasz dodatek, po przeregulowaniu
  +3800 LSB z 14.08 — i uzbrajała zniesienie w przejściu przez zero. Tylko to
  drugie jest Alana, a jego reguła jest węższa, niż zrobił to nasz port: korekta
  planowa (poziom `MR`) pada z zegara, przy fazie tam, gdzie akurat jest, więc
  nie ma znanego slewingu do zniesienia ani powodu, by w ogóle oczekiwać
  przejścia; a samo ZC się nie uzbraja, bo ZC *jest* zniesieniem. Uzbrojenie w
  którymkolwiek z tych przypadków pozwalało niezwiązanemu przejściu kilka minut
  później wyciągnąć krok z nieaktualnego nachylenia. Dwa zadania to teraz dwie
  flagi: blokada osiadania nadal po każdej korekcie, uzbrojenie — wyłącznie po
  ścieżce limitu.

- **Domyślne `MR` to 7 (256 s), nie 9 (1024 s)** — wartość samego Alana i ta,
  która czyni korektę planową koniem roboczym, którym ma być, a nie
  zabezpieczeniem raz na 17 minut. Kod zmienił się przy pracach nad algorytmem
  12; trzy instrukcje do teraz pisały 9 w dwóch miejscach każda.

- **`MLP` i `ML` mówią w nanosekundach.** Tablica limitów jest przechowywana w
  jednostkach akumulatora — poziom trzyma 2^(poziom+1) próbek `2*faza + 1`, więc
  zapisana liczba to faza przesunięta w lewo o `poziom+2` — a obie komendy
  drukowały tę surową liczbę, nazywając ją „ns". Strojenie wielkości, której nikt
  nie odniesie do oscyloskopu, to strojenie na ślepo. `MLP <n>` pokazuje teraz
  obie (`lim[6]=126ns (32350 units over 128s)`), `MLP <n> <ns>` przyjmuje
  nanosekundy, `ML` tabelaryzuje ns obok jednostek i okna, a pola limitów w
  tunerze są w nanosekundach. Format zapisu bez zmian, więc blok ustawień i
  istniejące zapisy są nietknięte.

- **Nic już nie twierdzi, że detektor fazy jest obecny, skoro nie może tego
  wiedzieć.** Dave Solder_Junkie zbudował płytkę bez detektora i każda warstwa
  mówiła mu, że jest dobrze: baner startowy drukował
  `HW: LTIC phase input OK (PA1 analog)`, `LA 12` zostało przyjęte, a pętla
  dyscyplinowała OCXO szumem ADC na pływającym pinie. Trzy zmiany — żadna nie
  wykrywa sprzętu, bo nic go nie wykryje — ale przestają udawać. Baner brzmi
  teraz `enabled (PA1) - needs the ramp detector hw`. Komentarz przy
  `GPSDO_LTIC` w `gpsdo_config.h` mówi wprost o wymaganiu, zamiast opisywać
  układ. A `LA 10`, `LA 11` i `LA 12` sprawdzają, czy `LC` KIEDYKOLWIEK biegło:
  jeśli nachylenie TIC *i* zakres detektora są nadal domyślne, ostrzeżenie nie
  jest już łagodnym „uncalibrated", tylko „no detector calibrated, phase may be
  floating — is the hardware really there?".

- **Centrowanie w ACQ wstrzymuje się przy nieważnym odczycie detektora**, tak
  jak robi to od dawna algorytm 12. Człon centrujący steruje surowym napięciem,
  a nasycony detektor podaje napięcie, które już nie śledzi fazy; bramka dryfu
  tego nie łapała, bo odczyt na rygle jest płaski i jego dryf wychodzi zero.
  Zmierzone 20.08 19:42, trzy sekundy po przełączeniu na algorytm 10: Vphase
  3,187 V przy paśmie 0,818..2,865 V, człon centrujący nasycony na własnym
  ograniczeniu i pchający tak długo, jak detektor był poza pasmem, 690 LSB PWM
  przemiecione, zanim się uspokoiło. Wstrzymanie nic nie kosztuje — ścieżka
  częstotliwości pracuje z TIM2, który widzi offset niezależnie od tego, co robi
  detektor, i to jest dokładnie to, na co spada algorytm 12.

### Dodane
- **Zewnętrzny DAC AD5680: sterownik istnieje.** `dac_ext.cpp` wychodzi ze
  stadium stuba — bit-bang na GPIO CS/SCK/MOSI = PB4/PB0/PB2 (ścieżki PCB
  Dana Wieringa; PB2 to zarazem BOOT1, więc ścieżka MOSI bez pull-upa),
  słowo 24-bit MSB-first (`code << 2`, zerowe bity komend = zapis i tryb
  normalny), DIN próbkowane na opadającym zboczu SCLK, zatrzask na
  narastającym SYNC, przesłanie pod ~20 µs zamaskowanych przerwań. Włącza
  `GPSDO_DAC_EXT`. Przy okazji: TM1637 i generator 2 kHz są domyślnie
  WYŁĄCZONE (polityka v1.06, opcje historyczne) i automatycznie ustępują
  pinów przy włączonym DAC-u zewnętrznym — zamiast #error build po prostu
  je pomija.

  Buduje się już na prawdziwym toolchainie: `hostcheck` dostał dwa wiersze
  AD5680 (z detektorem fazy i bez) i oba kompilują się oraz linkują dla
  cortex-m4 pod `arm-none-eabi-g++`. To zamyka zastrzeżenie „tylko przegląd",
  z którym sterownik wyszedł — nie zamyka testu na sprzęcie, ten należy do
  Dana.
### Dodane
- **Algorytm 13 — trzystanowy filtr Kalmana (faza, częstotliwość, starzenie).**
  Każda dotychczasowa pętla w tym firmware ma pasmo wybrane raz i przyjęte na
  stałe: algorytm 10 przełącza się między trzema, algorytm 11 ma stałą czasową,
  algorytm 12 wybiera poziom z tablicy progów. Wszystkie trzy odpowiadają na to
  samo pytanie — ile z tegosekundowego odczytu uwierzyć — liczbą ustaloną z góry.
  Ten odpowiada z wariancji i odpowiada od nowa co sekundę.

  **Kosztuje tyle, że nie warto liczyć.** Trzy stany i pomiar **skalarny**, więc
  słynne odwracanie macierzy to jedno dzielenie: około 140 mnożeń z dodawaniem i
  36 bajtów stanu, raz na sekundę, mniej więcej mikrosekunda na M4F 100 MHz.
  Obiegowa opinia, że Kalman wymaga Cortex-A albo FPGA, dotyczy dwudziestostanowych
  filtrów w odbiornikach GNSS, a nie tego.

  **Obie liczby szumu są mierzone, nie ustawiane.** R bierze się z pierwszych
  różnic samego detektora — ten sam estymator, którego algorytm 12 używa do
  swojej sigmy. Q adaptuje się z ciągu innowacji: filtr przewiduje, jak duże
  powinny być jego własne zaskoczenia, a gdy konsekwentnie są większe, to znaczy
  że szum procesu jest za mały. `KR` i `KQ` przypinają którąkolwiek na potrzeby
  eksperymentu; zero znaczy „zmierz". Zaszczepione wartościami z nocy 26/27.08 —
  2,64 ns i 2e-6 (ns/s)²/s, przy czym ta druga zgodna w oknach 600, 1800 i
  3600 s, co jest podpisem random-walk FM — więc filtr jest sensowny od pierwszej
  sekundy, a nie dopiero po zbieżności estymatorów.

  **Holdover nie potrzebuje własnego kodu.** Stan niesie częstotliwość ORAZ
  starzenie wraz z kowariancjami, więc utrata fazy nie jest przypadkiem
  szczególnym: przestań aktualizować, dalej przewiduj, dalej steruj. Trend
  pokazuje `HOLD`, a `KL` mówi, jak długo pętla jedzie na samym modelu.

  Zmierzone `tools/loopsim`, na oscylatorze odtworzonym z logów 26/27.08, pięć
  ziaren szumu, wobec pętli, które już są:

  ```
                       sd fazy [ns]        ADEV @ 1024 s
                   okno algo-12 / algo-11  okno algo-12 / algo-11
    algorytm 11      3,62 / 7,39            7,7e-12 / 1,5e-11
    algorytm 12      3,07 / 4,20            4,1e-12 / 7,6e-12
    algorytm 13      1,20 / 1,25            1,4e-12 / 1,8e-12
  ```

  Ciekawa jest druga kolumna. Pozostałe dwie pętle tracą na planszy z większym
  dryfem, a ta nie — i to jest właśnie adaptacyjne pasmo przy pracy, a nie lepiej
  dobrana stała.

  Dwie bariery, obie wstawione przez symulator, a nie przez gust. **Bramka, która
  nigdy się nie otwiera, to zepsuty filtr**: bramka innowacji odrzuca odczyt
  odległy o więcej niż cztery sigmy od przewidywania — noc 26/27.08 miała dwa
  takie piki po ±40 ns — ale dziesięć odrzuceń z rzędu znaczy, że zły jest STAN,
  a nie dane, więc filtr poszerza własne przekonanie o rozmiar tego, co
  uporczywie odrzuca. Bez tego start 1500 ns od zera nigdy nie wracał. Oraz:
  **faza wciąż poza oknem ACQ po pięciu horyzontach to odczyt, który nie rusza
  się razem z oscylatorem** — a na tej płytce znaczy to niezsynchronizowany
  picDIV: uzbrój go raz i uruchom filtr od nowa.

  Nowe komendy `KR` / `KQ` / `KT` / `KL`, zapisywane od razu we własnym rekordzie
  pierścienia flash (`REC_A13`), a nie w bloku ustawień, w którym nie ma już
  wyrównania do wykorzystania — powiększenie go wymusiłoby podbicie
  `SETTINGS_VER` i skasowanie wszystkim PID-ów, LC i strefy czasowej dla trzech
  liczb.

- **TAB albo ESC zatrzymuje i wznawia telemetrię jednym klawiszem.** Pomysł
  Alana Cashina i trafiony: `RP` i `RR` robią to samo, ale wpisanie komendy, gdy
  raporty przewijają się w oczy, jest dokładnie tym, co jest trudne — a lekarstwo
  nie powinno samo wymagać spokojnej chwili na wpisanie. Samo ESC przełącza tak
  jak TAB; ESC z następującym `[` albo `O` to strzałka lub klawisz funkcyjny i
  zostaje połknięty, więc sięgnięcie po historię powłoki nie zatrzymuje już
  raportów. Niedokończona linia jest przy przełączeniu porzucana, a nie doklejana
  do następnej.

- **Obciążenie procesora w linii telemetrii.** `CPU:7%`, dopisane do linii
  czujników. Mierzone, nie modelowane, i bez własnego timera:
  `vApplicationIdleHook()` inkrementuje licznik, a raz na sekundę liczba zamienia
  się w procent względem najwyższego zliczenia, jakie kiedykolwiek padło — a to z
  definicji sekunda bezczynna. Alternatywą było
  `configGENERATE_RUN_TIME_STATS`, które chce wolnego timera i zegara bazowego,
  żeby wyprodukować statystyki per zadanie, o które nikt nie prosił.

  Co widzi: wszystko, czego nie dostało zadanie bezczynne, łącznie z czasem w
  przerwaniach, bo ISR podbiera temu, co akurat biegnie. Czego nie widzi: płytki,
  która nigdy nie była bliska bezczynności — wtedy odniesienie jest zaniżone, a
  wskazanie zbyt optymistyczne. Odniesienie ucieka o ~0,02% na sekundę, więc
  podąża za płytką, zamiast zostać przybite jedną szczęśliwą sekundą przy
  starcie. Dopisane na KOŃCU linii celowo: każdy czytnik szuka swojego pola, a
  nie kotwiczy na końcu, ale wstawienie pola w środku to i tak klasyczny sposób
  na ciche zepsucie czyjegoś wyrażenia regularnego.

### Zmierzone
- **Liczba „27% gorzej od algo 11" była błędna i ją wycofuję.** Wzięła się z
  porównania dwóch różnych nocy, a oba stojące za nią przebiegi algo 13 były źle
  skonfigurowane: w jednym `KR` było przypięte na 2,5 ns, co kosztowało jedenaście
  procent odczytów na bramce innowacji, a w sesji z 29.08 12:31 estymator R
  uciekał w trakcie wciągania do **47,70** — czyli dokładnie ta awaria, dla której
  powstał bezpiecznik „zamroź, gdy pętla jedzie", na buildzie sprzed niego.
  Żaden z tych przebiegów nie powinien był służyć do oceny pętli.

  Log z 28.08 przełącza algorytmy w trakcie sesji, co w jedno popołudnie
  rozstrzyga to, czego porównanie międzynocne nie rozstrzyga wcale. Algo 13 szło
  4,24 h, a zaraz po nim algo 11 przez 3,53 h, na tej samej płytce:

  ```
    algo   sd fazy  podłoga  wolne  track sd    r    wymagane  zadane
     13     3,67     2,72    2,46     0,54    0,996    20,7     59,0
     11     3,22     2,70    1,76     0,48    0,959    10,0     23,0
     12     6,18     2,67    5,57     0,87    0,959    20,0     28,0
  ```

  Trzynaście procent różnicy w błędzie śledzenia, czternaście w sd fazy — przy
  czym algo 13 dostało **trudniejszą połowę**: wymagana rozpiętość sterowania
  20,7 LSB wobec 10,0 dla algo 11. Oba są daleko przed algo 12 w tej samej sesji.
  To jest inna pętla niż ta, którą opisywała liczba międzynocna.

- **Razem z nią wycofuję werdykt o estymatorze R.** Replay mówił, że lag 16 s
  wypada minimalnie gorzej niż 1 s; stół mówi, że R sięgnęło **47,70** na
  buildzie bez bezpiecznika, przy białej podłodze 6,4. `loopsim` tego nie
  odtwarza, bo jego plansze nie mają dość dużego transjentu wciągania — ucieczka
  dzieje się wtedy, gdy pętla rusza fazą, czyli dokładnie w stanie, przez który
  bezpiecznik zamraża estymator. Zmiana zostaje, a wynik z replayu zostaje tym,
  czym jest: pomiarem sytuacji, której replay nie zawiera.

- **Jedna różnica w zachowaniu przeżywa każdą sesję: algo 13 katuje wykonawczy
  o wiele mocniej, niż musi.** 59 LSB zadane przy 20,7 wymaganych na 28.08 i 111
  przy 8,0 na złym przebiegu z 29.08, wobec 23 przy 10,0 i 17 przy 6,1 dla
  algo 11. To nie jest niedosterowanie — ta hipoteza jest zamknięta — to jest
  konsekwentne przesterowanie i to zostaje do ugryzienia.

### Dodane
- **`tools/logab.py` — porównanie algorytmów ze sobą wewnątrz jednego logu.**
  Dla każdego ciągłego odcinka jednego algorytmu wypisuje sd fazy, podłogę
  detektora, wolną strukturę, za którą odpowiada pętla, oraz błąd śledzenia
  wobec sterowania, którego oscylator naprawdę potrzebował (odtworzonego tak, jak
  `loopsim` buduje swoje plansze). Dwa algorytmy w dwie noce to dwa
  eksperymenty; jeden log, który się między nimi przełącza, to porównanie.

  Naprawia też pułapkę, która kosztowała dzień: linia Learn ma inny kształt dla
  każdej rodziny algorytmów, więc regex napisany pod jedną z nich po cichu
  zachowuje poprzednią wartość dla pozostałych — i za pierwszym podejściem
  zamienił sesję z czterema algorytmami w jeden 20-godzinny odcinek „algo 13".
  Z linii Learn brane jest tylko `algo=`; reszta z linii, które drukuje każdy
  algorytm.

### Zmierzone
- **Trzy przebiegi algo 13, a jedyny pewny wniosek jest taki, że przypięte `KR`
  kosztowało jedenaście procent odczytów.** Przebieg z 29/30.08 wyrzucił na
  bramce innowacji **4904 z 45275** próbek — `rej` poszło z 98 na 5021 — i nic
  tego nie powiedziało: linia Learn niesie licznik skumulowany, którego nikt nie
  różniczkuje, patrząc jak się przewija, a sd fazy wyglądało zwyczajnie na
  6,07 ns. `KR` było przypięte na 2,5 ns, co ustawia R poniżej innowacji, jakie
  detektor faktycznie produkuje, i zostawia bramkę 4 sigma za ciasną. Dwa krótkie
  przebiegi z 30.08, z R mierzonym, nie odrzuciły **ani jednej** — i symulator
  też nie. Czyli: zostawiaj `KR` na 0, chyba że przypięcie jest eksperymentem.

- **Pętla nie niedosterowuje**, co było podejrzeniem po obserwacji 57 wobec
  93 LSB. Odtworzenie z każdego logu tego, czego oscylator naprawdę potrzebował:
  wymagane sterowanie miało rozpiętość **17,6 LSB** w nocy algo 13 i **18,6 LSB**
  w nocy algo 11 — noce były porównywalne, jak tylko można chcieć — a pętle
  zadały odpowiednio 61 i 93 LSB. Obie ruszają trzy do pięciu razy więcej, niż
  trzeba; algo 11 rusza **więcej** i mimo to trzyma fazę lepiej.

- **Metryka, która rozróżnia, i ostrzejsze postawienie sprawy z symulatorem.**
  Wygładzona po 300 s korelacja między sterowaniem zadanym a wymaganym wynosi
  **0,992 dla algo 11 i 0,942 dla algo 13**, przy błędach śledzenia 0,70 i
  0,81 LSB. `loopsim` liczy teraz te same dwie liczby i na odtworzonym
  oscylatorze z 26.08 daje dla algo 11 **0,71 LSB** wobec 0,70 ze stołu —
  rekonstrukcja planszy jest zdrowa, a metryka trafna. Dla algo 13 daje 0,20 LSB
  wobec 0,81 ze stołu. Rozbieżność jest **specyficzna dla tej jednej pętli**, nie
  dla planszy.

  Kalibracja skorelowanego błędu detektora w symulatorze tego nie zamyka: algo 13
  pasuje przy około 12 ns błądzenia zera (0,96 LSB, r 0,944), a algo 11 pasuje
  przy zerze (0,71, r 0,971). Nie ma ustawienia, przy którym oba są prawdziwe.
  Ten sam detektor zachowuje się tak, jakby dla jednej pętli był o 12 ns
  głośniejszy niż dla drugiej.

### Odrzucone
- **Podnoszenie R tego nie naprawia.** Lag estymatora R przemieciony w punkcie
  zgodnym ze stołem: 1 s daje 0,96 LSB / r 0,945, 16 s daje 1,01 / 0,939, a 64,
  128 i 256 s monotonicznie gorzej, do 1,49 / 0,886. Przypięcie Q niżej — drugi
  sposób na mocniejsze filtrowanie — jest jeszcze gorsze (1,30 LSB przy 1e-7
  wobec 1,01 adaptacyjnie); przypięcie wyżej jest minimalnie lepsze. Każda
  dźwignia sprawdzona dotąd idzie w złą stronę albo nie robi nic: lag R, Q,
  horyzont od 50 do 800 s, podanie starzenia w przód i test bieli innowacji.
  Mechanizm jest prawdziwy — skorelowany błąd detektora degraduje tę pętlę pięć
  razy szybciej niż algo 11 — ale inflacja R nie jest na to lekarstwem, bo
  wolniejszy filtr gorzej śledzi prawdziwy dryf.

  A/B w JEDNĄ noc nadal jest tym eksperymentem, który to rozstrzyga, a dopóki go
  nie ma, każda kolejna zmiana w tej pętli jest zgadywaniem.

### Naprawione
- **Odczyt CPU na belce TFT gubił ogon znaku `%`, a build 320x240 w ogóle się
  nie kompilował.** Dwie osobne rzeczy, znalezione razem, bo obie ma teraz kto
  sprawdzić.

  Pole CPU było centrowane na `TFT_W / 2`. Zegar LMT obok jest kotwiczony do
  prawej z paddingiem `TFT_S(130)`, a wymazanie paddingu w TFT_eSPI to prostokąt
  tej szerokości kończący się na kotwicy — 276..471 na panelu 480. `CPU 66%` we
  FreeSans9pt ma około 79 px, więc wycentrowane na 240 kończy się koło 279,
  trzy piksele w środku tego pasa, a zegar (rysowany po nim) ściera ostatni
  glif. Na panelu 320 ta sama arytmetyka zostawia jakiś jeden piksel — to nie
  jest margines, to zbieg okoliczności: odczyt trzycyfrowy tnie się i tam. Pole
  jest teraz centrowane w luce, jaką faktycznie zostawiają dwa pozostałe, obie
  szerokości brane z `textWidth()`, a nie zakładane — panele nie używają nawet
  tego samego kroju — i rysowane **po** zegarze, więc żadne wymazanie go już nie
  dosięgnie, jakiekolwiek by te szerokości były. Jeśli luka nie mieści napisu,
  nie rysuje nic: ucięta liczba jest gorsza niż jej brak. Stała paddingu ma
  teraz jedną nazwę, `HDR_LMT_PAD`, bo dwie kopie liczby, od której zależy
  jednopikselowy margines, to jest dokładnie to, jak do tego doszło.

- **Build TFT 320x240 miał zmienną zadeklarowaną w gałęzi 480 i użytą w gałęzi
  320** (`phs`, sformatowana faza). Zapewne od czasu rozdzielenia tego wiersza —
  i nikt nie miał jak tego zauważyć: **żaden test nie kompilował ani jednej
  linii kodu wyświetlacza.** Wszystkie przełączniki paneli były w
  `tools/hostcheck` wyłączone z braku biblioteki, a blok wyświetlacza to
  największa rzecz w `gpsdo_tasks.cpp`. Konfiguracja, której nikt nie potrafi
  zbudować, nie jest konfiguracją, tylko pogłoską.

  `tools/hostcheck/stub/TFT_eSPI.h` to teraz tyle tego API, ile trzeba do
  kompilacji, a hostcheck urósł o trzy wiersze: oba panele z detektorem i mały
  bez. Czternaście konfiguracji. Nic nie rysuje i uciętego glifu nie wyłapie —
  to potrafi tylko panel — ale wyłapuje literówkę, zły typ, nieistniejący datum
  i zmienną poza zasięgiem, czyli tę klasę błędów, która tu naprawdę występuje.

### Zmierzone
- **Algo 13 na płytce, 11,8 h nocą: stały offset zniknął, a pętla jest o 27%
  gorsza od algo 11.** Oba fakty są ważne i tego drugiego nie umiem jeszcze
  wyjaśnić.

  Co zadziałało. Stały offset +18,7 ns z poprzedniego przebiegu to teraz
  **+0,06 ns** — poprawka księgowania w stopniu wyjściowym zrobiła dokładnie to,
  co miała. Wyprowadzenie z CT/LC wypisało `res 1.01ns R0 2.52ns Q0 6.36e-6
  P0 1500ns lim 750LSB arm<0.25Hz`, a zmierzone R ustaliło się na 6,28 wobec
  zasianego 6,35 — zasiew trafiony w granicach szumu. **163 odrzucenia bramki
  innowacji przez dwanaście godzin** wobec 281 w poprzednich pięćdziesięciu
  trzech minutach i **zero** uzbrojeń picDIV. Średnia estymata częstotliwości
  +0,0007 ns/s: bez biasu.

  Co nie zadziałało. Sd fazy **5,94 ns wobec 4,68 ns algo 11** na porównywalnej
  nocy 11,0 h na tej samej płytce — ta sama podłoga detektora (2,74 wobec
  2,64 ns), ten sam wychył termiczny (2,4 wobec 2,7 °C), więc to pętla, a nie
  pokój. ADEV 1,0e-11 przy 1024 s wobec 7,8e-12 i 2,7e-12 przy 4096 s wobec
  2,0e-12; identyczne przy 1 s i 16 s, czyli krótki koniec jest u obu
  ograniczony detektorem, a cała różnica siedzi w środkowych tau.

  **Symulator mówi coś odwrotnego, i to trzykrotnie.** Cztery próby zmuszenia go
  do zgodności zawiodły — skorelowany szum detektora, realistyczny TIM2, horyzont
  od 50 do 800 s i podanie starzenia w przód do sterowania. Jedna wskazówka
  została bez wyjaśnienia: przez noc ta pętla ruszyła PWM o **57 LSB tam, gdzie
  algo 11 ruszyło o 93** na porównywalnej nocy. To sygnatura **niedosterowania**,
  a nie gonienia szumu. Przebiegi były z różnych nocy, więc uczciwy następny krok
  to A/B w jedną noc — po dwie godziny każdego, na przemian — co wyjmuje
  środowisko z porównania.

### Naprawione
- **Dwa modele w symulatorze schlebiały, a jeden był po prostu błędem.**
  `loopsim` podawał firmware'owi dokładną, natychmiastową i bezszumową
  częstotliwość co sekundę. Prawdziwy TIM2 bramkuje całe cykle przez sekundę,
  więc liczba jednosekundowa to **całkowita** liczba herców, średnia stusekundowa
  to średnia ze stu takich — stąd jej rozdzielczość 0,01 Hz — i jest to okno
  prostokątne, spóźnione o pięćdziesiąt sekund. Nie miało to znaczenia, dopóki
  częstotliwość była członem drugorzędnym; nabrało go w chwili, gdy algo 13
  wzięło ją jako pomiar Kalmana. Plansza liczy teraz całe cykle i uśrednia je
  tak, jak robi to licznik.

  Szum detektora był biały, a rampa TIC czytana 12-bitowym ADC biała nie jest.
  To nie jest detal: każda pętla, która estymuje swój szum pomiarowy z
  **pierwszych różnic** — R w algo 13, sigma w algo 12 — mierzy tylko część białą
  i na resztę jest ślepa z definicji. `LOOPSIM_DNOISE=<ns>` dokłada teraz
  stacjonarny wolny błąd, więc pytanie ma liczbę zamiast opinii. Degraduje algo
  13 pięć razy szybciej niż algo 11 (1,22 → 7,23 ns wobec 7,45 → 10,07 przy 8 ns
  błądzenia zera) — właściwy kształt, i wciąż za mało, żeby odwrócić wynik ze
  stołu.

### Odrzucone
- **Test bieli innowacji**, napisany po to, by wyjaśnić wynik ze stołu, i
  zmierzony gorzej na każdym poziomie, łącznie z czystym detektorem (1,22 →
  1,72 ns) i 8 ns szumu skorelowanego (7,23 → 8,28). Rozumowanie było
  podręcznikowe: innowacje filtru optymalnego są białe, więc dodatnia korelacja
  lag-1 znaczy, że skorelowany jest **pomiar**, i właściwą reakcją jest
  podniesienie R, a nie poszerzenie Q. Wada: innowacje tego filtru nigdy nie
  miały szans być białe — sterowanie co sekundę zapisuje jego własny stan
  częstotliwości — więc test odpala z powodów niemających nic wspólnego z
  detektorem, a inflacja tylko usypia pętlę. Nie ma tego w drzewie; opisane w
  miejscu, żeby nie wróciło jako propozycja.

### Dodane
- **Obciążenie procesora per task, mierzone licznikiem cykli, z prawdziwym oknem
  100 s.** `SW` pokazuje to raz, od najbardziej obciążającego, obok znaczników
  stosu; `TL 1` wystawia te same liczby na linię telemetrii, `TL 0` je zdejmuje.
  Bez zapisu we flashu i po każdym resecie wyłączone: to diagnostyka przy stole,
  kosztuje linię telemetrii na sekundę, a ustawienie, które przeżywa restart, to
  ustawienie, o którego włączeniu nikt nie pamięta.

  To nie jest próbkowanie. FreeRTOS woła `traceTASK_SWITCHED_IN()` przy każdym
  przełączeniu kontekstu, a Cortex-M4 ma swobodnie bieżący licznik cykli w bloku
  DWT — więc odstęp między dwoma przełączeniami jest znany dokładnie i należy,
  dokładnie, do zadania, które biegło. Kilkanaście cykli na przełączenie i ani
  jednego zajętego timera. Profiler próbkujący z ticka był oczywistą
  alternatywą i byłby ślepy na każde zadanie, które startuje na granicy ticka i
  kończy przed następnym — czyli na większość zadań w tym firmwarze.

  Okno to sto jednosekundowych kubełków, a nie średnia wykładnicza: przy
  prośbie o średnią ze 100 s, EWMA ze stałą czasową 100 s wciąż niesie piątą
  część wagi sprzed pięciu minut. Udziały liczone są z własnej sumy każdej
  sekundy, więc do arytmetyki nie wchodzi żaden zegar, a kolumny sumują się do
  100. Kubełek zamykany jest w sekcji krytycznej, która dolicza też kawałek
  zadania biegnącego w tej chwili — bez tego zadanie bezczynne, które rutynowo
  trzyma procesor przez większość sekundy bez przerwania, nie wniosłoby nic do
  kubełka, w którym dominowało.

  Warto powiedzieć, co gdzie ląduje: czas przerwań przypada temu zadaniu, które
  zostało przerwane, bo ISR nie przełącza kontekstu. Odczyt znaczy „procesor
  spędził tyle z tym zadaniem jako bieżącym" — co jest uczciwe i nie całkiem to
  samo co „to zadanie zużyło tyle".

### Zmienione
- **Ogólne obciążenie liczone jest teraz z tej samej księgowości: 100% minus
  udział zadania bezczynnego.** Było licznikiem obrotów w haku bezczynności, z
  najwyższym kiedykolwiek widzianym zliczeniem jako 0% obciążenia — co działa i
  ma wadę, której z jego wnętrza zmierzyć się nie da: płytka, która nigdy nie
  była blisko bezczynności, ma zaniżony punkt odniesienia, a więc odczyt
  optymistyczny, na zawsze. Licznik cykli nie ma punktu odniesienia, w którym
  mógłby się mylić. Hak bezczynności i `configUSE_IDLE_HOOK` znikają razem z nim.

  Nie jest też już taktowane z linii telemetrii, co oznaczało, że TAB (pauza
  telemetrii) po cichu zatrzymywał również pomiar obciążenia. Napędza to teraz
  zadanie uptime, a przewijanie idzie po upływie milisekund, a nie po tym, że
  ktoś zawołał dokładnie raz na sekundę — więc pominięte albo zdublowane
  wywołanie nie skróci ani nie wydłuży kubełka.

### Zmienione
- **Algo 13 bierze skalę z CT i LC, a nie z jednej płytki.** Weszło z trzema
  liczbami zmierzonymi na moim stole — R zasiane jako (2,64 ns)^2, Q jako 2e-6 i
  kowariancja startowa fazy (100 ns)^2 — a na innym OCXO albo innym detektorze są
  po prostu złe. Płytka z detektorem 300 ns startowałaby z apriori czterokrotnie
  szerszym niż całe jej pasmo; z detektorem 10 000 ns — dużo węższym niż prawda,
  odrzucając dobre odczyty przez pierwsze dziesięć minut. Log z 27.08 pokazuje tę
  drugą awarię na tej samej płytce, z której te stałe pochodzą: **281 odrzuceń i
  500 s** wciągania z 1300 ns.

  W `kf_scale()` nie ma już ani jednej stałej. CT daje liczbę zliczeń zerującą
  nanosekundę w sekundę; LC daje ns na wolt i użyteczny zakres; część ustala ADC
  na 12 bitów na 3,3 V, więc własny kwant detektora to `ns_per_volt * 3,3/4096` —
  1,01 ns na tej płytce, przy zmierzonym szumie 2,6 ns, czyli dwa i pół kwantu, i
  stąd bierze się zasiew R. Dalej:

  | było | jest | na tej płytce |
  |---|---|---|
  | zasiew R (2,64 ns)^2 | (2,5 kwantu)^2 | 6,4 ns^2 (zmierzone 6,6) |
  | podłoga R 0,25 ns^2 | kwadrat jednego kwantu | 1,02 ns^2 |
  | zasiew Q 2e-6 | R / KT^3 | 6,6e-6 (zmierzone 2e-6) |
  | P00 (100 ns)^2 | (pół pasma)^2 | (1500 ns)^2 |
  | P11 (1 ns/s)^2 | (pasmo przejechane w horyzoncie)^2 | (15 ns/s)^2 |
  | Q starzenia 1e-12 | Q / (100 KT^2) | 6,6e-12 |
  | bramka uzbrojenia 0,5 Hz | pasmo / (2 x 100 x karencja) | 0,25 Hz |
  | podłoga testu 8 ns | dwa kwanty | 2,0 ns |

  Liczone co sekundę, a nie zapamiętane, więc ponowne CT albo LC działa bez
  restartu pętli — i wypisywane raz, jedną linią `KAL: from CT/LC ...`, gdy filtr
  startuje: wyprowadzona stała, której nikt nie widzi, to stała, której nikt nie
  sprawdzi. Zasiew Q wymaga słowa: nie ma pomiaru błądzenia losowego oscylatora w
  czasie CT ani LC, ale filtr adaptuje Q z własnych innowacji w ciągu minut, więc
  zasiew ma tylko ustawić sensowne pasmo na te minuty. `Q = R/KT^3` ma jednostki
  dokładnie (ns/s)^2 na sekundę i wypada w granicach trójki od tego, co zmierzył
  nocny przebieg — dwie niezależne drogi do tej samej liczby, a tyle można od
  zasiewu wymagać.

  Zmierzone, pięć ziaren: sd fazy **1,19 -> 0,81 ns** na planszy algo-12 z 26.08
  i 1,24 -> 1,20 na planszy algo-11.

### Naprawione
- **Pętla księgowała korekty, których pin nigdy nie dostał — i to jest ten stały
  offset fazy z logu 27.08.** +18,7 ns trzymane przez trzydzieści osiem minut z
  nieruchomym PWM, przy filtrze raportującym nachylenie -0,19 ns/s, którego nie
  zadawał. Ograniczenie było uwzględnione; **zaokrąglenie nie**.

  Kiedy faza jest w domu, korekty są ułamkiem LSB. Zaksięgowanie żądanego `du` w
  stanie częstotliwości mówi filtrowi, że już jedzie z `-x0/T`; w następnej
  sekundzie sterowanie liczy `-(x1 + x0/T) = 0` i o nic nie prosi. Jeśli ten
  ułamek nie dotarł do pinu, filtr jest teraz pewien, że naprawia błąd fazy,
  którego nic nie naprawia — i pętla parkuje, na dowolnym offsecie, na zawsze, bo
  stan, który mógłby to zauważyć, jest **pisany przez sterowanie**, a nie
  estymowany z danych.

  Poprawka nie polega na staranniejszym zaokrąglaniu. Polega na księgowaniu
  **różnicy między wartościami DAC** — co obejmuje ograniczenie, zaokrąglenie i
  ścieżkę sub-LSB naraz i czego nie oszuka też przyszły stopień wyjściowy — oraz
  na przeniesieniu reszty do następnej sekundy, żeby żądanie poniżej LSB było
  odłożone, a nie wyrzucone. To sigma-delta o okresie sekundy: płytka bez
  ścieżki ułamkowej pyta dalej, aż ruszy całe zliczenie. Ta sama lekcja co przy
  przejściu przez zero w algo 12, o warstwę niżej: stan zaktualizowany
  niezadanym sterowaniem to stan, który kłamie.

  Symulacja na płytce 16-bitowej (`LOOPSIM_FINE=0`), stały offset fazy:
  +0,29 -> +0,02 ns na spokojnej planszy, +0,16 -> +0,04 na planszy z 26.08, sd
  0,65 -> 0,55.

- **Werdykt o zaufaniu do detektora mógł się zatrzasnąć — i zatrzaskiwał się.**
  Dwie usterki, obie znalezione pomiarem, nie czytaniem. Po pierwsze, test
  działał na 1-sekundowej EMA częstotliwości, gdy bramkowana średnia 100 s
  jeszcze nie istniała; ta EMA spóźnia się o pięćdziesiąt sekund, więc w trakcie
  wciągania mówi, że faza powinna jechać tempem sprzed minuty — i skazuje
  całkiem zdrowy detektor. Teraz działa tylko przy `have100`. Po drugie, wyroku
  nie dało się wzruszyć: detektor bez zaufania zostawia pętlę na TIM2, ta trzyma
  częstotliwość dobrze, więc nic nie ma się ruszać, żadne okno nie rozstrzyga, a
  wyrok stoi w nieskończoność na dowodach, które dawno wygasły. Trzydzieści minut
  bez rozstrzygającego okna oddaje teraz wątpliwość oskarżonemu — a pomost
  uzbrajania nie zeruje już tego zegara, co odtwarzało tę samą blokadę przez
  karencję 600 s. Detektor już raz przyłapany jest skazywany po jednym oknie
  zamiast po trzech, co o połowę zmniejsza koszt okresowego przesłuchania przy
  faktycznie zamrożonym.

  Przy poprawnie zamodelowanym zablokowanym detektorze (`LOOPSIM_RAIL` siedział
  na 3,27 V, czyli WEWNĄTRZ pasma dla LRN 3000 — to był test zamrożonego
  detektora pod cudzą nazwą) wyjście z awarii to teraz jedno uzbrojenie w t+106 s
  i przebieg nie do odróżnienia od zdrowego: sd fazy 0,55 ns, częstotliwość
  0,0001 Hz. Trwale zamrożony detektor: częstotliwość trzymana z 0,0002 Hz.

### Dodane
- **Tożsamość wsadu, która nie może być nieaktualna: CRC-32 obrazu flash,
  liczone przy starcie z samej pamięci.** Baner i komenda `V` pokazują je obok
  czasu kompilacji.

  Stempel kompilacji był brany na wiarę i 26.08 skłamał — dwa zrzuty z dwóch
  różnych wsadów miały ten sam, i godzina poszła na kłótnię z logiem, który miał
  rację. Nikt niczego nie zrobił źle. `__DATE__` jest wtapiane w tę jednostkę
  kompilacji, która o nim wspomina, czyli w szkic, a builder Arduino nie
  przekompilowuje jednostki, której źródła się nie zmieniły. Zmieniasz
  `GPSDO_algorithms.cpp`, wgrywasz — i plik obiektowy szkicu jest ponownie użyty
  ze stemplem sprzed tygodnia w środku. Ten stempel mówi prawdę o tym, kiedy
  skompilowano SZKIC, i nic o reszcie firmware'u.

  CRC obrazu nie ma żadnej z tych wad: nic go nie liczy, dopóki płytka nie
  ruszy, więc nie może wyjść z cache'a, i zmienia się, gdy zmieni się dowolny
  bajt dowolnej jednostki kompilacji. Dwie płytki z tym samym wsadem pokazują tę
  samą liczbę. Kiedy log i pamięć się nie zgadzają, wierzyć należy tej liczbie.

  Obejmuje tablicę wektorów, kod, dane tylko do odczytu i inicjalizatory `.data`
  — każdy bajt, który programator zapisał — w granicach `_sidata`, `_sdata` i
  `_edata` z linkera. Te symbole są **słabe**: toolchain, który nazywa je
  inaczej, dostanie "unavailable" zamiast błędu linkowania, bo brak tożsamości
  to niedogodność, a firmware, który się nie linkuje, to usterka. Około 2,5 ms
  raz przy starcie i 64 bajty tablicy.

- **`build_id.h` i `tools/bumpbuild.py`, żeby stempel czasu też był świeży.**
  Szkic dołącza `build_id.h` wyłącznie po to, żeby ten plik istniał: dotknięcie
  go zmusza builder do przekompilowania szkicu i tylko szkicu — ułamek sekundy,
  wobec pełnej przebudowy, jaką ta sama sztuczka kosztowałaby przez
  `build_opt.h`, gdzie zmiana flagi unieważnia wszystko. `bumpbuild.py`
  zwiększa numer; dowolna edycja robi to samo, bo builder patrzy na plik, a nie
  na jego zawartość. Numer trafia do banera. Jeśli nikt go nie zwiększy, nic się
  nie psuje i nic nie kłamie — gwarancją jest CRC, to jest tylko wygoda.

### Zmienione
- **Tuner rysuje dla algo 13 estymatę fazy zamiast serii, której ten algorytm
  nigdy nie wysyła.** Algo 13 wpadał do rodziny PID, więc górny wykres nosił
  podpis "Learned drift (LSB) — LRN feed-forward" nad pustym polem. Teraz pokazuje
  estymatę fazy z filtru, a pod nią Vphase z prowadnicami pasma — bo pytanie,
  które ta pętla najczęściej stawia, brzmi: czy detektor w ogóle żyje. Prowadnice
  chodzą teraz za tym wykresem, na którym jest Vphase, a nie za zaszytą na sztywno
  rodziną. `ARM` dołącza do objaśnianych słów trendu, a linia Learn niesie `arm=`,
  gdy dzielnik został ponownie uzbrojony.

### Naprawione
- **Algorytm 13 nie miał żadnego pomostu do LTIC i przy zablokowanym detektorze
  po prostu puszczał oscylator luzem.** Z płytki zgłoszone jako "częstotliwość
  cały czas jedzie do góry" na algo 13. Przyczyna jest strukturalna, nie
  subtelna: filtr miał JEDEN pomiar — fazę. Przy niezsynchronizowanym picDIV nie
  ma poprawnej fazy, więc nie miał żadnego pomiaru: przewidywał ze stanu wciąż
  wyzerowanego, nic nie wystawiał i raportował HOLD, podczas gdy OCXO odpływał.
  Algorytmy 11 i 12 mają częstotliwość z TIM2 dokładnie po to, a algo 12 mówi to
  wprost: kiedy detektor fazy jest ślepy, częstotliwość nadal czyta prawdę.
  Gorzej — jedyna rzecz, która mogła to naprawić, czyli czuwanie nad zawieszeniem
  i uzbrojenie picDIV, **zerowała własny licznik**, ilekroć faza była niepoprawna,
  czyli w tym jednym przypadku, dla którego istniała.

  **TIM2 jest teraz drugim pomiarem filtru** — jeszcze jedną skalarną aktualizacją
  stanu, który filtr i tak niesie: bez odwracania macierzy, bez nowych pojęć, za
  jakieś trzydzieści dodatkowych mnożeń z akumulacją. Holdover, wciąganie z dużym
  błędem częstotliwości i martwy detektor przestają być przypadkami szczególnymi.

  **I dopiero to pozwala sprawdzić detektor.** Faza i częstotliwość to ta sama
  wielkość zróżniczkowana, więc w oknie faza MUSI przejechać tyle, ile wynosi suma
  błędu częstotliwości. Detektor, który nie rusza się, choć TIM2 mówi, że musi,
  nic nie mierzy — a to jest awaria z 26.08 21:47, gdzie przyklejone 3,116 V
  czytało się jako całkiem poprawne +1295 ns, które nigdy się nie zmieniło. Sam
  filtr jest wobec tego bezbronny: stały odczyt to odczyt spójny, innowacje idą do
  zera, R spada do podłogi i filtr wierzy mu tym bardziej, im dłużej ten kłamie.
  Ten test to czuwanie algo 12 z wyjętym zgadywaniem — tamto przewidywało ruch z
  nachylenia, które samo przed chwilą zadało, czyli pętla sprawdzała własną pracę
  domową; to porównuje z drugim przyrządem. Detektor bez zaufania jest traktowany
  dokładnie jak zablokowany, a zaufanie **nie wraca** przy ponownym uzbrojeniu:
  oddawanie go tam wpuszczało z powrotem detektor już udowodniony jako martwy i
  kosztowało w symulacji 1,35 Hz tam, gdzie pozostanie ślepym kosztuje 0,01.

  Pomost uzbrajania picDIV jest ten sam co w algo 12, z bramką i karencją — uzbrój
  dopiero, gdy częstotliwość jest blisko, bo uzbrojenie sadza fazę na skwantowanym
  offsecie i przy niecelnej częstotliwości detektor zablokuje się w kilka sekund.
  Przy uzbrojeniu filtr **poszerza wiarę w FAZĘ**, zamiast się resetować: nic z
  tego, czego nauczył się o częstotliwości i starzeniu, nie przyszło przez dzielnik,
  a to są rzeczy drogie do odtworzenia. Algo 12 musi tu wyrzucić cały akumulator —
  to właśnie kupuje niesienie kowariancji.

  Zmierzone w `tools/loopsim` na oscylatorze odtworzonym z logu z 26.08, z
  detektorem psującym się tak, jak psuje się sprzęt (nowe `LOOPSIM_RAIL` i
  istniejące `LOOPSIM_STUCK`), przed i po:

  ```
                                       PRZED       PO
    picDIV bez synchronizacji         -5,06 Hz    -0,0002 Hz (1 uzbrojenie)
    zamrożony +1295 ns, 300 LSB obok  -4,26 Hz    +0,0004 Hz
    zamrożony +1295 ns, na częstotl.  -4,17 Hz    -0,0005 Hz
    zdrowy detektor, 1500 ns od zera  sd 209,80   sd 209,77 ns
    zdrowy detektor (5 ziaren)        sd  1,23    sd  1,19 ns
  ```

  Jakość pętli na działającym detektorze bez zmian — i o to chodzi: nic z tego
  nie siedzi w prawie sterowania.

- **Kanał częstotliwości w symulatorze miał zły znak** i nikt tego nie zauważył,
  bo aż do tego filtru nic nie używało obu kanałów naraz. `loopsim` napędzał
  detektor i TIM2 z tego samego błędu sterowania, przy czym częstotliwość SPADAŁA
  wraz ze wzrostem PWM — co razem ze spadającą fazą daje ten sam znak dla
  nachylenia fazy i błędu częstotliwości. Na tej płytce są przeciwne, i mówią to
  niezależnie dwie pętle, które działają na stole: algo 11 obniża PWM, gdy
  oscylator czyta się za szybko, a korekta TIM2 w algo 12 sprowadziła `f100` do
  domu ujemnymi krokami podczas burzy 16.08. Obie wymagają, by częstotliwość
  rosła z PWM, podczas gdy rekonstrukcja fazy ma ją malejącą. Żaden wcześniejszy
  pomiar się nie zmienia: człon TIM2 w algo 12 jest bramkowany znacznie powyżej
  błędów częstotliwości, jakie te plansze wytwarzają, i nigdy nie zadziałał.

- **Szkic się nie kompilował, a jedenaście czystych konfiguracji nic o tym nie
  mówiło.** Dwa błędy trafiły do IDE razem: `vApplicationIdleHook()` miał
  `extern "C"` w linii nad funkcją zamiast w tej samej, a `setup()` wywoływał
  `kf_store_load()` bez nagłówka, który go deklaruje.

  Pierwszy wart jest wyjaśnienia, bo nie jest oczywisty. Builder Arduino wstawia
  prototyp C++ dla każdej funkcji zdefiniowanej w szkicu, a jego przebieg ctags
  patrzy NA LINIE: `extern "C"` w linii poprzedniej nie jest widziany, prototyp
  więc powstaje, a definicja pod nim — która linkowanie C ma — jest z nim
  sprzeczna. Sam FreeRTOS nie deklaruje tego haka (wywołuje go z C), więc
  wygenerowany prototyp jest pierwszą deklaracją i kompilator nie ma czego mu
  przeciwstawić.

  Żadnego z tych błędów `tools/hostcheck` nie widział: kompilował każdy `.cpp` w
  jedenastu konfiguracjach i nigdy `.ino`. **Teraz kompiluje**:
  `tools/hostcheck/ino2cpp.py` odtwarza obie rzeczy, które builder robi ze
  szkicem — dokłada `<Arduino.h>` i wstawia prototypy liczone po liniach — a
  szkic jest kompilowany i linkowany razem z resztą w każdej konfiguracji.
  Cofnięcie którejkolwiek z poprawek wywala teraz przebieg z tym samym
  komunikatem, który dało IDE.

- **Algorytm 12 był trzykrotnie gorszy, niż musiał, a przyczyną był jeden wpis w
  CLI.** Przebieg 26.08: 2,25 h na algorytmie 12 przy sd fazy 41 ns, w cyklu
  granicznym ±70 ns o okresie 2,3 h — wobec 3,0 ns algorytmu 11 na tej samej
  płytce tego samego popołudnia. Podłoga szumu detektora w obu odcinkach
  identyczna: 2,47 wobec 2,45 ns. Cała różnica leży więc w pętli, i po to
  właśnie jest miara struktury wolnej.

  `MG` było ustawione na **2,130 LSB/ns**. To jest `LG` algorytmu 11, czyli jego
  własne wzmocnienie VCO; `MG` algorytmu 12 to liczba zliczeń potrzebna do
  wyzerowania jednej nanosekundy fazy w jedną sekundę, a CT zmierzył **31,3**.
  Obie wielkości drukują się jako „LSB per ns" i nie są tą samą wielkością. Każda
  korekta była więc **14,7× za mała** — w logu widać to jako trzynaście korekt
  przesuwających PWM łącznie o cztery LSB, podczas gdy sterowanie wymagane przez
  oscylator wędrowało o 3,3.

  Ten sam wpis zrobił jeszcze jedną rzecz, o którą nikt nie prosił. `MF 0`
  znaczyło „idź za MG", więc wpisanie wzmocnienia przestawiło również tablicę
  limitów ze wzoru na szum na tablicę zapisaną, której limit na poziomie 6 to
  ~126 ns. Pętla korygowała więc słabo I stanowczo za późno.

  Trzy zmiany, każda zmierzona, nie przedyskutowana:

  - **Tablica limitów nie chodzi już za wzmocnieniem.** `MF 0` to teraz wzór na
    szum, cokolwiek mówi `MG`; o ręcznie edytowaną tablicę `MLP` prosi się przez
    `MF 1`. Że to zespawanie jest złe, argumentowano już przy wprowadzaniu `MF`
    — wzmocnienie należy do oscylatora, limity do szumu fazy w danym miejscu — i
    trzymała je tylko zgodność wstecz. Ta zgodność właśnie tyle kosztowała.
  - **Test przejścia przez zero uzbraja się na obu ścieżkach korekcji.** Uzbrajał
    się tylko na ścieżce limitu, z argumentem, że korekta z harmonogramu „nie ma
    znanego zjazdu do skasowania". Obie ścieżki kilka linii wcześniej liczą ten
    sam `slew_lsb = -(p_ns/span)*lsb_per_ns` i obie go zapisują, więc ten
    argument opisuje pętlę Alana, a nie tę.
  - **Płytka mówi teraz, kiedy ręczne `MG` nie może być decyzją strojeniową.**
    Powyżej czterokrotności tego, co zmierzył CT, to pomyłka we wpisie, nie
    strojenie. Wartość dalej jest używana — świadomy eksperyment musi pozostać
    możliwy — ale i `MG`, i sama pętla przy pierwszym użyciu po odtworzeniu
    ustawień drukują obok zmierzoną liczbę. Ścieżka odtworzenia jest tu istotna:
    to ta, której nikt nie testuje.

  Zmierzyło to nowe narzędzie `tools/loopsim/`, trzecie tego rodzaju:
  `hostcheck` pyta, czy drzewo się kompiluje, `algoswitch` — czy pętla startuje,
  `loopsim` — czy utrzymuje fazę. Odgrywa oscylator zrekonstruowany z logu, a nie
  wymyślony na tę okazję: mając zadane PWM i zaraportowaną fazę, sterowanie,
  którego oscylator potrzebował w każdej sekundzie, wychodzi z algebry. Buduje
  prawdziwy algorytm dwa razy, ze zmianą i bez, więc porównanie kolumn jest samą
  zmianą i niczym więcej. Sd fazy, średnia z pięciu ziaren szumu:

  ```
                          PRZED    PO      algo 11
    okno algo-12 z 26.08
      MG 0  (wyliczane)    3,57     2,95     3,64
      MG 2,130 (jak było) 53,81    21,06
      MG 31,3 (z CT)       6,63     2,95
    okno algo-11 z 26.08
      MG 0  (wyliczane)    4,62     4,01     7,39
      MG 2,130 (jak było) 66,04    61,96
      MG 31,3 (z CT)      16,41     4,01
  ```

  Przy poprawnym wzmocnieniu algorytm 12 trzyma teraz **lepiej niż algorytm 11**
  na obu oknach — 2,95 wobec 3,64 na krótszym i 4,01 wobec 7,39 na dłuższym,
  który niesie czterokrotnie większy dryf.

  **Dwie poprawki napisano i odrzucono; obie są opisane w miejscu, którego
  dotyczą.** Test przejścia przez zero nie odpalił w tym logu ani razu, bo faza
  po każdej korekcie zmieniała znak dopiero po 810 do 3283 sekund, przy oknie
  rezygnacji 300 s — każdy powrót je przeżył. Zastąpienie tego okna regułą
  „trzymaj uzbrojenie, dopóki faza wraca" jest poprawką oczywistą i wypada
  *gorzej* na dłuższej planszy (4,01 → 6,76 ns), bo na dryfującej płytce faza
  pełznąca ku zeru to często oscylator, a nie zjazd tej korekty. Długie powroty
  były objawem błędu wzmocnienia 14,7×, a nie usterką tej stałej. Druga bariera
  — uzbrajać tylko wtedy, gdy ekstrapolowana faza akumulatora zgadza się znakiem
  z fazą na wyjściu — wypadła na zero i też jej w drzewie nie ma. Odrzucona
  poprawka, po której nie zostaje ślad, wraca po jakimś czasie jako propozycja.

  Istniejące instalacje: jeśli `MG` jest niezerowe, porównaj je z liczbą z `CT` —
  `ML` drukuje obie — a w razie wątpliwości `MG 0`. Jeśli świadomie edytowałeś
  limity `MLP`, dopisz `MF 1`, żeby je zachować, a potem `ES ALGO12`.
- **Test przejścia przez zero oddawał zjazd, który nigdy nie dotarł na wyjście.**
  To jest ta rzecz, która czyniła algo 12 bezużytecznym przy fazie daleko od
  zera, a log z 26.08 22:41 jest jej czystym zapisem: po tym, jak strażnik
  przestoju uzbroił dzielnik, faza uczciwie wróciła, doszła do −50 ns, po czym
  pętla rzuciła PWM o 1038 zliczeń w jednej sekundzie. Przez następnych
  siedemnaście minut przemierzała całe pasmo detektora — PWM od 40348 do 41410,
  faza od −1600 do +20 ns, pięć uzbrojeń — i ani razu nie weszła w ±200 ns.

  Korekta liczy celowy zjazd, a potem ogranicza sumę względem pasma detektora.
  Przejście przez zero zdejmuje później ten zjazd, żeby oscylator został z dobrą
  częstotliwością I bez błędu fazy. Tyle że zdejmowana wartość to zjazd
  POLICZONY, zapisany przed ogranicznikiem — a gdy ogranicznik działa, czyli
  dokładnie wtedy, gdy faza jest daleko, to jest inna liczba. Przy −1500 ns na
  tej płytce policzony zjazd to 734 LSB, a na wyjście trafia 500; przejście
  oddaje 734, więc nadmiarowe 234 to świeży błąd częstotliwości skierowany w
  drugą stronę. Faza rusza z powrotem, trafia w ogranicznik przy przeciwnej
  szynie i pętla maszeruje po paśmie bez końca.

  Teraz zapisywane jest to, co przeżyło ogranicznik — ograniczona suma minus człon
  częstotliwościowy, który nie jest celowym zjazdem i nie podlega kasowaniu.
  Wchodzenie z dużego offsetu, odtworzone na oscylatorze z tamtego dnia, średnia
  z pięciu ziaren szumu:

  ```
     faza startowa      przed         po
        ±500 ns          0,6 ns       1,1 ns
       ±1000 ns          0,7 ns       1,1 ns
       ±1500 ns      4,6e6 ns         1,0 ns
  ```

  Poniżej mniej więcej 1000 ns ogranicznik rzadko działa i obie wersje radzą
  sobie tak samo; przy 1500 stara rozbiega się za każdym razem, a nowa siada
  poniżej 2 ns. Trzymanie bez zmian (3,07 ns wobec 3,62 algo 11 na tej samej
  planszy), a przypadek z ręcznym wzmocnieniem też się poprawia: 53,8 → 16,7 ns.

- **Detektor, który przestał śledzić, zostaje wreszcie zauważony.** Druga połowa
  wieczoru 26.08. Płytka zresetowała się prosto w algorytm 12 z niezsynchronizowanym
  picDIV: rampa stanęła przy górnej szynie, Vphase płaskie na 3,116 V z dokładnością
  ±5 mV przez cały 5,5-minutowy zapis. Przy `LRN` 3000 pasmo użyteczne to ±1650 ns,
  więc to napięcie leży wygodnie WEWNĄTRZ niego — detektor raportował całkowicie
  poprawne +1295 ns, które nigdy się nie zmieniało. Pętla była przekonana, że ma
  fazę, więc nigdy nie uzbroiła dzielnika (`arm=0` przez cały czas), wykonała jedną
  korektę z poziomu 0, która nasyciła ogranicznik ±500 LSB, i stanęła.

  **Test, który wchodzi, jest przewidywaniem, a nie progiem.** Pętla zna zjazd,
  który zadała, więc wie, ile faza powinna przejechać w oknie:
  `|slew_lsb| / lsb_per_ns` nanosekund na sekundę. Porównuje to z tym, co faza
  faktycznie zrobiła — z różnicą średnich dwóch półokien, której szum wynosi
  `sigma*sqrt(2/W)`, a nie `sigma`, czyli 1,8 ns zamiast 7. Jeśli przewidywanie
  jest na tyle duże, że mierzalne, a faza dowozi mniej niż jego ćwierć przez trzy
  32-sekundowe okna z rzędu, odczyt nie jest już połączony z oscylatorem. Wtedy
  uzbrojenie picDIV i komunikat. Odtworzone na oscylatorze z tamtego dnia przy
  detektorze zamrożonym na +1320 ns: odpala po 229 s.

  **Powstały trzy wersje tego testu i dwóch nie ma w drzewie.** Pierwsza
  porównywała pojedyncze próbki z punktem odniesienia i zerowała licznik przy
  każdym wychyleniu o cztery sigmy — co szum robi niemal co sekundę, więc licznik
  nigdy nie doszedł dalej niż do jedynki i nie odpalił na płytce, dla której
  powstał. Druga dołożyła bramkę odmawiającą działania na fazie, o której nie
  wiadomo, czy się rusza — i zakleszczyła się: bramka blokowała korektę, której
  zadaniem było fazę ruszyć. Diagnozować, nie krępować.

  **Uzbrajanie przy świeżym wejściu też napisano i odrzucono**, na wzór pytania
  startowego algorytmu 10. Odpala się także wtedy, gdy detektor jest SPRAWNY, a
  faza po prostu jest daleko, i wtedy uzbrojenie WYRZUCA dobry pomiar — dzielnik
  synchronizuje się na skwantowany offset rzędu setek nanosekund. Jest też
  niepotrzebne: odtworzone z +1295 ns przy sprawnym detektorze i realnym błędzie
  częstotliwości algorytm 12 wchodzi sam za każdym razem, 17–19 przejść przez
  zero, powrót do ±17 ns, ani razu nie uzbrajając. Uzbrojenie nie jest darmowe,
  „faza jest daleko" nie jest dowodem, że dzielnik się zgubił, a pętla nie
  potrzebuje pomocy, dopóki detektor mówi prawdę.

- **Akumulator jest wyrzucany przy każdym uzbrojeniu picDIV.** Uzbrojenie
  resynchronizuje dzielnik do zbocza 1PPS, więc każda faza już siedząca w
  hierarchii była mierzona względem wyrównania, które przestało istnieć;
  zatrzymanie ich miesza dwa różne zera. W symulacji pierwsza korekta po
  uzbrojeniu wyszła −436 LSB z testu na poziomie 3, który był w połowie sprzed
  skoku i w połowie po. Istniejące `s_mla_post_arm` tego nie obejmuje — trzyma
  stan przejściowy PO uzbrojeniu POZA akumulatorem, ale akumulator był już pełny.
- **Zmiana algorytmu wreszcie restartuje pętlę, na którą się przełączasz.**
  Nie robiła tego żadna, i nikt jej o to nigdy nie prosił: każda pętla trzyma
  swój stan w statykach funkcji, a statyk nie wie, że operator wpisał `LA 12`.
  Zgłoszone przy stole — algorytm 11 w LOCK, przełączenie na 12, i 12 stał na
  poziomie 0 z zerem korekt i nieuzbrojonym picDIV.

  Trzy osobne mechanizmy, jedna przyczyna. **Algorytm 12** zostawał z ustawioną
  flagą `s_mla_returning`, którą podnosi korekta na czas, gdy jej celowy zjazd
  sprowadza fazę do zera; flaga blokuje OBIE ścieżki korekcji (test limitu na
  poziomie i harmonogram `MR`), a 300-sekundowy timeout, który ją zwalnia, tyka
  wyłącznie wtedy, gdy 12 jest pętlą bieżącą — więc wyjście w trakcie zjazdu i
  powrót dusiły każdą korektę aż do wygaśnięcia tego timeoutu, przy `level=0
  corr=0` w telemetrii i bez śladu, dlaczego. Hierarchia akumulatorów wciąż
  trzymała też sumy fazy sprzed przełączenia, więc pierwsza korekta, która
  jednak zadziałała, działała na dowodach sprzed kilku minut. **Algorytm 10**
  trzyma `integ` jako BEZWZGLĘDNY cel PWM, czyli napięcie sterujące dobrane do
  warunków sprzed być może wielu godzin, a `prev_state` wciąż pokazujący LOCK
  sprawia, że przejście wejściowe nigdy nie następuje — więc `ltic_autotune()`
  nigdy nie rusza, a picDIV nigdy nie jest uzbrajany. **Algorytmy 3–9**
  przenosiły przez przełączenie swoje całki PID, co przy pierwszej aktualizacji
  po zmianie daje skok korekty, o który nikt nie prosił.

  Hak siedzi w `adjustVctlPWM()`, a nie w obsłudze `LA`, bo to jedyna droga,
  którą przechodzi każda zmiana: CLI, odtworzenie ustawień przy starcie i
  wszystko inne, co kiedykolwiek zapisuje `gCtrl.active_algo`. Hak na komendzie
  przegapiłby odtworzenie — czyli dokładnie ten przypadek, którego nikt nie
  testuje. Każda pętla bierze flagę raz i czyści własny stan; pętle starsze
  wykorzystują flush bufora cyklicznego, który już miały.

  Restart CELOWO nie kasuje tego, co pętle ZMIERZYŁY na płytce — LSB na ns
  algorytmu 12, jego podłogi szumu detektora i zmierzonych progów, ani estymaty
  szumu algorytmu 10. To opisuje sprzęt, nie poprzedni przebieg, a odbudowa
  kosztowałaby minuty pracy na ślepo przy każdym przełączeniu. Liczniki
  sesyjne kasowane są z odwrotnego powodu: „zero korekt od przełączenia" jest
  czytelnym faktem tylko wtedy, gdy licznik startuje od zera.

  Jedno NIE było usterką: to, że algorytm 12 nie uzbraja picDIV przy wejściu z
  zaryglowanego algorytmu 11. Uzbraja tylko wtedy, gdy detektor jest ślepy, a
  częstotliwość blisko (`!have_phase && |f| < 0,5 Hz`); przychodząc z ryglu
  faza jest ważna, więc nie ma czego uzbrajać, a ruszanie dzielnika tylko
  rzuciłoby fazę na skwantowany offset. `arm=0` jest tam objawem działania.

  Zweryfikowane przed wysyłką, nie po kolejnym logu. Nowe narzędzie
  `tools/algoswitch/run.sh` buduje `GPSDO_algorithms.cpp` na PC dwa razy — raz
  jak jest, raz z `algo_take_restart()` wymuszonym na false, czyli z kodem
  sprzed poprawki — i uruchamia oba na symulowanej płytce (319,5 µHz/LSB,
  detektor 1252 ns/V, LPOL −1). Ta sama sekwencja przełączeń, z wyjściem z 12 w
  trakcie zjazdu: przedtem 30 minut po powrocie wciąż młócił na poziomie 2 z
  308 ns błędu fazy; po poprawce siada na poziomie 7 przy 4 ns. Wejście w
  algorytm 10 z fazą 1200 ns poza rampą: przedtem zapamiętany LOCK jest brany
  na wiarę i utrzymany; po poprawce jest sprawdzany, degradowany do ACQ, a
  dzielnik uzbrajany na nowo. Wyśrodkowany rygiel przetrwa w obu — chodzi o
  sprawdzenie deklaracji, nie o psucie dobrej.
- **Errata manuala + obsługa klonów odbiorników.** Sekcja DFU obejmuje teraz
  aktualne płytki WeAct v3.1 z *przyciskiem* BOOT0 zamiast zworki (trzymaj
  BOOT0 przy podłączaniu USB, potem puść — recepta „trzymaj i stukaj NRST"
  z internetu nie działa), a Parte 3 zyskała akapit o doborze modułu GNSS:
  chińskie klony u-bloxa ignorują konfigurację binarną, a niektóre na
  strumień ramek bez odpowiedzi tracą auto-baud, aż re-proba po tunelu `T`
  je ratuje (raport z pola: Solder Junkie). Nowy przełącznik kompilacji
  `GPSDO_FAKE_UBLOX` (domyślnie wyłączony): sama proba baudrate, zero
  konfiguracji UBX — pętla niczego istotnego nie traci, PPS nigdy nie
  potrzebował UBX; znikają wyciszanie NMEA, tryb stacjonarny, survey-in/Time
  Mode i `qErr`.
- **Algorytmy 11 i 12 nie zgadują już polaryzacji EFC.** Dotąd traktowały nieustawiony `LPOL` jako +1 i sterowały — na płytce z odwróconym EFC każda korekta pchała w dal. Teraz czekają z jednolinijkowym ostrzeżeniem, aż ustawisz `LPOL ±1` (istniejące instalacje 11/12: ustaw `LPOL` raz i `ES LTIC`).
- **Help `ES` ukrywał grupę `ALGO12`.** Obie linie helpu (główna lista `H`
  i strona `H TZ`) mówiły `obj: TZ/PID/LTIC/FLAGS/ALGO/PO`, choć parser
  przyjmuje `ES ALGO12` od czasu wprowadzenia bloku algo-12 — użytkownik
  idący za helpem nie znalazłby grupy zapisującej MG/MR/MF/MFT i tabelę
  limitów per poziom, a podpowiedzi `[not saved — run 'ES ALGO12']`
  wskazywały komendę, której help nie przyznaje do istnienia. Obie linie
  wymieniają teraz `ALGO12`. W helpie tunera `ES` było już poprawne, ale
  `FR 0|1` opisywany był jako przełącznik, a sekcja PID niosła
  nieosiągalną pozycję `LRN 0|1|R` — poprawione zgodnie z firmwarem.
- **Nieblokujący zapis raportu wyciszał telemetrię 1 Hz na USB CDC.** Guard
  dodany na usterkę „wchodzi USB, zamraża się wyświetlacz" pytał
  `availableForWrite()` raz i dropował **cały** raport, gdy zwracał mniej niż
  rozmiar raportu. Na USB CDC to każdy raport: kolejka TX `USBSerial` to
  `USB_FS_MAX_PACKET_SIZE * CDC_TRANSMIT_QUEUE_BUFFER_PACKET_NUMBER` = 64 × 2 =
  **128 bajtów** (domyślne stm32duino 2.12.0), a raport human-readable ma 400+.
  Boot i CLI działały dalej — krótkie linie, inna ścieżka — więc płytka, która
  przed chwilą logowała bez zarzutu po UART (bufor TX 512 B z `build_opt.h`, gdzie
  raport akurat się mieścił), zamikała w chwili przejścia na logowanie po USB.
  Stąd pozorna zależność od konfiguracji.

  Raport jest teraz zapisywany **porcjami** nie większymi niż to, co port
  potrafi przyjąć, z budżetem 25 ms: host czytający wynosi cały raport w kilku
  iteracjach (kolejkę opróżnia w znacznie mniej niż milisekundę pollingu);
  host nieczytający dostaje drop ogona po budżecie, a wyświetlacz żyje dalej —
  o to właśnie chodziło. `room == 0` czytane jest jako „pełna, czekaj", nigdy
  jako zgoda na ślepy zapis reszty — na pełnej kolejce CDC `USBSerial::write()`
  kręci się, dopóki host jest połączony, czyli dokładnie ten freeze, przed
  którym guard ma chronić.

  Kolejka TX CDC jest przy okazji powiększona do 1 KB
  (`-DCDC_TRANSMIT_QUEUE_BUFFER_PACKET_NUMBER=16` w `build_opt.h`; makro ma
  `#ifndef` w bibliotece USBDevice, więc flaga ze szkicu do niej dociera —
  działa na rdzeniach, w których ten guard istnieje, sprawdzono na 2.12.0).
  Zdrowy host ma teraz ~2,5 s zapasu i nic nie jest dropowane; zapis porcjami
  zostaje jako zabezpieczenie na hosta połączonego, ale nieczytającego —
  przed którym żaden rozmiar bufora nie ratuje.
- **Linkowanie nie przechodziło z wyłączonym `GPSDO_LTIC`, nawet po powyższej
  poprawce nagłówka.** `g_freq_damp_win_dpll` i `g_freq_damp_win_lock` — okna
  tłumienia FA / FAD / FAL — były definiowane wewnątrz bloku `GPSDO_LTIC` w
  `gpsdo_tasks.cpp`, ale należą do zapisywanego bloku ustawień, a CLI wypisuje je
  i ustawia bezwarunkowo, więc `settings_store.cpp` i `gpsdo_cli.cpp` sięgają po
  nie w każdej konfiguracji. Cztery bajty RAM-u wobec firmware'u, którego nie da
  się zbudować bez detektora fazy, to zły interes; definicje wyszły poza
  strażnika.
- **Firmware nie budował się z wyłączonym `GPSDO_LTIC`.** `GPSDO_algorithms.cpp`
  definiuje stan, globale i akcesory algorytmu 12 *poza* własnym blokiem
  `#ifdef GPSDO_LTIC`, a `gpsdo_cli.cpp` czyta je bezwarunkowo — ale wszystkie te
  deklaracje siedziały *wewnątrz* bloku `#ifdef GPSDO_LTIC` w
  `GPSDO_algorithms.h`. Wyłączenie detektora dawało czternaście błędów „was not
  declared in this scope" od `GPSDO_algorithms.cpp:1437` i całą grupę
  `ML`/`MLP`/`MG`/`MF` w CLI. Osobno: `#endif` zamykający strażnika wokół
  `multi_level_accum()` stał o linię *wyżej* niż klamra zamykająca tę funkcję,
  więc z wyłączonym strażnikiem klamra zostawała osierocona. Nikt na to nie
  trafił, bo każda płytka tej konstrukcji ma detektor — pierwszą osobą budującą
  bez niego był Dave (Solder_Junkie) z EEVbloga, którego płytka z M8N nie ma
  wejścia TIC. Deklaracja nic nie kosztuje, gdy definicji nie ma, więc strażnik
  obejmuje teraz dwie funkcje, które naprawdę potrzebują sprzętu, i nic więcej.
  Sprawdzone w obie strony: drzewo kompiluje się i linkuje czysto z `GPSDO_LTIC`
  włączonym i wyłączonym, a żaden symbol definiowany tylko pod strażnikiem nie
  jest używany spoza niego.
- **Blokujący zapis na USB CDC zamrażał wyświetlacz.** `vDisplayTask` obsługuje
  OLED, LCD, TM1637 i TFT *oraz* wypisuje raport 1 Hz, a `USBSerial::write()` z
  rdzenia STM32duino kręci się w miejscu, dopóki endpoint jest zajęty, tak długo
  jak host jest podłączony. Host, który wyliczył port, ale go nie odbiera,
  zatrzymywał więc to zadanie w martwym punkcie, wewnątrz zapisu, po drugiej
  stronie 30-milisekundowego timeoutu mutexu — więc widocznym objawem był
  zamrożony wyświetlacz, co czyta się jak zawieszona płytka i nie było nią wcale:
  zadania częstotliwości i sterowania w ogóle nie dotykają portu szeregowego i
  przez cały czas dyscyplinowały oscylator. W całym firmwarze nie było ani
  jednej bramki `availableForWrite()`. Raport pyta teraz najpierw o miejsce i
  porzuca całą linię, jeśli się nie zmieści; telemetria to strumień na żywo, nie
  log, a następny raport jest za sekundę. `TeeSerial` dostał własne
  `availableForWrite()`, zwracające mniejszy z dwóch portów, bo domyślne ze
  `Stream` zwraca 0 i uciszyłoby build `GPSDO_BLUETOOTH_PARALLEL` całkowicie.
  Zgłoszone przez Dave'a (Solder_Junkie) na EEVblogu.
- **Uptime liczony był z wolnobieżnego timera MCU — w zegarze dyscyplinowanym
  GPS-em.** Zmierzone na trzynastu zapisach z dwóch płytek, od 1 do 21 godzin
  każdy: wypisywany uptime zyskiwał wobec wypisywanego UTC od +129 do +169 ppm,
  najlepsze oszacowanie **+159 ppm** z trzech najdłuższych zapisów (+12 s w
  20,8 h, +13 s w 22,9 h, +7 s w 11,9 h). Obie płytki zgadzają się, więc to nie
  jest tolerancja kwarcu — takt 2 Hz, który napędzał licznik, po prostu nie ma
  2 Hz.

  Ten błąd tempa dawał też jitter ±1 s, czyli to, co widać najpierw: raport jest
  wypisywany na PPS, a licznik szedł z TIM9, więc oba zbocza przesuwały się
  wzdłuż siebie raz na ~6530 s (= 1/159 ppm), a każde przejście rzucało serię
  powtórzonych i przeskoczonych sekund. Serie w zapisie z 19.08 zaczynają się na
  325, 6854, 13383 i 19907 s — odstępy 6529, 6529, 6524.

  Uptime jest teraz zwiększany przez zwalidowany PPS, w tym samym miejscu, gdzie
  rośnie `ppscount` — a to jest również zdarzenie wyzwalające raport, więc
  wypisana wartość nie może być dudnieniem dwóch zegarów. `vUptimeTask` utrzymuje
  zegar tylko w holdoverze, po ponad 1,5 s ciszy na PPS. Symulacja wobec kwarcu
  szybkiego o 159 ppm: dokładnie 86 400 s przez 24 h w locku, dokładnie 3600
  przez godzinę czystego holdoveru i zero błędu przy 20 cyklach zaniku oraz
  godzinie z 2 % brakujących impulsów (`tools/uptime_test.cpp`).

- **Dwa ciche zgubienia w starej ścieżce uptime'u.** Brała mutex uptime'u z
  timeoutem 5 ms i robiła `continue` przy niepowodzeniu, odrzucając tę sekundę
  bez śladu, że była należna; i liczyła takty półsekundowe po parzystości, którą
  zgubiony give semafora binarnego odwraca. Żadne z tego już nie istnieje: nie ma
  mutexu, a liczone są upływające milisekundy, więc takt zgubiony, spóźniony i
  podwojony wychodzą tak samo, a zadanie zablokowane na minutę tę minutę
  nadrabia.

- **Takt korekty LOCK używał `ppscount % period`, choć okres mógł się
  zmieniać.** Postać z modulo jest poprawna tylko dla stałego okresu, a powyższe
  ograniczenie kadencji czyni go czymkolwiek innym: przy `LIV 300` i
  ograniczeniu ~110 s takt wypadał na wielokrotnościach tego, czym akurat był
  `period` w danej sekundzie, co w 6-godzinnej symulacji dało odstępy od 7 s do
  551 s i MEDIANĘ 300 — ograniczenie liczyło poprawną liczbę co sekundę i prawie
  nigdy nie zdążyło jej użyć. LOCK bramkuje teraz na czasie; ACQ i DPLL
  zachowują modulo, bo ich okresy są stałe. Potwierdzone na sprzęcie: każdy
  odstęp między korektami w przebiegu z 21.08 jest dokładną wielokrotnością
  ustawienia 30 s, poza jednym odstępem 12 s w trakcie ustalania, gdzie
  ograniczenie zadziałało zasadnie.

- **Limit kroku LOCK jest na KOREKTĘ, nie na sekundę.** Limit 4 mHz przy
  kadencji 300 s dawał pętli trzydziestą część uprawnień, które miała przy 30 s,
  przy tym samym dryfie do skasowania; integrator siedział na ograniczniku
  korekta po korekcie i przestrzeliwał, gdy w końcu nadrobił (symulowany RMS
  fazy 174 ns przy `LIV 300` wobec 34 po poprawce). Limit ma teraz podłogę na
  poziomie czterokrotności kroku częstotliwości, który test nachylenia właśnie
  policzył, i jest niezmieniony na płytce bez rozpoznawalnego nachylenia.

- **Obie długości okien były przyjmowane jako `lock_interval_s`.** Nie są, gdy
  ograniczenie kadencji może skrócić interwał: na płytce proszącej o 300 s a
  chodzącej na 73 nachylenie wychodziło czterokrotnie za małe, a ekstrapolacja
  sięgała czterokrotnie za daleko. Obie długości są teraz odczytywane z licznika
  PPS.

- **Przewinięcie okna siedziało wewnątrz testu pary, więc LOCK nie robiłby nic w
  ogóle.** Po skasowaniu próbek na przejściu DPLL→LOCK starsze okno jest puste,
  więc test się nie wykonywał, więc przewinięcie nie następowało, więc starsze
  okno zostawało puste. Przetrwało to symulację tylko dlatego, że symulator
  zasiewał pierwsze okno ręcznie — różnica między modelem a firmware'em dokładnie
  tam, gdzie model miał ją wykluczać. Przewinięcie jest teraz bezwarunkowe, gdy
  tylko istnieje nowsze okno.

- **Przezbrojenie picDIV szło prosto do PI.** Dzielnik sadza fazę na
  skwantowanym przesunięciu — około ±3 µs w tej konstrukcji — a ten skok nie
  jest błędem fazy, na który pętla ma odpowiadać. Algorytm 12 pomija go od
  v1.05; pętla trójstopniowa zbrojła w trzech różnych miejscach i nie pomijała
  niczego. Wszystkie trzy wygaszają teraz detektor na 5 s.

- **Cyfry częstotliwości zmieniały kolor tylko na `LOCK`**, więc algorytm 12 —
  który raportuje `CORR` i `ZC` — pokazywał kolor niezablokowany będąc
  zablokowanym. Zabezpieczenie przed nieaktualnym echem wynosiło też ±0,050 Hz
  wobec średniej 10 s skwantowanej do 0,1 Hz, więc pojedyncza uprawniona
  jednostka czytała się jako stare echo; teraz jest ±0,150.

- **Każdy wykres w tunerze był ściśnięty 4:1 wzdłuż osi czasu.** Do wspólnego
  bufora czasu dopisywało się zawsze, gdy *jakiekolwiek* pole udało się
  sparsować z linii — czyli przy czterech z sześciu linii bloku telemetrii —
  podczas gdy każda pojedyncza seria dostawała jeden wpis na sekundę. Oba
  zapełniały się więc w różnym tempie, a kod rysujący parował N najnowszych
  próbek serii z N najnowszymi *znacznikami czasu*, które pokrywały tylko
  ostatnią ćwiartkę okresu zajmowanego przez te próbki. Zmierzone na zapisie z
  21.08: 1725 sekund telemetrii, 6872 próbki czasu. Bufor „30 h" to było 30 h
  fazy wobec 7,5 h zegara, a wskaźnik „sekund w buforze" pokazywał
  czterokrotność prawdy. Taktem jest teraz linia `Up:`, wypisywana dokładnie raz
  na sekundę przez każdy algorytm, a każda seria jest do niej dopychana, więc
  długości zgadzają się z konstrukcji, a nie przez przypadek.
- **`HDOP:TIME` było odrzucane jako nieparsowalne.** LEA-T po zakończonym
  survey-in raportuje tryb fiksa w polu HDOP, a ta flaga jest ważniejszym z
  dwóch faktów — to tryb, w którym 1PPS jest wart zaufania. Teraz przechodzi
  tak, jak został zapisany.

### Dodane
- **`tools/hostcheck/` — kompilacja i linkowanie każdej kombinacji przełączników
  na PC.** Trzy błędy budowania pod rząd wzięły się z jednej osoby budującej bez
  detektora fazy, każdy zasłaniał następny, a ostatni był błędem LINKOWANIA,
  którego żadne czytanie pojedynczego pliku by nie złapało. To przepuszcza
  kompilator hosta przez siedem konfiguracji w jakieś trzydzieści sekund i
  raportuje każdy symbol, który istnieje w jednej, a nie ma go w drugiej. Nie
  zastępuje builda Arduino — zaślepki sięgają tylko tyle, żeby zadowolić
  `#include`, a cel ARM nie jest w ogóle sprawdzany — ale klasa błędów, którą
  łapie, to dokładnie ta, która kosztowała trzy wymiany maili.

- **Zmierzone progi per poziom dla algorytmu 12 (`MF`, `MFT`).** Tabela `auto`
  nie była adaptacyjna: sigma opiera się o swoją podłogę 5 ns na każdej płytce
  tej konstrukcji, więc tabela wychodziła wszędzie identyczna, a jej skalowanie
  poziomów zakłada biały szum fazy (wykładnik 0,5) tam, gdzie obie tutejsze
  płytki mierzą 0,95–1,03. `MF 3` zastępuje założenie EMA statystyki testowej
  per poziom, dopasowaniem wykładnika metodą najmniejszych kwadratów i kwantylem
  przy interwale docelowym ustawianym przez `MFT`. `ML` raportuje, które źródło
  jest w użyciu, dopasowany wykładnik i liczbę poziomów. Zweryfikowane wobec
  własnego wyjścia `ML` firmware'u i wobec dwóch zapisów sprzętowych offline;
  **jeszcze nie w zamkniętej pętli** — ten pomiar jest zaległy, a na cichszej z
  dwóch płytek zmierzona tabela odtworzyła się GORZEJ niż założona (mediana
  13-minutowego RMS 11,6 ns wobec 5,5), bo obie tabele zadają różne pytania.
- **Anonimizacja pozycji GPS w tunerze.** Checkbox, zatrzaskiwany przy otwarciu
  pliku zapisu i wyszarzany w trakcie logowania, zastępuje Lat/Lon/Alt w
  zapisywanym logu i dopisuje dwuwierszowy nagłówek proweniencji. Liczba
  satelitów i HDOP zostają.
- **`tools/lock_sim_algo10.py`** — symulator etapu LOCK, z którego pochodzą
  powyższe liczby, żeby dało się je sprawdzić niezależnie.
- **Tuner zapisuje CSV obok surowego logu i trzyma tydzień.** Powstał jako
  narzędzie do strojenia i tak o sobie mówi, ale jest używany jako rejestrator,
  a przebieg stabilnościowy, który kończy się przez przewinięcie bufora, to
  przebieg do powtórzenia. Historia wykresów rośnie z 30 h do 604 800 próbek —
  tygodnia przy telemetrii 1 Hz — a lista rozwijana obok **Start logging**
  wybiera *Full log*, *CSV only* albo *Both*, zamrożona na czas życia pliku tak
  samo jak checkbox anonimizacji.

  CSV to jeden wiersz na sekundę telemetrii i niesie to, czego każda
  dotychczasowa analiza tych logów faktycznie potrzebowała, a nie wszystko, co
  firmware wypisuje: `utc, up_s, algo, state, dph_ns, qerr_ns, vphase_v, pwm,
  f10, f100, ph_ns, level, corr, sig_ns, zc, bmp_c, sat, hdop`. Około 65 MB na
  tydzień wobec 217 dla pełnego logu. `Vctl` pominięte, bo to `pwm` przez RC, a
  `pwm` jest liczbą dokładną; wilgotność, ciśnienie i szyny INA pominięte, bo
  przez wszystkie dotychczasowe przebiegi nigdy nie ruszyły się na tyle, żeby
  cokolwiek wyjaśnić. Kolumn z pozycją nie ma w ogóle, więc CSV jest
  zanonimizowany z konstrukcji, niezależnie od checkboxa.

  Budowniczy wiersza nie zakłada niczego o kolejności sześciu linii bloku
  telemetrii, bo ta kolejność nie jest stała — linię czujników wypisuje inne
  zadanie niż linię pętli, i zapis z 20.08 ma ją na końcu tam, gdzie zapis z
  21.08 ma ją na początku. Wiersz zamyka się w chwili, gdy linia próbuje zapisać
  pole już ustawione, co może znaczyć tylko tyle, że zaczęła się następna
  sekunda.

- **Trzy sekcje instrukcji, na które zapracowało odpowiadanie dwa razy na to
  samo.** *10.1 Jak zgłosić problem* prosi o cztery rzeczy, których potrzebowała
  tu każda diagnoza — log bootowania jako tekst, skompilowany
  `gpsdo_config.h`, jaki moduł GNSS (prawdziwy timingowy, prawdziwy nawigacyjny
  czy klon) i ile nieba widzi antena — plus jedno sprawdzenie, które warto
  zrobić najpierw: baner bez linii `compiled <data>` znaczy build sprzed v1.05,
  a kilka zgłoszeń okazało się błędami już naprawionymi. *Jak zapisać survey na
  stałe* (Część 3.2) opisuje wykonanie survey raz w u-center V8.29 i zapisanie
  go w podtrzymywanej bateryjnie konfiguracji modułu, co bije kompromis 300 s /
  5 m firmware'u i przeżywa wyłączenia zasilania — zalecenie Alana Cashina,
  najtańsza dokładność w całej konstrukcji. A *Aneks C — Słowniczek* definiuje
  dwadzieścia jeden pojęć, których ten projekt używa w konkretnym znaczeniu, od
  ADEV po ZC, bo połowa z nich gdzie indziej znaczy co innego.

---

## [v1.05-rtos] — 2026-08-20

Algorytm 12 doprowadzony do działania. W v1.04 wyszedł z poprawną arytmetyką i
pięcioma osobnymi usterkami w maszynerii wokół niej, z których każda zasłaniała
następną. Pętla trzyma teraz fazę na poziomie 5–8 ns RMS przez 23 godziny przy
jednym przezbrojeniu picDIV, wobec 10–23 ns najlepszego dotychczasowego
odniesienia — a każda poprawka poniżej została przesymulowana przed wgraniem, bo
dwie zmiany w tym projekcie, które poszły na samym rozumowaniu, obie okazały się
błędne.

### Naprawione
- **Estymator szumu mógł już tylko maleć.** Bramka odstających brzmiała
  `dp_lim = 5*sigma` i czytała estymatę, którą sama karmiła: gdy sigma raz
  zmalała, każda różnica dość duża, by ją podnieść, była odrzucana jako
  odstająca. Zmierzone 14.08 — `sig` czytało dokładnie 2 ns przez wszystkie 1020
  próbek przebiegu, a wyprowadzone z niej granice poziomów przypięły hierarchię
  do podłogi 100 jednostek: 79 z 80 korekcji odpaliło na poziomie 0. Akumulator
  wielopoziomowy, który nigdy nie opuszcza poziomu zerowego, nie jest
  wielopoziomowy. Bramka jest teraz bezwzględna (300 ns), a prawdziwe odstające,
  dla których powstała — różnice liczone w poprzek przerwy NOPH/SYNC/re-arm —
  są odcinane strukturalnie flagą ciągłości, a nie statystycznie. Sigma ma
  podłogę 5 ns, poniżej której ten detektor nie rozdziela uczciwie.
- **Człon częstotliwości miał odwrócony znak.** Nosił `+polarity`, przepisane z
  gałęzi częstotliwości algorytmu 11 — ale tamta gałąź czyta TIM2, a własny
  komentarz algo 11 w tym firmware zapisuje ustalenie sprzętowe, że TIM2 i
  detektor LTIC mają na tym okablowaniu przeciwną orientację. Nachylenie `f_nss`
  nie jest odczytem TIM2: to pochodna tych samych wartości akumulatora, które
  dają człon fazowy, z tego samego czujnika. Wielkość i jej własna pochodna po
  czasie, mierzone jednym czujnikiem, nie mogą wymagać przeciwnych znaków
  sprzężenia. `cvPWM` Alana zgadza się z tym — przepuszcza fazę i nachylenie
  przez jedną konwersję i dodaje je. Przy plancie zmierzonym, a nie założonym
  (+319,5 µHz/LSB, z regresji stusekundowej średniej PWM wobec drukowanej
  stusekundowej średniej częstotliwości, korelacja 0,999 przy zerowym
  opóźnieniu), stary znak dawał `d(phase_rate) = +0,4·f_ns`. To dodatnie
  sprzężenie.
- **`s_mla_wait` nigdy nie było zerowane.** Występowało w pliku dokładnie dwa
  razy — w deklaracji i przy `++` w teście rezygnacji — i nigdy nie wracało do
  zera. Około pięciu minut po starcie przekraczało 300 i test rezygnacji kasował
  flagę w tej samej sekundzie, w której była podnoszona, co zabijało obie rzeczy,
  które ta flaga bramkuje: korekcję przejścia przez zero i blokadę nowych
  korekcji w trakcie dochodzenia fazy. Przebieg, który to wykrył: `zc` = 7 przez
  76 minut, wszystkie w pierwszych pięciu, i 1072 z 1174 korekcji dokładnie dwie
  sekundy od siebie, czyli goła kadencja poziomu 0, gdy nic jej nie hamuje.
  Mechanizm przejścia przez zero nigdy więc nie działał dłużej niż pierwsze
  minuty jakiegokolwiek przebiegu od czasu wprowadzenia.
- **Ratunkowy FLL był regulatorem bang-bang i nie mógł być niczym innym.** Krok
  wynosił `-f*lsb_per_hz*0,10` klamrowany do ±64, co nasyca się przy
  |f| = 0,256 Hz, podczas gdy bramka poniżej otwierała się dopiero przy 0,3 Hz —
  człon proporcjonalny nie mógł więc zadziałać nigdy. Napędzany co sekundę ze
  stusekundowej średniej, czyli około 50 s opóźnienia, daje 64 LSB/s × 50 s =
  3200 LSB drogi, zanim pomiar zareaguje: 1,0 Hz przeregulowania. Obie liczby są
  w logach (462 z 655 kolejnych kroków dokładnie ±64; PWM przebiegające 12 845
  LSB; `f100` wahające się od −1,29 do +1,55 Hz). Teraz aplikuje całą policzoną
  korekcję raz i przytrzymuje przez tyle, ile potrzebuje na odświeżenie średnia,
  z której ją policzono, w dwóch biegach: średnia 10-sekundowa przy dużym
  błędzie, 100-sekundowa gdy zmaleje, a przytrzymanie zawsze odpowiada oknu w
  użyciu. Bramka zeszła z 0,3 Hz na 0,05 Hz, bo powyżej 0,147 Hz faza przebiega
  całe pasmo detektora ±940 ns w jednym horyzoncie 64 s — stara bramka
  zostawiała martwą strefę od 0,147 do 0,3 Hz, w której pętla fazowa nie
  dostawała dość długiego okna, a FLL uznawał swoją pracę za skończoną.
- **`instant_offset` zawijało się.** `FREQ_LOWER`/`FREQ_UPPER` dopuszczają
  ±500 Hz, a pole było `int8_t`, więc wszystko powyżej ±127 podawało śmieci
  każdej bramce, która je czytała. Teraz `int16_t`, we wszystkich trzech plikach,
  które go dotykają — w strukturze, w rzutowaniu, które je wypełnia, i w
  migawce, która je kopiuje. Naprawienie tylko jednego skompilowałoby się
  czysto i zostawiło zawijanie na miejscu.
- **Gałąź częstotliwości i gałąź FLL brały znak z zaszytego minusa.** Poprawne
  wyłącznie dlatego, że `LPOL` na tej płytce wynosi −1; na płytce z `LPOL +1` to
  dodatnie sprzężenie. Obie biorą teraz `polarity` z płytki, co tutaj wylicza się
  identycznie, a gdzie indziej poprawnie.
- **Dither zapisywał tylko jedną ze swoich dwóch tablic DMA.** Podwójne
  buforowanie zmienia tablicę co przebieg, więc druga — wciąż z poprzednim kodem
  — grała aż do następnego zapisu, a wyjście przeskakiwało między starą a nową
  wartością z częstością około 3 Hz (jeden przebieg to 2^(24−N) okresów nośnej =
  167,8 ms niezależnie od wyboru N). Filtr dwubiegunowy 0,8 Hz tłumi 3 Hz
  zaledwie 14×. Obie tablice są teraz wypełniane, pod muteksem, bo
  `pwm24_write()` jest osiągalne i z ControlTask, i z CliTask, a dwa równoległe
  wypełnienia jednej tablicy przeplatają się w rozdarty 168-milisekundowy
  przebieg. Nadpisywanie tablicy czytanej przez DMA jest bezpieczne przez
  wyprzedzenie: wypełnianie zapisuje wpis co kilka mikrosekund, gdy DMA
  konsumuje jeden co 81,9 µs.
- **`PO` i `AO` nie dawały się ustawić na zero.** Kontrola zakresu brzmiała
  `v >= −3000 && v <= 3000 && v != 0.0f`, więc jedyna wartość, której użytkownik
  najpewniej chce, była jedyną odrzucaną. Zakresy poprawione na ±5000 Pa i
  ±3000 m, a obie komendy podają teraz jednostki w pomocy i w echu.

- **Płytka nie zawsze wstawała po zimnym starcie, a szyna 3,3 V nigdy nie była
  przyczyną.** `ubx_poll_svin_nav()` wołało `vTaskDelay()` bezwarunkowo. Jego
  bliźniak `ubx_poll_svin()` ma strażnika i komentarz, który to wyjaśnia —
  *„before vTaskStartScheduler() this must not be vTaskDelay(): calling it with
  no scheduler hangs the system"* — a poprawka trafiła do jednej z tych dwóch
  funkcji i nie trafiła do drugiej.

  Przed startem schedulera `vTaskDelay()` pisze przez `pxCurrentTCB`, który jest
  jeszcze `NULL`, więc płytka wpada w hard fault, a domyślny handler kręci się w
  pętli z wyłączonymi przerwaniami: żadnego wyjścia, żadnego watchdoga, tylko
  przycisk reset. Haki awaryjne FreeRTOS dodane w v1.04 tego nie złapią —
  potrzebują działającego jądra.

  Usterka chowała się za kolejnością wywołań w `gpsdo_gps_init()`: NAV-SVIN jest
  odpytywane wyłącznie wtedy, gdy TIM-SVIN nie odpowie w swoim oknie 500 ms.
  Odbiornik, który już pracuje, odpowiada i płytka startuje — tak jest po
  resecie, bo odbiornik ma własne zasilanie. Odbiornik dopiero wstający nie
  odpowiada i płytka staje. To właśnie przypadek zimnego startu, i ta asymetria
  jest powodem, dla którego rzecz przez długi czas wyglądała na zasilanie
  siadające przy narastaniu szyny.

  Znalezione w logu z czterema kolejnymi startami, z których każdy kończył się
  po `UBX: CFG-NAV5 ACK`, a przed `LEA-T: starting survey-in`, przy przyczynie
  resetu `PIN/NRST` za każdym razem — czyli przycisku operatora. Dekoder wypisuje
  `POWER-ON/BROWN-OUT` i osobną linię o sprawdzeniu szyny 3V3, gdy winne jest
  zasilanie, i nie pojawiła się ani razu. Usterka zasilania nie zatrzymuje się
  cztery razy na tej samej linii kodu.
- **Cyfry częstotliwości nie podążały za stanem lock algorytmu 12.** Logika
  koloru ma gałąź autorytatywną dla pętli publikujących stan na żywo, a
  algorytmu 12 nie było na tej liście — spadał więc do gałęzi oceniającej lock ze
  średnich częstotliwości, czyli robił dokładnie to, przed czym ostrzega
  komentarz nad tą gałęzią.

  Zmierzone na przebiegu 2,99 h: pętla i kolor rozjeżdżały się przez **15,0%**
  próbek, a każdy taki przypadek to pętla w LOCK i białe cyfry — nigdy odwrotnie.
  Pętla weszła w LOCK w 108 s, cyfry zzieleniały w 1041 s. Piętnaście minut
  zdyscyplinowanego oscylatora wyglądającego na niezdyscyplinowany, przy każdym
  starcie, bo dopóki nie napełni się średnia 1000 s, ta gałąź nie ma czym
  oceniać i `locked` jest fałszywe z konstrukcji.

  Algorytm 12 bierze teraz kolor z własnego trendu, jak 10 i 11. `CORR` i `ZC`
  liczą się jako lock: to stany jednosekundowe znaczące, że pętla robi swoje —
  ta sama logika, dla której nie zerują `s_mla_quiet`. Bez tego cyfry mrugałyby
  na biało przy każdej korekcji, czyli szesnaście razy w zmierzonych trzech
  godzinach. Zgodność wynosi teraz 100%.
- **Bramka „stale echo" była ustawiona na pół kwantu.** Odbiera ona lock oparty
  na długiej średniej, gdy średnia 10-sekundowa odjechała, a próg wynosił
  ±50 mHz. Ale średnia 10 s to zliczenie cykli przez dziesięć sekund, więc jej
  ziarno to 0,1 Hz: przez trzy godziny przyjęła dokładnie trzy wartości —
  −100, 0 i +100 mHz — i nic pomiędzy. Próg 50 mHz nie znaczył więc „w granicach
  50 mHz", tylko „licznik musi pokazać dokładnie 10 000 000", a jeden kwant w
  którąkolwiek stronę gasił zieleń. To 8,3% ustalonych próbek i 46% opisanego
  wyżej rozjazdu. Teraz ±0,15 Hz: jeden pełny kwant plus pół kwantu zapasu, więc
  pojedyncze drgnięcie przechodzi, a rzeczywista utrata dyscypliny — wiele
  kwantów, czyli to, po co ta bramka istnieje — nadal ją zatrzymuje. Algorytmy
  0–9 mają tę samą bramkę i tę samą poprawkę.

### Dodane
- **Bramka poziomowa na człon częstotliwości.** Własny szum nachylenia to
  `sd(f_nss) = sigma · 2^((1−3L)/2)`, więc na poziomie 0 jest to 1,41·sigma
  czystego szumu skalowanego przez 12,5 LSB na ns/s, wobec 0,39 LSB na ns dla
  członu fazowego — przewaga 32:1 na rzecz niewłaściwej wielkości. Log pokazał,
  co to kupuje: 46% korekcji uderzało w klamrę ±470, jedna z nich przy fazie
  czytającej dokładnie 0 ns i korekcji na pełnej skali. Człon jest teraz używany
  od poziomu 3 w górę, gdzie ta sama estymata jest uśredniona po parach
  16-sekundowych i znów jest pomiarem.
- **Trim częstotliwości z TIM2.** Gdy średnia stusekundowa pokazuje więcej niż
  0,03 Hz, składowa częstotliwościowa korekcji brana jest z tego pomiaru zamiast
  z nachylenia akumulatora. W stanie ustalonym jest uśpiony — w dziesięciogodzinnej
  symulacji nie odpalił ani razu — i o to właśnie chodzi: łapie wyskoki
  częstotliwości, które inaczej wyprowadziłyby fazę poza pasmo detektora, więc
  pętla nigdy nie musi wchodzić w lock od nowa. Dwudziestotrzygodzinny przebieg,
  który ustalił powyższe liczby, zanotował **jedno** przezbrojenie picDIV, wobec
  121 przy tych samych nastawach bez niego.
- **`configUSE_MUTEXES` i `INCLUDE_xTaskGetSchedulerState`**, ustawione jawnie.
  Blokada dithera potrzebuje obu, żadne nie było ustawione przez ten projekt, a
  tego, czy domyślna konfiguracja biblioteki je włącza, nie zostawia się
  przypadkowi: brakujące makro to błąd kompilacji, nie niespodzianka w runtime.

- **Dolne 8 bitów ditheru dociera wreszcie do pętli.** v1.04 wypuściła wyjście
  24-bitowe i napisała w tym changelogu, że pętla nie dostaje jeszcze
  drobniejszego kroku: każdy algorytm wołał `gpsdo_dac_write16()`, które
  przesuwało wartość w górne 16 bitów, żeby zapisane ustawienia zachowały swoje
  napięcie, a dolny bajt zawsze był zerem. Już nie jest.

  Ułamek należy do `gpsdo_dac.cpp`, nie do pętli sterowania, i to jest cały
  pomysł. Wartość sterująca zapisywana jest z 21 miejsc — z przemiatań `CT` i
  `LC`, z ramp akwizycji, ze sterowania w holdoverze, z `SP` i z samej pętli — a
  dwadzieścia z nich jest zgrubnych z rozmysłem: przemiatanie, które zatrzyma się
  na 30720,4 zamiast na 30720, nie jest lepszym przemiataniem, tylko takim,
  którego punktu odniesienia nikt nie potrafi podać. Każdy zgrubny zapis kasuje
  ułamek przy okazji tego, że trafia do `gpsdo_dac_write16()`, więc żaden
  wywołujący nie musi o tym pamiętać. Trzymanie ułamka w pętli oznaczałoby
  dwadzieścia miejsc, z których każde musiałoby wiedzieć, że ma go wyzerować — a
  to dokładnie ta klasa błędu, dla której ten jeden punkt zapisu powstał.

  Co to daje na zmierzonym tutaj obiekcie: jeden krok 16-bitowy to około 320 µHz,
  czyli 3,2e-11 z 10 MHz — grubiej, niż 4e-12, które pętla zmierzono trzymała
  przez 10 000 s. Dochodziła tam ditherując między sąsiednimi kodami z korekcji
  na korekcję, co działa, ale zostawia napięcie sterujące w ciągłym polowaniu. Z
  zachowanym ułamkiem korekcja mniejsza od jednego kroku jest stosowana zamiast
  obcinana, a krok schodzi do 1,25e-13.

  Obcięcie, które znika, było przy tym stronnicze: `(int32_t)` zaokrągla w stronę
  zera, więc każda korekcja traciła część siebie w tę samą stronę — co pętla
  widzi jako błąd wzmocnienia sięgający jednej szóstej przy korekcjach rzędu
  6 LSB, obserwowanych w normalnej pracy.

  Powyżej warstwy DAC nie zmieniło się nic. `gpsdo_dac_last16()` nadal zwraca
  zwykłe `uint16_t`, więc wyświetlacze, linia telemetrii i flash ring widzą
  dokładnie to, co widziały wcześniej, a blok ustawień nadal zapisuje 16 bitów:
  odtworzenie startuje z zerowym ułamkiem i oddaje co najwyżej 1,25e-13, czyli
  mniej, niż ten sprzęt jest w stanie pokazać.
- **`DAC` — komenda, która mówi, czym naprawdę jest napięcie sterujące.** Ścieżka
  wyjściowa, a dla ditheru częstotliwość nośnej i RAM zajęty przez tablice; kod w
  trzech ujęciach — 24-bitowym, zaokrąglonym 16-bitowym, którego używają
  wyświetlacze i flash ring, oraz dokładnym ułamkowym wraz z różnicą względem
  zaokrąglonego; zmierzone Vctl; oraz wielkość kroku dla obu szerokości, w µHz i
  jako ułamek 10 MHz. Kod 24-bitowy niebędący wielokrotnością 256 jest dowodem na
  to, że pinem steruje ścieżka precyzyjna — i dlatego drukowane są wszystkie trzy
  ujęcia, a nie jedno; komenda mówi też wprost, czy ścieżka precyzyjna jest
  aktywna, czy wyjście i tak ją zaokrągla.

  Liczby kroku wymagają wzmocnienia obiektu, którego dostarcza tylko `CT`. Bez
  niego komenda to mówi, zamiast drukować liczbę wyprowadzoną z wartości
  domyślnej. Wpisana także do zakładki Help w tunerze.

- **`MF` i `MFT` — limity per poziom dostają własne źródło, wybierane niezależnie
  od wzmocnienia.** Oba siedziały w jednym `if`, więc `MG 0` znaczyło
  „wzmocnienie z CT **i** limity z formuły szumu", a `MG > 0` — „wzmocnienie
  ręcznie **i** limity ręcznie". Nie ma powodu, by były zespawane: wzmocnienie
  należy do OSCYLATORA (jest w LSB na ns, a inny OCXO ma inną czułość Vctl),
  natomiast limity należą do SZUMU FAZY, jaki widzi płytka, czyli do miejsca i
  odbiornika. „Zmierzone wzmocnienie, ręczne limity" — dokładnie to, czego
  potrzebuje hałaśliwa instalacja — nie dało się w ogóle wyrazić.

  `MF 0` śledzi `MG` jak dotąd i jest domyślne, więc bez wyraźnej prośby nic się
  nie zmienia. `MF 1` trzyma tablicę zapisaną, `MF 2` formułę szumu, `MF 3`
  tablicę mierzoną poniżej. Oba ustawienia mieszczą się w trzech bajtach
  wyrównania, które blok algo-12 już miał, więc układ, rozmiar i `SETTINGS_VER`
  zostają bez zmian, a starszy zapis nadal się ładuje — odczytuje się jako 0/0,
  czyli dokładnie zachowanie tamtej wersji.
- **`MF 3` — limity per poziom mierzone zamiast ekstrapolowanych.** Formuła
  brzmi `thr[L] = 8·σ·√(2^L)·√10`, a `√(2^L)` mówi, że faza jest BIAŁA, czyli że
  uśrednienie 2^L próbek zbija test jak 2^(L/2). Zmierzone na dwóch płytkach tej
  konstrukcji — to samo PCB, ten sam OCXO, różne pomieszczenia — wykładnik
  wynosi **0,95 i 1,03**, a nie 0,50. Uśrednianie prawie nic tu nie daje, bo
  liczy się wolna wędrówka (autokorelacja 0,96 przy 60 s, 0,64 przy 300 s), a nie
  szum próbka-do-próbki. Błąd rośnie z poziomem: formuła zaniża rzeczywisty
  rozrzut około 5× na poziomie 0 i ponad 100× na poziomie 10, więc jej tablica
  opada 32× w skali hierarchii tam, gdzie sama faza opada 1,3×.

  Wykładnik jest więc mierzony. Każdy poziom trzyma średni kwadrat swojej własnej
  statystyki testowej, dopasowanie najmniejszych kwadratów log2(sd) względem
  poziomu daje amplitudę i wykładnik naraz, a tablica powstaje z dopasowania.
  Dopasowanie PO poziomach, a nie zaufanie każdemu z osobna, jest tym, co czyni
  je użytecznym wcześnie — poziom 8 jest testowany raz na 512 s i sam
  potrzebowałby pół doby na własną wariancję, ale niskie poziomy zapełniają się w
  minuty i dopasowanie ekstrapoluje.

  Znika przy tym pięć liczb wpisanych na sztywno: wykładnik 0,5, mnożnik `8.0`
  (teraz kwantyl rozkładu normalnego dla częstości fałszywych strzałów zadanej
  przez `MFT` — czyli robota, którą ósemka wykonywała ręcznie, bo hierarchia
  testuje poziom 0 tysiąc razy częściej niż poziom 10), propagacja białego szumu
  `√10`, podłoga σ na 5 ns — właściwość tego detektora, nie arytmetyki — oraz
  podłoga 100 jednostek. Zostaje jedna liczba o fizycznym znaczeniu: co ile
  czasu wolno wystąpić korekcji wywołanej samym szumem.

  Wykładnik jest klamrowany do [0,5; 1,0] i jest to fizyka, nie gust. Poniżej 0,5
  uśrednianie usuwałoby więcej, niż pozwala biały szum; powyżej 1,0 rozrzut
  rośnie szybciej niż płasko-w-ns, czyli mamy RAMPĘ fazy, a nie hałaśliwszą
  płytkę — a wpuszczenie rampy do progu to awaria już w tym pliku zapisana, gdzie
  sigma wspięła się 165 → 746 ns i pętla zamarzła.

  Zweryfikowane przez odtworzenie arytmetyki firmware po zapisach obu płytek:
  tablica warsztatowa wychodzi 74 ns opadające do 14, czyli tam, gdzie ta płytka
  została ustawiona ręcznie po tym, jak auto okazało się niestabilne, a płytka
  domowa odtwarza własne ustalone zachowanie. Na trzygodzinnym przebiegu dom
  dopasował **α = 1,00** i korygował na poziomach od 5 do 9 — po raz pierwszy ta
  hierarchia użyła więcej niż jednego czy dwóch swoich poziomów.

  **Nie jest to automatycznie lepsze.** Na płytce domowej, gdzie znacznie
  ciaśniejsza tablica z formuły przypadkiem pasowała do cichego miejsca, tablica
  mierzona podwaja RMS fazy (mediana 11,6 ns wobec 5,5 ns w oknie tej samej
  długości), bo koryguje trzy razy rzadziej. Te dwie tablice zadają różne
  pytania — formuła pyta, czy odchylenie przekracza szum pomiarowy, a tablica
  mierzona, czy jest nietypowe dla tej płytki — i to, która ma rację, zależy od
  miejsca. Po to właśnie jest `MF`.

### Zmienione
- `LOCK` w polu trendu znaczy teraz, że hierarchia jest cicha **oraz** że
  częstotliwość z TIM2 mieści się w 0,05 Hz, liczone po kolejnych cichych
  sekundach, a nie po `s_mla_count`, który zeruje się przy każdej korekcji i był
  kiepskim wskaźnikiem tego, jak dawno cokolwiek się wydarzyło.

- **`GPSDO_PWM_DITHER` jest włączony w wysyłanej konfiguracji.** W v1.04 wyszedł
  wyłączony, dopóki ścieżka wyjściowa nie była sprawdzona; po domknięciu ścieżki
  precyzyjnej i 23-godzinnym przebiegu „wyłączone" przestało być uczciwym
  domyślnym ustawieniem. Zakomentowanie go nadal wraca do zwykłego PWM
  16-bitowego, a pin, filtr i okablowanie są w obu przypadkach te same.
- **Układ pól na panelu 320×240 jest wreszcie taki jak na 480×320.** Ten podręcznik
  od v0.93 pisze, że ekran roboczy jest projektowany raz i skalowany — i to była
  prawda o geometrii, a nie o treści: oba panele rozjechały się pole po polu.
  qErr przeniósł się do wiersza Alt, obok danych fiksu, do których należy; AHT i
  pole fazy zamieniły się kolumnami, więc czujniki środowiskowe dzielą lewą
  kolumnę, a elektryczne prawą; Vcc i Vdd zajęły wspólnie zwolniony wiersz. Mały
  panel pokazuje wszystko to, co duży.

  Każde pole, które łączyło etykietę z wartością o zmiennej szerokości, zostało
  rozbite na dwa. Pojedynczy napis kotwiczony do prawej unieruchamia jednostkę, a
  etykietę ciągnie na boki wraz ze zmianą szerokości cyfr — na qErr widać to było
  jako etykietę skaczącą raz na sekundę. Etykieta i wartość to teraz osobne sloty
  z osobnym paddingiem: etykieta trzyma lewą krawędź kolumny, wartość zachowuje
  prawą kotwicę, a zmienia się tylko odstęp między nimi. Tak samo `dph` i prąd
  INA.
- **Font 2 jest proporcjonalny, a ten układ był liczony po 8 px na znak.**
  Sprawdzone z tablicą szerokości samej biblioteki: to zawyża napisy małego panelu
  o jakąś jedną piątą — `Vph:1.951V` mierzy 70 px, nie 80. Błąd nie był
  akademicki: to on kosztował etykietę `dph` i to on trzymał Vcc na dwóch
  miejscach po przecinku tam, gdzie 480 pokazuje trzy. Obie rzeczy wróciły. Pola
  po prawej dzielą teraz jedną linię wyrównania na x=314 — tę, na której Vdd
  siedziało od dawna — więc qErr, dph, prąd INA i Vdd tworzą kolumnę zamiast
  czterech prawie-trafień. Padding każdego pola to teraz zmierzona szerokość jego
  własnej najszerszej formy, a nie dzisiejszego odczytu, i paddingi w wierszu
  kafelkują go dokładnie, więc żadne tło nie może zjeść krawędzi sąsiada.

### Podziękowania
- **Alan Cashin** (MIS42N z forum EEVBlog) jest teraz wymieniony tam, gdzie praca
  jest jego: w `V`, w nagłówku pomocy, na ekranie About tunera i w tabeli
  podziękowań wszystkich trzech instrukcji. Algorytm 12, korekcja przejścia przez
  zero, ditherowany PWM i pomysł na samoocenę `CS` pochodzą z jego Budget GPSDO.
  Dotąd figurował jako „dither / DAC discussion", co znacznie to zaniżało.

### Zmierzone
Dwadzieścia trzy godziny, progi automatyczne, `MR 9`, dither 13-bitowy:

| | ten przebieg | najlepszy poprzedni |
|---|---|---|
| faza RMS, po osiadaniu | **5–8 ns** | 10–23 ns |
| \|faza\| < 10 ns | **86,7%** próbek | — |
| przezbrojenia picDIV | **1** | 121 |
| osiągane poziomy korekcji | **typowo 5–6, do 8** | 0 |
| częstotliwość na 10 000 s | **4e-12** | 1,4e-11 |
| odstęp między korekcjami | 254 s | 130 s |

`NOPH` trzy razy na 82 572 próbki; `FLL` raz. Ciśnienie otoczenia spadło w trakcie
przebiegu o 4 hPa, a pętla nie zareagowała.

---

## [v1.04-rtos] — 2026-08-12

### Dodane
- **`GPSDO_PWM_DITHER` — 24-bitowe napięcie sterujące z krótkiego PWM z ditherem.**
  Pomysł Alana Cashina (MIS42N): puść PWM na mniejszej liczbie bitów, niż
  potrzebujesz, i zmieniaj wypełnienie z okresu na okres tak, by resztę niosła
  średnia.

  Zyskiem jest NOŚNA, a nie dodatkowe bity. Tętnienie trzeba odfiltrować poniżej
  jednego kroku wyjścia, a jak trudne to jest, zależy od odstępu między nośną a
  zakresem filtru: PWM 16-bitowy przy 2 kHz pozwala na zakres 0,7 Hz i stałą
  czasową 230 ms, a dither 13-bitowy przy 12,2 kHz — na 4,2 Hz i 38 ms.
  Opóźnienie filtru wchodzi do pętli wprost jako przesunięcie fazy, więc
  sześciokrotnie krótszy filtr jest wart więcej niż sama rozdzielczość.

  Alan ditheruje w przerwaniu timera, bo PIC nie ma DMA. Tutaj byłoby to 12 000
  przerwań na sekundę konkurujących z przechwytem 1PPS — jedynym przerwaniem,
  którego nie wolno opóźniać. Ale wzór dla stałej wartości jest okresowy, więc
  jest liczony raz do tablicy i odtwarzany przez DMA do rejestru porównania:
  0,012% CPU przy 13 bitach i ani trochę tego w przerwaniu. Średnia jest dokładna
  z konstrukcji — tablica trzyma dokładnie Y wpisów o wartości X+1 wśród
  2^(24-N).

  Ten sam pin co dotąd (PB9, TIM4 CH4), więc dotychczasowy filtr i okablowanie
  zostają bez zmian. TIM4_UP steruje DMA1 Stream 6 Channel 2; takt 2 Hz jest na
  TIM9, a łańcuch 1PPS na TIM2/TIM3, więc nic innego nie jest ruszane. Dwa bufory
  w sprzętowym trybie double-buffer sprawiają, że zmiana wartości nigdy nie daje
  glitcha na pinie.

  Domyślnie wyłączone. Kosztuje 8 KB RAM przy 13 bitach, 16 KB przy 12.

  **Czego to jeszcze nie daje**, to drobniejszego kroku dla pętli: każdy algorytm
  woła `gpsdo_dac_write16()`, które przesuwa wartość w górne 16 bitów, żeby
  zapisane ustawienia zachowały swoje napięcie. Dolne 8 bitów czekają na pętlę,
  która zawoła `gpsdo_dac_write24()`.
- **Korekcja przy przejściu przez zero, ze schematu Alana.** Po korekcji
  granicznej, która zmienia częstotliwość, faza idzie dalej w tę stronę, w którą
  już szła: przemiata przez zero, wychodzi po drugiej stronie i zwykle znów
  przekracza granicę — więc pętla koryguje, przestrzeliwuje, koryguje z powrotem i
  osiada powoli.

  Chwila przejścia fazy przez zero jest szczególna. Błąd fazy jest zerowy, ale
  błąd częstotliwości, który ją tam doprowadził, wciąż istnieje; skasowanie błędu
  częstotliwości dokładnie wtedy zostawia oscylator z właściwą częstotliwością ORAZ
  bez błędu fazy — zamiast w stanie, do którego pętla musi dopiero dochodzić
  kolejnymi iteracjami.

  Zmierzone względem własnych logów Alana z tej samej konstrukcji: jego pętla
  koryguje co 506 sekund, gdzie ta korygowała co 130. Większość tej różnicy to
  właśnie ten test, który Alan nazywa niezbędnym, a którego tutaj brakowało.

  Raportowane jako `zc=` w telemetrii; trend pokazuje `ZC` w momencie zadziałania.
- **Algorytm 12 — akumulator wielopoziomowy.** Wg konstrukcji Alana Cashina
  (MIS42N z forum EEVblog). Każda inna pętla tutaj ma jedną stałą czasową, a ta
  jest kompromisem, którego nikt nie wygrywa: zmierzone względem wzorca
  rubidowego, `LTC 60` jest do 1,58× lepsze powyżej 800 s, a `LTC 240` do 1,44×
  lepsze między 10 a 400 s. Ta pętla nie wybiera. Odczyty gromadzą się w
  poziomach — poziom n obejmuje 2^n sekund — a korekcja następuje na
  **najniższym** poziomie, którego błąd przekracza granicę. Duży błąd działa w
  ciągu dwóch sekund, mały czeka na dłuższe uśrednienie. Nie ma `LTC` do
  ustawienia.

  Poziomy wynikają z układu bitów licznika sekund, a nie z tablicy buforów:
  jedenaście poziomów, od 2 s do 2048 s, za 22 bajty.

  **Wejściem jest faza w nanosekundach z detektora LTIC.** Pierwsza wersja
  karmiła algorytm błędem zliczeń TIM2 w całych hercach i była ślepa —
  zdyscyplinowany oscylator siedzi daleko poniżej 1 Hz, więc pole czytało zero w
  83% i 95% próbek w dwóch przebiegach, a akumulator nie gromadził nic. Faza się
  całkuje tam, gdzie sekundowy pomiar częstotliwości nie. Alan zapytał, dlaczego
  podałem 100 ns, skoro TIC rozdziela 1 ns — miał rację, podałem rozdzielczość
  licznika zamiast detektora.

  **Test częstotliwości został usunięty**, zgodnie z radą Alana: *„To był
  eksperyment... chcemy stabilnego układu, w którym testy zawsze przechodzą. Więc
  test częstotliwości jest zbędny."*

  Nowe komendy `MG`, `MR`, `MLP` i `ML`, zapisywane przez `ES ALGO12`. Granice
  poziomów są edytowalne i utrwalane, bo tylko **jedna** została kiedykolwiek
  wyprowadzona — 125 ns przy 128 s, ze specyfikacji 10 MHz ±0,01 Hz. Resztę Alan
  nazywa arbitralną.
- **Raportowanie przyczyny resetu przy starcie.** `RCC->CSR` jest odczytywany i
  dekodowany, zanim cokolwiek innego ruszy, więc sporadyczny restart nie wygląda
  już identycznie niezależnie od tego, czy przyszedł z zaniku zasilania, pinu
  reset czy resetu programowego. Dodane po tym, jak płytka restartowała się
  wielokrotnie w tym samym miejscu konfiguracji GPS, bez możliwości rozstrzygnięcia
  przyczyny.
- **Haki awaryjne FreeRTOS i własny `STM32FreeRTOSConfig.h`.**
  `configCHECK_FOR_STACK_OVERFLOW` i `configUSE_MALLOC_FAILED_HOOK` domyślnie są
  zerami, więc przepełniony stos po cichu psuje sąsiada, a `configASSERT` wpada w
  `for(;;)` z wyłączonymi przerwaniami — martwy biały panel i cisza na konsoli.
  Dokładnie tak wyglądały trzy ostatnie awarie: za mały stos CLI przy zapisie do
  flash ringu, odczyt pustej grupy zdarzeń przed startem schedulera i struktura w
  martwej gałęzi, która i tak powiększyła ramkę zadania wyświetlacza.

  Nadpisanie włącza oba haki i przedefiniowuje `configASSERT`, by wypisał plik i
  numer linii przed zatrzymaniem. Haki nazywają winne zadanie na konsoli USB i
  migają diodą, więc następna awaria przedstawi się sama w kilka sekund. Goły
  `Serial`, nie `OUT_SERIAL`: hak nie może dotykać muteksu ani strumienia
  Bluetooth, który sam może być przyczyną awarii.

  Autorstwo pliku: GLM-5.2, przyjęte tu zasadniczo bez zmian.

### Naprawione
- **Progi algorytmu 12 są teraz mierzone, a nie odziedziczone.** Były wzięte z
  konstrukcji Alana i przeskalowane stosunkiem kroków licznika, co jest złą
  wielkością: próg musi przekraczać **szum** pomiaru fazy, a ten różni się między
  konstrukcjami z powodów, których rozmiar kroku nie oddaje. Zmierzone na tej
  płytce: średnia fazy −1 ns przy odchyleniu standardowym 462 ns — oscylator był
  poprawnie ustawiony, a całe to rozrzucenie to szum, podczas gdy próg poziomu 0
  wynosił 462 ns. Przekraczało go 41% próbek. 620 korekcji w 1685 sekundach,
  hierarchia resetowana co 2,7 s i nigdy nieosiągająca poziomu 2.

  Firmware szacuje teraz szum fazy na bieżąco i wylicza z niego próg każdego
  poziomu. Przy okazji wyszedł drugi błąd: próg dotyczy wyrażenia testowego
  |3b − a|, którego odchylenie wynosi sigma·√(2^L)·√10, a nie średniej fazy, dla
  której jest to sigma/√N. Użycie drugiego czyniło próg 4,5× za niskim na poziomie
  0 i gorzej wyżej. Sześć sigma na właściwej wielkości daje odstęp między
  korekcjami rzędu minuty, wobec 256 s, na których osiada konstrukcja Alana.

  `ML` raportuje zmierzony szum i to, czy granice za nim podążają. W telemetrii
  jest jako `sig=`. Ustawienie `MG` powyżej zera zatrzymuje autotuning.
- **Algorytm 12 ignorował polaryzację detektora i mylił nanosekundy z hercami.**
  Dwie usterki w tym samym przeliczeniu, znalezione razem z jednego logu.

  `LPOL -1` nie było stosowane w ogóle — algorytm 11 mnoży swój człon fazowy przez
  `-polarity`, a ten nie — więc na takiej płytce każda korekcja szła w złą stronę.
  Do tego średnia faza w nanosekundach była mnożona przez LSB-na-herc, jakby to
  była ta sama wielkość: skasowanie P ns w czasie T sekund wymaga P/(100·T) Hz przy
  10 MHz, więc korekcja wychodziła 100·T razy za duża — od 200× na poziomie 0 do
  102 400× na poziomie 9. Każda uderzała w ogranicznik ±2000.

  Dodatnie sprzężenie zwrotne cztery rzędy wielkości za silne to uczciwy opis tego,
  co pokazał log: 6000 kroków wahnięcia PWM w 148 korekcjach.
- **`MG` i `MR` były przyjmowane i zapisywane, ale nigdy nieczytane.** Komendy
  działały, tuner je wysyłał, `ML` je odczytywało, a algorytm nie używał żadnej —
  ręcznie zadane wzmocnienie nic nie robiło, a wymuszony poziom korekcji nie
  istniał. Oba są teraz podłączone.
- **Algorytm 12 wymaga teraz detektora LTIC i wstrzymuje się zamiast zgadywać.**
  Był napisany tak, by na płytkach bez detektora całkować błąd zliczeń TIM2. Ta
  gałąź awaryjna była wręcz szkodliwa: zliczenia są skwantowane do całych herców i
  na zdyscyplinowanym oscylatorze czytają zero, więc ich całkowanie dawało
  błądzenie losowe szumu kwantyzacji, a nie fazę. Błądzenie przekraczało granicę
  poziomu, korekcja uderzała w ogranicznik, oscylator był wyrzucany na tyle, że
  detektor lądował na szynie, a szyna utrzymywała gałąź awaryjną przy życiu.
  Zmierzone: 6000 kroków wahnięcia PWM w 148 korekcjach, detektor na szynie przez
  58% czasu, a raportowana faza zablokowana na zerze.

  `LA 12` odmawia teraz bez `GPSDO_LTIC`, a gdy detektor jest, ale nie czyta,
  algorytm wstrzymuje się i pozwala pracować pomostowi picDIV. Cicha gałąź
  awaryjna niszcząca lock jest gorsza niż odmowa uruchomienia.
- **Algorytm 12 zbroi teraz picDIV.** Nie robił tego, a awaria była cicha: przy
  rampie na szynie detektor nigdy nie zwraca poprawnego odczytu, więc kod spadał
  do całkowania błędu zliczeń i algorytm znów był ślepy — dokładnie w sposób,
  któremu przejście na detektor miało zapobiec, bez żadnego śladu w telemetrii.
  Ten sam pomost z opóźnieniem, co w algorytmie 11.
- **Tuner przestał cokolwiek odczytywać z płytki.** `STATE_HINT` zostało dodane do
  `TelemetryParser`, ale użyte jako `self.STATE_HINT` z `GpsdoTuner` — innej klasy.
  Każda linia telemetrii rzucała wtedy `AttributeError` w obsłudze linii, więc
  żadna odpowiedź nie docierała do swojego absorbera i ani pola kalibracji z `LL`,
  ani tablica granic algorytmu 12 się nie wypełniały. Dwa objawy, jedna awaria.

  Obsługa jest teraz opakowana: błąd parsowania kosztuje jedną linię i wpis w
  monitorze, zamiast po cichu zabijać odbiór.
- **`MG` i `LG` odpowiadały tak samo — `gain=`.** Absorber Larsa biegnie pierwszy
  i przechwytywał odpowiedź algorytmu 12. Firmware odpowiada teraz `m_gain=` i
  `m_run_level=`.
- **Tabela granic w tunerze pokazywała zera.** Nigdy nie była odczytywana:
  zapytanie o parametry obejmowało tylko skalary, więc jedenaście pól stało na
  zerach, a naciśnięcie wysyłki nadpisałoby zerami tablicę, dla której firmware ma
  wartości domyślne. Tabela jest teraz czytana przy połączeniu, a wysyłka odmawia,
  dopóki którykolwiek wiersz jest zerem.
- **Płytka nie startowała: brak diody, brak konsoli, nic.** `setup()` zapisuje DAC
  trzy razy, zanim wykona się `xEventGroupCreate()`. Nowe statystyki korekcji wiszą
  na ścieżce zapisu DAC, a ich bramka czytała `xSysEvents`, wtedy jeszcze puste.
  Ramka stosu jest wymiarowana przy kompilacji, więc struktura w gałęzi, która
  nigdy się nie wykonuje, i tak rezerwuje miejsce przy każdym wywołaniu; dwadzieścia
  bajtów przelało stos zadania wyświetlacza i zginęło ono przed `tft.init()`.

### Zmienione
- **`SETTINGS_VER` 4 → 5** dla bloku algorytmu 12, **z migracją**. Blok v4 jest
  przyjmowany, jego pola stosowane, a wartości algorytmu 12 zostają domyślne.
  Odrzucenie go odebrałoby działający PID, LC i strefę czasową tylko dlatego, że
  doszedł nowy algorytm.

## [v1.03-rtos] — 2026-08-01

Zbudowane na v1.01. Eksperymenty z v1.02 — przetwornik delta-sigma na PB5 oraz
obsługa rdzenia STM32duino 3.0.0 — nie zostały przeniesione: pierwszy został
zmierzony i nie dawał tego, co obiecywał, drugi zawieszał płytkę na sprzęcie.
v1.01 pozostaje sprawdzoną bazą, z dwoma dodatkami.

### Naprawione
- **Ciepły restart nie uruchamia już od nowa ukończonego survey-in.** Odbiornik
  zachowuje przez `RB` własne zasilanie i własny stan, więc survey ukończony przed
  resetem jest nadal ważny: pozycja, którą ustalił, się nie zmieniła. Firmware
  wcześniej i tak zlecało nowy survey, odrzucając wynik, który powstawał minutami,
  i wypychając moduł z Time Mode na czas powtarzania wykonanej już pracy.
  `gpsdo_gps_init()` odpytuje teraz najpierw TIM-SVIN i pomija start, gdy
  odbiornik zgłasza valid=1 przy active=0 — czyli Time Mode z ukończonym survey za
  sobą. Zgłaszane jako *already in Time Mode from an earlier survey*.

  Wymagało to uczynienia `ubx_poll_svin()` bezpiecznym do wywołania przed
  schedulerem: funkcja oddawała procesor przez `vTaskDelay()` bezwarunkowo, co
  zawiesza system, gdy scheduler jeszcze nie działa. Teraz w takim przypadku używa
  `delay()` — tego samego wzorca, który stosował już czytnik ACK.
- **Płytka nie startowała: brak LED, brak konsoli, nic.** `setup()` zapisuje DAC
  trzy razy — początkowe 127, odczytany PWM i wartość domyślną — zanim wykona się
  `xEventGroupCreate()`. Nowe statystyki korekcji wiszą na ścieżce zapisu DAC, a
  ich bramka czytała `xSysEvents`, które w tym momencie było jeszcze NULL. Podanie
  NULL do `xEventGroupGetBits()` wyzwala `configASSERT` i zatrzymuje procesor,
  więc awaria następowała przed pierwszym mignięciem i nie zostawiała na konsoli
  żadnego śladu. Bramka sprawdza teraz najpierw NULL; te wczesne zapisy to
  komendy, nie korekcje, więc ich wykluczenie jest zarazem bezpieczne i poprawne.

### Dodane
- **`CS` — statystyki korekcji, czyli pętla oceniająca samą siebie.** Algorytm 11
  został zweryfikowany względem wzorca rubidowego na cudzym stanowisku; prawie
  nikt, kto to zbuduje, takiego nie ma, a bez niego zostaje słowo autora i
  wskaźnik locka. Korekcja, którą stosuje pętla, jest błędem, który przed chwilą
  zaobserwowała, więc wielkość tych korekcji mówi, czy dyscyplinowanie działa — a
  odniesieniem jest GPS, więc nie ma nic lepszego do porównania częstotliwości.
  Firmware i tak liczyło te wartości i je wyrzucało.

  Podaje RMS korekcji na oknach ostatnich **100, 1 000, 10 000 i 100 000
  korekcji** — w jednostkach DAC oraz, gdy `CT` zmierzyło nachylenie oscylatora,
  we względnej częstotliwości, wprost porównywalnej z liczbą z ADEV. Także stałe
  odchylenie, niezerowe wtedy, gdy pętla nadąża za rzeczywistym dryfem, a nie za
  szumem.

  Okna liczą korekcje, a nie sekundy, bo tempo korekcji zależy od algorytmu:
  algorytm 11 steruje raz na sekundę, algorytm 10 raz na `LIV`. Oznaczenie ich w
  minutach znaczyłoby co innego przy jednym algorytmie i sześćdziesiąt razy tyle
  przy drugim — ta sama liczba opisywałaby dwa różne przedziały. `CS` mierzy
  rzeczywisty odstęp i wypisuje, ile okna aktualnie obejmują w czasie
  rzeczywistym, żeby czytelnik nie musiał tego przeliczać. Przy jednej korekcji na
  sekundę 100 000 pokrywa około 28 godzin.

  To wagi wykładnicze, nie ostre okna: mniej więcej 63% wagi mieści się w N
  korekcjach, a 95% w 3N. Kosztuje to cztery mnożenia z dodawaniem na korekcję i
  zero pamięci, podczas gdy bufor na 100 000 próbek zająłby większość dostępnego
  RAM-u, odpowiadając na to samo pytanie nie lepiej.

  Liczone wyłącznie przy zalockowanej pętli i bez trwającej kalibracji: rampa
  akwizycji, trzy skoki `CT` i przemiatanie `LC` to komendy, nie korekcje, a jedna
  taka zdominowałaby średnią godzinną długo po tym, jak się skończyła. Algorytmy
  0-9 nie mają stanu locka, na którym dałoby się oprzeć bramkę, więc są wykluczone
  — i `CS` mówi to wprost, zamiast podawać liczbę bez określonego znaczenia.

  **Zastrzeżenie jest w wyjściu, w nagłówku i w README:** mierzy, czy PĘTLA JEST
  USTALONA, a nie czy WYJŚCIE JEST DOBRE. Zaszumiony detektor sprawia, że pętla
  goni szum; korekcje rosną, `CS` wiernie je raportuje, a oscylator był w
  porządku, dopóki pętla go nie popsuła. Nic mierzonego wewnątrz pętli tego nie
  zobaczy.

  Pomysł jest Alana (MIS42N z forum EEVblog), którego własna konstrukcja opiera
  się dokładnie na tym i dlatego nie potrzebuje wzorca wtórnego.
- **`GPSDO_DAC_EXT` — zewnętrzny przetwornik SPI, planowany, niezaimplementowany.**
  Włączenie go daje celowo błąd kompilacji: `dac_ext.cpp` jest zaślepką bez
  wybranego układu. 16-bitowe PWM daje około 50 µV na krok przy 3,3 V, blisko
  2,7×10⁻¹¹ względnie na oscylatorze 5,3 Hz/V; układ 18-bitowy z odniesieniem
  zaprojektowanym do tego zadania osiąga mniej więcej 17 µV, blisko 9×10⁻¹², bez
  opóźnienia filtru w pętli.

  Sprzętowe SPI nie jest potrzebne ani dostępne — SPI1 należy do TFT, a wszystkie
  piny SPI2 w tej obudowie są zajęte — ale DAC zapisuje się raz na sekundę, więc
  programowe kluczowanie kosztuje mikrosekundy. Proponowane piny PB0, PB2, PB4,
  dobrane tak, by ominąć PB6/PB7: te wyglądają na wolne, ale są domyślnymi pinami
  I2C1, które zajmuje `Wire.begin()`, a przetwornik tam rozwaliłby czujniki i
  wyświetlacz zegarowy.

### Zmienione
- **Wszystkie 23 wywołania `analogWrite(PIN_VCTL_PWM, ...)` przechodzą teraz przez
  `gpsdo_dac_write16()`.** Dodanie drugiej ścieżki wyjściowej przez edycję każdego
  z osobna prosiłoby się o przeoczenie jednego, a przeoczone miejsce to najgorszy
  rodzaj błędu tutaj: pętla sterowałaby poprawnie prawie zawsze i przeskakiwała
  przy każdym trafieniu w starą ścieżkę. Dodanie przetwornika sprowadza się teraz
  do wypełnienia jednej funkcji.

## [v1.01-rtos] — 2026-07-29

> **Buduj rdzeniem STM32duino 2.12.0 lub starszym.** Rdzeń 3.0.0 (23 lipca 2026)
> wdraża ArduinoCore-API, co usuwa `ltoa()` i zamienia `HardwareSerial` w
> interfejs abstrakcyjny — obu tu używamy — a co ważniejsze, pozostawia TFT_eSPI
> bez możliwości zainicjalizowania panelu (biały ekran, CLI działa normalnie).
> Dwa pierwsze są drobne i można je uwarunkować wersją; trzeci leży w bibliotece.
> Szczegóły w README.

Wydanie-kamień milowy: łączy gałąź trwałego zapisu we flash ringu z gałęzią
algorytmu 11 (LTIC-Lars). Algorytm 11 opiera się na oryginalnym kontrolerze GPSDO
z ciągłą pętlą PI autorstwa śp. **Larsa Waleniusa**, udostępnionym społeczności
time-nuts; został tutaj rozwinięty o poniższą auto-kalibrację i akwizycję, ku jego
pamięci.

### Dodane
- **Algorytm 11 „LTIC-Lars"** — pojedyncza ciągła pętla PI (bez maszyny stanów
  ACQ/DPLL/LOCK), dyscyplinująca OCXO z fazy sprzętowego TIC. Wybierany przez
  `LA 11`; trend LFQ (prowadzony częstotliwością) / LPH (faza) / LLK (lock).
  Strojony na żywo przez LG/LD/LTC/LFD/LTO/LPL/LPF/LTK/LTR.
- **Auto-kalibracja z CT dla algorytmu 11.** gain domyślnie 0 = auto: pętla
  wyprowadza skalę częstotliwości ze zmierzonego przez CT K (Hz na LSB PWM), tej
  samej stałej, której używa algo 10, więc jedno CT kalibruje też pętlę Larsa.
  Niezerowe LG nadpisuje skalą ręczną.
- **Akwizycja prowadzona częstotliwością** z dominującym, samohamującym członem
  proporcjonalnym, ograniczeniem kroku i anti-windup — zimny start dociąga bez
  ucieczki i bez oscylacji ±2 Hz obserwowanych podczas prac.
- **Pomost przechwytywania fazy picDIV**: gdy częstotliwość jest ustalona, ale
  faza wciąż railed, picDIV zostaje przezbrojony raz, by wprowadzić fazę w okno
  detektora, gdzie gałąź fazowa kończy lock.
- **Tuner: wersjonowany i dopasowany do firmware.** Narzędzia mają teraz
  TOOL_VERSION śledzące wydanie firmware, a tuner przy połączeniu odczytuje wersję
  z płytki: niezgodność jest zgłaszana na pasku stanu i w monitorze, zamiast
  objawiać się dziwnie czytanymi polami. Okno główne otwiera się zmaksymalizowane
  ze splashem na wierzchu, a w Windows konsola powstająca po kliknięciu skryptu
  jest minimalizowana na pasek zadań (tylko gdy należy do tunera — terminal
  otwarty przez operatora zostaje nietknięty).
- **Tuner: zakładka Help i splash startowy.** Tuner zyskał zakładkę Help z pełnym
  wykazem komend pogrupowanym tematycznie oraz trzysekundowy splash animujący dwie
  przesunięte w fazie sinusoidy zbiegające się w jedną — ta sama metafora locka co
  na ekranie startowym TFT (kliknięcie pomija). Przy połączeniu odczytuje też
  wszystkie grupy parametrów (LTIC, FA, PID 3-9, Lars) zamiast dwóch, a algorytm 11
  ma własną zakładkę obok algorytmu 10.
- **Trwały zapis algorytmu 11 we flash ringu.** Wszystkie parametry g_lars są
  zapisywane we flash ringu razem z ustawieniami LTIC (SETTINGS_VER 2); `ES LTIC`
  zapisuje oba. Nigdzie EEPROM — trwałość w 100% oparta na flash ringu.

### Zmienione
- **`LC` ostrzega, gdy uruchomione przed `CT`.** Te komendy nie są niezależne:
  `LC` potrzebuje nachylenia Hz na LSB, które mierzy `CT`, a bez niego przyjmuje
  wartość ogólną. Awaria jest cicha, nie oczywista — jedna płytka zgłosiła
  ns_per_volt 1592,8 przed `CT` i 921,2 po, różnica 1,7×, przy czym nic w pierwszym
  przebiegu tego nie sugerowało. `LC` mówi teraz o tym wprost i mimo to
  kontynuuje, a README podaje kolejność jednoznacznie.
- **Każde ustawienie mówi teraz, czy zostało zapisane.** Preferencje niedotykające
  pętli sterującej — strefa czasowa (`TZ`/`TO`/`LT`), offsety czujników (`PO`/`AO`)
  oraz flagi startu i survey-in (`WU`/`SPL`/`SV`) — zapisują się same, a odpowiedź
  podaje zapisaną grupę. Strojenie pętli pozostaje ręczne, a odpowiedź podaje
  dokładną komendę, która je utrwali, np. `[not saved — run 'ES LTIC' to keep it]`,
  więc grupy nie trzeba zgadywać. `SET_FLAGS` niesie `SAW` i `LRN` razem z flagami
  startu, więc auto-zapis utrwala także je; komunikat wymienia całą grupę, zamiast
  to ukrywać. Wartość odrzucona jest zgłaszana jako taka —
  `[not saved — value out of range; accepted range shown above]` — zamiast
  proponować komendę `ES` dla zmiany, która nie nastąpiła.
- **`LT` jest teraz trwałe.** Komenda była zaimplementowana, ale nie miała pola w
  bloku ustawień, więc wybór UTC/czas lokalny nie przeżywał restartu. Dodane do
  grupy strefy czasowej (SETTINGS_VER 4).
- **`CT` zapisuje teraz wynik automatycznie.** Tak jak `LC`, trzyminutowa
  kalibracja po powodzeniu wpisuje współczynniki do flash ringu, zamiast liczyć na
  to, że operator pamięta o `ES PID`. Zapisywana jest tylko grupa PID, więc
  strojenie pętli prowadzone równolegle pozostaje nietknięte.
- **Etykiety trendu algorytmu 11 przemianowane** na ACQ / PLL / LOCK, zgodnie ze
  słownictwem algorytmu 10, żeby wyświetlacze, CLI i tuner czytały się spójnie.
  Algo 11 pokazuje PLL tam, gdzie algo 10 pokazuje DPLL, co wciąż rozróżnia oba
  w logu.
- **Telemetria Learn pokazuje, co naprawdę steruje daną pętlą**: algo 11 pokazuje
  tryb gain / skalę / filtrowaną fazę, algo 10 swoją maszynę stanów, algo 3-9
  zachowują liczby LRN. qErr zostaje w każdej linii (wspólny dla obu gałęzi LTIC).
- **Komunikat CT** mówi teraz, że stroi algo 3-9 oraz LTIC 10 i 11.
- **Wrappery trwałości przemianowane** eeprom_* → persist_*, by odzwierciedlić, że
  zapis to flash ring, nie EEPROM; nazwy przestają wprowadzać w błąd.

### Naprawione
- **Survey-in nigdy nie przechodził w tło po resecie.** Licznik cierpliwości biegł
  od bootu hosta, ale odbiornik timingowy prowadzi survey nieprzerwanie przez reset
  MCU — ma własne zasilanie i własny stan, i zgłasza własny czas trwania. Każde
  przeprogramowanie zerowało więc licznik i dawało survey kolejny pełny limit na
  pierwszym planie, w nieskończoność. Zaobserwowane na stanowisku: odbiornik
  zgłaszał 4450 s survey przy 7 minutach pracy hosta, a komunikat o timeoucie nie
  padł ani razu. Deadline wygasa teraz, gdy przekroczy limit **którykolwiek** z
  dwóch zegarów, a komunikat mówi który.
- **Algorytm 10 mógł zamarznąć przy zdrowym dociąganiu.** Zabezpieczenie przed
  ucieczką wyzwalało się na samym zablokowanym detektorze plus dużym błędzie
  częstotliwości — a to jest normalny stan zimnego lub odległego OCXO na początku
  akwizycji, i zamrożenie w tym miejscu odcina jedyną drogę powrotu, bo to właśnie
  człon częstotliwościowy wciąga oscylator w okno detektora. Zaobserwowany przebieg
  przeszedł 3855 LSB w trakcie całkowicie zdrowego dociągania DPLL i został
  zamrożony w połowie. Oba zabezpieczenia wymagają teraz dodatkowo, by błąd
  przestał się poprawiać przez kilka cykli (LTIC_RUNAWAY_STALL) — co prawdziwa
  ucieczka przy złej polaryzacji wyzwala, a zdrowa akwizycja nigdy. Próg szyny
  znów pochodzi z kalibracji LC zamiast ze stałych 3,28 V pasujących do jednej płytki.
- **`LIV` było ograniczone do 30 s.** I CLI, i pętla przycinały interwał korekcji
  LOCK do 30, a pętla dla wartości spoza zakresu skakała na 5 s — więc prośba o
  wolniejszą pętlę po cichu dawała najszybszą. Przywrócone 1..600 s, z przycinaniem
  do najbliższej granicy. Miało to natychmiastowe znaczenie: tester porównujący
  LIV 30 z LIV 60 dostałby odrzucone 60.
- **Ustawienia w rzeczywistości nigdy nie były zapisywane.** Nagłówek slotu
  przechowywał długość danych w jednym bajcie, więc wszystko powyżej 255 B się
  zawijało: 324-bajtowy blok ustawień zapisywał się jako 68. Same dane trafiały do
  flash poprawnie, a CRC je obejmowało, więc nic nie wyglądało źle — ale każdy
  odczyt zwracał obciętą długość, zostawiając ogon wczytanego bloku jako to, co
  akurat leżało na stosie. Stamtąd wzięło się dziwne `temp_coeff=-1`, a gdy
  długość zaczęła być sprawdzana dokładnie, odczyt zaczął odrzucać rekord i płytka
  wstawała na wartościach domyślnych. Pole długości jest teraz 16-bitowe (nagłówek
  slotu 4 B → 6 B, dane 506 B → 504 B), a magic pierścienia podbity, żeby starszy
  pierścień sam się przeformatował, zamiast dekodować się jako śmieć. Dotyczyło to
  gałęzi flash-ring od początku — blok GML-a miał już 292 B, też ponad limit.
- **Przepełnienie stosu przy zapisie do flash ringu.** Zapis ustawień potrzebuje
  około 1,4 KB stosu — `fr_write()` buduje 512-bajtowy obraz slotu plus
  512-bajtową kopię do weryfikacji, a `settings_store` dokłada blok ~324 B — a
  zadanie CLI miało 1 KB, zadanie kontrolne 1,5 KB. Zadanie CLI wychodziło poza
  swój stos i nadpisywało sąsiada: płytka drukowała potwierdzenie zapisu i
  zawieszała się z zamrożonym wyświetlaczem. Oba stosy podniesione z zapasem
  (CLI 1 KB → 3 KB, kontrolne 1,5 KB → 3,25 KB; 4 KB RAM więcej ze 128 KB).
  Zagrożenie istniało przed pracą nad auto-zapisem — `ES` był równie narażony —
  ale auto-zapis sprawił, że łatwo je było trafić.
- **`EW` podawał zły sektor flash.** Pierścień od zawsze mieszka w sektorze 7
  (0x08060000, ostatni sektor, żeby firmware zachowało maksymalną ciągłą
  przestrzeń poniżej), ale komunikat `EW` miał na sztywno „sector 6, 0x08040000" —
  jedyne miejsce, w które zagląda operator, było jedynym, które kłamało. Komunikat
  czyta teraz adres z implementacji przez nowe funkcje `flash_ring_sector_no()` /
  `flash_ring_base_addr()`, więc nie może się już rozjechać. Dokumenty bring-up
  miały te same nieaktualne liczby i zostały poprawione w trzech językach: limit
  firmware to 393216 B (384 KB), nie 262144 B, a zakres kasowania J-Link dla
  wyczyszczenia pierścienia to 0x08060000-0x0807FFFF, a nie zakres sektora 6,
  który zostawiłby pierścień nietknięty.
- **LC nie wyrzuca już własnego dorobku.** Pętla zerująca tempo kończyła po trzech
  próbach i, jeśli nie trafiła jeszcze w pasmo akceptacji, wracała do
  `saved_pwm + offset` — co zakłada, że zapisany PWM leży w punkcie locka.
  Uruchomiona, zanim oscylator jest blisko 10 MHz, to założenie jest fałszywe:
  obserwowany przebieg zbiegał -244, -57, -16 ns/s (krok od pasma), po czym to
  odrzucał i próbkował przy PWM dającym -244 ns/s, gdzie faza przebiega całe okno
  detektora między publikacjami. Każde zbrojenie picDIV lądowało na szynie i
  kalibracja przerywała. Pętla ma teraz sześć prób, a gdy się wyczerpią, zachowuje
  wysterowany PWM zamiast wracać do początku.
- **Przywrócone FA / FAD / FAL.** Okno uśredniania członu tłumiącego per stan
  (i człon `damp_e_freq`, który zasila w algorytmie 10) istniało w v0.97, ale nie
  w gałęzi flash-ring, więc przepadło przy merge'u. Przywrócone i zapisywane teraz
  we flash ringu zamiast w EEPROM.
- **Rekordy ustawień mają sprawdzaną długość.** `settings_recall` i
  `settings_save_partial` przyjmowały dowolny rekord od dwóch bajtów wzwyż do
  bloku na stosie, więc rekord krótszy od bieżącej struktury zostawiał ogon jako
  śmieć ze stosu — a zapis częściowy wpisywał ten śmieć z powrotem. Oba zerują
  teraz blok i wymagają dokładnego rozmiaru.
- **settings_store.cpp kompiluje się teraz.** Czytał trzy globalne, których nie
  widział — g_pressure_offset, g_altitude_offset (definiowane w gpsdo_control.cpp,
  bez własnego nagłówka) oraz g_qerr_enable (deklarowany w ubx_timtp.h, który nie
  był zaincludowany). Dodano include i dwa lokalne externy, zgodnie ze wzorcem
  używanym w reszcie projektu.
- **Zaimplementowana komenda LT.** Pomoc od zawsze dokumentowała `LT 0|1`, a
  ścieżki wyświetlania i raportów od zawsze czytały g_show_local_time, ale handler
  w CLI nigdy nie powstał — więc komenda po cichu nic nie robiła. Teraz przełącza
  i raportuje UTC / czas lokalny, tak jak obiecuje pomoc.
- **dph na serialu zgadza się teraz z panelem.** Wiersz TFT odejmował sawtooth
  odbiornika, a raport szeregowy nie — więc ten sam moment czytał się inaczej na
  obu, o cały sawtooth (~±10 ns na LEA-6T, więcej na M8T). Ścieżka szeregowa też
  go teraz odejmuje, zgodnie z tym, co jej własny komentarz już deklarował.
- **CR (zimny restart) naprawdę czyści teraz ring.** persist_erase() woła nowy
  flash_ring_wipe(), który fizycznie eraseuje i reformatuje sektor ringu, więc
  zimny restart faktycznie wraca do domyślnych, zamiast tylko oznaczać stan jako
  nieaktualny.

## [v0.95-rtos] — 2026-07-16

### Dodane
- **Strefy czasowe z DST, w całym świecie.** `TZ Adelaide` wystarczy, żeby
  zegar był poprawny — łącznie z offsetem pół godziny i DST półkuli
  południowej. Same nazwy miast są akceptowane: są unikalne w całej bazie
  IANA, więc region jest opcjonalny (`TZ Australia/Adelaide` też działa),
  a wielkość liter nie ma znaczenia.

  Regułę można też wpisać w całości: `TZ ACST-9:30ACDT,M10.1.0,M4.1.0/3`. Ta
  forma ma znaczenie, gdy rząd zmieni przepisy, a firmware jeszcze o tym nie
  wie — użytkownik poprawi to z CLI, zamiast czekać na wydanie.

  Wbudowane 407 stref i 88 reguł, generowane z systemowej tzdata przez
  `tools/gen_tz_table.py`. Pełna baza IANA to ~2 MB, czterokrotność całego
  flasha tego MCU, a jej prawdziwa wartość polega na aktualizacjach kilka razy
  w roku — z czego GPSDO bez internetu i tak nie skorzysta. String POSIX TZ, do
  którego sprowadza się każda strefa, ma 4–44 bajty i niesie to samo
  zachowanie na dziś, więc to on jest przechowywany. Koszt: ~7 KB flasha.
- **`H TZ`** — pierwsza strona pomocy dla pojedynczej komendy. `TZ` przyjmuje
  dwa całkiem różne argumenty i ta różnica ma znaczenie, więc dostaje własną
  stronę zamiast ciasnej linijki w głównej liście.
- **`TO` przyjmuje teraz minuty**: `TO 9:30`, `TO -3:30`, `TO 5:45`. Same
  godziny nadal działają.
- **Vcc na ekranie (480×320).** Prośba Dana Wieringa, obok Vdd. Szyna 5 V była
  już mierzona, ale nie miała gdzie trafić — każda komórka w obu kolumnach jest
  zajęta. Komórka `Alt` ma ~134 px luzu za wysokością, więc oddaje prawą połowę,
  a pola zostały przy okazji przegrupowane: `qErr` idzie w górę obok `Alt` (to
  raport odbiornika o własnym 1PPS, więc jego miejsce jest przy danych z fixa),
  a `Vcc` zajmuje miejsce zwolnione przez `qErr` obok `Vdd` — napięcia razem.
  `Vdd` odzyskuje drugie miejsce po przecinku, które oddawało wyłącznie po to,
  by zrobić miejsce dla `qErr`.

  Oba tylko na 480. Na 320 `Alt` i `qErr` chcą ~168 px, a komórka ma 148, więc
  ten panel zostaje przy starym układzie.

### Naprawione
- **Zgłoszone przez Dana Wieringa: auto-strefa nie łapała DST w Australii
  Południowej.** Dwa osobne błędy, z czego widoczny był jeden. `TO A` zgaduje
  strefę z długości geograficznej i stosuje europejską regułę DST, więc poza
  Europą nie dawało DST w ogóle — to był zgłoszony objaw. Ale zwracało też
  całe godziny, a Adelaide to UTC+9:30, więc zegar był o pół godziny obok
  nawet zimą, przy naprawionym DST. Indie (+5:30), Nepal (+5:45), Nowa
  Fundlandia (−3:30) i Chatham (+12:45) miały ten sam cichy błąd.

  `TZ <strefa>` rozwiązuje oba. `TO A` zostaje bez zmian — jest poprawne
  w większości Europy i nie wymaga konfiguracji — ale teraz mówi wprost,
  czego nie potrafi.
- **Częstotliwość skakała na boki na panelu 320×240.** v0.94 usunęło szerokość
  pola `dtostrf` w przekonaniu, że font monospace i tak trzyma cyfry
  w kolumnach. Trzyma — ale string, który traci znak, wciąż jest centrowany na
  nowo, co przesuwa każdy glif o pół znaku. To szerokość pola sprawia, że
  *string* ma stałą długość, i wróciła — jest tam od v0.89. Panel 480×320
  nietknięty: kotwiczy odczyt prawą krawędzią, co jest zweryfikowane na
  sprzęcie.
- **Boczne szyny znikały przy częstotliwości.** Sprite częstotliwości czyści
  całe swoje pasmo przed rysowaniem, a rysował tylko linię separatora nad sobą
  — więc szyny z początkowego layoutu były zamazywane z tego pasma przy
  pierwszej aktualizacji i ramka wyglądała, jakby nie dochodziła do linii
  nagłówka. Sprite niesie teraz także szyny. Oba panele.
- **Vdd pokazywało się wyłącznie przy zbudowanym LTIC.** Siedzi w wierszu fazy,
  a cały wiersz był pod `#ifdef GPSDO_LTIC` — więc płytka bez TIC nie widziała
  własnej szyny 3.3 V, bez lepszego powodu niż to, gdzie akurat napisano to pole.
  Szyny są teraz poza tym warunkiem: `Vcc` i `Vdd` pokazują się niezależnie od
  sprzętu, a pod LTIC zostaje samo pole fazy — bez niego lewa połowa wiersza jest
  po prostu pusta. `qErr` też zostaje pod warunkiem, bo pojawia się wyłącznie
  przy algo 10.
- **`CT` pokazywało „Tune 0s" przez cały przebieg.** Ustawiało flagi
  kalibracji, ale nigdy nie zasiewało licznika, w przeciwieństwie do `C` i
  `LC`. Trzy punkty po `OCXO_CALIB_SECS`, czyli 185 s.
- **`qErr` przesuwało się na panelu 480×320.** Przy kotwicy z lewej pole rosło
  w prawo wraz ze zmianą szerokości wartości i „ns" wędrowało tam i z powrotem.
  Zakotwiczenie całego stringu z prawej naprawiło jednostkę, ale w zamian
  ciągnęło etykietę `qErr:` razem z cyframi. Etykieta i wartość to teraz dwa
  osobne pola: etykieta dosunięta do lewej krawędzi slotu, wartość trzyma
  kotwicę z prawej, żeby jednostka stała, a zmienia się wyłącznie odstęp między
  nimi — czyli tak, jak od zawsze zachowuje się wiersz `Vph`/`dph`.

### Zmienione
- **`dph` podawało pewną siebie liczbę długo po tym, jak detektor przestał
  mierzyć.** `ns_per_volt` to nachylenie LOKALNE, odczytywane wokół kotwicy,
  którą LC stawia na 0.632·Vsat; sama rampa to `V = Vsat·(1 − e^(−φ/τ))`, więc z
  dala od kotwicy krzywa płaszczeje i liniowy odczyt zaniża fazę. Powyżej Vsat
  nie ma już żadnego odczytu — impuls stopu minął okno i kondensator ładuje się
  dalej do szyny zasilania. Wyświetlacz zameldował ten stan dwukrotnie jako
  niewzruszone „+1561 ns" i za każdym razem kosztowało to pomiar, zanim ktoś
  spojrzał na napięcie obok. `dph` pokazuje teraz `ovf` poza pasmem 15–85% Vsat,
  a stojące obok `Vph` mówi, którym końcem wyjechało.

  Vsat nie jest nigdzie zapisywane — LC je dopasowuje, stawia kotwicę i wyrzuca
  — ale kotwica jest z definicji na 0.632·Vsat, więc `zero_offset` je odzyskuje.
  Na tej płytce wychodzi 2.91 V, co zgadza się z 2.93 V, które komentarze
  kalibracji podają dla niej.

  Osobno warte odnotowania: własna ochrona pętli przed ucieczką (`railed_now`)
  testuje twardo wpisane 3.28 V. Przy detektorze nasycającym się koło 2.9 V nie
  ma prawa zadziałać, więc pasmo między ~2.9 a 3.28 V jest nasycone z punktu
  widzenia sprzętu i zdrowe z punktu widzenia pętli. Tego ta zmiana nie rusza.
- **`dph` na ekranie nigdy nie miało odjętego sawtootha.** Wyświetlacz liczył
  fazę własną ścieżką — napięcie, środek, `ns_per_volt` — i pomijał korektę,
  którą pętla stosuje w `ltic_phase_error_ns()`. Czyli algo 10 sterowało na
  fazie skorygowanej, a pokazywało nieskorygowaną; różnica to cały sawtooth
  odbiornika: zmierzone na sprzęcie ~14 ns rozrzutu 1σ na skądinąd płaskim
  odczycie. Teraz wyświetlacz też go odejmuje.

  Najbardziej znaczy to poza algo 10. Algorytmy 3–9 nigdy nie wołają fazowej
  ścieżki pętli, więc `dph` było ich jedynym widokiem na prawdziwą fazę — i to
  tym zaszumionym. A to właśnie tam TIC jest cenny: rozróżnia odchyłkę
  częstotliwości na poziomie ~5e-11 w 100 s, podczas gdy licznik cykli potrzebuje
  1000 s na 1e-10. Pole `qErr` i pozycja `qErr=` w raporcie szeregowym też nie są
  już bramkowane na algo 10: to, co zostało odjęte, musi być widoczne, inaczej
  liczby nie da się potem sprawdzić.
- **Każda kolumna ma jedną linię wyrównania z prawej (480×320).** Lewa kończy
  się tam, gdzie „hPa" w wierszu BMP, prawa tam, gdzie „ns" w wierszu fazy — bo
  to najszersze i najstabilniejsze stringi w każdej z nich. `Vct`, `% rH` i prąd
  z INA są teraz zakotwiczone do tych linii, zamiast każdy kończyć się tam, gdzie
  akurat wyczerpie się jego tekst — przez co krawędzie kolumn były trzema
  rozjazdami po kilka pikseli. `PWM:` i `INA:` zachowują etykiety przy lewej
  krawędzi kolumny, więc oba wiersze musiały stać się dwoma polami zamiast
  jednym stringiem.

  Linie są mierzone przez `textWidth()` przy pierwszym użyciu, a nie wpisane jako
  stałe: każda wartość w tych wierszach ma stałą szerokość, więc każda krawędź
  jest stałą — ale stałą wynikającą z metryk glifów fontu, a tego nie warto
  zgadywać. Paddingi też wyprowadzone z pomiaru, więc pola kafelkują wiersz
  niezależnie od tego, ile wyjdzie.
- **Wiersze sensorów grupowane kolumnami, nie sensorami (480×320).** BMP i AHT
  wypełniają teraz lewą kolumnę, a pola elektryczne prawą — odczyt fazy na
  górze, szyny zasilania bezpośrednio pod nim. `AHT` i `Vph`/`dph` zamieniły się
  miejscami. Przeniesienie pola fazy do węższej prawej kolumny kosztowało jedną
  spację przed `dph:`; jego padding jest wymierzony na najszerszy możliwy string,
  nie na kolumnę, więc kurcząca się wartość nie zostawi ogona.
- **`dPh:` to teraz `dph:`**, pasujące do `Vph:` obok. Zmienione na TFT i w
  raporcie szeregowym razem — te etykiety miały się zgadzać, więc zmiana tylko
  jednej pogorszyłaby sprawę, zamiast ją poprawić.
- **Powiadomienie survey-in przeniesione z belki górnej na belkę statusu.**
  Pulsowało między nazwą programu a zegarem i na panelu 480 nie pojawiało się
  w ogóle — usterka, która przetrwała każde czytanie kodu i kilka pewnych
  siebie błędnych diagnoz. Zamiast polować dalej, powiadomienie dopisuje się
  teraz do tego, co belka statusu i tak mówi: `DISCIPLINED  FIX OK SURVEY`,
  albo `SV` na 320, gdzie pełne słowo wyszłoby poza pasek.

  Belka jest lepszym miejscem niezależnie od błędu. Przemalowuje całe swoje tło
  przed rysowaniem, więc słowa nie utnie padding sąsiada — czego slot w nagłówku
  nie potrafił zagwarantować; jest jedynym miejscem na ekranie, gdzie oko i tak
  szuka stanu; a stojąc tam nie musi migać, żeby je zauważyć, więc pulsowanie
  też zniknęło.

  Warunek bez zmian, bo nigdy nie był problemem: napis pojawia się po timeoucie
  monitora survey-in, gdy odbiornik nadal go prowadzi, i gaśnie z chwilą
  wejścia w Time Mode.
- `g_time_offset` (int8, godziny) to teraz `g_time_offset_min` (int16, minuty),
  z jednym zapisującym. `g_tz_auto` (bool) stało się `g_tz_mode` (ręczny /
  auto-EU / POSIX): każda komenda ustawia tryb, więc nie ma pół-stanu, w
  którym jeden mechanizm jest skonfigurowany, a inny go po cichu nadpisuje.

### EEPROM
- Blok stref przeniesiony na `[234..284]`: tryb, ręczny offset w minutach i
  reguła POSIX jako tekst.
- **Istniejące ustawienia są migrowane automatycznie — bez factory reset.**
  EEPROM sprzed v0.95 nigdy nie był zapisywany powyżej `[233]`, więc blok
  odczytuje się jako skasowany flash; to jest znacznik, a stara para
  `[9]`/`[142]` jest przenoszona (godziny × 60 to dokładnie to, co znaczyła).
  Sygnatura bez zmian.
- **Ale powrót do starszej wersji jest jednokierunkowy.** Starsze bajty `[9]`
  i `[142]` są nadal zapisywane, więc v0.94 wgrane na płytkę z v0.95 odczyta
  sensowny offset w całych godzinach — ale reguły `TZ` nie da się tam
  zapisać i zostanie utracona.

### Dokumentacja
- Przeniesiona do [`doc/`](../doc/), pliki angielskie dostały sufiks `_EN`, żeby
  wszystkie trzy języki nazywały się tak samo. Główny `README.md` to teraz krótki
  indeks — GitHub renderuje go na stronie projektu i stamtąd prowadzi do `doc/`.
- Przewodniki uruchomienia flash-ringu były sierotami: nic do nich nie linkowało
  i one do niczego. Mają teraz tę samą nawigację językową co reszta.
- Ich liczba budżetu flasha była przestarzała o pięć wersji (~170 KB przy v0.90).
  Teraz mówi 216976 B (212 KB) przy v0.95, ~44 KB zapasu poniżej ringu na
  0x08040000 — zmierzone, nie szacowane. Ta liczba jest całym sensem tego
  sprawdzenia, więc nie powinna gnić. Przewodnik ostrzega też, że procent z IDE
  liczy od pełnych 512 KB i wygląda dużo różowiej niż prawda: „41%" to w
  rzeczywistości 83% tego, co firmware może wykorzystać.

### Uwagi
- `tz_table.h` jest generowany. Uruchom `tools/gen_tz_table.py` ponownie przy
  aktualizacji tzdata; reguła zapisana w EEPROM przetrwa regenerację.
- Africa/Casablanca i Africa/El_Aaiun degradują się do offsetu standardowego
  z ostrzeżeniem: ich DST zależy od ramadanu, czego format POSIX w ogóle nie
  wyraża. Każda inna strefa w obecnej tzdata rozstrzyga się w pełni.

---

## [v0.94-rtos] — 2026-07-15

### Naprawione
- **Pole częstotliwości na 320×240 wciąż rysowało się fontami GFX.** v0.93
  cofnęło mały panel na klasyczne fonty, ale poprawka trafiła tylko do ścieżki
  rysowania bezpośredniego — a ta nigdy się nie wykonuje, bo sprity tworzone są
  na *obu* panelach, nie tylko na 480×320. Gałąź sprite'owa miała nadal
  zaszyte `GF_FREQ`/`GF_STATUS`, więc odczyt (i `no signal`) dalej renderował
  się we FreeMono. Teraz idzie przez te same makra `TFT_FONT_*` co reszta.
- **Częstotliwość drgała w bok na panelu 480×320.** Odczyt był centrowany, więc
  każda zmiana długości napisu ruszała wszystkimi znakami: okno uśredniania
  zmienia liczbę miejsc po przecinku, a 10000000.0000 → 9999999.9999 gubi cały
  znak, przy czym centrowanie rozkładało tę różnicę na oba końce. Odczyt jest
  teraz zakotwiczony prawą krawędzią na x=464 — dobrane tak, by nominalne
  `10000000.0000 Hz` (16 znaków × 28 px stałej szerokości = 448 px) nadal
  wypadało idealnie na środku, z 16 px powietrza po obu stronach. „Hz" już się
  nie rusza; ruszają się tylko cyfry. Komunikaty statusu zostają wycentrowane —
  używają proporcjonalnego fontu, gdzie nie ma kolumn do wyrównania.

### Zmienione
- **Ramka jest biała na obu panelach.** Poza ujednoliceniem z dużym panelem, to
  właśnie pozwala 1-bitowemu sprite'owi danych nieść ramkę samodzielnie: ten
  sprite ma dokładnie dwa kolory (biel i tło), więc granatowej ramki nie dało
  się w nim narysować i trzeba ją było domalowywać na panelu po każdym pushu.
  Biel oznacza, że ramka i tekst wychodzą teraz razem, jednym atomowym
  transferem, na obu rozmiarach. Separator pod nagłówkiem przeniósł się do
  sprite'a częstotliwości z tego samego powodu (jego paleta 4-bit ma już biel).
- **Splash nie używa już fontów GFX.** Był ostatnim bastionem GFX na małym
  panelu, co oznaczało, że każdy przechodzący z v0.92 musiał dodać
  `LOAD_GFXFF` do `User_Setup.h` albo patrzeć, jak podtytuł zwija się do samego
  „p" — zagadkowa awaria za kosmetyczny zysk. Podtytuł używa teraz klasycznego
  fontu 4 (który ma pełny alfabet — to fonty 6/8 są bez liter), a kredyty fontu
  1 na obu panelach. **Wersja 320×240 potrzebuje teraz tylko `LOAD_GLCD`,
  `LOAD_FONT2` i `LOAD_FONT4`**; `LOAD_GFXFF` jest wymagane wyłącznie dla
  480×320. Osierocone makra `GF_TITLE`/`GF_SUB`/`GF_CREDIT` i martwa gałąź 320
  w bloku `GF_*` znikają razem z tym.
- Napisy paska statusu siedzą 2 px niżej na panelu 320×240. Są pisane samymi
  kapitalikami, więc pole na dolne wydłużenia glifów jest puste, a centrowanie
  geometryczne czyta się jako za wysokie; przesunięcie centruje to, co oko
  faktycznie widzi. Panel 480×320 bez zmian.
- Podbicie wersji do v0.94-rtos, wraz z nagłówkami plików (które wciąż mówiły
  v0.92).

## [v0.93-rtos] — 2026-07-14

### Naprawione
- **Odliczanie szło wolniej niż zegar.** Rozgrzewka OCXO i kalibracje mierzyły
  sekundy przez `vTaskDelay(1000)`, które śpi *przez* sekundę, a nie *do*
  następnej — więc odczyty ADC, wydruki na serial i każde wywłaszczenie
  doliczały się na wierzch, a pokazywana liczba zostawała w tyle za realnym
  czasem (tym bardziej, im bardziej obciążony system). Oba używają teraz
  `vTaskDelayUntil`, które pochłania czas pracy i utrzymuje każdy krok jako
  prawdziwą sekundę. Licznik kalibracji zatrzymywał się też na 1 zamiast dojść
  do 0.
- **Survey-in, który przeżyje okno monitorowania, nie jest już niewidoczny.**
  Gdy zadziała zabezpieczający timeout, firmware przestaje odpytywać, ale
  odbiornik dalej prowadzi survey („continuing anyway" w logu) — a skoro pasmo
  częstotliwości wraca do pokazywania częstotliwości, nic na ekranie o tym nie
  mówiło. Wolno pulsujący `SURVEY` siedzi teraz w nagłówku między wersją a
  zegarem i gaśnie sam, gdy odbiornik zgłosi Time Mode (`HDOP: TIME`), co jest
  prawdziwym sygnałem zakończenia survey-in.
- **`qErr` zostawiał resztki znaków na panelu ILI9488** (widoczne jako
  `qErr: -1.6 nsss`). Padding tekstu pola wynosił 55 jednostek autorskich
  (~82 px), a najszersza wartość `qErr: -21.3ns` potrzebuje ~104 px w FreeSans
  9pt — TFT_eSPI przemalowuje tło tylko pod paddingiem, więc ogon poprzedniego,
  dłuższego napisu zostawał. Padding poszerzony do 75 jednostek (~112 px), co
  pokrywa tekst i nadal omija zakotwiczone do prawej pole `Vdd`.
- **Vctl / Vcc / Vdd pokazywały 0,000 V przez całą rozgrzewkę OCXO.** Te średnie
  ADC są próbkowane w głównej pętli zadania sterowania, ale `do_warmup()`
  wykonuje się *przed* wejściem do tej pętli i tylko spał — więc nic ich nie
  wypełniało. Odliczanie rozgrzewki próbkuje teraz te same trzy kanały co
  sekundę, tak jak już robi `wait_secs_pwm()` podczas kalibracji.
- **Odczyt częstotliwości siedział na prawo od środka i skakał w bok.** Wartość
  była formatowana przez `dtostrf(..., 14, ...)`, dopełniając ją z lewej do 14
  znaków; `MC_DATUM` centrował potem napis *razem* z tymi niewidocznymi
  spacjami, więc widoczne cyfry siedziały ~40 px na prawo od środka — a ponieważ
  liczba spacji zmienia się z oknem uśredniania (1–4), odczyt przesuwał się przy
  każdej zmianie precyzji. Szerokość pola usunięta: `GF_FREQ` to FreeMonoBold,
  który i tak trzyma cyfry w stałych kolumnach, więc dopełnianie nic nie dawało.
  Wraz z nim znika obejście z końcową spacją na panelu 480.

### Zmienione
- **Panele 320×240 wracają do klasycznych fontów na ekranie roboczym.** v0.92
  przeniosło wszystkie panele na fonty GFX; na 480×320 to była wyraźna wygrana,
  ale przy 320×240 kroje proporcjonalne są za szerokie dla layoutu ułożonego
  wokół fontów numerycznych — wartości uciekały poza swoje kolumny do sąsiedniej,
  a środkowy separator przecinał to, co wystawało. Nie było też mniejszego kroju
  do odwrotu (FreeSans zaczyna się na 9 pt; niżej jest tylko nieczytelny
  TomThumb 3×5). Mały panel używa teraz fontu 2 na nagłówek i siatkę, fontu 4 na
  pasek statusu i fontu 1 ×3 (stała szerokość) na częstotliwość, a splash
  zostaje na GFX na obu panelach. Makra `TFT_FONT_*` wybierają to na etapie
  kompilacji — nadal jeden layout, nie dwa. Środkowy separator kolumn jest teraz
  tylko na 480 (na 320 nie ma na niego miejsca), a ramka wraca na małym panelu
  do granatu.
- **Żywe obszary wyświetlacza są podwójnie buforowane jako sprity.** Nagłówek,
  pasmo częstotliwości i obszar danych są renderowane do `TFT_eSprite` w RAM i
  wypychane na panel jednym ciągłym transferem SPI, zamiast kasowania panelu
  przez `setTextPadding` i rysowania na wierzchu. To kasowanie-a-potem-rysowanie
  było widoczne jako migotanie raz na sekundę, zwłaszcza na panelu 480×320,
  gdzie czyści 2,4× więcej pikseli. Palety trzymają koszt nisko (4-bit
  nagłówek/freq, 1-bit dane; ~25 KB łącznie na dużym panelu). Jeśli
  `createSprite()` zawiedzie przy pofragmentowanej stercie, każde pasmo wraca do
  rysowania bezpośredniego — migotanie wraca, ale nic się nie psuje; log
  startowy mówi, która ścieżka działa.
- **Komunikaty statusu piszą się teraz pełnymi słowami i nazywają, która
  kalibracja trwa.** `WARMUP 285s` → `OCXO warmup 285s`, `SVIN 120s 5m` →
  `Survey 120s +/-5m`, a niejednoznaczne `CAL 245s` staje się `Calibrate`,
  `Tune` lub `LTIC cal` — C, CT i LC trwają bardzo różnie, więc samo odliczanie
  niewiele mówiło operatorowi. Oba panele. Uwaga: obie liczby są innego rodzaju:
  rozgrzewka i kalibracje odliczają w dół, a survey-in liczy w górę (odbiornik
  raportuje czas, który minął, a zakończenie zależy też od dokładności, więc
  liczba „pozostało" byłaby zgadywanką).
- **`SPI_FREQUENCY 40000000` jest teraz udokumentowanym ustawieniem** (w README
  było 27 MHz, podczas gdy `gpsdo_config.h` mówił już 40). SPI1 w F411 kończy
  się na 50 MHz, więc 40 zostawia zapas; ma to znaczenie głównie na panelu
  480×320, gdzie push sprite'a to jeden transfer, którego czas skaluje się z
  zegarem. Zejdź do 27 MHz, jeśli długie przewody połączeniowe zaczną
  bruździć.
- **Wiersze kredytów na splashu dostały większą interlinię na panelu 480×320.**
  Autorski odstęp 12 jednostek skaluje się tam do zaledwie ~16 px, a kredyty to
  FreeSans 9pt (~13 px wysokości), więc oba wiersze zlewały się optycznie. Duży
  panel używa teraz odstępu 16 jednostek (~21 px, interlinia ~1,6×); panel
  320×240 zostaje przy 12, co pasuje do jego fontu 6×8.
- `dPh:` i `qErr:` na ILI9488 tracą spację przed jednostką `ns`.
- Podbicie wersji do v0.93-rtos.

## [v0.92-rtos] — 2026-07-12


### Zmienione
- **Uproszczony splash i dopracowane proporcje ekranu pracy.** Duży zielony
  tytuł „GPSDO" usunięto ze splashu startowego; podtytuł „GPS Disciplined OCXO"
  jest teraz podniesiony na górę, jak w oryginalnym układzie 320×240. Na ekranie
  pracy tekst nagłówka zmniejszono do rozmiaru fontu danych, dolny pasek statusu
  zmniejszono o połowę wysokości z mniejszym fontem statusu, a odzyskane miejsce
  poszło na szersze odstępy między wierszami telemetrii (row pitch 17→20
  authored), żeby siatka „oddychała". Font danych zostaje FreeSans 9pt.
- **Cały tekst TFT przeniesiony na fonty Adafruit GFX (GFXFF).** Nagłówek, duży
  odczyt częstotliwości, siatka danych, pasek statusu oraz tytuł/podtytuł
  splashu rysowane są teraz fontami FreeSans / FreeMono zamiast klasycznych
  numerycznych fontów GLCD. Naprawia to długotrwały błąd, w którym napisy
  literowe rysowane fontami numerycznymi (6/8, zawierającymi tylko
  `0-9 . : - a p m`) zwijały się do pojedynczego znaku — najwyraźniej podtytuł
  splashu „GPS Disciplined OCXO" renderowany jako samo „p" oraz pusty napis na
  kolorowym pasku statusu. Warstwa fontów per rola i per panel (`GF_DATA` /
  `GF_HEAD` / `GF_STATUS` / `GF_TITLE` / `GF_SUB` / `GF_FREQ` w
  `gpsdo_config.h`) dobiera automatycznie FreeSans 9/12 pt, FreeSansBold
  12/18/24 pt oraz FreeMonoBold 18/24 pt dla paneli 320×240 i 480×320, więc ten
  sam kod układu obsługuje oba. Duża częstotliwość używa FreeMonoBold, aby jej
  cyfry pozostały stałej szerokości i nie przeskakiwały przy zmianie wartości.
- **Wymaga `#define LOAD_GFXFF` w `User_Setup.h`** (patrz README). Stare linie
  `LOAD_FONT2/4/6/8` nie są już potrzebne; `LOAD_GLCD` pozostaje tylko dla dwóch
  drobnych linii autorskich na splashu.
- **Układ ekranu pracy przeliczony geometrycznie pod 480×320.** Granice pasów
  (częstotliwość, siatka, sensory, status) przeliczone tak, aby wyższe wiersze
  fontów proporcjonalnych nigdy nie przecinały separatora na żadnym panelu, obie
  kolumny danych wypełniają pełną szerokość z delikatnym separatorem środkowym,
  a pasek statusu wypełnia cały pas do dołu ekranu (brak martwego paska koloru
  pod napisem). Wartości siatki są kotwiczone prawym datum, więc zmienne
  szerokości pozostają przypięte zamiast dryfować. Zweryfikowane na panelu
  ILI9486 480×320.

### Naprawione
- **Usunięto nieaktualne „jeszcze nie zaimplementowane / phase A" z CLI i
  telemetrii.** `LA` z błędną wartością pisało „0..9 (10=LTIC, not yet
  available)", `LL` drukowało „(loop not yet implemented — phase A)", a
  pomoc/komentarze wciąż opisywały algo 10 jako niezaimplementowany podgląd.
  Algorytm 10 dyscyplinuje pętlę od wielu wydań; wszystkie te miejsca opisują
  teraz działającą 3-stopniową pętlę fazową ACQ→DPLL→LOCK. (`Vdd:` na TFT
  dostało też spację przed wartością, dla spójności z innymi etykietami.)
- **Animacje spinnera LED (rozgrzewka / survey-in / kalibracja) chodziły ~5× za
  wolno i skakały.** Task wyświetlacza budzi się na powiadomienie PPS 1 Hz, ale
  spinnery zmieniają klatkę co 200 ms — więc przy budzeniu co 1100 ms
  przesuwały się tylko raz na sekundę. Task budzi się teraz ~co 150 ms gdy
  animacja jest aktywna (a poza tym trzyma wolne 1100 ms, bo zegar i tak zmienia
  się raz na sekundę). Żeby szybsze budzenie nie przepychało identycznych
  segmentów po programowo bit-bangowanym TM1637 (~5–8 ms na zapis), mały cache
  zapisu (`tm_set`) pomija transfer gdy wzorzec się nie zmienił. To poprawka
  szeregowania/cache — bez DMA; DMA zostaje osobnym przyszłym krokiem dla ścieżki
  TFT SPI.
- **Podniesiona klamra tłumienia nie działała po ponownym wgraniu — lock
  oscylował LOCK↔DPLL.** Mnożnik tłumienia jest zapisywany w flash ring (dane
  żywe) i odtwarzany przy starcie. Flash zapisany przez build ze starą klamrą
  0,30 odtwarzał więc damp = 0,30 nawet po podniesieniu klamry do 0,45, a
  ponieważ damp adaptuje się tylko na przejściach cyklu granicznego, zostawał
  tam zablokowany — pętla działała z 30% mocą korekcji, faza rosła ponad próg
  locka, a pętla skakała LOCK↔DPLL co ~30 s (widoczne na sprzęcie). Odtworzony
  damp jest teraz clampowany do bieżącego legalnego zakresu przy wczytaniu
  (flash ring i EEPROM), więc ponowne wgranie działa od razu. Klamry damp
  przeniesione do wspólnego nagłówka, by pamięć i learner się zgadzały.
- **TFT pokazuje teraz qErr, a faza dostała etykietę `dPh:`.** Na algo 10 z
  aktywnym SAW prawe pole wiersza czujników zaczyna od piły odbiornika
  `qErr:…ns`, a dalej `Vdd:` skrócone do 1 miejsca. Oba są rysowane osobno — qErr do lewej,
  Vdd zakotwiczone do prawej krawędzi ekranu — więc Vdd nie przesuwa się już
  w bok, gdy qErr zmienia szerokość. Przy SAW wyłączonym pokazywane jest samo
  Vdd z pełną precyzją, nadal przy prawej krawędzi. Faza LTIC po lewej ma
  etykietę `dPh:±…ns` (bez spacji po `Vph:`) dla czytelniejszego, spójnego
  odczytu; raport szeregowy używa tej samej etykiety `dPh:` po `Vphase:`, więc
  oba są zgodne. qErr i dPh używają pola o stałej szerokości ze znakiem (znak zawsze
  widoczny, wartość wyrównana do prawej), więc cyfry i jednostki stoją w
  miejscu zamiast skakać w bok przy przejściu przez zero lub zmianie liczby
  cyfr.
### Naprawione
- **LOCK mógł tracić lock przy dryfującym OCXO — dostaje teraz łagodny człon
  częstotliwości.** W normalnej gałęzi LOCK ścieżka częstotliwości była
  wyłączona (freq_term = 0), więc jedyną obroną przed realnym dryfem OCXO był
  wolny feed-forward dryfu. Na ciepłym sprzęcie mocno się opóźniał, a faza
  wychodziła z okna locka (11 → −425 ns w 51 s, potem LOCK→DPLL→ACQ). LOCK
  stosuje teraz lekki człon 0,1×Kp — dość, by anulować bieżący dryf w każdym
  kroku, na tyle łagodny, by nie wtrącać szumu TIM2 do cichego locka. Łączy się
  z szybszym feed-forwardem (niżej). Analiza przyczyny: GML-5.2.
- **Usunięty limit-cycle w ACQ (bujanie PWM ±150 LSB).** Algorytm 10 brał błąd
  częstotliwości z avg10 (kwantyzacja 0,1 Hz); razy Kp (~1550 LSB/Hz) dawało to
  skoki PWM ±150 LSB w cyklu ~10 s, które nie pozwalały fazie ustabilizować się
  poniżej progu locka i spowalniały akwizycję. Teraz używa avg100 (0,01 Hz),
  10× drobniejsze, i akwizycja ustala się czysto. Analiza: GML-5.2.
- **Feed-forward dryfu robi teraz bootstrap po locku.** Jego pierwsze okno
  uczenia było wolne (30 s), więc szybki dryf po locku uciekał zanim ruszył.
  Teraz robi trzy szybkie okna 8 s z większym krokiem tuż po locku (absorbując
  dryf w ~10–20 s), potem wraca do cichego reżimu 30 s.
- **Dolna klamra tłumienia podniesiona 0,30 → 0,45.** Learner mógł tłumić tak
  mocno, że pętla miała tylko 30% mocy korekcji i nie nadążała za dryfem; 0,45
  wciąż tłumi cykl graniczny, ale zachowuje dość mocy, by śledzić.

### Zmienione
- **Kotwica kalibracji LC jest teraz uniwersalna — 0,632·Vsat, wyliczana per
  płytka.** Detektor to rampa ładowania RC V(φ) = Vsat·(1 − e^(−φ/τ)); punkt
  φ = τ, gdzie V = 0,632·Vsat, to ta sama względna wysokość na każdym detektorze
  wykładniczym, niezależnie od Vsat. LC odzyskuje teraz Vsat dopasowaniem 1-D
  (linearyzacja −ln(1 − V/Vsat) względem t, wybór Vsat o najmniejszej reszcie)
  i kotwiczy tam. Poprzednie zaszyte 1,85 V działało tylko dlatego, że detektory
  Marka i Dana Wiering mają Vsat ≈ 2,93 V; detektor o innym Vsat minąłby pasmo.
  LC samoadaptuje się teraz per płytka bez konfiguracji, a `LTIC_ZERO_ANCHOR_V`
  zostaje wycofane. Zweryfikowane na zalogowanych przebiegach: Vsat odzyskane do
  ~0,3%, kotwice zgodne ~0,8% między przebiegami. Fizyka i wyprowadzenie: GML-5.2.
- **Podtytuł splasha** brzmi teraz `GPS Disciplined OCXO` (spacja, nie myślnik).

### Podziękowania
- Diagnoza anomalii pętli i wyprowadzenie uniwersalnej kotwicy w tym wydaniu
  pochodzą od **GML-5.2**, zweryfikowane tutaj względem zalogowanych danych i
  zachowania sprzętu. Logi polowe i testy: **danieljw** (wzorzec Rb) i **lucido**.

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
- **Odczyt fazy `Vph` na TFT był martwym kodem, a przy włączeniu — błędnym.**
  Warunkował się stałą kompilacyjną `LTIC_NS_PER_VOLT` (domyślnie 0, więc wartość
  ns nigdy się nie pokazywała po kalibracji), a gdyby stała była ustawiona,
  liczył `V × ns_per_volt` od 0 V zamiast względem `zero_offset`. Teraz używa
  ZMIERZONYCH `g_ltic.ns_per_volt` i `zero_offset` z LC, pokazując fazę ze znakiem
  `(V − zero_offset) × ns/V` zgodną z błędem pętli, albo same wolty gdy
  nieskalibrowane.
- **CT odrzucał wąskie (lepsze) OCXO.** Sanity check wzmocnienia obiektu miał
  dolny próg K = 0,1 mHz/LSB, ale wąski span EFC jest pożądany — mniejsze Hz/LSB
  to lepsza rozdzielczość i jedna z dróg do E-12. Sprzęt Dana Wiering mierzy
  0,048 mHz/LSB (~1,05 V span EFC) i był błędnie odrzucany. Próg obniżony do
  0,02 mHz/LSB; odrzucane są teraz tylko przebiegi szum/brak-GPS.
- **Poprawki układu ILI9488 (480×320) — ze zdjęć użytkowników, jeszcze nie
  zweryfikowane na panelu.** Pierwsi użytkownicy Dan Wiering i lucido przysłali
  zdjęcia swoich buildów 480×320. Kilka problemów rozwiązano na ich podstawie
  bez panelu pod ręką: (1) font głównych danych był nadmiernie skalowany —
  TFT_F mapował font 2→4 (rosnąc 1.63× gdy wiersze skalują się tylko 1.33×),
  więc linie nachodziły w pionie, a pasek statusu wypadał poza ekran; font
  danych zostaje teraz na 2. (2) Wiersz czujnika BMP skrócono (temperatura i
  ciśnienie do 1 miejsca), by szersze skalowane glify nie nachodziły na kolumnę
  AHT. (3) W instrukcji `User_Setup.h` brakowało `LOAD_FONT8`, którego wymaga
  odczyt częstotliwości — bez niego ta linia zostaje pusta. To poprawki „na
  najlepsze wyczucie” ze zdjęć; finalne przejście po geometrii nastąpi, gdy
  będzie dostępny panel ILI9488. Małe panele 320×240 są nietknięte (TFT_F jest
  tam tożsamością).
- **LOCK mógł tracić lock przy dryfującym OCXO (odbicie LOCK→DPLL→ACQ).** Przy
  realnym dryfie częstotliwości (zmierzone ~8,5 ns/s na ciepłym sprzęcie) faza
  wychodziła z okna locka — 11 → −425 ns w 51 s — a korekcja była za słaba, by
  nadążyć: learner tłumienia dobił do 0,30 (korekcja na 30% mocy), a
  feed-forward dryfu wciąż zbierał pierwsze okno 30 s, więc nie ruszył zanim
  lock został utracony. Dwie zmiany: dolna klamra tłumienia podniesiona 0,30 →
  0,45 (zachowuje dość mocy, by śledzić dryf, wciąż tłumiąc cykl graniczny), a
  feed-forward robi teraz BOOTSTRAP po locku — trzy szybkie okna 8 s z większym
  krokiem absorbują dryf w ~10–20 s, potem wraca do wolnego, cichego reżimu
  30 s. W symulacji na zalogowanym dryfie faza trzyma się teraz ~−125 ns zamiast
  uciekać. Stabilne setupy o małym dryfie (np. build z referencją Rb) są
  nietknięte — bootstrap zbiega natychmiast, a wyższa klamra to nadal netto
  tłumienie.
- **Faza w ns dodana do raportu szeregowego**, po `Vphase:`, gdy LC skalibruje
  detektor — `(V − zero_offset) × ns/V`, ta sama konwencja co pętla i wiersz TFT.
- **Kotwica LC to teraz zmierzony środek rampy, nie stałe 1,85 V.** Kotwica
  lokalnego nachylenia była zaszyta pod pasmo detektora Marka; sprzęt, którego
  rampa przemiata inny zakres (Dan pracuje niżej, ~1,3 V), całkiem mijał okno
  kotwicy i wracał do zgrubnej średniej range/span (wynik „weak”). Kotwica to
  teraz `vlow + span/2` z rzeczywistego przebiegu, a stała `LTIC_ZERO_ANCHOR_V`
  jest używana tylko gdy faktycznie mieści się w przemiecionym paśmie. LC
  samoadaptuje się per płytka.
- **Ciśnienie na TFT mogło wchodzić na kolumnę AHT.** Ciśnienie BMP drukowane było
  z 2 miejscami (`1013.25hPa`), co przy 4-cyfrowym ciśnieniu wychodziło poza lewą
  kolumnę. Zmniejszone do 1 miejsca (`1013.2hPa`), spójnie z raportem szeregowym.
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
