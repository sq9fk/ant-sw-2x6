# SP9PDF RemoteQTH Antenna Switch (6×2)

Firmware sterownika przełącznika antenowego **6 anten × 2 TRX** oparty na projekcie
[RemoteQTH 6×2 Antenna Controller](https://remoteqth.com/6x2-antenna-controler.php)
(oryginał **OK1HRA**, rev 0.3), zmodyfikowany przez **SQ9FK** na potrzeby stacji **SP9PDF**.

Urządzenie pozwala dwóm transceiverom niezależnie i bezkolizyjnie korzystać ze wspólnego
zestawu anten, z ochroną przed konfliktem, gdy oba TRX żądają tej samej anteny. Sterowanie
odbywa się ręcznie (enkoder/LCD + interfejs WWW); automatyka BCD z radia oraz blokowanie
przez PTT to funkcje **opcjonalne** (patrz [Funkcje opcjonalne](#funkcje-opcjonalne-sq9fk)),
domyślnie wyłączone.

Rozwój firmware (modyfikacje SQ9FK, dokumentacja, symulator WWW) prowadzony jest przy wsparciu
[Claude Code](https://claude.com/claude-code) (Anthropic).

## Sprzęt

- Platforma: **Arduino Nano** (ATmega328P)
- Ekspandery I/O: **MCP23017** na magistrali I²C (adresy 0x20–0x23)
- Wyświetlacz: **LCD 16×2** (jedna linia na TRX)
- Wejścia: enkoder obrotowy + podświetlany przycisk (menu ręczne)
- Pomiar napięcia zasilania na `A3` (ostrzeżenia LOW/HIGH < 10 V / > 15 V)
- Moduł Ethernet **W5500** (interfejs WWW) — w firmware **domyślnie włączony** (`#define EthModule`)

Dokumentacja sprzętowa w repo:
- Schemat (SVG): [`docs/6x2-antenna-switch-control-03-sch.svg`](docs/6x2-antenna-switch-control-03-sch.svg)
- Projekt **KiCad** rev 03 (schemat + PCB + netlista): [`hw/`](hw/)
- **Analiza połączeń** (pinout MCU, I²C, przekaźniki, BCD/PTT, OTRSP), zweryfikowana
  netlistą: [`docs/CONNECTIONS.md`](docs/CONNECTIONS.md)
- **Architektura firmware** (model stanu, pętla, serwer WWW, flagi funkcji):
  [`docs/DESIGN.md`](docs/DESIGN.md)

Źródło sprzętu: RemoteQTH / OK1HRA, licencja **CC BY-SA 4.0**
(<https://remoteqth.com/6x2-antenna-controler.php>).

## Funkcje / modyfikacje SQ9FK

W stosunku do oryginału OK1HRA (rev 0.3) wprowadzono:

- **6 anten (projekt pierwotny)** — czysty one-hot `bit0..bit5` = anteny 1..6 (bez dawnej
  7. pozycji i bez sprzężenia GXP11 4/5).
- **Radio Flex — dwa niezależne wyjścia** na `GPA7`/`GPB7` (przekaźniki `K1`/`K2`, złącza
  `J7`/`J6`), przełączane **ikonami power w wierszach TRX** (WWW), niezależne od wyboru anteny.
  Dawniej `bit7` = przekaźnik pasma GXP11 40 m.
- **Nowy wygląd WWW** wg konwencji projektu
  [`rotator_wifi_bridge`](https://github.com/sq9fk/rotator_wifi_bridge) — ciemny motyw teal, karty,
  statusy sekcji przy nagłówku „Anteny", legenda „Opis anten", zwijana karta **Settings**.
  **Responsywny (mobile-first)** — cele dotykowe min. 44×44 px poniżej 520 px, wyśrodkowana ikona
  Flex, siatka przycisków TRX zawsze wyrównana do lewej krawędzi karty niezależnie od liczby
  zawiniętych linii (etykieta TRX na własnym pełnym wierszu na wąskich ekranach).
- **Edycja nazw przez WWW** (`WWW_EEPROM_NAMES`, karta Settings) — **nazwa stacji** (topbar) oraz
  nazwy anten 1–6, zapisywane w EEPROM (trwałe), z limitem długości 11 znaków.
- **Konfiguracja sieciowa edytowalna przez WWW** (karta Settings) — **IP, brama, maska, DNS**
  zamiast na sztywno w kodzie; zapisywane w EEPROM (`IPAddress::fromString`, walidacja — błędny
  adres jest ignorowany, stara wartość zostaje). Zmiana wymaga restartu urządzenia.
- **Sterowanie OTRSP** (SO2R, zgodne z [protokołem OTRSP](https://www.k1xm.org/OTRSP/OTRSP_Protocol.pdf))
  — komendy `AUX1`/`AUX2`, zapytania `?AUX1`/`?AUX2`/`?NAME`/`?`, zgodne m.in. z N1MM+. Komendy
  kończy CR (`\r`), zgodnie ze specyfikacją. Kanał USB (`OTRSP`, domyślnie **wyłączony**) i surowe
  gniazdo TCP (`OTRSP_TCP`, domyślny port 4534, domyślnie **włączone**) są **niezależne** — można
  włączyć jeden, drugi, albo oba naraz; sam parser komend jest wspólny
  (`OTRSP_parse(cmd, Print&)`), każdy kanał ma własny, niezależny bufor linii. Każdy z nich **osobno**
  mieści się razem ze stroną WWW (`EthModule`) — `OTRSP` (USB) z zapasem ~650 B, `OTRSP_TCP`
  (gniazdo TCP, **domyślny build**) z zapasem ~246 B (patrz uwaga niżej). **Oba naraz**
  (`OTRSP`+`OTRSP_TCP`) ze stroną WWW **nie mieszczą się** (brakuje ~116 B) — to jedyna blokowana
  kombinacja.
- **Automatyka BCD i blokowanie przez PTT** — dostępne jako opcje (`BCD_INPUT`, `PTT_BLOCKING`),
  domyślnie wyłączone (patrz [Funkcje opcjonalne](#funkcje-opcjonalne-sq9fk)).
- **Watchdog** (`<avr/wdt.h>`, zawsze włączony) — jeśli `loop()` się zawiesi (na WWW, TCP OTRSP,
  USB OTRSP albo czymkolwiek innym) i nie wróci w ciągu ~8 s, urządzenie samo się resetuje.
  Włączany dopiero na końcu `setup()` (po wszystkich `delay()` przy starcie — LCD/IP splash), żeby
  nie zresetować urządzenia w trakcie rozruchu.
- **Przycisk „Restart" w Settings** — realny, programowy restart urządzenia z poziomu WWW (nie
  tylko informacja, że trzeba go ręcznie zrestartować). Wykorzystuje watchdog ze skróconym
  timeoutem (15 ms) — jedyny niezawodny sposób softwarowego resetu na AVR (czyści też peryferia,
  nie tylko licznik rozkazów, w przeciwieństwie do skoku na adres 0).
- **Ostrzeżenia napięciowe** — czerwona kropka statusu w topbarze przy napięciu poza 10–15 V oraz
  ostrzeżenie na LCD — **nieblokujące** (nie zamrażają WWW/przełączania podczas awarii). Samo
  napięcie nie jest już pokazywane liczbowo w Settings (usunięte dla oszczędności flash).
- **Zawsze static IP** — bez DHCP (usunięte z projektu, patrz niżej) — urządzenie startuje pod
  stałym adresem, ustawionym w `src/main.ino` lub edytowalnym po flashowaniu przez WWW+EEPROM
  (patrz „Konfiguracja sieciowa" wyżej), IP na LCD widoczne od razu przy starcie.
- Optymalizacja rozmiaru firmware (PROGMEM, pętle WWW) i utwardzenie serwera WWW.
- **Biblioteka Ethernet: oficjalna `arduino-libraries/Ethernet`** (była `adafruit/Ethernet2` —
  przestarzała/nieutrzymywana). Kosztuje więcej flash niż Ethernet2 (obsługa W5100/W5200/W5500
  z auto-detekcją chipu w runtime, niewyłączalna `#define`m). **DHCP usunięte z projektu** —
  kosztowało ~3,8 KB flash, a przydatne było tylko w wariantach bez strony WWW; nie warto było
  trzymać całej ścieżki kodu dla tego jednego przypadku. Strona WWW działa zawsze na
  **static IP** (ustaw `ip`/`gateway`/`subnet` w `src/main.ino` pod docelową sieć).

Domyślne nazwy anten są w `antDefault[]`, domyślna nazwa stacji w `siteDefault` (`src/main.ino`).
Przy włączonym `WWW_EEPROM_NAMES` nazwa stacji i nazwy anten 1–6 są **edytowalne przez WWW** (karta
Settings) i zapisywane w EEPROM (patrz niżej).

## Konfiguracja (dyrektywy `#define`)

| Define          | Domyślnie | Opis                                                        |
|-----------------|-----------|---------------------------------------------------------------|
| `Inputs`        | 6         | liczba anten (stała — zmiana nie jest w pełni zaimplementowana) |
| `Ports`         | 2         | liczba par IN/OUT i linii LCD (wspiera 2–4)                  |
| `inputHigh`     | **WŁ.**   | poziom aktywny wejść (HIGH)                                  |
| `OTRSP`         | WYŁ.      | włącza sterowanie OTRSP po porcie szeregowym                |
| `OTRSP_TCP`     | **WŁ.**   | surowy TCP dla OTRSP — niezależne od `OTRSP`; domyślnie razem z `EthModule` (zapas ~4 B) |
| `OTRSP_TCP_PORT`| 4534      | port surowego TCP dla OTRSP                                  |
| `SERBAUD`       | 9600      | prędkość portu szeregowego                                   |
| `EthModule`     | **WŁ.**   | włącza moduł Ethernet + interfejs WWW (zawsze static IP, bez DHCP) |

### Funkcje opcjonalne (SQ9FK)

| Define          | Domyślnie | Opis                                                    |
|-----------------|-----------|---------------------------------------------------------|
| `WWW_EEPROM_NAMES` | **WŁ.**   | edycja nazwy stacji i nazw anten 1–6 przez WWW (karta Settings), zapis w EEPROM, limit `ANT_MAXLEN` (11 zn.) |
| `BCD_INPUT`     | WYŁ.      | automatyczny wybór anteny z BCD radia (wejścia MCP IN); wyłączony = tryb wyłącznie ręczny (WWW/enkoder), bez przełącznika Manual/BCD |
| `PTT_BLOCKING`  | WYŁ.      | odczyt PTT + blokada przełączania podczas TX + plakietka PTT; wyłączony zgodnie ze zmianą HW (gniazda PTT jako wyjścia) |

> **Wykrywanie kolizji** między TRX (blokada tej samej anteny, para 4↔5 GXP) działa
> **niezależnie** od `PTT_BLOCKING` — pozostaje aktywne.

## Struktura repozytorium

```
.
├── platformio.ini                 # konfiguracja budowania (Nano / ATmega328P)
├── src/
│   └── main.ino                   # AKTUALNY firmware SP9PDF/SQ9FK
├── reference/                     # firmware referencyjny (nie budowany)
│   ├── ant-sw-6x2-03_orig.ino     #   oryginał OK1HRA rev 0.3
│   ├── ant-sw-6x2-03.ino          #   wariant rev 03
│   └── ant-sw-6x2-04.ino          #   wariant rev 04
├── docs/
│   ├── 6x2-antenna-switch-control-03-sch.svg   # schemat (RemoteQTH rev 03)
│   ├── CONNECTIONS.md             # analiza połączeń (kod + netlista)
│   └── DESIGN.md                  # architektura firmware (model stanu, WWW, flagi)
├── hw/                            # projekt KiCad rev 03 (OK1HRA, CC BY-SA 4.0)
│   ├── 6x2-antenna-switch-control-03.zip
│   └── 6x2-antenna-switch-control-03/   # .sch, .kicad_pcb, .net, .lib, .pro
├── tools/
│   ├── websim.html                # symulator interfejsu WWW (test wyglądu bez sprzętu)
│   └── serve.py                   # launcher: wystawia symulator i otwiera przeglądarkę
├── README.md
├── CLAUDE.md                      # wskazówki dla Claude Code
└── LICENSE                        # GPLv3
```

## Kompilacja i wgranie

### PlatformIO (zalecane)

```bash
# Nano ze STARYM bootloaderem (57600):
pio run -e nanoatmega328 -t upload
# Nano z NOWYM bootloaderem (115200):
pio run -e nanoatmega328new -t upload
# Podgląd portu szeregowego (9600):
pio device monitor -b 9600
```

`Wire`/`SPI` są w rdzeniu AVR; `LiquidCrystal` i `Ethernet` PlatformIO pobiera automatycznie
z `lib_deps` w [`platformio.ini`](platformio.ini). Ethernet (`#define EthModule`/`OTRSP_TCP`
w `src/main.ino`) to oficjalna biblioteka Arduino `arduino-libraries/Ethernet` (2026-07-29:
zastąpiła przestarzałą `adafruit/Ethernet2`).

> **Domyślna konfiguracja: Ethernet WŁ. + OTRSP po TCP WŁ.** (strona WWW + gniazdo TCP dla
> OTRSP, np. dla N1MM+, port 4534 — static IP). Build zweryfikowany: `pio run -e nanoatmega328`
> → **SUCCESS**, bez ostrzeżeń (Flash **99,2%** / 30474 B, RAM **52,5%** / 1075 B — domyślne
> flagi: BCD/PTT wył., nazwy WWW wł., konfiguracja sieciowa edytowalna wł., watchdog wł.).
> Wariant **BCD+PTT razem z Ethernetem już się nie mieści** — te opcje bez `EthModule`.
>
> ⚠️ **Oficjalna biblioteka Ethernet kosztuje więcej flash niż Ethernet2** (obsługa
> W5100/W5200/W5500 z auto-detekcją chipu w runtime — niewyłączalna `#define`m, zawsze
> skompilowana). **DHCP usunięte z projektu** — kosztowało ~3,8 KB (`Dhcp.cpp`+`EthernetUdp.cpp`),
> przydatne było tylko w wariantach bez strony WWW; urządzenie zawsze startuje na **static IP**
> (ustaw `ip`/`gateway`/`subnet` w `src/main.ino` pod docelową sieć, albo edytuj po flashowaniu
> przez WWW+EEPROM). Sama strona WWW (HTML/CSS/`BufP`) kosztuje dodatkowo ~8 KB — to ona, nie
> sam Ethernet, zajmuje większość budżetu.
>
> `OTRSP` (USB) i `OTRSP_TCP` (gniazdo TCP) są **niezależne** — każdy może być włączony osobno,
> i każdy z osobna mieści się razem ze stroną WWW (`EthModule`). Wyklucza się rozmiarowo tylko
> **oba naraz razem z WWW** (patrz `#error` w `src/main.ino`). Pięć wariantów:
> - **Strona WWW + OTRSP po surowym TCP** (**obecnie, domyślne**): `#define EthModule`,
>   `//#define OTRSP`, `#define OTRSP_TCP` → Flash **99,2%** (30474 B), RAM **52,5%** (1075 B)
>   — zapas ~246 B. Nadal najciaśniejszy z pięciu wariantów — buduj go najpierw przy każdej
>   zmianie we współdzielonym kodzie (patrz `docs/DESIGN.md` §9/§11).
> - **Strona WWW, static IP, bez OTRSP**: `#define EthModule`, `//#define OTRSP`,
>   `//#define OTRSP_TCP` → Flash **96,2%** (29552 B), RAM **48,3%** (989 B) — zapas ~1,1 KB,
>   bezpieczniejszy wybór jeśli OTRSP-TCP nie jest potrzebne.
> - **Strona WWW + OTRSP po USB**: `#define EthModule`, `#define OTRSP`, `//#define OTRSP_TCP`
>   → Flash **97,9%** (30070 B), RAM **51,5%** (1055 B) — zapas ~650 B
> - **OTRSP po USB** (bez strony WWW): `//#define EthModule`, `#define OTRSP`, `//#define OTRSP_TCP`
>   → Flash **38,0%** (11680 B), RAM **37,3%** (763 B)
> - **OTRSP po USB + surowy TCP równolegle** (bez strony WWW): `//#define EthModule`,
>   `#define OTRSP`, `#define OTRSP_TCP` → Flash **70,5%** (21652 B), RAM **54,3%** (1112 B)
>
> Strona WWW + **oba** kanały OTRSP naraz (`EthModule`+`OTRSP`+`OTRSP_TCP`) **nie mieści się**
> (brakuje ~116 B) — to jedyna blokowana kombinacja, wymuszona `#error` w compile-time.

### Optymalizacje rozmiaru (zastosowane)

- `port[8][6]`, `BCDmatrixOUT` → `byte` / `PROGMEM`; `ant[]` i glify LCD → `PROGMEM`
  (odczyt: `antName()` / `pgm_read_byte` / `memcpy_P`) — **−264 B RAM**.
- Generowanie przycisków WWW (poz. 0–7) i listy anten zwinięte w pętlę, `switch` na `GET`
  uproszczony do `if` — **−1808 B Flash** (bez zmiany wygenerowanego HTML).

### Optymalizacje serwera WWW (zastosowane)

- **Buforowanie całego wyjścia (`BufP`)** — w bibliotekach Wiznet (Ethernet2 i oficjalna
  `arduino-libraries/Ethernet` — sprawdzone w obu) każde `write()` to osobny segment TCP z
  busy-waitem na `SEND_OK`, a `print(F("..."))` wysyła **znak po znaku** (setki drobnych pakietów).
  Klasa `BufP` zbiera znaki w RAM i oddaje do W5500 porcjami 128 B — cała strona idzie w kilkudziesięciu
  `send()` zamiast tysiąca, **wielokrotnie szybciej** i z krótszą blokadą `loop()`. Statyczny HTML
  (nagłówek/CSS/ikona) też przez ten bufor (`out.print`), na końcu `out.done()`.
- **Batchowany odczyt żądania** — dostępne bajty czytane jednym `client.read(buf,len)` (`recv`)
  zamiast po bajcie, koniec linii wykrywany `memchr` — mniej transakcji SPI na wejściu.
- **Walidacja żądania** (kontrola zakresu banku) — obce żądania (`/favicon.ico`, gołe `GET /`)
  nie przełączają już anten i nie piszą poza tablicą `port[]` (usunięty ukryty błąd OOB).
- Czytanie tylko pierwszej linii żądania (`GET …`) + usunięty debug `Serial.print` —
  krótsza obsługa połączenia. Wygenerowany HTML bez zmian.
- Parsowanie żądania **bez `String`** — bank/kod czytane wprost z bufora `char[16]`
  (usunięty globalny `String HTTP_req`). Brak alokacji sterty na ścieżce żądania.

### Arduino IDE

1. Skopiuj `src/main.ino` do katalogu o tej samej nazwie (`main/main.ino`) lub zmień nazwę.
2. Płytka: *Arduino Nano*, procesor *ATmega328P (Old Bootloader)* w razie potrzeby.
3. Wybierz port COM, *Upload*.

> Uwaga: instalacja biblioteki `Ethernet` (Library Manager, oficjalna Arduino) jest wymagana
> dla `EthModule`/`OTRSP_TCP`. Firmware nie używa DHCP (usunięte z projektu) ani nie wymaga
> żadnych patchy biblioteki — urządzenie startuje zawsze na static IP.

## Symulator interfejsu WWW

Podgląd wyglądu strony urządzenia bez sprzętu — odtwarza HTML/CSS generowany przez firmware.
Symuluje też **protokół OTRSP** (`OTRSP_parse()` z `src/main.ino`) — dwie niezależne, włączane
osobno checkboxami sekcje "OTRSP po USB" / "OTRSP po TCP", każda z monitorem terminala
(wysłane na niebiesko, odebrane na zielono — niezależnie od tego, kto w danym trybie jest
"komputerem", a kto "urządzeniem", patrz niżej) i przyciskami szybkich komend
(`?`, `?AUX1`, `?AUX2`, `?NAME`, `AUX101`, `AUX206`). Komendy `AUX1n`/`AUX2n` faktycznie
przełączają antenę na podglądzie WWW — obie symulacje (WWW i OTRSP) dzielą ten sam stan.

```bash
python tools/serve.py
```

Skrypt wystawia stronę lokalnie (domyślnie `http://127.0.0.1:8765/websim.html`) i otwiera ją
w przeglądarce. Można też otworzyć plik [`tools/websim.html`](tools/websim.html) bez serwera —
ale wtedy **nie działają** poniższe dwie funkcje realnego połączenia (wymagają `http://`).

**Prawdziwe połączenia OTRSP** (nie tylko symulacja w JS):
- **USB** — przycisk „Połącz z prawdziwym urządzeniem" w monitorze USB używa
  [Web Serial API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Serial_API)
  (Chrome/Edge) — przeglądarka prosi o wybór **realnego portu COM** i rozmawia bezpośrednio
  z podłączonym przełącznikiem antenowym (9600 8N1, jak `SERBAUD`). Wysyłane komendy i
  odpowiedzi realnego urządzenia widać w tym samym monitorze co w trybie symulacji.
- **TCP** — przycisk „Uruchom most dla N1MM+" otwiera WebSocket do `python tools/serve.py`,
  który jednocześnie nasłuchuje na **prawdziwym porcie TCP 4534** (jak `OTRSP_TCP_PORT`).
  Prawdziwy N1MM+ (albo inny program OTRSP) może się połączyć z `127.0.0.1:4534` dokładnie
  jak z prawdziwym urządzeniem w wariancie `OTRSP_TCP` — komendy trafiają do przeglądarki przez
  most (surowa "rura" bajtów w Pythonie, `tools/serve.py`), a `OTRSP_parse()` w JS liczy
  odpowiedź i odsyła ją z powrotem tym samym mostem. W tym trybie to **symulator gra rolę
  urządzenia** (odwrotnie niż USB, gdzie to my jesteśmy "komputerem" mówiącym do sprzętu).

## Licencja

- **Firmware** (`src/`, `reference/`): **GPL v3** — patrz [`LICENSE`](LICENSE).
  Oryginał © OK1HRA, modyfikacje © SQ9FK.
- **Sprzęt** (`hw/`, schemat): **CC BY-SA 4.0** © OK1HRA / RemoteQTH.com.
