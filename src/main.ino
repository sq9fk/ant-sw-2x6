#include <Arduino.h>   // jawne dla IntelliSense (build .ino i tak go docieka; naprawia falszywy blad PROGMEM w VS Code)

/*

  6 x 2 Antenna control
  ----------------------
  http://remoteqth.com/6x2-antenna-controler.php
  2016-12 by OK1HRA
  rev 0.3

  ___               _        ___ _____ _  _
  | _ \___ _ __  ___| |_ ___ / _ \_   _| || |  __ ___ _ __
  |   / -_) '  \/ _ \  _/ -_) (_) || | | __ |_/ _/ _ \ '  \
  |_|_\___|_|_|_\___/\__\___|\__\_\|_| |_||_(_)__\___/_|_|_|


  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

  Features:
  INPUTS
    - Four TRX
    - Automatic four bit BCD - may be activate LOW or HIGH, must be configure on pcb and firmware
    - Manual menu with light button and rotary encoder
    - Manually through the web interface
    - Free configure matrix input table
  OUTPUTS
    - 6 antennas
    - 2x6 gpio outputs (12V or GND output dependancy to use IO driver)
    - serialline
    - web page
  BLOCKED
    - more inpunts in same time = select only one antenna for each TRX
    - colision between TRX and same antenna
    - switching during PTT ON, separately for each TRX
    - interruption PTT path if colision detected
  SHOW on LCD/WEB
    - active number outputs
    - colision
    - PTT ON
    - name of the antenna
    - power voltage
  ETHERNET (with optional IP module)
    - DHCP
    - fixed IP
    - Show IP on LCD during start up
  - refresh input < 25 ms two TRX, < 55 ms four TRX (arduino Nano)
  - all mounted on compact pcb - useable without enclosure
  - output connector included power for switch - all in one cable
  - Ethernet hardware support
  - support two line LCD, one for each TRX
  - measure input voltage
  
  Changelog:
  2016-12 change to rev 0.3 pinout

  SQ9FK: dawniej patchowano Dhcp.h biblioteki, zeby skrocic domyslny timeout DHCP (60s->6s).
  Od migracji na oficjalna biblioteke arduino-libraries/Ethernet (2026-07-29, byla adafruit/
  Ethernet2 - przestarzala) timeout jest parametrem Ethernet.begin(mac, timeout, responseTimeout)
  - patch biblioteki juz niepotrzebny, patrz wywolanie w setup().


  
*/
//=====[ Settings ]===================================================
// ---- Funkcje opcjonalne (SQ9FK) -------------------------------------------
//#define BCD_INPUT        // automatyczny wybor anteny z BCD radia (wejscia MCP IN) - WYLACZONE
//#define PTT_BLOCKING     // odczyt PTT + blokada przelaczania podczas TX - WYLACZONE
#define WEB_ANT_NAMES      // edycja nazw anten przez WWW + zapis w EEPROM (wymaga EthModule)
#define ANT_MAXLEN 11      // limit dlugosci nazwy anteny (mieści sie na LCD: LCDculumn-5)

// Domyslne nazwy anten (zrodlo inicjalizacji). Indeks 0 nieedytowalny (OFF); 7 = sentinel trybu BCD.
const char ant_0[] PROGMEM = "OFF";          // <-- do not change this line
const char ant_1[] PROGMEM = "160m INV-V";
const char ant_2[] PROGMEM = "80m Dipole";
const char ant_3[] PROGMEM = "Delta 80/40";
const char ant_4[] PROGMEM = "GXP11 40";
const char ant_5[] PROGMEM = "GXP11 20/10";
const char ant_6[] PROGMEM = "UB50";
#if defined(BCD_INPUT)
// SQ9FK: poz. 7 = sentinel trybu BCD ("M-off->BCD"), osiagalna tylko przy BCD_INPUT (enkoder 0..7).
// Sentinel przesuniety z 8 na 7 -> zlikwidowana martwa luka (dawna poz. 7 po usunietej 7. antenie).
const char ant_7[] PROGMEM = "M-off->BCD";
#endif
const char* const antDefault[] PROGMEM = {
  ant_0, ant_1, ant_2, ant_3, ant_4, ant_5, ant_6,
#if defined(BCD_INPUT)
  ant_7,                                     // sentinel trybu BCD (poz. 7) - tylko gdy BCD_INPUT
#endif
};
// SQ9FK: dwa niezalezne wyjscia Radio Flex na GPA7 (K1/J7) i GPB7 (K2/J6) - dawniej przekaznik
// pasma GXP11 40m. Sterowane osobnymi ikonami power w wierszach TRX (WWW), bez nazw.
// Domyslna nazwa stacji (topbar), edytowalna przez WWW i zapisywana w EEPROM:
const char siteDefault[] PROGMEM = "SP9PDF";
#if defined(WEB_ANT_NAMES)
  #include <EEPROM.h>
  #define ANT_EE_MAGIC  0xA5                         // magic sekcji nazw anten 1..6 (bez zmian -> nazwy zachowane)
  // SQ9FK: sekcja nazwy stacji ma WLASNY magic (za 6 nazwami anten). Dzieki temu przy zachowaniu
  // starego EEPROM (magic 0xA5) nazwy anten 1..6 zostaja, a nazwa stacji startuje z domyslnej.
  #define SITE_EE_MAGIC 0x5B
  #define SITE_MAG_OFF  (1 + 6 * ANT_MAXLEN)         // 67: bajt-magic sekcji nazwy stacji
  #define SITE_EE_OFF   (SITE_MAG_OFF + 1)           // 68: nazwa stacji
  char antRAM[8][ANT_MAXLEN + 1];            // 0=OFF, 1..6 anteny, 7=sentinel BCD (tylko BCD_INPUT)
  char siteRAM[ANT_MAXLEN + 1];              // SQ9FK: edytowalna nazwa stacji (topbar)
  static const char* antName(byte idx) { return antRAM[idx]; }
  static const char* siteName()        { return siteRAM; }

  static void loadAntNames() {
    strcpy_P(antRAM[0], (PGM_P)pgm_read_word(&antDefault[0]));  // OFF - staly (indeks 0, wybieralny zawsze)
#if defined(BCD_INPUT)
    strcpy_P(antRAM[7], (PGM_P)pgm_read_word(&antDefault[7]));  // sentinel trybu BCD (poz. 7)
#endif
    boolean ok = (EEPROM.read(0) == ANT_EE_MAGIC);
    for (byte k = 1; k <= 6; k++) {                            // SQ9FK: 6 anten (bylo 7)
      if (ok) {
        for (byte cc = 0; cc < ANT_MAXLEN; cc++)
          antRAM[k][cc] = EEPROM.read(1 + (k - 1) * ANT_MAXLEN + cc);
        antRAM[k][ANT_MAXLEN] = '\0';
      } else {
        strcpy_P(antRAM[k], (PGM_P)pgm_read_word(&antDefault[k]));
      }
    }
    if (EEPROM.read(SITE_MAG_OFF) == SITE_EE_MAGIC) {          // SQ9FK: nazwa stacji
      for (byte cc = 0; cc < ANT_MAXLEN; cc++)
        siteRAM[cc] = EEPROM.read(SITE_EE_OFF + cc);
      siteRAM[ANT_MAXLEN] = '\0';
    } else {
      strcpy_P(siteRAM, siteDefault);
    }
  }
  static void saveAntNames() {
    for (byte k = 1; k <= 6; k++)                             // SQ9FK: 6 anten
      for (byte cc = 0; cc < ANT_MAXLEN; cc++)
        EEPROM.update(1 + (k - 1) * ANT_MAXLEN + cc, antRAM[k][cc]);
    for (byte cc = 0; cc < ANT_MAXLEN; cc++)                  // SQ9FK: nazwa stacji
      EEPROM.update(SITE_EE_OFF + cc, siteRAM[cc]);
    EEPROM.update(0, ANT_EE_MAGIC);
    EEPROM.update(SITE_MAG_OFF, SITE_EE_MAGIC);
  }
  static byte hexNib(char h) {
    if (h >= '0' && h <= '9') return h - '0';
    if (h >= 'A' && h <= 'F') return h - 'A' + 10;
    if (h >= 'a' && h <= 'f') return h - 'a' + 10;
    return 0;
  }
  // Dekoduje wartosc z URL (po '=') do bufora dst, obcina do ANT_MAXLEN (#zabezpieczenie).
  static void parseName(const char* q, char* dst) {
    byte n = 0;
    while (*q && *q != ' ' && *q != '&' && n < ANT_MAXLEN) {
      char ch = *q++;
      if (ch == '+') ch = ' ';
      else if (ch == '%' && q[0] && q[1]) { ch = (hexNib(q[0]) << 4) | hexNib(q[1]); q += 2; }
      dst[n++] = ch;
    }
    dst[n] = '\0';
  }
#else
  static const __FlashStringHelper* antName(byte idx) {
    return (const __FlashStringHelper*)pgm_read_word(&antDefault[idx]);
  }
  static const __FlashStringHelper* siteName() {
    return (const __FlashStringHelper*)siteDefault;
  }
#endif
#define Inputs      6      // number of antenna used ** not implemented ** //SQ9FK was 6
#define Ports       2      // number of - IN/OUT pair devices and LCD lines (support from 2 to 4)
#define LCDculumn  16      //
#define inputHigh          // enable input High level (default)
//#define serialECHO       // enable TX echo on serial port
//#define OTRSP            // enable serial OTSRP on serial port (disabled to free flash for EthModule)
// SQ9FK: OTRSP_TCP dodaje surowe gniazdo TCP dla OTRSP obok USB (wymaga OTRSP; wyklucza sie ze
// strona WWW - EthModule - patrz #error nizej i docs/DESIGN.md). Port dowolny, nie narzucony
// przez protokol OTRSP.
//#define OTRSP_TCP
#define OTRSP_TCP_PORT 4534
//#define OTRSP_DEBUG
#define SERBAUD    9600    // [baud] Serial port baudrate
#define EthModule        // enable Ethernet module
// SQ9FK: DHCP WYLACZONE domyslnie (static IP - patrz ip/gateway/subnet nizej, ustaw pod siec
// docelowa). Powod: obsluga DHCP w oficjalnej bibliotece Arduino Ethernet kosztuje ~3,8 KB flash
// (Dhcp.cpp+EthernetUdp.cpp) - ze strona WWW (EthModule) budzet ATmega328 tego nie miesci razem
// z DHCP. Warianty z zapasem (np. OTRSP+OTRSP_TCP, bez strony WWW) MOGA odkomentowac ta linie -
// tam DHCP sie miesci (patrz docs/DESIGN.md).
//#define __USE_DHCP__       // Uncomment to Enable DHCP
//====================================================================
#if defined(OTRSP_TCP) && !defined(OTRSP)
  #error "OTRSP_TCP wymaga zdefiniowania OTRSP (surowy TCP to dodatkowy kanal do tego samego parsera)"
#endif
#if defined(OTRSP_TCP) && defined(EthModule)
  #error "OTRSP_TCP wyklucza sie z EthModule (strona WWW) - razem nie miesca sie w flash ATmega328"
#endif
// SQ9FK: sprzet sieciowy (W5500/DHCP) potrzebny zarowno dla strony WWW (EthModule) jak i dla
// surowego TCP OTRSP (OTRSP_TCP) - bring-up jest wspolny, tylko serwer/tresc na porcie sa inne.
#if defined(EthModule) || defined(OTRSP_TCP)
  // SQ9FK: oficjalna biblioteka Arduino (arduino-libraries/Ethernet) - Ethernet2/Ethernet3 sa
  // przestarzale/nieutrzymywane. Jeden naglowek <Ethernet.h> (EthernetClient/Server/Dhcp w srodku,
  // bez osobnych util.h/Dhcp.h/EthernetServer.h jak w Ethernet2). Auto-detekcja chipu W5100/
  // W5200/W5500, domyslny pin CS=10 dla AVR (zgodny z PCB: D10-D13 SPI -> W5500).
  #include <Ethernet.h>
  #include <SPI.h>
#endif
#include "Wire.h"
#include <LiquidCrystal.h>
LiquidCrystal lcd(A0, A1, 7, 6, 5, 4);     // rev. 0.3
#if defined(EthModule) || defined(OTRSP_TCP)
  // SQ9FK: mac/ip/gateway/subnet wspolne dla strony WWW (EthModule) i surowego TCP (OTRSP_TCP).
  // Domyslnie static IP (bez __USE_DHCP__) - dostosuj ip/gateway/subnet pod siec docelowa.
  // Przy __USE_DHCP__ te wartosci sluza jako fallback po nieudanych probach DHCP (patrz setup()).
  byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xEE};
  IPAddress ip(192, 168, 5, 45);         // IP
  IPAddress gateway(192, 168, 5, 254);    // GATE
  IPAddress subnet(255, 255, 255, 0);     // MASK
  IPAddress myDns(8, 8, 8, 8);            // DNS (google pub)
#endif
#if defined(OTRSP_TCP)
  // SQ9FK: surowe gniazdo TCP dla OTRSP - trwale polaczenie (nie request/response jak WWW),
  // klient utrzymywany miedzy iteracjami loop(); wlasny bufor linii, niezalezny od USB (in_buf).
  EthernetServer otrspServer(OTRSP_TCP_PORT);
  EthernetClient otrspClient;
  char otrsp_tcp_buf[64];
  byte otrsp_tcp_len = 0;
#endif
#if defined(EthModule)
  EthernetServer server(80);              // server PORT (strona WWW)

  // SQ9FK: statyczne fragmenty strony w PROGMEM, wysylane jednym sendP() w wiekszych
  // porcjach (mniej zapisow do W5500 -> krotsza transakcja -> mniejsza blokada PTT/switch).
  const char HTTP_HEAD[] PROGMEM =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<!DOCTYPE html>\r\n<html lang=\"pl\">\r\n<head>\r\n";
  // SQ9FK: nowy wyglad WWW wg konwencji projektu rotator_wifi_bridge (ciemny teal, akcent zolty,
  // karty, font Inter z fallbackiem systemowym - bez zewnetrznego linku, oszczedza flash/RTT).
  const char HTTP_HEAD2[] PROGMEM =
    "<meta charset=\"utf-8\">\r\n"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\r\n"
    "<meta name=\"theme-color\" content=\"#12333b\">\r\n"
    "<style>\r\n"
    // SQ9FK: mobile @media (max-width:520px) - wieksze cele dotykowe, kompaktowy topbar/chipy
    "body{margin:0;padding:0 0 calc(1.5rem + env(safe-area-inset-bottom,0));min-width:290px;"
      "background:#12333b;color:#f7f7f7;font-family:Inter,Helvetica,Arial,sans-serif}\r\n"
    ".tb{display:flex;align-items:center;gap:.55rem;max-width:760px;margin:0 auto;"
      "padding:.9rem 1.2rem;font-weight:800;font-size:1.1rem}\r\n"
    ".dot{width:.6rem;height:.6rem;border-radius:50%;background:#06ac51;flex:0 0 auto}\r\n"
    ".dot.bad{background:#d11534}\r\n"
    ".wrap{max-width:760px;margin:0 auto;padding:0 1rem;display:flex;flex-direction:column;gap:1rem}\r\n"
    ".card{background:#1a3a42;border-radius:1.1em;padding:1.1rem 1.2rem}\r\n"
    "h2{margin:0 0 .8rem;font-size:1rem;font-weight:700}\r\n"
    ".ahead{display:flex;align-items:center;flex-wrap:wrap;gap:.6rem .7rem;margin-bottom:.85rem}\r\n"
    ".ahead h2{margin:0}\r\n"
    ".astat{display:flex;gap:.5rem;flex:1 1 auto;min-width:13rem}\r\n"
    ".st{display:flex;align-items:center;gap:.5rem;flex:1 1 0;min-width:0;background:#21505c;"
      "border-radius:.4em;padding:.3rem .5rem;font-size:.88rem}\r\n"
    ".st .an{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}\r\n"
    ".sq{display:inline-flex;align-items:center;justify-content:center;width:1.5rem;height:1.5rem;"
      "border-radius:.35em;background:#2a5d6b;color:#f7f7f7;font-weight:700;font-size:.85rem;flex:0 0 auto}\r\n"
    ".sq.on{background:#f5d33c;color:#162f36}\r\n"
    ".trx{display:flex;align-items:center;flex-wrap:wrap;gap:.35rem;padding:.5rem 0;"
      "border-bottom:1px solid rgba(255,255,255,.07)}\r\n"
    ".bcd,.bcdr{border-radius:.4em;padding:.4rem .7rem;margin-right:.35rem;min-width:5rem;"
      "font-weight:700;font-size:.9rem;display:inline-block}\r\n"
    ".bcd{background:#21505c}.bcdr{background:#d11534}\r\n"
    "input[type=submit]{border:none;border-radius:.4em;background:#2a5d6b;color:#f7f7f7;"
      "font-family:inherit;font-size:.9rem;font-weight:700;padding:.45rem .7rem;margin:.12rem;cursor:pointer}\r\n"
    "input[type=submit].g{background:#f5d33c;color:#162f36}\r\n"
    "input[type=submit].gr{background:#d11534;color:#f7f7f7}\r\n"
    "input:not([type=submit]){border:none;border-radius:.4em;background:#21505c;color:#f7f7f7;"
      "font-family:inherit;font-size:.9rem;padding:.45rem .6rem}\r\n"
    ".flx{display:inline-flex;align-items:center;margin-left:auto;border:none;border-radius:.4em;"
      "background:#2a5d6b;color:#a7b9be;padding:.4rem .55rem;cursor:pointer}\r\n"
    ".flx svg{width:15px;height:15px}\r\n"
    ".flx.on{background:#f5d33c;color:#162f36}\r\n"
    ".leg{display:grid;grid-template-columns:repeat(auto-fit,minmax(12.5rem,1fr));gap:.5rem 1.1rem}\r\n"
    ".li{display:flex;align-items:center;gap:.6rem;font-size:.92rem}\r\n"
    ".li b{display:inline-flex;align-items:center;justify-content:center;min-width:1.6rem;height:1.6rem;"
      "background:#2a5d6b;border-radius:.4em;color:#f7f7f7;font-weight:700;font-size:.85rem;flex:0 0 auto}\r\n"
    "details.card summary{list-style:none;cursor:pointer;display:flex;align-items:center;justify-content:space-between}\r\n"
    "details.card summary::-webkit-details-marker{display:none}\r\n"
    "details.card summary h2{margin:0}\r\n"
    ".chev{color:#a7b9be}\r\n"
    ".nm{display:flex;align-items:center;gap:.5rem;margin:.4rem 0}\r\n"
    ".nm b{min-width:3rem;color:#a7b9be;font-weight:600;font-size:.85rem}\r\n"
    ".rows .row{display:flex;justify-content:space-between;padding:.5rem 0 0}\r\n"
    ".rows span{color:#a7b9be}\r\n"
    "@media(max-width:520px){.tb{font-size:.95rem;padding:.7rem max(.85rem,env(safe-area-inset-right)) "
      ".7rem max(.85rem,env(safe-area-inset-left))}.astat{min-width:0;flex:1 1 100%}.bcd,.bcdr{min-width:0;"
      "margin-right:.2rem;font-size:.82rem;padding:.35rem .55rem}input[type=submit],.flx{min-width:2.75rem;"
      "min-height:2.75rem;padding:.45rem .55rem;margin:.15rem}.flx svg{width:18px;height:18px}.trx{gap:.25rem;"
      "padding:.65rem 0}.nm{flex-wrap:wrap}}\r\n"
    "</style>\r\n</head>\r\n<body>\r\n";

  // SQ9FK: ikona power (wlacz/wylacz) dla przyciskow Flex - dziedziczy kolor (currentColor)
  const char POWER_SVG[] PROGMEM =
    "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.5\" "
    "stroke-linecap=\"round\"><line x1=\"12\" y1=\"3\" x2=\"12\" y2=\"12\"/>"
    "<path d=\"M6.4 7.3a8 8 0 1 0 11.2 0\"/></svg>";

  // SQ9FK: bufor wyjscia strony. W Ethernet2 kazde send() to osobny segment TCP + busy-wait na
  // SEND_OK, a out.print(F("...")) wysyla ZNAK PO ZNAKU -> setki drobnych pakietow = wolno.
  // BufP zbiera znaki w RAM i oddaje je do W5500 porcjami 64 B (kilkadziesiat send() zamiast tysiecy).
  // Cala strona idzie przez `out` (print/println), na koncu out.done() domyka bufor.
  class BufP : public Print {
      EthernetClient &cl;
      uint8_t buf[128];   // wiekszy bufor = mniej segmentow TCP (na stosie, RAM ma zapas)
      uint8_t n;
    public:
      BufP(EthernetClient &c) : cl(c), n(0) {}
      size_t write(uint8_t b) {
        buf[n++] = b;
        if (n >= sizeof(buf)) { cl.write(buf, n); n = 0; }
        return 1;
      }
      void done() { if (n) { cl.write(buf, n); n = 0; } }
  };
#endif
#if defined(BCD_INPUT)
const byte BCDmatrixOUT[2][16] PROGMEM = {
                     { 0,  1,  2,  3,  4,  5,  4,  5,  4,  5,  6,  3,  3,  3,  3,  3 },
                     { 0,  1,  2,  3,  4,  5,  4,  5,  4,  5,  6,  3,  3,  3,  3,  3 },
};
#endif
byte a = 0;
byte b = 0;
unsigned int ab;
//long Loops=0;
int i = 0;
int j = 0;
int c = 0;
int val;
int e = 0;
const int enc0PinA = 2;
const int enc0PinB = 3;
const int sw = 9;
const int swLED = 8;
int enc0Pos = 0;
byte enc0PinALast = HIGH;
int n = HIGH;
boolean menu1state = false;
boolean menu2state = false;
unsigned long Timeout[5][2] = {
  {0, 100},
  {0, 500},
  {0, 1000},
  {0, 5000},
  {0, 3000},
};
unsigned long voltWarn = 0;   // SQ9FK: znacznik czasu ostrzezenia napiecia (nieblokujaco, 0=brak)
boolean buttonActive = false;
boolean longPressActive = false;
// Glify LCD w PROGMEM (CGRAM 0..5): ERR, PTT, MAN, Cursor, mCursor, m
const byte glyphs[6][8] PROGMEM = {
  {0b11111, 0b11011, 0b11011, 0b11011, 0b11011, 0b11111, 0b11011, 0b11111}, // 0 ERR
  {0b11111, 0b10011, 0b10101, 0b10101, 0b10011, 0b10111, 0b10111, 0b11111}, // 1 PTT
  {0b11111, 0b01110, 0b00100, 0b01010, 0b01110, 0b01110, 0b01110, 0b11111}, // 2 MAN
  {0b00000, 0b00000, 0b11000, 0b11000, 0b11000, 0b00000, 0b00000, 0b00000}, // 3 Cursor
  {0b00011, 0b00010, 0b11010, 0b11010, 0b11010, 0b00010, 0b00011, 0b00000}, // 4 mCursor
  {0b00011, 0b00010, 0b00010, 0b00010, 0b00010, 0b00010, 0b00011, 0b00000}, // 5 m
};
String Note;
byte port[8][6] = {
  //  adr   # ptt Err Manual part
  { 0x21, 0, 0,  0, 0, 1 }, // port1 IN
  { 0x21, 0, 0,  0, 0, 2 }, // port2 IN
  { 0x23, 0, 0,  0, 0, 1 }, // port3 IN
  { 0x23, 0, 0,  0, 0, 2 }, // port4 IN
  { 0x20, 0, 0,  0, 0, 1 }, // port1 OUT
  { 0x20, 0, 0,  0, 0, 2 }, // port2 OUT
  { 0x22, 0, 0,  0, 0, 1 }, // port3 OUT
  { 0x22, 0, 0,  0, 0, 2 }, // port4 OUT
};
// SQ9FK: stan dwoch niezaleznych wyjsc Radio Flex: [0]=GPA7 (K1/J7), [1]=GPB7 (K2/J6). 0=off 1=on
byte flexState[2] = { 0, 0 };

#if defined(OTRSP)
char        in_buf[64];  // UART input buffer
static byte in_len;      // Number of chars in buffer
boolean stringComplete = false;  // whether the string is complete
#endif

#if defined(OTRSP)
static void OTRSP_parse(char *cmd, Print &out);
#endif

//=================================================================
void setup()
{
#if defined(WEB_ANT_NAMES)
  loadAntNames();          // SQ9FK: wczytaj nazwy anten z EEPROM (lub domyslne)
#endif
  Wire.begin();
  for (i = 0; i < Ports; i++) {
    Wire.beginTransmission(port[i + 4][0]);
    Wire.write((byte)0x00);
    Wire.write((byte)0x00);
    Wire.endTransmission();
    Wire.beginTransmission(port[i + 4][0]);
    Wire.write((byte)0x01);
    Wire.write((byte)0x00);
    Wire.endTransmission();
  }
  lcd.begin(16, Ports);
  lcd.setCursor(0, Ports / 2 - 1);
  lcd.print(F("      SQ9FK"));
  lcd.setCursor(1, Ports / 2);
  lcd.print(Inputs);
  lcd.setCursor(2, Ports / 2);
  lcd.print(F("x  ANT control"));
  lcd.setCursor(3, Ports / 2);
  lcd.print(Ports);

  delay(2000);      // splash startowy na LCD (2 s); daje tez czas na rozruch W5500
  lcd.clear();
  byte cbuf[8];
  for (byte g = 0; g < 6; g++) {
    memcpy_P(cbuf, glyphs[g], 8);
    lcd.createChar(g, cbuf);
  }
  pinMode(swLED, OUTPUT);
  digitalWrite (swLED, LOW);
  pinMode(sw, INPUT);
  digitalWrite (sw, HIGH);
  pinMode (enc0PinA, INPUT);
  pinMode (enc0PinB, INPUT);
  attachInterrupt(digitalPinToInterrupt(enc0PinB), encI, FALLING);
#if defined(serialECHO) || defined(EthModule) || defined(OTRSP)
  Serial.begin(SERBAUD);
#endif
  lcd.setCursor(1, Ports / 2 - 1);
  lcd.print(F("Input voltage:"));
  lcd.setCursor(1, Ports / 2);
  lcd.print(volt(analogRead(A3)));
  lcd.setCursor(7, Ports / 2);
  lcd.print("V");
  delay(3000);      // pokaz napiecie zasilania na LCD (3 s)
  lcd.clear();
#if defined(EthModule) || defined(OTRSP_TCP)
// SQ9FK: bring-up sprzetu sieciowego (DHCP+fallback) wspolny dla strony WWW i surowego TCP OTRSP.
#if defined __USE_DHCP__
  // DHCP z ponawianiem (router bywa wolny przy starcie). SQ9FK: oficjalna biblioteka Ethernet
  // przyjmuje timeout jako parametr begin() - 6 s/proba (bylo 60 s domyslne w Ethernet2, wymagalo
  // patchowania Dhcp.h). 3 proby x 6 s = do ~18 s zamiast do 180 s, potem fallback na static IP.
  byte dhcpTry = 0;
  while (Ethernet.begin(mac, 6000, 4000) == 0) {
    dhcpTry++;
    lcd.clear();
    lcd.setCursor(1, Ports / 2 - 1);
    lcd.print(F("DHCP... proba "));
    lcd.print(dhcpTry);
    if (dhcpTry >= 3) {
      Ethernet.begin(mac, ip, myDns, gateway, subnet);   // brak DHCP -> static IP (osiagalny zdalnie)
      break;
    }
  }
#else
  Ethernet.begin(mac, ip, myDns, gateway, subnet);
#endif
#if defined(EthModule)
  server.begin();          // strona WWW (port 80)
#endif
#if defined(OTRSP_TCP)
  otrspServer.begin();      // surowy TCP dla OTRSP (OTRSP_TCP_PORT)
#endif
  Serial.print(F("server is at "));
  Serial.println(Ethernet.localIP());
  lcd.setCursor(1, Ports / 2 - 1);
  lcd.print(F("IP address:"));
  lcd.setCursor(1, Ports / 2);
  lcd.print(Ethernet.localIP());
  delay(5000);       // pokaz IP na LCD (5 s)
#endif
}

void loop() {

    //=====[ OTRSP ]=================
#if defined(OTRSP)
  if (stringComplete) {
    OTRSP_parse(in_buf, Serial);   // kanal USB
    stringComplete = false;
  }
#endif

  //=====[ GPIOs 4]=================
  for (i = 0; i < Ports; i++) {
#if defined(BCD_INPUT)
    if (menu1state == 1 && i == enc0Pos) {
      if (port[i][1] != 7) { //SQ9FK poz. 7 = tryb BCD (sentinel); inaczej tryb reczny
        port[i][4] = 1;
      } else {
        port[i][4] = 0;
      }
    } else if (port[i][4] == 0) {
      rx(port[i][0], i, 0, port[i][5]);   // auto: wybor anteny z BCD radia
    }
#endif

    c = 0;
    for (j = 0; j < Ports; j++) {
      // SQ9FK: kolizja tylko gdy oba TRX zadaja tej samej anteny. Blokada 4<->5 (GXP11)
      // usunieta - poz. 4 i 5 to teraz niezalezne anteny (bit3/bit4), bit7 przejalo Radio Flex.
      if (i != j && port[i][1] == port[j + 4][1]) {
        c++;
      }
    }
    if (c > 0) {
      port[i][3] = 1;
#if defined(PTT_BLOCKING)
      if (port[i][2] == 0)
#endif
        port[i + 4][1] = 0;               // kolizja -> wyjscie OFF
    } else {
      port[i][3] = 0;
#if defined(PTT_BLOCKING)
      if (port[i][2] == 0)
#endif
        port[i + 4][1] = port[i][1];      // brak kolizji -> wyjscie = wybor
    }
    if (port[i + 4][5] == 2) {
      tx(port[i + 4][0], i);
    }
  }
  //=====[ Ethernet ]=================
#if defined(EthModule) || defined(OTRSP_TCP)
#if defined __USE_DHCP__
  Ethernet.maintain();   // SQ9FK: odnawianie dzierzawy DHCP (nieblokujace); bez tego IP moze przepasc
#endif
#endif
#if defined(EthModule)
  EthernetClient client = server.available();
  if (client) {
    // SQ9FK (#5): bufor linii zadania bez String (mniej sterty). Dla komend anteny
    // potrzebne indeksy 7 (bank) i 8-9 (kod); dla edycji nazw (WEB_ANT_NAMES) tez wartosc.
#if defined(WEB_ANT_NAMES)
    char reqBuf[48];
#else
    char reqBuf[16];
#endif
    byte reqLen = 0;
    // SQ9FK (#4): czytamy tylko pierwsza linie zadania (GET ...) i od razu odpowiadamy
    // -> mniej odczytow i krotsza blokada loop() (switching/PTT).
    while (client.connected()) {
      int avail = client.available();
      if (avail > 0) {
        // SQ9FK: czytaj dostepne bajty jednym recv (zamiast po bajcie -> mniej transakcji SPI)
        int room = (int)sizeof(reqBuf) - 1 - reqLen;
        if (avail > room) avail = room;
        if (avail > 0) reqLen += client.read((uint8_t*)reqBuf + reqLen, avail);
        reqBuf[reqLen] = '\0';
        if (memchr(reqBuf, '\n', reqLen) || reqLen >= (int)sizeof(reqBuf) - 1) {
          // koniec linii zadania (lub pelny bufor) -> generuj odpowiedz
          BufP out(client);   // SQ9FK: cala strona przez bufor (mniej segmentow TCP = szybciej)
          // ---- naglowek HTTP + <head> + CSS: statyczne, z PROGMEM (przez bufor) ----
          out.print((const __FlashStringHelper*)HTTP_HEAD);
          out.print(F("<title>"));
          out.print(Inputs);
          out.print(F("x"));
          out.print(Ports);
          out.print(F(" "));
          out.print(siteName());
          out.println(F(" - Antenna switch</title>"));
          out.print(F("<meta http-equiv=\"refresh\" content=\"10;url=http://"));
          out.print(Ethernet.localIP());
          out.println(F("\">"));
          out.print((const __FlashStringHelper*)HTTP_HEAD2);
          // SQ9FK: topbar - kropka statusu (czerwona gdy napiecie poza 10-15 V) + nazwa stacji
          float vv = volt(analogRead(A3));
          out.print(F("<nav class=\"tb\"><span class=\"dot"));
          if (vv < 10 || vv > 15) out.print(F(" bad"));
          out.print(F("\"></span>"));
          out.print(siteName());
          out.print(F(" "));
          out.print(Inputs);
          out.print(F("x"));
          out.print(Ports);
          out.println(F(" Antenna Switch</nav>"));
          out.println(F("<div class=\"wrap\">"));
          // SQ9FK (#5): parsuj prosto z bufora. Zadania:
          //   /?S{bank}{kod}   - wybor anteny / tryb (00..06, 20/21)
          //   /?N{k}={nazwa}   - edycja nazwy anteny 1..6           (WEB_ANT_NAMES)
          //   /?NS={nazwa}     - edycja nazwy stacji (topbar)        (WEB_ANT_NAMES)
          //   /?F{s}{0|1}      - zalaczenie/wylaczenie Flex s=1/2 (GPA7/GPB7)
#if defined(WEB_ANT_NAMES)
          if (reqBuf[6] == 'N' && reqBuf[7] >= '1' && reqBuf[7] <= '6') {
            parseName(reqBuf + 9, antRAM[reqBuf[7] - '0']);   // edycja nazwy anteny + zapis EEPROM
            saveAntNames();
          } else if (reqBuf[6] == 'N' && reqBuf[7] == 'S') {
            parseName(reqBuf + 9, siteRAM);                   // edycja nazwy stacji + zapis EEPROM
            saveAntNames();
          } else
#endif
          if (reqBuf[6] == 'F' && (reqBuf[7] == '1' || reqBuf[7] == '2') &&
                                  (reqBuf[8] == '0' || reqBuf[8] == '1')) {
            flexState[reqBuf[7] - '1'] = (reqBuf[8] == '1'); // SQ9FK: przelaczenie Radio Flex
          } else {
            int bankIdx = -1, getVal = 0;
            if (reqLen >= 10 &&
                reqBuf[7] >= '0' && reqBuf[7] <= '9' &&
                reqBuf[8] >= '0' && reqBuf[8] <= '9' &&
                reqBuf[9] >= '0' && reqBuf[9] <= '9') {
              bankIdx = (reqBuf[7] - '0') - 1;
              getVal  = (reqBuf[8] - '0') * 10 + (reqBuf[9] - '0');
            }
            // SQ9FK (#2): dzialaj tylko na poprawne zadanie sterujace. Zapobiega
            // przypadkowym przelaczeniom od obcych zadan (np. /favicon.ico, gole GET /)
            // oraz zapisowi poza tablica port[] przy blednym numerze banku.
            if (bankIdx >= 0 && bankIdx < Ports) {
              if (getVal >= 0 && getVal <= 6) {              // SQ9FK: 6 anten (bylo 7)
                port[bankIdx][1] = getVal;
              } else if (getVal == 20) {
                port[bankIdx][4] = 0;
              } else if (getVal == 21) {
                port[bankIdx][4] = 1;
              }
            }
          }

          // SQ9FK: naglowek karty Anteny + statusy sekcji (chipy 50/50: numer + nazwa wl. anteny)
          out.print(F("<section class=\"card\"><div class=\"ahead\"><h2>Anteny</h2><div class=\"astat\">"));
          for (i = 0; i < Ports; i++) {
              out.print(F("<span class=\"st\"><span class=\"sq"));
              if (port[i][1]) out.print(F(" on"));
              out.print(F("\">"));
              out.print(i+1);
              out.print(F("</span><span class=\"an\">"));
              out.print(antName(port[i][1]));
              out.print(F("</span></span>"));
          }
          out.println(F("</div></div><form method=\"get\">"));
          for (i = 0; i < Ports; i++) {
              out.print(F("<div class=\"trx\"><span class=\"bcd"));
              if (port[i][3] == 1) {
                out.print(F("r"));
              }
              out.print(F("\">TRX"));
              out.print(i+1);
              out.print(F(" &#10148; "));
              out.print(port[i][1]);
              out.print(F("</span>"));
#if defined(BCD_INPUT)
              if (port[i][4] == 0) {
                out.print(F("<input type=\"submit\" name=\"S"));
                out.print(i+1);
                out.print(F("21\" value=\"Manual\"> "));
              } else {
#endif
                //SQ9FK: pozycje 0..6 generowane w petli (6 anten; oszczednosc flash)
                for (byte pos = 0; pos <= 6; pos++) {
                  if (pos == 0) {
                    out.print(F("<input type=\"submit\" name=\"S"));
                  } else {
                    out.print(F("\"><input type=\"submit\" name=\"S"));
                  }
                  out.print(i+1);
                  out.print(F("0"));
                  out.print(pos);
                  out.print(F("\" value=\""));
                  if (pos == 0) {
                    out.print(F("-"));
                  } else {
                    out.print(pos);
                  }
                  out.print(F("\" class=\""));
                  if (port[i][1] == pos) {
                    out.print(F("g"));
                    if (port[i][3] == 1) {
                      out.print(F("r"));
                    }
                  }
                }
#if defined(BCD_INPUT)
                out.print(F("\"><input type=\"submit\" name=\"S"));
                out.print(i+1);
                out.print(F("20\" value=\"BCD-"));
                out.print(i+1);
                out.println(F("\"> "));
              }
#else
                out.println(F("\"> "));   // zamknij ostatni przycisk (bez przelacznika BCD)
#endif
#if defined(PTT_BLOCKING)
              if (port[i][2] == 1) {
                out.print(F("<span class=\"ptt\">PTT</span>"));
              }
#endif
              // SQ9FK: ikona Radio Flex tego TRX (tylko TRX1/2 = GPA7/GPB7); klik przelacza F{s}{new}
              if (i < 2) {
                out.print(F("<button type=\"submit\" class=\"flx"));
                if (flexState[i]) out.print(F(" on"));
                out.print(F("\" name=\"F"));
                out.print(i+1);
                out.print(flexState[i] ? 0 : 1);
                out.print(F("\" title=\"Radio Flex TRX"));
                out.print(i+1);
                out.print(F("\">"));
                out.print((const __FlashStringHelper*)POWER_SVG);
                out.print(F("</button>"));
              }
              out.print(F("</div>"));
          }
          out.println(F("</form></section>"));
          // SQ9FK: Opis anten - legenda tylko do odczytu (widac co jest pod numerem)
          out.println(F("<section class=\"card\"><h2>Opis anten</h2><div class=\"leg\">"));
          for (byte k = 1; k <= 6; k++) {
            out.print(F("<div class=\"li\"><b>"));
            out.print(k);
            out.print(F("</b>"));
            out.print(antName(k));
            out.println(F("</div>"));
          }
          out.println(F("</div></section>"));
          // SQ9FK: Settings (zwijane <details>) - nazwa stacji + nazwy anten + ukryte napiecie
          out.println(F("<details class=\"card\"><summary><h2>Settings</h2><span class=\"chev\">&#9662;</span></summary>"));
#if defined(WEB_ANT_NAMES)
          // SQ9FK: nazwa stacji -> /?NS={nazwa}
          out.print(F("<div class=\"nm\"><b>Nazwa</b><form method=\"get\" style=\"display:inline\">"
                         "<input name=\"NS\" maxlength=\"11\" size=\"12\" value=\""));
          out.print(siteName());
          out.println(F("\"><input type=\"submit\" value=\"OK\"></form></div>"));
          // SQ9FK: edycja nazw anten (1..6) -> /?N{k}={nazwa}
          for (byte k = 1; k <= 6; k++) {
            out.print(F("<div class=\"nm\"><b>"));
            out.print(k);
            out.print(F("</b><form method=\"get\" style=\"display:inline\"><input name=\"N"));
            out.print(k);
            out.print(F("\" maxlength=\"11\" size=\"12\" value=\""));
            out.print(antName(k));
            out.println(F("\"><input type=\"submit\" value=\"OK\"></form></div>"));
          }
#else
          out.print(F("<div class=\"nm\"><b>Nazwa</b>"));
          out.println(siteName());
          out.println(F("</div>"));
          for (byte k = 1; k <= 6; k++) {
            out.print(F("<div class=\"nm\"><b>"));
            out.print(k);
            out.print(F("</b>"));
            out.println(antName(k));
            out.println(F("</div>"));
          }
#endif
          // SQ9FK: ukryty (w zwinietym Settings) odczyt napiecia zasilania (ostrzezenie = czerwona kropka w topbarze)
          out.print(F("<div class=\"rows\"><div class=\"row\"><span>Napi&#281;cie zasilania</span><strong>"));
          out.print(vv);
          out.println(F("V</strong></div></div>"));
          out.println(F("</details></div></body></html>"));
          out.done();   // SQ9FK: wyslij reszte bufora

          break;
        }
      }
    }
    delay(1);
    client.stop();
  }
#endif

#if defined(OTRSP_TCP)
  //=====[ OTRSP TCP ]=================
  // SQ9FK: polaczenie TRWALE (nie request/response jak WWW) - klient zostaje podlaczony,
  // dopoki sam sie nie rozlaczy; komendy moga przychodzic wieloma porcjami w czasie. Wlasny
  // bufor linii (otrsp_tcp_buf), niezalezny od USB (in_buf) - dwa kanaly dzialaja rownolegle,
  // bez wzajemnego mieszania komend. Jeden aktywny klient TCP na raz (device dla jednego
  // operatora/loggera; kolejne przychodzace polaczenie zastapi biezace przy odbiorze danych).
  if (otrspClient && !otrspClient.connected()) {
    otrspClient.stop();
    otrspClient = EthernetClient();     // zerwane polaczenie -> wyczysc powiazany stan
    otrsp_tcp_len = 0;
  }
  {
    EthernetClient newC = otrspServer.available();   // ma dane do odczytania TERAZ
    if (newC) {
      if (!(newC == otrspClient)) {
        otrspClient = newC;             // inny socket = nowe polaczenie -> czysty bufor
        otrsp_tcp_len = 0;
      }
      int avail = otrspClient.available();
      int room = (int)sizeof(otrsp_tcp_buf) - 1 - otrsp_tcp_len;
      if (avail > room) avail = room;
      if (avail > 0)
        otrsp_tcp_len += otrspClient.read((uint8_t*)otrsp_tcp_buf + otrsp_tcp_len, avail);
      otrsp_tcp_buf[otrsp_tcp_len] = '\0';
      char *cr = (char*)memchr(otrsp_tcp_buf, '\r', otrsp_tcp_len);
      if (cr) {
        *cr = '\0';
        OTRSP_parse(otrsp_tcp_buf, otrspClient);
        byte consumed = (byte)(cr - otrsp_tcp_buf) + 1;
        byte remain = otrsp_tcp_len - consumed;
        if (remain > 0 && cr[1] == '\n') { memmove(cr + 1, cr + 2, --remain); }  // pomin CRLF
        if (remain > 0) memmove(otrsp_tcp_buf, cr + 1, remain);
        otrsp_tcp_len = remain;
      } else if (otrsp_tcp_len >= sizeof(otrsp_tcp_buf) - 1) {
        otrsp_tcp_len = 0;   // przepelnienie bez CR - porzuc linie (zabezpieczenie)
      }
    }
  }
#endif

  //=====[ Serial ]=================
#if defined(serialECHO)
  if (millis() - Timeout[2][0] > (Timeout[2][1])) {
    for (i = 0; i < Ports; i++) {
      Serial.print(F("<"));
      Serial.print(i + 1);
      Serial.print(F(','));
      Serial.print(port[i][1]);
      Serial.print(F(','));
      Serial.print(port[i][2]);
      Serial.print(F(','));
      Serial.print(port[i][3]);
      Serial.print(F(','));
      Serial.print(port[i][4]);
      Serial.println(F(">"));
    }
    Serial.flush();
    Timeout[2][0] = millis();
  }
#endif

  //=====[ Button ]=================
  if (digitalRead(sw) == 0) {
    if (buttonActive == 0) {
      buttonActive = 1;
      Timeout[1][0] = millis();
    }
    if ((millis() - Timeout[1][0] > Timeout[1][1]) && (longPressActive == 0)) {
      longPressActive = 1;
      menu1state = !menu1state;
      digitalWrite(swLED, menu1state);
    }
  } else {
    if (buttonActive == 1) {
      if (longPressActive == 1) {
        longPressActive = 0;
      } else {
      }
      buttonActive = 0;
    }
  }
  //=====[ LCD ]=================
  if (millis() - Timeout[0][0] > (Timeout[0][1])) {
    Timeout[0][0] = millis();
    // SQ9FK: kontrola napiecia co Timeout[4] - ostrzezenie NIEBLOKUJACE (bez delay -> WWW/switch
    // i enkoder dzialaja podczas awarii napiecia). Ostrzezenie trzymane ~2 s przez voltWarn.
    if (millis() - Timeout[4][0] > (Timeout[4][1])) {
      Timeout[4][0] = millis();
      float v = volt(analogRead(A3));
      if (v < 10 || v > 15) {
        lcd.clear();
        lcd.setCursor(1, Ports / 2 - 1);
        lcd.print(v < 10 ? F("LOW voltage!") : F("HIGH voltage!"));
        lcd.setCursor(8, Ports / 2);
        lcd.print(v);
        voltWarn = millis();
      }
    }
    // normalne linie LCD tylko gdy nie trwa ostrzezenie napiecia
    if (voltWarn == 0 || millis() - voltWarn >= 2000) {
      for (j = 0; j < Ports; j++) {
        show(j);
      }
    }
  }
}

//=====[ Encoder ]========================== interrupt
void encI(){
  if(digitalRead(enc0PinA) == LOW){
      e=1;  // ++
  }else{
      e=-1; // --
  }
  Timeout[3][0] = millis();

  if (menu1state == 0) {
    enc0Pos = enc2(enc0Pos, Ports-1, e);
  } else {
#if defined(BCD_INPUT)
    port[enc0Pos][1] = enc2(port[enc0Pos][1], 7, e);   //SQ9FK 0..7 (7 = tryb BCD, bez martwej luki)
#else
    port[enc0Pos][1] = enc2(port[enc0Pos][1], 6, e);   //SQ9FK 0..6 (6 anten, bez pozycji BCD)
#endif
  }  
}


//=====[ Encoder2 ]========================== with interrupt

int enc2(int encPos, int range, int count) {
  encPos = encPos + e;
  if(encPos>range){
    encPos = 0;
  }
  if(encPos<0){
    encPos = range;
  }
  return encPos;
}


//=====[ show one LCD line ]=================
void show(int portNR) {
  //=====[ IN/OUT number ]==========
  lcd.setCursor(0, portNR);
  // SQ9FK: puste miejsce numeru dla OFF (poz. 0); przy BCD tez dla trybu BCD (poz. 7 = "M-off->BCD").
  // Anteny 1..6 pokazuja swoj numer. Poz. 7 istnieje tylko przy BCD_INPUT.
  if (port[portNR][1] == 0
#if defined(BCD_INPUT)
      || port[portNR][1] == 7
#endif
     ) {
    lcd.print("  ");
  } else {
    if (port[portNR][1] < 10) {
      lcd.print(' ');
    }
    lcd.print(port[portNR][1], DEC);
  }
  //=====[ Cursor ]=================
  for (i = 0; i < Ports; i++) {
    if (i == enc0Pos) {
      lcd.setCursor(2, i);
      if (menu1state == 1) {
        lcd.write(byte(2));
      } else if (port[i][4] == 1) { 
        if (millis() - Timeout[3][0] > (Timeout[3][1])) {
          lcd.write(byte(5));
        } else {
          lcd.write(byte(4));
        }
      } else {
        if (millis() - Timeout[3][0] > (Timeout[3][1])) {
          lcd.print(' ');
        } else {
          lcd.write(byte(3));
        }
      }
    } else {
      lcd.setCursor(2, i);
      if (port[i][4] == 1) {
        lcd.write(byte(5));
      } else {
        lcd.print(' ');
      }
    }
  }
  //=====[ Status ]=================
  lcd.setCursor(3, portNR);
  if (port[portNR][3] == 1 && (port[portNR][1] != 0)) {
    lcd.write(byte(0));
  } else if (port[portNR][1] == 0     // OFF (przy BCD tez tryb BCD, poz. 7) - bez wskaznika statusu
#if defined(BCD_INPUT)
             || port[portNR][1] == 7
#endif
            ) {
    lcd.print(' ');
  } else if (port[portNR][1] == port[portNR + 4][1]) {
    lcd.print('>');
  } else {
    lcd.print(' ');
  }

  lcd.setCursor(4, portNR);
#if defined(PTT_BLOCKING)
  if (port[portNR][2] == 1) {
    lcd.write(byte(1));
  } else
#endif
  {
    lcd.print(' ');
  }
  //=====[ Note ]=================
  lcd.setCursor(5, portNR);
  if (port[portNR][3] == 1 && port[portNR][1] != 0) {
    Note = "- (used)";
  } else {
    Note = antName(port[portNR][1]);
  }
  Note.remove(LCDculumn - 5);
  while (Note.length() < LCDculumn - 5) {
    Note += " ";
  }
  lcd.print(Note);
#if defined(BCD_INPUT)
  if (port[portNR][1] == 7) { // SQ9FK: poz. 7 = tryb BCD ("M-off->BCD") - wskaznik "M" + numer TRX
    lcd.setCursor(5, portNR);
    lcd.write(byte(2));
    lcd.setCursor(15, portNR);
    lcd.print(portNR + 1);
  }
#endif
}

//=====[ TX ]===================================================
void tx(byte addr, int portNR) {
  // SQ9FK: czysty one-hot 6 anten (projekt pierwotny). bit0..bit5 = anteny 1..6, bit6 wolny.
  // bit7 NIE nalezy juz do anteny - steruje nim niezaleznie Radio Flex (patrz nizej).
  switch (port[portNR + 3][1]) {
    case 0: a = B00000000; break;
    case 1: a = B00000001; break;
    case 2: a = B00000010; break;
    case 3: a = B00000100; break;
    case 4: a = B00001000; break;
    case 5: a = B00010000; break;
    case 6: a = B00100000; break;
    default: a = B00000000; break; //7/8 nieuzywane w wersji 6-antenowej
  }
  switch (port[portNR + 4][1]) {
    case 0: b = B00000000; break;
    case 1: b = B00000001; break;
    case 2: b = B00000010; break;
    case 3: b = B00000100; break;
    case 4: b = B00001000; break;
    case 5: b = B00010000; break;
    case 6: b = B00100000; break;
    default: b = B00000000; break;
  }
  // SQ9FK: GPA7/GPB7 = dwa niezalezne wyjscia Radio Flex (dawniej przekaznik pasma GXP11 40m).
  // Sterowane osobnymi przyciskami WWW (flexState[]), niezaleznie od wyboru anteny.
  if (flexState[0]) a |= (1 << 7);   // GPA7 -> Radio Flex 1 (Q1/K1/J7)
  if (flexState[1]) b |= (1 << 7);   // GPB7 -> Radio Flex 2 (Q2/K2/J6)
  Wire.beginTransmission(port[portNR + 4][0]);
  Wire.write(0x12);
  Wire.write((byte)a);
  Wire.endTransmission();
  Wire.beginTransmission(port[portNR + 4][0]);
  Wire.write(0x13);
  Wire.write((byte)b);
  Wire.endTransmission();
}

//=====[ Volt ]===================================================    
float volt(int raw) {
  float voltage = (raw * 5.0) / 1024.0 * 7.25 + 0.4;    // resistor coeficient 
  #if defined(serialECHO)
    Serial.print("Input voltage ");
    Serial.println(voltage);
  #endif
  return voltage;
}

//=====[ RX ]===================================================
#if defined(BCD_INPUT)
void rx(byte addr, int portNR, int PTTonly, int Bank) {
  Wire.beginTransmission(addr);
  Wire.write(0x12);
  Wire.endTransmission();
  Wire.requestFrom(addr, (byte)1);
#if defined(inputHigh)
  a = Wire.read();
#else
  a = ~Wire.read();
#endif
  Wire.beginTransmission(addr);
  Wire.write(0x13);
  Wire.endTransmission();
  Wire.requestFrom(addr, (byte)1);
  b = ~Wire.read();
  if (Bank == 1) {
#if defined(PTT_BLOCKING)
    if (b & (1 << 1)) {
      port[portNR][2] = 1;
    } else {
      port[portNR][2] = 0;
    }
#endif
    if (PTTonly == 0) {
      if (a & (1 << 0)) {
        a = a | (1 << 7);
      } else {
        a = a & ~(1 << 7);
      };
      if (a & (1 << 1)) {
        a = a | (1 << 6);
      } else {
        a = a & ~(1 << 6);
      };
      if (a & (1 << 2)) {
        a = a | (1 << 5);
      } else {
        a = a & ~(1 << 5);
      };
      if (a & (1 << 3)) {
        a = a | (1 << 4);
      } else {
        a = a & ~(1 << 4);
      };
      a = a >> 4;

      port[portNR][1] = pgm_read_byte(&BCDmatrixOUT[0][a]);
    }
  } else if (Bank == 2) {
#if defined(PTT_BLOCKING)
    if (b & (1 << 0)) {
      port[portNR][2] = 1;
    } else {
      port[portNR][2] = 0;
    }
#endif
    if (PTTonly == 0) {
      a = a >> 4;
      port[portNR][1] = pgm_read_byte(&BCDmatrixOUT[1][a]);

    }
  }
}
#endif   // BCD_INPUT

#if defined(OTRSP)
// SQ9FK: parser przyjmuje bufor komendy i cel odpowiedzi (Print&) zamiast na sztywno globalnego
// in_buf/Serial - dzieki temu ten sam kod obsluguje zarowno USB (Serial) jak i (opcja OTRSP_TCP)
// surowy klient TCP (EthernetClient), bo oba dziedzicza po Print. Zero duplikacji logiki komend.
static void OTRSP_parse(char *cmd, Print &out) {
//----------------------------------------------------------------------
// Parse and handle a command from the computer
//----------------------------------------------------------------------
    // Commands are not checked very thoroughly - the computer
    // should not send garbage.

#define COMPARE(command)  (memcmp_P(cmd, PSTR(command), \
                                    sizeof(command)-1) == 0)

#define QCOMPARE(command) (memcmp_P(cmd+1, PSTR(command), \
                                    sizeof(command)-1) == 0)

#define AFTER(command) (sizeof(command)-1)

// Handle the queries
    if (cmd[0] == '?') {

        // Check for 'ping' - just the ? by itself
        if (cmd[AFTER("?")] == '\0') {
            out.print(F("?\r"));
            return;
        }

        // Return AUX1 value
        if (QCOMPARE("AUX1")) {
            // SQ9FK: sama wartosc dziesietna (bylo: dopisywane zbedne "0" -> np. 3 stawalo sie "30",
            // co lamalo round-trip AUXn -> ?AUXn niezgodnie ze specyfikacja OTRSP).
            out.print(F("AUX1"));
            out.print(port[4][1]);
            out.print('\r');
            return;
        }

        // Return AUX2 value
        if (QCOMPARE("AUX2")) {
            out.print(F("AUX2"));
            out.print(port[5][1]);
            out.print('\r');
            return;
        }

        // Return the SO2R device's name
        if (QCOMPARE("NAME")) {
            out.print(F("NAME2x6SP9PDFRemoteAntennaSwitch\r"));
            return;
        }

        // Unknown query command
        out.print(cmd);
        out.print('\r');
        return;
    }

    // SQ9FK: AUX1/AUX2 scalone (byly niemal identycznym zdublowanym kodem - rozny tylko indeks
    // portu). Stara galaz "jesli wartosc sie zmienila" byla martwa (przypisanie tej samej wartosci
    // jest bezkosztowe, a komentowane efekty uboczne juz nie istnialy) - usunieta.
    if (COMPARE("AUX1") || COMPARE("AUX2")) {
        byte idx = (cmd[3] == '2') ? 1 : 0;               // "AUX1"->port[0], "AUX2"->port[1]
        int val = atoi((char *)&cmd[AFTER("AUXn")]) & 15;
        #if defined(OTRSP_DEBUG)
          Serial.print(F("Debug: ")); Serial.print(cmd);
          Serial.print(F(" idx=")); Serial.print(idx);
          Serial.print(F(" val=")); Serial.println(val);
        #endif
        port[idx][1] = val;
        return;
    }

    // Unknown commands are ignored, as are commands that try to
    // set something which is read-only such as NAME or CR0.
    return;
}

void serialEvent() {
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    // SQ9FK: OTRSP konczy komendy znakiem CR (\r, wg specyfikacji), NIE LF (\n) - bylo odwrotnie,
    // wiec parser nigdy nie widzial konca komendy od zgodnego z protokolem klienta (np. N1MM+).
    if (inChar == '\r') {
      in_buf[in_len] = '\0';
      in_len = 0;
      stringComplete = true;
    } else if (inChar == '\n') {
      // SQ9FK: ignoruj samotny LF (niektore programy wysylaja CRLF) - CR juz zakonczyl komende.
    } else if (in_len < sizeof(in_buf) - 1) {
      // SQ9FK: zabezpieczenie przed przepelnieniem in_buf (bylo bez kontroli granic - OOB write).
      in_buf[in_len++] = inChar;
    }
  }
}
#endif


