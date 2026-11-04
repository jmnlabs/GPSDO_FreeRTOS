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

## [v1.08] — wydane 2026-11-04 (build 71)

Wydane jako build 71: `GPSDO v1.08.71rt`. Poniżej czternaście buildów od
v1.07 (58–71), od najnowszego, każdy opisany tak, jak go zmierzono. W skrócie:

- **Najwięcej pracy poszło w algorytm 13, filtr Kalmana.** Bije teraz
  algorytm 11 na płytce autora (build 61); czyta detektor poniżej okna i tym
  mniej ufa odczytowi, im dalej od zera on leży (66); jego zimny start nie
  płaci już za długi `KT` (67); regulator znów pracuje z `KC` = `KT`/3 (68),
  a dopóki trwa zakłócenie — z `KF` (16 s) (69). Na płytce Dana Wieringa,
  przy `KT 1000`, względem rubidu: 2,9·10⁻¹² przy 20–40 s i 1,5·10⁻¹³ przy
  40 000 s (build 70).
- **Detektor fazy jest czytany na zboczu PPS**, z prostej przez całą serię,
  na cichym układzie, z przetwornikiem sterowanym przez rejestry (61–64): na
  płytce autora jego biały szum spadł z 2,07 do 1,14 ns, a skok co 233 s
  zniknął. Dopasowanie pomija do dwudziestu zakłóconych pomiarów (70) i
  trzecią część serii, gdy detektor łapie 10 MHz (71); typowe wygięcie płytki
  idzie za poziomem detektora (71).
- **Start daleko od punktu pracy się ustala** (65–66): live store pamięta
  punkt pracy i jego span, pełny ring we flashu nie gubi już ustawień, a
  każdy z 264 zimnych startów w symulatorze ustala się po jednym uzbrojeniu
  picDIV.
- **Z forka Zsolta Zsirosa (68):** holdover i picDIV potrzebują impulsu GPS,
  a nie tylko fixa, `LC` uzbraja raz i czeka, raport idzie do USB i do
  Bluetootha osobno, tunel GPS nadąża za szybkim odbiornikiem, a termometr
  LM75 jest czytany i logowany.
- **Drobniejsze:** spinery na wyświetlaczach LED nie podwajają już obciążenia
  CPU, a `DAC` podaje prawdziwą nośną (67); pin PWM jest trzymany nisko, gdy
  EFC steruje AD5680 (69); `CT` i moduł spanu wierzą tylko liczbom, którym
  powinny (58); `LB` liczy od startu, co stało się z każdą sekundą (63–71);
  tuner rysuje napięcie sterujące, którego żąda pętla (60); komentarze w
  źródłach przeczytano od nowa razem z kodem, a martwy kod usunięto (59).

Build 71, `GPSDO v1.08.71rt`: dwie rzeczy, które pokazały testy skoków z
26–27.10 i serie Dana z 23–28.10. Dopasowanie prostej w detektorze pomija
całą trzecią część serii, gdy co trzeci pomiar odstaje razem — sprzężenie z 10 MHz —
zamiast czytać serię o kilka kodów obok albo ją odrzucać. Typowe wygięcie
płytki, którym dopasowanie ocenia każdą serię, nadąża teraz za poziomem
detektora, gdy ten się zmienia, więc skok nie powoduje już odrzucania serii
jako wygiętych. `KL` i `KT` przestają też traktować Q pod sufitem jako powód
do skrócenia `KT`. Bramka algorytmu 13 jest taka jak w buildzie 70:
puszczanie jej przy czwartym odrzuceniu skoku, jak robił ten build w pierwszej
wersji, kończyło się na płytce siedmioma puszczeniami w jednym skoku, a każda
wersja, która puszczała raz, wypadała gdzie indziej gorzej niż build 70 (zob.
Odrzucone).

### Zmienione (build 71)

- **Co trzeci pomiar pomijany w całości.** W części serii pomiary następują
  po sobie co 16⅔ taktu 10 MHz (w innych co ~17,7: przetwornik chodzi na
  zegarze MCU), więc co trzeci trafia w ten sam punkt okresu 10 MHz, a
  detektor, który łapie jego zbocza, ma jedną trzecią serii przesuniętą o
  tych samych kilka kodów. Na płytce Dana taka trzecia część była o 17–30
  kodów za wysoko (24 i 28.10) albo o 7,5 za nisko (23.10), a dwie pozostałe
  się zgadzały. Ciągnęła prostą o jedną trzecią swojego przesunięcia, a
  jeszcze bardziej, gdy słabła w trakcie serii: 24.10 jedna seria wyszła
  13 kodów za wysoko przy użyciu wszystkich pomiarów, a inna miała 21 pomiarów poza
  prostą jeden po drugim i została odrzucona. Teraz, na pierwszej prostej i
  zanim odpadnie którykolwiek pojedynczy pomiar, każda trzecia część jest
  brana medianą swoich reszt, a ta najdalsza od dwóch pozostałych jest
  pomijana, jeśli od ich średniej dzieli ją więcej niż 6 kodów
  (`LTIC_FIT_THIRD`) i trzykrotność własnego szumu serii
  (`LTIC_FIT_THIRD_SIG`; szum liczony wokół mediany każdej trzeciej części
  osobno, więc przesunięcie go nie zawyża), dwie pozostałe różnią się nie
  bardziej niż o tyle ani nie bardziej niż o połowę tej odległości, a dwie
  trzecie jej własnych pomiarów leżą co najmniej w połowie tej odległości.
  Nie kosztuje to żadnego z dwudziestu pominięć — te zostają na zakłócenia.
  Przez dopasowanie z firmware cztery serie Dana z taką trzecią częścią
  czytają się teraz na prostej przez dwie pozostałe: 24.10 A, wcześniej
  odrzucona; 24.10 B, o 12,7 kodu niżej niż w buildzie 70; 28.10, o 1,7
  niżej; 23.10, o 2,2 wyżej. Pięć pozostałych serii Dana i jedenaście autora
  czyta się dokładnie jak przedtem.
- **Typowe wygięcie płytki nadąża za poziomem.** Na płytce autora wygięcie
  serii nie jest jedną liczbą: rośnie, gdy odczyt spada, ok. 0,19 kodu na
  kod (~0,23 na ns fazy; od −5 kodów przy 2340 do +10 przy 2280, 26–27.10).
  Skok częstotliwości oscylatora wyprowadza fazę o 60–100 ns na kilka minut,
  a typowe wygięcie szło za tym najwyżej o jedną trzydziestą drugą kodu na
  sekundę: między dwoma wydrukami `LB` w odstępie 16 minut, w czasie dwóch
  skoków z 27.10, 131 serii zostało odrzuconych jako wygięte i 77 razy został
  poprzedni odczyt — akurat wtedy, gdy pętla sprowadzała fazę z powrotem.
  Teraz zapamiętywane są też wygięcia ostatnich 15 serii w paśmie `LC`
  (`LTIC_BEND_WIN`), a gdy ich mediana odbiega od typowego wygięcia o więcej
  niż 3,5 kodu, typowe od razu na nią przechodzi; w przeciwnym razie jest
  śledzone jak dotąd. Model tych 16 minut (wygięcie każdej sekundy z
  zalogowanego poziomu, 1,5 kodu rozrzutu, bramka jak w firmware) odrzuca
  270–290 serii przy dotychczasowym śledzeniu i 14–21 teraz; przy poziomie,
  który się nie rusza, mediana w ciągu dwunastu godzin ani razu nie odbiega
  tak daleko przy 2,1 kodu rozrzutu (płytka autora) i 0–12 razy przy 3
  kodach, kosztem ok. 1 % więcej odrzuconych serii.
- **`KL` i `KT` nie radzą już skracać `KT`, gdy Q siedzi pod sufitem.** `KL`
  nazywało ratio adaptacji `wants more Q` / `wants less`, a pod sufitem pisało,
  że „innovations want a loop faster than KT”; komunikat `KT` powyżej 200 s
  kazał je skrócić, jeśli `KL` pokazuje Q pod sufitem i błądzącą fazę. Na
  płytce Dana (28–29.10) `KT 1000` siedziało pod sufitem i mimo to wypadło
  lepiej niż `KT 500`: ADEV płaskie na 2,9·10⁻¹² od 40 do 400 s, a przy 500 s
  garb 4·10⁻¹² przy 100–150 s. To, co pcha Q w górę, równie często pochodzi
  z powolnego błądzenia po stronie GPS albo detektora, co z oscylatora, a
  szybsza pętla je kopiuje. Ratio pokazuje teraz `Q pushed up`,
  `Q pushed down` albo `consistent`, linia sufitu mówi, że innowacje są
  większe, niż tłumaczy model oscylatora, i że samo to nie jest powodem do
  skrócenia `KT` (porównaj ADEV), a komunikat `KT` każe oceniać je po ADEV,
  po przebiegu na każdą wartość. Poprawione jest też zdanie instrukcji, że
  „pętla siedząca tam przez cały przebieg mówi, że `KT` jest dłuższe, niż ten
  oscylator udźwignie”.

### Dodane (build 71)

- **`LB`** liczy od startu, w ilu seriach pominięto trzecią część i ile razy
  typowe wygięcie od razu się przesunęło (`every third conversion left out N,
  typical bend moved N`), a dla pokazanej serii mówi, która trzecia część
  została pominięta i o ile odstawała (`every third conversion from #0 left
  out: +29.6 codes off the other two`), albo jak daleko jest najdalsza
  (`thirds kept: the furthest -0.5 codes off the other two`).

### Odrzucone (build 71)

- **Puszczanie bramki algorytmu 13 przy czwartym odrzuceniu skoku.** Dziesięć
  odrzuceń z rzędu to własny test bramki, że błędny jest stan, a nie dane, i
  na początku skoku pętla stała 18–26 s (`SP`, 26–27.10). Build 71 w pierwszej
  wersji puszczał bramkę także przy czwartym odrzuceniu z rzędu, gdy każde
  było po tej samej stronie co poprzednie i nie bliżej filtru
  (`kf_runaway_step()`). Na płytce autora (30.10, `SP` +95 LSB, `KF 16`) faza
  doszła do −58 ns i wróciła w ±10 ns po 3,1 minuty, wobec −62 ns i 4,0 minuty
  przy +90 LSB w buildzie 70 — ale bramka puściła siedem razy w 80 s, przy 31
  odrzuconych odczytach, a PWM skakał przy każdym puszczeniu o ok. 30 LSB.
  Puszczenie poszerza tylko fazę filtru: bierze on odczyt, zostaje przy swojej
  częstotliwości i po kilku sekundach znów odjeżdża. Dziesięć odrzuceń puszcza
  raz, bo do tego czasu faza odjeżdża na tyle daleko, że człon fazowy
  sterownika mniej więcej równoważy skok. Filtr da się nauczyć też nachylenia
  serii (jego kowariancja poszerzona wzdłuż fazy i nachylenia serii): w
  `tools/loopsim` skok o +95 LSB puszcza wtedy raz i dochodzi do −21 ns
  zamiast −59, a w ±10 ns wraca po 45 s zamiast 283. Ale pętla gasi wtedy
  zakłócenie szybciej, niż pokazuje je 100-sekundowa średnia TIM2, a test
  zaufania detektora, który oczekuje, że faza ruszy się tak, jak mówi ta
  średnia, skazywał zdrowy detektor: w 8 z 16 odtworzeń spadku z 21.10 (w 5 z
  nich pętla była ślepa ponad dziesięć minut, najdłużej 56) i w 6 z 8
  przebiegów 120-sekundowego impulsu z `KF 0`. Impulsy 10–60 s były też brane
  za skoki i dawały przestrzał (+16 ns wobec +10 przy 300 LSB przez 10 s).
  Węższe wersje — nachylenie z połową wagi, dopiero przy drugim puszczeniu
  albo sama częstotliwość bez powiązania z fazą — wypadały gorzej niż build 70
  w 9–19 z 42 przypadków (21 zakłóceń, `KF 16` i `KF 0`, osiem ziaren);
  ograniczenie wczesnych puszczeń do jednego–trzech na zakłócenie dawało skok
  o 8–12 ns głębszy niż w buildzie 70, a seria pięciu do ośmiu odrzuceń nadal
  puszczała średnio 2,5–6,5 razy na skok (build 70: 1,5). Bramka jest więc
  taka jak w buildzie 70 i kompiluje się do tego samego kodu; uczenie jej
  nachylenia czeka na test zaufania, który uwzględni zakłócenie
  (`doc/TODO_v1.06.md`, punkt 178).

### Weryfikacja (build 71)

- Dziewięć serii Dana (21–28.10) i jedenaście autora (26–27.10) przez
  `ltic_fit()` z firmware, skompilowane jak w buildzie 70 i 71: cztery z
  odstającą trzecią częścią czytają się jak wyżej, z dokładnością do
  0,01 kodu na prostej przez dwie pozostałe, i przy bramce wygięcia na
  typowym wygięciu, które pokazało `LB`, żadna nie jest odrzucana; szesnaście
  pozostałych wychodzi jak w buildzie 70 co do ostatniej cyfry.
- Serie syntetyczne, po tysiąc na przypadek: przy trzeciej części odstającej
  o 15–30 kodów (narastającej albo słabnącej w trakcie serii) i do 8 zakłóceń
  968–1000 czyta się w granicach 5 kodów, gdzie build 70 czytał 184–541; przy
  12–16 zakłóceniach 763–984 wobec 296–471; przy trzeciej części o 8 kodów
  758–961 wobec 753–884. Bez odstającej trzeciej części nic nie jest
  pomijane i oba buildy zgadzają się seria w serię; przy 4 kodach jedna seria
  na 5000 straciła trzecią część. Zestawy zakłóceń z buildu 70 (12–28
  zakłóceń, cztery ziarna po tysiąc) wychodzą identycznie.
- `tools/lticsim`: wszystkie sprawdzenia przechodzą dla 27, 5 i 100 ms
  zaniku, w tym dziewięć nowych (trzecia część 30 w górę i 8 w dół pominięta,
  4 kody zostawione, trzecie części +30/−15/0 niepominięte, trzecia część
  plus dziesięć szpilek; wygięcie, które się przesuwa, śledzone, jedna
  dziwna seria i serie z szyny nie, −8 płytki znalezione na starcie);
  dotychczasowe liczby modelu są identyczne jak w buildzie 70. Nowe liczby:
  trzecia część odstająca o 8–30 kodów jest pomijana w 999–1000 seriach na
  1000; skok, który przesuwa wygięcie: 93 odrzucone przy dotychczasowym
  śledzeniu, 7 teraz; nieruchome wygięcie przy 3 kodach rozrzutu przesuwa się
  5 razy w dwanaście godzin.
- `tools/loopsim`: `gpsdo_algorithms.cpp` kompiluje się do tego samego kodu co
  w buildzie 70 (`.text`, `.data`, `.bss` i `.rodata` bajt w bajt), więc każda
  liczba pętli jest taka jak w buildzie 70.
- `tools/hosttests`: wszystkie siedem programów przechodzi (`kfdist` z 23
  sprawdzeniami buildu 70); hostcheck czysty we wszystkich 20 konfiguracjach.
  Statyczny RAM rośnie o 396 bajtów, ramka stosu dopasowania o 32.
- Kod przejrzany niezależnie; dwa komentarze, które twierdziły więcej, niż
  robi kod, poprawione, a okno wygięcia skrócone do nieparzystych 15, więc
  jego mediana jest zawsze wygięciem jednej serii.
- Testy skoków z 26–27.10 (build 70, płytka autora, `SP` +82 i +90 LSB): z
  `KF 16` faza doszła do −58 i −62 ns i była w ±10 ns po 4,4 i 4,0 minuty; z
  `KF 0` do −86 i −96 ns, pierwszy raz w ±10 ns po 13 i 16 minutach,
  przestrzeliła o +14 i +12 ns i była w środku na dobre po 25–27 i 32
  minutach. `KF 16` zostaje domyślne.
- Na płytce autora (29–30.10, build 71 w pierwszej wersji; to, co zostaje, od
  tego czasu bez zmian): 18 godzin bez resetu. `LB` naliczyło potem zero
  pominiętych trzecich części — żadnej w 21,7 h, łącznie z oboma skokami, a
  najdalsza trzecia część w 24 seriach odstawała najwyżej o 3,4 kodu — i 15
  przesunięć typowego wygięcia w tym przebiegu (lądowanie po starcie
  przechodzi ok. 600 kodów poziomu), a 29 po dwóch skokach `SP`. Odrzucone
  jako wygięte: ok. 13 serii przez cały skok z `KF 16` i ok. 29 przez skok z
  `KF 0`, wobec 131 w 16 minutach dwóch skoków z 27.10. W spokojnej pracy 14
  pojedynczych serii w 17 godzin odrzuconych jako wygięte (build 70: jedna w
  20 godzin tydzień wcześniej); skoro żadna trzecia część nie odpadła, to
  bramka wygięcia z buildu 70 — typowe wygięcie przeskakuje dopiero, gdy
  mediana 15 serii odbiega o 3,5 kodu, a przy locku waha się o ok. 0,5 kodu —
  na wygięciach rozrzuconych przy locku o ok. 1,8 kodu wobec 1,2 z 04.10. Stos
  FreqRelay: 102 ze 192 słów wolne.
- Znowu na płytce autora (31.10–1.11, build 71 w obecnej postaci, CRC obrazu
  8EED4610): 25 godzin bez resetu. Dwa skoki `SP` z `KF 16`, +77 i +79 LSB:
  bramka puściła raz w każdym, po dziesięciu odrzuceniach, 21 i 23 s po
  skoku; faza doszła do −54 i −59 ns i była w ±10 ns po 4,4 i 4,2 minuty, bez
  przestrzału (build 70, +82 i +90 LSB: 19 s, −58 i −62 ns, 4,4 i 4,0
  minuty). `LB`: żadna trzecia część nie odpadła w 25 h, najdalsza w 21
  seriach odstawała o 4,3 kodu od pozostałych dwóch; typowe wygięcie
  przesunęło się 27 razy w pierwszych pięciu godzinach, z lądowaniem po
  starcie, 9 razy w dwóch skokach i ani razu przez 18 spokojnych godzin po
  nich; w skokach ok. 17 i ok. 11 serii odrzuconych jako wygięte. W tych 18
  godzinach 22 pojedyncze serie odrzucone jako wygięte, za każdym razem na
  sekundę został poprzedni odczyt, przy fazie 1,60 ns rms (build 70
  25–26.10: jedna w 20 godzin, 1,66 ns). Gdy nic nie odpada i wygięcie nie
  przeskakuje, build 71 czyta każdą serię jak build 70, a szum pomiaru,
  którego używa filtr, rósł w tych samych dniach (R, mediana 1,56 ns
  25–26.10, 1,68 29–30.10, 1,83 teraz — między dwoma ostatnimi przy tym
  samym kodzie): zaszumiły się same serie, nie odczyt.

### Przy wydaniu (2026-11-04)
- Ściąga komend w tunerze zna `KC` i `KF` (algorytm 13), `LB` i `LRM`
  (detektor) oraz `MM` (algorytm 12), których jej brakowało; `H` w firmware
  już je wymieniał.
- Log startowy w podręcznikach pokazuje baner buildu 71 z objaśnioną linią
  `image CRC32`, a przykład paska stanu w podręczniku tunera jest z buildu 71;
  build v1.08 zajmuje ok. 300 KB (303 364 bajty), a nie 265 KB, jak podawał
  podręcznik.
- Kod się nie zmienił: firmware to build 71 w postaci z 30.10 (CRC obrazu
  8EED4610 na płytce autora).

Build 70, `GPSDO v1.08.70rt`: dopasowanie prostej w detektorze może pominąć
dwadzieścia pomiarów zamiast dwunastu, a opisy przestają obwiniać nośną PWM o
zakłócenia u Dana. Te zakłócenia przychodzą parami co ok. 21 µs, cztery–pięć
na serię, od czegoś poza GPSDO, co przełącza z częstotliwością ok. 23 kHz:
zostały, gdy build 69 trzymał już pin PWM nisko, także przy podświetleniu
`BL 100`, i pojawiały się i znikały zależnie od pory dnia (podejrzany jest
jego falownik fotowoltaiczny). Przy dwunastu pominięciach seria z sześcioma
parami lub więcej była odrzucana i zostawał ostatni odczyt; przy dwudziestu
jest czytana — w seriach, które przysłał, 0,2 kodu od odpornej prostej.

### Zmienione (build 70)

- **Dopasowanie w detektorze może pominąć dwadzieścia pomiarów**
  (`LTIC_FIT_DROPS` 12 → 20). Przez 57 godzin, 23–25.10, piąta część sekund
  na płytce Dana była odrzucana jako „off the line” i zostawał ostatni
  odczyt. Przez dopasowanie z firmware'u dwie odrzucone serie, które
  przysłał i które wypełniały pary zakłóceń (21.10 i 25.10), czytają się
  teraz z 18 i 15 pominiętymi pomiarami, 0,2 kodu od odpornej prostej; sześć
  pozostałych czyta się dokładnie jak wcześniej, tak samo jak czysta seria,
  w której nic się nie pomija (u autora 63–64 z 64 użytych). Na seriach
  syntetycznych z 13 do 18 zakłóceniami po 15–50 kodów 947–1000 na tysiąc
  czyta się teraz z błędem poniżej 5 kodów, a przy dwunastu 0–23. Seria
  rozpoczęta, gdy impuls jeszcze zanikał, ma w modelu `tools/lticsim` do 18
  takich pomiarów pominiętych; dalej odrzuca ją wygięcie, tak jak impuls,
  który jeszcze ładuje.
- **Czego to nie zmienia.** Seria z tyloma zakłóceniami może czasem ułożyć
  się na prostej przesuniętej o kilka kodów, bo MAD, którym dopasowanie
  ocenia odstające pomiary, rośnie razem z nimi; przy dwunastu działo się to
  równie często (0–94 na tysiąc od 13 do 20 zakłóceń, w obu wersjach tak
  samo z dokładnością do dwóch). A z dwóch serii, w których co trzeci pomiar
  jest zawyżony — sprzężenie 10 MHz na płytce Dana, 24.10 — jedna nadal ma
  21 pomiarów poza prostą i jest odrzucana, a w drugiej sprzężenie słabnie
  wzdłuż serii, MAD rośnie razem z nim i cała seria przechodzi, około
  13 kodów za wysoko — tak samo jak przy dwunastu.

### Naprawione (build 70)

- **Opisy zakłóceń u Dana.** Buildy 67–69 wiązały je z nośną PWM toru EFC
  (build 69: z jej drugą harmoniczną, 23,4 kHz). Nośna była prawdziwa —
  działała pod `EXT`, dopóki build 69 nie zaczął trzymać pinu nisko — ale
  zakłócenia to nie ona: zostały przy pinie trzymanym nisko. Wpisy buildu 69
  poniżej, instrukcja, README i komentarze w kodzie mówią teraz tak samo.

### Weryfikacja (build 70)

- Osiem serii Dana (21–25.10) przez `ltic_fit()` z firmware'u, skompilowane
  z dwunastoma i z dwudziestoma pominięciami: dwie odrzucane przez pary
  zakłóceń czytają się teraz (18 i 15 pominiętych, −0,2 i 0,0 kodu od
  odpornej prostej), dwie ze sprzężeniem 10 MHz wychodzą jak dotąd (jedna
  odrzucona, druga odczytana około 13 kodów za wysoko), a pozostałe cztery
  czyta się tak samo co do ostatniej cyfry.
- Serie syntetyczne, cztery ziarna po tysiąc dla każdej liczby zakłóceń od
  12 do 20: liczby jak wyżej. Do 12 zakłóceń obie wersje zgadzają się seria
  w serię.
- `tools/lticsim`: wszystkie sprawdzenia przechodzą dla zaniku 27, 5 i
  100 ms. „20 zakłóceń pominiętych” i „21 odrzuconych” idą za nowym limitem.
  Impuls zanikający jest pomijany do 18 pomiarów i odrzucany przy 21 (przez
  wygięcie): sprawdzenie w modelu stało o jeden pomiar przed limitem, a przy
  dwudziestu ogon tego modelu od 19 odrzuca wygięcie, więc sprawdzenie bierze
  teraz 18. Przebiegi szumowe i liczby odrzuceń identyczne z buildem 69.
- `tools/hosttests` jak w buildzie 69 (7 programów); hostcheck czysty we
  wszystkich 20 konfiguracjach.
- Build 69 na płytkach. U autora: 26 godzin 22–24.10 w dwóch przebiegach,
  bez `KAL: disturbance` i bez `KF` w żadnej linii `Learn:`; po pierwszej
  godzinie faza filtra poniżej 9,4 ns (próg to 20 ns), odchylenie fazy
  2,1–2,3 ns jak na buildzie 68. Na V3 Dana: raport `DAC` pokazuje
  `PWM pin PB9 held low`, w obu punktach pomiarowych PWM nie ma 11,7 kHz, a
  przy `KT 1000` / `KC 0` ADEV leży płasko na ok. 3·10⁻¹² od 8 do 400 s —
  poziom samego OCXO — tam, gdzie `KT 300` / `KC 100` dawały 7,5–8·10⁻¹² przy
  80–100 s. Test zakłócenia przy `KF 16` i przy `KF 0` jeszcze przed nami.

Build 69, `GPSDO v1.08.69rt`: sterownik algorytmu 13 przyspiesza, dopóki trwa
zakłócenie, a pin PWM jest trzymany nisko, gdy EFC steruje AD5680. `KC` to
wybór co do szumu — ile własnego dryfu detektora oscylator
ma skopiować — i build 68 go spowolnił. Skok częstotliwości oscylatora szumem
nie jest, a wolny sterownik pozwala mu odepchnąć fazę odpowiednio dalej. 21.10
płytka autora trafiła na taki skok: szyna 5 V spadła na trzy minuty do 4,37 V,
a OCXO przesunął się razem z nią o 2,7·10⁻⁹; przy `KC 33` faza poszła do
+75 ns, po powrocie napięcia przeszła na −51 ns i wróciła w ±10 ns dopiero po
12 minutach. Teraz sterownik pracuje z `KF` (16 s) od chwili, gdy faza jest
wyraźnie poza domem, aż przez dwie minuty będzie w domu; odtworzone w
symulatorze to samo zdarzenie daje +41/−29 ns i powrót w ±10 ns po 5½ minuty.
Spokojnej pracy to nie zmienia, co do ostatniego bitu PWM.

Do buildu 68 płytka na `EXT` miała na pinie PWM działający silnik ditheru. Dan
znalazł go oscyloskopem na sekcji PWM swojej V3 (22.10): 11,71 kHz, nośna
13-bitowa, z drugą harmoniczną 23,4 kHz — z taką częstotliwością powtarzają
się zakłócenia w jego seriach `LB`, o których build 68 pisał, że z tego pinu
pochodzić nie mogą. (Z niego też nie pochodzą — zob. build 70.)

### Naprawione (build 69)

- **Pin PWM jest trzymany nisko, gdy EFC steruje AD5680.** Przy starcie
  inicjowana jest każda skompilowana ścieżka, i do buildu 68 niewybrana robiła
  dalej to, co jej ostatnio kazano: AD5680 trzymał poziom DC, nieszkodliwy;
  PWM — nośną. Pierwszy zapis przy starcie (127) następuje przed odczytem
  ustawień, więc szedł domyślną ścieżką, DITH; odczytane `EXT` przejmowało
  każdy następny zapis, a silnik ditheru odtwarzał to 127 — przy 13 bitach
  impuls 0,16 µs co 85,3 µs — przez cały czas pracy płytki. Po `DAC EXT` w
  trakcie sesji odtwarzał ostatni kod, który dostał od pętli. Zworka nie
  dopuszcza tej nośnej do EFC, ale sekcja filtra PWM jest dalej na płytce i
  dalej jest sterowana. Teraz pierwszy zapis na `EXT` ustawia compare na 0,
  więc pin jest niski przez cały okres. Timer i DMA dalej pracują: `DAC DITH`
  albo `DAC PWM` wydaje kod pętli tak jak zawsze. Build bez silnika ditheru
  miał ten sam błąd przy 1,92 kHz i ma tę samą poprawkę. Pod `EXT` raport
  `DAC` mówi `PWM pin PB9 held low - no carrier while EXT is live`. Płytka na
  `DITH` albo `PWM` pracuje dokładnie jak wcześniej.
- **Uwaga buildu 68 o zakłóceniach u Dana była błędna.** Mówiła, że nie mogą
  pochodzić z PWM toru EFC: jego płytka steruje EFC z AD5680, więc przyjęto,
  że pin PWM jest niski od startu. Nie był (wyżej), a 23,4 kHz to dokładnie
  druga harmoniczna nośnej 13-bitowej (96 MHz / 4096). Czy zakłócenia znikną razem
  z nośną, pokaże jego płytka. Instrukcja i komentarze w kodzie mówią teraz
  tak samo. (Nie zniknęły — zob. build 70.)

### Dodane (build 69)

- **`KF` — horyzont sterownika na czas zakłócenia** (algorytm 13; domyślnie
  16 s, 10–250 s, `KF 0` = wyłączone; zapisywany razem z `KR`/`KQ`/`KT`/`KC`).
  Tryb włącza się, gdy estymata fazy filtru przekroczy 10-krotność spokojnego
  szumu płytki — R z czasu, gdy faza była spokojna, nigdy poniżej 2 ns: 20 ns
  na płytce autora, gdzie estymata przez 35 godzin nie przekroczyła 10 ns,
  około 37 ns na detektorze tak szumiącym jak Dana — albo gdy bramka innowacji
  puści po dziesięciu odrzuconych odczytach: tak właśnie wygląda początek
  skoku, a 21.10 odczyty uciekały z prędkością 2,7 ns/s od filtru, który przez
  13 s ich nie przyjmował. Wyłącza się po 120 s z rzędu poniżej 3-krotności
  tego szumu. Czyta estymatę filtru, nie odczyt, więc szumiący detektor nim nie
  trzęsie; nigdy nie robi sterownika wolniejszym niż `KC`; a po starcie albo
  uzbrojeniu picDIV czeka, aż faza raz się uspokoi — lądowanie to sprawa
  akwizycji. Log mówi, kiedy zmienia horyzont (`KAL: disturbance (|phase|
  43 ns) - controller 33 -> 16 s`, potem `KAL: calm for 120 s - controller
  16 -> 33 s`); dopóki działa, linia `Learn:` raportu `RH` kończy się na `KF`,
  `KL` pisze `disturbance: yes`, a `KC` pokazuje 16 s w mocy. `KF` jedzie w
  wolnym bajcie rekordu Kalmana, więc rekord zachowuje wersję: build 68 nadal
  go czyta, a rekord z buildu 68 lub starszego czyta się jako `KF 16`.
- **Dlaczego dwie minuty, a nie horyzont idący za fazą.** Po skoku błąd żyje
  dalej w estymacie częstotliwości filtru, której ponowne nauczenie trwa
  minuty, a faza, jaką zostawia, to ten błąd razy horyzont w mocy. Horyzont
  liczony co sekundę z |x0| — 16 s daleko, `KC` przy zerze, płynnie pomiędzy —
  dał w symulacji te same szczyty i ±10 ns po 8½ minuty zamiast 5½.

### Weryfikacja (build 69)

- Zdarzenie z 21.10 w `tools/loopsim` — profil częstotliwości oscylatora
  dopasowany z logu (−2,6·10⁻⁹ na początku spadku, łagodniejący do
  −1,9·10⁻⁹, i odwrotnie z krótkim przerzutem po powrocie napięcia), model
  płytki autora z buildu 66, pięć ziaren szumu: build 68 +78/−46 ns i powrót w
  ±10 ns po 12,1 minuty (płytka zalogowała +74,6/−51 ns i 12,0); build 69
  +41/−29 ns i 5,5 minuty, jak `KC 16` (+41/−29, 5,2). `KF 0` daje liczby
  buildu 68 co do cyfry.
- Spokojna praca: siedem scenariuszy — dwie godziny przed spadkiem, noc 29.09
  tak, jak ją puszcza `bench.py --hw`, i z dodanymi 4 ns wolnego dryfu
  detektora, noc 03.09 z 3,5 ns białego i 3 ns wolnego szumu, plany 26.08 i
  27.09 oraz model płytki Dana przy `KT 300` — po trzy ziarna, 1,14 mln
  symulowanych sekund: tryb nie włączył się ani razu, a PWM wyszedł identyczny
  z buildem 68. `coldstart.sh` wypisuje to samo co dla buildu 68.
- `tools/hosttests`: nowy `kfdist` 23/23 — włączony przy 20,1 ns, a przy 19,9
  nie; włączony, gdy bramka puszcza, choć faza stoi na 0,3 ns; wyłączony po
  dokładnie 120 spokojnych sekundach i tylko z rzędu; zablokowany od restartu
  i od uzbrojenia, aż faza raz się uspokoi; bajt `KF` (0, jak zapisywał każdy
  wcześniejszy build, czyta się jako 16 s; 10–250 s przechodzą w obie strony).
  Sześć celowo zepsutych kopii oblewa po 1–4 z 23 sprawdzeń. Pozostałe pięć
  jak w buildzie 68.
- `tools/hosttests`: nowy `dacpark` 15/15 — `pwm_park()` i `dac_emit()` wyjęte
  z `gpsdo_dac.cpp` i zbudowane dwa razy, z silnikiem ditheru i ze zwykłym
  timerem: start w kolejności firmware'u zostawia silnik na 0 (build 68
  zostawiał tam 127) i compare zwykłego timera na 0; godzina pod `EXT`
  napełnia tablice raz; `DAC DITH`, `DAC PWM` i `DAC EXT` przekazują kod pętli
  i znowu parkują; skalowanie AD5680 bez zmian. Cztery celowo zepsute kopie
  oblewają po 2–6 z 15 sprawdzeń.
- hostcheck czysty we wszystkich 20 konfiguracjach.
- Rozważone i pominięte: horyzont wolniejszy niż `KC`, gdy faza jest blisko
  zera. W symulacji działa jak lekko dłuższe `KC` — o 5 % mniejsze ADEV przy
  1–16 s i o 2 % większe przy 256–1024 s na modelu płytki autora, o 15–20 %
  mniejsze przy 1–64 s na modelu Dana — a `KT 200` z buildu 60 pokazał, że
  symulacja schlebia wolnym sterownikom. Czystszym testem jest noc przy
  `KC 50` na płytce.
- Na płytce do potwierdzenia: noc przy `KF 16` taka jak noce przy `KC 33`
  (w logu żadnego `KAL: disturbance`) i zakłócenie — np. krok zasilania —
  przy `KF 16` i przy `KF 0`; a na V3 Dana pod `EXT`: nic przy 11,7 kHz na
  sekcji PWM i to, co robi w `LB` liczba serii odrzuconych jako „off the
  line”. (Zob. build 70.)

Build 68, `GPSDO v1.08.68rt`: odniesienie z GPS musi być impulsem, a nie tylko
fixem, `LC` uzbraja picDIV raz i czeka na fazę, zamiast uzbrajać sześć razy,
raport telemetrii idzie do USB i do Bluetootha osobno, tunel GPS nadąża za
szybkim odbiornikiem, termometr LM75 jest czytany i logowany, a regulator
algorytmu 13 wraca do `KC` = `KT`/3. Pierwsze cztery zmiany i pomysł z LM75
pochodzą z forka Zsolta Zsirosa (ZsZs73), który na każdą z tych rzeczy trafił
na własnej płytce — dziękuję, Zsolt. Ostatnia to dwie noce i dzień na płytce
autora przy `KC 33` wobec nocy przy `KC 16` (15–17.10): o połowę mniej ruchu
PWM i 1,7 razy mniejszy szum częstotliwości przy 4–16 s, tyle samo od 256 s.

### Naprawione (build 68)

- **Holdover potrzebuje impulsu, nie tylko fixa.** Automatyczny holdover
  zaczynał się, gdy znikał fix, i kończył, gdy wracał. Ale odbiornik
  timingowy w Time Mode podaje swoją stałą pozycję jako ważną bez żadnego
  sygnału, a Zsolt widział, jak jego płytka wychodzi z holdoveru na tej
  pozycji bez impulsu. Teraz odniesienie to fix ORAZ impuls, którego odstęp
  OCXO zmierzył jako wiarygodny: holdover zaczyna się, gdy znika fix albo po
  10 s bez impulsu (`GPS 1PPS lost — auto-holdover engaged`), a kończy dopiero,
  gdy fix wrócił razem z 10 s nieprzerwanych impulsów — impuls czy dwa z anteny,
  która to łapie, to gubi sygnał, już go nie kończą. Pierwsze odniesienie po
  starcie jest przyjmowane od razu, jak dotąd. A skoro wyjście z holdoveru
  trwa teraz te 10 s, zajęta blokada nie udaje już utraty odniesienia:
  tyknięcie pętli, które nie zdołało odczytać fixa, zostaje przy ostatnio
  odczytanym (do buildu 67 nieudane 5 ms czekania na odczyt liczyło się jako
  utrata fixa, co kosztowało wtedy jedno tyknięcie, 200 ms, a teraz
  kosztowałoby te 10 s), a zmiana flag holdoveru jest ogłaszana i uznawana za
  zrobioną dopiero wtedy, gdy naprawdę została zrobiona.
- **Ręczny holdover zostaje ręczny mimo utraty odniesienia.** Do buildu 67
  utrata odniesienia w trakcie `MH` zamieniała go w holdover automatyczny,
  który powrót fixa potem kończył: GPS kończył holdover operatora. Teraz
  utrata jest ogłaszana (`GPS fix lost — manual holdover kept` albo `GPS 1PPS
  lost — …`), a holdover czeka, aż zakończy go `MD`.
- **picDIV jest uzbrajany tylko wtedy, gdy ma się do czego zsynchronizować.**
  Każde uzbrojenie — to, o które prosi pętla, i własne uzbrojenia `LC` —
  wymaga teraz fixa i impulsu w ostatnich 1,5 s (`picDIV: no GPS fix or no
  1PPS - arming deferred until both are back`): bez impulsu na wejściu Sync
  dzielnik pozostałby zatrzymany. `LC` pomija bez niego pierwsze uzbrojenie,
  tak jak dotąd bez fixa, a zamiast uzbrajać na ślepo pod rampę rezygnuje i
  przywraca kod pętli (`LC: ERROR no GPS fix or no 1PPS to sync the picDIV to
  — aborting.`).
- **Host USB, który nie czyta, nie wstrzymuje już raportu w trybie
  równoległym.** Przy `GPSDO_BLUETOOTH_PARALLEL` (domyślnie włączony) raport
  szedł przez tee, a tee bierze port, który zgłasza brak miejsca, za port,
  którego nie ma — tymczasem podłączony host, który przestał czytać, zgłasza
  to samo zero, bo jego kolejka jest pełna. Tee odpowiadało wtedy miejscem w
  UART i pisało do obu portów, w pełną kolejkę USB: dokładnie tę blokadę, przed
  którą procedura zapisu raportu ma chronić. Teraz do każdego portu raport
  jest zapisywany osobno, najpierw do Bluetootha, każdy z własnym limitem
  czasu — 25 ms dla USB, jak dotąd, i 200 ms dla UART, który opróżnia się przy
  57600 bodów bez względu na to, kto słucha, więc dostaje cały raport nawet za
  pełnym buforem (build z samym Bluetoothem też dostaje te 200 ms). Wskaźnik
  przesuwa się o tyle, ile `write()` naprawdę przyjął, a nie o tyle, ile mu
  zaproponowano, a zapis, który nie przyjmuje nic — USB bez otwartego
  terminala albo bez hosta w ogóle — od razu kończy próbę dla tego portu.
- **Tunel GPS nadąża za szybkim odbiornikiem.** Przenosił jeden bajt w każdą
  stronę na milisekundę: dość przy 9600 bodów, za mało przy 38400 (domyślne dla
  ZED-F9T), gdzie każda paczka NMEA powyżej ~350 bajtów przepełniała 256-bajtowy
  bufor odbiorczy, a u-center widział połamane ramki. Teraz w każdym przebiegu
  opróżnia bufor odbiornika do końca, a bajty od hosta przekazuje tak szybko,
  jak przyjmuje je bufor nadawczy UART — nigdy szybciej, więc duży plik z
  u-center (konfiguracja, firmware) nie zatrzyma zadania w zapisie, który
  czeka na UART.
- **Notatki buildu 67 przypisywały zakłócenia Dana nośnej PWM z toru EFC.** To
  fala prostokątna 23,4 kHz — dokładnie tyle dałby dither 12-bitowy (96 MHz /
  4096). Ale płytka Dana buduje domyślny dither 13-bitowy (11,7 kHz), od startu
  wybiera AD5680, więc pin PWM zostaje w stanie niskim, a 19,2 kHz PWM
  podświetlenia TFT też nie daje 23,4 kHz: skąd się biorą, jeszcze nie wiadomo.
  Wpis buildu 67 niżej, instrukcja, README i komentarze w kodzie mówią to teraz
  wprost; dopasowanie nigdy od tego nie zależało.

### Zmienione (build 68)

- **`LC` uzbraja picDIV raz i czeka na rampę.** Do buildu 67 uzbrajał go do
  sześciu razy, aż lądowanie wypadło w paśmie detektora, w przekonaniu, że
  synchronizacja kładzie fazę po losowej stronie zbocza GPS. Tak nie jest:
  zbocze dzielnika wypada w takt jego własnego cyklu rozkazowego, a ponowne
  uzbrojenie ląduje tam, gdzie poprzednie (model płytki z buildu 66). Płytka,
  której lądowania wypadają poniżej pasma, oblewała więc wszystkie sześć prób
  tak samo, a każde sprawdzenie następowało 6 s po zwolnieniu, 5–6 s po
  synchronizacji — 30–36 ns przemiatania przy 6 ns/s wobec ~39 ns progu
  0,10 V — więc nawet lądowanie tuż przy zboczu było odrzucane za każdym
  razem. Przemiatanie wydłuża odstęp, więc `LC` uzbraja teraz raz i pozwala mu
  wprowadzić fazę do pasma: ze 100 ns po złej stronie zbocza w ~24 s od
  lądowania, najwyżej przez dwie minuty (`LC: ramp wait … s, V = …` co 10 s),
  z drugim uzbrojeniem, gdy ten czas minie. Lądowanie w paśmie idzie dalej od
  razu, jak dotąd; to poniżej wchodzi od dołu, więc przejście obejmuje całą
  rampę. Z forka Zsolta, gdzie `LC` przechodzi na jego płytce; lądowania płytki
  autora są w paśmie, więc jego `LC` działa jak przedtem.
- **`KC` 0 = auto to znów `KT`/3** (33 s przy domyślnym `KT 100`; w buildach
  61–67 `KT`/6, 16 s). Logowana faza, według której wybrano `KT`/6, jest
  odczytem detektora, a szybki regulator poprawia ją, przepisując do oscylatora
  własne powolne błądzenie detektora — a z płytki wychodzi oscylator. To, co
  robi oscylator, to scałkowane PWM: kroki kodu razy herce na LSB; ADEV tego
  iloczynu zgadzało się co do kilku procent od 4 do 400 s z pomiarem płytki
  Dana względem wzorca rubidowego (14.10). Na płytce autora, w jednej sesji,
  noc przy `KC 16`, a potem dwie noce i dzień przy `KC 33`:

  | | `KC 16` | `KC 33` |
  |---|---|---|
  | zmiany PWM na godzinę | 1349 | 664–744 |
  | ADEV scałkowanego PWM, 4 s | 1,85e-11 | 1,05–1,12e-11 |
  | 16 s | 3,68e-11 | 2,11–2,32e-11 |
  | 64 s | 4,41e-11 | 3,51–3,72e-11 |
  | 256 s | 3,08e-11 | 2,76–3,23e-11 |
  | odch. std. logowanej fazy | 1,83 ns | 2,14–2,30 ns |

  Płytka, która zapisała własne `KC`, zachowuje je; `KC 0` bierze nową
  domyślną. Pomoc i odpowiedzi komendy `KC` mówią `KT/3`.

### Dodane (build 68)

- **Termometr z rodziny LM75** (`GPSDO_LM75`, domyślnie włączony; adres
  `GPSDO_LM75_ADDR`, 0x48): LM75A/B, PCT2075, TMP75/175, TMP1075 albo dowolny
  układ z rejestrem temperatury LM75, czytany zwykłym `Wire` — bez biblioteki.
  Wykrywany przy starcie jak pozostałe czujniki (`HW: LM75 sensor OK (I2C
  0x48)` albo `not found`), czytany co 2 s pod blokadą magistrali i logowany w
  linii czujników raportu czytelnego (`RH`), po `CPU:`, jako `LM75:<°C>C` z
  trzema miejscami po przecinku: krok 1/8 °C w LM75A, LM75B NXP i PCT2075,
  1/16 °C w TMP1075, 1/2 °C w układach 9-bitowych (oryginalny LM75, TMP75/175
  w ustawieniu po włączeniu zasilania). `LM75:---`, gdy nie odpowiada od 6 s.
  Odczyt, jakiego żaden taki układ nie mógłby wysłać — z ustawionym którymś z
  czterech najmłodszych bitów albo spoza −55…125 °C — liczy się jak brak
  odpowiedzi, więc INA219 ustawiony na 0x48 nie zostanie wzięty za termometr;
  ADS1x15 albo PCF8591 pod tym adresem może (wtedy zmień adres albo zbuduj bez
  niego). Na wyświetlaczach na razie nic, a raport tabelaryczny (`RD`)
  zachowuje swoje kolumny. Przyklejony do obudowy OCXO daje w każdym logu
  temperaturę samego oscylatora obok fazy.

### Weryfikacja (build 68)

- hostcheck czysty we wszystkich 20 konfiguracjach — nowy wiersz buduje płytkę
  autora bez LM75, który budują teraz wszystkie pozostałe, a wiersze płytki
  autora mają teraz AD5680 (`GPSDO_DAC_EXT`) i `GPSDO_GPS_TIMING`, tak jak
  płytka wychodzi; przy `-Wall -Wextra -Wshadow -Wdouble-promotion` na każdej
  jednostce kompilacji konfiguracji domyślnej te same 181 ostrzeżeń co w
  buildzie 67. Kod ok. +1,4 kB, statyczny RAM +16 bajtów; ramka zadania
  czujników 48 bajtów (8), a zapis raportu sięga w zadaniu wyświetlacza 48
  bajtów w głąb (32).
- Nowe `tools/hosttests` (`run.sh`): nowy kod wyjęty z firmware'u dosłownie i
  uruchomiony na PC. `holdover` 80/80 — zamrożona pozycja bez impulsu włącza
  holdover po 10 s, nierówny impuls (3 s jest, 2 s nie ma) nigdy go nie kończy,
  równy kończy go po 10 s; `MH` zostaje ręczny mimo utraty i powrotu
  odniesienia; blokada zajęta przez 10 s to nie utrata fixa, a zmiana, którą
  opóźnia, zostaje zrobiona i ogłoszona raz; picDIV czeka na impuls; i
  wszystko to jeszcze raz przez przepełnienie `millis()`. `lcland` 67/67 —
  lądowanie +500 ns idzie dalej od razu, +2 ns po 2 s czekania, 100 ns po złej
  stronie po 19 s czekania (~24 s od lądowania), 400 ns przy 4 ns/s po 105 s;
  drugie uzbrojenie, gdy pierwsze czekanie minie; lądowanie powyżej pasma albo
  takie, które nie dochodzi, kończy się czystym przerwaniem z przywróconym
  kodem; bez odniesienia żadnego uzbrojenia, a po jego utracie żadnego
  drugiego. Na tym samym modelu sześć prób buildu 67 odrzuca każde powtarzalne
  lądowanie poniżej +9 ns (na płytce od +3 do +9 ns, zależnie od tego, gdzie w
  sekundzie wypada zwolnienie). `report` 20/20 — nieczytane CDC kosztuje swoje
  25 ms i nic nie przyjmuje, port USB bez hosta nie kosztuje nic, krótki zapis
  niczego nie gubi, UART za pełnym buforem dostaje cały raport w 200 ms, gdzie
  dawne 25 ms ucinało koniec, a w trybie równoległym zablokowane USB nic nie
  odbiera Bluetoothowi. `lm75` 32/32 — każda rozdzielczość, oba znaki, końce
  zakresu, kolejność bajtów, odrzucony rejestr INA219, magistrala, która
  zawodzi. `tunnel` 14/14 — przy 9600, 38400 i 115200 bodów nie ginie żaden
  bajt z odbiornika, a 20 KB z u-center przechodzi bez ani jednego zapisu do
  pełnego bufora nadawczego. Każda z siedemnastu celowo zepsutych kopii oblewa
  swój test: `holdover` 4–25 z 80 sprawdzeń (bez wymogu ciągu impulsów przy
  wyjściu z holdoveru 10, bez sprawdzania impulsu 25, zajęta blokada GPS
  czytana jako utrata fixa 6, `MH` zamieniany w automatyczny 9), `lcland` 10–21
  z 67, `report` 2 z 20, `lm75` 2–11 z 32 (zamienione bajty 11), `tunnel` 3–8
  z 14 (jeden bajt na przebieg, jak w buildzie 67, 8).
- `KC` w skalibrowanej powtórce (`tools/loopsim/bench.py`, cztery nocne
  obiekty): ruch DAC −40 %, ADEV oscylatora −33 % przy 10 s i −21 % przy
  30 s, równo przy 100 s, +7 % przy 300–1000 s, odch. std. fazy 0,54 wobec
  0,50 ns; w scenariuszu płytki (`--hw`) 822 zmiany PWM na godzinę wobec 1468.
  `coldstart.sh`: każdy start się ustala z jednym uzbrojeniem, 0–15 s później
  niż w buildzie 67 (wolniejsze ręce domykają ostatnie nanosekundy później).
  Przy `KC 16` `bench.py` daje liczby buildu 67 co do cyfry.
- `tools/algoswitch`, `railwatch` (17/17), `livestore` (49/49 i 3/3),
  `flashring`, `spansim` i `lticsim` drukują dokładnie to samo co dla buildu 67.
- Na płytce, do potwierdzenia: `HW: LM75 sensor` przy starcie i `LM75:` w
  raporcie; `LC` jak dotąd; po odłączeniu anteny `auto-holdover engaged`, a po
  podłączeniu `disengaged` ok. 10 s po powrocie impulsów; to samo pod `MH` —
  `manual holdover kept` i holdover nadal włączony po powrocie; noc przy
  `KC 0` taka jak noce przy `KC 33`.

Build 67, `GPSDO v1.08.67rt`: spinery na wyświetlaczach LED przestają podwajać
obciążenie procesora, seria detektora z zakłóceniami od fali prostokątnej jest
czytana, zimny start algorytmu 13 nie płaci już za długi `KT`, a raport `DAC`
podaje nośną, jaką płytka naprawdę ma. Na płytce autora wskaźnik CPU stał na
55–56 % przez cały survey-in wobec 27 % później, a zadanie wyświetlacza, z
41–49 % procesora wobec 24 %, było większością tej różnicy. Listingi `LB` Dana
(14.10) pokazały trzecią część jego serii odrzuconą jako „off the line”, a
każda z nich niosła parę zakłóceń na każdym zboczu fali prostokątnej 23,4 kHz.
Do tego długi `KT`, jakiego chce jego cichy OCXO, wydłużał zimny start na
modelu jego płytki do pół godziny. Przy `KT 100` pętla działa bit w bit jak w
buildzie 66.

### Naprawione (build 67)

- **Spinery LED nie podwajają już obciążenia CPU.** W czasie rozgrzewki,
  survey-in i `C`/`CT` zadanie wyświetlacza budzi się co 150 ms, żeby
  przesunąć spinery na TM1637 lub HT16K33, a do buildu 66 każda taka pobudka
  wykonywała całe zadanie: nagłówek TFT budowany od nowa i wysyłany, OLED i
  LCD przerysowywane — sześć, siedem razy na sekundę, żeby pokazać to, co
  zmienia się raz. Sam zapis do LED był tu drobiazgiem (~1,2 ms na HT16K33
  przy 100 kHz, 5–8 ms bit-bangiem na 6-cyfrowym TM1637). Teraz pobudka, której
  nie wywołało żadne powiadomienie, w czasie animacji przesuwa cyfry LED i nic
  więcej; powiadomienie (PPS, zdanie czasu z GPS) nadal uruchamia pełny
  przebieg, tak samo każda pobudka 1,1 s po ostatnim pełnym, więc żaden ekran
  nie czeka na odświeżenie dłużej niż przedtem (flagi animacji są czytane
  ponownie po czekaniu, więc ekran animacji, która się właśnie skończyła, nie
  zostaje na dłużej). Poza animacją każda pobudka jest pełnym przebiegiem, jak
  była.
- **Seria z zakłóceniami jest czytana** (`LTIC_FIT_DROPS` 8 → 12). Detektor,
  który łapie falę prostokątną, dostaje parę zakłóceń, jedno w dół i jedno w
  górę, na każdym jej zboczu: na płytce Dana 15–30 kodów co 21,3 µs (23,4 kHz),
  cztery–pięć par w każdej 90-mikrosekundowej serii. (Ten wpis nazywał to
  najpierw nośną PWM z toru EFC. To nie ona — zob. build 68.) Dopasowanie
  pomija pomiar odstający od prostej i liczy ją od nowa, a przy ośmiu takich
  pominięciach trzecia część jego serii była odrzucana i zostawał poprzedni
  odczyt. Przez własne dopasowanie firmware'u dwie odrzucone serie czytają się
  teraz przy 11 i 9 pominiętych pomiarach, w granicach 1,5 kodu od odpornej
  prostej (Theil–Sen), a przyjęta czyta się jak przedtem. Czysta seria nie
  pomija niczego, więc płytka bez takich zakłóceń czyta dokładnie tak jak
  dotąd. Jedno zmienia się razem z tym: seria zaczęta, gdy impuls jeszcze
  wygasał przez pierwsze dziewięć–dwanaście pomiarów, którą build 66 odrzucał,
  ma je teraz pominięte jak zakłócenia i czyta się tak dobrze jak czysta (przez
  dopasowanie firmware'u, po 200 zaszumionych serii dla każdej długości:
  mediana błędu 0,3–0,4 kodu, jak dla czystej). Poza zasięgiem pominięć albo
  wygięta tak, jak wygina prostą impuls wciąż ładujący, jest odrzucana jak
  przedtem.
- **Zimny start algorytmu 13 nie płaci już za długi `KT`.** `KT` mówi, jak
  długo można ufać własnej częstotliwości oscylatora, a cichy oscylator chce go
  długiego — na płytce Dana model daje 300–1000 s. Do buildu 66 ta sama liczba
  ustawiała też wszystko, co pętla robi, zanim pierwszy raz się ustali: horyzont
  sterowania, pierwsze przybliżenie Q (R/KT³), założenie o częstotliwości,
  częstotliwość, na którą czeka bramka uzbrojenia, i to, jak daleko rzutuje
  lądowanie, ile odczytów uśrednia, zanim przejmie `KC`, a przez to także
  cierpliwość testu zawieszenia i awaryjne wyjście bramki uzbrojenia. Przy
  `KT 1000`
  każda z tych rzeczy wychodziła dziesięć razy wolniejsza albo tysiąc razy
  ciaśniejsza, zimny start trwał w modelu 28–41 minut, a skoro `KT` jest
  zapisywane, jedynym ratunkiem było `KT 100` przed każdym restartem.
  Dociąganie używa teraz `KT` tylko do 100 s (`kf_acq_horizon`); śledzenie —
  sufit Q R/KT³, domyślne `KC` = `KT`/6, szum starzenia, szyna dla
  przypiętego `KQ` — zachowuje wpisane `KT`. Przy 100 s i poniżej nic się nie
  zmienia.
- **`DAC` podaje prawdziwą nośną.** Raport dzielił zakładany zegar timerów
  100 MHz; rdzeń STM32duino taktuje F411 BlackPilla na 96 MHz (kwarc 25 MHz,
  PLL N 192, P 2, żeby USB dostało 48 MHz), więc każda wypisana nośna była o
  4 % za wysoka — 12 207 Hz zamiast 11 719 silnika 13-bitowego, 24 414 zamiast
  23 438 12-bitowego. Zegar jest teraz odczytywany z RCC (`pwm24_timer_hz()`)
  i wypisywany obok nośnej. Nic innego od niego nie zależało: dither uśrednia
  wypełnienie, a wypełnieniu zegar jest obojętny. Komentarze, które podawały
  100 MHz, mówią, na czym płytka chodzi (PWM podświetlenia 19,2 kHz, zwykły
  PWM 16-bitowy 1,92 kHz przy 50 000 krokach).
- **Anonimizacja pozycji w tunerze łapie uszkodzoną linię.** Łącze szeregowe
  czasem gubi bajt, a linia z pozycją, która straciła pierwsze znaki albo znak
  nowej linii, nie zaczyna się już od `Lat:`. Zapis z 07.10 zachował dwie
  takie linie w całości: jedną zaczynającą się w połowie szerokości, drugą od
  `at: `, ze zlepioną następną linią `Up:`. Poza dokładnymi formami linia traci
  teraz liczbę po każdej etykiecie `Lat:` lub `Lon:` (z cyframi całości albo
  bez nich — poniżej 10 stopni jeden zgubiony bajt zabiera jedyną), liczby tuż
  przed `Lon:` i `Alt:`, liczbę po `Alt:`, krótkie formy w dowolnym miejscu,
  także etykiety przyklejone do poprzedniej linii, i — w linii ze słowami z
  raportu pozycji — każdą liczbę o kształcie współrzędnej (od czterech do
  siedmiu miejsc po przecinku). Zwykła linia wychodzi bajt w bajt jak
  przedtem.

### Zmienione (build 67)

- Komunikat komendy `KT` powyżej 200 s nie mówi już, że długi horyzont
  „źle śledzi”: to była prawda o szybko błądzącym OCXO autora i nieprawda o
  cichym. Mówi zamiast tego, na co patrzeć — Q pod sufitem w `KL` i błądząca
  faza znaczą: skrócić — i że zimny start dociąga przy 100 s, cokolwiek mówi
  `KT`.
- HT16K33 ma pamięć zapisu, jak TM1637: ramka (cztery cyfry i dwukropek)
  wychodzi tylko wtedy, gdy się zmieni, więc pobudka, która wysłałaby tę samą
  ramkę jeszcze raz, nie dotyka magistrali. Zapamiętywany jest tylko zapis
  potwierdzony przez wyświetlacz, a co 10 s układ jest ustawiany od nowa
  (oscylator, wyświetlanie, jasność) i ramka wysyłana ponownie, więc moduł po
  zaniku zasilania — wraca ciemny, z wyłączonym oscylatorem — świeci znów w
  ciągu sekund, a nie dopiero po restarcie.
- `tools/lticsim`: sprawdzenia z zakłóceniami idą za `LTIC_FIT_DROPS` (tyle
  zakłóceń, ile ono puszcza, jest pomijanych, o jedno więcej — odrzucane);
  `run.sh` przekazuje je modelowi, a dwa sprawdzenia idą za wygasającym
  impulsem (o jeden pomiar mniej, niż sięgają pominięcia: pominięty; o jeden
  więcej: odrzucony). Nowy `tools/redact_test.py` sprawdza anonimizację tunera
  na zmyślonych współrzędnych, a z podanymi logami pokazuje, co zmienia w
  prawdziwej telemetrii. hostcheck ma dziewiętnasty wiersz — płytkę autora z
  HT16K33, który domyślna konfiguracja włącza, a żaden wiersz nie budował; jego
  zaślepka zna zegar APB1. `tools/carrier.py` liczy przy 96 MHz.
- Instrukcja: rozdział o cyfrach LED, akapit o serii (pomijane pomiary i na
  co wskazuje wiele odrzuconych serii), `KT`/`KC` i dodatek D.3 oraz nośna w
  `DAC`; tabele nośnych w README przy 96 MHz; README_TUNER o anonimizacji.
  Linia firmware'u wymienia build 67.

### Weryfikacja (build 67)

- hostcheck czysty we wszystkich 19 konfiguracjach (arm-none-eabi,
  cortex-m4); przy `-Wall -Wextra -Wshadow -Wdouble-promotion` na wszystkich
  jednostkach konfiguracji domyślnej te same 181 ostrzeżeń co w buildzie 66.
  Flash +514 bajtów, statyczny RAM +19; ramka zadania wyświetlacza 312 bajtów
  (304), dopasowania serii (104) i `kalman_ctl()` (608) bez zmian.
- `tools/loopsim/coldstart.sh`, build 66 wobec buildu 67: wszystkie trzy bloki
  identyczne, start w start. `bench.py run --hw`: identycznie co do cyfry —
  algorytm 13 2,40 ns, część wolna 0,88 ns, ADEV 4,08e-12 przy 1024 s, 1468
  zmian PWM na godzinę, algorytmy 11 i 12 jak dotąd.
- `KT` na modelu płytki Dana (3 ns białego szumu detektora, co trzecia sekunda
  z powtórzonym odczytem, cichy OCXO), po 18 zimnych startów przy 0 i ±300 LSB:
  `KT 100` identycznie jak build 66 (mediana 375 s, najdłużej 464); `KT 300`
  mediana 392 s, najdłużej 508 (build 66: 761 i 12 082); `KT 1000` mediana
  540 s, 355–737 (build 66: 2090, 1689–2466). Jedno uzbrojenie w każdym starcie.
  W locku, sześć ziaren po pierwszych 5000 s: ADEV od 1 do 4096 s i aktywność
  PWM w granicach 1 % od buildu 66 przy `KT 300` i `1000`, identycznie przy
  `100`; z `KQ` przypiętym na 1e-5 lub 1e-4 przy `KT 1000` ta sama szyna co w
  buildzie 66 i te same liczby z dokładnością do 0,1 %.
- `tools/lticsim`: wszystkie 22 sprawdzenia przechodzą przy każdej z trzech
  stałych zaniku — 12 zakłóceń pominiętych i 13 odrzuconych, impuls wygasający
  przez 11 pominięty, przez 13 odrzucony, ładujący na 12 pomiarach odrzucony
  przez wygięcie — a statystyki odrzuceń są jak dotąd. Trzy
  serie Dana przez dopasowanie wycięte z tego drzewa: dwie odrzucone czytają
  się 2366,53 i 2359,76, trzecia 2367,29 jak przedtem; serie autora z trzech
  logów czytają się jak dotąd co do cyfry.
- Pobudki pętli wyświetlacza, w modelu: z PPS i zdaniem GPS dwa pełne
  przebiegi na sekundę (jak poza animacją) i pięć tylko-LED; bez żadnego
  powiadomienia pełny przebieg co 1,2 s (wcześniej 1,1 s); HT16K33 zapisywany
  ok. pięć razy na sekundę przy spinerze, raz na sekundę przy czasie.
- `tools/redact_test.py`: linia raportu dokładnie jak przedtem; dwa
  uszkodzone kształty z 07.10 i dziewięć innych tracą każdą współrzędną (ani
  trzech miejsc po przecinku z częścią całkowitą, ani czterech po kropce, ani
  pięciu z rzędu); piętnaście innych linii telemetrii bez zmian, w tym trzy ze
  słowami z raportu pozycji. Na 30 568 syntetycznych uszkodzonych raportach —
  zgubiony bajt albo dwa, zgubione serie 3–16 bajtów, linie ucięte, zlepione
  albo zaczęte w środku — nic ze współrzędnej nie przetrwa, gdy build 66
  przepuszczał w 17 161. W sześciu prawdziwych logach (ok. 1,7 mln linii)
  zmieniają się tylko dwie uszkodzone linie z 07.10, a potem żadna linia nie
  niesie liczby bliżej niż 0,05° od pozycji odbiornika.
- `tools/algoswitch`, `railwatch` (17/17), `livestore` (49/49 i 3/3),
  `flashring` i `spansim` wypisują dokładnie to, co dla buildu 66.
- Niezależny przegląd kodu znalazł dwa błędy, oba poprawione, zanim build
  wyszedł: pierwsza wersja anonimizacji przepuszczała jeszcze jeden–dwa
  zgubione bajty (długość poniżej 10 stopni bez cyfry całości, linię zaczętą
  w środku długości), a ziarno Q akwizycji przesuwało w śledzeniu także szynę
  dla przypiętego `KQ` przy `KT` powyżej 100 s. Weszły też jego mniejsze
  uwagi: flagi animacji czytane ponownie po czekaniu, HT16K33 ustawiany od
  nowa przy każdym odświeżeniu, dokładny test dzielnika APB1, wiersz HT16K33
  w hostcheck, sprawdzenia wygasającego impulsu i poprawione zdanie o nim,
  `tools/carrier.py` przy 96 MHz.
- Na płytce, do potwierdzenia: przez survey-in i rozgrzewkę wskaźnik `CPU`
  koło 28 % zamiast 55 % (`SW`: zadanie wyświetlacza koło 24 %), spinery tak
  samo płynne; `DAC` pokazuje `carrier 11719 Hz (timer 96.0 MHz)`; liczniki
  `LB` jak dotąd na płytce autora; noc przy `KT 100` jak w buildzie 66.

Build 66, `GPSDO v1.08.66rt`: algorytm 13 czyta detektor poniżej okna, daje
odczytowi tym mniejszą wagę, im dalej od zera leży, nie bierze fazy z odczytu,
który detektor tylko powtórzył, i uzbraja według zakresu, który potrafi
odczytać. Oba starty buildu 65 z 07.10 (11:49 i 17:33) zaczęły się na
częstotliwości, od punktu pracy zachowanego w live store — poprawki buildu 65
zrobiły swoje — a mimo to doszły do locka po 17 i 22 minutach, gdy start na
częstotliwości powinien trwać kilka. Żadnego z nich nie pokazał symulator:
losował lądowanie każdego uzbrojenia i czytał detektor jak prostą, tak samo
zaszumioną na brzegach jak przy zerze, ze świeżym odczytem co sekundę.
`tools/loopsim` modeluje teraz dzielnik, rampę i odczyty tej płytki; z nimi
build 65 nie dochodzi w 46 z 264 zimnych startów, przy nawet 78 uzbrojeniach,
a build 66 dochodzi w każdym, przy jednym. W symulacji pętla w locku jest co
do cyfry ta sama, szarpnięcie po restarcie też.

### Naprawione (build 66)

- **Algorytm 13 czyta rampę poniżej okna.** Odczyt detektora liczy się jako
  faza tylko w granicach 0,55 rozpiętości z `LC` po obu stronach zera. Powyżej
  zera to słuszne: krzywa RC spłaszcza się ku napięciu zasilania i odczyt tam
  może oznaczać kilka różnych faz. Poniżej zera krzywa jest najbardziej stroma
  — odczyt zawyża fazę (o 17:33 spadł o 538 ns, gdy PWM, scałkowany, przesunął
  fazę o 243), ale pozostaje jednoznaczny aż do miejsca, w którym impuls się
  zawija, a odczyt skacze na górę. I właśnie poniżej zera ląduje dzielnik tej
  płytki. Uzbrojenie może przesunąć zbocze dzielnika tylko o całe kroki jego
  własnego zegara, więc lądowania wypadają w jednym oknie szerokości ok.
  450 ns (od −830 do −375 ns prawdziwej fazy na tej płytce), a ponowne
  uzbrojenie zsynchronizowanego już dzielnika zostawia go tam, gdzie był. O
  11:49 odczyt po pierwszym lądowaniu wyniósł −1070 ns, poniżej okna, a przy
  oscylatorze na częstotliwości każde ponowne uzbrojenie lądowało w tym samym
  miejscu (−1106 → −1103, −1099 → −1099, −1117 → −1118): cztery uzbrojenia i
  800 s `NOPH`, aż faza zeszła na następny krok (−438). Algorytm 13 bierze
  teraz także odczyt poniżej okna, do 0,9 rozpiętości poniżej zera (0,445 V na
  tej płytce) i nigdy poniżej 0,15 V (ostatni odczyt przed zawinięciem impulsu
  o 17:33 to 0,114 V), jako piętnaście razy bardziej zaszumiony niż odczyt
  przy zerze (następny punkt). Wyświetlacz i pozostałe algorytmy trzymają się
  okna.
- **Odczyt bliżej brzegów liczy się mniej.** R filtru to szum detektora przy
  zerze, gdzie filtr spędza życie — 1,2 ns na tej płytce. Bliżej brzegów ten
  sam detektor szumiał ~11 ns przy −900 ns i 10–17 ns poniżej okna, a
  prostoliniowy odczyt myli się tam do tego nawet o jedną trzecią. O 17:33
  uzbrojenie wylądowało przy −909 ns, pierwsze dwa odczyty różniły się o
  19 ns, a przy liczniku jeszcze wygaszonym po resecie częstotliwość była
  zupełnie otwarta: filtr wziął tę parę za nachylenie 16,7 ns/s, pętla pognała
  za nim PWM o 519 LSB (0,17 Hz) i faza zjechała z rampy. Teraz dalej niż
  ćwierć pasma od zera — i nigdy wewnątrz `LAT`, więc pętla w locku jest
  ważona dokładnie jak dotąd na każdym detektorze — odczyt jest brany za tym
  bardziej zaszumiony, im dalej leży: dziesięć razy na brzegu pasma,
  piętnaście poniżej okna. Odtworzone bez tego zimne starty płytki nadal
  dochodzą, ale niektóre po ~1100 s, przy dwóch uzbrojeniach i z PWM 1084 LSB
  za punktem pracy.
- **Powtórzony odczyt nie jest nowym.** Gdy serii nie da się odczytać przy
  zboczu, firmware publikuje poprzedni odczyt jeszcze raz, do trzech sekund z
  rzędu, i robi to samo przez sekundę, gdy jego bramka odrzutów wstrzymuje
  skok: przy locku co tysięczna sekunda, ale 30–38 % sekund w trakcie
  dociągania fazy po lądowaniu (07.10, oba zapisy). Algorytm 13 brał każdy za
  świeży — drugi pomiar starej fazy — a przy dociąganiu, gdy faza przesuwa się
  o kilka ns/s, odczyt stał, a potem skakał, i bramka 4σ odrzucała skoki: 35 i
  57 odrzutów w obu dociąganiach, odczyt zamrożony na −548,7 ns przez cztery
  sekundy. Teraz powtórzona sekunda nie daje filtrowi fazy: bez aktualizacji,
  bez lądowania, bez oceny R, a w teście śledzenia tylko ruch, który ta
  sekunda powinna była przynieść. Nie jest też przerwą — detektor nie zamilkł
  — więc werdykt locka i 20 minut live store biegną dalej; więcej niż cztery z
  rzędu, więcej niż firmware powtarza za jednym razem (trzy nieodczytane serie
  i jeden wstrzymany skok), to już przerwa. Trend pokazuje `KEPT`. Odtworzone
  na modelu płytki: odczytów odrzuconych przed pierwszym lockiem jest w
  medianie 2 zamiast 26 (najwięcej 42 zamiast 230); bez tego punktu byłoby 15
  (61).
- **Bramka uzbrojenia patrzy na zakres, który pętla czyta.** Gdy już widziała
  lądowanie, bramka przepuszcza następne uzbrojenie tylko wtedy, gdy to
  lądowanie, przeniesione o jeden horyzont wzdłuż zmierzonego dryfu, pozostaje
  czytelne — i chciała lądowania w granicach ±pół pasma. O 17:33, po lądowaniu
  przy −909 ns, odmawiała każdego uzbrojenia przez 1000 s, aż do ślepej
  ucieczki (obejścia bramki po dziesięciu horyzontach bez fazy). Poniżej zera
  zakres sięga teraz do podłogi ogona. W odtworzeniach nic to nie zmienia —
  przy czytanym ogonie build 66 nie uzbraja ponownie — ale bramka, która nie
  zgadza się z pętlą co do tego, co da się odczytać, to zakleszczenie
  czekające na swoją chwilę.

### Zmienione (build 66)

- **Trend algorytmu 13 może pokazać `KEPT`**: detektor powtórzył poprzedni
  odczyt i filtr nie wziął z niego fazy (wyżej). Przy locku mniej więcej co
  tysięczna sekunda, w trakcie dociągania co trzecia; werdykt locka się na nim
  nie zmienia. `tools/gpsdo_tuner.py` podaje jego znaczenie przy etykiecie
  stanu.
- `tools/loopsim`: dzielnik, rampa i odczyty tej płytki, każde jako pokrętło —
  `LOOPSIM_RAMP=<Vsat>` (detektor jako ładowanie RC: poniżej zera odczyt
  wyprzedza fazę, a za własną szerokością zera impuls się zawija i czyta
  górę), `LOOPSIM_LANDQ=<L>:<Q>` (uzbrojenie ląduje całymi krokami zegara
  dzielnika, od miejsca, w którym była faza), `LOOPSIM_EDGEN=<ns>` (szum
  rosnący z odległością od zera) oraz `LOOPSIM_KEPT`, `LOOPSIM_KEPT0=<p>`
  (sekundy publikujące poprzedni odczyt jeszcze raz; przy włączonym
  którymkolwiek pokrętle modelu płytki skok wstrzymany przez bramkę odrzutów
  też się do nich liczy). Przy wszystkich wyłączonych harness odtwarza
  przebiegi buildu 65 co do sekundy. `coldstart.sh` uruchamia trzeci blok z
  wartościami tej płytki (3,15 V, 380:450 ns, 12 ns, 0,35).
- Instrukcja: algorytm 13 nazywa sekundę bez fazy `NOPH` od buildu 51, a
  instrukcja wciąż w czterech miejscach mówiła `HOLD` — poprawione, `KEPT`
  dopisany do listy trendów, a w Aneksie D.4 trzy nowe punkty (poniżej okna,
  brzegi, powtórzony odczyt). Linia firmware wymienia build 66.

### Weryfikacja (build 66)

- hostcheck czysty we wszystkich 18 konfiguracjach (arm-none-eabi, cortex-m4);
  przy `-Wall -Wextra -Wshadow -Wdouble-promotion` na wszystkich jednostkach
  te same 182 ostrzeżenia co w buildzie 65.
- `tools/loopsim/coldstart.sh`, build 65 wobec buildu 66: dwa bloki z
  losowanym lądowaniem (04.10, 07.10) wychodzą jak dotąd z dokładnością do
  dwóch sekund — 149–483 s, przy jednym uzbrojeniu; w trzecim, na dzielniku,
  rampie i odczytach tej płytki, build 65 nie dochodzi w 7 z 33 startów (77
  uzbrojeń), a w jeszcze jednym dochodzi po 10,4 h, build 66 dochodzi w każdym
  w 263–587 s, przy jednym uzbrojeniu.
- Poza drzewem ten sam model płytki, po 24 ziarna dla odejść 0, ±300, ±800,
  ±1498, ±3000 i ±6000 LSB: build 65 nie dochodzi w 46 z 264 startów, przy
  nawet 78 uzbrojeniach; build 66 dochodzi we wszystkich 264, mediana
  281–510 s zależnie od odejścia, najdłużej 587 s, przy jednym uzbrojeniu,
  pierwszy lock w medianie po 261–495 s. PWM nigdy nie wychodzi więcej niż
  340 LSB za punkt pracy (znany dołek tuż po locku, bez zmian; build 65 do
  736). Części osobno, 24 ziarna przy pięciu odejściach: bez ogona 27 ze 120
  nie dochodzi (78 uzbrojeń); bez ważenia brzegów dochodzą wszystkie, ale
  nawet po 1125 s, przy dwóch uzbrojeniach i z PWM 1084 LSB za punktem pracy;
  z powtórzonymi odczytami branymi za świeże czasy zostają, odrzuty nie
  (wyżej); ze starą bramką uzbrojenia nic się nie zmienia.
- Przy K z `CT` 0,5, 0,7, 1,4 i 2 razy K obiektu (model płytki, dwanaście
  ziaren przy ośmiu odejściach): build 66 dochodzi we wszystkich 384, przy
  jednym uzbrojeniu, najdłużej 911 s (K × 2); build 65 nie dochodzi w jednym
  (K × 2, −6000 LSB, 75 uzbrojeń) i dochodzi nawet po 1750 s (K × 0,5, trzy
  uzbrojenia). Przy K zawyżonym build 66 jest wolniejszy przy każdym odejściu
  — przy K × 2 nawet 1,7 raza (−300 LSB: 562 s w medianie wobec 324) — a przy
  zaniżonym przy większości ujemnych.
- Cena, na płytce 3000 ns harnessu, której szyna przez bramkę odrzutów czyta
  się w paśmie: start z szyny 300 LSB obok dochodzi w medianie po 466 s wobec
  396 (najdłużej 596 wobec 429, do trzech uzbrojeń), start 1500 ns obok po
  442 s wobec 397; mediany pozostałych startów przesuwają się najwyżej o 18 s,
  najdłuższe nawet o 72 s. Całość to ważenie brzegów: bez niego liczby są co
  do sekundy te z buildu 65. Na modelu tej płytki starty na częstotliwości
  dochodzą po 263–277 s wobec 217–226 buildu 65 (trzy ziarna `coldstart.sh`;
  na 24 ziarnach mediana 281 s wobec 248, a 4 starty buildu 65 nie dochodzą).
- Zamrożony detektor, przy pętli wołanej jak na płytce: wewnątrz okna (+300 i
  +1295 ns, na częstotliwości i obok) jak dotąd, 2,5–12 mHz rms w drugiej
  połowie; zamrożony tuż poniżej (−1700 ns na płytce 3000 ns, gdzie build 65 w
  ogóle go nie czytał i trzymał częstotliwość w 0,2–0,5 mHz rms) build 66
  czyta teraz jak każdy zamrożony odczyt wewnątrz i wypada gorzej niż każdy z
  nich — 14,5–15,4 mHz rms, sd fazy 650–700 ns: koszt przyjęty świadomie.
  Szyny zostają poza ogonem: górna leży nad zerem, 0 V pod podłogą.
- Ani jednej sekundy `NOPH` po pierwszych 2000 s w 72 przebiegach po 12,5 h,
  przy lądowaniu z 07.10 i na modelu płytki (build 65 na modelu płytki: cztery
  przebiegi, które nie doszły, po 43 092 s każdy).
- `bench.py run --hw`: algorytm 13 2,40 ns, część wolna 0,88 ns, ADEV 4,08e-12
  przy 1024 s, 1468 zmian PWM na godzinę — liczby buildu 65 co do cyfry, tak
  samo algorytmy 11 i 12 oraz cztery starsze obiekty, z wyjściem precyzyjnym i
  16-bitowym. Szarpnięcie po restarcie w scenariuszu buildu 54: 9 / 28 / 9 /
  22 / 9 LSB (16 bitów 9 / 28 / 9 / 21 / 9; z pętlą ruszającą jak na płytce
  19 / 14 / 19 / 21 / 19), bez zmian.
- `tools/algoswitch`, `lticsim`, `railwatch` (17/17), `livestore` (49/49 i
  3/3), `flashring` i `spansim` drukują dokładnie to, co dla buildu 65.
- Stos: `kalman_ctl()` 608 bajtów (było 584); najgłębszą ścieżką zadania
  Control jest nadal zapis ringu (1728 bajtów), ścieżka pętli to 736. Flash
  +0,9 kB, RAM statyczny +12 bajtów.
- Niezależny przegląd kodu nie znalazł błędu; jego sugestie weszły: powtórzona
  sekunda nadal dolicza w teście śledzenia oczekiwany ruch, więcej niż cztery
  powtórzone sekundy z rzędu liczą się jako przerwa, test ogona czyta
  dokładnie to napięcie, z którego wyszła faza (odczyt publikuje inne
  zadanie), ważenie brzegów nigdy nie zaczyna się wewnątrz `LAT`, `KEPT` stał
  się słowem trendu, a `coldstart.sh` nie wpuszcza `LOOPSIM_KEPT0` z powłoki
  do pierwszych dwóch bloków.
- Na płytce, do potwierdzenia: zimny start i reset programowy prosto w `LA 13`
  — po jednym uzbrojeniu i lock w ciągu dziesięciu minut, bez `NOPH` po
  uzbrojeniu poza czterema celowymi sekundami; w trakcie dociągania `KEPT`
  mniej więcej co trzecią sekundę i prawie żadnego `REJ`; lądowanie poniżej
  okna (odczyt poniżej ~1,0 V w logu) dociągnięte, a nie ponownie uzbrojone; w
  locku `KEPT` mniej więcej raz na tysiąc sekund; `SW` nadal z zapasem na
  stosie Control.

Build 65, `GPSDO v1.08.65rt`: trzy poprawki startu daleko od punktu pracy —
dwie znalezione przy pierwszym uruchomieniu buildu 64 na płytce (04.10),
trzecia przy pierwszym starcie samego buildu 65 (07.10); punkt pracy, razem ze
spanem, do którego należy, przechowywany w live store; ring we flashu, który
po zapełnieniu nie gubi już ustawień (przy zapisie punktu pracy zdarzałoby się
to co kilka miesięcy) i do którego pisze naraz tylko jedno zadanie; zapisy
ustawień, które już się nie znoszą, i `CR`, które kasuje raz; `LC`, które
zostawia kod pętli tam, gdzie go zastało; ostrzeżenie, gdy pętla przechodzi od
jednego końca zakresu DAC do drugiego; oraz rozładowanie kondensatora
detektora opisane zgodnie z tym, czym jest. Poprawka z buildu 64 przetrwała
jego pierwszą noc (23:30–08:00, algorytm 13, 47,3–48,2 °C): licznik wygięć w
`LB` prawie stał — 209 odrzuceń na 56 726 s od startu, a zamrożone odczyty w
logu sytuują je w pierwszych minutach po starcie — i tylko co tysięczna
sekunda publikowała wstrzymany odczyt, gdy w nocy buildu 63 co dziewiąta. To
najlepsza noc w historii tej płytki: sd fazy 1,64 ns, biały szum 0,94 ns,
część wolna 0,90 ns, R 1,32, ADEV 1,6e-9 przy 1 s, 2,8e-12 przy 1024 s i
7,0e-13 przy 4096 s (build 63: 1,77, 1,15, —, 1,51, 1,9e-9, 3,0e-12, —), PWM
zmieniało się 1316 razy na godzinę (1525), największy krok z sekundy na
sekundę 6,2 ns, żadnego kroku powyżej 10 ns i żadnego odczytu odrzuconego
przez filtr Kalmana.

### Naprawione (build 65)

- **Algorytm 13 dociąga z daleka, zamiast huśtać się od szyny do szyny.**
  Pierwszy start buildu 64 wstał na PWM 42429, 1469 LSB (0,47 Hz) od punktu
  pracy płytki (~40960 — kod trzymany w live store był stary, patrz niżej), z
  jeszcze niezsynchronizowanym picDIV, a `LA 13` wpisano w trakcie survey-in:
  dociąganie zostało w całości po stronie algorytmu 13, na samym TIM2.
  Rozhuśtał PWM od szyny do szyny, 1 ↔ 65535 i −12,8 ↔ +8,4 Hz, z okresem
  dziesięciu minut, przez półtorej godziny, i ani razu nie zbliżył się
  częstotliwością na tyle, żeby uzbroić dzielnik. Przyczyna to ograniczenie
  tempa zmian korekty sekundowej, obecne od v1.07, żeby restart nie szarpał
  oscylatorem: trzymało ono hamowanie tak samo jak przyspieszanie. W logu
  korekta zmienia się co sekundę o ~5 LSB przez całe huśtanie (limit: 516 LSB
  na 100-sekundowy horyzont akwizycji), więc PWM rozpędzony do kilkuset LSB na
  sekundę nie umiał się zatrzymać i przestrzeliwał mniej więcej o całą drogę,
  którą miał przejść. Przy małym odejściu człony fazy i TIM2 to tłumią; przy
  dużym każde wahnięcie przerastało poprzednie. Ograniczenie trzyma teraz
  tylko narastanie od zera (i zawrócenie, które przechodzi przez zero);
  korekta może maleć tak szybko, jak chce filtr. Odtworzone na liczbach tej
  płytki — detektor na szynie, licznik liczący tak, jak liczy, bramka odrzutów
  firmware, pętla ruszająca dopiero przy pełnej średniej 100 s, tak jak rusza
  na płytce, dzielnik lądujący mniej więcej tam, gdzie wylądował 04.10
  (−449 ns; symulacja losuje −450 ± 150 ns) — build 64 zawodzi od ±800 LSB
  przy każdym ziarnie i przy ±300 przy niektórych; z tą poprawką dochodzi
  każdy start do ±6000 LSB (±1,9 Hz), ale z daleka powoli i przy nawet sześciu
  uzbrojeniach: szybki robi go dopiero następny punkt. Pętla w locku bez
  zmian, tak samo szarpnięcie po restarcie, przeciw któremu ograniczenie
  wprowadzono (patrz Weryfikacja). Wcześniej się to nie pokazało, bo płytka
  zwykle startuje na algorytmie 11 i jest przełączana na 13 po locku, jak
  przed tą nocą.
- **Algorytm 13 nie steruje już fazą, której nikt nie zmierzył, a nieświeża
  średnia nie skazuje jego detektora.** Pierwszy start samego buildu 65
  (07.10: reset programowy prosto w algorytm 13 na starym 42429, 1498 LSB —
  0,48 Hz — od punktu pracy, detektor na szynie) nie złapał locka do `CT`
  uruchomionego 14 minut później: cztery uzbrojenia, do 37,7 % odczytów
  odrzuconych, a od ~540 s już tylko `NOPH`, choć detektor czytał w paśmie.
  Odtworzenie wyżej tego nie pokazało, bo wołało pętlę od pierwszej sekundy, a
  płytka woła ją dopiero przy pełnej średniej 100 s; na tamtej ścieżce filtr
  bierze częstotliwość z wygładzanego odczytu jednosekundowego i uzbrajał już
  w ~14 s, zanim wygładzanie nadążyło — uzbrojenie, którego płytka nigdy nie
  robi. Odtworzony tak, jak płytka go wykonuje, poranek wychodzi niemal co do
  sekundy (najniższe PWM 38377 w 263 s wobec 38395 w 264, pierwsze uzbrojenie
  w 273 s wobec 273, 100 odrzutów do 400 s wobec 99, drugie uzbrojenie w 410 s
  wobec 409), a dochodzi dopiero po ~41 minutach; od +800 LSB tak samo kończą
  dwa starty na dwanaście. Cztery błędy w łańcuchu. Przy detektorze na szynie
  filtr niósł fazę zbudowaną z własnej oceny częstotliwości — pierwsze odczyty
  licznika wpisały w nią od −3 do −7 µs — i zerował ją w horyzoncie, pędząc
  oscylator 0,8 Hz w drugą stronę (PWM 42429 → 38395 przy punkcie pracy
  40931). Bramka uzbrojenia patrzyła na średnią 100 s, która przeszła przez
  zero, gdy oscylator był jeszcze 0,77 Hz obok, i uzbroiła w fazę przelatującą
  z prędkością 77 ns/s. Test śledzenia porównywał detektor z tą samą nieświeżą
  średnią — oczekiwał +1269 ns w oknie, w którym faza przesunęła się o +189 —
  i po trzech takich oknach odebrał głos zdrowemu detektorowi na pół godziny,
  jakie trwa wygaśnięcie wyroku; kolejne uzbrojenia nie umiały tego zaufania
  oddać. A odczyty licznika były przewidywane z opóźnieniem połowy zmiany, co
  jest prawdą tylko dla prostej rampy. Teraz: do pierwszego odczytu detektora
  po resecie albo uzbrojeniu, albo po 10 s bez odczytu, filtr nie niesie fazy,
  więc sterowanie jest czysto częstotliwościowe, dopóki fazy nie ma; bez fazy
  odczyt licznika odrzucony dziesięć razy z rzędu jest przyjmowany, a nie
  przycinany, a wariancja częstotliwości nie może deklarować więcej, niż
  pozwala K z `CT`; średnia 100 s jest przenoszona do bieżącej sekundy,
  dokładnie, o poprawki zaksięgowane przez pętlę — dla aktualizacji z
  licznika, dla testu śledzenia (zaokrąglone do kroku licznika, 0,01 Hz) i dla
  bramki uzbrojenia (która musi przejść z ćwiercią tej poprawki w każdą stronę
  — K o ćwierć obok); a odczyt, który skacze przy ponownej synchronizacji
  dzielnika, oddaje zaufanie, czego zamrożony detektor nie potrafi.
  Odtworzone: każdy start od ±300 do ±6000 LSB dochodzi w 327–480 s (mediana),
  najdłużej w 543 s, przy najwyżej dwóch uzbrojeniach (wcześniej 342–3305 s,
  najdłużej 4298 s, do sześciu uzbrojeń); poranny start płytki dochodzi w
  429 s przy jednym uzbrojeniu, a jego PWM zatrzymuje się w drodze 5 LSB od
  punktu pracy, zamiast przelecieć 2500 za niego; przy K z `CT` od połowy do
  dwukrotności K obiektu dochodzi każdy start (najdłużej 1462 s wobec
  11 177 s). Zamrożony detektor trzyma częstotliwość lepiej, nie gorzej
  (2,5–12 mHz rms wobec 6–26), pętla w locku jest taka sama z dokładnością do
  setnej nanosekundy, szarpnięcie po restarcie także.
- **Wygięcia własnego płytki firmware uczy się tylko z serii, które czytają
  rampę.** Build 64 uśredniał wygięcie każdej serii wziętej na zboczu, gdzie
  by nie leżała. Detektor oparty o szynę to nie rampa: seria spada o ~160
  kodów w pierwszych 50 µs, a potem leży płasko, co daje „wygięcie” ok. +70
  kodów. Przez pierwsze uruchomienie buildu 64 (2,7 h na szynie) typowe
  wygięcie wspięło się do +20 i za każdym razem, gdy faza przechodziła przez
  okno, własne serie płytki (−3 do −4) były odrzucane jako „bent unlike the
  board's bursts”: odczyt stał zamrożony do 7 s naraz i był publikowany jako
  ważny. Start nocnego uruchomienia pokazał to samo w miniaturze — 150 s na
  szynie przed pierwszym uzbrojeniem, potem odczyty zamrożone po 3–4 s przez
  kolejne siedem minut. Teraz seria liczy się tylko wtedy, gdy jej prosta
  wypada w paśmie zmierzonym przez `LC` (zero ± 0,55 przemiatanego zakresu,
  okno, w którym faza jest uznawana za ważną; bez `LC` pomijane są tylko
  szyny).
- **Kod zapisany pod niewłaściwym spanem jest przeliczany ze spanu, w którym
  naprawdę jest.** Rekord spanu (ustawienia) i kod (live store) to dwa zapisy,
  a start przeliczał przywołany kod ze spanu, który podaje REKORD. Są dwa
  sposoby, żeby się rozjechały, oba odegrane w `tools/spansim` na płytce z
  obiema pozycjami skalibrowanymi: zanik zasilania między zapisami po ruchu
  zworki (rekord mówi nowy span, a kod jest jeszcze starego) i nieudany zapis
  samego rekordu przy ruchu (rekord mówi stary span, a kod jest już nowego).
  Build 64 startował w pierwszym przypadku 1,2e-7 obok częstotliwości — kod
  starego spanu na nowej ścieżce — z 23 µs fazy do odrobienia i lockiem po
  1112 s, a w drugim przeliczał kod, który przeliczenia nie potrzebował: do
  −11785, obciętego do 1, 9,4e-8 obok, 41 µs fazy, 1483 s. Slot live store
  mówi teraz, w którym spanie jest jego kod, i start przelicza od tego: build
  65 startuje w obu przypadkach przy −9,7e-13 i −3,0e-13. Slot zapisany przez
  build 64 lub starszy takiego bajtu nie ma i start traktuje go dokładnie tak
  jak dotąd.
- **Zapełniony ring we flashu nie kasuje już ustawień.** Ustawienia (`ES`, K z
  `CT`, rekord spanu), rekord Kalmana (KR/KQ/KT) i dane live dzielą jeden
  sektor z wyrównywaniem zużycia, a gdy jego 255 slotów się zapełniło, ring
  kasował sektor i zapisywał z powrotem tylko slot, który go zapełnił.
  Najnowszy slot każdego innego typu znikał razem ze starymi: następny start
  szedł na domyślnych — „Settings: none stored”, bez algorytmu, `CT` i
  KR/KQ/KT — albo, jeśli zapełniło go `ES`, bez kalibracji `LC`. Błąd pochodzi
  z czasu, gdy ustawienia trafiły do ringu, i pozostawał niewidoczny, bo ring
  zapełniał się długo: na algorytmach 11–13 pisały do niego tylko zapisy, `LC`
  i ruchy zworki spanu. Zapis punktu pracy (niżej) na dryfującej płytce
  doprowadzałby do zawinięcia w ciągu miesięcy. Teraz przy zawinięciu
  najnowszy slot każdego typu jest odczytywany przed kasowaniem i zapisywany z
  powrotem po nim, a dopiero potem nowy. Kasowanie, które się nie udało,
  nagłówek, który nie odczytuje się poprawnie, i kopia, która się nie
  zaprogramowała, dostają po jednej powtórce; zginąć mogą już tylko przy
  zaniku zasilania w trakcie kasowania albo w kilku milisekundach po nim, albo
  gdy flash zawiedzie dwa razy. Do tego slot, który nieudane programowanie
  zostawiło do połowy zapisany, jest teraz pomijany: do buildu 64 każdy
  następny zapis, `ES` też, wracał do niego i znów się nie udawał, aż do
  następnego startu.
- **Do ringu pisze naraz tylko jedno zadanie.** Do ringu piszą zadanie
  sterowania (live store, K z `CT`, rekord spanu) i CLI (`ES`, KR/KQ/KT,
  `CR`), a nic ich nie rozdzielało. Zapis live store wypadający w kilku
  milisekundach `ES` — zadanie sterowania wywłaszcza CLI między dwoma słowami
  slotu — kładł oba zapisy do jednego slotu: do buildu 64 każdy następny zapis
  nie udawał się aż do restartu. Skoro zepsuty slot jest teraz pomijany
  (wyżej), każde z zadań przeszłoby dalej, slot za nim zostałby pusty za
  najnowszym, a następny start zacząłby pisać od niego — po rekordach
  zapisanych później: rekord ustawień zapisany i odczytany poprawnie znikałby
  po kolejnym restarcie. W trakcie zawinięcia ta sama kolizja mogła cofnąć
  przenoszenie rekordów. Teraz zapis, odczyt i kasowanie ringu biorą mutex, a
  start kontynuuje za ostatnim slotem, który przyjął jakiekolwiek bity, a nie
  od pierwszego pustego. Sam wyścig jest starszy niż build 65; znalazł go
  drugi niezależny przegląd tego buildu, wstrzykując drugie zadanie piszące
  przy każdej operacji flasha jednego zapisu.
- **`LC` odkłada kod pętli tam, gdzie go zastało.** Trzy wyjścia z `LC`
  przywracały pin, ale nie `gCtrl.pwm_output`, w którym przemiatanie trzyma
  dla wyświetlaczy własny kod. Pętla rusza od tej wartości (a jej ścieżka
  ułamkowa od pinu tylko wtedy, gdy oba zgadzają się co do LSB), więc po
  każdym `LC` pierwszy krok pętli wracał na kod przemiatania — 0,06 Hz obok,
  ~190 LSB na płytce autora — a zapis, o który prosi udane `LC`, utrwalał ten
  kod jako punkt pracy. Teraz każde wyjście przywraca oba, razem z ułamkiem,
  jak nieudane `CT`. Błąd sprzed buildu 65; znalazł go ten sam przegląd.
- **Dwa zapisy ustawień naraz już się nie znoszą.** Zapis częściowy (`ES PID`,
  `ES TZ` i pozostałe, autozapisy, ruch zworki spanu, zapis po `CT`) czyta
  najnowszy rekord ustawień, zmienia swoją grupę i zapisuje cały rekord z
  powrotem, a zapisują i CLI, i zadanie sterowania. Przeplecione, drugie
  zapisanie powstawało z rekordu, którego pierwsze jeszcze nie zastąpiło, i
  grupa pierwszego wracała do poprzedniej wartości — ruch zworki w trakcie
  `ES PID` zostawiał we flashu stare PID. Blokada ringu robi całym każdy
  odczyt i każdy zapis z osobna, nie ich parę; teraz własna blokada magazynu
  ustawień robi z każdego zapisu ustawień jeden odczyt i jeden zapis. Błąd
  sprzed buildu 65 (TODO 159 (l)).
- **`CR` kasuje raz, zachowuje licznik kasowań i mówi, kiedy się udało.**
  Czyszczenie kasowało sektor, a formatowanie po nim nie znajdowało nagłówka,
  brało ring za obcy i kasowało go jeszcze raz, licząc kasowania od 1 — a
  ponieważ świeży ring nie ma danych, czyszczenie zgłaszało porażkę: każde
  `CR` pisało „ring erase FAILED (flash locked?)”. Teraz jedno kasowanie i
  świeży nagłówek, licznik idzie dalej (`EW` liczy dalej), a komunikat mówi,
  że ring został wyczyszczony. Błąd sprzed buildu 65.

### Zmienione (build 65)

- **Live store przechowuje punkt pracy.** Do buildu 64 kod był zapisywany
  tylko razem z wyuczonym dryfem albo tłumieniem, a algorytmy 11–13 tych
  wartości nie ruszają: ring autora trzymał 42429 przez tygodnie, gdy pętla
  pracowała przy ~40960, więc każdy zimny start zaczynał 0,47 Hz obok — i stąd
  pierwszy start buildu 64 w ogóle poprosił algorytm 13 o długie dociąganie.
  Teraz kod jest zapisywany sam, gdy odszedł o ponad 0,02 Hz od zapisanego —
  przez K z `CT` to 62 LSB na płytce autora i ~818 na spanie REDUCED shiftera
  V3, gdzie stałe 64 LSB (1,6e-10) dawałyby zapis mniej więcej raz dziennie;
  64 LSB na płytce bez `CT` — a pętla jest w locku od 20 minut bez przerwy i
  kodu nie ruszało nic poza nią. Werdykt locka pętli zmienia się dopiero przy
  jej następnym kroku, więc każde z tych zdarzeń liczy 20 minut od nowa: kod,
  który między dwoma tyknięciami co 200 ms przesunął się o ponad 256 LSB; kod
  wpisany przez `SP`, `up` albo `dp`, choćby o jeden LSB (CLI zgłasza to live
  store); i każdy odcinek, który zadanie sterowania spędziło gdzie indziej —
  `C`, `CT` i `LC` działają na nim przez minuty, niewidoczne dla tyknięć live
  store. Nigdy w holdoverze, nigdy w trakcie ruchu zworki spanu (debounce,
  zapis, kotwica albo oczekujące CT) i nie częściej niż co 20 minut, jak
  pozostałe wartości live. Algorytmy 0–9 nie mają locka i w ten sposób kodu
  nie zapisują. Slot rośnie z 27 do 28 bajtów o bajt spanu. Nieudany zapis
  jest ponawiany po 20 minutach, a nie przy każdym tyknięciu co 200 ms jak
  dotąd, a zamówiony zapis (po `LC`, po ruchu zworki) czeka na niego, zamiast
  przepaść. Slot pakowany, gdy mutex sterowania był zajęty dłużej niż 50 ms,
  nie jest w tym tyknięciu zapisywany: do buildu 64 szedł z kodem domyślnym,
  32767, w miejscu kodu pętli.
- **Pętla idąca od szyny do szyny mówi o tym.** Huśtanie buildu 64 trwało
  półtorej godziny i nic nie mówiło, że to huśtanie: `NOPH` i PWM akurat tam,
  gdzie był w danej sekundzie. Gdy kod, który pętla wystawia na DAC, dochodzi
  do jednego końca zakresu, a potem do drugiego, bez locka pomiędzy, każde
  takie przejście jest teraz raz zgłaszane na konsoli — `LOOP: rail to rail
  with no lock - PWM 65483 -> 52 in 181 s (swing 1, algorithm 13): diverging,
  not pulling in. LA 11 pulls in from far off.` (przy algorytmie 11 kończy się
  „Check CT and LPOL.”, bez detektora „Check CT.”) — a linia telemetrii
  pokazuje `RAILS` w miejscu słowa trendu, dopóki pętla nie złapie locka, nie
  zmieni się algorytm, kodu nie przestawi coś innego niż pętla albo nie minie
  30 minut bez dotknięcia któregoś końca. Patrzy się na to, dokąd idzie kod,
  nie jak szybko: uczciwe dociąganie z daleka robi duże korekty tak długo, jak
  trzeba, i może minutami stać przy jednym końcu, ale do drugiego nie
  dochodzi. Liczą się tylko kroki samej pętli: kod postawiony między dwoma
  krokami przez `SP`, `CT`, `LC` albo ruch zworki zaczyna obserwację od nowa,
  więc `SP 30`, a potem `SP 65500` to nie przejście. To tylko komunikat;
  decyzja należy do operatora.
- **Kondensator detektora jest rozładowywany po każdym odczycie, przy obu
  detektorach — i komentarze oraz dokumentacja mówią teraz to samo.**
  `GPSDO_LTIC_ACTIVE_RESET` było włączone w dostarczanej konfiguracji, a jego
  komentarz, `gpsdo_tasks.cpp` i instrukcja mówiły, żeby dla klasycznego
  detektora RC Kaashoeka zostawić je wyłączone, bo ten „czyści się sam przez
  upływ”, a ściąganie PA1 do masy „psuje mierzony ładunek”. To drugie nigdy
  nie dotyczyło rozładowania zrobionego po odczytaniu serii (~0,2 ms po
  zboczu; impuls trwa ok. mikrosekundy przy locku i najwyżej ~3 µs w paśmie).
  To pierwsze jest prawdą tylko dopóty, dopóki upływ jest szybki: na płytce
  bez rezystora upływowego (na płytce autora go nie ma; oryginał Larsa z
  HC4046 rozładowuje kondensator przez 1 MΩ) upływ to najpewniej prąd wsteczny
  diody Schottky — kilkadziesiąt ms w 48 °C (1,4–1,5 ms, które `LB` pokazuje
  jako „decay”, to głównie pobór samego przetwornika) — a ten na zimnej płytce
  maleje kilkukrotnie i mógłby zostawić część ładunku z jednej sekundy pod
  odczytem następnej. Kod bez zmian; komentarze, tabela przełączników i część
  3.3 instrukcji oraz notatki o detektorze w README zalecają teraz
  rozładowanie przy obu wariantach.
- `tools/loopsim`: `coldstart.sh` uruchamia zimne starty algorytmu 13 z
  detektorem na szynie przy odejściach do ±6000 LSB na liczbach danej płytki i
  zgłasza błąd, jeśli któryś nie dochodzi (z drugim drzewem drukuje oba). Nowe
  pokrętła: `LOOPSIM_LNV`, `LOOPSIM_LZO`, `LOOPSIM_LRN`, `LOOPSIM_KP7` (`LC` i
  `CT` płytki), `LOOPSIM_CNT=1` (licznik przenosi ułamek z bramki na bramkę;
  domyślne zaokrąglanie poniżej pół herca czyta dokładnie zero) i
  `LOOPSIM_GATE=1` (bramka odrzutów firmware). Od pierwszego startu samego
  buildu 65 woła też pętlę dopiero przy pełnej średniej 100 s, tak jak zadanie
  sterowania (`LOOPSIM_FULL100=1`, nowe — bez tego algorytm 13 uzbrajał start
  0,47 Hz obok już w ~14 s, ścieżką, której płytka nigdy nie bierze), i gra
  oprócz lądowania z 04.10 także to z 07.10 (−665 ± 110 ns). Lądowanie
  dzielnika zdejmuje też szynę, która przy paśmie węższym niż ~2800 ns
  zostawała na zawsze.
- `tools/spansim`: slot live store niesie swój span tak jak w firmware, a nowy
  scenariusz odgrywa oba powyższe przypadki zaniku zasilania (`livestop`,
  `setfail`).
- Nowe: `tools/livestore` uruchamia `gpsdo_live_store.cpp` z firmware na ringu
  w pamięci, `tools/railwatch` — strażnika z `gpsdo_health.cpp`, a
  `tools/flashring` — rdzeń ringu (`gpsdo_flash_ring_core.c`) na sektorze w
  RAM, który zachowuje się jak flash NOR.
- `tools/lticsim`: harness uruchamia też `ltic_bend_track()`.
- `tools/gpsdo_tuner.py`: etykieta stanu zna `RAILS` — podaje jego znaczenie i
  robi się czerwona.
- Linia firmware w instrukcji podaje build, który opisuje (65; było nadal 59).

### Weryfikacja (build 65)

- hostcheck czysty we wszystkich 18 konfiguracjach (arm-none-eabi,
  cortex-m4); zmienione pliki bez nowych ostrzeżeń przy `-Wall -Wextra`.
- `tools/loopsim/coldstart.sh`, build 64 wobec buildu 65, trzy ziarna przy
  odejściach 0, ±300, ±800, ±1469, ±3000 i ±6000 LSB, pętla ruszająca przy
  pełnej średniej 100 s, dla lądowania z 04.10 i z 07.10: build 64 dochodzi z
  0, z ±300 w siedmiu startach na dwanaście i z niczego większego; build 65 z
  każdego, w 149–482 s i przy jednym uzbrojeniu. Build 65 sprzed poprawek z
  07.10 też dochodził z każdego, ale nawet w 3338 s i przy nawet sześciu
  uzbrojeniach.
- Szarpnięcie po restarcie, przeciw któremu ograniczenie wprowadzono w
  buildzie 54, w scenariuszu z buildu 54 (nocny plant z 03.09, pięć ziaren
  szumu, największy krok sekundowy w pierwszej minucie, LSB):

  | | s1 | s2 | s3 | s4 | s5 |
  |---|---|---|---|---|---|
  | przed buildem 54 | 48 | 78 | 175 | 120 | 16 |
  | build 64 | 13 | 27 | 9 | 19 | 10 |
  | build 65 | 9 | 28 | 9 | 22 | 9 |

  Build 64 odtwarza liczby z buildu 54 co do cyfry; build 65 nie jest ani
  lepszy, ani gorszy. Tak samo przy wyjściu 16-bitowym (`LOOPSIM_FINE=0`):
  13 / 27 / 9 / 19 / 11 wobec 9 / 28 / 9 / 21 / 9. Poprawki z 07.10 zostawiają
  te liczby bez zmian, przy obu wyjściach; przy pętli ruszającej z pełną
  średnią, jak na płytce, jej pierwsza minuta daje 19 / 14 / 19 / 21 / 19 LSB
  przed nimi i po nich.
- `bench.py run --hw` (scenariusz skalibrowany na płytce): algorytm 13
  2,40 ns, część wolna 0,88 ns, ADEV 4,08e-12 przy 1024 s, jak w buildzie 64,
  przy 1468 zmianach PWM na godzinę wobec 1475; algorytmy 11 i 12 identyczne.
  Na czterech starszych plantach średnie geometryczne algorytmu 13 zgodne w
  granicach 1 % (faza i ADEV w granicach 0,25 %); przy wyjściu 16-bitowym
  największy pojedynczy ruch DAC w całym przebiegu spada z 2,6 do 2,3 LSB.
- Poza drzewem, `loopsim` z pętlą ruszającą tak jak na płytce, na liczbach tej
  płytki, dla poprawek z 07.10: dwanaście ziaren na każde odejście od ±300 do
  ±6000 LSB, oba lądowania — liczby podane wyżej; przy K z `CT` równym 0,5;
  0,7; 0,8; 1; 1,25; 1,4 i 2 razy K obiektu dochodzi każdy start (mediana
  434–597 s wobec 541–926, najdłużej 1462 s wobec 11 177); zamrożony detektor
  (+300 i +1295 ns, na częstotliwości i obok) 2,5–12 mHz rms w drugiej połowie
  przebiegu wobec 6–26, odch. std. fazy 143–545 ns wobec 191–1794; na płytce z
  pasmem 3000 ns, której szyna przez bramkę odrzutów czyta się w paśmie,
  starty 0–300 LSB obok dochodzą w 391 s (mediana) wobec 482 (najdłużej 578 s
  wobec 4431), a starty 800–3000 LSB obok w 479 s, gdzie wcześniej 40 z 72 nie
  doszło wcale; ani jednej sekundy `NOPH` po pierwszych 2000 s w 72
  przebiegach po 12,5 h (szum biały i ten z `--hw`; wcześniej pięć, wszystkie
  ze startów przy +1498, które nie złapały locka). Części osobno: dokładne
  opóźnienie zastąpione połową zmiany zostawiło przy K 1,4 razy K obiektu 6 z
  60 startów bez locka; bez ograniczenia P11 przy K 0,5 i 0,7 razy mediana
  wyniosła 1352 i 788 s zamiast 597 i 454; bez zaufania oddawanego przy
  lądowaniu małe starty płytki 3000 ns trwały 2307 s (mediana), trzy z 72
  wcale; bez zaokrąglenia w teście śledzenia szumna noc straciła detektor na
  30 minut.
- `tools/lticsim/run.sh`: trzy nowe sprawdzenia — serie z szyny nie uczą
  wygięcia, serie z pasma nadal uczą, także po 600 s na szynie — 19/19 przy
  τ 5, 27 i 100 ms; ze śledzeniem z buildu 64 pierwsze i trzecie nie
  przechodzą. Liczby modelu identyczne jak w buildzie 64.
- `tools/spansim/run.sh`: każdy scenariusz sprzed tego buildu drukuje
  dokładnie to, co drukował build 64; nowy daje liczby podane wyżej.
- `tools/livestore/run.sh`: 52/52 — 27-bajtowy slot z buildu 64 ładuje się z
  nieznanym spanem; żaden kod nie jest zapisywany przed 20 minutami
  nieprzerwanego locka, po przerwie, przy 64 LSB, w holdoverze, w trakcie
  zmiany spanu, zaraz po `SP` (wciąż „w locku”), w trakcie kalibracji ani w 20
  minutach po niej (dwanaście minut bez tyknięcia, kod zostawiony 186 LSB
  obok), ani po `SP`, `up` czy `dp` zbyt małym dla testu skoku (także `up1`);
  65 LSB po 20 minutach — tak, jako 28-bajtowy slot z jego spanem; płytka bez
  żadnego slotu zapisuje pierwszy kod po locku; zapis odrzucony przez flash
  jest próbowany raz, a potem nie przez 20 minut, a zamówiony zapis czeka na
  niego, zamiast przepaść; przy zajętym mutexie sterowania nic nie jest
  zapisywane, a następne tyknięcie zapisuje kod pętli; próg idzie za K z `CT`
  — 62 LSB na płytce autora, 818 na spanie REDUCED, 64 LSB bez `CT`. Na kodzie
  sprzed drugiego przeglądu siedem ze sprawdzeń dodanych razem z nim nie
  przechodzi.
- `tools/railwatch/run.sh`: 17/17 — huśtanie z 04.10 jest zgłaszane przy
  pierwszym dojściu do drugiego końca i aż do locka; dociąganie stojące
  dziesięć minut przy jednym końcu, ten sam koniec dwa razy, lock między
  końcami, zmiana algorytmu, 30 minut między końcami, `SP 30`, a potem
  `SP 65500` przy pętli trzymającej kod, i przywrócenie kodu między końcami —
  wszystko cicho; po `SP` na jeden koniec własny bieg pętli na drugi jest
  zgłaszany, a `SP` kończy epizod. Odegrany na logu z 04.10 odezwałby się
  872 s po włączeniu i czternaście razy aż do `LA 11`; na 15,7 h nocy oraz na
  33 symulowanych zimnych startach buildu 65 (każde odejście z listy wyżej,
  trzy ziarna, liczone jako nigdy w locku) — ani razu; na dwudziestu startach
  buildu 64, które nie doszły (każde ziarno przy ±1469, ±3000 i ±6000, po
  jednym przy +800 i −800) — we wszystkich dwudziestu.
- `tools/flashring/run.sh` (nowe): rdzeń ringu z firmware na sektorze w RAM,
  który zachowuje się jak flash NOR — 1000 zapisów live przez trzy zawinięcia
  zachowuje jedyny rekord ustawień i Kalmana, restart odczytuje wszystkie
  trzy, zawinięcie zrobione przez `ES` zachowuje najnowszy rekord live, 8000
  losowych zapisów dowolnego typu i długości przez trzydzieści zawinięć z
  restartami pomiędzy zawsze odczytuje najnowszy rekord każdego typu, slot
  zepsuty nieudanym programowaniem jest pomijany, także ostatni, start
  kontynuuje za ostatnim zapisanym slotem, a nie od pustego zostawionego za
  nim, zawinięcie ponawia raz nieudane kasowanie, nieudany nagłówek (z błędem
  albo krótszy bez błędu) i nieudaną kopię, a czyszczenie `CR` kasuje raz i
  zachowuje licznik kasowań: 17/17. Rdzeń z buildu 64 nie przechodzi 13 z
  nich.
- Poza drzewem, na rdzeniu ringu: harness drugiego przeglądu wstawia zanik
  zasilania, błąd flasha albo drugie zadanie piszące przy każdej operacji
  flasha jednego zapisu — po zmianie startu żaden późniejszy zapis nie trafia
  już w zapisany slot (wcześniej trafiało 288 przebiegów z drugim zadaniem i 6
  z zanikiem zasilania, który zostawił puste sloty między zapisanymi); drugie
  zadanie wewnątrz rdzenia nadal gubi rekordy i właśnie to wyklucza mutex.
  Przy błędzie flasha w dowolnej jednej operacji zapisu nie ginie już żaden
  rekord — przed powtórkami 51 strat po restarcie i 183 w trakcie sesji. Zanik
  zasilania przy każdej z 370 operacji flasha ciągu zapisów przez zawinięcie
  nigdy nie daje odczytu wartości, której nie zapisano, a ring zawsze startuje
  i przyjmuje zapisy.
- Stos: `kalman_ctl()` 584 bajty, `ltic_read_fast()` 64 i `ltic_fit()` 104,
  bez zmian; w zadaniu sterowania `live_store_tick()` 88 (było 56) i
  `health_watch_loop()` 16; ścieżka zapisu do ringu ma 1112 bajtów głębokości
  zamiast 1048 (`fr_write()` 64 + `fr_put()` 1048, wobec `fr_write()` 1048) —
  w zadaniach sterowania i CLI, do sprawdzenia przez `SW`. Flash +2,4 kB
  (0,7 kB z tego to poprawki z 07.10); statyczny RAM +1,6 kB, z czego 1,5 kB
  to bufor ringu na przenoszone rekordy, a dwa mutexy, ringu i magazynu
  ustawień, około 160 bajtów sterty (build 64 na płytce autora zajmował
  statycznie 49,5 kB ze 128 kB).
- Na płytce, do potwierdzenia: zimny start prosto w `LA 13` — albo `LA 13`
  wpisane w trakcie survey-in — dociąga, nie przelatując za punkt pracy,
  uzbraja dzielnik raz i dochodzi w ciągu dziesięciu minut, bez linii `LOOP:`;
  po nim `LB`, kilka razy pod rząd, pokazuje „typical” w okolicy −3 do −4,
  prawie żadnych odrzuceń za wygięcie i żadnego odrzuconego odczytu, a log —
  żadnych zamrożonych odczytów po uzbrojeniu; po 20 minutach locka live store
  zapisuje nowy slot, a przy następnym starcie `Initial PWM=` jest w okolicy
  40960 zamiast 42429; po `LC` PWM na linii telemetrii wraca tam, gdzie był
  przed nim; a `SW` nadal pokazuje zapas na stosach zadań Control i CLI.

Build 64, `GPSDO v1.08.64rt`: dwie poprawki odczytu na zboczu, po pierwszej
nocy buildu 63 na płytce (algorytm 13, 47,8–49,4 °C) i 70 poleceniach `LB`
wpisanych następnego ranka. Poprawka z buildu 63 działa: w godzinach
23:30–08:00, jak dla buildu 62, największy krok fazy z sekundy na sekundę
wyniósł 6,5 ns (build 62: 22 ns, 98 kroków powyżej 10 ns), a filtr Kalmana nie
odrzucił ani jednego odczytu (54). Noc nie była jednak cichsza — sd fazy
1,77 ns, biały szum 1,15 ns, ADEV 1,9e-9 przy 1 s i 3,0e-12 przy 1024 s (build
62: 1,74, 1,14, 2,0e-9, 2,9e-12) — a co dziewiąta sekunda publikowała
wstrzymany odczyt, gdy w nocy buildu 62 mniej więcej co siedemdziesiąta.
Liczniki `LB` pokazały dlaczego: na 61 380 s 7064 serie odrzucone za
wygięcie (11,5 %), 75 za zbyt wiele pomiarów poza prostą (wszystkie w
pierwszych 80 minutach), za nic innego żadna.

### Naprawione (build 64)

- **Bramka wygięcia liczy od wygięcia własnego płytki.** Każda seria na
  płytce jest wygięta: każdy pomiar zabiera ~2,8 kodu ładunku kondensatora
  (0,12 %), więc seria spada o ~180 kodów — „decay ~1.4 ms” w `LB` to głównie
  to, nie upływ — a prosta przez nią mija zbocze o kilka kodów, co sekundę
  tak samo. 70 serii pokazanych przez `LB` 04.10 było wygiętych o −3,1 kodu
  (mediana), z rozrzutem 1,2. Liczony od zera, jak do buildu 63, ogon tego
  rozkładu był odrzucany jako wygięty (5 z 70, każda jak pozostałe), a każde
  odrzucenie wstrzymywało poprzedni odczyt. To, jak bardzo wyginają się serie
  płytki, nie jest stałe — przy tej samej bramce noc buildu 62 wstrzymała
  mniej więcej co siedemdziesiąty odczyt, a noc buildu 63 co dziewiąty —
  więc firmware teraz za tym idzie: średnia, z ~32 s, wygięcia każdej serii
  wziętej na zboczu (także odrzuconych, inaczej zmiany nie dałoby się
  śledzić), każda liczona najwyżej za 1 kod, od zera po starcie (do −3 płytki
  zbliża się na pół kodu po mniej więcej dwóch minutach). Seria jest
  odrzucana tylko wtedy, gdy jej wygięcie różni się od tej średniej o więcej
  niż 3,5 kodu i o 5 sigma jej własnego szumu — ten sam test co wcześniej,
  liczony od wygięcia płytki zamiast od zera. Licząc od −3,1, z 70 serii
  odrzucona zostaje co najwyżej jedna zamiast pięciu: ta, którą transmisja
  szeregowa podniosła o ~20 kodów.
- **Linia poleceń milczy wokół zbocza.** Przy `LB` wpisanym 70 razy w
  niecałe trzy minuty odczyt przy następnym zboczu wyszedł o 8–20 ns za
  wysoki 30 razy: ~3 kB z `LB` jeszcze wychodziły przez UART Bluetooth (57600
  bodów, ~0,5 s), gdy przychodziło zbocze, a seria wzięta wtedy leżała ~20
  kodów wyżej. Filtr Kalmana odrzucił w tych minutach 27 odczytów (przez całą
  noc przed nimi jeden), a jego R wzrosło z 1,50 do 3,64. Teraz CLI wstrzymuje
  wypisywanie od 150 ms przed każdym zboczem (przewidywanym jak dla serii) do
  2 ms po nim; 512-bajtowy bufor UART Bluetooth opróżnia się w ~90 ms. Nic nie
  ginie: długi wydruk, jak `LB`, wychodzi z krótką przerwą co sekundę. Bez
  zbocza, z którego dałoby się przewidywać, przerwy nie ma. Raport okresowy
  nie jest wstrzymywany; wychodzi tuż po zboczu.

### Zmienione (build 64)

- `LB` pokazuje wygięcie własne płytki za wygięciem serii (`bend -3.32
  (typical -3.10)`), a serię odrzuconą z tego powodu jako „bent unlike the
  board's bursts”.

### Weryfikacja (build 64)

- hostcheck czysty we wszystkich 18 konfiguracjach (arm-none-eabi,
  cortex-m4); zmienione pliki bez nowych ostrzeżeń przy `-Wall -Wextra`.
- `tools/lticsim/run.sh` ma dwa nowe sprawdzenia — seria wygięta o 5 kodów
  jest odrzucana, licząc od zera, i przechodzi, licząc od własnych −5 płytki,
  a wygięta o 6 kodów dalej jest odrzucana — 16/16 przy τ 5, 27 i 100 ms;
  oraz nową linię modelu: serie wygięte jak na płytce (−3,1 kodu, szum 2,1
  kodu z pomiaru na pomiar) są odrzucane w 2,3–2,5 % przypadków, licząc od
  zera, i nigdy, licząc od własnego wygięcia. Pozostałe liczby modelu
  identyczne jak w buildzie 63.
- Stos FreqRelay: `ltic_fit()` 104 bajty ramki (było 96), `ltic_read_fast()`
  bez zmian, 64, więc szczyt (odczyt i dopasowanie) głębszy o 8 bajtów;
  `ltic_quiet_now()` w zadaniu CLI 8. Statyczny RAM +20 bajtów; flash
  +2,3 kB, prawie całość to czekanie CLI, wstawione w każde jego wywołanie
  wypisywania.
- Nowe `LC` nie jest potrzebne: seria, której prosta się utrzymała, jest
  odczytywana jak dotąd.
- Na płytce, do potwierdzenia: licznik wygięć w `LB` prawie stoi, a z nim
  wstrzymane odczyty; „typical” w okolicy −3 na tej płytce; a `LB` wpisywane
  raz po raz nie powoduje żadnego odrzuconego odczytu.

Build 63, `GPSDO v1.08.63rt`: jedna poprawka i jedne liczniki, po pierwszej
nocy buildu 62 na płytce (algorytm 13, 23:30–08:00, w tej samej temperaturze
47,5–48,5 °C co noce przed nią). Przy odczycie na zboczu biały szum detektora
spadł do 1,14 ns (build 61: 1,64, build 60: 2,07), sd fazy do 1,74 ns (2,65,
2,63), a szum pomiaru R w filtrze Kalmana do 1,41 (2,27, 2,43); ADEV fazy
wynosi 2,0e-9 przy 1 s (2,8e-9, 3,6e-9) i 2,9e-12 przy 1024 s (w obu 4,5e-12),
a PWM zmieniało się 1330 razy na godzinę (1860, 1791). Skok co 233,24 s
zniknął: test okresowości na tej siatce daje 68 dla buildu 61 i 0 dla buildu
62. Zostały pojedyncze skoki do ~20 ns, 55 przez noc.

### Naprawione (build 63)

- **Wstrzymany odczyt zachowuje swoją poprawkę piły.** Gdy odczyt sekundy jest
  wstrzymany — zbocze nie złapane albo prosta odrzucona (do 3 s z rzędu), albo
  skok zatrzymany przez bramkę odrzutów — publikowane napięcie pochodzi z
  wcześniejszego impulsu, a build 62 zatrzaskiwał przy nim qErr nowego
  impulsu, tak samo dla pętli (`ubx_timtp_latch()`) jak dla wyświetlacza. W
  sekundzie, w której piła odbiornika przeskoczyła, faza skakała razem z nią:
  we wszystkich 97 skokach z tego logu Vphase jest takie samo jak sekundę
  wcześniej, a qErr nie. Wstrzymanych było ok. 1,3 % sekund nocy. Filtr
  Kalmana odrzucał te skoki (~6 na godzinę), więc pętla prawie ich nie
  odczuwała; log i wyświetlacz tak. Teraz wstrzymany odczyt zachowuje qErr
  zatrzaśnięte razem z nim (`ubx_timtp_latch_keep()` przesuwa tylko numer
  impulsu). Na logu z tej nocy: sd fazy 1,74 → 1,57 ns, największy skok
  23 → 8 ns, ADEV przy 1 s −23 %. To samo dotyczy `LRM 0`, którego bramka
  tak samo łączyła zatrzymany skok z nowym qErr.

### Zmienione (build 63)

- **`LB` liczy, co stało się z każdą sekundą od startu**, żeby skąd biorą się
  wstrzymane sekundy — jedna na sto, losowo, czego nie pokaże żadna pojedyncza
  seria — dało się odczytać z płytki. Zbocze: złapane, przyszło pierwsze
  (zbocze, zanim seria była gotowa), zgubione, TIM2 (nie doszedł), nie
  próbowano (brak sekundy do przewidywania, zbocze jeszcze nieodczytane,
  otwarty tunel). Prosta: utrzymana albo odrzucona — za mało pomiarów, za dużo
  poza prostą, wygięcie, inne (czas serii, TIM2, poza skalą). Dalej: ile razy
  został poprzedni odczyt i sekundy bez nowego zbocza. Dla ostatniej serii
  `LB` mówi też, dlaczego jej prosta została odrzucona. Liczniki są tylko w
  RAM i się nie zapisują; raport okresowy bez zmian.

### Weryfikacja (build 63)

- hostcheck czysty we wszystkich 18 konfiguracjach (arm-none-eabi,
  cortex-m4); zmienione pliki bez nowych ostrzeżeń przy `-Wall -Wextra`.
- `tools/lticsim/run.sh` sprawdza teraz także powód, który dopasowanie podaje
  przy każdym odrzuceniu, tak jak wypisuje go `LB`: TIM2 stoi i wynik poza
  skalą — inne; 15 pomiarów — za mało; dziewięć szpilek — poza prostą; impuls
  trwający jeszcze przez 12 pomiarów i wygięcie o 6 kodów — wygięcie. 14/14
  przy τ 5, 27 i 100 ms, liczby modelu identyczne jak w buildzie 62.
- Stos FreqRelay: `ltic_edge_sync()` 48 bajtów ramki (było 40),
  `ltic_read_fast()` bez zmian, 64, więc szczyt (odczyt i dopasowanie) bez
  zmian. Statyczny RAM +96 bajtów: liczniki i ich kopia w CLI.
- Na płytce, do potwierdzenia: w nocnym logu brak jednosekundowych skoków przy
  przeskokach qErr, a liczniki `LB` pokazują, z którego powodu biorą się
  wstrzymane sekundy.

Build 62, `GPSDO v1.08.62rt`: detektor fazy odczytywany na samym zboczu PPS, na
wyciszonym układzie, z przetwornikiem obsługiwanym bezpośrednio przez rejestry.
Prosta z buildu 61, zalogowana na płytce przez noc, obniżyła biały szum
detektora z 2,06 do 1,61 ns — a przy tak czystym odczycie odsłoniła
jednosekundowy skok +10…16 ns co 233,24 s, w obu trybach odczytu: dudnienie
sekundy GPS z 1-milisekundowym tickiem RTOS (zegar MCU na tej płytce odbiega o
4,29 ppm), które raz na okres stawiało tick, i to, co tick obudził, na impulsie
ładującym. Żadna arytmetyka na serii tego nie usunie; build 62 zdejmuje układ z
impulsu.

### Zmienione (build 62)

- **LTIC: seria brana na zboczu, na wyciszonym układzie.** `vFreqRelayTask`
  przewiduje następne zbocze z dwóch ostatnich przechwyceń (TIM2 liczy OCXO,
  więc jego ostatnia sekunda mówi, gdzie skończy się następna, z dokładnością
  do taktu), śpi do 1–3 ms przed nim i czeka przy wstrzymanym schedulerze. Od
  10 µs przed zboczem do 5 µs po nim wszystkie przerwania są zablokowane, a
  zadanie samo odpytuje flagę przechwycenia: w chwili impulsu działa tylko ta
  pętla — żaden tick, żadne zadanie, żadne przerwanie. To ~15 µs na sekundę
  (najwyżej 35, gdy zbocze się spóźni). Przerwanie przechwycenia wykonuje się
  zaraz po serii.
- **Przetwornik obsługiwany przez rejestry, nie przez `analogRead()`.** Jedno
  `analogRead()` trwa na płytce 54,5 µs — przy każdym wywołaniu inicjuje ADC,
  resetuje go i wyłącza mu zegar — więc szesnaście pomiarów buildu 61
  rozciągało się od 64 do 942 µs po zboczu. Teraz 64 pomiary po ~1,4 µs, z tym
  samym czasem próbkowania (15 cykli) i zegarem (PCLK2/4) co `analogRead()`,
  pierwszy 5 µs po zboczu, razem ~90 µs (`LTIC_EDGE_N`, oraz
  `LTIC_ADC_CHANNEL` obok pinu w `gpsdo_config.h`). Biegną w sekcji krytycznej
  FreeRTOS: tick, timery i przerwanie przechwycenia czekają do końca serii, ale
  porty szeregowe i USB, które w tym rdzeniu stoją ponad poziomem RTOS, działają
  dalej — GPS przy 115200 bodów nic nie traci. Pomiar, który któreś z nich
  wydłużyło, jest pomijany w prostej (dla każdego zapisywany jest początek i
  czas trwania). ADC zostaje w takim stanie, w jakim zostawia go
  `analogRead()`, a odczyty `ControlTask` są bez zmian; wstrzymanie schedulera
  wokół nich jest tym, co trzyma przetwornik wolny, gdy przychodzi zbocze. Przy
  otwartym tunelu GPS (`T`), którego prędkość ustala użytkownik, zbocze nie
  jest wyczekiwane.
- **Pomiar VREFINT ~60 µs przed zboczem**, z czasem próbkowania 20 µs, jakiego
  VREFINT potrzebuje. Każdy pomiar zaczyna się od podziału ładunku między ~4 pF
  kondensatorem próbkującym ADC a węzłem 1 nF — 0,4 % tego, co ten kondensator
  zachował z poprzedniego pomiaru, do ~9 kodów. Teraz jest to co sekundę to
  samo, a nie to, co zostawiło Vctl, Vcc czy poprzednia seria.
- **Prosta odczytywana na zboczu, t = 0** — ładunek, który dostarczył impuls,
  zanim cokolwiek z niego upłynęło. Odczyt nie zależy już od tempa upływu (`LB`
  zmierzył τ ≈ 27–30 ms, a nie ~5 ms, które zakładało źródło; upływ diody
  Schottky'ego rośnie z temperaturą) ani od czasu trwania pomiaru;
  `LTIC_TREF_US` znika. W modelu ze zmierzonymi szerokościami kodów
  (`tools/lticsim`, τ 27 ms, szum ADC 1,0–1,5 LSB): błąd na odczyt 1,18–1,56
  LSB dla mediany, 0,37–0,47 dla prostej na zboczu; nieliniowość przy punkcie
  lock 0,22 → 0,07 LSB rms. Seria obejmuje tylko ~7 kodów, więc schodki ADC
  wygładza szum, a nie rozładowanie, i lokalne nachylenie wynosi tam 1,09
  (mediana: 0,95). Bez pomiaru VREFINT model daje 1,01 LSB.
- **Gdy zbocza nie da się tak złapać** — dwa pierwsze po starcie, zbocze
  wcześniejsze lub zgubione, pobudka opóźniona, otwarty tunel — ta sama seria
  brana jest, gdy tylko przerwanie przechwycenia obudzi zadanie, mierzona od
  przechwyconego zbocza i odczytywana tak samo w t = 0 (model: sd 0,91 LSB,
  poziom w granicach 0,04 LSB od odczytu na zboczu). Tylko impuls nie trafił
  na wyciszony układ, więc taki odczyt jest wstrzymywany: zostaje poprzedni,
  najwyżej 3 sekundy z rzędu, potem spóźnione odczyty są używane. Seria, na
  której prosta nie może się oprzeć — mniej niż 16 pomiarów, więcej niż 8 poza
  prostą, TIM2 nie liczy, początek później niż 300 µs po zboczu, wynik poza
  skalą ADC albo, dla serii wziętej na zboczu, wygięcie rzeczywiste (5σ wobec
  własnego szumu serii) i warte ponad 3,5 kodu na zboczu — jest wstrzymywana
  tak samo, a potem ustępuje medianie serii, przeniesionej na zbocze wzdłuż
  nachylenia ostatniej prostej (z nie dalej niż 1 ms). Sekunda bez nowego
  zbocza (timeout 2 s) dostaje medianę, jak wcześniej.
- **`LRM 0` to odczyt z buildu 60, bez zmian** (szesnaście `analogRead()` od
  50 µs, ich mediana, bez czekania na zbocze), do porównania A/B. Oba tryby
  odczytują upływ w innej chwili — t = 0 wobec ~470 µs, ok. 2 % poziomu — więc
  kalibracja `LC` należy do trybu, w którym ją zrobiono: po aktualizacji uruchom
  `LC` raz. `LB` wypisuje serię: wzięta na zboczu czy później, prosta na
  zboczu, jej nachylenie i wynikająca z niego stała upływu, rozrzut i wygięcie,
  kod VREFINT oraz 64 pomiary z czasami od zbocza i czasem trwania.
- Odczyt startowy w `SensorTask` znika; `vFreqRelayTask` jest jedynym
  wywołującym.

### Weryfikacja (build 62)

- hostcheck czysty we wszystkich 18 konfiguracjach (arm-none-eabi,
  cortex-m4); zmienione pliki kompilują się bez ostrzeżeń przy `-Wall
  -Wextra`.
- `tools/lticsim/run.sh`, przepisany pod odczyt na zboczu, uruchamia własne
  `ltic_fit()` firmware: zatrzymany TIM2 i 15 pomiarów odrzucone; wydłużone
  pomiary, jeden i cztery skoki oraz zestaw 80/40/20 pominięte, dziewięć skoków
  odrzuconych; impuls trwający jeszcze przez pierwsze 5 pomiarów pominięty,
  przez 12 odrzucony; wygięcie warte 6 kodów odrzucone na zboczu, a w serii
  spóźnionej nie bramkowane; własna krzywa upływu przechodzi; wynik poza skalą
  odrzucony; liczby powyżej przy τ 5, 27 i 100 ms. Żadna seria, na zboczu ani
  spóźniona, nie jest odrzucana przy szumie ADC od 0,2 do 2 LSB; wygięcie warte
  6 kodów odrzucane w 97 % przy 1,5 LSB.
- Dwa przeglądy subagenta. Wprowadzone: pomiary wyjęte z okna pełnej blokady
  (trzymały wszystkie przerwania wyłączone ~110–170 µs, dość, by przepełnić
  UART GPS przy 115200), pomijanie wydłużonych pomiarów, czas próbkowania
  VREFINT, bramka wygięcia tylko dla serii na zboczu (w spóźnionych przenosi
  pozorne wygięcie za daleko), mediana zastępcza przeniesiona na zbocze,
  porzucenie przewidywania po sekundzie bez zbocza, tunel pozostawiony w
  spokoju.
- Stos FreqRelay (192 słowa): ramka `ltic_read_fast()` 64 bajty (wcześniej
  48), `ltic_edge_sync()` 40 plus `ltic_burst()` 40, 96 bajtów dopasowania
  dopiero po serii; seria zapisywana prosto do statycznego rekordu, który
  czyta `LB` (arm-none-eabi `-fstack-usage`). Statyczny RAM +~0,8 kB,
  głównie ten rekord i jego kopia w CLI.
- Na płytce, do potwierdzenia: `LB` pisze „taken at the edge”, pierwszy pomiar
  ~5 µs po zboczu, upływ ~27–30 ms jak wcześniej (znacznie krótszy oznaczałby,
  że węzeł obciąża sam przetwornik), a w nocnym logu nie ma już skoków co
  233,24 s.

Build 61, `GPSDO v1.08.61rt`: domyślne ustawienia algorytmów 12 i 13 z buildu
60 cofnięte tam, gdzie płytka pokazała, że są złe. Dobrano je na symulacji bez
szybkiego szumu samego oscylatora, a logi z tego samego tygodnia dały obu
algorytmom mniej więcej dwa razy większy szum fazy niż algorytmowi 11.
Algorytm 13 bije teraz algorytm 11 na płytce. A detektor fazy jest czytany z
prostej przez całą serię pomiarów, w stałej chwili po zboczu, zamiast z
jednego pomiaru.

### Zmienione (build 61)

- **Algorytm 13: znów `KT` 100 s, a `KC` 0 = auto = `KT/6` (16 s; w buildzie
  60 było `KT`, wcześniej `KT/3`).** Zmierzone na płytce, po jednej nocy:
  odchylenie std. fazy z logu 2,63 ns wobec 2,73 dla algorytmu 11, ADEV przy
  1024 s 4,4e-12 wobec 4,7e-12, przy 4096 s 1,11e-12 wobec 1,17e-12; średnia
  64-sekundowa 1,07 ns wobec 1,13. Kosztem jest ruch DAC: PWM zmienia się
  ok. 2,5 razy częściej niż przy algorytmie 11, o 1–4 LSB. `KT 200` / `KC` =
  `KT` z buildu 60 dało w logu 5,62 ns. Przez całe 24,5 h tego logu, także w
  dzień (płytka 47,8–51,7 °C), odchylenie godzinowe mieściło się w
  2,4–2,9 ns. Płytka zachowuje to, co zapisała, a wpisanie któregokolwiek z
  `KR`, `KQ`, `KT`, `KC` zapisuje wszystkie cztery: płytka, na której w
  buildzie 60 wpisano którekolwiek z nich, wstanie z `KT 200` (auto `KC` 33).
  `KL` to pokaże; `KT 100` i `KC 0` przywracają domyślne.
- **Algorytm 12, tryb spokojny (`MM 1`): krótkie poziomy nie potrzebują już
  trzykrotności swojego limitu, a poziom, od którego człon częstotliwości
  idzie w całości, jest stały — 128 s — zamiast zależeć od `MR`.** Na płytce
  tryb spokojny z buildu 60 dał 6,07 ns przy `MR 7` i 9,51 przy `MR 10`, wobec
  2,73 dla algorytmu 11 w tym samym tygodniu: krótkie poziomy odpowiadały na
  szybką wędrówkę samego oscylatora, a przytrzymane — przepuszczały ją; przy
  `MR 10` granica przesuwała się na 1024 s i połowiła człon częstotliwości
  poniżej. W skalibrowanej symulacji (niżej): 5,2 ns przy `MR 7` i 5,1 przy
  `MR 10` (build 60: 6,9 i 9,3; `MM 0`: 6,1 i 5,6), przy PWM ruszającym się
  pięć razy mniej niż przy algorytmie 11. Algorytm 12 nadal nie dorównuje
  algorytmowi 11. Rampa (1 LSB/s) i dokładne zdjęcie slewu przy przejściu
  przez zero zostają. Poniżej `MR 6` żadna korekta nie sięga 128 s, więc
  człon zostaje tam połowiony (w symulacji `MR 5` nic na tym nie traci:
  5,1 ns wobec 5,3).
- **LTIC: faza jest czytana z prostej przez całą serię, nie z jej mediany.**
  Szesnaście pomiarów po każdym zboczu to nie szesnaście spojrzeń na jedno
  napięcie: kondensator się rozładowuje (~5 ms), a seria trwa ~350 µs, więc
  kody spadają o ponad sto, a ich mediana to po prostu środkowy w czasie
  pomiar — jeden pomiar, w chwili zależnej od tego, jak wybudziło się zadanie.
  To kosztowało podwójnie. W 24,6 h zapisanych odczytów przy punkcie pracy kody
  ADC są dalekie od równych — kod będący wielokrotnością 4 ma średnio 2,4 LSB
  szerokości, kod o trzy wyżej 0,1 — więc pętla stała na schodkach o stopniach
  ~3 ns; a rozładowanie zamienia każdą mikrosekundę w czasie odczytu na
  ~0,4 ns. Każdy pomiar ma teraz znacznik czasu z TIM2, licznika, który złapał
  zbocze; przez serię prowadzona jest prosta względem tych czasów (wywołania
  wydłużone przez przerwanie i pomiary daleko od prostej są pomijane) i
  odczytywana 200 µs po zboczu (`LTIC_TREF_US`, tam gdzie wypadała mediana,
  więc kalibracja `LC` dalej obowiązuje). W modelu ze zmierzonymi szerokościami
  kodów (`tools/lticsim`, który uruchamia dopasowanie z firmware): błąd
  pojedynczego odczytu 1,3–1,7 → 0,14–0,30 LSB (tu 0,85 ns na LSB),
  nieliniowość przy zerze 0,20 → 0,03 LSB rms, wybudzenie spóźnione o 60 µs
  27 → 0,7 LSB; przy dwadzieścia razy wolniejszym upływie dwa do trzech razy
  lepiej niż mediana, przy pięć razy szybszym osiem razy. Pomiary daleko od
  prostej ocenia się medianami, więc kilka zakłóceń nie schowa się za sobą.
  Seria, na której prosta nie może się oprzeć — brak świeżego zbocza, TIM2 nie
  liczy, zostało mniej niż 8 pomiarów albo więcej niż 4 odbiega, wynik poza
  skalą ADC — wraca do mediany; seria rozpoczęta ponad 150 µs za późno, gdzie
  prosta odchodziłaby od krzywej rozładowania, zostawia poprzedni odczyt (co
  najwyżej trzy sekundy z rzędu). `LRM 0|1` przełącza między nimi do porównania A/B (nie zapisuje się;
  każdy start to prosta), `LB` pokazuje ostatnią serię.
- **CLI:** `KC` pokazuje `auto = KT/6`; podpowiedź `KT` podaje 100 s jako
  domyślne i znów pojawia się powyżej 200 s, a ta o długim `KC` powyżej 60 s
  (build 60 przesunął obie na 400 s); podpowiedź przy krótkim `KC` nie
  twierdzi już, że faza jest najlepsza przy 20–30 s (w skalibrowanej
  symulacji poniżej 16 s faza wciąż się poprawia — o kilka procent, za dużo
  więcej ruchu); `MM` pokazuje `calm: ramped`.
- **Symulator pętli skalibrowany na płytce.** Nowy plant
  `plant/20260929_algo11_night.csv`; `bench.py run --hw` odtwarza go z
  przywróconym szybkim szumem oscylatora (`LOOPSIM_RWFM=0.1`) i zmierzoną
  wędrówką detektora (`LOOPSIM_DNOISE=1.5`, `LOOPSIM_DTAU=45`), oceniając
  wynik tak, jak ocenia się log. Odtwarza cztery przebiegi z tamtego tygodnia
  z dokładnością do 20 %, w tej samej kolejności, i przewidział powyższy wynik
  algorytmu 13, zanim płytka go zmierzyła. `LOOPSIM_LD` ustawia tłumienie
  algorytmu 11, obok pozostałych pokręteł `L*`.

### Weryfikacja (build 61)

- hostcheck czysty we wszystkich 18 konfiguracjach; `tools/loopsim/run.sh` i
  `tools/algoswitch/run.sh` działają; kontrola typów przy linkowaniu czysta.
- `tools/lticsim/run.sh`: nieaktualne zbocze i stojący TIM2 są odrzucane;
  jeden, dwa i cztery skoki o 20 kodów, zestaw 80/40/20 i wydłużone wywołanie
  są pomijane; spóźniona seria jest oznaczana; liczby jak wyżej przy 1, 5, 20 i
  100 ms. Stos FreqRelay (192 słowa) nietknięty: bufory serii są statyczne,
  ramka `ltic_read_fast()` ma 48 bajtów jak wcześniej, a 56 bajtów dopasowania
  istnieje dopiero po serii (arm-none-eabi `-fstack-usage`). Przy `LRM 0`
  publikowany odczyt to mediana, a bramka decyduje dokładnie jak wcześniej.
  Przerwanie przechwytu zapisuje teraz także wartość TIM2 zbocza, z której
  korzysta odczyt zamiast samemu czytać CCR3 (to kasowałoby flagę przechwytu).
- `bench.py run --hw`, trzy seedy (odch. std. fazy z logu / ADEV 1024 s /
  zmiany PWM na godzinę): algorytm 11 2,49 / 4,2e-12 / 729; algorytm 13
  2,40 / 4,1e-12 / 1475; algorytm 12 5,18 / 9,0e-12 / 141.
- Cztery starsze nocne planty z tym samym szumem (`bench.py run`, prawdziwa
  faza, dwa seedy): bez niestabilności; algorytm 13 1,65–1,69 ns, algorytm
  12 4,5–5,3 ns przy `MR 7` i 4,2–5,2 przy `MR 10`.

Build 60, `GPSDO v1.08.60rt`: algorytmy 12 i 13 domyślnie spokojniejsze, a
tuner rysuje napięcie sterujące zadane przez pętlę. Po drodze znalazł się błąd
w symulatorze pętli, przez który oba algorytmy wyglądały gorzej, niż są —
niestabilność algorytmu 13 przy długich horyzontach była symulatora, nie pętli.

### Zmienione (build 60)

- **Algorytm 12: tryb spokojny, nowy domyślny (`MM 1`; `MM 0` to pierwotna
  ścieżka, bez zmian).** Gdy faza jest już w `LAT`: poziomy poniżej tego tuż
  pod `MR` (poniżej 128 s przy `MR 7`) potrzebują trzykrotności swojego
  limitu, więc duży błąd nadal łapią od razu, ale nie pulsują już na wędrówce
  detektora; od tego poziomu w górę człon częstotliwości idzie w całości
  zamiast połowy; wyjście dochodzi do korekty najwyżej o 1 LSB na sekundę
  (zmiana ponad 32 LSB — akwizycja, FLL, uzbrojenie — nadal od razu); a
  przejście przez zero zdejmuje dokładnie założony slew, przez ścieżkę fine,
  zamiast go zaokrąglać i gubić ułamek. Na czterech nocnych plantach, dwa
  seedy: ADEV przy 10 s 1,6e-11 → 8,5e-12 z białym szumem detektora i
  3,7e-11 → 1,8e-11 ze zmierzoną na stanowisku wędrówką detektora; największy
  pojedynczy ruch 9–18 LSB → 1 LSB; błąd fazy w granicach ok. 10 %; akwizycja
  bez zmian. Algorytm 11 pozostaje spokojniejszy przy 10–100 s — impulsy to
  sposób, w jaki ten algorytm przesuwa fazę. `MM` zapisuje `ES ALGO12`, w
  bajcie wypełnienia bloku ustawień (rozmiar bez zmian; starszy rekord czyta
  go jako nieustawiony = spokojny).
- **Algorytm 13: `KT` 200 s (było 100) i `KC` 0 = auto = `KT` (było
  `KT/3`).** Przy `KC` 33 regulator wpycha w oscylator własną wolną wędrówkę
  detektora — 2,6 ns przy τ ≈ 45 s, zmierzone na stanowisku. Z tą wędrówką w
  symulacji: ADEV przy 10 s 2,9e-11 → 6,3e-12, przy 100 s 3,5e-11 → 1,4e-11,
  czyli tyle co algorytm 11 (6,3e-12, 1,4e-11), a od 300 s lepiej (1,3e-11
  wobec 1,7e-11); prawdziwe sd fazy 4,2 ns wobec 5,9 ns algorytmu 11; DAC
  rusza się pięć razy mniej. Z białym szumem detektora wygrywa z algorytmem 11
  przy każdym τ od 10 do 1000 s. Koszt to akwizycja: ok. 8½ minuty po
  uzbrojeniu zamiast 3½. `KT 100` i `KC 33` przywracają dawne ustawienie;
  płytka z zapisanym `KT`/`KC` zachowuje zapisane. Ostrzeżenie `KT` przesuwa
  się z 200 na 400 s, ostrzeżenie `KC` z 60 na 400 s.
- **Tuner: okna napięcia sterującego rysują `Vcmd` = kod PWM / 65536 × `DV`,
  napięcie zadane, zamiast odczytu z ADC `Vctl`.** Odczyt ma ok. 1 mV szumu,
  jakieś dwadzieścia LSB, a ustalona pętla rusza kodem o jeden, dwa, więc okno
  pokazywało ADC, nie pętlę. `DV` jest czytane przy połączeniu; `Vctl` zostaje
  w polach na żywo i w logu.
- **Symulator pętli.** `LOOPSIM_MM` wybiera tryb algorytmu 12; `LOOPSIM_RWFM`
  dodaje do plantu błądzenie losowe częstotliwości, dla oscylatora bardziej
  szumiącego niż ten, z którego plant pochodzi; zrzut dostaje kolumnę `det`,
  fazę tak, jak ją widzi detektor; a `tools/loopsim/bench.py` ocenia
  konfiguracje na wszystkich nocnych plantach i kilku seedach szumu (sd fazy,
  aktywność DAC, największy ruch, ADEV 10–1000 s).

### Naprawione (build 60)

- **Symulator pętli: `gpsdo_dac_last16f()` był zdefiniowany jako `float` w
  `tools/loopsim/loopsim.cpp` i `tools/algoswitch/switchsim.cpp`, a firmware
  deklaruje `double`.** Nic nie ostrzegło — żaden z tych plików nie dołącza
  nagłówka — a `fine_base()` z firmware'u czytał rejestr zwrotny jako double:
  wartość bliską prawdy, więc wiarygodną, ale błędną do jednego LSB przy
  każdej korekcie algorytmów 12 i 13. Stan częstotliwości algorytmu 13 niósł
  błąd kilku LSB, a jego dłuższe horyzonty wyglądały na niestabilne (`KT 1000`
  na plancie z jednej nocy: sd fazy 777 ns), choć nie są (9 ns). Płytka nigdy
  tego błędu nie miała; tam definicja jest `double`. Wcześniejsze sweepy
  `KT`/`KC`, które uznały długie `KC` za niestabilne, szły na tym błędzie.
  Znalezione przez `g++ -flto -Wlto-type-mismatch`, który teraz pokazuje
  firmware (trzy konfiguracje płytki) i trzy symulatory jako czyste.

### Weryfikacja (build 60)

- `MM 0` z `KT 100` / `KC 33` odtwarza build 59 bit w bit w symulatorze
  (zrzuty i podsumowania identyczne, algorytmy 12 i 13, dwa planty).
- hostcheck czysty we wszystkich 18 konfiguracjach; `tools/loopsim/run.sh`,
  `tools/algoswitch/run.sh` i `tools/spansim/run.sh` przechodzą.
- Akwizycja z railu, z fazą 1300 ns obok i z częstotliwością 50 lub 200 LSB
  obok: algorytm 12 dochodzi jak przedtem (z 200 LSB obok szybciej: 1091 s
  wobec 3042).

Build 59, `GPSDO v1.08.59rt`: brak zmian funkcjonalnych. Komentarze szkicu
sprawdzono względem kodu i odświeżono, a martwy kod usunięto; każda jednostka
kompilacji preprocessuje się do tego samego strumienia tokenów co w build 58,
więc skompilowany program jest ten sam.

### Zmienione (build 59)

- **Komentarze źródłowe odświeżone, kod nietknięty (build 59).** Każdy długi
  komentarz i każdy, który wymieniał datę, numer builda lub osobę — około 10
  000 z 12 000 linii komentarzy szkicu — sprawdzono względem kodu; zmieniono 31
  plików. Historię — daty, numery buildów, kto co zmierzył, „kiedyś" — usunięto
  albo przeniesiono tutaj (patrz *Historia przeniesiona z komentarzy w kodzie*
  poniżej); każda zmierzona wartość uzasadniająca stałą pozostała, podana jako
  fakt o sprzęcie. Poprawiono stwierdzenia, którym przeczył kod, między innymi:
  ustawienia i wyuczony stan żyją w pierścieniu flash, nie w EEPROM (nagłówek
  szkicu, uwagi LRN i LTIC); nagłówek `gpsdo_algorithms.cpp` wymienia algorytmy
  0-13 zamiast „dziesięciu algorytmów (0-9)"; uwagi o współczynnikach
  algorytmów 3-8 podają wartości domyślne, które faktycznie trzyma `g_pid[]`
  (kilka cytowało starsze nastawy, a algorytm 7 zestaw „zoptymalizowany GA",
  którego już nie ma); pole danych slotu flash to [6..509], 504 bajty
  (`FR_PAYLOAD`), nie [4..509]; statystyki CS liczą cztery stałe czasowe, nie
  trzy; `LC` nie ma kroku „przepołów przesunięcie i spróbuj ponownie" i steruje
  swoim przemiataniem do 6 ns/s, nie 4 lub 1,5; raport zapisywany jest w
  budżetowanych porcjach, nie jednym `Serial.write()`.
- **Martwy kod usunięty (build 59).** Makra konfiguracyjne, których żaden kod
  nie czyta, a które wyglądały jak ustawienia: `LTIC_NS_PER_VOLT` (nieużywany
  „fallback" — `LC` jest jedynym źródłem ns/V), `LTIC_CAL_PWM_OFFSET` (`LC`
  wyprowadza swój impuls ze skalibrowanego wzmocnienia), `LTIC_DISCHARGE_MS`,
  `LTIC_DET_PERIOD_NS` i `AUTHOR_NAME`. `PIN_TFT_*` i `PIN_BT_*` jedynie
  dokumentowały okablowanie ustawiane gdzie indziej (`User_Setup.h` biblioteki
  TFT_eSPI, `Serial2` rdzenia); teraz jest to komentarz, który to mówi wprost.
  Zniknęły też: nieużywane makra wyświetlacza (`TFT_COL_LABEL`, `TFT_COL_LVAL`,
  `TFT_COL_RVAL`, `HPAL_NOTICE`), zdublowane identyfikatory rekordów flash
  (`FR_REC_LIVE`, `FR_REC_SETTINGS`, `FR_MAGIC0`) oraz komentarze opisujące
  kod, którego już nie ma.
- **Jak sprawdzono „brak zmian" (build 59).** Każdą jednostkę poddano
  preprocesorowi w 18 konfiguracjach hostcheck i 21 kolejnych (czujniki, OLED,
  LCD, TM1637, ILI9341, fałszywy u-blox, generator 2 kHz, gadatliwe NMEA, …) —
  razem sięgają każdej linii kodu szkicu poza jednym odczytem rejestru CMSIS —
  a jej strumień tokenów porównano z build 58: identyczny we wszystkich 918
  buildach jednostkowych. hostcheck jest czysty we wszystkich 18 wierszach, a
  funkcje, które narzędzia testowe wyciągają ze źródła, nadal się wyciągają.

### Historia przeniesiona z komentarzy w kodzie (build 59)
Te fakty były zapisane tylko w komentarzach w kodzie źródłowym, dopóki build 59
nie usunął ich z kodu.

**Algorytm 12**
- Wartość domyślna `arm_holdoff` zmieniła się z 15 na 60 próbek.
- Estymator szumu białego był kiedyś bramkowany warunkiem
  `g_mlacc_gain <= 0.0f` razem z budową tabeli progów; teraz działa zawsze.
- Człon częstotliwości algorytmu 12 usunięto po tym, jak wczesne, błędne
  pomiary szumu zdawały się przemawiać przeciwko niemu, a przywrócono go po
  naprawieniu samego estymatora sigma.

**Algorytm 13**
- 28.08 przełączenie z algorytmu 10 na algorytm 11 zostawiło odczyt referencji
  fazy na +1272 ns (2719 LSB).
- Przebieg z 29.08 pokazał estymator R rosnący do około 33 (wobec białego dna
  6,4) podczas wciągania fazy, które wprowadzało około 110 ns skomenderowanego
  ruchu w każde 16-sekundowe okno; przez kolejne około dwie godziny filtr nie
  ufał detektorowi, podczas gdy odchylenie standardowe fazy poprawiło się z
  11,19 ns po dziesięciu minutach do 3,36 ns w ostatniej godzinie.
- Pułap Q był pierwotnie płaskim mnożnikiem 8× wartości zasiewu; działało to
  tylko dzięki szczęściu, bo stosunki R/zasiew różnią się między płytkami (2,5×
  na jednej, 6,8× na drugiej), zanim zastąpiono go ograniczeniem `R/KT³`
  opartym na horyzoncie.
- Zanim istniało `s_kf_locked`, nic nie potrafiło stwierdzić, czy algorytm 13
  jest zsynchronizowany, poza cofnięciem się do dziesięciotysięcznosekundowej
  średniej częstotliwości.
- Opóźnienie estymatora R zmieniło się z różnicowania detektora co 1 s na 16 s
  (`KF_R_LAG`), aby uwzględnić błądzenie zera detektora widoczne w rozbieżności
  między ławą a symulatorem; potwierdzenie na sprzęcie było jeszcze w toku w
  chwili pisania tej uwagi.
- Wcześniejsza wersja komentarza `KC`/`KT` twierdziła, że rozdzielenie
  horyzontów regulatora i estymatora wymaga `Sg` zmierzonego z oscylatora —
  odniesienia, którego ta płytka nie ma; to rozumowanie było błędne i
  rozdzielenie tego nie wymagało.

**CLI**
- Wcześniejszy build składał linie telemetrii z fragmentów
  `cli_puts()`/`cli_putfloat()`, co na łączu wychodziło uszkodzone; teraz linie
  są składane przed wysłaniem.
- Zapis przez `ES FLAGS` był kiedyś zalecanym sposobem utrwalania tych
  ustawień, ale zapisywał tylko przełącznik wł./wył., więc reset nie zawsze
  przeżywał cykl zasilania.

**GPS**
- 19200 bodów brakowało na liście kandydatów autobaud, więc płytka ustawiona na
  tę prędkość nigdy nie była wykrywana. Kolejność kandydatów też miała kiedyś
  znaczenie — przy dopasowaniu pojedynczego bajtu `$` płytka 38400 była błędnie
  rozpoznawana jako 57600 we wszystkich 24 symulowanych próbach — dopóki
  wykrywanie nie przeszło na pełną walidację sumy kontrolnej, po czym kolejność
  przestała mieć znaczenie.
- Zawieszenie konfiguracji UBX z komunikatem „3 próby, wszystkie nieudane"
  prześledzono do zawsze przełączania na 38400 bodów przed wysłaniem poleceń
  UBX, podczas gdy moduł GY-NEO6MV2 NEO-6M po włączeniu domyślnie pracuje na
  9600 bodach; polecenie trafiało na prędkość, której moduł nie słuchał, i nie
  dostawało ACK.
- Parowanie telemetrii po `ppscount` dodano jako poprawkę z audytu w v0.95;
  wcześniej `qErr` mógł zostać przypisany do złego impulsu, przesunięty o
  losową liczbę impulsów.
- Wcześniejsza ramka konfiguracyjna ZED-F9T była uszkodzona: zdublowaną wartość
  `fixedAltVar` przez pomyłkę zakodowano jako dwa dodatkowe bajty
  „zarezerwowane" (payload 38 bajtów zamiast 36), więc suma kontrolna się nie
  zgadzała i moduł po cichu odrzucał ramkę, co powodowało przekroczenie czasu
  wszystkich trzech prób konfiguracji; brakującymi polami były
  `cnoThreshNumSVs` i `cnoThresh`.
- Wczesny kod inicjalizacji GPS na starcie na ślepo zakładał prędkość modułu, a
  przełączanie prędkości przez `PUBX,41` nie dawało ACK, dopóki nie naprawiono
  tego pełnym opróżnieniem TX, odczekaniem 150 ms i opróżnieniem RX;
  `taskYIELD()` przed startem schedulera okazał się niebezpieczny na tym
  porcie. Dwa 3000-milisekundowe opóźnienia rozruchu w `setup()` skrócono do
  jednego opóźnienia 500 ms, a wykrywanie autobaud zmieniło się z aktywnego
  odpytywania każdego kandydata na najpierw bierne nasłuchiwanie, z rezerwowym
  aktywnym odpytywaniem 350 ms.
- Wcześniejszy projekt holdover resetował linię bazową przy każdym PPS, co w
  symulacji z 2% brakujących impulsów w ciągu godziny dawało stratę 77 s; próg
  podwójnego zaliczenia wypróbowano najpierw na 500 ms, zanim ustalono go na
  750 ms.
- Zrzut z 02.09 pokazał, że parowanie SAW udawało się tylko w 23% przypadków
  (8439 z 36757, wobec 36755 ramek w 10 h) — co oznacza, że algorytm 13
  sterował na nieskorygowanym ząbku piłokształtnym przez około trzy czwarte
  każdego przebiegu, mimo że wyświetlacz pokazywał fazę skorygowaną.
- Dwudniowy zrzut (01–02.09) zmierzył efekt zatrzaśnięcia odczytu fazy w
  punkcie próbkowania ścieżki wyświetlacza zamiast późniejszego własnego
  odczytu pętli: dno szumu pierwszej różnicy spadło z 13,25 ns (przy
  przesunięciu o jeden impuls) do 2,55 ns.

**Ustawienia i flash**
- Bibliotekę EEPROM usunięto z projektu w buildzie v0.96.
- Opcję trwałości `GPSDO_EEPROM` usunięto w v1.00 na rzecz pierścienia flash.
- `gpsdo_settings_store.h` zastąpił funkcje emulowanego EEPROM ze STM32duino,
  które wcześniej mieszkały w `gpsdo_state.cpp`; to właśnie pełny zapis stanu
  do EEPROM ryzykował kiedyś nadpisaniem przez celowe `ES TZ`.
- `SETTINGS_VER` śledził rozrost układu ustawień: v2 dodała pola LTIC-Lars, v3
  okna tłumienia FA, v4 flagę LT, v5 blok algorytmu 12, a v6 skale wyjścia
  DV/AV.
- Wersjonowanie układu ustawień przeszło przez zwykłe podbicie numeru wersji, a
  potem osobne `else-if` na każdy `SETTINGS_VER` z ręcznie pisanym `offsetof`,
  zanim osiadło na progu `SETTINGS_V6_BYTES` (build 49), który pozwala nowemu
  polu obejść podbicie wersji.
- Pierścień flash ustawień/stanu jest zawsze włączony od v0.96, bez
  przełącznika w czasie działania i bez ścieżki awaryjnej do emulowanego
  EEPROM.

**Wyświetlacze**
- Zewnętrzny DAC AD5680 i wyświetlacz TM1637 kiedyś wykluczały się wzajemnie w
  czasie budowania z powodu konfliktu pinów; teraz automatyczny `#undef`
  rozwiązuje ten konflikt, więc oba można skompilować razem.
- Zadanie wyświetlacza kiedyś odczytywało Vphase i rozładowywało PA1 (otwarty
  dren) ze swojej 2-sekundowej pętli czujników — stanowczo za późno na pik
  trwający ułamek milisekundy, co zaburzało ładunek.
- Pasmo fali na ekranie powitalnym kiedyś było rysowane bezpośrednio na panelu
  przy każdym odświeżeniu; później podniesiono je do obecnej pozycji, gdy
  usunięto pasek nagłówka nad nim.
- Lista kontrolna sprzętu przy rozruchu kiedyś bezwarunkowo wymuszała
  `show=true` dla OLED i wiersza czujników, więc pojawiały się na liście nawet
  w buildach, w których nie były skompilowane.
- Wyświetlacz kiedyś pokazywał krótkie kody statusu (`SVIN`, `CAL`) zamiast
  pełnych nazw procedur, których używa teraz.
- Wcześniejsza wersja nie stosowała zerowego dopełnienia dla pola PWM na
  wyświetlaczu, zakładając, że formatowanie o stałej szerokości czyni je
  zbędnym; w praktyce ostatnia cyfra zachowywała na panelu widoczne fragmenty
  poprzedniej wartości.
- Wzorce kodowania wyświetlacza statusu TM1637 odpowiadają oryginalnemu (sprzed
  RTOS) projektowi firmware André Balsy.

**Kalibracja (CT/LC)**
- Na szerokim detektorze fazy HC74 stały próg 5 ns kiedyś zostawiał
  współczynnik tłumienia utknięty na swoim dnie, podczas gdy dryft wciąż
  oscylował.
- Nagłówek kiedyś cytował typową wartość ~5 µHz/LSB z karty katalogowej NDK
  ENE3311B; pomiar na tej konstrukcji pokazał rozbieżność około 70×, więc
  nagłówek podaje teraz zmierzone wartości.

**Inne**
- Wcześniejsza lista pinów błędnie wymieniała PA4, PA6, PA9 i PA10 jako również
  wolne dla zewnętrznego DAC; te cztery były błędne i listę usunięto.
- Ryzyko przepełnienia int8 w `g_time_offset` zgłoszono w audycie v0.95, na
  długo przed naprawieniem go przez poszerzenie pola do `g_time_offset_min`
  (int16, minuty).

Build 58, `GPSDO v1.08.58rt`: pierwsze dni builda 57 na prototypie V3 Dana
Wieringa, z obiema pozycjami zakresu po raz pierwszy skalibrowanymi na jednej
referencji. Żadna pętla się nie zmienia; zmienia się to, którym liczbom moduł
zakresu i `CT` gotowe są uwierzyć.

### Naprawione (build 58)
- **Para zakresu jest mierzona tylko z kodów, które są zerami (build 58).**
  Build 57 brał kod, który pętla w starej pozycji trzymała w chwili
  przestawienia, za zero tej pozycji zawsze wtedy, gdy pętla mówiła, że jest
  zablokowana, a stanowisko Dana obnażyło trzy sposoby, na które to nie
  wystarcza. W **holdoverze** pętla nie steruje, a pętla, która nie pracuje,
  nie aktualizuje też swojej flagi locka, więc flaga dalej mówi to, co mówiła
  wcześniej. **Kod ustawiony ręcznie** (`SP`, `up`/`dp`) siedzi pod flagą
  locka, która jeszcze tego nie zauważyła. Te dwa wynikały z tego, co się na
  stanowisku robiło — odczyty DMM w holdoverze, na kodach wpisanych przez `SP`
  — i prędzej czy później zapisałyby fałszywą parę. Trzeci zapisał dwie: na
  stanowisku łączonym przewodami przesuwnik może wejść w tor EFC albo z niego
  wyjść, zanim PB14 za nim pójdzie, pętla łapie wtedy lock na obiekcie jednej
  pozycji pod nazwą drugiej, a jej kod jest **zerem drugiej pozycji** —
  `FULL 54295 = REDUCED 54156`, złapana z przesuwnikiem już odłączonym i PB14
  jeszcze na REDUCED, oraz `FULL 38660 = REDUCED 39051`, z przesuwnikiem z
  powrotem w torze i PB14 jeszcze na FULL. Następne przestawienie liczy przez
  taką parę tam, gdzie ona wskaże: przy drugiej 1,4e-7 obok.

  Teraz w holdoverze, ręcznym czy automatycznym, kotwica nie jest brana i nie
  jest w nim domykana; po `SP`, `up1`/`up10` albo `dp1`/`dp10` nie jest brana,
  dopóki pętla nie zostanie zobaczona bez locka; a każda pozycja trzyma swoje
  **zero** — kod, na którym `CT` znalazło 10 MHz, odnawiany przez każdą parę
  zmierzoną od tego czasu i pokazywany przez `SPAN` — i kod jest uznawany za
  zero pozycji tylko wtedy, gdy leży w granicach 5e-8 (0,5 Hz) od niego i nie
  bliżej zera drugiej pozycji; obie odległości w hercach, każda przez K swojej
  pozycji. W granicach 5e-9 od własnego zera kod jest przyjmowany bez względu
  na to, czego jeszcze jest blisko: płytka, której oba zera leżą prawie na tym
  samym kodzie, odrzucałaby inaczej wszystko, a para wzięta na takim kodzie
  myli się najwyżej o tyle. Sprawdzane są obie połówki pary, a połówka starej
  pozycji jeszcze raz, gdy zero nowej pozycji jest już w ręku — i to łapie parę
  taką jak pierwsza, gdzie FULL nie miało własnego zera aż do `CT`, które ją
  domknęło; a gdy stara pozycja nie ma jeszcze zera, przy sprawdzaniu nowego
  locka zastępuje je jej kotwica. Każda odmowa jest mówiona na konsoli, a
  odmowa z testu zera podaje swoje odległości.

  **Zasymulowane.** `tools/spansim` dostaje operatora (`mh`/`md`, `sp:N`,
  `clr:0|1`) i PB14, które da się przytrzymać niezależnie od zworki, oraz pięć
  przypadków — czwarty na płytce, której kalibracja FULL pochodzi sprzed tego
  builda i nie ma zera, pozostałe na takiej, której obie pozycje mają własne
  `CT`. Po każdym czyste przestawienie pokazuje, ile warta jest zapisana para:

  | | build 57 | build 58 |
  |---|---|---|
  | holdover, `SP 52000`, przestawienie, `MD` — następne przestawienie ląduje przy | 3,3e-8 | 3,6e-12 |
  | `SP 52000` pod działającą pętlą, przestawienie od razu | 3,3e-8 | 1,3e-12 |
  | przesuwnik w torze, PB14 trzymane wysoko, potem puszczone | 1,2e-7 | 8,6e-12 |
  | PB14 trzymane nisko, przesuwnik jeszcze odłączony | 3,0e-8 | 1,3e-12 |
  | przesuwnik odłączony, PB14 trzymane nisko, `SPAN CLR FULL`, `CT` | zapisana fałszywa para | żadna |

  W ostatnim przypadku następne przestawienie ląduje 3,0e-8 obok w obu
  buildach — fałszywa para przypadkiem prawie nie zmieniała kodu, a bez pary
  kod zostaje — więc różni je tylko fałszywa para zostawiona we flashu.

  Zera pochodzą z `CT` i tylko z `CT`; zmierzona para odnawia zero, przeciw
  któremu właśnie przeszła test, i nigdy żadnego nie tworzy. Rekord zapisany
  przed tym buildem nie ma żadnego, a dopóki w danej pozycji nie przejdzie
  kolejne `CT`, jej kod z locka jest brany tak jak zawsze — na płytce
  skalibrowanej buildem 57 test włącza więc jedno `CT` w każdej pozycji.
  `SPAN CLR` zapomina zero pozycji razem z jej K. Cena testu „bliżej":
  oscylator, który od ostatniego odnowienia własnego zera przesunął się, w
  hercach, dalej niż do połowy drogi do zera drugiej pozycji — na płytce Dana,
  której dwa zera dzieli 15 000 kodów, to około 3e-8 — zostaje wzięty za zły
  obiekt, co kosztuje parę niezmierzoną przy tym przestawieniu, nigdy parę
  błędną. Zapisywane jako 4 bajty dopisane na końcu bloku ustawień (396 → 400
  bajtów, bez zmiany wersji, sprawdzone `offsetof` pod arm-none-eabi); build 57
  czyta taki rekord i je pomija.
- **`CT`, które się nie uda, oddaje pętli jej kod (build 58).** Każde wyjście
  po błędzie zapisywało z powrotem kod trzymany w stanie sterowania — który
  przebieg ustawia na bieżący punkt dla wyświetlaczy — więc nieudane `CT`
  zostawiało oscylator na ostatnim kodzie przebiegu zamiast tam, gdzie miała go
  pętla. Najbardziej liczy się to przy automatycznym `CT` pozycji zakresu,
  która nie ma własnego, ponawianym do czterech razy pod działającą pętlą, i
  przy przestawieniu zworki zauważonym w trakcie `CT`, które było wtedy
  liczone — i mogło zostać zakotwiczone — od punktu przebiegu. Teraz
  przywracany jest kod, który `CT` zastało na pinie, gdy startowało, średnie
  częstotliwości, opisujące przebieg, są czyszczone, a statystyka korekcji
  zerowana — jak po `CT`, które się udało.
- **`LRN` czyta cały argument (build 58).** Używało `atoi()`, które czyta tyle
  cyfr, ile się da, a resztę pomija: `LRN 1e3` **włączało** uczenie (czytało
  1), `LRN ON` je **wyłączało** (bez cyfr, 0), `LRN 2500.7` ustawiało 2500.
  `0` i `1` to teraz dokładnie te dwa słowa, które przełączają samouczenie, a
  wszystko inne musi dać się przeczytać do końca jako liczba: zakres detektora
  w ns, 2…1e9, z ułamkiem i wykładnikiem. Inne słowa są odrzucane z listą form,
  które są przyjmowane. `LRN R` zapisuje reset od razu, w danych live, zamiast
  podpowiadać `ES FLAGS`, które zapisuje tylko przełącznik — reset trwał
  dotąd do następnego wyłączenia zasilania, chyba że dane live zdążyły się
  zapisać wcześniej. Pole `LRN` w tunerze odmawia zakresu poniżej 2 i wysyła
  zwykły zapis dziesiętny: dla miliona pisało `1e+06`, co v1.06 i v1.07
  czytały jako 1, uczenie włączone.

### Zmienione (build 58)
- **Drugi przebieg `CT` czyta średnie 100-sekundowe, na tych samych 0,8 Hz w
  obu pozycjach zakresu (build 58).** `avg10` to zliczenie z dziesięciu sekund
  podzielone przez dziesięć: kroki 0,1 Hz i, jako różnica dwóch przechwyceń,
  błąd do jednego kroku (σ ≈ 0,04 Hz) — jakieś 7 % K przy 0,8 Hz drugiego
  przebiegu. K wychodziło więc skokami: REDUCED na płytce Dana czytało na
  przebiegu 0,8 albo 0,9 Hz (40873, 36331 i 37113 LSB/Hz) i nic pomiędzy.
  Każdy punkt drugiego przebiegu to teraz średnia ze 100 s wzięta w całości po
  ustaleniu — średnie są czyszczone, gdy wyjście się ustali, i czytane, gdy
  jest 100 świeżych sekund — z krokiem 0,01 Hz. Monte Carlo licznika z 0–20 ns
  jittera 1PPS daje rozrzut K drugiego przebiegu 7–9 % przedtem i poniżej 1 %
  teraz.

  Rozstaw nie jest też już trzymany na co najmniej 20 480 LSB z pierwszego
  przebiegu, co na pozycji FULL płytki Dana dawało 1,8 Hz na 1,3 V EFC wobec
  0,8 Hz na 0,5 V na REDUCED: dwa różne odcinki krzywej oscylatora, która nie
  musi być prosta (1,41 Hz/V zmierzone na jednym, 1,74 na drugim, razem z
  rozdzielczością `CT`). Stała huśtawka w hercach to stały odcinek krzywej
  wokół zera, więc obie pozycje są teraz mierzone na tym samym. Drugi przebieg
  działa tylko przy łagodnym obiekcie (K poniżej 0,12 mHz/LSB); tam trwa około
  ośmiu minut zamiast trzech, a odliczanie je uwzględnia.

### Dokumentacja (build 58)
- **K obu pozycji różnią się 3,3–3,6 razy, nie cztery do pięciu.** Zmierzone
  na jednej referencji na płytce Dana: 11378 LSB/Hz w pełnym zakresie, 37113 i
  40873 w zawężonym. Dzielnik to 4,03:1 (DMM); pełny tor dostarcza 94,7 %
  napięcia DAC-a na wejście EFC oscylatora (około 785 kΩ wobec 44 kΩ toru), co
  zostawia 3,8 razy; reszta to krzywa strojenia i rozdzielczość `CT`.
  Poprawione w instrukcjach, README, pomocy tunera i komentarzach w źródłach,
  z adnotacją we wpisie v1.07 poniżej.
- Instrukcje: zasady pary, zero w `SPAN`, czas `CT`, formy `LRN`, co od razu
  zapisuje dane live; akceptowany zakres K to 0,01–2 mHz/LSB (stało jeszcze
  0,02); dwie osobliwości `LRN` z listy dla v1.07 zniknęły razem z poprawkami.
  README: sekcja `CT` (drugi przebieg, zakres 0,01–2 mHz/LSB — stało 0,1 — i
  samoczynny zapis grupy PID tam, gdzie wciąż kazała uruchomić `ES`), wiersze
  `LRN` i `LPOL` (`LPOL 0` to „nie ustawione", nie „auto"). Przykładowa linia
  stanu w README tunera podaje v1.08.58rt.
- Komentarze: `span_begin()` działa przed zapisem kodu, od którego startuje
  pętla, a nie przed pierwszym zapisem DAC-a — odtworzony kod jest na DAC-u
  przez kilka milisekund, które trwają odczyty; gałąź telemetrii, która
  nazywała algorytm 10 „nie pętlą LRN" (jedzie na feed-forwardzie LRN,
  uczonym w LOCK i stosowanym w każdym stanie); nick Dave'a pisany wszędzie
  Solder_Junkie.
- `TOOL_VERSION` tunera to 1.08.

**Aktualizacja z v1.07:** nic nie trzeba robić — ustawienia przechodzą. Żeby
na płytce skalibrowanej przed buildem 58 włączyć test zer, uruchom `CT` raz w
każdej pozycji zakresu.

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
  jednej referencji obie pozycje różnią się o dzielnik, 4,1–4,7 razy
  *(tak sądzono przy wydaniu; zmierzone potem na jednej referencji: 3,3–3,6
  razy, zob. v1.08)*. Z tej liczby wyprowadzany jest każdy współczynnik pętli,
  więc do tej pory płytka po
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
  je ratuje (raport z pola: Solder_Junkie). Nowy przełącznik kompilacji
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
