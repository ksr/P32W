// din96_tester.ino -- the DIN 96 shorts tester (docs/din96-tester.md, D129): an Arduino Nano and six
// MCP23S17 port expanders, one expander pin on each pin of a 96-pin DIN 41612 connector.
//
// For a board with nothing on it but its connectors, at soldering time: never one with power on it. Press
// TEST: each pin in turn is pulled low and all 96 are read; a pin that reads low is joined to the one pulled.
// GREEN: only the supply pins are joined, as the connector type says. RED: something else is, or a supply pin
// is not; the serial port (115200) says which pins, by place and by name.
//
// The connector type is the ONE lever of the TYPE switch that is on (each lever is a connector, named beside
// it on the board); with none on, no pin may be joined to another: the tester by itself.
//
// Type t and Return on the serial port to test as the button does; ? for the type the switch names.
//
// The logic is core.h (tested on the Mac: emulator/test/din96_test.c); the connector types and where each
// connector pin sits on the expanders are profiles.h, made by generators/card_din96.py. Do not edit that one.
// This file too runs on the Mac, against a model of the board (emulator/test/din96_sim.cc): make test.
#include <SPI.h>
#include <avr/pgmspace.h>
#define T_S(s) PSTR(s)
#include "core.h"
#include "profiles.h"

const uint8_t PIN_TEST = 2, PIN_PASS = 3, PIN_FAIL = 4;         // the button (to ground), the green and red LEDs
const uint8_t PIN_CS[6] = {5, 6, 7, 8, 9, 10};                  // an expander each: U1 to U6
const uint8_t PIN_TYPE[8] = {A0, A1, A2, A3, A4, A5, A6, A7};   // the TYPE switch's levers 1 to 8: on = to ground (10k up, RN1)
// For a line to follow a pull, and to come back up through the pull-ups (40 uA a pin at the least). The
// slowest is a big bare board's +5 V plane, dragged down through its capacitance to the ground plane (5 to
// 50 nF, by the layers between them) each time a ground pin is pulled, and brought back by the pull-ups of
// its own pins, six on a P32W J1: back above the high threshold (0.8 of the supply) in 1.7 ms with every
// figure at its worst, a tenth of that as a rule. Too short a wait would fail a good board, not pass a bad one
const unsigned SETTLE_US = 5000;

// MCP23S17 registers (IOCON.BANK = 0, as it wakes up): port A's; port B's is the next address
enum { IODIRA = 0x00, IPOLA = 0x02, GPINTENA = 0x04, DEFVALA = 0x06, INTCONA = 0x08, IOCON = 0x0A, GPPUA = 0x0C,
       GPIOA = 0x12, OLATA = 0x14 };
static const SPISettings spi(1000000, MSBFIRST, SPI_MODE0);
static uint8_t type_now = 255;

static void xwrite(uint8_t chip, uint8_t reg, uint8_t a, uint8_t b)   // two registers running: port A's, port B's
{
    SPI.beginTransaction(spi);
    digitalWrite(PIN_CS[chip], LOW);
    SPI.transfer(0x40); SPI.transfer(reg); SPI.transfer(a); SPI.transfer(b);
    digitalWrite(PIN_CS[chip], HIGH);
    SPI.endTransaction();
}
static void xwrite1(uint8_t chip, uint8_t reg, uint8_t v)             // one register
{
    SPI.beginTransaction(spi);
    digitalWrite(PIN_CS[chip], LOW);
    SPI.transfer(0x40); SPI.transfer(reg); SPI.transfer(v);
    digitalWrite(PIN_CS[chip], HIGH);
    SPI.endTransaction();
}
static void xread(uint8_t chip, uint8_t reg, uint8_t *a, uint8_t *b)
{
    SPI.beginTransaction(spi);
    digitalWrite(PIN_CS[chip], LOW);
    SPI.transfer(0x41); SPI.transfer(reg); *a = SPI.transfer(0); *b = SPI.transfer(0);
    digitalWrite(PIN_CS[chip], HIGH);
    SPI.endTransaction();
}
// every pin an input with its pull-up, and its output latch low: making a pin an output pulls it low.
// 0: all six expanders answer; else the first that does not, 1 to 6
static uint8_t expanders()
{
    for (uint8_t c = 0; c < 6; c++) {
        uint8_t a, b;
        // Whatever state the chip is in. Its reset is the Nano's, so it should be as it woke; but its select
        // line floats while the Nano is in its bootloader, which blinks D13, the clock: stray clocks may have
        // reached it, and left its registers in the other arrangement (IOCON.BANK: IOCON is then at 05, not
        // 0A) or not stepping from port A's to port B's (IOCON.SEQOP). So IOCON first, at both places, a
        // register at a time; then every register that matters
        xwrite1(c, 0x05, 0x00); xwrite1(c, IOCON, 0x00);
        xwrite(c, IODIRA, 0xFF, 0xFF);
        xwrite(c, IPOLA, 0x00, 0x00);
        xwrite(c, GPINTENA, 0x00, 0x00);
        xwrite(c, INTCONA, 0x00, 0x00);
        xwrite(c, GPPUA, 0xFF, 0xFF);
        xwrite(c, OLATA, 0x00, 0x00);
        xwrite(c, DEFVALA, 0x5A, 0xC3);                         // (a register with nothing to do here: is the chip there?)
        xread(c, DEFVALA, &a, &b);
        xwrite(c, DEFVALA, 0, 0);
        if (a != 0x5A || b != 0xC3) return c + 1;
    }
    delayMicroseconds(SETTLE_US);                               // (a pull-up only now turned on: let its line come up)
    return 0;
}

void t_probe(uint8_t p, uint8_t low[T_BYTES])
{
    uint8_t raw[12], at = 0;
    if (p != 255) {
        at = pgm_read_byte(&t_map[p]);                          // 16 * expander + 8 * port + bit
        uint8_t dir[2] = {0xFF, 0xFF};
        dir[(at >> 3) & 1] = ~(1 << (at & 7));
        xwrite(at >> 4, IODIRA, dir[0], dir[1]);
        delayMicroseconds(SETTLE_US);
    }
    for (uint8_t c = 0; c < 6; c++) xread(c, GPIOA, &raw[2 * c], &raw[2 * c + 1]);
    if (p != 255) {
        xwrite(at >> 4, IODIRA, 0xFF, 0xFF);
        delayMicroseconds(SETTLE_US);
    }
    memset(low, 0, T_BYTES);
    for (uint8_t q = 0; q < T_PINS; q++) {
        uint8_t m = pgm_read_byte(&t_map[q]);
        if (!((raw[m >> 3] >> (m & 7)) & 1)) low[q >> 3] |= 1 << (q & 7);
    }
}
void t_say(const char *s) { Serial.print((const __FlashStringHelper *)s); }
void t_say_num(uint8_t n) { Serial.print(n); }
void t_say_end(void) { Serial.println(); }
void t_say_pin(uint8_t p)                                       // b26 (-MWR)
{
    Serial.print((char)('a' + p % 3)); Serial.print(p / 3 + 1);
    const char *names = type_now < T_NTYPES ? (const char *)pgm_read_ptr(&t_pinnames[type_now]) : 0;
    if (!names) return;
    for (uint8_t k = 0; k < p; k++) names += strlen_P(names) + 1;
    Serial.print(F(" (")); Serial.print((const __FlashStringHelper *)names); Serial.print(')');
}

// The type the TYPE switch names: lever k alone on is type k (profiles.h has them in the levers' order),
// none on is type 0, the tester by itself. 255: more than one lever on, or a lever with no type.
static uint8_t switches()
{
    uint8_t on = 0, type = 0;
    for (uint8_t k = 0; k < 8; k++) {
        // (A6 and A7 are analog inputs only: read as a voltage, low with their lever on)
        bool low = k < 6 ? digitalRead(PIN_TYPE[k]) == LOW : analogRead(PIN_TYPE[k]) < 512;
        if (low) { on++; type = k + 1; }
    }
    return on > 1 || type >= T_NTYPES ? 255 : type;
}
static void say_type(uint8_t type)
{
    Serial.print(F("TYPE: "));
    if (type < T_NTYPES) Serial.println((const __FlashStringHelper *)pgm_read_ptr(&t_typename[type]));
    else Serial.println(F("set ONE lever of the TYPE switch, the connector's (5 to 8 are spare); none for the tester by itself"));
}
static void trouble(uint8_t chip)                               // an expander does not answer: the two LEDs in turn, for good
{
    Serial.print(F("expander U")); Serial.print(chip); Serial.println(F(" does not answer: no test"));
    for (;;) {
        digitalWrite(PIN_PASS, HIGH); digitalWrite(PIN_FAIL, LOW); delay(250);
        digitalWrite(PIN_PASS, LOW); digitalWrite(PIN_FAIL, HIGH); delay(250);
    }
}
static void test()
{
    uint8_t sw = switches(), nv = 0, ng = 0, bad;
    digitalWrite(PIN_PASS, LOW); digitalWrite(PIN_FAIL, LOW);
    if ((bad = expanders()) != 0) trouble(bad);
    say_type(sw);
    if (sw >= T_NTYPES) { digitalWrite(PIN_FAIL, HIGH); return; }
    type_now = sw;
    for (uint8_t p = 0; p < T_PINS; p++)
        t_class[p] = (pgm_read_byte(&t_typeclass[sw][p >> 2]) >> (2 * (p & 3))) & 3;
    uint8_t faults = t_test(&nv, &ng);
    if (faults) {
        Serial.print(F("FAIL: ")); Serial.print(faults); Serial.println(faults == 1 ? F(" fault") : F(" faults"));
        digitalWrite(PIN_FAIL, HIGH);
    } else {
        Serial.print(F("PASS: +5V ")); Serial.print(nv); Serial.print(F(" pins, GND ")); Serial.print(ng);
        Serial.println(F(" pins, no other pin joined to another"));
        digitalWrite(PIN_PASS, HIGH);
    }
}

void setup()
{
    pinMode(PIN_TEST, INPUT_PULLUP);
    pinMode(PIN_PASS, OUTPUT); pinMode(PIN_FAIL, OUTPUT);
    for (uint8_t k = 0; k < 6; k++) pinMode(PIN_TYPE[k], INPUT);
    // (high first, then an output: the other way round the line is low for a moment, an expander selected)
    for (uint8_t c = 0; c < 6; c++) { digitalWrite(PIN_CS[c], HIGH); pinMode(PIN_CS[c], OUTPUT); }
    SPI.begin();
    Serial.begin(115200);
    Serial.println(F("DIN 96 shorts tester (P32W D129): TEST, or t and Return; ? for the type set"));
    digitalWrite(PIN_PASS, HIGH); digitalWrite(PIN_FAIL, HIGH); delay(300);    // both LEDs, to show they work
    digitalWrite(PIN_PASS, LOW); digitalWrite(PIN_FAIL, LOW);
    uint8_t bad = expanders();
    if (bad) trouble(bad);
    say_type(switches());
}

void loop()
{
    static uint8_t was = HIGH;
    uint8_t now = digitalRead(PIN_TEST);
    if (now == LOW && was == HIGH) { delay(30); if (digitalRead(PIN_TEST) == LOW) test(); }
    was = now;
    if (Serial.available()) {
        char ch = Serial.read();
        if (ch == 't' || ch == 'T') test();
        if (ch == '?') say_type(switches());
    }
}
