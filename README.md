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
- **Edycja nazw przez WWW** (`WEB_ANT_NAMES`, karta Settings) — **nazwa stacji** (topbar) oraz
  nazwy anten 1–6, zapisywane w EEPROM (trwałe), z limitem długości 11 znaków.
- **Sterowanie OTRSP** (SO2R) — komendy `AUX1`/`AUX2`, zapytania `?AUX1`/`?AUX2`/`?NAME`
  (`2x6SP9PDFRemoteAntennaSwitch`), zgodne m.in. z N1MM+. **Opcjonalne** (`OTRSP`), domyślnie
  wyłączone — wyklucza się rozmiarowo z Ethernetem.
- **Automatyka BCD i blokowanie przez PTT** — dostępne jako opcje (`BCD_INPUT`, `PTT_BLOCKING`),
  domyślnie wyłączone (patrz [Funkcje opcjonalne](#funkcje-opcjonalne-sq9fk)).
- **Ostrzeżenia napięciowe** na LCD przy zbyt niskim/wysokim napięciu zasilania.
- Optymalizacja rozmiaru firmware (PROGMEM, pętle WWW) i utwardzenie serwera WWW.

Domyślne nazwy anten są w `antDefault[]`, domyślna nazwa stacji w `siteDefault` (`src/main.ino`).
Przy włączonym `WEB_ANT_NAMES` nazwa stacji i nazwy anten 1–6 są **edytowalne przez WWW** (karta
Settings) i zapisywane w EEPROM (patrz niżej).

## Konfiguracja (dyrektywy `#define`)

| Define          | Opis                                                        |
|-----------------|-------------------------------------------------------------|
| `Inputs`        | liczba anten (6)                                            |
| `Ports`         | liczba par IN/OUT i linii LCD (2)                           |
| `inputHigh`     | poziom aktywny wejść (HIGH — domyślnie)                     |
| `OTRSP`         | włącza sterowanie OTRSP po porcie szeregowym                |
| `SERBAUD`       | prędkość portu szeregowego (9600)                           |
| `EthModule`     | włącza moduł Ethernet + interfejs WWW                       |
| `__USE_DHCP__`  | DHCP dla modułu Ethernet                                    |

### Funkcje opcjonalne (SQ9FK)

| Define          | Domyślnie | Opis                                                    |
|-----------------|-----------|---------------------------------------------------------|
| `WEB_ANT_NAMES` | **WŁ.**   | edycja nazwy stacji i nazw anten 1–6 przez WWW (karta Settings), zapis w EEPROM, limit `ANT_MAXLEN` (11 zn.) |
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

`Wire`/`SPI` są w rdzeniu AVR; `LiquidCrystal` PlatformIO pobiera automatycznie z `lib_deps`
w [`platformio.ini`](platformio.ini). Dla wariantu Ethernet (`#define EthModule`
w `src/main.ino`) odkomentuj tam `arduino-libraries/Ethernet2`.

> **Domyślna konfiguracja: Ethernet WŁ., OTRSP WYŁ.** Build zweryfikowany:
> `pio run -e nanoatmega328` → **SUCCESS**, bez ostrzeżeń
> (Flash **95,8%** / 29418 B, RAM **45,4%** / 930 B — domyślne flagi: BCD/PTT wył., nazwy WWW wł.).
>
> ⚠️ Na Nano (30 KB flash / 2 KB RAM) **Ethernet i OTRSP** najlepiej trzymać osobno.
> Wybór konfiguracji:
> - **Ethernet** (obecnie): `#define EthModule`, `//#define OTRSP`
> - **OTRSP/SO2R**: `//#define EthModule`, `#define OTRSP` → Flash ~40%, RAM ~46%

### Optymalizacje rozmiaru (zastosowane)

- `port[8][6]`, `BCDmatrixOUT` → `byte` / `PROGMEM`; `ant[]` i glify LCD → `PROGMEM`
  (odczyt: `antName()` / `pgm_read_byte` / `memcpy_P`) — **−264 B RAM**.
- Generowanie przycisków WWW (poz. 0–7) i listy anten zwinięte w pętlę, `switch` na `GET`
  uproszczony do `if` — **−1808 B Flash** (bez zmiany wygenerowanego HTML).

### Optymalizacje serwera WWW (zastosowane)

- Statyczny nagłówek + CSS wysyłany z **PROGMEM** porcjami 64 B (`sendP()` → `client.write`)
  zamiast ~30 `print()` — mniej zapisów do W5500 i **krótsza blokada `loop()`** (PTT/przełączanie).
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

> Uwaga (z oryginału): dla szybszego startu z DHCP zmień w `Dhcp.h`
> `timeout = 60000` na `6000`.

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
