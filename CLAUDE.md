# CLAUDE.md

Wskazówki dla Claude Code przy pracy nad tym repozytorium.

## Czym jest ten projekt

Firmware sterownika przełącznika antenowego **6 anten × 2 TRX** dla stacji SP9PDF.
Bazuje na RemoteQTH 6×2 Antenna Controller (OK1HRA, rev 0.3), zmodyfikowany przez SQ9FK.
Cel: dwa transceivery bezkolizyjnie współdzielą zestaw anten. Sterowanie ręczne
(enkoder/LCD + interfejs WWW). Automatyka BCD z radia, blokowanie przez PTT i OTRSP po USB to
funkcje **opcjonalne** (`#ifdef`, patrz „Funkcje opcjonalne") — domyślnie **wyłączone**.
**OTRSP po TCP jest domyślnie WŁĄCZONE** razem ze stroną WWW (np. do N1MM+, port 4534) —
to obecny build domyślny, patrz „Budżet pamięci" niżej. Wykrywanie kolizji między TRX działa
zawsze.

## Platforma i budowanie

- MCU: **Arduino Nano (ATmega328P)**, framework Arduino.
- Build: **PlatformIO** — `pio run` (env `nanoatmega328` = stary bootloader,
  `nanoatmega328new` = nowy). Wgranie: `pio run -t upload`. Monitor: 9600 8N1.
  `LiquidCrystal` i `Ethernet` (`arduino-libraries/Ethernet`, oficjalna — **NIE** `adafruit/
  Ethernet2`, przestarzała/nieutrzymywana, zastąpiona 2026-07-29) są w `lib_deps` (NIE w rdzeniu
  PIO); `Wire`/`SPI` z rdzenia.
- **Budżet pamięci Nano (30 KB flash / 2 KB RAM):** BCD_INPUT+PTT_BLOCKING razem z EthModule
  już się NIE mieszczą (te opcje tylko bez `EthModule`). **Domyślny build to `EthModule`+
  `OTRSP_TCP`** (strona WWW + OTRSP po TCP, np. do N1MM+, port 4534), **BCD/PTT WYŁ.,
  WWW_EEPROM_NAMES WŁ.** — Flash **97,9%** (30082 B) / RAM **55,0%** (1127 B), **zapas ~638 B**
  (poprawilo sie znaczaco po usunieciu `String` z `show()` przy przegladzie bugow 2026-07-29 -
  wczesniej ~90 B; net -84 B po 2026-08-02: wysrodkowanie LCD + JS wstrzymujacy auto-odswiezanie
  + "nie zwijaj Settings po zapisie" kosztowaly razem +186 B, ale awaryjna edycja sieci po
  Serial odzyskala wiecej dzieki wspoldzielonej `setNetField()` (patrz nizej) - RAM wzrosl
  o +66 B, bo `in_buf[64]` jest teraz kompilowany takze bez `OTRSP`). Jest domyslny i ma DRUGI
  najmniejszy zapas z szesciu wariantow (najciasniejszy to `EthModule`+`OTRSP`+`OTRSP_TCP`
  naraz, ~552 B, patrz nizej) — KAZDA zmiana we wspoldzielonym
  kodzie (WWW HTML/CSS, OTRSP_parse(), siec) MUSI byc zbudowana i zmierzona na OBU tych
  wariantach NAJPIERW.
  Przy dokladaniu do WWW/OTRSP_TCP pilnuj budzetu (odchudz CSS/markup, generuj w petli zamiast
  rozwijac kod, wydziel powtarzajace sie fragmenty PROGMEM do wspolnych stalych - patrz `nmOpen`/
  `nmMid`/`nmLen11`/`nmClose` nizej).
  **Oficjalna biblioteka Ethernet jest większa niż Ethernet2** (auto-detekcja W5100/W5200/W5500
  w runtime, niewyłączalna `#define`m — zawsze skompilowana). **DHCP usuniete z projektu**
  (2026-07-29) — kosztowalo ~3,8 KB (`Dhcp.cpp`+`EthernetUdp.cpp`), przydatne bylo tylko
  w wariantach bez strony WWW; nie warto bylo trzymac calej sciezki kodu (`__USE_DHCP__`,
  retry-loop w `setup()`, `Ethernet.maintain()` w `loop()`) dla tego jednego przypadku. IP jest
  zawsze statyczne (`ip`/`gateway`/`subnet` w kodzie, edytowalne tez przez WWW+EEPROM przy
  `WWW_EEPROM_NAMES`). Sama strona WWW (HTML/CSS/`BufP`/print-y) kosztuje dodatkowo ~8 KB — nie
  sam Ethernet.
  **`OTRSP` (USB) i `OTRSP_TCP` (gniazdo TCP) SA NIEZALEZNE** — kazdy moze byc wlaczony osobno
  albo razem (wspolny `OTRSP_parse(char*, Print&)` kompiluje sie przy `#if defined(OTRSP) ||
  defined(OTRSP_TCP)`). `serialEvent()`/`in_buf` (bufor linii CR-terminowanej) od 2026-08-02
  NIE sa juz specyficzne dla `OTRSP` - kompiluja sie tez przy `WWW_EEPROM_NAMES &&
  (EthModule||OTRSP_TCP)` (bez wzgledu na `OTRSP`), bo obsluguja TAKZE awaryjna edycje sieci po
  Serial (patrz nizej). Serial i EthernetClient dziedzicza po `Print` — nie duplikuj logiki
  komend przy zmianach.
  **Szesc wariantow** (od 2026-08-01 ZADNA kombinacja nie jest juz blokowana — dawny `#error`
  dla `EthModule`+`OTRSP`+`OTRSP_TCP` naraz (brakowalo ~116 B) zostal USUNIETY po przegladzie
  bugow 2026-07-29, bo odzyskane ~660 B z `String` sprawilo, ze ta kombinacja faktycznie sie
  miesci): (1) `EthModule`+`OTRSP_TCP` — strona WWW + OTRSP po TCP, **DOMYSLNY**, **97,9%**
  (30082 B) **— zapas ~638 B**; (2) `EthModule` — strona WWW bez OTRSP, 94,9% (29146 B, zapas
  ~1,5 KB, bezpieczniejszy jesli OTRSP niepotrzebne); (3) `EthModule`+`OTRSP` — strona WWW +
  OTRSP po USB, **96,3%** (29570 B, zapas ~1,1 KB); (4) `EthModule`+`OTRSP`+`OTRSP_TCP` — strona
  WWW + OTRSP po USB **i** TCP jednoczesnie, **98,2%** (30168 B, zapas ~552 B) —
  NAJCIASNIEJSZY ze wszystkich szesciu, buduj i mierz go PRZED merge kazdej zmiany we
  wspoldzielonym kodzie (WWW/OTRSP_parse/siec), obok wariantu (1); (5) `OTRSP` — OTRSP tylko po
  USB, bez Ethernetu, 33,7% (10356 B, bez konfiguracji sieciowej - `EthModule`/`OTRSP_TCP` oba
  wyl., wiec bez komend sieciowych po Serial); (6) `OTRSP`+`OTRSP_TCP` — OTRSP po USB **i**
  surowym TCP (`OTRSP_TCP_PORT`, domyslnie 4534) jednoczesnie, bez strony WWW, **69,8%**
  (21436 B).
  Uwaga: `DNSClient::getHostByName`/wirtualna metoda `connect(hostname)` w `EthernetClient`
  (~1,2 KB martwego kodu, bo klasa polimorficzna linkuje cala tabele wirtualna) to koszt JUZ
  OBECNY w kazdym buildzie z `EthernetClient` (nawet w najlzejszym WWW-bez-OTRSP) — nie jest to
  specyficzne dla `OTRSP_TCP`. Etykiety pol w Settings (`.nm b`) maja `flex:0 0 4.3rem` (nie
  `min-width`) - stala szerokosc, zeby input zawsze zaczynal sie w tym samym miejscu
  niezaleznie od dlugosci etykiety ("Gateway" vs "IP").
  **LCD splash (2026-08-02):** nazwa stacji byla drukowana od kolumny 0 (z boku dla krotszych
  nazw) - dodano `siteNameLen()` (strlen/strlen_P zaleznie od `WWW_EEPROM_NAMES`) i wyliczenie
  kolumny startowej `(LCDculumn - dlugosc) / 2`, zeby zawsze byla wysrodkowana na 16-znakowej
  linii.
  **Auto-odswiezanie strony (2026-08-02):** dawny `<meta http-equiv="refresh" content="10;...">`
  przeladowywal strone BEZWARUNKOWO co 10 s - zwijal rozwiniete Settings (`<details>`, stan tylko
  w DOM, NIE zapisany po stronie urzadzenia) w trakcie edycji, zanim uzytkownik zdazyl zapisac.
  Zastapione inline `<script>` (funkcja `r()`): przed przeladowaniem sprawdza
  `document.querySelector('[open]')`, jesli cokolwiek jest rozwiniete - odracza sprawdzenie o 2 s
  (petla `setTimeout`), w przeciwnym razie `location.href` jak wczesniej. Pierwszy JS w tym
  projekcie (reszta strony to czysty HTML/CSS + GET) - uzyty wylacznie bo natywny `<details>` nie
  ma zadnego innego mechanizmu przetrwania przeladowania strony.
  **Settings zostaje rozwiniete po zapisie pola (2026-08-02):** kazde pole (nazwa/antena/siec) ma
  WLASNY `<form>`/submit (parser czyta tylko jeden parametr na zadanie), wiec zapisanie calej
  konfiguracji sieciowej to do 4 klikniec OK z rzedu - a strona domyslnie renderuje `<details>`
  zwiniete, wiec trzeba bylo za kazdym razem klikac "Settings" ponownie. Dodano `settingsOpen`
  (lokalna zmienna w handlerze WWW, USTAWIANA w kazdej galezi `N1..N6`/`NS`/`N{IGMD}`) - jesli
  `true`, `<details>` renderuje sie z atrybutem `open`. W polaczeniu z auto-odswiezaniem wyzej
  (wstrzymuje sie przy otwartym `[open]`) mozna zapisac IP/gateway/maske/DNS po kolei bez
  ponownego rozwijania karty i bez presji czasu.
  **Awaryjna edycja sieci przez Serial (2026-08-02):** IP/gateway/maska/DNS byly edytowalne
  TYLKO przez WWW - jesli siec byla zle skonfigurowana, strona byla nieosiagalna i nie dalo sie
  jej poprawic. Dodano komendy `N{I|G|M|D}={a.b.c.d}\r` po USB (dokladnie ten sam format co WWW
  `/?N{kod}={wartosc}`, tylko bez `/?`), odpowiedz `OK`/`ERR`. Reuzywa `parseIPField()`/
  `netCfg[]`/`saveNetConfig()` z handlera WWW przez nowa wspolna funkcje `setNetField(char code,
  const char* val)` (deklarowana obok `parseIPField()`) - handler WWW tez ja teraz woła zamiast
  wlasnej petli, wiec refaktoryzacja ODZYSKAlA flash zamiast go kosztowac netto. `in_buf[64]`/
  `serialEvent()`/`stringComplete` (dawniej `#if defined(OTRSP)`) maja teraz POSZERZONY guard:
  `#if defined(OTRSP) || (defined(WWW_EEPROM_NAMES) && (defined(EthModule) || defined(OTRSP_TCP)))`
  - dzialaja WIEC TEZ na wariantach (1) i (2), ktore wczesniej nie mialy zadnego czytania Serial.
  Dispatch w `loop()`: jesli `in_buf` pasuje do wzorca `N{IGMD}=`, obsluz i NIE wolaj
  `OTRSP_parse()` (uzyj realnego `if/else` sterowanego `#if`, NIE runtime-bool zostawionego bez
  uzycia w wariantach bez `OTRSP` - inaczej `-Wunused-but-set-variable`, sprawdz `pio run` bez
  ostrzezen po kazdej zmianie w tym bloku). Brak kolizji z protokolem OTRSP (komendy SET to
  `AUX1`/`AUX2`, bez litery `N`; `?NAME` to QUERY z wiodacym `?`, sprawdzane przez `cmd+1`).
- **Watchdog (`<avr/wdt.h>`, zawsze wlaczony, wszystkie warianty):** `MCUSR=0; wdt_disable();`
  na SAMYM POCZATKU `setup()` (niektore bootloadery zostawiaja WDT wlaczony z krotkim timeoutem
  po poprzednim resecie - bez tego byla by petla resetow). `wdt_enable(WDTO_8S)` na SAMYM KONCU
  `setup()` - MUSI byc PO wszystkich `delay()` przy starcie (splash+napiecie+IP, lacznie do ~10 s
  w wariancie EthModule/OTRSP_TCP) - WDTO_8S to najdluzszy pojedynczy timeout AVR, wlaczenie go
  wczesniej zresetowaloby urzadzenie w trakcie rozruchu. `wdt_reset()` na samym poczatku `loop()`
  - jesli petla sie zawiesi (WWW/TCP/USB/cokolwiek), reset po ~8 s. Koszt: ~46 B flash.
- **Przycisk Restart w Settings (`#if defined(EthModule)`):** `/?R1` ustawia `pendingRestart`
  (flaga globalna), restart wykonuje sie DOPIERO na POCZATKU NASTEPNEJ iteracji `loop()` (po
  `wdt_reset()`) - `wdt_enable(WDTO_15MS); while(1){}` - zeby klient najpierw dostal PELNA
  odpowiedz (strona renderuje sie normalnie, `out.done()`, potem dopiero restart). Skrocony
  timeout WDT to jedyny niezawodny sposob softwarowego resetu na AVR (czysci tez peryferia, nie
  tylko licznik rozkazow - NIE uzywaj skoku na adres 0). Koszt z HTML przycisku: ~206 B (patrz
  nizej - wiekszosc kosztu to sam string).
  **Napiecie usuniete z Settings** (bylo tylko do wgladu, nie wplywalo na nic poza wyswietlaniem)
  - ostrzezenie (czerwona kropka w topbarze, `vv` w generowaniu strony) zostaje, bo jest tanie
    i uzywane gdzie indziej.
  **Wspolne stale PROGMEM dla pol Settings** (`nmOpen`/`nmMid`/`nmLen11`/`nmClose`, deklarowane
  przy `POWER_SVG`) - pole nazwy stacji, petla nazw anten (1-6), petla sieci (IP/gateway/maska/
  DNS) i przycisk Restart mialy NIEMAL IDENTYCZNY HTML zapisany jako osobne literaly `F()` (3-4x
  zdublowany `<div class="nm"><b>...`/`</b><form method="get" style="display:inline">...` itd.) -
  wydzielenie do wspolnych stalych odzyskalo ~440 B. **Jesli dodajesz nowe pole do Settings,
  UZYWAJ tych stalych zamiast pisac nowy literal `F()` z tym samym HTML.**
- **Konfiguracja sieciowa edytowalna przez WWW** (`netCfg[4]` = wskaźniki na `ip/gateway/subnet/
  myDns`, `loadNetConfig()`/`saveNetConfig()`, `parseIPField()` używa `IPAddress::fromString` —
  **nie pisz własnego parsera dotted-decimal**, biblioteka go ma). IP nie są już „na sztywno" —
  patrz §8 EEPROM w `docs/DESIGN.md`. Ładowane też dla `OTRSP_TCP` (bez formularza edycji).
- **Optymalizacja rozmiaru — konwencje do zachowania:** `glyphs[6][8]` w `PROGMEM` (glify
  przez `memcpy_P`); `BCDmatrixOUT` w `PROGMEM` (`pgm_read_byte`, tylko przy `BCD_INPUT`);
  `port[8][6]` jest `byte`; nazwy anten patrz „Funkcje opcjonalne". Bloki WWW (przyciski
  poz. 0–7) są w pętli — przy zmianie HTML pilnuj nazw pól `S{bank}{kod}`, bo od nich zależy
  parsowanie żądania.
- **Serwer WWW (konwencje):** **całe wyjście idzie przez bufor `BufP out(client)`** (`out.print`/
  `out.println`, na końcu `out.done()`) — w bibliotekach Wiznet (sprawdzone w Ethernet2 i oficjalnej
  `arduino-libraries/Ethernet`) każde `write()` to osobny segment TCP z busy-waitem na `SEND_OK`,
  a `print(F())` leci znak-po-znaku; `BufP` zbiera w RAM i wysyła porcjami 128 B. **Nie
  wracaj do `client.print()` ani per-`send()`.** Statyczny nagłówek+CSS+ikona są w PROGMEM
  (`HTTP_HEAD`/`HTTP_HEAD2`/`POWER_SVG`) i też lecą przez `out.print((const __FlashStringHelper*)…)`.
  **Układ strony** (ciemny teal,
  wg konwencji projektu [rotator_wifi_bridge](https://github.com/sq9fk/rotator_wifi_bridge)): topbar (`.tb` = nazwa stacji + kropka napięcia), karta
  **Anteny** (nagłówek `.ahead` + statusy sekcji `.astat`/`.st` 50/50, wiersze `.trx` z przyciskami
  i ikoną Flex `.flx`), karta **Opis anten** (`.leg`), karta **Settings** (`<details>`: nazwa
  stacji, nazwy anten, sieć, przycisk Restart — **bez** napięcia, usunięte). **Flash prawie
  pełny — przy zmianach HTML/CSS pilnuj budżetu.**
  Parsowanie żądania jest **bez `String`** — z bufora `reqBuf` (`S{bank}{kod}`:
  bank=`reqBuf[7]`, kod=`reqBuf[8..9]`; `F{s}{0|1}` = Radio Flex; `R1` = Restart (patrz
  „Budżet pamięci" wyżej); `J` = odczyt stanu dla [rotator_wifi_bridge](https://github.com/sq9fk/rotator_wifi_bridge)
  (`A=ant1,ant2`, reużywa `HTTP_HEAD`, +68 B); `K` = nazwy anten + nazwa stacji dla tego samego
  mostu (`K=` + 6 nazw + nazwa stacji, przez przecinek, łącznie +88 B — **zapas na domyślnym
  wariancie to już tylko ~90 B**, patrz `docs/DESIGN.md` §7/§9 przed kolejną zmianą we
  współdzielonym kodzie WWW); przy `WWW_EEPROM_NAMES` też
  `N{k}={nazwa}` (antena), `NS={nazwa}` (nazwa stacji), `N{I|G|M|D}={a.b.c.d}` (IP/gateway/maska/
  DNS) od `reqBuf[6]`), z walidacją cyfr i
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

- `WWW_EEPROM_NAMES` (WŁ.): nazwy anten 1–6 w RAM (`antRAM[8][ANT_MAXLEN+1]`, `ANT_MAXLEN=11`) oraz
  **nazwa stacji** w `siteRAM[…]` (topbar), ładowane z EEPROM (`loadAntNames()`), edytowane przez
  WWW w karcie **Settings** (formularze `/?N{k}={nazwa}` i `/?NS={nazwa}`, wspólny `parseName(q, dst)`
  z dekodowaniem URL i twardym limitem długości), zapis `saveAntNames()` (EEPROM.update). Przy
  `EthModule`/`OTRSP_TCP`: **konfiguracja sieciowa** (`ip/gateway/subnet/myDns`) też w EEPROM —
  `loadNetConfig()`/`saveNetConfig()` (osobne funkcje, zadeklarowane PO tych zmiennych — nie
  przenoś do `loadAntNames()`, bo `IPAddress` tam jeszcze nie istnieje), edycja `/?N{I|G|M|D}=`
  przez WWW **i** (od 2026-08-02) `N{I|G|M|D}=...\r` przez Serial — oba wołają wspólne
  `setNetField(char code, const char* val)` (`parseIPField()` → `IPAddress::fromString`).
  **Układ EEPROM**: magic `0xA5` @0 + nazwy anten
  1–6; **osobny** magic `0x5B` @`SITE_MAG_OFF` (67) + nazwa stacji; **osobny** magic `0x5C`
  @`NET_MAG_OFF` (79) + 16 B (ip/gateway/maska/dns) — każda sekcja ma własny magic, więc wgranie
  na stary EEPROM zachowuje to, co już tam było, a nowe sekcje startują z domyślnych. **Radio
  Flex nie ma już konfigurowalnych nazw** (usunięte — same ikony power w wierszach TRX). Bez tej
  flagi nazwy wracają do PROGMEM (`antDefault[]`/`siteDefault`), `antName()`/`siteName()`
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
  `inputHigh`, `SERBAUD`, `EthModule`, `OTRSP`/`OTRSP_DEBUG`/`OTRSP_TCP`/
  `OTRSP_TCP_PORT` oraz flagi funkcji `WWW_EEPROM_NAMES`, `BCD_INPUT`, `PTT_BLOCKING`, `ANT_MAXLEN`
  (patrz „Funkcje opcjonalne").
- **I²C / MCP23017**: `0x20`/`0x22` = wyjścia, `0x21`/`0x23` = wejścia (patrz
  `docs/CONNECTIONS.md`). Rejestry GPIOA=0x12, GPIOB=0x13.
- **Kluczowe funkcje**: `tx()` (one-hot na przekaźniki), `show()` (rysowanie linii LCD),
  `encI()`/`enc2()` (enkoder na przerwaniu). Opcjonalnie: `rx()` (BCD+PTT, tylko `BCD_INPUT`),
  `OTRSP_parse(char *cmd, Print &out)` (SO2R, przy `OTRSP` LUB `OTRSP_TCP`) — wywoływany z dwóch
  niezależnych miejsc: `loop()` dla USB (`OTRSP_parse(in_buf, Serial)`, tylko gdy `in_buf` NIE
  pasuje do wzorca `N{IGMD}=` — patrz „Awaryjna edycja sieci przez Serial" wyżej) — bufor/
  terminator w `serialEvent()`, od 2026-08-02 kompilowane przy `OTRSP` LUB gdy istnieje
  konfiguracja sieciowa (`WWW_EEPROM_NAMES && (EthModule||OTRSP_TCP)`), NIE tylko przy `OTRSP`
  i — przy `OTRSP_TCP` — z bloku „OTRSP TCP" w `loop()` (`OTRSP_parse(otrsp_tcp_buf, otrspClient)`,
  własny bufor `otrsp_tcp_buf`/`otrsp_tcp_len`). Połączenie TCP jest **trwałe** (klient trzymany
  w `otrspClient` między iteracjami `loop()`), inaczej niż serwer WWW (request/response,
  łączy-odpowiada-zamyka) — nie kopiuj wzorca WWW przy zmianach w tym bloku.
- **Nazwy anten**: domyślne w `antDefault[]` PROGMEM. Indeks 0="OFF" = **wybieralny zawsze** (antena
  odłączona, przycisk „-"/enkoder 0). Indeks 7 = "M-off->BCD" (**sentinel trybu BCD**), osiągalny
  tylko przy `BCD_INPUT` (enkoder 0..7) — **pod `#ifdef BCD_INPUT`** (def., `antDefault[]`, `antRAM[7]`);
  bez tej flagi nie istnieje, nie zajmuje flash. `antRAM` ma rozmiar `[8]` (0..7). Sentinel przesunięty
  z 8 na 7 → **zlikwidowana martwa luka** (dawna poz. 7). Przy `WWW_EEPROM_NAMES` nazwy 1–6 są w `antRAM[]` (EEPROM), czytane przez
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
  `tx()`, parser WWW (`F`), przyciski-ikony w wierszach TRX. **`flexState[]` jest też w `/?J`**
  (2026-08-03, jako 3./4. pole po stanie anten) — rotator_wifi_bridge ma teraz własny przycisk
  PWR per TRX i musi znać prawdziwy stan, nie tylko ostatnio wysłany; +36 B na wariancie
  domyślnym, zmierzone też na najciaśniejszym (`EthModule`+`OTRSP`+`OTRSP_TCP`), patrz
  `docs/DESIGN.md` §9. Przy zmianie formatu `/?J` zaktualizuj też `tools/serve.py`.
- **PTT** — traktowanie PTT (odczyt + blokada) jest pod `#define PTT_BLOCKING` i domyślnie
  **wyłączone** (zmiana HW: gniazda PTT jako wyjścia). Nie włączaj bez potwierdzenia.

## Konwencje

- Język dokumentacji i komentarzy commitów: **polski** (użytkownik preferuje PL).
- Nowe zmiany firmware oznaczaj komentarzem `//SQ9FK` jak dotychczasowe.
- Nie commituj artefaktów budowania (`.pio/`, `*.hex`) — objęte `.gitignore`.
- Przy zmianie pinoutu/adresów I²C **zaktualizuj `docs/CONNECTIONS.md`**.

## Weryfikacja

- Kompilacja: `pio run` (bez ostrzeżeń z naszego kodu; ostrzeżenia z biblioteki `Ethernet`
  są nieszkodliwe). Warto sprawdzić też build z `-DBCD_INPUT -DPTT_BLOCKING`, żeby te gałęzie
  `#ifdef` nie uległy rozjechaniu. **Sześć wariantów do zbudowania** przy zmianach w Ethernet/OTRSP
  — patrz „Budżet pamięci" wyżej i `docs/DESIGN.md` §11. Wariant `EthModule`+`OTRSP_TCP` to
  **domyślny build** i ma zapas ~638 B — buduj go PIERWSZY przy każdej zmianie, bo to on trafi do
  każdego, kto skompiluje projekt bez modyfikacji `#define`; drugi w kolejności to
  `EthModule`+`OTRSP`+`OTRSP_TCP` (najciaśniejszy ze wszystkich, zapas ~552 B).
- **Wygląd interfejsu WWW i protokół OTRSP** można podejrzeć bez sprzętu: `tools/websim.html`
  (odtwarza HTML/CSS firmware + symuluje `OTRSP_parse()` — dwa niezależne monitory USB/TCP,
  TX/RX kolorowane) lub `python tools/serve.py`. Przy zmianie HTML strony **lub** logiki
  `OTRSP_parse()` zaktualizuj też symulator (`DEVICE_CSS`/`buildDeviceHTML()` dla WWW,
  `otrspRespond()` dla OTRSP — to reczny odpowiednik firmware'owego parsera, **nie** wolno
  pozwolic mu sie rozjechac).
  **Prawdziwe polaczenia** (wymagaja `python tools/serve.py`, nie `file://`): USB przez
  Web Serial API (`navigator.serial`, Chrome/Edge) - przeglądarka rozmawia bezposrednio z
  realnym portem COM; TCP przez most WebSocket<->TCP w `tools/serve.py` (`OtrspBridge`,
  `WSConnection` - minimalny serwer WS z stdlib, bez zewnetrznych zaleznosci) - prawdziwy
  N1MM+ moze polaczyc sie z `127.0.0.1:4534` (jak `OTRSP_TCP_PORT`), a most jest "glupa rura"
  bajtow do przegladarki - cala logika protokolu zostaje w `otrspRespond()` (JS), zeby nie
  duplikowac jej w Pythonie. Przy zmianie protokolu w `otrspRespond()` most w Pythonie
  **nie wymaga zmian** (nie zna semantyki OTRSP, tylko przekazuje bajty).
- Brak realnego sprzętu w tym środowisku — testy funkcjonalne (EEPROM, W5500) wykonuje
  użytkownik na stacji.
