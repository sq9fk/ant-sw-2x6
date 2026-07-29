# SP9PDF RemoteQTH Antenna Switch (6×2)

Firmware sterownika przełącznika antenowego **6 anten × 2 TRX** oparty na projekcie
[RemoteQTH 6×2 Antenna Controller](https://remoteqth.com/6x2-antenna-controler.php)
(oryginał **OK1HRA**, rev 0.3), zmodyfikowany przez **SQ9FK** na potrzeby stacji **SP9PDF**.

Urządzenie pozwala dwóm transceiverom niezależnie i bezkolizyjnie korzystać ze wspólnego
zestawu anten, z ochroną przed konfliktem, gdy oba TRX żądają tej samej anteny. Sterowanie
odbywa się ręcznie (enkoder/LCD + interfejs WWW); automatyka BCD z radia oraz blokowanie
przez PTT to funkcje **opcjonalne** (patrz [Funkcje opcjonalne](#funkcje-opcjonalne-sq9fk)),
domyślnie wyłączone.

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
- **Edycja nazw przez WWW** (`WWW_EEPROM_NAMES`, karta Settings) — **nazwa stacji** (topbar) oraz
  nazwy anten 1–6, zapisywane w EEPROM (trwałe), z limitem długości 11 znaków.
- **Konfiguracja sieciowa edytowalna przez WWW** (karta Settings) — **IP, brama, maska, DNS**
  zamiast na sztywno w kodzie; zapisywane w EEPROM (`IPAddress::fromString`, walidacja — błędny
  adres jest ignorowany, stara wartość zostaje). Zmiana wymaga restartu urządzenia.
- **Sterowanie OTRSP** (SO2R, zgodne z [protokołem OTRSP](https://www.k1xm.org/OTRSP/OTRSP_Protocol.pdf))
  — komendy `AUX1`/`AUX2`, zapytania `?AUX1`/`?AUX2`/`?NAME`/`?`, zgodne m.in. z N1MM+. Komendy
  kończy CR (`\r`), zgodnie ze specyfikacją. **Opcjonalne** (`OTRSP`), domyślnie wyłączone —
  wyklucza się rozmiarowo ze stroną WWW (`EthModule`).
  Dodatkowa opcja **`OTRSP_TCP`** (wymaga `OTRSP`, wyklucza się z `EthModule`) udostępnia OTRSP
  jednocześnie po USB **i** po surowym gnieździe TCP (`OTRSP_TCP_PORT`, domyślnie 4534) — bez
  strony WWW, ale z Ethernetem/DHCP. Sam parser komend jest wspólny dla obu kanałów
  (`OTRSP_parse(cmd, Print&)`), każdy kanał ma własny, niezależny bufor linii.
- **Automatyka BCD i blokowanie przez PTT** — dostępne jako opcje (`BCD_INPUT`, `PTT_BLOCKING`),
  domyślnie wyłączone (patrz [Funkcje opcjonalne](#funkcje-opcjonalne-sq9fk)).
- **Ostrzeżenia napięciowe** na LCD przy zbyt niskim/wysokim napięciu zasilania — **nieblokujące**
  (nie zamrażają WWW/przełączania podczas awarii).
- **Odporny start sieci** (opcja `__USE_DHCP__`) — DHCP ponawiane (6 s/próbę) z **fallbackiem na
  static IP** po 3 próbach (urządzenie zawsze osiągalne) + `Ethernet.maintain()` (odnawianie
  dzierżawy). **Domyślnie WYŁĄCZONE** (static IP) — patrz niżej, migracja biblioteki.
- Optymalizacja rozmiaru firmware (PROGMEM, pętle WWW) i utwardzenie serwera WWW.
- **Biblioteka Ethernet: oficjalna `arduino-libraries/Ethernet`** (była `adafruit/Ethernet2` —
  przestarzała/nieutrzymywana). Kosztuje więcej flash niż Ethernet2 (obsługa W5100/W5200/W5500
  z auto-detekcją chipu w runtime, niewyłączalna `#define`m), stąd **DHCP wyłączone domyślnie**
  (sam koszt DHCP+UDP to ~3,8 KB) — strona WWW działa na **static IP** (ustaw `ip`/`gateway`/
  `subnet` w `src/main.ino` pod docelową sieć). Warianty z zapasem (np. `OTRSP_TCP`) mogą włączyć
  `__USE_DHCP__` z powrotem.

Domyślne nazwy anten są w `antDefault[]`, domyślna nazwa stacji w `siteDefault` (`src/main.ino`).
Przy włączonym `WWW_EEPROM_NAMES` nazwa stacji i nazwy anten 1–6 są **edytowalne przez WWW** (karta
Settings) i zapisywane w EEPROM (patrz niżej).

## Konfiguracja (dyrektywy `#define`)

| Define          | Opis                                                        |
|-----------------|-------------------------------------------------------------|
| `Inputs`        | liczba anten (6)                                            |
| `Ports`         | liczba par IN/OUT i linii LCD (2)                           |
| `inputHigh`     | poziom aktywny wejść (HIGH — domyślnie)                     |
| `OTRSP`         | włącza sterowanie OTRSP po porcie szeregowym                |
| `OTRSP_TCP`     | + surowy TCP dla OTRSP (wymaga `OTRSP`, wyklucza `EthModule`) |
| `OTRSP_TCP_PORT`| port surowego TCP dla OTRSP (domyślnie 4534)                |
| `SERBAUD`       | prędkość portu szeregowego (9600)                           |
| `EthModule`     | włącza moduł Ethernet + interfejs WWW                       |
| `__USE_DHCP__`  | DHCP dla modułu Ethernet (**wył. domyślnie** — koszt ~3,8 KB flash z oficjalną biblioteką) |

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

> **Domyślna konfiguracja: Ethernet WŁ. (strona WWW, static IP), OTRSP WYŁ.** Build zweryfikowany:
> `pio run -e nanoatmega328` → **SUCCESS**, bez ostrzeżeń
> (Flash **97,1%** / 29822 B, RAM **48,2%** / 988 B — domyślne flagi: DHCP wył., BCD/PTT wył.,
> nazwy WWW wł., konfiguracja sieciowa edytowalna wł.). Flash **prawie pełny** (~900 B wolne).
> Wariant **BCD+PTT razem z Ethernetem już się nie mieści** — te opcje bez `EthModule`.
>
> ⚠️ **Oficjalna biblioteka Ethernet kosztuje więcej flash niż Ethernet2** (obsługa
> W5100/W5200/W5500 z auto-detekcją chipu w runtime — niewyłączalna `#define`m, zawsze
> skompilowana). Sama obsługa **DHCP dokłada ~3,8 KB** (`Dhcp.cpp`+`EthernetUdp.cpp`) — z tego
> powodu **DHCP jest wyłączone domyślnie** dla strony WWW (`//#define __USE_DHCP__`), a urządzenie
> startuje na **static IP** (ustaw `ip`/`gateway`/`subnet` w `src/main.ino` pod docelową sieć).
> Sama strona WWW (HTML/CSS/`BufP`) kosztuje dodatkowo ~8 KB — to ona, nie sam Ethernet, zajmuje
> większość budżetu.
>
> Trzy warianty (wzajemnie wykluczające się — patrz `#error` w `src/main.ino`):
> - **Strona WWW, static IP** (obecnie): `#define EthModule`, `//#define __USE_DHCP__`,
>   `//#define OTRSP`, `//#define OTRSP_TCP` → Flash **97,1%**
> - **OTRSP po USB**: `//#define EthModule`, `#define OTRSP`, `//#define OTRSP_TCP`
>   → Flash ~38%, RAM ~37%
> - **OTRSP po USB + surowy TCP równolegle** (bez strony WWW, **z DHCP** — jest zapas):
>   `//#define EthModule`, `#define OTRSP`, `#define OTRSP_TCP`, `#define __USE_DHCP__`
>   → Flash **83,1%**, RAM **60,1%**

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
> dla `EthModule`/`OTRSP_TCP`. Dawniej trzeba było ręcznie patchować `Dhcp.h`, żeby skrócić
> timeout DHCP (60 s → 6 s) — teraz to parametr `Ethernet.begin(mac, timeout, responseTimeout)`
> w `src/main.ino`, żaden patch biblioteki nie jest potrzebny.

## Symulator interfejsu WWW

Podgląd wyglądu strony urządzenia bez sprzętu — odtwarza HTML/CSS generowany przez firmware.

```bash
python tools/serve.py
```

Skrypt wystawia stronę lokalnie (domyślnie `http://127.0.0.1:8765/websim.html`) i otwiera ją
w przeglądarce. Można też otworzyć plik [`tools/websim.html`](tools/websim.html) bez serwera.

## Licencja

- **Firmware** (`src/`, `reference/`): **GPL v3** — patrz [`LICENSE`](LICENSE).
  Oryginał © OK1HRA, modyfikacje © SQ9FK.
- **Sprzęt** (`hw/`, schemat): **CC BY-SA 4.0** © OK1HRA / RemoteQTH.com.
