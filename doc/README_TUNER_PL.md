# GPSDO Tuner

[English](README_TUNER_EN.md) | **Polski** | [Español](README_TUNER_ES.md)

📖 [Strona projektu](../README.md) · [README](README_PL.md) · Instrukcja: [MD](MANUAL_PL.md) · [PDF](MANUAL_PL.pdf)

Konsola na PC do strojenia pętli na żywo i obserwowania, co robi: trzy
przewijające się wykresy, po jednej zakładce na grupę parametrów oraz pole
komend ręcznych na wszystko, czego zakładki nie obejmują.

To **narzędzie do strojenia**, nie przyrząd pomiarowy — przed wyciąganiem
wniosków z tego, co pokazuje, przeczytaj *Ograniczenia* poniżej.

---

## Wymagania

**Python** 3.9 lub nowszy oraz cztery pakiety:

```
pip install PySide6 pyqtgraph pyserial tzdata
```

| Pakiet | Do czego |
|--------|----------|
| PySide6 | interfejs użytkownika Qt |
| pyqtgraph | wykresy na żywo |
| pyserial | komunikacja z płytką |
| tzdata | wyłącznie przycisk **Generate gpsdo_tz_table.h** |

`tzdata` jest opcjonalne, jeśli nigdy nie używasz tego przycisku, a na Linuksie
i macOS system dostarcza te same dane stref. Na Windows to jedyne źródło, bo
system nie zawiera bazy IANA. Późniejsza aktualizacja: `pip install -U tzdata`.

Uruchom przez `python gpsdo_tuner.py` albo dwuklikiem w Windows: otwierające się
okno konsoli jest automatycznie minimalizowane na pasek zadań i pozostaje
dostępne, gdyby trzeba było odczytać komunikat błędu.

---

## Zgodność wersji

Tuner ma `TOOL_VERSION` śledzące wydanie firmware, dla którego powstał. Przy
połączeniu odczytuje wersję z płytki i porównuje:

- **zgodność** — pasek stanu pokazuje całą tożsamość płytki, na ile ją podała:

  ```
  connected — firmware v1.07.57rt  2026-09-23 11:02  CRC 5A41C90B
  ```

  Te trzy fakty odpowiadają na różne pytania. **Wersja** mówi, jakim protokołem
  tuner rozmawia. **Build** — od builda 56 w samej nazwie, `.57rt` (sam build
  56 pisał `-rt56`); starszy firmware (`v1.07-rtos`) podaje go osobno i linia pokazuje `build N` — i czas
  kompilacji mówią, z którego drzewa źródeł to pochodzi. **CRC** mówi, który binarny obraz naprawdę działa, i jako jedyne nie
  może być nieaktualne — płytka liczy je z własnego flasha przy starcie, więc
  pozostaje uczciwe nawet wtedy, gdy builder Arduino użyje ponownie starego
  pliku obiektowego, a znacznik czasu tego nie odnotuje. Wszystko poza wersją
  jest opcjonalne: starszy firmware odpowiada na `V` samą nazwą i linia po
  prostu mówi mniej.
- **niezgodność** — pasek stanu i monitor surowy mówią o tym wprost

Niezgodność nie jest błędem krytycznym i tuner nadal rozmawia z płytką, ale
należy się spodziewać dziwnie czytanych pól albo odrzucanych komend: starszy
tuner nie zna nowszej telemetrii, a nowszy może wysyłać komendy, których płytka
nigdy nie widziała. Używaj pary, która przyszła razem.

---

## Zakładki

| Zakładka | Przeznaczenie |
|----------|---------------|
| **PID algo 3-9** | Kp / Ki / Kd / I_LIMIT dla algorytmów częstotliwościowych |
| **LTIC (algo 10)** | PID per stan trójstopniowej pętli fazowej, kalibracja detektora oraz okno uśredniania członu tłumiącego (`FAD` / `FAL`, per stan) |
| **LTIC-Lars (algo 11)** | Parametry ciągłej pętli PI (`LG`, `LD`, `LTC`, …) |
| **LTIC-MLA (algo 12)** | Dwa skalary (`MG`, `MR`) i jedenaście limitów fazy per poziom |
| **Calibration** | `LC`, `CT` i stałe detektora |
| **Raw monitor** | Wszystko, co wysyła płytka, bez parsowania |
| **Help** | Pełny wykaz komend firmware |

Każda grupa parametrów jest odczytywana przy połączeniu, więc panele startują
wypełnione, a nie puste. Zakładki idą wg numerów algorytmów. Komendy opisujące
płytkę, nie pętlę — skale toru wyjściowego (`DV`, `AV`, `VS`), zworka zakresu
EFC (`SPAN`), podświetlenie (`BL`) — nie mają własnej zakładki: przechodzą
przez pole komend, a zakładka **Help** dokumentuje je razem z resztą.

---

## Wykresy

Trzy okna, odświeżane raz na sekundę. To, co pokazują dwa górne, zależy od tego,
jaki algorytm zgłasza płytka:

| | Algorytmy 10 / 11 (LTIC) | Algorytm 12 | Algorytm 13 | Algorytmy 0-9 |
|---|---|---|---|---|
| Górne | Faza `dph` (ns) | Błąd fazy `ph` (ns) | **Estymata** fazy `ph` (ns) | Wyuczony dryf (LSB) |
| Środkowe | `Vphase` detektora (V), z liniami pasma | Napięcie sterujące `Vctl` (V) | `Vphase` detektora (V), z liniami pasma | Napięcie sterujące `Vctl` (V) |
| Dolne | Błąd częstotliwości (Hz) | Błąd częstotliwości (Hz) | Błąd częstotliwości (Hz) | Błąd częstotliwości (Hz) |

Tylko pętle LTIC mają detektor fazy, więc przy każdym innym algorytmie te dwa
okna stałyby puste przez całą sesję. Zamiast tego są przekierowane na inne
wielkości, a tytuły zmieniają się automatycznie — nie ma czego przestawiać.

Algorytm 12 dostaje własną parę, a nie pożycza żadnej z pozostałych: nie używa
samouczącego się sprzężenia w przód, więc wykres dryfu byłby płaski, a jego faza
idzie prosto z detektora, a nie przez filtr pętli — to nie jest ta sama wielkość,
którą rysuje `dph`. Linie pasma detektora znikają zawsze wtedy, gdy środkowe
okno pokazuje zamiast niego napięcie sterujące.

Algorytm 13 rysuje to, w co filtr Kalmana WIERZY, że jest fazą, a nie odczyt z
tej sekundy — ta estymata jest właśnie sensem posiadania filtru — a pod nią
`Vphase`, bo pytanie, które ta pętla stawia najczęściej, brzmi: czy detektor w
ogóle żyje. Na początku wpadał do pary 0-9, więc górne okno nosiło podpis
„Wyuczony dryf" nad serią, której algorytm 13 nigdy nie wysyła.

### Span i Follow

**Span** ustala, ile historii jest widoczne: 1 min, 5 min, 15 min, 1 h albo
*all*. Z wybranym oknem wykres przewija się w lewo ze stałą skalą, zamiast
rozciągać oś na cały bufor.

**Follow live** trzyma okno przypięte do najnowszej próbki. Przeciągnij wykres
lub przewiń kółkiem, a opcja sama się odznaczy, oddając oś myszy — wtedy można
przejrzeć cały bufor. Zaznacz ją ponownie (albo zmień Span), by wrócić na żywo.

**Clear plots** odrzuca wszystkie zbuforowane próbki i restartuje oś czasu od
zera — przydatne po nieudanym starcie i szybsze niż restart narzędzia, przy
którym traci się połączenie. Czyści bufory razem z krzywymi, więc nic nie wróci
później na wykres.

**About** odtwarza animację startową, bez powodu innego niż przyjemność.

---

## Ograniczenia

**Historia sięga tygodnia.** Tuner trzyma 604 800 próbek przy telemetrii 1 Hz.
Wszystko starsze jest odrzucane w miarę napływu nowych danych i nie da się tego
odzyskać; nic z wykresów nie trafia na dysk. Bufory to tablice liczb
podwójnej precyzji, a nie listy pythonowych floatów, więc pełny tydzień
wszystkich serii kosztuje około 82 MB RAM zamiast 406 — i nic nie jest
alokowane z góry, więc pięciominutowa sesja nadal kosztuje kilobajty.

**Wykresy nie są rejestratorem.** Dane wykresów żyją wyłącznie w pamięci i
znikają po zamknięciu okna. Do wszystkiego, co chcesz zachować, użyj **Start
logging** (zakładka Raw monitor) — patrz niżej.

### Co zapisuje logowanie

Lista rozwijana obok **Start logging** wybiera format i jest zamrożona na czas
życia pliku:

| Ustawienie | Zapisuje | Około tygodnia |
|---|---|---|
| **Full log** | każdą odebraną linię, dokładnie tak jak wypisana, do `gpsdo_RRRR-MM-DD_GG-MM-SS.log` | ~217 MB |
| **CSV only** | jeden wiersz na sekundę telemetrii, tylko kolumny analityczne, do `…​.csv` | ~65 MB |
| **Both** | ten sam przebieg zapisany do obu plików | ~282 MB |

Oba otwierane są obok skryptu z buforowaniem liniowym, więc przebieg zakończony
awarią zostawia użyteczne dane, a nie pusty plik niezrzuconych buforów.

**Pełny log** to surowy tekst telemetrii — wszystko, co powiedziała płytka,
łącznie z odpowiedziami CLI i banerami startowymi. To format dla kogoś, kto ma
na przebieg *popatrzeć*, a nie na nim liczyć, i jedyny, który zachowuje
cokolwiek, na co CSV nie ma kolumny.

**CSV** jest do liczenia: `pandas.read_csv` i `numpy.loadtxt` czytają go z
domyślnymi ustawieniami, bo dwie linie proweniencji zaczynają się od `#`.
Kolumny to to, czego każda dotychczasowa analiza tych logów faktycznie
potrzebowała, a nie wszystko, co firmware wypisuje:

```
utc, up_s, algo, state, dph_ns, qerr_ns, vphase_v, pwm, f10, f100,
ph_ns, level, corr, sig_ns, zc, bmp_c, sat, hdop
```

`ph_ns`, `level`, `corr`, `sig_ns` i `zc` to diagnostyka algorytmu 12 i pozostają
puste przy każdym innym algorytmie; `f100` jest puste, dopóki okno 100 s się nie
wypełni. Pusta komórka zawsze znaczy *tego pola nie było w telemetrii tej
sekundy*, nigdy zero.

Trzy kolumny wymagają komentarza:

- **`up_s` jest wiarygodne dopiero od firmware'u v1.06.** Zmierzone na 75 055
  blokach z v1.05: UTC szło do przodu dokładnie o sekundę za każdym razem,
  podczas gdy licznik uptime powtórzył lub przeskoczył sekundę 118 razy (0,16 %)
  i zyskał 12 s w 20,8 h. Liczony był z wolnobieżnego timera MCU, który idzie o
  jakieś 159 ppm za szybko; v1.06 liczy go z PPS. Tuner zapisuje obie kolumny
  dokładnie tak, jak przyszły, i żadnej nie naprawia — rejestrator, który po
  cichu poprawia swoje wejście, nie nadaje się do znajdowania takich rzeczy —
  więc w zapisie z v1.05 lub starszego używaj `utc`.
- **`hdop` nie zawsze jest liczbą.** LEA-T po zakończonym survey-in wypisuje
  `HDOP:TIME`, a ta flaga jest ważniejszym z dwóch faktów — to tryb, w którym
  1PPS jest wart zaufania. Jeśli chcesz mieć kolumnę liczbową, parsuj ją z
  `errors="coerce"`.
- **`vphase_v`** to surowe napięcie rampy detektora i jedyna kolumna, która
  ujawnia detektor na ograniczniku. `dph_ns` policzone z rampy na ograniczniku
  wygląda jak zwyczajna liczba.

Świadomie pominięte: `Vctl` (to `pwm` przez RC, a `pwm` jest liczbą dokładną),
wilgotność, ciśnienie i szyny INA (przez wszystkie dotychczasowe przebiegi nigdy
nie ruszyły się na tyle, żeby cokolwiek wyjaśnić). **Kolumn z pozycją nie ma w
ogóle**, więc CSV jest zanonimizowany z konstrukcji, niezależnie od checkboxa.

**Redact position** (obok przycisku logowania, domyślnie włączone) dotyczy
**pełnego logu** — CSV nie ma kolumn z pozycją, więc nie ma tam czego
anonimizować. Zastępuje `Lat` / `Lon` / `Alt` odbiornika symbolami zastępczymi
**tylko w zapisywanym pliku** — Raw monitor i wykresy nadal pokazują prawdziwy fiks. Liczba satelitów,
HDOP i znacznik TIME zostają: są diagnostyczne i nie mówią nic o tym, gdzie
jesteś.

Log telemetrii to jest właśnie ten plik, który trafia na forum albo do kogoś,
kto zaproponował pomiar Twojej płytki, a każda jego sekunda niesie pozycję z
sześcioma miejscami po przecinku — czyli z dokładnością do kilkunastu
centymetrów. Czyszczenie po fakcie działa, ale zależy od pamiętania, a ten jeden
raz, kiedy się zapomni, jest tym razem, kiedy plik już poszedł. Ustawienie jest
zamrażane w chwili otwarcia pliku, a pole wyszarzone do końca logowania — więc
log jest albo w całości zanonimizowany, albo w całości nie; plik zanonimizowany
częściowo wygląda na bezpieczny, a nie jest. Tak czy inaczej log sam mówi, który
to przypadek, w swojej drugiej linii.

**Generate gpsdo_tz_table.h** przebudowuje tablicę stref czasowych firmware'u z danych
IANA na tej maszynie i zapisuje `gpsdo_tz_table.h` obok skryptu. Zastępuje dawny
`gen_tz_table.py`, więc tuner jest teraz jedynym skryptem do utrzymania.

Dane stref pochodzą z bazy systemowej na Linuksie/macOS albo z pakietu `tzdata`
Pythona — i tak to działa na Windows, gdzie system nie dostarcza żadnej bazy
IANA. Jeśli przycisk zgłosi brak danych, uruchom `pip install tzdata`; aby je
później odświeżyć, `pip install -U tzdata`. Wygenerowany nagłówek zapisuje, z
którego wydania IANA pochodzi, o ile da się to ustalić.

> Sama IANA publikuje *źródła* wymagające kompilatora `zic`, więc pobieranie
> bezpośrednio od nich nic by nie dało — pakiet `tzdata` to te same dane już
> skompilowane.

**Okno Raw monitor trzyma tylko ostatnie ~2500 linii** (niecałe pięć minut przy
tempie telemetrii). To ograniczenie wyświetlania, nie zapisu: po włączeniu
logowania plik dostaje wszystko, niezależnie od tego, co okno jeszcze pokazuje.

**Rozdzielczość to tempo telemetrii.** Jedna próbka na sekundę, więc cokolwiek
szybszego niż około 2 s jest niewidoczne: szybki cykl graniczny albo jitter na
pojedynczych pulsach się nie pojawi, a to, co widzisz, zostało już uśrednione
wewnątrz firmware.

**Jedno połączenie naraz.** Port szeregowy jest na wyłączność. Zamknij najpierw
inny terminal na tym samym porcie i pamiętaj, że tuner trzyma go, dopóki jest
otwarty.

**Wykresy ufają płytce.** Wartości są parsowane z tekstu telemetrii tak, jak
przyszły. Jeśli firmware zgłosi nieaktualną lub błędną liczbę, tuner narysuje ją
wiernie — niczego nie weryfikuje krzyżowo.

**Zapisy nie są trwałe.** Ustawienie parametru zmienia go tylko w RAM.
Parametry strojenia pętli wymagają jawnego `ES` (odpowiedź podaje dokładną
komendę); preferencje zapisują się same i mówią o tym.

---

*Część GPSDO FreeRTOS — [instrukcja firmware](README_PL.md) · [changelog](CHANGELOG_PL.md) · [repozytorium](https://github.com/jmnlabs/GPSDO_FreeRTOS)*
