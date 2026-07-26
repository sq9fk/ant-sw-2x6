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
  WEB_ANT_NAMES WŁ.** — Flash **95,4%** / RAM **46,0%** (nowy wygląd WWW + buforowanie wyjścia).
  Build z wszystkim WŁ. (BCD+PTT+Ethernet): **99,4%** — mieści się (CSS odchudzony pod budżet).
  Przy dokładaniu do WWW pilnuj budżetu (odchudź CSS/markup). `OTRSP_parse()`/`serialEvent()` są pod
  `#if defined(OTRSP)`; włączenie OTRSP wymaga wyłączenia `EthModule`.
- **Optymalizacja rozmiaru — konwencje do zachowania:** `glyphs[6][8]` w `PROGMEM` (glify
  przez `memcpy_P`); `BCDmatrixOUT` w `PROGMEM` (`pgm_read_byte`, tylko przy `BCD_INPUT`);
  `port[8][6]` jest `byte`; nazwy anten patrz „Funkcje opcjonalne". Bloki WWW (przyciski
  poz. 0–7) są w pętli — przy zmianie HTML pilnuj nazw pól `S{bank}{kod}`, bo od nich zależy
  parsowanie żądania.
- **Serwer WWW (konwencje):** **całe wyjście idzie przez bufor `BufP out(client)`** (`out.print`/
  `out.println`, na końcu `out.done()`) — w Ethernet2 każde `write()` to osobny segment TCP z busy-waitem
  na `SEND_OK`, a `print(F())` leci znak-po-znaku; `BufP` zbiera w RAM i wysyła porcjami 128 B. **Nie
  wracaj do `client.print()` ani per-`send()`.** Statyczny nagłówek+CSS+ikona są w PROGMEM
  (`HTTP_HEAD`/`HTTP_HEAD2`/`POWER_SVG`) i też lecą przez `out.print((const __FlashStringHelper*)…)`.
  **Układ strony** (ciemny teal,
  wg [[project-rotator-wifi-bridge]]): topbar (`.tb` = nazwa stacji + kropka napięcia), karta
  **Anteny** (nagłówek `.ahead` + statusy sekcji `.astat`/`.st` 50/50, wiersze `.trx` z przyciskami
  i ikoną Flex `.flx`), karta **Opis anten** (`.leg`), karta **Settings** (`<details>`: nazwa stacji,
  nazwy anten, ukryte napięcie). **Flash prawie pełny — przy zmianach HTML/CSS pilnuj budżetu.**
  Parsowanie żądania jest **bez `String`** — z bufora `reqBuf` (`S{bank}{kod}`:
  bank=`reqBuf[7]`, kod=`reqBuf[8..9]`; `F{s}{0|1}` = Radio Flex; przy `WEB_ANT_NAMES` też
  `N{k}={nazwa}` (antena) i `NS={nazwa}` (nazwa stacji) od `reqBuf[6]`), z walidacją cyfr i
  `bankIdx 0..Ports-1`. Odczyt żądania jest **batchowany** (`client.read(reqBuf+…, avail)` +
  `memchr('\n')`), do końca pierwszej linii; potem odpowiedź, `out.done()`, `delay(1); client.stop()`.
  Nie wracaj do czytania po bajcie ani `String HTTP_req`.
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

- `WEB_ANT_NAMES` (WŁ.): nazwy anten 1–6 w RAM (`antRAM[9][ANT_MAXLEN+1]`, `ANT_MAXLEN=11`) oraz
  **nazwa stacji** w `siteRAM[…]` (topbar), ładowane z EEPROM (`loadAntNames()`), edytowane przez
  WWW w karcie **Settings** (formularze `/?N{k}={nazwa}` i `/?NS={nazwa}`, wspólny `parseName(q, dst)`
  z dekodowaniem URL i twardym limitem długości), zapis `saveAntNames()` (EEPROM.update). **Układ
  EEPROM**: magic `0xA5` @0 + nazwy anten 1–6; **osobny** magic `0x5B` @`SITE_MAG_OFF` (67) + nazwa
  stacji — dzięki temu wgranie na stary EEPROM zachowuje nazwy anten, a nazwa stacji startuje z
  domyślnej. **Radio Flex nie ma już konfigurowalnych nazw** (usunięte — same ikony power w wierszach
  TRX). Bez tej flagi nazwy wracają do PROGMEM (`antDefault[]`/`siteDefault`), `antName()`/`siteName()`
  zwracają `__FlashStringHelper*`. **Nie zakładaj typu zwrotu** — zależy od flagi (char* vs FSH),
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
  nieedytowalne). Przy `WEB_ANT_NAMES` nazwy 1–6 są w `antRAM[]` (EEPROM), czytane przez
  `antName()`. **Nazwa stacji** (topbar): `siteDefault` PROGMEM / `siteRAM[]` (EEPROM), przez `siteName()`.

## Modyfikacje SQ9FK — o czym pamiętać

Zmiany są oznaczone komentarzami `//SQ9FK` w kodzie. Przy edycji zachowaj spójność między
wszystkimi miejscami dotyczącymi danej zmiany:

- **6 anten (projekt pierwotny)** — one-hot `bit0..bit5` = anteny 1..6 w `tx()`. Zakres enkodera
  0..6 (`enc2`), przyciski WWW 1..6, walidacja `getVal<=6`. Pozycja „8" (BCD) tylko przy `BCD_INPUT`.
- **Radio Flex (`bit7` = GPA7/GPB7)** — dwa niezależne wyjścia (`flexState[2]`), nakładane na
  bajt one-hot w `tx()` niezależnie od anteny. Sterowanie WWW: `/?F{s}{0|1}` — **ikony power w
  wierszach TRX** (`<button class=flx>` + `POWER_SVG`, tylko `i<2`). **Bez konfigurowalnych nazw**
  (usunięte). Dawniej `bit7` = przekaźnik pasma GXP11 40 m — **blokada kolizji 4↔5 usunięta**
  (poz. 4/5 to niezależne anteny; ogólne wykrywanie kolizji zostaje). Przy edycji pilnuj spójności:
  `tx()`, parser WWW (`F`), przyciski-ikony w wierszach TRX.
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
