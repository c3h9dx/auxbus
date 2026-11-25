# Elecraft AUXBUS - Reverse Engineering

[English](README.md) | Polski

> AUXBUS jest zastrzeżonym protokołem firmy Elecraft. Niniejsza
> dokumentacja powstała na podstawie analizy podsłuchanej komunikacji
> urządzeń K4, K3, KPA1500, KPA500 i KRC2 oraz ogólnodostępnej
> dokumentacji Elecraft. Nie jest to dokumentacja oficjalna i może
> zawierać błędy.

AUXBUS to jednoprzewodowa magistrala, którą radia Elecraft (K4, K3)
wymieniają informacje z akcesoriami: wzmacniaczami KPA1500 i KPA500,
przełącznikiem KRC2 i transwerterami?. Radio przekazuje
pasmo i częstotliwość, a wzmacniacz zgłasza swój stan, błędy, przyciski
pasm i żądanie nośnej do strojenia.

## Zawartość repozytorium

| Ścieżka | Zawartość |
|---|---|
| [`AUXBUS.pl.md`](AUXBUS.pl.md) | specyfikacja protokołu: warstwa fizyczna, ramki K\* i ACC, polecenia, czasy, wskazówki dla implementacji |
| [`saleae_lla/`](saleae_lla) | Low Level Analyzer (C++) dla Saleae Logic 2: „Elecraft AUXBUS LLA” |
| [`saleae_hla/`](saleae_hla) | High Level Analyzer (Python) dla Saleae Logic 2: „Elecraft AUXBUS HLA” |
| [`auxbus_tool/`](auxbus_tool) | monitor i nadajnik ramek AUXBUS na Arduino Uno |
| [`examples/`](examples) | nagrania komunikacji w formacie Saleae Logic 2 (`.sal`) |

---

## Saleae Logic 2

Analizator działa w dwóch warstwach:

- **LLA** dzieli przebieg na ramki i próbkuje bity tak, jak robi to
  odbiornik danej ramki. Ramki K\* próbkuje w środkach bitów 476,1 µs,
  ramki ACC tak, jak odbiera je K4, a bajt response tak, jak odczytuje go
  K4.
- **HLA** składa bity w ramki i opisuje je: źródło, typ, pola, a na końcu
  interpretację.

### Kompilacja LLA

Wymagane są CMake ≥ 3.13, kompilator C++17 i dostęp do sieci: przy
pierwszej konfiguracji CMake pobiera
[Saleae AnalyzerSDK](https://github.com/saleae/AnalyzerSDK).

```bash
cd saleae_lla
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Wynikowa biblioteka:

| System | Plik |
|---|---|
| macOS | `build/libauxbus_lla.dylib` |
| Linux | `build/libauxbus_lla.so` |
| Windows | `build/Release/auxbus_lla.dll` |

### Instalacja w Logic 2

1. **LLA:** w *Preferences → Custom Low Level Analyzers* wskaż katalog
   z biblioteką (np. `saleae_lla/build`) i zrestartuj Logic 2. Po każdej
   podmianie biblioteki Logic 2 trzeba zrestartować.
2. **HLA:** w panelu *Extensions* wybierz *Load Existing Extension…* i
   wskaż plik `saleae_hla/extension.json`.

### Użycie

1. Podłącz kanał analizatora do linii AUXBUS, a masę analizatora do
   masy radia. Wystarczy próbkowanie 1 MS/s.
2. Dodaj analizator **Elecraft AUXBUS LLA**:
   - *AUXBUS*: kanał z linią AUXBUS,
   - *Amplifier LOW threshold [us]*: LOW o co najmniej tej długości
     oznacza ramkę ACC. Domyślnie 10 000 µs; ramki K\* mają LOW ≈ 1900 µs,
     ramki ACC ≈ 80 000 µs.
3. Dodaj analizator **Elecraft AUXBUS HLA** i jako wejście wybierz LLA.

Na wykresie LLA pokazuje:

- `PRE`: preambułę, od zbocza opadającego do pierwszej próbki. Strzałka
  w górę oznacza zbocze narastające `t_r`, od którego liczone są próbki.
- bity: okna wokół chwil próbkowania, ze znacznikiem `1` albo `0` w
  miejscu próbki. Czerwona kropka oznacza zbocze bliżej niż 10 % okresu
  bitu od próbki, czyli niepewny odczyt.
- `IDLE`: spoczynek do następnej ramki.

Jeśli po ostatniej próbce linia jest nadal LOW, LLA zaczyna w tym
miejscu nową ramkę i oznacza ją jako kolizję. LOW zaraz po oknie
response jest oznaczane jako ogon odpowiedzi (patrz
[AUXBUS.pl.md §2.5](AUXBUS.pl.md#25-response)).

Przykładowe wyniki HLA:

```
K* BAND cmd=0x20 data=0x02 response=0xFF  band 40m
K* XVTR cmd=0x21 data=0x00 response=0xFF  XVTR off (HF band)
K* FREQ value=0x437828  7105 kHz, checksum OK
K* STATUS cmd=0x34 data=0x01 response=0xA1  read KRC2 status char; K4 stores response: AX; -> AXA1;
KPA*500 STATE byte=0x37  on (KPA1500)
KPA*500 FAULT byte=0x29  fault 9: SWR too high
? ? byte=0x7F  unknown, ignored by K4, OPER  ! response tail after K4FP window, K4FP reads it as an amp frame
```

- Źródło: `K*` to radio, `KPA*500` to ramka ACC, `?` to ogon odpowiedzi
  odebrany przez K4 jako ramka ACC.
- Ramka CMD zawsze ma pole `response` w postaci, w jakiej odczyta je K4.
  `0xFF` oznacza brak odpowiedzi.
- Ostrzeżenia, na przykład kolizja, LOW preambuły spoza 1,6–2,2 ms albo
  zbocze przy próbce, są na końcu wiersza po `!`.

### Nagrania przykładowe

Pliki z katalogu [`examples/`](examples) otwiera się w Logic 2 przez
*File → Open Capture*. Potem wystarczy dodać analizatory jak wyżej.

---

## Arduino AUXBUS

Szkic [`auxbus_tool`](auxbus_tool) zamienia Arduino Uno w
monitor i nadajnik AUXBUS:

- odbiera ramki K\* i ACC i wypisuje je razem z interpretacją,
- `AXccdd;` nadaje ramkę K\* CMD i od razu wypisuje response, odczytany
  tak jak w K4,
- `AAdd;` nadaje ramkę ACC,
- `PU1;` włącza podciąganie AUXBUS do 1.

### Podłączenie

| Arduino | AUXBUS |
|---|---|
| D2 | linia AUXBUS |
| GND | masa |

D2 pracuje jako wyjście open-drain: `0` to wyjście LOW, `1` to wejście
w stanie wysokiej impedancji. Linii nigdy nie wymusza się w stan HIGH.
Po starcie podciąganie jest wyłączone. Gdy na magistrali nie ma innego
podciągnięcia, na przykład przy samym KRC2 bez radia, włącz wewnętrzny
pull-up komendą `PU1;`.

### Wgranie

Otwórz `auxbus_tool/auxbus_tool.ino` w Arduino IDE i wgraj na
Arduino Uno albo użyj arduino-cli:

```bash
arduino-cli compile --fqbn arduino:avr:uno auxbus_tool
arduino-cli upload  --fqbn arduino:avr:uno -p <port> auxbus_tool
```

Port szeregowy: 115200 bit/s. Komendę kończy `;`, Enter albo oba.
Wielkość liter nie ma znaczenia.

### Komendy

| Komenda | Działanie |
|---|---|
| `AXccdd;` | nadaje ramkę K\* CMD (cc = command, dd = data, hex), potem odczytuje response w oknie K4 i wypisuje ramkę razem z nim |
| `AX;` | ostatni response, odpowiedź `AXnn;` |
| `AAdd;` | nadaje ramkę ACC z bajtem dd = `00`…`7F` (bit 6 = OPER) |
| `PU1;` / `PU0;` | podciąganie AUXBUS do 1 (pull-up na D2) włączone / wyłączone |
| `PU;` | stan podciągania, odpowiedź `PU0;` albo `PU1;` |
| `?;` | pomoc i stan linii |

Program odsyła na bieżąco każdy wpisany znak (echo), Backspace kasuje
ostatni znak. Jeśli w trakcie pisania przyjdzie ramka, wpisany tekst
pojawia się ponownie pod nią. Błędna komenda daje `?;`. Przed nadaniem program czeka, aż magistrala
będzie wolna przez 15 ms. Jeśli nie zwolni się w ciągu 2 s, ramka nie
zostanie wysłana.

Przykładowe bajty ACC (pełna lista w [AUXBUS.pl.md §6](AUXBUS.pl.md#6-polecenia-acc)):

| Komenda | Ramka |
|---|---|
| `AA03;` | przycisk pasma 40 m, STBY |
| `AA43;` | przycisk pasma 40 m, OPER |
| `AA29;` | błąd 9: SWR too high |
| `AA37;` | włączony (KPA1500) |
| `AA35;` / `AA36;` | KEY START / KEY STOP |

Ramki K\* nadawane komendą `AX` K4 odbiera tak, jak każde LOW na
magistrali, czyli jako ramkę ACC. Komenda ma sens przede wszystkim przy
akcesoriach bez radia na magistrali (KRC2, KPA1500).

### Wydruk

Każdy wiersz zaczyna się od kierunku: `RX` oznacza ramkę odebraną, `TX`
nadaną. Dalej są źródło (`K*` albo `ACC`), typ, pola i interpretacja.
Ostrzeżenia są na końcu po `!`.

```
RX K* BAND cmd=0x20 data=0x02 response=0xFF  band 40m
RX K* XVTR cmd=0x21 data=0x00 response=0xFF  XVTR off (HF band)
RX K* FREQ value=0x437828  7105 kHz, checksum OK
RX ACC STATE byte=0x37  on (KPA1500, RADIO TYPE = K4)
TX K* STATUS cmd=0x34 data=0x01 response=0xA1  KRC2 status char
RX ACC ? byte=0x7F  unknown, ignored by K4, OPER  ! response tail, K4 reads it as an ACC frame
```

Ostrzeżenia:

| Ostrzeżenie | Znaczenie |
|---|---|
| `response tail` | LOW zaraz po oknie response, K4 odbiera je jako ramkę ACC (AUXBUS.pl.md §2.5) |
| `collision` | po ostatniej próbce linia była nadal LOW: nowa ramka zaczęła się w trakcie poprzedniej |
| `preamble LOW N us` | LOW preambuły K\* poza zakresem 1,6–2,2 ms |
| `edge near sample N` | zbocze bliżej niż 10 % bitu od próbki N, odczyt niepewny |
| `AUXBUS held LOW for more than 1 s` | linia trzymana w stanie LOW, K4 nie może nadawać |

Przykład: wzmacniacz zgłasza przycisk 40 m, a radio odpowiada zmianą
pasma i ramkami BAND, XVTR i FREQ.

```
AA03;
TX ACC BAND byte=0x03  band button 40m
RX K* BAND cmd=0x20 data=0x02 response=0xFF  band 40m
RX K* XVTR cmd=0x21 data=0x00 response=0xFF  XVTR off (HF band)
RX K* FREQ value=0x437828  7105 kHz, checksum OK
```
