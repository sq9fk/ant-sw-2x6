# CLAUDE.md

Wskazówki dla Claude Code przy pracy nad tym repozytorium.

## Czym jest ten projekt

Firmware sterownika przełącznika antenowego **6 anten × 2 TRX** dla stacji SP9PDF.
Bazuje na RemoteQTH 6×2 Antenna Controller (OK1HRA, rev 0.3), zmodyfikowany przez SQ9FK.
Cel: dwa transceivery bezkolizyjnie współdzielą zestaw anten. Sterowanie ręczne
(enkoder/LCD + interfejs WWW). Automatyka BCD z radia, blokowanie przez PTT i OTRSP to
funkcje **opcjonalne** (`#ifdef`, patrz „Funkcje opcjonalne") — domyślnie **wyłączone**.
Wykrywanie kolizji między TRX działa zawsze.

## Platforma i budowanie

- MCU: **Arduino Nano (ATmega328P)**, framework Arduino.
- Build: **PlatformIO** — `pio run` (env `nanoatmega328` = stary bootloader,
  `nanoatmega328new` = nowy). Wgranie: `pio run -t upload`. Monitor: 9600 8N1.
  `LiquidCrystal` i `Ethernet2` (`adafruit/Ethernet2`, W5500) są w `lib_deps` (NIE w rdzeniu
  PIO); `Wire`/`SPI` z rdzenia.
- **Budżet pamięci Nano (30 KB flash / 2 KB RAM):** `EthModule` i `OTRSP` trzymać osobno
  (wykluczają się rozmiarowo). Domyślnie: **Ethernet WŁ., OTRSP WYŁ., BCD/PTT WYŁ.,
  WEB_ANT_NAMES WŁ.** — Flash **83,0%** / RAM **44,7%**. Build z wszystkim WŁ. (BCD+PTT):
  ~87%. `OTRSP_parse()`/`serialEvent()` są pod `#if defined(OTRSP)`; włączenie OTRSP wymaga
  wyłączenia `EthModule`.
- **Optymalizacja rozmiaru — konwencje do zachowania:** `glyphs[6][8]` w `PROGMEM` (glify
  przez `memcpy_P`); `BCDmatrixOUT` w `PROGMEM` (`pgm_read_byte`, tylko przy `BCD_INPUT`);
  `port[8][6]` jest `byte`; nazwy anten patrz „Funkcje opcjonalne". Bloki WWW (przyciski
  poz. 0–7) są w pętli — przy zmianie HTML pilnuj nazw pól `S{bank}{kod}`, bo od nich zależy
  parsowanie żądania.
- **Serwer WWW (konwencje):** statyczny nagłówek+CSS jest w PROGMEM (`HTTP_HEAD`/`HTTP_HEAD2`)
  i wysyłany `sendP()` (chunki 64 B, `client.write`) — nie zamieniaj z powrotem na serie
  `print(F())`. Parsowanie żądania jest **bez `String`** — z bufora `reqBuf` (`S{bank}{kod}`:
  bank=`reqBuf[7]`, kod=`reqBuf[8..9]`; przy `WEB_ANT_NAMES` też `N{k}={nazwa}` od `reqBuf[6]`),
  z walidacją cyfr i `bankIdx 0..Ports-1`. Serwer czyta tylko pierwszą linię żądania i wtedy
  odpowiada (`if (c=='\n')`), potem `delay(1); client.stop()`. Nie przywracaj `String HTTP_req`.
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

## Funkcje opcjonalne (#ifdef, na górze src/main.ino)

- `WEB_ANT_NAMES` (WŁ.): nazwy anten 1–7 w RAM (`antRAM[9][ANT_MAXLEN+1]`, `ANT_MAXLEN=11`),
  ładowane z EEPROM (`loadAntNames()`), edytowane przez WWW (formularze `/?N{k}={nazwa}`,
  `parseAntName()` z dekodowaniem URL i twardym limitem długości), zapis `saveAntNames()`
  (EEPROM.update). Bez tej flagi nazwy wracają do PROGMEM (`antDefault[]`), `antName()` zwraca
  `__FlashStringHelper*`. **Nie zakładaj typu zwrotu `antName()`** — zależy od flagi (char* vs FSH),
  ale `client.print()` i `String =` obsługują oba.
- `BCD_INPUT` (WYŁ.): automatyczny wybór anteny z BCD radia. Wyłączony ⇒ brak `rx()`,
  `BCDmatrixOUT`, przełącznika Manual/BCD (WWW), pozycji „8", zakres enkodera 0..7.
- `PTT_BLOCKING` (WYŁ.): odczyt PTT (`port[i][2]`) + blokada `if(port[i][2]==0)` + plakietka PTT.
  Wyłączony ⇒ przełączanie bezwarunkowe, brak PTT na LCD/WWW. **Wykrywanie kolizji między TRX
  jest osobne i zostaje aktywne.**
- Przy edycji sekcji WWW/GPIO/show()/rx() pilnuj tych `#ifdef`-ów (są w wielu miejscach naraz).

## Architektura kodu (src/main.ino)

> Pełny opis architektury: [`docs/DESIGN.md`](docs/DESIGN.md). Pinout/sprzęt:
> [`docs/CONNECTIONS.md`](docs/CONNECTIONS.md).


- **Model stanu**: tablica `port[8][6]` — wiersze 0–3 = wejścia TRX1–4, 4–7 = wyjścia.
  Kolumny: `{adres_I2C, wybrana_antena, PTT, kolizja, tryb_ręczny, część(bank)}`.
- **Konfiguracja przez `#define`** na początku pliku: `Ports` (2 lub 4), `Inputs`,
  `inputHigh`, `SERBAUD`, `EthModule`, `__USE_DHCP__`, `OTRSP`/`OTRSP_DEBUG` oraz flagi funkcji
  `WEB_ANT_NAMES`, `BCD_INPUT`, `PTT_BLOCKING`, `ANT_MAXLEN` (patrz „Funkcje opcjonalne").
- **I²C / MCP23017**: `0x20`/`0x22` = wyjścia, `0x21`/`0x23` = wejścia (patrz
  `docs/CONNECTIONS.md`). Rejestry GPIOA=0x12, GPIOB=0x13.
- **Kluczowe funkcje**: `tx()` (one-hot na przekaźniki), `show()` (rysowanie linii LCD),
  `encI()`/`enc2()` (enkoder na przerwaniu). Opcjonalnie: `rx()` (BCD+PTT, tylko `BCD_INPUT`),
  `OTRSP_parse()` (SO2R, tylko `OTRSP`).
- **Nazwy anten**: domyślne w `antDefault[]` PROGMEM (indeks 0 = "OFF", 8 = "M-off->BCD" —
  nieedytowalne). Przy `WEB_ANT_NAMES` nazwy 1–7 są w `antRAM[]` (EEPROM), czytane przez
  `antName()`.

## Modyfikacje SQ9FK — o czym pamiętać

Zmiany są oznaczone komentarzami `//SQ9FK` w kodzie. Przy edycji zachowaj spójność między
wszystkimi miejscami dotyczącymi danej zmiany:

- **7. pozycja anteny** — dotyczy: `antDefault[]`, zakres enkodera (`enc2`), `tx()`
  (case 7 → `bit5`), interfejs WWW (przyciski „7"). Pozycja „8" (BCD) tylko przy `BCD_INPUT`.
- **GXP11 poz. 4/5** — współdzielą fizyczny port; `bit7` = przekaźnik pasma 40 m. Blokada
  kolizji 4↔5 jest w `loop()` **oraz** w `OTRSP_parse()` — zmieniać w obu miejscach.
- **PTT** — traktowanie PTT (odczyt + blokada) jest pod `#define PTT_BLOCKING` i domyślnie
  **wyłączone** (zmiana HW: gniazda PTT jako wyjścia). Nie włączaj bez potwierdzenia.

## Konwencje

- Język dokumentacji i komentarzy commitów: **polski** (użytkownik preferuje PL).
- Nowe zmiany firmware oznaczaj komentarzem `//SQ9FK` jak dotychczasowe.
- Nie commituj artefaktów budowania (`.pio/`, `*.hex`) — objęte `.gitignore`.
- Przy zmianie pinoutu/adresów I²C **zaktualizuj `docs/CONNECTIONS.md`**.

## Weryfikacja

- Kompilacja: `pio run` (bez ostrzeżeń z naszego kodu; ostrzeżenia z biblioteki `Ethernet2`
  są nieszkodliwe). Warto sprawdzić też build z `-DBCD_INPUT -DPTT_BLOCKING`, żeby te gałęzie
  `#ifdef` nie uległy rozjechaniu.
- **Wygląd interfejsu WWW** można podejrzeć bez sprzętu: `tools/websim.html` (symulator
  odtwarzający HTML/CSS firmware) lub `python tools/serve.py`. Przy zmianie HTML strony
  zaktualizuj też symulator.
- Brak realnego sprzętu w tym środowisku — testy funkcjonalne (EEPROM, W5500) wykonuje
  użytkownik na stacji.
