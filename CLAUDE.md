# CLAUDE.md

Wskazówki dla Claude Code przy pracy nad tym repozytorium.

## Czym jest ten projekt

Firmware sterownika przełącznika antenowego **6 anten × 2 TRX** dla stacji SP9PDF.
Bazuje na RemoteQTH 6×2 Antenna Controller (OK1HRA, rev 0.3), zmodyfikowany przez SQ9FK.
Cel: dwa transceivery bezkolizyjnie współdzielą zestaw anten, z blokadą przełączania
podczas PTT i sterowaniem ręcznym (enkoder/LCD/WWW) oraz automatycznym (BCD z radia, OTRSP).

## Platforma i budowanie

- MCU: **Arduino Nano (ATmega328P)**, framework Arduino.
- Build: **PlatformIO** — `pio run` (env `nanoatmega328` = stary bootloader,
  `nanoatmega328new` = nowy). Wgranie: `pio run -t upload`. Monitor: 9600 8N1.
  `LiquidCrystal` i `Ethernet2` (`adafruit/Ethernet2`, W5500) są w `lib_deps` (NIE w rdzeniu
  PIO); `Wire`/`SPI` z rdzenia.
- **Budżet pamięci Nano (30 KB flash / 2 KB RAM):** `EthModule` i `OTRSP` wykluczają się —
  oba naraz to Flash ~97% / RAM ~70%. Domyślnie: **Ethernet WŁ., OTRSP WYŁ.**
  (Flash 93,2%, RAM 52,6%). `OTRSP_parse()` i `serialEvent()` są pod `#if defined(OTRSP)`.
  Włączenie OTRSP wymaga wyłączenia `EthModule` (i odwrotnie).
- Jedyny budowany plik to `src/main.ino`. Katalog `reference/` jest **poza** budowaniem
  (to warianty historyczne — nie kompilować, nie mieszać z `src/`).
- To sketch Arduino (`.ino`): funkcje mogą być użyte przed definicją (PlatformIO/Arduino
  generuje prototypy). `serialEvent()` to magiczna funkcja Arduino wywoływana między iteracjami
  `loop()`.

## Sprzęt

Projekt KiCad rev 03 jest w `hw/` (© OK1HRA, **CC BY-SA 4.0** — inna licencja niż firmware).
Zweryfikowany pinout i mapowanie połączeń: `docs/CONNECTIONS.md` (wyprowadzone z netlisty
`hw/.../ant-sw-control.net`). Płyta rev 03 = wersja 2-TRX: Nano `U8`, MCP23017 `U5`(OUT 0x20)/
`U6`(IN 0x21), drivery ULN `U4`/`U7`, LCD `U9`, opcjonalny W5500 `U1`. Nie modyfikować plików
w `hw/` bez potrzeby — to materiał źródłowy autora.

## Architektura kodu (src/main.ino)

- **Model stanu**: tablica `port[8][6]` — wiersze 0–3 = wejścia TRX1–4, 4–7 = wyjścia.
  Kolumny: `{adres_I2C, wybrana_antena, PTT, kolizja, tryb_ręczny, część(bank)}`.
- **Konfiguracja przez `#define`** na początku pliku: `Ports` (2 lub 4), `Inputs`,
  `inputHigh`, `OTRSP`, `OTRSP_DEBUG`, `EthModule`, `__USE_DHCP__`, `SERBAUD`.
- **I²C / MCP23017**: `0x20`/`0x22` = wyjścia, `0x21`/`0x23` = wejścia (patrz
  `docs/CONNECTIONS.md`). Rejestry GPIOA=0x12, GPIOB=0x13.
- **Kluczowe funkcje**: `rx()` (odczyt BCD+PTT, dekodowanie `BCDmatrixOUT`),
  `tx()` (one-hot na przekaźniki), `show()` (rysowanie linii LCD), `encI()`/`enc2()`
  (enkoder na przerwaniu), `OTRSP_parse()` (SO2R po serialu).
- **Nazwy anten**: tablica `ant[]` (indeks 0 = "OFF", ostatni = "M-off->BCD" — nie ruszać
  tych dwóch skrajnych).

## Modyfikacje SQ9FK — o czym pamiętać

Zmiany są oznaczone komentarzami `//SQ9FK` w kodzie. Przy edycji zachowaj spójność między
wszystkimi miejscami dotyczącymi danej zmiany:

- **7. pozycja anteny** — dotyczy: `ant[]`, zakres enkodera (`enc2(..., 7+1, ...)`),
  `tx()` (case 7 → `bit5`), interfejs WWW (przyciski „7"), warunki `==8` (dawniej `==7`)
  w `show()` i `loop()`.
- **GXP11 poz. 4/5** — współdzielą fizyczny port; `bit7` = przekaźnik pasma 40 m. Blokada
  kolizji 4↔5 jest w `loop()` **oraz** w `OTRSP_parse()` — zmieniać w obu miejscach.
- **Kontrola PTT wyłączona** — kilka wywołań `rx(..., 1, ...)` jest zakomentowanych.
  Nie „naprawiać" ich bez potwierdzenia — to celowa decyzja.

## Konwencje

- Język dokumentacji i komentarzy commitów: **polski** (użytkownik preferuje PL).
- Nowe zmiany firmware oznaczaj komentarzem `//SQ9FK` jak dotychczasowe.
- Nie commituj artefaktów budowania (`.pio/`, `*.hex`) — objęte `.gitignore`.
- Przy zmianie pinoutu/adresów I²C **zaktualizuj `docs/CONNECTIONS.md`**.

## Weryfikacja

- Kompilacja: `pio run` (bez ostrzeżeń o brakujących prototypach/bibliotekach).
- Brak realnego sprzętu w tym środowisku — testy funkcjonalne wykonuje użytkownik na stacji.
