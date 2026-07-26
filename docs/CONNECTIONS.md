# Analiza połączeń — SP9PDF RemoteQTH 6×2 Antenna Switch

> **Dwa niezależne źródła, w pełni zgodne:**
> 1. Firmware [`src/main.ino`](../src/main.ino) — pinout MCU i logika I²C.
> 2. Projekt KiCad rev 03 ([`hw/`](../hw/), © OK1HRA, CC BY-SA 4.0) — netlista
>    `ant-sw-control.net` (128 komponentów, 149 sieci), z której wyprowadzono stronę
>    sprzętową. Pozycje oznaczone **✓ netlista** są potwierdzone bezpośrednio połączeniami PCB.

Płyta rev 03 to wersja **2-portowa (6 anten × 2 TRX)**: 1× Arduino Nano, 2× MCP23017,
2× ULN/UDN (drivery), opcjonalny W5500 (Ethernet), LM2576 (zasilanie).

> **⚠️ Sprzęt vs firmware.** Ten dokument opisuje **możliwości sprzętu** (płyta OK1HRA rev 03).
> Domyślna konfiguracja firmware SQ9FK **nie używa** części z nich:
> - **Ethernet WŁ.** (interfejs WWW), **OTRSP WYŁ.** (wyklucza się rozmiarowo z Ethernetem);
> - wejścia **BCD** (`BCD_INPUT`) i **odczyt/blokada PTT** (`PTT_BLOCKING`) — **wyłączone**;
> - na tej stacji gniazda **PTT przerobiono sprzętowo na wyjścia** (nie służą jako wejścia PTT).
>
> Sekcje 4 (BCD/PTT) i 6 (OTRSP) opisują więc funkcje **opcjonalne**. Wyjścia anten (sekcja 3),
> I²C, LCD, enkoder, zasilanie i wykrywanie kolizji działają zawsze.

## 1. Mikrokontroler — Arduino Nano (ATmega328P) `U8`

Nazwy sieci w schemacie odpowiadają 1:1 definicjom w firmware — **✓ netlista**:

| Pin Nano | Sieć (KiCad) | Funkcja | Firmware |
|----------|--------------|---------|----------|
| A0  | `/RS`     | LCD RS → `U9.4` | `lcd(A0,...)` |
| A1  | `/E`      | LCD E → `U9.6` | `lcd(A0,A1,...)` |
| D7  | `/D4`     | LCD D4 → `U9.11` | `lcd(...,7,...)` |
| D6  | `/D5`     | LCD D5 → `U9.12` | `lcd(...,6,...)` |
| D5  | `/D6`     | LCD D6 → `U9.13` | `lcd(...,5,...)` |
| D4  | `/D7`     | LCD D7 → `U9.14` | `lcd(...,4)` |
| D2  | `/ENC-A`  | Enkoder A (RC: R5/C2) | `enc0PinA = 2` |
| D3  | `/ENC-B`  | Enkoder B (RC: R8/C6), INT1 | `enc0PinB = 3` |
| D9  | `/SW`     | Przycisk `S1` | `sw = 9` |
| D8  | `/SW-LED` | LED przycisku (R36) | `swLED = 8` |
| A3  | `/12V`    | Pomiar napięcia (dzielnik R3/R4) | `analogRead(A3)` |
| A4  | `/SDA`    | I²C → `U5.13`, `U6.13` (pull-up R26) | `Wire` |
| A5  | `/SCL`    | I²C → `U5.12`, `U6.12` (pull-up R25) | `Wire` |
| D0/D1 | `/RX` `/TX` | Serial 9600 — OTRSP/SO2R (opcja, dom. wył.) | `#define OTRSP` |
| D10–D13 | `Net-(U1-*)` | SPI → W5500 (`U1`) — dom. **wł.** | `#define EthModule` |
| 3V3 | `+3V3`    | zasilanie W5500 | — |
| A2, A6, A7, AREF | — | niepodłączone | — |

Pomiar napięcia: `volt = analogRead(A3)*5/1024*7.25 + 0.4`. Dzielnik R3/R4 od szyny `/12V`,
ostrzeżenia LCD przy `< 10 V` / `> 15 V` (nominał 13,8 V).

## 2. Ekspandery I²C MCP23017

| Ozn. | Adres | Rola | GPIOA | GPIOB |
|------|-------|------|-------|-------|
| `U5` | `0x20` | **OUT** | TRX1 → ULN `U7` | TRX2 → ULN `U4` |
| `U6` | `0x21` | **IN**  | BCD (P4=TRX1, P3=TRX2) | PTT (`GPB0/GPB1`) |

> Rev 03 ma fizycznie **2 ekspandery** (`0x20`+`0x21`) — to wersja 2-TRX. Adresy `0x22`/`0x23`
> z firmware dotyczą rozbudowy do 4 TRX. Adres ustawiają zworki na A0/A1/A2. Rejestry:
> `IODIRA=0x00`, `IODIRB=0x01`, `GPIOA=0x12`, `GPIOB=0x13`.

## 3. Wyjścia anten — ✓ netlista

`U5` (OUT) steruje wejściami dwóch driverów ULN/UDN, których wyjścia (open-collector, 12 V)
trafiają przez ferryty na gniazda **RJ45**:

| Sygnał logiczny | U5 pin | Driver | Wyjście |
|-----------------|--------|--------|---------|
| TRX1 anteny 1–6 | `GPA0..GPA5` | `U7` (ULN) | **RJ45 `J2`** (przez L29/L31/L35/L39/L43/L47) |
| TRX2 anteny 1–6 | `GPB0..GPB5` | `U4` (ULN) | **RJ45 `J1`** (przez L5/L7/L11/L15/L19/L23) |
| **Radio Flex 1** | `GPA7` → R2 → `Q1` | przekaźnik `K1` | styki na **`J7`** |
| **Radio Flex 2** | `GPB7` → R1 → `Q2` | przekaźnik `K2` | styki na **`J6`** |
| (nieużywane)    | `GPA6`, `GPB6` | — | — (firmware `bit6` wolny) |

Kodowanie one-hot z funkcji `tx()` (numer anteny → bit portu) — **projekt 6-antenowy**:

| Poz. | Antena | Bit | Uwaga |
|------|--------|-----|-------|
| 1 | 160m INV-V   | `bit0` | |
| 2 | 80m Dipole   | `bit1` | |
| 3 | Delta 80/40  | `bit2` | |
| 4 | (nazwa WWW)  | `bit3` | niezależna antena |
| 5 | (nazwa WWW)  | `bit4` | niezależna antena |
| 6 | UB50         | `bit5` | |
| 0 | OFF          | `0x00` | |

> **SQ9FK (zmiana):** `bit7` (GPA7/GPB7) **nie należy już do anteny** — steruje dwoma
> **niezależnymi wyjściami Radio Flex** (`flexState[]`), przełączanymi osobnymi przyciskami WWW,
> niezależnie od wyboru anteny. Dawniej `bit7` = przekaźnik pasma GXP11 40 m sprzężony z poz. 4/5.
> Poz. 4 i 5 to teraz **niezależne anteny** (`bit3`/`bit4`), więc blokada kolizji 4↔5 została
> usunięta — pozostaje ogólne wykrywanie kolizji (ta sama antena na obu TRX).

## 4. Wejścia BCD + PTT — ✓ netlista

> **Funkcja opcjonalna — domyślnie nieaktywna.** Poniższa ścieżka wejściowa jest obsługiwana
> przez firmware tylko przy `#define BCD_INPUT` (odczyt BCD) i `#define PTT_BLOCKING` (PTT) —
> oba domyślnie **wyłączone**. `U6` (IN) nie jest wtedy czytany (`rx()` nie jest kompilowany),
> a `port[i][1]` ustawia się wyłącznie ręcznie (WWW/enkoder). Na tej stacji gniazda PTT są
> ponadto przerobione na **wyjścia** (zmiana HW poza tą netlistą).

Dane pasma z radia wchodzą na złącza **P3/P4** (ferryt + R9–R16 + RC), do `U6` (IN):

| Złącze | Piny | → U6 | Rola |
|--------|------|------|------|
| **`P4`** | 2–5 | `GPA0..GPA3` (dolny nibble) | **TRX1** BCD → `BCDmatrixOUT[0][]` |
| **`P3`** | 2–5 | `GPA4..GPA7` (górny nibble) | **TRX2** BCD → `BCDmatrixOUT[1][]` |
| `P3/P4` | 6 | `+5V` (L57) | zasilanie |
| — | `GPB1` (D3/R34) | PTT **TRX1** | blokada podczas TX |
| — | `GPB0` (D4/R35) | PTT **TRX2** | blokada podczas TX |

Poziom aktywny wejść: **HIGH** (`#define inputHigh`). `GPB2..GPB7` na masie (nieużywane).
Tablica `BCDmatrixOUT[2][16]` mapuje 16 kodów pasma → numer anteny:
```
{ 0, 1, 2, 3, 4, 5, 4, 5, 4, 5, 6, 3, 3, 3, 3, 3 }
```

## 5. Złącza i zasilanie — ✓ netlista

| Ozn. | Typ | Funkcja |
|------|-----|---------|
| `J5` | DC jack | wejście zasilania ~12 V (→ LM2576 `U10` → +5 V; warystor V1, bezp. F1) |
| `J1` | RJ45 | wyjście sterowania anten **TRX2** (6 linii + `+12V` pin8 + GND) |
| `J2` | RJ45 | wyjście sterowania anten **TRX1** (6 linii + `+12V` pin8 + GND) |
| `P4` | header | wejście BCD **TRX1** (4 bity + 5 V + GND) |
| `P3` | header | wejście BCD **TRX2** (4 bity + 5 V + GND) |
| `J7` | 2-pin | styki przekaźnika `K1` — pasmo GXP11 **TRX1** |
| `J6` | 2-pin | styki przekaźnika `K2` — pasmo GXP11 **TRX2** |
| `P1`, `P2` | header | rozbudowa I²C (SDA/SCL/5V/GND) — kolejne porty |
| `J1/J2` `U1` | RJ45 / moduł | opcjonalny Ethernet W5500 (SPI, `#define EthModule`) |

## 6. Sterowanie z komputera — OTRSP (SO2R)

> **Funkcja opcjonalna — domyślnie nieaktywna** (`#define OTRSP`). Wyklucza się rozmiarowo
> z Ethernetem (30 KB flash), więc w domyślnym buildzie z WWW jest wyłączona.

Port szeregowy Nano (D0/D1, 9600 8N1), obsługa w `OTRSP_parse()`:

| Komenda | Działanie |
|---------|-----------|
| `AUX1n` / `AUX2n` | ustaw antenę TRX1 / TRX2 (4-bit) |
| `?AUX1` / `?AUX2` | odczyt bieżącej wartości |
| `?NAME` | zwraca `2x6SP9PDFRemoteAntennaSwitch` |
| `?` | ping |

## 7. Schemat blokowy

```
                 P4 (BCD TRX1) ┐         ┌ RJ45 J2 → anteny TRX1 (open-coll. 12V)
                 P3 (BCD TRX2) ┤         │ RJ45 J1 → anteny TRX2
                    PTT ────────┤         │
                                ▼         │
   Radio ─BCD─►  [ U6 MCP23017 0x21 IN ]  │        LCD U9 ◄─ A0,A1,D4-D7
                          ▲ I²C (A4/A5)   │        Enkoder ◄─ D2,D3 + S1(D9),LED(D8)
                    [ Arduino Nano U8 ]───┼──I²C── Uzas.  ◄─ A3 (/12V, dziel. R3/R4)
                          │ I²C           │        PC/SO2R◄─ D0/D1 (OTRSP)
                          ▼               │        W5500  ◄─ D10-D13 (opcja)
                 [ U5 MCP23017 0x20 OUT ]─┘
                    │GPA→U7(ULN)  │GPB→U4(ULN) │GPA7/GPB7→Q1/Q2→K1/K2
                    └─ anteny 1-6 ┘            └─ przekaźniki pasma GXP (J7/J6)

   Zasilanie: J5 (DC) → V1/F1 → LM2576 (U10) → +5V ; +12V rozprowadzane na RJ45/BCD
```

## 8. Zestawienie kluczowych układów (BoM — wybór)

| Ozn. | Wartość | Rola |
|------|---------|------|
| `U8` | Arduino Nano 3.0 | mikrokontroler |
| `U5`, `U6` | MCP23017 | ekspandery I²C (OUT / IN) |
| `U4`, `U7` | ULN/UDN | drivery open-collector do RJ45 |
| `U9` | HD44780 16×2 | wyświetlacz LCD |
| `U1` | W5500 (USR-ES1) | Ethernet (opcja) |
| `U10` | LM2576 | przetwornica step-down +5 V |
| `K1`, `K2` | przekaźnik | pasmo GXP11 (40 m / 20-10 m) |
| `Q1`, `Q2` | tranzystor | sterowanie cewek K1/K2 |
| `V1` / `F1` | warystor 18 V / bezpiecznik | ochrona wejścia zasilania |
| `S1` | mikroprzełącznik | przycisk enkodera |

*Pełna netlista i komponenty: [`hw/6x2-antenna-switch-control-03/ant-sw-control.net`](../hw/6x2-antenna-switch-control-03/ant-sw-control.net).*
