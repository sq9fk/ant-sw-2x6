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
| `__USE_DHCP__` | **wył.** | DHCP dla Ethernetu (koszt ~3,8 KB flash z oficjalną biblioteką — domyślnie static IP) |
| `WEB_ANT_NAMES` | **wł.** | edycja nazw anten przez WWW + zapis EEPROM |
| `ANT_MAXLEN` | 11 | limit długości nazwy anteny (szerokość LCD) |
| `BCD_INPUT` | wył. | automatyczny wybór anteny z BCD radia (`rx()`) |
| `PTT_BLOCKING` | wył. | odczyt PTT + blokada przełączania podczas TX |
| `OTRSP` | wył. | sterowanie SO2R po porcie szeregowym (USB) |
| `OTRSP_TCP` | wył. | + surowe gniazdo TCP dla OTRSP (wymaga `OTRSP`, wyklucza `EthModule`) |
| `OTRSP_TCP_PORT` | 4534 | port surowego TCP dla OTRSP |
| `inputHigh` | wł. | poziom aktywny wejść BCD |

**Ograniczenie rozmiaru:** ATmega328 ma 30 KB flash / 2 KB RAM. `EthModule` (strona WWW) i
`OTRSP` nie mieszczą się razem — trzymamy je rozłącznie (`#error` w kodzie wymusza to w
compile-time). Domyślnie: Ethernet wł. (strona WWW), OTRSP wył. **Trzeci wariant** —
`OTRSP`+`OTRSP_TCP` — daje OTRSP jednocześnie po USB i po surowym TCP, z Ethernetem/DHCP, ale
**bez strony WWW** (patrz §6, §9).

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
| `[1]` | antena | wybrana/żądana antena: `0`=OFF, `1..6`, `7`=tryb BCD (tylko `BCD_INPUT`) |
| `[2]` | PTT | stan PTT (tylko `PTT_BLOCKING`) |
| `[3]` | kolizja | 1 = konflikt z drugim TRX |
| `[4]` | tryb | 0=auto/BCD, 1=ręczny (istotne tylko przy `BCD_INPUT`) |
| `[5]` | bank | 1 lub 2 (który nibble/half ekspandera) |

Adresy: TRX1/2 → IN `0x21` + OUT `0x20`; TRX3/4 → IN `0x23` + OUT `0x22`. W wersji 2-TRX
używane są `0x20`/`0x21`.

## 4. Pętla główna `loop()`

Kolejność w każdej iteracji:

1. **(opcja) OTRSP przez USB** — jeśli przyszła kompletna komenda serial (`serialEvent()`,
   terminator CR), `OTRSP_parse(in_buf, Serial)`.
2. **GPIO / logika przełączania** — dla każdego TRX `i`:
   - *(opcja `BCD_INPUT`)* jeśli tryb auto → `rx()` czyta antenę z BCD radia;
   - **wykrywanie kolizji**: licznik `c` porównuje `port[i][1]` (żądanie) z `port[j+4][1]`
     (wyjście innego TRX). Kolizja gdy ta sama antena (≠0). (Blokada 4↔5 GXP usunięta — 6 anten.);
   - jeśli kolizja → `port[i][3]=1`, wyjście OFF; inaczej → wyjście = żądanie
     *(przy `PTT_BLOCKING` przełączenie wstrzymane, gdy PTT aktywne)*;
   - `tx()` wystawia wyjścia na ekspander OUT.
3. **(opcja `EthModule`/`OTRSP_TCP`)** — `Ethernet.maintain()` (odnawianie dzierżawy DHCP) +
   *(opcja `EthModule`)* obsługa jednego klienta strony WWW (patrz §7) + *(opcja `OTRSP_TCP`)*
   obsługa **trwałego** połączenia OTRSP-TCP (patrz §6 — inny model niż request/response WWW).
4. **(opcja `serialECHO`)** — telemetria na serial.
5. **Przycisk** — długie naciśnięcie przełącza `menu1state` (tryb edycji enkoderem).
6. **LCD** — odświeżenie linii (`show()`) co 100 ms + kontrola napięcia co 3 s. Ostrzeżenia
   LOW/HIGH są **nieblokujące** (znacznik `voltWarn`, ~2 s bez `delay()`) — awaria napięcia nie
   zamraża WWW/przełączania/enkodera.

**Start (`setup()`).** Splash LCD (2 s) + napięcie (3 s) dają czas na rozruch W5500. **Domyślnie
`__USE_DHCP__` wyłączone** — static IP (`ip/gateway/subnet`), koszt DHCP z oficjalną biblioteką
(~3,8 KB) nie mieści się razem ze stroną WWW (patrz §9). Gdy `__USE_DHCP__` włączone: DHCP jest
**ponawiany** (`while Ethernet.begin(mac, 6000, 4000)==0`, timeout **6 s/próbę jako parametr**
— oficjalna biblioteka eksponuje go wprost, bez patchowania `Dhcp.h` jak dawniej), a po 3
nieudanych próbach **fallback na static IP**. **IP pokazywane na LCD (5 s)** przez
`lcd.print(Ethernet.localIP())` — zawsze rzeczywisty, aktualnie aktywny adres (uwzględnia
ewentualną edycję `ip`/`gateway`/`subnet` przez WWW Settings + EEPROM, załadowaną wcześniej
przez `loadNetConfig()`), niezależnie od trybu static/DHCP.

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
  linii a zmianą numeru anteny. Zakres 0..6 (0..7 przy `BCD_INPUT`, gdzie 7 = tryb BCD).
- **WWW:** żądanie `GET /?S{bank}{kod}` (antena), `GET /?F{s}{0|1}` (Radio Flex) (patrz §7).
- **(opcja) BCD:** `rx()` czyta 4-bit BCD z ekspandera IN, dekoduje przez `BCDmatrixOUT[2][16]`
  (PROGMEM) → numer anteny.
- **(opcja) OTRSP:** `AUX1n`/`AUX2n` ustawiają antenę TRX1/TRX2; `?AUX1/?AUX2/?NAME/?` — zapytania.
  Zgodnie ze specyfikacją [OTRSP](https://www.k1xm.org/OTRSP/OTRSP_Protocol.pdf) komendy kończy
  znak **CR (`\r`)** — `serialEvent()` terminuje na `\r` (LF ignorowany, na wypadek CRLF), z
  kontrolą granic `in_buf`. Odpowiedzi `?AUX1`/`?AUX2` zwracają samą wartość dziesiętną (bez
  dopełnień) — zgodny round-trip `AUXn` → `?AUXn`. Parser `OTRSP_parse(char *cmd, Print &out)`
  przyjmuje bufor komendy i cel odpowiedzi (`Print&`) — używany identycznie z USB (`Serial`) i,
  przy `OTRSP_TCP`, z surowego gniazda TCP (`EthernetClient`); zero duplikacji logiki komend.
- **(opcja) OTRSP przez TCP (`OTRSP_TCP`, wymaga `OTRSP`):** dodatkowy `EthernetServer
  otrspServer(OTRSP_TCP_PORT)` (domyślnie port 4534) w `loop()`. **Połączenie trwałe** — inaczej
  niż serwer WWW (accept→jedna linia→odpowiedź→`stop()`), klient `otrspClient` jest trzymany
  między iteracjami `loop()`, dopóki sam się nie rozłączy; komendy mogą przychodzić wieloma
  porcjami w czasie. Własny bufor linii `otrsp_tcp_buf`/`otrsp_tcp_len`, niezależny od USB
  (`in_buf`) — oba kanały działają **równolegle**, bez wzajemnego mieszania komend. Jeden aktywny
  klient TCP na raz (nowe połączenie zastępuje bieżące przy odbiorze danych — rozróżniane po
  numerze gniazda, `EthernetClient::operator==`). Wymaga `EthModule` **wyłączonego** — patrz §2/§9.

## 7. Serwer WWW (przy `EthModule`)

Jednowątkowy serwer HTTP na porcie 80 (biblioteka `arduino-libraries/Ethernet`, oficjalna Arduino,
W5500 — auto-detekcja chipu; do 2026-07-29 była `adafruit/Ethernet2`, przestarzała/nieutrzymywana).

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
- *(opcja `WEB_ANT_NAMES`)* `N{I|G|M|D}={a.b.c.d}` — **konfiguracja sieciowa** (IP/gateway/maska/DNS)
  zamiast na sztywno w kodzie. `parseIPField()` kopiuje wartość do bufora i woła
  `IPAddress::fromString()` (biblioteka Arduino — nie własny parser); parsuje do zmiennej
  tymczasowej, commituje do żywej `ip`/`gateway`/`subnet`/`myDns` tylko gdy poprawny adres
  (błędny input jest **ignorowany**, stara wartość zostaje), potem `saveNetConfig()`. Zmiana
  wymaga restartu urządzenia (IP nie jest przeładowywane w locie).

**Generowanie strony.** **Całość idzie przez bufor wyjścia `BufP out(client)`** (patrz §9) — `out.print`/
`out.println`, na końcu `out.done()`. Statyczny nagłówek HTTP + `<head>` + CSS + ikona `POWER_SVG` są
w **PROGMEM** (`HTTP_HEAD`/`HTTP_HEAD2`/`POWER_SVG`) i lecą przez ten sam bufor. Dynamicznie (w
kontenerze `.wrap`, sekcje `.card`): **topbar** (kropka `.dot`
czerwona przy napięciu poza 10–15 V + nazwa stacji, `.tb`); karta **Anteny** — nagłówek `.ahead`
ze **statusami sekcji** (`.astat`/`.st`, chipy 50/50: numer + nazwa włączonej anteny) oraz wiersze
TRX `.trx` (przyciski anten w pętli, klasy `g`/`gr` = wybrana/kolizja; *(opcja)* Manual/BCD;
*(opcja)* plakietka PTT; **ikona power Radio Flex** `.flx` na końcu wiersza, `F{s}{0|1}`); karta
**Opis anten** (`.leg` — legenda numer→nazwa); karta **Settings** (`<details>`, zwinięta) z nazwą
stacji, nazwami anten (`.nm`), **edytowalną konfiguracją sieciową** (IP/gateway/maska/DNS, pętla
po 4 polach — oszczędność flash, jak przyciski anten) i **ukrytym odczytem napięcia**. Strona
odświeża się `meta refresh` co 10 s. Split usunięty. **Flash prawie pełny** — przy zmianach
HTML/CSS pilnuj budżetu.

**Wygląd (SQ9FK).** Konwencja designu przeniesiona z projektu
[`rotator_wifi_bridge`](https://github.com/sq9fk/rotator_wifi_bridge): ciemny motyw teal
(tło `#12333b`, karty `#1a3a42`/`#21505c`/`#2a5d6b`), akcent żółty `#f5d33c` (wybrana antena /
Flex WŁ.), alert `#d11534` (kolizja / błąd napięcia), font **Inter** z fallbackiem systemowym
(bez zewnętrznego linku — oszczędza flash i RTT). Zaokrąglone karty (`1.1em`) i przyciski (`.4em`).
Klasy CSS (`bcd/bcdr`, `g/gr`, `ptt`, `nm`, `trx`, `card`, `dot`) współdzielone z symulatorem
[`tools/websim.html`](../tools/websim.html) (`DEVICE_CSS` = lustro `HTTP_HEAD2`).

Podgląd wyglądu bez sprzętu: [`tools/websim.html`](../tools/websim.html) / `python tools/serve.py`.

## 8. Nazwy anten, nazwa stacji, konfiguracja sieciowa i EEPROM

- Domyślne nazwy: `antDefault[]` w PROGMEM. Indeks `0`=`OFF` (**wybieralny zawsze** — antena
  odłączona). Indeks `7` = `M-off->BCD` (**sentinel trybu BCD**) osiągalny **tylko przy `BCD_INPUT`**
  (enkoder 0..7) — pod `#ifdef`, w domyślnym buildzie nie istnieje i nie zajmuje flash. Sentinel
  przesunięty z 8 na 7 → **zlikwidowana martwa luka** po usuniętej 7. antenie (`antRAM` = `[8]`);
  domyślna nazwa stacji: `siteDefault` (`"SP9PDF"`). Radio Flex nie ma nazw (same ikony power).
- Przy `WEB_ANT_NAMES`: nazwy anten 1–6 w RAM `antRAM[8][ANT_MAXLEN+1]` oraz **nazwa stacji** w
  `siteRAM[ANT_MAXLEN+1]`, ładowane w `setup()` przez `loadAntNames()` z fallbackiem na domyślne.
  Zapis `saveAntNames()` używa `EEPROM.update` (mniejsze zużycie komórek).
- Przy `EthModule`/`OTRSP_TCP`: **konfiguracja sieciowa** (IP/gateway/maska/DNS) też w EEPROM,
  zamiast na sztywno w kodzie — `netCfg[4]` (wskaźniki na `ip/gateway/subnet/myDns`),
  `loadNetConfig()`/`saveNetConfig()` (zadeklarowane **po** tych zmiennych — `loadAntNames()` nie
  może ich dotykać, bo `IPAddress` tam jeszcze nie istnieje). Ładowane też dla `OTRSP_TCP` (bez
  formularza edycji — gdyby EEPROM miał już zapisane wartości z wcześniejszego flashowania
  wariantu WWW na tym samym urządzeniu).
- **Układ EEPROM (SQ9FK):** bajt `0` = magic `0xA5` (sekcja anten) + 6×`ANT_MAXLEN` (offset `1`);
  bajt `67` = magic `0x5B` (sekcja nazwy stacji) + `ANT_MAXLEN` (nazwa stacji, offset `68`);
  bajt `79` = magic `0x5C` (sekcja sieciowa) + 16 B — 4×4 B (`ip`/`gateway`/`subnet`/`myDns`,
  offset `80`). **Każda sekcja ma własny magic** — wgranie na istniejący EEPROM zachowuje to,
  co już tam było, a nowe sekcje (np. sieciowa na starszym EEPROM) startują z domyślnych.
- Odczyt zawsze przez `antName(idx)` / `siteName()` — zwraca `char*` (RAM) lub
  `__FlashStringHelper*` (PROGMEM) zależnie od flagi; oba typy obsługują `client.print()` i `String`.
  Konfiguracja sieciowa nie ma takiego rozróżnienia — `IPAddress` zawsze w RAM.

## 9. Budżet pamięci i optymalizacje

**Biblioteka Ethernet (2026-07-29):** migracja z `adafruit/Ethernet2` (przestarzała, nieutrzymywana)
na oficjalną `arduino-libraries/Ethernet`. Oficjalna biblioteka jest **większa** — obsługuje
W5100/W5200/W5500 z auto-detekcją chipu w runtime (kod dla wszystkich trzech zawsze skompilowany,
brak `#define`a ograniczającego do samego W5500, w przeciwieństwie do Ethernet2 pisanej wyłącznie
pod W5500). Efekt: **domyślny build (strona WWW) z DHCP przestał się mieścić** — stąd DHCP
wyłączone domyślnie (patrz niżej).

Domyślny build (strona WWW, **static IP edytowalna przez WWW**, DHCP wył.): **Flash 97,1 %**
(29822 B) / **RAM 48,2 %** (988 B). Flash **skrajnie ciasny** (~900 B wolne — konfiguracja
sieciowa edytowalna przez WWW kosztowała dodatkowe ~1 KB względem samego static IP na sztywno).
Build z wszystkim WŁ. (BCD+PTT+strona WWW) **już się nie mieści** niezależnie od DHCP.

**Sama strona WWW kosztuje ~8 KB flash** (HTML/CSS/`BufP`/print-y w bloku obsługi klienta) — nie
sam Ethernet. **DHCP kosztuje dodatkowo ~3,8 KB** (`Dhcp.cpp`+`EthernetUdp.cpp`, niewyłączalne
osobno od reszty biblioteki). Zmierzone (usunięcie ciała `if (client) {...}` strony WWW, zastąpione
zaślepką accept+close): Ethernet bez DHCP i bez strony WWW = **72,5 %** (22270 B); + `OTRSP` po
USB = **74,1 %** (22772 B); + `OTRSP_TCP` (USB+TCP równolegle) **z DHCP włączonym** = **82,8 %**
(25422 B) — tu jest zapas (~5,3 KB), więc ten wariant DHCP zostawia włączone. Strona WWW +
DHCP razem = **106,3 %** (32650 B) — **nie mieści się**, stąd domyślnie static IP dla `EthModule`.

Zastosowane techniki (patrz komentarze `//SQ9FK`):
- tablice stałe w **PROGMEM** (`antDefault`, `glyphs`, `BCDmatrixOUT`), `port[8][6]` jako `byte`;
- generowanie przycisków WWW i listy nazw w **pętli** (mniej powtórzonych łańcuchów `F()`);
  konfiguracja sieciowa (IP/gateway/maska/DNS) tym samym wzorcem (`netCfg[4]` + PROGMEM etykiety
  `netLbl[]`/`netCode[]`) zamiast 4 rozwiniętych bloków — istotne przy ~900 B zapasu;
- **`BufP`** — buforowanie **całego** wyjścia strony (RAM, porcje 128 B) zamiast per-znak `send()`
  do W5500: kilkadziesiąt segmentów TCP zamiast tysiąca → **znacznie szybsze wyświetlanie** i krótsza
  blokada `loop()`. Static (nagłówek/CSS/ikona) i dynamiczny HTML tym samym buforem;
- **batchowany odczyt żądania** — `client.read(buf,len)` (`recv`) + `memchr('\n')` zamiast po bajcie;
- parsowanie żądań **bez `String`** (bufor `char`), walidacja zakresu (brak zapisu poza `port[]`);
- czytanie tylko pierwszej linii żądania.

BCD/PTT/OTRSP/OTRSP_TCP jako `#ifdef` — wyłączone nie zajmują flash/RAM.

## 10. Mapa `#ifdef` (gdzie szukać przy zmianach)

| Funkcja | Miejsca w kodzie |
|---------|------------------|
| `EthModule` \| `OTRSP_TCP` | mac/ip/gateway/subnet + DHCP+fallback w `setup()`, `Ethernet.maintain()` w `loop()` — **wspólne** dla obu |
| `EthModule` | `server(80)`, `HTTP_HEAD*`, `POWER_SVG`, `BufP`, sekcja serwera WWW w `loop()` |
| `OTRSP_TCP` | `otrspServer`/`otrspClient`/`otrsp_tcp_buf`/`otrsp_tcp_len`, sekcja „OTRSP TCP" w `loop()` |
| `WEB_ANT_NAMES` | deklaracja `antRAM`/`siteRAM`/`antName`/`siteName`/EEPROM, `setup()` (`loadAntNames`), parser (`N`/`NS`) i formularze Settings, rozmiar `reqBuf` |
| `BCD_INPUT` | `BCDmatrixOUT`, gałąź auto w `loop()`, zakres enkodera, przyciski Manual/BCD w WWW, pozycja „7" w `show()`, cała funkcja `rx()` |
| `PTT_BLOCKING` | warunki `if(port[i][2]==0)` w `loop()`, plakietka PTT w `show()` i WWW, odczyt PTT w `rx()` |
| `OTRSP` | deklaracja `in_buf`/`in_len`, wywołanie w `loop()` (USB), `OTRSP_parse(char*,Print&)`, `serialEvent()` |

> Wykrywanie kolizji między TRX jest **osobne** od `PTT_BLOCKING` i działa zawsze.

## 11. Weryfikacja

- Kompilacja: `pio run -e nanoatmega328` (0 ostrzeżeń z naszego kodu; ostrzeżenia z biblioteki
  `Ethernet` są nieszkodliwe). Warto też zbudować z `-DBCD_INPUT -DPTT_BLOCKING` — razem ze
  stroną WWW **nie mieści się**; sprawdzaj te gałęzie bez `EthModule`.
- **Trzy warianty do sprawdzenia przy zmianach w Ethernet/OTRSP:** (1) domyślny (`EthModule`,
  strona WWW, `__USE_DHCP__` **wył.** — static IP); (2) `#define OTRSP` + `//#define EthModule`
  (USB, bez Ethernetu); (3) `#define OTRSP` + `#define OTRSP_TCP` + `//#define EthModule` (USB+TCP
  równolegle, `__USE_DHCP__` można zostawić **wł.** — ma zapas). Kombinacja `EthModule` +
  `OTRSP_TCP` **musi** dać `#error` w compile-time (celowe zabezpieczenie, nie regresja). Kombinacja
  `EthModule` + `__USE_DHCP__` **musi się nie mieścić** (106,3% — celowe, nie regresja).
- Testy funkcjonalne (W5500, EEPROM, przełączanie, klient OTRSP przez TCP) — na docelowej
  stacji (brak sprzętu w CI).
