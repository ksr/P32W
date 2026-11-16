/* profiles.h -- the connector types, and where each connector pin sits on the expanders.
 * WRITTEN BY generators/card_din96.py --firmware from the tester's netlist and hardware/din96-tester/pins:
 * do not edit. */
#define T_NTYPES 5
static const char t_tn0[] PROGMEM = "the tester by itself: no pin may be joined to another";
static const char t_tn1[] PROGMEM = "P32W J1";
static const char t_tn2[] PROGMEM = "P32W J2";
static const char t_tn3[] PROGMEM = "P32W J3";
static const char t_tn4[] PROGMEM = "YACC1 X1";
static const char *const t_typename[T_NTYPES] PROGMEM = {t_tn0, t_tn1, t_tn2, t_tn3, t_tn4};
static const uint8_t t_typeclass[T_NTYPES][24] PROGMEM = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x55, 0x85, 0x00, 0x08, 0x80, 0x00, 0x08, 0x80, 0x00, 0x08, 0x80, 0x00, 0x08, 0x80, 0x00, 0x08, 0x80, 0x00, 0x08, 0x80, 0x00, 0x08, 0xA0, 0xAA},
    {0x55, 0x85, 0x10, 0x08, 0x81, 0x10, 0x08, 0x81, 0x10, 0x08, 0x81, 0x10, 0x08, 0x81, 0x10, 0x08, 0x81, 0x10, 0x08, 0x81, 0x10, 0x08, 0xA1, 0xAA},
    {0x55, 0x85, 0x00, 0x08, 0x80, 0x00, 0x08, 0x80, 0x00, 0x08, 0x80, 0x00, 0x08, 0x80, 0x00, 0x08, 0x80, 0x00, 0x08, 0x80, 0x30, 0x08, 0xA3, 0xAA},
    {0x6A, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0xA9}};
static const char t_pn1[] PROGMEM =
    "+5V\0" "+5V\0" "+5V\0" "+5V\0" "+5V\0" "+5V\0" "D0\0" "GND\0" "D28\0" "D1\0" "A26\0" "D29\0"
    "D2\0" "GND\0" "D30\0" "D3\0" "A27\0" "D31\0" "D4\0" "GND\0" "A2\0" "D5\0" "A28\0" "A3\0"
    "D6\0" "GND\0" "A4\0" "D7\0" "A29\0" "A5\0" "D8\0" "GND\0" "A6\0" "D9\0" "A30\0" "A7\0"
    "D10\0" "GND\0" "A8\0" "D11\0" "A31\0" "A9\0" "D12\0" "GND\0" "A10\0" "D13\0" "BE0\0" "A11\0"
    "D14\0" "GND\0" "A12\0" "D15\0" "BE1\0" "A13\0" "D16\0" "GND\0" "A14\0" "D17\0" "BE2\0" "A15\0"
    "D18\0" "GND\0" "A16\0" "D19\0" "BE3\0" "A17\0" "D20\0" "GND\0" "A18\0" "D21\0" "-MRD\0" "A19\0"
    "D22\0" "GND\0" "A20\0" "D23\0" "-MWR\0" "A21\0" "D24\0" "GND\0" "A22\0" "D25\0" "-ACK\0" "A23\0"
    "D26\0" "GND\0" "A24\0" "D27\0" "-WAIT\0" "A25\0" "GND\0" "GND\0" "GND\0" "GND\0" "GND\0" "GND\0";
static const char t_pn2[] PROGMEM =
    "+5V\0" "+5V\0" "+5V\0" "+5V\0" "+5V\0" "+5V\0" "CLK\0" "GND\0" "-BREQ3\0" "-CLK\0" "+5V\0" "-BREQ4\0"
    "-RES\0" "GND\0" "-BREQ5\0" "PWROK\0" "+5V\0" "-BREQ6\0" "-LOCK\0" "GND\0" "-BREQ7\0" "-IRQ0\0" "+5V\0" "-BREQ8\0"
    "-IRQ1\0" "GND\0" "-BREQ9\0" "-IRQ2\0" "+5V\0" "-BREQ10\0" "-IRQ3\0" "GND\0" "-BGNT3\0" "-IRQ4\0" "+5V\0" "-BGNT4\0"
    "-IRQ5\0" "GND\0" "-BGNT5\0" "-IRQ6\0" "+5V\0" "-BGNT6\0" "-IRQ7\0" "GND\0" "-BGNT7\0" "-BREQ\0" "+5V\0" "-BGNT8\0"
    "-BGNT\0" "GND\0" "-BGNT9\0" "-IACK\0" "+5V\0" "-BGNT10\0" "-FAST\0" "GND\0" "-BREQ0\0" "XCLK\0" "+5V\0" "-BGNT0\0"
    "-XCLKEN\0" "GND\0" "SPARE_J2C21\0" "SPARE_J2A22\0" "+5V\0" "SPARE_J2C22\0" "SPARE_J2A23\0" "GND\0" "SPARE_J2C23\0" "SPARE_J2A24\0" "+5V\0" "SPARE_J2C24\0"
    "SPARE_J2A25\0" "GND\0" "SPARE_J2C25\0" "SPARE_J2A26\0" "+5V\0" "SPARE_J2C26\0" "SPARE_J2A27\0" "GND\0" "SPARE_J2C27\0" "SPARE_J2A28\0" "+5V\0" "SPARE_J2C28\0"
    "SPARE_J2A29\0" "GND\0" "SPARE_J2C29\0" "SPARE_J2A30\0" "+5V\0" "SPARE_J2C30\0" "GND\0" "GND\0" "GND\0" "GND\0" "GND\0" "GND\0";
static const char t_pn3[] PROGMEM =
    "+5V\0" "+5V\0" "+5V\0" "+5V\0" "+5V\0" "+5V\0" "LR0\0" "GND\0" "LR28\0" "LR1\0" "FAULT\0" "LR29\0"
    "LR2\0" "GND\0" "LR30\0" "LR3\0" "FC0\0" "LR31\0" "LR4\0" "GND\0" "RSRC0\0" "LR5\0" "FC1\0" "RSRC1\0"
    "LR6\0" "GND\0" "RSRC2\0" "LR7\0" "FC2\0" "RSRC3\0" "LR8\0" "GND\0" "RDST0\0" "LR9\0" "FC3\0" "RDST1\0"
    "LR10\0" "GND\0" "RDST2\0" "LR11\0" "-WSTB\0" "MEM0\0" "LR12\0" "GND\0" "MEM1\0" "LR13\0" "STALL\0" "MEM2\0"
    "LR14\0" "GND\0" "SIZE0\0" "LR15\0" "LOCKREQ\0" "SIZE1\0" "LR16\0" "GND\0" "SIGNED\0" "LR17\0" "CCLK\0" "CRSEL0\0"
    "LR18\0" "GND\0" "CRSEL1\0" "LR19\0" "PCINC\0" "CRSEL2\0" "LR20\0" "GND\0" "SUPER\0" "LR21\0" "MARPC\0" "TLBW\0"
    "LR22\0" "GND\0" "SPARE_J3C25\0" "LR23\0" "MARINC\0" "SPARE_J3C26\0" "LR24\0" "GND\0" "SPARE_J3C27\0" "LR25\0" "-CPUIN\0" "SPARE_J3C28\0"
    "LR26\0" "GND\0" "SPARE_J3C29\0" "LR27\0" "-MMUIN\0" "SPARE_J3C30\0" "GND\0" "GND\0" "GND\0" "GND\0" "GND\0" "GND\0";
static const char t_pn4[] PROGMEM =
    "GND\0" "GND\0" "GND\0" "+5V\0" "+5V\0" "+5V\0" "ADDR0\0" "DATA12\0" "ADDR-REG-ID0\0" "ADDR1\0" "DATA13\0" "ADDR-REG-ID1\0"
    "ADDR2\0" "DATA14\0" "ADDR-REG-ID2\0" "ADDR3\0" "DATA15\0" "ADDR-REG-ID3\0" "ADDR4\0" "-REG-FUNC-RD\0" "IO-ADDR0\0" "ADDR5\0" "-REG-FUNC-LD\0" "IO-ADDR1\0"
    "ADDR6\0" "REG-RD-ID0\0" "IO-ADDR2\0" "ADDR7\0" "REG-RD-ID1\0" "IO-ADDR3\0" "ADDR8\0" "REG-RD-ID2\0" "-IO-ADDR-LD\0" "ADDR9\0" "REG-RD-ID3\0" "-VMA\0"
    "ADDR10\0" "REG-LD-ID0\0" "-INT\0" "ADDR11\0" "REG-LD-ID1\0" "-INTA\0" "ADDR12\0" "REG-LD-ID2\0" "-ALU-FUNC\0" "ADDR13\0" "REG-LD-ID3\0" "ALU0\0"
    "ADDR14\0" "-REG-RD-LO\0" "ALU1\0" "ADDR15\0" "-REG-LD-LO\0" "ALU2\0" "DATA0\0" "-REG-RD-HI\0" "ALU3\0" "DATA1\0" "-REG-LD-HI\0" "-AC-LD-INV\0"
    "DATA2\0" "-REG-DN\0" "-AC-RD\0" "DATA3\0" "-REG-UP\0" "-AC-LD\0" "DATA4\0" "-MEM-RD\0" "-SR-LD\0" "DATA5\0" "-MEM-WR\0" "BR-COND\0"
    "DATA6\0" "-IO-RD\0" "-HL-SWAP\0" "DATA7\0" "-IO-WR\0" "IN\0" "DATA8\0" "-TMP-REG-RD0\0" "OUT\0" "DATA9\0" "-TMP-REG-LD0\0" "-BUS-EN\0"
    "DATA10\0" "-TMP-REG-RD1\0" "-RUN\0" "DATA11\0" "-TMP-REG-LD1\0" "-RESET\0" "+5V\0" "+5V\0" "+5V\0" "GND\0" "GND\0" "GND\0";
static const char *const t_pinnames[T_NTYPES] PROGMEM = {0, t_pn1, t_pn2, t_pn3, t_pn4};
/* 16 * (expander - 1) + 8 * (port B) + bit, for pins a1 b1 c1 a2 ... c32 */
static const uint8_t t_map[96] PROGMEM = {
     8,  9, 10, 11, 12, 13, 14, 15,  7,  6,  5,  4,  3,  2,  1,  0,
    24, 25, 26, 27, 28, 29, 30, 31, 23, 22, 21, 20, 19, 18, 17, 16,
    40, 41, 42, 43, 44, 45, 46, 47, 39, 38, 37, 36, 35, 34, 33, 32,
    56, 57, 58, 59, 60, 61, 62, 63, 55, 54, 53, 52, 51, 50, 49, 48,
    72, 73, 74, 75, 76, 77, 78, 79, 71, 70, 69, 68, 67, 66, 65, 64,
    88, 89, 90, 91, 92, 93, 94, 95, 87, 86, 85, 84, 83, 82, 81, 80};
