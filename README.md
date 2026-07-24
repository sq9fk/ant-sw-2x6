# SP9PDF RemoteQTH Antenna Switch (6×2)

Firmware sterownika przełącznika antenowego **6 anten × 2 TRX** oparty na projekcie
[RemoteQTH 6×2 Antenna Controller](https://remoteqth.com/6x2-antenna-controler.php)
(oryginał **OK1HRA**, rev 0.3), zmodyfikowany przez **SQ9FK** na potrzeby stacji **SP9PDF**.

Urządzenie pozwala dwóm transceiverom niezależnie i bezkolizyjnie korzystać ze wspólnego
zestawu anten, z blokadą przełączania podczas nadawania (PTT) i ochroną przed konfliktem,
gdy oba TRX żądają tej samej anteny.

## Sprzęt

- Platforma: **Arduino Nano** (ATmega328P)
- Ekspandery I/O: **MCP23017** na magistrali I²C (adresy 0x20–0x23)
- Wyświetlacz: **LCD 16×2** (jedna linia na TRX)
- Wejścia: enkoder obrotowy + podświetlany przycisk (menu ręczne)
- Pomiar napięcia zasilania na `A3` (ostrzeżenia LOW/HIGH < 10 V / > 15 V)
- Opcjonalny moduł Ethernet (interfejs WWW) — domyślnie wyłączony (`#define EthModule`)

Dokumentacja sprzętowa w repo:
- Schemat (SVG): [`docs/6x2-antenna-switch-control-03-sch.svg`](docs/6x2-antenna-switch-control-03-sch.svg)
- Projekt **KiCad** rev 03 (schemat + PCB + netlista): [`hw/`](hw/)
- **Analiza połączeń** (pinout MCU, I²C, przekaźniki, BCD/PTT, OTRSP), zweryfikowana
  netlistą: [`docs/CONNECTIONS.md`](docs/CONNECTIONS.md)

Źródło sprzętu: RemoteQTH / OK1HRA, licencja **CC BY-SA 4.0**
(<https://remoteqth.com/6x2-antenna-controler.php>).

## Funkcje / modyfikacje SQ9FK

W stosunku do oryginału OK1HRA (rev 0.3) wprowadzono:

- **7. pozycja anteny** — obsługa dodatkowej kombinacji wyjść (GXP11 ze sterowaniem
  zewnętrznym przekaźnikiem 40 m z pinu GPA7).
- **Sterowanie OTRSP** (SO2R) po porcie szeregowym — komendy `AUX1`/`AUX2` oraz zapytania
  `?AUX1`, `?AUX2`, `?NAME` (nazwa urządzenia: `2x6SP9PDFRemoteAntennaSwitch`).
  Kompatybilne m.in. z N1MM+.
- **Blokada kolizji między pozycjami 4 i 5** — GXP11 współdzieli tor, więc oba TRX nie mogą
  jednocześnie wybrać 4 i 5.
- **Wyłączona kontrola PTT** w torze RX/logice menu (patrz komentarze `//SQ9FK` w kodzie).
- **Ostrzeżenia napięciowe** na LCD przy zbyt niskim/wysokim napięciu zasilania.
- Interfejs WWW rozszerzony o 7. pozycję i etykietę „SP9PDF”.

Nazwy anten konfiguruje się w tablicy `ant[]` na początku pliku
[`SP9PDF-RemoteQTH-Antenna-Switch.ino`](SP9PDF-RemoteQTH-Antenna-Switch.ino),
a mapowanie BCD → wyjście w `BCDmatrixOUT[][]`.

## Konfiguracja (dyrektywy `#define`)

| Define        | Opis                                              |
|---------------|---------------------------------------------------|
| `Inputs`      | liczba anten (6)                                  |
| `Ports`       | liczba par IN/OUT i linii LCD (2)                 |
| `inputHigh`   | poziom aktywny wejść (HIGH — domyślnie)           |
| `OTRSP`       | włącza sterowanie OTRSP po porcie szeregowym      |
| `OTRSP_DEBUG` | logi diagnostyczne OTRSP                           |
| `SERBAUD`     | prędkość portu szeregowego (9600)                 |
| `EthModule`   | włącza moduł Ethernet + interfejs WWW             |
| `__USE_DHCP__`| DHCP dla modułu Ethernet                          |

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
│   └── CONNECTIONS.md             # analiza połączeń (kod + netlista)
├── hw/                            # projekt KiCad rev 03 (OK1HRA, CC BY-SA 4.0)
│   ├── 6x2-antenna-switch-control-03.zip
│   └── 6x2-antenna-switch-control-03/   # .sch, .kicad_pcb, .net, .lib, .pro
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

> Build zweryfikowany: `pio run -e nanoatmega328` → **SUCCESS**, bez ostrzeżeń
> (Flash 44,4% / 13652 B, RAM 59% / 1209 B).

### Arduino IDE

1. Skopiuj `src/main.ino` do katalogu o tej samej nazwie (`main/main.ino`) lub zmień nazwę.
2. Płytka: *Arduino Nano*, procesor *ATmega328P (Old Bootloader)* w razie potrzeby.
3. Wybierz port COM, *Upload*.

> Uwaga (z oryginału): dla szybszego startu z DHCP zmień w `Dhcp.h`
> `timeout = 60000` na `6000`.

## Licencja

- **Firmware** (`src/`, `reference/`): **GPL v3** — patrz [`LICENSE`](LICENSE).
  Oryginał © OK1HRA, modyfikacje © SQ9FK.
- **Sprzęt** (`hw/`, schemat): **CC BY-SA 4.0** © OK1HRA / RemoteQTH.com.
