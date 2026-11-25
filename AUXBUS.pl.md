# Elecraft AUXBUS

[English](AUXBUS.md) | Polski

> AUXBUS jest zastrzeżonym protokołem firmy Elecraft. Niniejszy opis
> powstał na podstawie analizy podsłuchanej komunikacji urządzeń K4, K3,
> KPA1500, KPA500 i KRC2 oraz ogólnodostępnej dokumentacji Elecraft. Nie
> jest to dokumentacja oficjalna i może zawierać błędy.

## 1. Warstwa fizyczna

| Parametr | Wartość |
|---|---|
| Linia | jeden przewód, wspólny dla wszystkich urządzeń |
| Wyjścia | otwarty kolektor / dren, linia podciągnięta do +5 V |
| Spoczynek | HIGH |
| Logika | HIGH = `1`, LOW = `0` |
| Kolejność bitów | MSB first |
| Kierunek | half-duplex, w danej chwili nadaje jedno urządzenie |
| Kodowanie | NRZ, bez bitu startu i stopu; synchronizacja tylko na preambule |

Urządzenie nadaje `0` przez ściągnięcie linii do masy, a `1` przez
zwolnienie linii. Linii nigdy nie wymusza się aktywnie w stan HIGH.

Na magistrali występują ramki o dwóch różnych prędkościach.

| Ramka | Nadawca | Czas bitu | LOW na początku | Bity danych |
|---|---|---|---|---|
| **K\*** | radio (K4, K3) | **476,1 µs** (≈ 2100 bit/s) | ≈ 1,9 ms | 16 albo 24 |
| **ACC** | akcesorium (KPA1500, KPA500) | **1000 µs** | ≈ 80 ms | 7 |

Nazwa „K\*” obejmuje radiotelefony Elecraft wysyłające ten format
(K4 i K3?). „ACC” oznacza ramkę akcesorium (np. wzmacniacz KPA1500)

Bity, które na końcu ramki mają wartość `1`, nie dają żadnego zbocza i
zlewają się ze spoczynkiem. Odbiornik nie może więc szukać końca ramki
po zboczach. Zawsze próbkuje stałą liczbę bitów, odliczając czas od
preambuły.

---

## 2. Ramka K*

### 2.1 Preambuła

Preambuła to 5 bitów `00001`:

- LOW przez **1896 µs** (4 bity),
- HIGH przez 476 µs (1 bit).

Zbocze narastające kończące LOW (dalej `t_r`) jest jedynym punktem
synchronizacji ramki. Pierwszy bit danych zaczyna się 476 µs po `t_r`,
a środek bitu danych `k` (k = 0, 1, 2…) wypada w chwili:

```
t(k) = t_r + 714,15 µs + k · 476,1 µs
```

### 2.2 Dwa warianty: CMD i FREQ

O wariancie decyduje pierwszy bajt po preambule:

| Pierwszy bajt | Wariant | Bitów po preambule |
|---|---|---|
| `0x40`–`0x7F` (`0b01xxxxxx`) | **FREQ**: częstotliwość | 24 |
| pozostałe (w praktyce `0x20`–`0x3F`) | **CMD**: polecenie | 16, po nich opcjonalnie 8 bitów response |

Radio K4 wysyła FREQ zawsze z pierwszym bajtem `0x40`–`0x5F`.

### 2.3 Ramka CMD

```bytefield
(def boxes-per-row 29)
(def box-width 22)
(def row-header-fn nil)
(def left-margin 1)
(draw-column-headers {:font-size 9
  :labels ["" "" "" "" "" "7" "6" "5" "4" "3" "2" "1" "0" "7" "6" "5" "4" "3" "2" "1" "0" "7" "6" "5" "4" "3" "2" "1" "0"]})
(draw-box "0" {:fill "#e8e8e8"})
(draw-box "0" {:fill "#e8e8e8"})
(draw-box "0" {:fill "#e8e8e8"})
(draw-box "0" {:fill "#e8e8e8"})
(draw-box "1" {:fill "#e8e8e8"})
(draw-box "command" {:span 8})
(draw-box "data" {:span 8})
(draw-box "[response]" {:span 8 :fill "#f4f4f4"})
(draw-box "preambuła" {:span 5 :borders #{}})
(draw-box "radio" {:span 16 :borders #{}})
(draw-box "adresat" {:span 8 :borders #{}})
```

- **command**: kod polecenia (lista w [rozdziale 4](#4-polecenia-k)),
- **data**: argument polecenia,
- **response**: bajt, który adresat może odesłać zaraz po ramce. Nadaje
  go adresat, nie radio (patrz [2.5](#25-response)).

Przykład: `0x2002`, czyli BAND = 40 m, bez odpowiedzi (response = `0xFF`).

```wavedrom
{ signal: [
    { name: 'AUXBUS', wave: '1.0...10.10..........101.........' },
    { name: 'pole',   wave: 'x.=....=.......=.......=.......x.',
      data: ['preambuła 00001', 'command 0x20', 'data 0x02', 'response (brak = 0xFF)'] }
  ],
  head: { text: 'Ramka CMD 0x2002 (BAND 40 m), 1 znak = 1 bit = 476 µs' },
  config: { hscale: 1 }
}
```

Czas trwania: 21 bitów ≈ 10,0 ms, plus 3,8 ms okna response.

### 2.4 Ramka FREQ

```bytefield
(def boxes-per-row 29)
(def box-width 22)
(def row-header-fn nil)
(def left-margin 1)
(draw-column-headers {:font-size 9
  :labels ["" "" "" "" "" "23" "22" "21" "20" "19" "18" "17" "16" "15" "14" "13" "12" "11" "10" "9" "8" "7" "6" "5" "4" "3" "2" "1" "0"]})
(draw-box "0" {:fill "#e8e8e8"})
(draw-box "0" {:fill "#e8e8e8"})
(draw-box "0" {:fill "#e8e8e8"})
(draw-box "0" {:fill "#e8e8e8"})
(draw-box "1" {:fill "#e8e8e8"})
(draw-box "0")
(draw-box "1")
(draw-box "kHz (17 bitów)" {:span 17})
(draw-box "0")
(draw-box "cs" {:span 4})
(draw-box "preambuła" {:span 5 :borders #{}})
(draw-box "V (24 bity)" {:span 24 :borders #{}})
```

Pełny opis i suma kontrolna: [rozdział 5](#5-ramka-freq).

Przykład: 7105 kHz, `V = 0x437828`.

```wavedrom
{ signal: [
    { name: 'AUXBUS', wave: '1.0...1010...1.01...0....1010..1.' },
    { name: 'pole',   wave: 'x.=....=..=...............==...x.',
      data: ['preambuła 00001', '010', 'kHz = 7105', '0', 'cs = 8'] }
  ],
  head: { text: 'Ramka FREQ 0x437828 (7105 kHz), 1 znak = 1 bit = 476 µs' },
  config: { hscale: 1 }
}
```

Czas trwania: 29 bitów ≈ 13,8 ms.

### 2.5 Response

Po ramce CMD radio zwalnia linię i nasłuchuje odpowiedzi. Odpowiedź to
jeden bajt: bez preambuły, 8 bitów po 476 µs, MSB first.

K4 próbkuje odpowiedź w chwilach:

```
t_end + 238,1 µs + j · 476,1 µs,   j = 0…7
```

`t_end` to koniec ostatniego bitu `data` (`t_r` + 17 · 476,1 µs). Jest to
ta sama siatka próbek co bity ramki, czyli próbki 16–23 liczone od
preambuły. Brak odpowiedzi daje `0xFF`. Po ramce FREQ radio nie
nasłuchuje odpowiedzi.

Spośród znanych urządzeń odpowiada tylko KRC2 i tylko na polecenia
`0x34` i `0x35`. KRC2 zaczyna odpowiedź 274–290 µs po `t_end`, a jego bit
trwa ≈ 479 µs. Pierwsza próbka K4 (238 µs) wypada więc jeszcze przed
startem odpowiedzi, a następne pod koniec kolejnych bitów. W efekcie K4
odczytywałby `0x80 | (response >> 1)`, na przykład `0x43` daje
`0xA1`.

Odpowiedź KRC2 kończy się ≈ 4,1 ms po `t_end`, czyli już po oknie
nasłuchu K4 (≈ 3,8 ms). Wiec powoduje to nieścisłość i wymaga wyjaśnienia.
Jeśli ostatni bit odpowiedzi to `0`, linia jest
LOW w chwili, gdy K4 wraca do oczekiwania na ramki ACC. K4 może to
odebrać jako ramkę ACC o wartości `0x7F`, którą ignoruje (patrz
[rozdział 6](#6-polecenia-acc)).

### 2.6 Odbiór ramki K*

1. Ramka zaczyna się zboczem opadającym po co najmniej 4 ms spoczynku.
2. LOW musi trwać 1,6–2,2 ms.
3. Od zbocza narastającego `t_r` należy odczekac 1.5 długosci bitu i odczytać pierwszy bajt.
4. Pierwszy bajt `0x40`–`0x7F` oznacza FREQ: należy odczytać jeszcze 16
   bitów i sprawdzić sumę kontrolną.
5. Każdy inny bajt oznacza CMD: należy odczytać jeszcze 8 bitów `data`,
   a dla odpowiedzi kolejne 8 (`t(16)`…`t(23)`).

Odbiornik zbierający zawsze 24 bity dostanie dla ramki CMD bez
odpowiedzi wartość `(command << 16) | (data << 8) | 0xFF`.

---

## 3. Ramka ACC

Ramkę ACC nadaje akcesorium do radia. Nie ma w niej pola typu ani sumy
kontrolnej.

```bytefield
(def boxes-per-row 16)
(def box-width 30)
(def row-header-fn nil)
(def left-margin 1)
(draw-column-headers {:font-size 9
  :labels ["" "" "" "" "" "" "" "" "" "6" "5" "4" "3" "2" "1" "0"]})
(draw-box "LOW ≈ 80 ms" {:span 4 :fill "#e8e8e8"})
(draw-box "1" {:fill "#e8e8e8"})
(draw-box "1" {:fill "#e8e8e8"})
(draw-box "1" {:fill "#e8e8e8"})
(draw-box "1" {:fill "#e8e8e8"})
(draw-box "1" {:fill "#e8e8e8"})
(draw-box "polecenie" {:span 7})
(draw-box "" {:span 4 :borders #{}})
(draw-box "HIGH 5 ms" {:span 5 :borders #{}})
(draw-box "bajt b6…b0" {:span 7 :borders #{}})
```

Zbocze narastające kończące LOW to `t_r`; dane zajmują czas od `t_r` + 5 ms
do `t_r` + 12 ms.

- LOW ≈ 80 ms (KPA1500: 79,91 ms) zajmuje magistralę i sygnalizuje
  ramkę ACC.
- 5 ms HIGH (5 bitów `1`) to część synchronizacyjna.
- 7 bitów danych po 1000 µs, MSB first. Nadawany bajt ma zawsze wartość
  `0x00`–`0x7F`.

```wavedrom
{ signal: [
    { name: 'AUXBUS', wave: '1.0..|..1....0....1...' },
    { name: 'pole',   wave: 'x.=.....=....=......x.',
      data: ['LOW ≈ 80 ms', 'HIGH 5 ms', 'b6…b0 = 0000011'] }
  ],
  head: { text: 'Ramka ACC 0x03, 1 znak = 1 ms (LOW skrócone)' },
  config: { hscale: 1 }
}
```

### 3.1 Odbiór ramki ACC w K4

- Każde LOW na wolnej magistrali, które nie jest jego własną transmisją
  ani oknem response, traktuje jako początek ramki ACC.
- Po LOW czeka na HIGH (`t_r`). Bit `k` (k = 0…6, czyli b6…b0) próbkuje
  w chwili `t_r` + 5500 µs + k · 1000 µs.
- Po odebraniu bajtu K4 przez ≈ 16 ms nie reaguje na magistralę. Kolejna
  ramka zostanie odebrana, jeśli jej LOW trwa jeszcze po ≈ `t_r` + 28 ms.
- Od pierwszego LOW do końca tej przerwy K4 nie nadaje własnych ramek,
  tylko je odkłada.
- K4 nie ma limitu czasu na LOW. Linia trzymana stale w LOW blokuje
  nadawanie K4.

---

## 4. Polecenia K*

| command | Nazwa | data | Kto reaguje | response | K4 wysyła sam |
|---|---|---|---|---|---|
| `0x20` | BAND | numer pasma (4.1) | KRC2 | brak | tak |
| `0x21` | XVTR | slot transwertera 0–7 (4.2) | KRC2 | brak | tak |
| `0x34` | STATUS | indeks znaku (4.3) | KRC2 | 1 bajt | nie |
| `0x35` | AC | przesunięcie (4.4) | KRC2 | 1 bajt, stan wyjść AC | nie |

### 4.1 `0x20` BAND

`command = 0x20, data = <patrz tabela>`. Numeracja pasm w kierunku radio → akcesorium:

| `data` | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|--------|---|---|---|---|---|---|---|---|---|---|---|
| pasmo  | 160 m | 80 m | 40 m | 30 m | 20 m | 17 m | 15 m | 12 m | 10 m | **60 m** | 6 m |

60 m ma wartość 9 i nie stoi w kolejności częstotliwości. Ta numeracja
różni się od numeracji CAT `BN` i od numeracji w ramkach ACC (6.1).
Na paśmie transwertera K4 wysyła `v = 8` (10 m).

KRC2 przełącza wyjścia pasma; wewnętrznie zapamiętuje `(v & 0x0F) + 1`.

### 4.2 `0x21` XVTR

`ramka = 0x21`:

- `data = 0`: zwykłe pasmo HF, transwerter nieaktywny,
- `data = 1`…`7`: aktywny transwerter w slocie `s`.

K4 wysyła XVTR po każdej ramce BAND. KRC2 zapamiętuje `s`.

### 4.3 `0x34` STATUS (KRC2)

### 4.4 `0x35` AC (KRC2)

```
response = (data + stan) & maska
stan     = response
```

- `maska` wynosi `0x03` przy zdjętej zworce W4 (ustawienie domyślne) i
  `0x07` przy założonej.
- Odpowiedź od razu staje się nowym stanem wyjść AC1–AC3 KRC2. Ramka
  działa tak samo jak ręczna kombinacja HOLD F4 + TAP F1…F4.

| stan (maska `0x03`) | AC1 | AC2 | AC3 |
|---|---|---|---|
| 0 | aktywne | aktywne | aktywne |
| 1 | wyłączone | aktywne | aktywne |
| 2 | aktywne | wyłączone | aktywne |
| 3 | aktywne | aktywne | wyłączone |

Przy masce `0x07` wyjścia AC są kodowane binarnie.

`data = 0` odczytuje stan bez zmiany. `data = 1` przechodzi do
następnego stanu, cyklicznie 0 → 1 → 2 → 3 → 0.

---

## 5. Ramka FREQ

### 5.1 Pola

24 bity `V`, od bitu 23 do 0:

```bytefield
(def boxes-per-row 24)
(def box-width 26)
(def row-header-fn nil)
(def left-margin 1)
(draw-column-headers {:font-size 9
  :labels ["23" "22" "21" "20" "19" "18" "17" "16" "15" "14" "13" "12" "11" "10" "9" "8" "7" "6" "5" "4" "3" "2" "1" "0"]})
(draw-box "0")
(draw-box "1")
(draw-box "kHz (17 bitów)" {:span 17})
(draw-box "0")
(draw-box "cs" {:span 4})
(draw-box "nibel 5" {:span 4 :fill "#f4f4f4"})
(draw-box "nibel 4" {:span 4 :fill "#f4f4f4"})
(draw-box "nibel 3" {:span 4 :fill "#f4f4f4"})
(draw-box "nibel 2" {:span 4 :fill "#f4f4f4"})
(draw-box "nibel 1" {:span 4 :fill "#f4f4f4"})
(draw-box "nibel 0" {:span 4 :fill "#f4f4f4"})
```

- bit 23 = `0`, bit 22 = `1`: znacznik ramki FREQ,
- bity 21–5: częstotliwość w kHz (17 bitów); w K4 zawsze < 65 536, więc
  bit 21 = 0,
- bit 4 = `0`,
- bity 3–0: `cs`, suma kontrolna liczona po niblach (5.2).

Częstotliwość jest obcinana do pełnych kHz (7105,9 kHz → 7105).

### 5.2 Suma kontrolna

Suma sześciu półbajtów (nibli) `V` musi dawać 0 modulo 16:

```c
uint32_t freq_frame(uint32_t khz) {
    uint32_t x = (khz << 5) | 0x400000;
    uint32_t s = 0;
    for (int i = 0; i < 24; i += 4) s += (x >> i) & 0xF;
    return x | ((-s) & 0xF);
}

bool freq_ok(uint32_t v) {
    uint32_t s = 0;
    for (int i = 0; i < 24; i += 4) s += (v >> i) & 0xF;
    return (v & 0xC00010) == 0x400000 && (s & 0xF) == 0;
}
```

### 5.3 Odbiór FREQ w KPA1500

- KPA1500 przyjmuje częstotliwość tylko w obrębie pasma, na którym
  aktualnie pracuje. Ramka FREQ nie zmienia pasma wzmacniacza, służy do
  dokładnego doboru ustawień ATU.
- Przy RADIO TYPE = K4 wymagana jest poprawna suma kontrolna. Bez niej
  (albo przy innym RADIO TYPE) dolne 3 bity kHz są zerowane.

---

## 6. Polecenia ACC

Ramki ACC wysyłają KPA1500 i KPA500. Nie zaobserwowano ramek ACC od
KAT500 ani KRC2. Transwerterów serii XV nie badano.

**Bit 6 = OPER.** W każdym bajcie bit 6 oznacza, że wzmacniacz jest w
stanie OPER (1) albo STBY (0). Przed rozpoznaniem polecenia K4 maskuje
ten bit i nie pokazuje go na ekranie.

| Bajt (bez bitu 6) | Znaczenie | Reakcja K4 |
|---|---|---|
| `0x00`–`0x0A` | przycisk pasma na wzmacniaczu (6.1) | zmienia pasmo, potem wysyła BAND, XVTR i, jeśli się zmieniła, FREQ |
| `0x0B`–`0x1F` | — | ignoruje |
| `0x20` \| N | błąd wzmacniacza o kodzie N (6.2) | komunikat `KPA*500 Fault: <tekst>` |
| `0x31` | włączony | komunikat; po ≈ 250 ms ponownie wysyła FREQ |
| `0x32` | wyłączony | komunikat |
| `0x33` | STBY | komunikat |
| `0x34` | OPER (wysyłane jako `0x74`) | komunikat |
| `0x35` | KEY START: wzmacniacz potrzebuje nośnej do strojenia ATU | włącza TUNE (jak CAT `TU2;`) |
| `0x36` | KEY STOP | wyłącza TUNE (jak CAT `TU0;`) |
| `0x37` | włączony (KPA1500 przy RADIO TYPE = K4) | jak `0x31` |
| `0x38`–`0x3F` | — | ignoruje |

Przy KEY START i KEY STOP K4 nie wysyła niczego po magistrali.
Wzmacniacz sam wykrywa pojawienie się mocy. KPA1500 powtarza `0x35` co
≈ 187 ms, dopóki nie wykryje mocy, a po zakończeniu strojenia wysyła
`0x36`.

### 6.1 Numeracja pasm w kierunku akcesorium → radio

| Bajt | 1 | 2 | 0 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| pasmo | 160 m | 80 m | 60 m | 40 m | 30 m | 20 m | 17 m | 15 m | 12 m | 10 m | 6 m |
| K4 ustawia | `BN00` | `BN01` | `BN02` | `BN03` | `BN04` | `BN05` | `BN06` | `BN07` | `BN08` | `BN09` | `BN10` |

K4 wykonuje zmianę pasma nawet wtedy, gdy już na nim pracuje, i wtedy
wysyła BAND i XVTR ponownie.

### 6.2 Kody błędów (`0x20 | N`)

| N | Tekst na K4 | N | Tekst na K4 |
|---|---|---|---|
| 0 | powered on | 8 | 60 volt power supply |
| 1 | power-off problem | 9 | SWR too high |
| 2 | current 1 | 10 | T/R switch failure |
| 3 | current 2 | 11 | PA failure |
| 4 | temperature | 12 | PA output too high |
| 5 | firmware rev. changed | 13 | 60 V too low |
| 6 | power input too high | 14 | MON270 signal |
| 7 | frequency counter | 15 | excess gain |

---

## 7. Kolejność i czasy nadawania

### 7.1 Radio K4

| Zdarzenie | Ramki na magistrali |
|---|---|
| zmiana pasma | BAND, ≈ 40–50 ms później XVTR, ≈ 480 ms później FREQ |
| zmiana częstotliwości | FREQ ≈ 500 ms po ostatniej zmianie; dalsze strojenie przesuwa termin |
| ACC `0x31` / `0x37` (włączenie wzmacniacza) | FREQ ≈ 250 ms później, bez BAND i XVTR |
| ACC przycisk pasma | jak przy zmianie pasma |

- FREQ jest wysyłana tylko wtedy, gdy różni się od ostatnio wysłanej, i
  nie jest wysyłana na pasmach transwertera.
- Gdy magistrala jest zajęta, K4 ponawia FREQ po ≈ 250 ms.
- Odstęp między ramkami K\* wynosi zawsze znacznie więcej niż 5 ms.

### 7.2 Urządzenia na magistrali

| Urządzenie | Nadaje | Odbiera |
|---|---|---|
| K4 | K\*: BAND, XVTR, FREQ | ACC; response po CMD |
| KPA1500 | ACC | K\* FREQ, `0x38`, `0x39` |
| KPA500 | ACC | K\* (zakres reakcji nieustalony) |
| KRC2 | response na `0x34` / `0x35` | K\* `0x20`, `0x21`, `0x34`, `0x35` |

---

## 8. Wskazówki dla implementacji

**Nadawanie K\*** (symulator radia):

Błąd czasu bitu kumuluje się do końca ramki. Przy 24 bitach odchyłka
1 % daje na ostatnim bicie ≈ 120 µs, czyli 1/4 bitu.

**Nadawanie ACC** (symulator wzmacniacza):

- Najdłuższy ciąg HIGH wewnątrz ramki K\* to ≈ 8 ms (FREQ z wieloma
  jedynkami). Przed rozpoczęciem ramki ACC magistrala powinna być wolna
  przez co najmniej 15 ms.
- Ramka ACC zaczęta w trakcie ramki K\* psuje ją, a radio tego nie
  wykrywa. Ramka ACC zostanie przez K4 odebrana dopiero po zakończeniu
  jego własnej ramki.
- Czas bitu ACC należy trzymać z dokładnością lepszą niż ±40 µs, tak
  żeby łączny dryf do b0 nie przekroczył ≈ 250 µs.

**Odpowiadanie na CMD** (symulator akcesorium z response):

- Odpowiedź należy zacząć zaraz po końcu ostatniego bitu `data`. Wtedy
  próbki K4 (238 µs + j · 476 µs) trafią w środki bitów.
- Odpowiedź musi się skończyć przed końcem okna K4, czyli ≈ 3,8 ms po
  ramce. Późniejsze LOW K4 może uznać za ramkę ACC.

**Odbiór** (analizator, symulator akcesorium):

- Synchronizacja tylko na zboczu `t_r`. Później próbki należy odliczać
  od `t_r`, a nie od kolejnych zboczy.
- Należy zawsze próbkować pełną liczbę bitów: 24 dla K\* (CMD razem z
  response) i 7 dla ACC.
