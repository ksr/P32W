// din96_sim.cc -- the DIN 96 shorts tester's own sketch (hardware/din96-tester/firmware/din96_tester/
// din96_tester.ino, D129) run on the host against a model of its hardware: six MCP23S17s on a SPI bus with a
// select line each, the TEST button, the TYPE switch, the two LEDs, and a made-up board on the connector.
//
// What core.h's own test (din96_test.c) cannot see: the registers the sketch writes, the place of each
// connector pin on the expanders (profiles.h against the netlist's wiring, which this reads from
// card_din96.py --wiring, not from profiles.h), the pins' names, the TYPE switch, the LEDs, and what the
// tester does to the board under test: it must never drive a pin high, never pull two at once.
//
//   c++ -std=c++11 -I din96_stub -I ../../hardware/din96-tester/firmware/din96_tester din96_sim.cc -o din96_sim
//   ./din96_sim WIRING ../../hardware/din96-tester/pins
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <fstream>
#include <sstream>

// ---------------------------------------------------------------- the model
struct Chip { uint8_t reg[0x16]; bool there; int n; uint8_t op, addr; };
static Chip chip[6];
static int cs_level[6], cs_port[6], cs_mode[6];         // the Nano's D5-D10: the pin, its PORT bit, output or not
static int wire[6][2][8];                               // expander, port (0 A, 1 B), bit -> connector pin 0-95, from the netlist
static int group_of[96];                                // the board: pins of one group are joined
static int held_low = -1;                               // a pin held low from outside
static int levers = 0, button = 1, led_pass, led_fail, blinks;
static std::string out, keys;
static long violations, floating_reads, probes;
static std::vector<std::string> why;
static void violate(const char *s) { if (violations++ < 5) why.push_back(s); }

enum { R_IODIR = 0x00, R_IPOL = 0x02, R_IOCON = 0x0A, R_GPPU = 0x0C, R_GPIO = 0x12, R_OLAT = 0x14 };
static int canon(const Chip &c, int a)                  // an address as the chip takes it -> its BANK = 0 address
{
    if (!(c.reg[R_IOCON] & 0x80)) return a <= 0x15 ? a : -1;
    if (a <= 0x0A) return 2 * a;                        // BANK = 1: port A's at 00-0A, port B's at 10-1A
    if (a >= 0x10 && a <= 0x1A) return 2 * (a - 0x10) + 1;
    return -1;
}
static int pin_level(int pin)                           // a connector pin as the expanders see it
{
    int low = 0, high = 0, pulled = 0;
    for (int c = 0; c < 6; c++)
        for (int p = 0; p < 2; p++)
            for (int b = 0; b < 8; b++) {
                if (!chip[c].there || group_of[wire[c][p][b]] != group_of[pin]) continue;
                if (!((chip[c].reg[R_IODIR + p] >> b) & 1)) { if ((chip[c].reg[R_OLAT + p] >> b) & 1) high++; else low++; }
                else if ((chip[c].reg[R_GPPU + p] >> b) & 1) pulled++;
            }
    if (high) violate("a connector pin was driven HIGH");
    if (held_low >= 0 && group_of[held_low] == group_of[pin]) return 0;
    if (low) return 0;
    if (high) return 1;
    if (!pulled) { floating_reads++; return 0; }       // (nothing holds it: read as low, so a forgotten pull-up shows)
    return 1;
}
static void check_outputs(void)                         // never two connector pins pulled at once
{
    int n = 0;
    for (int c = 0; c < 6; c++)
        for (int p = 0; p < 2; p++)
            for (int b = 0; b < 8; b++) if (chip[c].there && !((chip[c].reg[R_IODIR + p] >> b) & 1)) n++;
    if (n > 1) violate("two connector pins were outputs at once");
    if (n == 1) probes++;
}
static uint8_t spi_byte(uint8_t v)
{
    int sel = -1, nsel = 0;
    for (int c = 0; c < 6; c++) if (!cs_level[c]) { sel = c; nsel++; }
    if (nsel != 1) { violate(nsel ? "two expanders selected at once" : "a SPI byte with no expander selected"); return 0xFF; }
    Chip &c = chip[sel];
    if (!c.there) return 0xFF;
    uint8_t r = 0;
    if (c.n == 0) c.op = v;
    else if (c.n == 1) c.addr = v;
    else if ((c.op & 0xFE) == 0x40) {                   // (HAEN off: the address 000 only)
        int a = canon(c, c.addr);
        if (a >= 0) {
            if (c.op & 1) r = (a == R_GPIO || a == R_GPIO + 1) ? [&]{ uint8_t x = 0; for (int b = 0; b < 8; b++) x |= pin_level(wire[sel][a - R_GPIO][b]) << b;
                                                                      return (uint8_t)(x ^ c.reg[R_IPOL + a - R_GPIO]); }()     // (as its polarity register has it)
                                                               : c.reg[a == R_IOCON + 1 ? R_IOCON : a];
            else { c.reg[a == R_IOCON + 1 ? R_IOCON : a] = v; if (a == R_GPIO || a == R_GPIO + 1) c.reg[R_OLAT + a - R_GPIO] = v; check_outputs(); }
        }
        if (!(c.reg[R_IOCON] & 0x20)) c.addr++;         // (IOCON.SEQOP set: the address stays)
    }
    c.n++;
    return r;
}

// ---------------------------------------------------------------- what the sketch has of an Arduino
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define MSBFIRST 1
#define SPI_MODE0 0
#define PROGMEM
#define PSTR(s) (s)
enum { A0 = 14, A1, A2, A3, A4, A5, A6, A7 };
class __FlashStringHelper;
#define F(s) ((const __FlashStringHelper *)(s))
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#define pgm_read_ptr(p) (*(const void *const *)(p))
#define strlen_P strlen
struct TroubleBlink {};
static void pinMode(uint8_t pin, uint8_t mode)
{
    if (pin >= 5 && pin <= 10) {
        cs_mode[pin - 5] = mode == OUTPUT;
        if (mode == OUTPUT) { cs_level[pin - 5] = cs_port[pin - 5]; if (!cs_level[pin - 5]) violate("a select line went low as it was made an output"); }
    }
}
static void digitalWrite(uint8_t pin, uint8_t v)
{
    if (pin >= 5 && pin <= 10) { cs_port[pin - 5] = v; if (cs_mode[pin - 5]) { if (cs_level[pin - 5] && !v) chip[pin - 5].n = 0; cs_level[pin - 5] = v; } }
    if (pin == 3) led_pass = v;
    if (pin == 4) led_fail = v;
}
static int digitalRead(uint8_t pin)
{
    if (pin == 2) return button;
    if (pin >= A0 && pin <= A5) return (levers >> (pin - A0)) & 1 ? LOW : HIGH;
    return HIGH;
}
static int analogRead(uint8_t pin) { return (pin == A6 || pin == A7) && ((levers >> (pin - A0)) & 1) ? 4 : 1012; }
static void delay(unsigned long ms) { if (ms == 250 && ++blinks >= 6) throw TroubleBlink(); }
static void delayMicroseconds(unsigned) {}
struct SPISettings { SPISettings(long, int, int) {} };
static struct { void begin() {} void beginTransaction(const SPISettings &) {} void endTransaction() {} uint8_t transfer(uint8_t v) { return spi_byte(v); } } SPI;
static struct SerialT {
    void begin(long) {}
    void print(const __FlashStringHelper *s) { out += (const char *)s; }
    void print(const char *s) { out += s; }
    void print(char c) { out += c; }
    void print(int n) { out += std::to_string(n); }
    void print(uint8_t n) { out += std::to_string((int)n); }
    void println() { out += "\n"; }
    template <class T> void println(T x) { print(x); out += "\n"; }
    int available() { return !keys.empty(); }
    int read() { char c = keys[0]; keys.erase(0, 1); return c; }
} Serial;

#include "din96_tester.ino"

// ---------------------------------------------------------------- the tests
static int nchecks, nbad;
static void check(bool ok, const std::string &what)
{
    nchecks++;
    if (!ok) { nbad++; printf("FAILED: %s\n%s", what.c_str(), out.c_str()); }
}
static int pin(const std::string &s) { return 3 * (atoi(s.c_str() + 1) - 1) + (s[0] | 32) - 'a'; }
static std::string place(int p) { return std::string(1, 'a' + p % 3) + std::to_string(p / 3 + 1); }
struct Conn { std::string sig[96]; bool gndok[96]; };
static std::map<std::string, Conn> conns;               // "J1" ... from the pin tables
static void read_pins(const std::string &path)
{
    std::ifstream f(path);
    std::string line;
    if (!f) { printf("cannot read %s\n", path.c_str()); exit(2); }
    while (std::getline(f, line)) {
        std::istringstream ss(line.substr(0, line.find('#')));
        std::string c, p, s, x;
        if (!(ss >> c >> p >> s)) continue;
        Conn &k = conns[c];
        k.sig[pin(p)] = s; k.gndok[pin(p)] = bool(ss >> x) && x == "gnd-ok";
    }
}
static bool vcc(const Conn &c, int p) { return c.sig[p] == "VCC" || c.sig[p] == "+5V"; }
static bool gnd(const Conn &c, int p) { return c.sig[p] == "GND"; }
static void bare(void) { for (int p = 0; p < 96; p++) group_of[p] = p; held_low = -1; }
static void join(int a, int b) { int ga = group_of[a], gb = group_of[b]; for (int p = 0; p < 96; p++) if (group_of[p] == gb) group_of[p] = ga; }
static void board(const Conn &c)                        // a good board: its supply pins joined, nothing else
{
    int v = -1, g = -1;
    bare();
    for (int p = 0; p < 96; p++) {
        if (vcc(c, p)) { if (v < 0) v = p; else join(v, p); }
        if (gnd(c, p)) { if (g < 0) g = p; else join(g, p); }
    }
}
static void press(void) { out.clear(); led_pass = led_fail = 0; button = 0; loop(); button = 1; loop(); }
static bool has(const std::string &s) { return out.find(s) != std::string::npos; }
static bool token(const std::string &s)                 // a pin's place as a whole word of the report
{
    for (size_t at = out.find(s); at != std::string::npos; at = out.find(s, at + 1)) {
        char b = at ? out[at - 1] : ' ', e = at + s.size() < out.size() ? out[at + s.size()] : ' ';
        if (!isalnum((unsigned char)b) && !isalnum((unsigned char)e)) return true;
    }
    return false;
}

int main(int argc, char **argv)
{
    if (argc != 3) { printf("usage: din96_sim WIRING PINS-DIRECTORY\n"); return 2; }
    {   // the netlist's wiring: "A1 1 B 0"
        std::ifstream f(argv[1]);
        std::string p, port; int u, bit, n = 0;
        memset(wire, -1, sizeof wire);
        while (f >> p >> u >> port >> bit) { wire[u - 1][port == "B"][bit] = pin(p); n++; }
        bool whole = n == 96;
        for (int c = 0; c < 6; c++) for (int q = 0; q < 2; q++) for (int b = 0; b < 8; b++) if (wire[c][q][b] < 0) whole = false;
        if (!whole) { printf("din96_sim: %s is not the wiring of 96 pins\n", argv[1]); return 2; }
    }
    read_pins(std::string(argv[2]) + "/p32w.txt"); read_pins(std::string(argv[2]) + "/yacc1.txt");
    const char *order[] = {"J1", "J2", "J3", "X1"};                 // the TYPE switch's levers 1-4
    for (int c = 0; c < 6; c++) { memset(&chip[c], 0, sizeof chip[c]); chip[c].there = true; chip[c].reg[0] = chip[c].reg[1] = 0xFF; }
    // two expanders wake up as stray clocks before the sketch runs could leave them: one with its registers
    // in the other arrangement and not stepping (IOCON.BANK, SEQOP), a pin an output and high, a port read
    // upside down; the other not stepping. The sketch must put both right before it tests
    chip[2].reg[R_IOCON] = 0xA0; chip[2].reg[R_IODIR] = 0xFE; chip[2].reg[R_OLAT] = 0x01; chip[2].reg[R_IPOL] = 0xFF;
    chip[4].reg[R_IOCON] = 0x20; chip[4].reg[R_IPOL + 1] = 0x0F; chip[4].reg[R_GPPU] = 0x00;
    bare();
    violations = 0;
    setup();
    check(violations == 0 && !led_pass && !led_fail, "the start-up: " + (why.empty() ? std::string("LEDs") : why[0]));
    check(has("TYPE: the tester by itself"), "the type said at the start");

    levers = 0; press();
    check(led_pass && !led_fail && has("PASS"), "nothing plugged in, no lever on: PASS");
    check(probes >= 96, "96 pins pulled, one at a time");
    join(pin("a1"), pin("c32")); press();
    check(led_fail && !led_pass && token("a1") && token("c32"), "no lever on: any join is a fault");

    for (int k = 0; k < 4; k++) {                                   // each connector type, its own good board
        const Conn &c = conns[order[k]];
        int nv = 0, ng = 0;
        for (int p = 0; p < 96; p++) { nv += vcc(c, p); ng += gnd(c, p); }
        levers = 1 << k; board(c); press();
        check(led_pass && !led_fail && has("PASS: +5V " + std::to_string(nv) + " pins, GND " + std::to_string(ng) + " pins"),
              std::string("a good ") + order[k] + " on lever " + std::to_string(k + 1));
        for (int j = 0; j < 4; j++)                                 // ... and no other type passes it, but J1 as J3
            if (j != k) {
                bool same = (k == 0 && j == 2) || (k == 2 && j == 0);   // (J1 and J3: the same supply pins)
                levers = 1 << j; press();
                check(same ? led_pass : (led_fail && !led_pass), std::string("a good ") + order[k] + " tested as " + order[j]);
            }
    }
    // the report names the pins: by place, and by the pin table's own names
    levers = 2; board(conns["J2"]); join(pin("a16"), pin("a17")); press();
    check(led_fail && has("joined to each other: a16 (" + conns["J2"].sig[pin("a16")] + "), a17 (" + conns["J2"].sig[pin("a17")] + ")") && has("FAIL: 1 fault"),
          "J2: a16 and a17 joined, named");
    levers = 4; board(conns["J3"]); join(pin("b28"), pin("a31")); press();
    check(led_pass, "J3: b28 tied to ground, as the CPU card has it");
    levers = 1; board(conns["J1"]); join(pin("b28"), pin("a31")); press();
    check(led_fail && has("joined to GND: b28 (" + conns["J1"].sig[pin("b28")] + ")"), "J1: b28 on ground is a fault there");
    levers = 8; board(conns["X1"]); join(pin("a2"), pin("a1")); press();
    check(led_fail && has("+5V and GND are joined"), "YACC1: +5 V on ground");
    levers = 1; board(conns["J1"]); held_low = pin("c9"); press();
    check(led_fail && has("held low with nothing pulling it: c9"), "a pin held low from outside");

    // the TYPE switch: two levers, a spare lever (5 to 8; 7 and 8 are read as voltages)
    board(conns["J1"]);
    { long before = probes; levers = 3; press(); check(led_fail && !led_pass && has("set ONE lever") && probes == before, "two levers on: no test"); }
    for (int k = 4; k < 8; k++) {
        long before = probes; levers = 1 << k; press();
        check(led_fail && has("set ONE lever") && probes == before, "spare lever " + std::to_string(k + 1) + " on: no test");
    }
    levers = 0x81; press(); check(has("set ONE lever"), "levers 1 and 8 on: lever 8 is seen");
    levers = 1; out.clear(); keys = "?"; loop(); check(has("TYPE: P32W J1"), "? on the serial port says the type");
    out.clear(); led_pass = 0; keys = "t"; loop(); check(led_pass && has("PASS"), "t on the serial port tests");

    // every pair of pins of a J2, joined in turn: each judged rightly, and the pins named are the pair's
    int wrong = 0, misnamed = 0;
    levers = 2;
    const Conn &j2 = conns["J2"];
    for (int a = 0; a < 96; a++)
        for (int b = a + 1; b < 96; b++) {
            bool sa = vcc(j2, a) || gnd(j2, a), sb = vcc(j2, b) || gnd(j2, b);
            bool fine = (vcc(j2, a) && vcc(j2, b)) || (gnd(j2, a) && gnd(j2, b));
            board(j2); join(a, b); press();
            if (bool(led_pass) != fine || bool(led_fail) == fine) wrong++;
            else if (!fine && ((!sa && !token(place(a))) || (!sb && !token(place(b))))) misnamed++;
        }
    check(wrong == 0, std::to_string(wrong) + " of the 4560 pairs judged wrongly");
    check(misnamed == 0, std::to_string(misnamed) + " of the 4560 pairs: the report does not name the pins joined");

    check(violations == 0, "what the tester did to the board: " + (why.empty() ? std::string("") : why[0]));
    check(floating_reads == 0, std::to_string(floating_reads) + " pins read with nothing holding them (a pull-up not set)");

    // an expander that does not answer: no test, the two LEDs in turn
    chip[3].there = false; blinks = 0; out.clear();
    bool blinked = false;
    try { press(); } catch (TroubleBlink &) { blinked = true; }
    check(blinked && has("expander U4 does not answer"), "an expander missing: said, and the LEDs blink");

    if (nbad) { printf("din96 tester's sketch: %d of %d checks FAILED\n", nbad, nchecks); return 1; }
    printf("DIN 96 tester's sketch on modelled expanders: %d checks; the netlist's wiring, every type, every one of the "
           "4560 pairs named; never a pin driven high, never two pulled at once\n", nchecks);
    return 0;
}
