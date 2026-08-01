# DESIGN — architektura firmware SP9PDF RemoteQTH 6×2 Antenna Switch

Opis budowy i działania firmware [`src/main.ino`](../src/main.ino). Sprzęt i pinout:
[`CONNECTIONS.md`](CONNECTIONS.md). Bazuje na RemoteQTH 6×2 (OK1HRA rev 0.3), zmodyfikowany
przez SQ9FK dla stacji SP9PDF.

## 1. Przegląd

Sterownik pozwala **2 transceiverom** dzielić **6 anten** bezkolizyjnie. Antenę dla każdego
TRX wybiera się:
- **ręcznie** — interfejs WWW (Ethernet) lub enkoder + LCD (zawsze dostępne);
- **automatycznie** — z danych pasma BCD radia (opcja `BCD_INPUT`, domyślnie wył.);
- **z komputera** — OTRSP/SO2R, po USB (opcja `OTRSP`, domyślnie wył.) lub po TCP (opcja
  `OTRSP_TCP`, **domyślnie wł.** razem ze stroną WWW, np. do N1MM+).

Rdzeń logiki (wybór anteny → wyjścia przekaźników, wykrywanie kolizji, LCD) działa niezależnie
od opcji. Pętla `loop()` jest jednowątkowa i nieblokująca poza obsługą pojedynczego żądania WWW.

## 2. Konfiguracja i flagi funkcji

Wszystkie `#define` są na górze `src/main.ino`.

| Flaga | Dom. | Znaczenie |
|-------|------|-----------|
| `Ports` | 2 | liczba TRX / linii LCD (2–4) |
| `Inputs` | 6 | liczba anten (etykieta „6x2") |
| `EthModule` | **wł.** | Ethernet W5500 + serwer WWW (zawsze static IP, bez DHCP) |
| `WWW_EEPROM_NAMES` | **wł.** | edycja nazw anten przez WWW + zapis EEPROM |
| `ANT_MAXLEN` | 11 | limit długości nazwy anteny (szerokość LCD) |
| `BCD_INPUT` | wył. | automatyczny wybór anteny z BCD radia (`rx()`) |
| `PTT_BLOCKING` | wył. | odczyt PTT + blokada przełączania podczas TX |
| `OTRSP` | wył. | sterowanie SO2R po porcie szeregowym (USB) |
| `OTRSP_TCP` | **wł.** | surowe gniazdo TCP dla OTRSP — niezależne od `OTRSP`; domyślnie razem z `EthModule` (zapas ~910 B) |
| `OTRSP_TCP_PORT` | 4534 | port surowego TCP dla OTRSP |
| `inputHigh` | wł. | poziom aktywny wejść BCD |

**Ograniczenie rozmiaru:** ATmega328 ma 30 KB flash / 2 KB RAM. `OTRSP` i `OTRSP_TCP` są
**niezależne od siebie** — wspólny `OTRSP_parse()` kompiluje się, gdy zdefiniowany jest
którykolwiek z nich (patrz §8). `EthModule` (strona WWW) **mieści się z każdym z nich osobno,
oraz z oboma naraz**: samo `OTRSP` (USB) — zmierzone **95,4%**, zapas ~1,4 KB; samo `OTRSP_TCP`
(gniazdo TCP) — zmierzone **97,0%**, zapas **~910 B**; **oba naraz**
(`EthModule`+`OTRSP`+`OTRSP_TCP`) — zmierzone **97,3%**, zapas ~824 B, **najciaśniejszy ze
wszystkich sześciu wariantów** (patrz §9). Do 2026-07-30 ta ostatnia kombinacja była zablokowana
przez `#error` w compile-time (brakowało ~116 B); po przeglądzie bugów 2026-07-29 (odzyskane
~660 B z usunięcia `String`) kombinacja faktycznie się mieściła, a **2026-08-01 `#error` został
usunięty** — wszystkie sześć wariantów jest teraz dostępnych. **Domyślnie: Ethernet wł. +
`OTRSP_TCP` wł.** (strona WWW + OTRSP po TCP), `OTRSP` (USB) wył. — to drugi najciaśniejszy
z sześciu wariantów i jest teraz domyślny, patrz §9/§11.
**Sześć wariantów** — patrz §9, §11.

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

0. **Watchdog + restart** — `wdt_reset()` na samym początku (jeśli `loop()` się zawiesi i nie
   wróci tu w ciągu ~8 s, AVR resetuje się sam — patrz §9 „Watchdog"). *(opcja `EthModule`)*
   jeśli `pendingRestart` (ustawione przez `/?R1` w poprzedniej iteracji, patrz §7) —
   `wdt_enable(WDTO_15MS); while(1){}` — restart urządzenia, **przed** czymkolwiek innym w tej
   iteracji (klient WWW już dostał pełną odpowiedź w poprzedniej iteracji).
1. **(opcja) OTRSP przez USB** — jeśli przyszła kompletna komenda serial (`serialEvent()`,
   terminator CR), `OTRSP_parse(in_buf, Serial)`.
2. **GPIO / logika przełączania** — dla każdego TRX `i`:
   - *(opcja `BCD_INPUT`)* jeśli tryb auto → `rx()` czyta antenę z BCD radia;
   - **wykrywanie kolizji**: licznik `c` porównuje `port[i][1]` (żądanie) z `port[j+4][1]`
     (wyjście innego TRX). Kolizja gdy ta sama antena (≠0). (Blokada 4↔5 GXP usunięta — 6 anten.);
   - jeśli kolizja → `port[i][3]=1`, wyjście OFF; inaczej → wyjście = żądanie
     *(przy `PTT_BLOCKING` przełączenie wstrzymane, gdy PTT aktywne)*;
   - `tx()` wystawia wyjścia na ekspander OUT.
3. **(opcja `EthModule`)** obsługa jednego klienta strony WWW (patrz §7) + *(opcja `OTRSP_TCP`)*
   obsługa **trwałego** połączenia OTRSP-TCP (patrz §6 — inny model niż request/response WWW).
4. **(opcja `serialECHO`)** — telemetria na serial.
5. **Przycisk** — długie naciśnięcie przełącza `menu1state` (tryb edycji enkoderem).
6. **LCD** — odświeżenie linii (`show()`) co 100 ms + kontrola napięcia co 3 s. Ostrzeżenia
   LOW/HIGH są **nieblokujące** (znacznik `voltWarn`, ~2 s bez `delay()`) — awaria napięcia nie
   zamraża WWW/przełączania/enkodera.

**Start (`setup()`).** Pierwsze linie: `MCUSR=0; wdt_disable();` — niektóre bootloadery nie
czyszczą włączonego watchdoga przed skokiem do aplikacji; bez tego, po resecie WDT z krótkim
timeoutem (np. z przycisku Restart), urządzenie wpadłoby w pętlę resetów. Splash LCD (2 s) +
napięcie (3 s) dają czas na rozruch W5500. **DHCP usunięte z projektu** (koszt ~3,8 KB
z oficjalną biblioteką nie mieścił się razem ze stroną WWW, a poza WWW nie było go sensu
trzymać dla jednego wariantu — patrz §9) — `Ethernet.begin(mac, ip, myDns, gateway, subnet)`
woła się **zawsze bezpośrednio**, bez retry-loop ani fallbacku. **IP pokazywane na LCD (5 s)**
przez `lcd.print(Ethernet.localIP())` — uwzględnia ewentualną edycję `ip`/`gateway`/`subnet`
przez WWW Settings + EEPROM, załadowaną wcześniej przez `loadNetConfig()`. **Watchdog włączany
na samym końcu** `setup()` (`wdt_enable(WDTO_8S)`) — musi być **po** wszystkich `delay()`
powyżej (łącznie do ~10 s w wariancie z `EthModule`/`OTRSP_TCP`), inaczej WDTO_8S (najdłuższy
pojedynczy timeout AVR) zresetowałby urządzenie w trakcie rozruchu, zanim dojdzie do `loop()`.

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
  **Kompiluje się, gdy zdefiniowany jest `OTRSP` LUB `OTRSP_TCP`** (`#if defined(OTRSP) ||
  defined(OTRSP_TCP)`) — `serialEvent()`/`in_buf` (specyficzne dla USB) zostają pod samym
  `OTRSP`, więc kanał TCP nie ciągnie za sobą kodu obsługi USB, którego nie potrzebuje.
- **(opcja) OTRSP przez TCP (`OTRSP_TCP`, niezależne od `OTRSP`):** dodatkowy `EthernetServer
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
- `J` — **odczyt stanu dla `rotator_wifi_bridge`** (SQ9FK): odpowiedź `A={port[0][1]},{port[1][1]}`
  (antena TRX1,TRX2), bez pełnej strony. Reużywa `HTTP_HEAD` zamiast nowego literału PROGMEM —
  konsumentem jest parser na moście (szuka podciągu `A=`), nie przeglądarka, więc rezygnacja
  z poprawnego HTML-a nic nie kosztuje, a endpoint jest tani we flashu (patrz §9). Bez tego most
  musiałby parsować pełny HTML karty Anteny, co jest kruche przy każdej zmianie jej wyglądu.
- `K` — **nazwy anten dla `rotator_wifi_bridge`** (SQ9FK): odpowiedź `K=` + 6 nazw (`antName(k)`,
  k=1..6) rozdzielonych przecinkiem, tak żeby jego legenda pokazywała **prawdziwe** nazwy z tego
  urządzenia zamiast osobnej, ręcznie duplikowanej kopii po drugiej stronie. Ta sama zasada co `J`
  (reużywa `HTTP_HEAD`), ale droższa — pętla po 6 nazwach zmiennej długości zamiast dwóch liczb —
  **+72 B, zapas na domyślnym wariancie spadł do ~106 B** (patrz §9). Separator `,`: nazwa anteny
  nie powinna go zawierać (`parseName()` tego nie zabrania, ale nikt tak nie robi).
- `F{s}{0|1}` — **Radio Flex** (SQ9FK): `s`=`reqBuf[7]` (1/2), stan=`reqBuf[8]` → `flexState[s-1]`.
- `R1` — **Restart** (przycisk w Settings): ustawia `pendingRestart`; sam restart (watchdog
  z timeoutem 15 ms) wykonuje się dopiero na początku **następnej** iteracji `loop()` (patrz §4),
  żeby klient najpierw dostał pełną, normalnie wyrenderowaną odpowiedź.
- *(opcja `WWW_EEPROM_NAMES`)* `N{k}={nazwa}` — `k`=`reqBuf[7]` (1..6); `parseName()` dekoduje
  URL (`+`→spacja, `%XX`) do `antRAM[k]` z obcięciem do `ANT_MAXLEN`, potem `saveAntNames()`.
- *(opcja `WWW_EEPROM_NAMES`)* `NS={nazwa}` — nazwa stacji (topbar) → `siteRAM` (`parseName()` + `saveAntNames()`).
- *(opcja `WWW_EEPROM_NAMES`)* `N{I|G|M|D}={a.b.c.d}` — **konfiguracja sieciowa** (IP/gateway/maska/DNS)
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
po 4 polach — oszczędność flash, jak przyciski anten) i **przyciskiem Restart** (`/?R1`, patrz
wyżej). Napięcie **nie jest już** wyświetlane w Settings (usunięte dla oszczędności flash —
ostrzeżenie w topbarze zostaje). Pole nazwy stacji, pętla nazw anten, pętla sieci i przycisk
Restart mają niemal identyczny HTML — generowane przez **wspólne stałe PROGMEM**
(`nmOpen`/`nmMid`/`nmLen11`/`nmClose`, patrz §9 „Watchdog + przycisk Restart") zamiast osobnych
literałów `F()` z tą samą treścią w każdym bloku. Strona odświeża się `meta refresh` co 10 s.
Split usunięty. **Flash prawie pełny** — przy zmianach HTML/CSS pilnuj budżetu.

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
  domyślna nazwa stacji: `siteDefault` (`"SQ9FK"`). Radio Flex nie ma nazw (same ikony power).
- Przy `WWW_EEPROM_NAMES`: nazwy anten 1–6 w RAM `antRAM[8][ANT_MAXLEN+1]` oraz **nazwa stacji** w
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
pod W5500). Efekt: **domyślny build (strona WWW) z DHCP przestał się mieścić**, a DHCP przydawało
się tylko w wariantach bez strony WWW — **DHCP zostało w całości usunięte z projektu**
(`__USE_DHCP__`, retry-loop w `setup()`, `Ethernet.maintain()` w `loop()`); IP jest teraz zawsze
statyczne, bez wyjątków.

**Endpoint `/?J`** (2026-07-29, patrz §7) dodał **+68 B** do wszystkich wariantów z `EthModule`
(reużywa `HTTP_HEAD` zamiast nowego literału — koszt to praktycznie tylko warunek + `F("A=")` +
dwa `out.print()` już używane gdzie indziej). Domyślny build: **Flash 99,4 %** (30542 B) —
**zapas ~178 B**. Zmierzono wszystkie 5 wariantów po tej zmianie, wszystkie się mieszczą.

**Endpoint `/?K`** (2026-07-29, patrz §7) dodał kolejne **+72 B** — pętla po 6 nazwach (`antName(k)`)
zamiast dwóch liczb jak w `/?J`, więc drożej mimo tego samego triku (reużycie `HTTP_HEAD`). Domyślny
build: **Flash 99,7 %** (30614 B) — **zapas ~106 B**. To już bardzo mało: kolejna zmiana we
współdzielonym kodzie WWW/OTRSP_TCP może wymagać najpierw znalezienia oszczędności gdzie indziej,
zanim się zmieści. Zmierzono wszystkie 5 wariantów, wszystkie się mieszczą.

**Nazwa stacji w `/?K`** (2026-07-30): rotator_wifi_bridge pokazuje teraz nazwę tego urządzenia przy
nagłówku karty Anteny — dokładnie tak, jak już pokazuje nazwy anten, żywo z urządzenia zamiast
osobnej kopii. Dopisana jako **7. pole** istniejącej odpowiedzi `/?K` (`out.print(',');
out.print(siteName());`) zamiast nowego endpointu — koszt to tylko te dwa wywołania, +16 B (nie
+68 B jak nowy endpoint reużywający `HTTP_HEAD`). Domyślny build: **Flash 99,7 %** (30630 B) —
**zapas ~90 B**. Zmierzono wszystkie trzy warianty z `EthModule` (jedyne, które w ogóle kompilują
handler `/?K`), wszystkie się mieszczą.

**Domyślny build to `EthModule` + `OTRSP_TCP`** (strona WWW + OTRSP po TCP, np. do N1MM+):
**Flash 97,0 %** (29810 B) — **~910 B** zapasu (patrz przegląd bugów 2026-07-29 wyżej — było
99,7 %/30630 B, ~90 B, przed tym przeglądem). To drugi najciaśniejszy z sześciu wariantów (patrz
§11; najciaśniejszy to `EthModule`+`OTRSP`+`OTRSP_TCP` naraz, ~824 B, patrz niżej) i jest teraz
domyślny — każda zmiana we współdzielonym kodzie (WWW HTML/CSS, `OTRSP_parse()`, sieć) musi być
zbudowana i zmierzona na **obu** tych wariantach **najpierw**. Ciekawostka: sam kanał TCP
(`EthernetServer`/`EthernetClient`) kosztuje więcej flash niż kanał USB (`serialEvent()` +
`in_buf`), mimo że intuicyjnie wydawałoby się odwrotnie — kod OTRSP_TCP (parser + bufor + logika
w `loop()`) waży ~968 B. (Uwaga: `EthernetClient::connect(hostname)`/`DNSClient::getHostByName`
— ~1,2 KB martwego kodu ciągniętego przez wirtualną tabelę `EthernetClient` — to koszt **już
obecny w każdym buildzie z `EthernetClient`**, nawet w najlżejszym WWW-bez-OTRSP, nie coś
specyficznego dla `OTRSP_TCP`.) Pierwotny niedobór 42 B odzyskano **poprawką logiki reconnect**
w bloku „OTRSP TCP" (`loop()`, patrz §8) — poprzednia wersja resetowała `otrspClient` przez
`otrspClient = EthernetClient()` zamiast po prostu wołać `.stop()` na starym połączeniu przy
zastąpieniu nowym, co dodatkowo **poprawiło poprawność** (stare gniazdo W5500 jest teraz
zawsze jawnie zamykane). **Oba kanały OTRSP naraz** (`EthModule`+`OTRSP`+`OTRSP_TCP`) — od
2026-08-01 **odblokowane**: **Flash 97,3 %** (29896 B) — zapas ~824 B, patrz niżej.

**Watchdog + przycisk Restart** (patrz §8) dołożyły **+252 B** do wszystkich wariantów z
`EthModule` (+46 B sam watchdog, wszystkie warianty; +206 B HTML przycisku Restart, tylko
`EthModule`) — domyślny build (wtedy jeszcze bez zapasu, 100,0 %/30716 B) **przestał się
mieścić o 248 B**. Odzyskano **494 B** przez (a) usunięcie wyświetlania napięcia z Settings
(zostało tylko ostrzeżenie — czerwona kropka w topbarze, sama wartość liczbowa nie była
niczym więcej niż ciekawostką) i (b) wydzielenie **wspólnych stałych PROGMEM**
(`nmOpen`/`nmMid`/`nmLen11`/`nmClose`, patrz §7) dla pól Settings — nazwa stacji, pętla nazw
anten i pętla sieci miały niemal identyczny HTML zapisany jako 3 osobne literały `F()` (kompi­
lator NIE scala automatycznie różnych literałów `F()`, nawet jeśli mają wspólne podłańcuchy).
Efekt netto: domyślny build ma teraz **większy zapas niż przed dodaniem watchdoga/Restart**
(246 B vs pierwotne 4 B).

**Przegląd bugów (2026-07-29)** — dogłębny przegląd `main.ino` znalazł i naprawił 6 realnych
błędów: (1) `OTRSP_parse()` — `AUX1n`/`AUX2n` maskowało wartość `& 15` (0-15), ale nigdy nie
walidowało jej względem faktycznego zakresu anten (0-6, lub 0-7 z `BCD_INPUT`) przed zapisem do
`port[idx][1]` — realny OOB-read w `antName()`, osiągalny przez sieć/port szeregowy; naprawione
identyczną walidacją jak już istniała w handlerze WWW; (2) odczyt żądania WWW nie miał timeoutu —
klient, który otworzył połączenie i nic nie wysłał, blokował `loop()` w nieskończoność; dodano
limit 2000 ms; (3) `e`, `enc0Pos` i `Timeout[5][2]` — zmienne czytane w `loop()` a zapisywane w
ISR enkodera (`encI()`) nie były `volatile`; `Timeout` (typ `unsigned long`, 4 B) był dodatkowo
podatny na "torn read" przy przerwaniu w trakcie odczytu; (4) globalny `String Note` w `show()`
(wołane co ~100 ms) zastąpiony stałobuforowym `char noteBuf[]` na stosie — unika fragmentacji
sterty na urządzeniu z 2 KB RAM i długim czasem pracy; **niespodziewany efekt uboczny: odzyskano
~660 B flash**, bo metody `String` (concat/assign/alokacja) ciągną za sobą sporo kodu, nie tylko
RAM; (5) `enc2()` używało globalnego `e` zamiast przekazanego parametru `count` — literówka, która
przy pewnych wywołaniach czytała nieaktualną wartość enkodera; (6) `reqBuf` dla wariantu
`WWW_EEPROM_NAMES` miało 48 B, za mało dla najdłuższego możliwego żądania (edycja pola sieciowego
z pełną nazwą) — zwiększone do 56 B. Efekt na domyślnym buildzie: **Flash 97,0 %** (29810 B) —
**zapas ~910 B** (patrz też §9 niżej) — głównie dzięki (4); (1)/(2)/(6) kosztowały łącznie kilka
do kilkudziesięciu bajtów, (3) i (5) nie mają wpływu na rozmiar. Przy tym zapasie kombinacja
`EthModule`+`OTRSP`+`OTRSP_TCP` (wszystkie trzy naraz) **technicznie już się mieściła**
(zmierzone: 97,3 %, zapas ~824 B) — `#error` blokujący tę kombinację nie został od razu zdjęty
w ramach tego przeglądu (odblokowanie 6. wariantu uznano wtedy za osobną decyzję
architektoniczną), ale **2026-08-01 blokada została usunięta** — patrz §9/§11 niżej.

**Strona WWW bez OTRSP** (`EthModule` samo, static IP edytowalna przez WWW) — bezpieczniejszy
wybór z dużym zapasem, jeśli OTRSP-TCP nie jest potrzebne: **Flash 93,9 %** (28856 B), ~1,8 KB
wolne. **`EthModule` + `OTRSP`** (USB, bez TCP): **Flash 95,4 %** (29316 B) — zapas ~1,4 KB.
Build z wszystkim WŁ. (BCD+PTT+strona WWW) **już się nie mieści** niezależnie od wariantu OTRSP.

**Sama strona WWW kosztuje ~8 KB flash** (HTML/CSS/`BufP`/print-y w bloku obsługi klienta) — nie
sam Ethernet. Bez WWW: `OTRSP` po USB (bez TCP, bez Ethernetu) = **33,6 %** (10322 B);
+ `OTRSP_TCP` (USB+TCP równolegle) = **68,0 %** (20900 B).

*Historia (przed usunięciem DHCP z projektu):* DHCP kosztowało dodatkowo ~3,8 KB
(`Dhcp.cpp`+`EthernetUdp.cpp`, niewyłączalne osobno od reszty biblioteki). Strona WWW + DHCP
razem = **106,3 %** (32650 B) — **nie mieściło się**, co był głównym powodem, dla którego DHCP
było domyślnie wyłączone dla `EthModule`, a docelowo — skoro dawało realną korzyść tylko
w wariancie `OTRSP`+`OTRSP_TCP` bez WWW (tam był zapas ~5,3 KB) — usunięte z projektu w całości
zamiast trzymane jako opcja dla jednego przypadku.

Zastosowane techniki (patrz komentarze `//SQ9FK`):
- tablice stałe w **PROGMEM** (`antDefault`, `glyphs`, `BCDmatrixOUT`), `port[8][6]` jako `byte`;
- generowanie przycisków WWW i listy nazw w **pętli** (mniej powtórzonych łańcuchów `F()`);
  konfiguracja sieciowa (IP/gateway/maska/DNS) tym samym wzorcem (`netCfg[4]` + PROGMEM etykiety
  `netLbl[]`/`netCode[]`) zamiast 4 rozwiniętych bloków — istotne przy ~246 B zapasu domyślnego
  buildu;
- **wspólne stałe PROGMEM dla pól Settings** (`nmOpen`/`nmMid`/`nmLen11`/`nmClose`) — nazwa
  stacji, pętla nazw anten, pętla sieci i przycisk Restart miały niemal identyczny HTML
  zapisany jako osobne literały `F()` (kompilator NIE scala różnych literałów `F()`, nawet
  z identycznymi podłańcuchami) — odzyskano ~440 B;
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
| `EthModule` \| `OTRSP_TCP` | mac/ip/gateway/subnet + `Ethernet.begin(mac,ip,myDns,gateway,subnet)` w `setup()` — **wspólne** dla obu |
| `EthModule` | `server(80)`, `HTTP_HEAD*`, `POWER_SVG`, `BufP`, sekcja serwera WWW w `loop()` |
| `OTRSP_TCP` | `otrspServer`/`otrspClient`/`otrsp_tcp_buf`/`otrsp_tcp_len`, sekcja „OTRSP TCP" w `loop()` |
| `WWW_EEPROM_NAMES` | deklaracja `antRAM`/`siteRAM`/`antName`/`siteName`/EEPROM, `setup()` (`loadAntNames`), parser (`N`/`NS`) i formularze Settings, rozmiar `reqBuf` |
| `BCD_INPUT` | `BCDmatrixOUT`, gałąź auto w `loop()`, zakres enkodera, przyciski Manual/BCD w WWW, pozycja „7" w `show()`, cała funkcja `rx()` |
| `PTT_BLOCKING` | warunki `if(port[i][2]==0)` w `loop()`, plakietka PTT w `show()` i WWW, odczyt PTT w `rx()` |
| `OTRSP` \| `OTRSP_TCP` | `OTRSP_parse(char*,Print&)` — wspólne dla obu (`#if defined(OTRSP) \|\| defined(OTRSP_TCP)`) |
| `OTRSP` | deklaracja `in_buf`/`in_len`, wywołanie w `loop()` (USB), `serialEvent()` — tylko USB, niezależne od `OTRSP_TCP` |

> Wykrywanie kolizji między TRX jest **osobne** od `PTT_BLOCKING` i działa zawsze.

## 11. Weryfikacja

- Kompilacja: `pio run -e nanoatmega328` (0 ostrzeżeń z naszego kodu; ostrzeżenia z biblioteki
  `Ethernet` są nieszkodliwe). Warto też zbudować z `-DBCD_INPUT -DPTT_BLOCKING` — razem ze
  stroną WWW **nie mieści się**; sprawdzaj te gałęzie bez `EthModule`.
- **Sześć wariantów do sprawdzenia przy zmianach w Ethernet/OTRSP:** (1) **domyślny** —
  `#define EthModule` + `//#define OTRSP` + `#define OTRSP_TCP` (WWW + OTRSP po TCP,
  **97,0% — zapas ~910 B**) — buduj i mierz TĘ gałąź **najpierw**, przy dosłownie
  każdej zmianie w WWW/CSS/OTRSP_TCP/sieci, bo trafia do każdego kto skompiluje projekt bez
  ruszania `#define`; (2) `#define EthModule` + `//#define OTRSP` + `//#define OTRSP_TCP`
  (strona WWW bez OTRSP, static IP, 93,9%, zapas ~1,8 KB — bezpieczniejsza alternatywa); (3)
  `#define EthModule` + `#define OTRSP` + `//#define OTRSP_TCP` (WWW + OTRSP po USB, 95,4%);
  (4) `#define EthModule` + `#define OTRSP` + `#define OTRSP_TCP` (WWW + OTRSP po USB **i** TCP
  naraz, **97,3% — zapas ~824 B**, najciaśniejszy ze wszystkich sześciu — buduj i mierz też TĘ
  gałąź **od razu po** wariancie (1), odblokowana 2026-08-01, dawny `#error` usunięty); (5)
  `#define OTRSP` + `//#define EthModule` (USB, bez Ethernetu, 33,6%); (6) `#define OTRSP`
  + `#define OTRSP_TCP` + `//#define EthModule` (USB+TCP równolegle, bez WWW, 68,0%).
- Testy funkcjonalne (W5500, EEPROM, przełączanie, klient OTRSP przez TCP) — na docelowej
  stacji (brak sprzętu w CI).
