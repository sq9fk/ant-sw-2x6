# DESIGN — architektura firmware SP9PDF RemoteQTH 6×2 Antenna Switch

Opis budowy i działania firmware [`src/main.ino`](../src/main.ino). Sprzęt i pinout:
[`CONNECTIONS.md`](CONNECTIONS.md). Bazuje na RemoteQTH 6×2 (OK1HRA rev 0.3), zmodyfikowany
przez SQ9FK dla stacji SP9PDF.

## 1. Przegląd

Sterownik pozwala **2 transceiverom** dzielić **6 anten** bezkolizyjnie. Antenę dla każdego
TRX wybiera się:
- **ręcznie** — interfejs WWW (Ethernet) lub enkoder + LCD (zawsze dostępne);
- **automatycznie** — z danych pasma BCD radia (opcja `BCD_INPUT`, domyślnie wył.);
- **z komputera** — OTRSP/SO2R (opcja `OTRSP`, domyślnie wył.).

Rdzeń logiki (wybór anteny → wyjścia przekaźników, wykrywanie kolizji, LCD) działa niezależnie
od opcji. Pętla `loop()` jest jednowątkowa i nieblokująca poza obsługą pojedynczego żądania WWW.

## 2. Konfiguracja i flagi funkcji

Wszystkie `#define` są na górze `src/main.ino`.

| Flaga | Dom. | Znaczenie |
|-------|------|-----------|
| `Ports` | 2 | liczba TRX / linii LCD (2–4) |
| `Inputs` | 6 | liczba anten (etykieta „6x2") |
| `EthModule` | **wł.** | Ethernet W5500 + serwer WWW |
| `__USE_DHCP__` | wł. | DHCP dla Ethernetu |
| `WEB_ANT_NAMES` | **wł.** | edycja nazw anten przez WWW + zapis EEPROM |
| `ANT_MAXLEN` | 11 | limit długości nazwy anteny (szerokość LCD) |
| `BCD_INPUT` | wył. | automatyczny wybór anteny z BCD radia (`rx()`) |
| `PTT_BLOCKING` | wył. | odczyt PTT + blokada przełączania podczas TX |
| `OTRSP` | wył. | sterowanie SO2R po porcie szeregowym |
| `inputHigh` | wł. | poziom aktywny wejść BCD |

**Ograniczenie rozmiaru:** ATmega328 ma 30 KB flash / 2 KB RAM. `EthModule` i `OTRSP` nie
mieszczą się razem — trzymamy je rozłącznie. Domyślnie: Ethernet wł., OTRSP wył.

## 3. Model stanu — `port[8][6]`

Centralna tablica stanu (`byte`):

```
port[0..3] = wejścia TRX1..4   (żądana antena)
port[4..7] = wyjścia TRX1..4   (antena wystawiona na przekaźniki)
```

Kolumny:

| idx | nazwa | znaczenie |
|-----|-------|-----------|
| `[0]` | adres | adres I²C ekspandera MCP23017 |
| `[1]` | antena | wybrana/żądana antena: `0`=OFF, `1..6`, `8`=BCD (tylko `BCD_INPUT`) |
| `[2]` | PTT | stan PTT (tylko `PTT_BLOCKING`) |
| `[3]` | kolizja | 1 = konflikt z drugim TRX |
| `[4]` | tryb | 0=auto/BCD, 1=ręczny (istotne tylko przy `BCD_INPUT`) |
| `[5]` | bank | 1 lub 2 (który nibble/half ekspandera) |

Adresy: TRX1/2 → IN `0x21` + OUT `0x20`; TRX3/4 → IN `0x23` + OUT `0x22`. W wersji 2-TRX
używane są `0x20`/`0x21`.

## 4. Pętla główna `loop()`

Kolejność w każdej iteracji:

1. **(opcja) OTRSP** — jeśli przyszła kompletna komenda serial, `OTRSP_parse()`.
2. **GPIO / logika przełączania** — dla każdego TRX `i`:
   - *(opcja `BCD_INPUT`)* jeśli tryb auto → `rx()` czyta antenę z BCD radia;
   - **wykrywanie kolizji**: licznik `c` porównuje `port[i][1]` (żądanie) z `port[j+4][1]`
     (wyjście innego TRX). Kolizja gdy ta sama antena (≠0). (Blokada 4↔5 GXP usunięta — 6 anten.);
   - jeśli kolizja → `port[i][3]=1`, wyjście OFF; inaczej → wyjście = żądanie
     *(przy `PTT_BLOCKING` przełączenie wstrzymane, gdy PTT aktywne)*;
   - `tx()` wystawia wyjścia na ekspander OUT.
3. **(opcja) Serwer WWW** — obsługa jednego klienta (patrz §7).
4. **(opcja `serialECHO`)** — telemetria na serial.
5. **Przycisk** — długie naciśnięcie przełącza `menu1state` (tryb edycji enkoderem).
6. **LCD** — odświeżenie linii (`show()`), kontrola napięcia (ostrzeżenia LOW/HIGH).

## 5. Wyjścia anten — `tx()`

Numer anteny kodowany **one-hot** na porcie ekspandera OUT (`GPIOA`=TRX1, `GPIOB`=TRX2) —
**projekt 6-antenowy** (`bit0..bit5` = anteny 1..6):

| Antena | Bit |
|--------|-----|
| 1 | `bit0` |
| 2 | `bit1` |
| 3 | `bit2` |
| 4 | `bit3` |
| 5 | `bit4` |
| 6 | `bit5` |
| 0 (OFF) | `0x00` |

**`bit7` = Radio Flex (SQ9FK):** `GPA7`/`GPB7` sterują dwoma **niezależnymi wyjściami Radio Flex**
(`flexState[0]`/`flexState[1]` → Q1/K1/J7 i Q2/K2/J6), nakładanymi na bajt one-hot **niezależnie od
wyboru anteny**. Przełączane **ikonami power w wierszach TRX** (`/?F{s}{0|1}`), bez konfigurowalnych
nazw. Dawniej `bit7` = przekaźnik pasma GXP11 40 m sprzężony z poz. 4/5.
`bit6` nieużywany. Poz. 4 i 5 to teraz niezależne anteny → **blokada kolizji 4↔5 usunięta**
(ogólne wykrywanie kolizji między TRX pozostaje).

## 6. Wybór anteny — źródła

- **Enkoder/LCD:** przerwanie `encI()` + `enc2()`; `menu1state` przełącza między wyborem
  linii a zmianą numeru anteny. Zakres 0..6 (0..8 przy `BCD_INPUT`).
- **WWW:** żądanie `GET /?S{bank}{kod}` (antena), `GET /?F{s}{0|1}` (Radio Flex) (patrz §7).
- **(opcja) BCD:** `rx()` czyta 4-bit BCD z ekspandera IN, dekoduje przez `BCDmatrixOUT[2][16]`
  (PROGMEM) → numer anteny.
- **(opcja) OTRSP:** `AUX1n`/`AUX2n` ustawiają antenę TRX1/TRX2; `?AUX1/?AUX2/?NAME/?` — zapytania.

## 7. Serwer WWW (przy `EthModule`)

Jednowątkowy serwer HTTP na porcie 80 (biblioteka `adafruit/Ethernet2`, W5500).

**Odczyt żądania.** Czytana jest **tylko pierwsza linia** (`GET …`) do bufora `reqBuf`
(bez `String`), z twardym limitem długości. Po znaku `\n` serwer od razu generuje odpowiedź
i kończy (`delay(1); client.stop()`).

**Parsowanie** (z `reqBuf`, indeksy liczone od początku linii):
- `S{bank}{kod}` — `bank`=`reqBuf[7]`, `kod`=`reqBuf[8..9]`:
  `00..06`=antena, `20`=tryb BCD, `21`=tryb ręczny. Walidacja cyfr + `bankIdx ∈ 0..Ports-1`
  (obce żądania jak `/favicon.ico` są ignorowane — brak przypadkowych przełączeń).
- `F{s}{0|1}` — **Radio Flex** (SQ9FK): `s`=`reqBuf[7]` (1/2), stan=`reqBuf[8]` → `flexState[s-1]`.
- *(opcja `WEB_ANT_NAMES`)* `N{k}={nazwa}` — `k`=`reqBuf[7]` (1..6); `parseName()` dekoduje
  URL (`+`→spacja, `%XX`) do `antRAM[k]` z obcięciem do `ANT_MAXLEN`, potem `saveAntNames()`.
- *(opcja `WEB_ANT_NAMES`)* `NS={nazwa}` — nazwa stacji (topbar) → `siteRAM` (`parseName()` + `saveAntNames()`).

**Generowanie strony.** Statyczny nagłówek HTTP + `<head>` + CSS są w **PROGMEM**
(`HTTP_HEAD`/`HTTP_HEAD2`) i wysyłane `sendP()` porcjami 64 B (`client.write`); ikona `POWER_SVG`
(Flex) też z PROGMEM. Dynamicznie (w kontenerze `.wrap`, sekcje `.card`): **topbar** (kropka `.dot`
czerwona przy napięciu poza 10–15 V + nazwa stacji `.brand`); karta **Anteny** — nagłówek `.ahead`
ze **statusami sekcji** (`.astat`/`.st`, chipy 50/50: numer + nazwa włączonej anteny) oraz wiersze
TRX `.trx` (przyciski anten w pętli, klasy `g`/`gr` = wybrana/kolizja; *(opcja)* Manual/BCD;
*(opcja)* plakietka PTT; **ikona power Radio Flex** `.flx` na końcu wiersza, `F{s}{0|1}`); karta
**Opis anten** (`.leg` — legenda numer→nazwa); karta **Settings** (`<details>`, zwinięta) z nazwą
stacji, nazwami anten (`.nm`) i **ukrytym odczytem napięcia**. Strona odświeża się `meta refresh`
co 10 s. Split usunięty. **Flash prawie pełny** — przy zmianach HTML/CSS pilnuj budżetu.

**Wygląd (SQ9FK).** Konwencja designu przeniesiona z projektu
[`rotator_wifi_bridge`](https://github.com/sq9fk/rotator_wifi_bridge): ciemny motyw teal
(tło `#12333b`, karty `#1a3a42`/`#21505c`/`#2a5d6b`), akcent żółty `#f5d33c` (wybrana antena /
Flex WŁ.), alert `#d11534` (kolizja / błąd napięcia), font **Inter** z fallbackiem systemowym
(bez zewnętrznego linku — oszczędza flash i RTT). Zaokrąglone karty (`1.1em`) i przyciski (`.4em`).
Klasy CSS (`bcd/bcdr`, `g/gr`, `ptt`, `nm`, `trx`, `card`, `dot`) współdzielone z symulatorem
[`tools/websim.html`](../tools/websim.html) (`DEVICE_CSS` = lustro `HTTP_HEAD2`).

Podgląd wyglądu bez sprzętu: [`tools/websim.html`](../tools/websim.html) / `python tools/serve.py`.

## 8. Nazwy anten, nazwa stacji i EEPROM

- Domyślne nazwy: `antDefault[]` w PROGMEM (indeks 0=`OFF`, 8=`M-off->BCD` — nieedytowalne);
  domyślna nazwa stacji: `siteDefault` (`"SP9PDF"`). Radio Flex nie ma nazw (same ikony power).
- Przy `WEB_ANT_NAMES`: nazwy anten 1–6 w RAM `antRAM[9][ANT_MAXLEN+1]` oraz **nazwa stacji** w
  `siteRAM[ANT_MAXLEN+1]`, ładowane w `setup()` przez `loadAntNames()` z fallbackiem na domyślne.
  Zapis `saveAntNames()` używa `EEPROM.update` (mniejsze zużycie komórek).
- **Układ EEPROM (SQ9FK):** bajt `0` = magic `0xA5` (sekcja anten) + 6×`ANT_MAXLEN` (offset `1`);
  bajt `67` = **osobny magic `0x5B`** (sekcja nazwy stacji) + `ANT_MAXLEN` (nazwa stacji, offset `68`).
  Osobny magic sprawia, że po wgraniu na istniejący EEPROM (magic `0xA5`) nazwy anten 1–6
  **zostają**, a nazwa stacji startuje z domyślnej.
- Odczyt zawsze przez `antName(idx)` / `siteName()` — zwraca `char*` (RAM) lub
  `__FlashStringHelper*` (PROGMEM) zależnie od flagi; oba typy obsługują `client.print()` i `String`.

## 9. Budżet pamięci i optymalizacje

Domyślny build: **Flash 95,8 %** (29418 B) / **RAM 45,4 %** (930 B).

Zastosowane techniki (patrz komentarze `//SQ9FK`):
- tablice stałe w **PROGMEM** (`antDefault`, `glyphs`, `BCDmatrixOUT`), `port[8][6]` jako `byte`;
- generowanie przycisków WWW i listy nazw w **pętli** (mniej powtórzonych łańcuchów `F()`);
- **`sendP()`** — chunkowana wysyłka statycznego HTML z PROGMEM;
- parsowanie żądań **bez `String`** (bufor `char`), walidacja zakresu (brak zapisu poza `port[]`);
- czytanie tylko pierwszej linii żądania.

BCD/PTT/OTRSP jako `#ifdef` — wyłączone nie zajmują flash/RAM.

## 10. Mapa `#ifdef` (gdzie szukać przy zmianach)

| Funkcja | Miejsca w kodzie |
|---------|------------------|
| `EthModule` | globalne (mac/ip/server, `HTTP_HEAD*`, `sendP`), sekcja serwera w `loop()`, `setup()` |
| `WEB_ANT_NAMES` | deklaracja `antRAM`/`siteRAM`/`antName`/`siteName`/EEPROM, `setup()` (`loadAntNames`), parser (`N`/`NS`) i formularze Settings, rozmiar `reqBuf` |
| `BCD_INPUT` | `BCDmatrixOUT`, gałąź auto w `loop()`, zakres enkodera, przyciski Manual/BCD w WWW, pozycja „8" w `show()`, cała funkcja `rx()` |
| `PTT_BLOCKING` | warunki `if(port[i][2]==0)` w `loop()`, plakietka PTT w `show()` i WWW, odczyt PTT w `rx()` |
| `OTRSP` | deklaracje bufora, wywołanie w `loop()`, `OTRSP_parse()`, `serialEvent()` |

> Wykrywanie kolizji między TRX jest **osobne** od `PTT_BLOCKING` i działa zawsze.

## 11. Weryfikacja

- Kompilacja: `pio run -e nanoatmega328` (0 ostrzeżeń z naszego kodu; ostrzeżenia z biblioteki
  `Ethernet2` są nieszkodliwe). Warto też zbudować z `-DBCD_INPUT -DPTT_BLOCKING`.
- Testy funkcjonalne (W5500, EEPROM, przełączanie) — na docelowej stacji (brak sprzętu w CI).
