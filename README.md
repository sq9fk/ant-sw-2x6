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

Schemat urządzenia (RemoteQTH, rev 03):
<https://remoteqth.com/hw/6x2-antenna-switch-control-03-sch.svg>

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

## Pliki w repozytorium

| Plik | Opis |
|------|------|
| `SP9PDF-RemoteQTH-Antenna-Switch.ino` | **Aktualny firmware SP9PDF/SQ9FK** |
| `ant-sw-6x2-04.ino` | wariant roboczy rev 04 (referencyjny) |
| `ant-sw-6x2-03.ino` | wariant rev 03 (referencyjny) |
| `ant-sw-6x2-03_orig.ino` | oryginalny firmware OK1HRA rev 0.3 |

## Kompilacja i wgranie

Arduino IDE:

1. Zainstaluj bibliotekę **LiquidCrystal** (w zestawie IDE). Dla wariantu Ethernet:
   **Ethernet2** oraz zależności (`Dhcp.h`, `EthernetServer.h`).
2. Płytka: *Arduino Nano*, procesor *ATmega328P (Old Bootloader)* w razie potrzeby.
3. Otwórz `SP9PDF-RemoteQTH-Antenna-Switch.ino`, wybierz port COM, *Upload*.

> Uwaga (z oryginału): dla szybszego startu z DHCP zmień w `Dhcp.h`
> `timeout = 60000` na `6000`.

## Licencja

GPL v3 — patrz [`LICENSE`](LICENSE). Oryginał © OK1HRA, modyfikacje © SQ9FK.
