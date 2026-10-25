// p32alu_top.v -- the ALU's FPGA: a Sipeed Tang Nano 20K module
// (GW2AR-LV18QN88C8/I7) on the CPU card's headers (D21). Its pin
// constraints come with the CPU card's netlist.
//
// The module has 34 I/O pins, so the 32-bit data bus reaches it 16 bits at a
// time through a local bus FD[15:0] shared by eight 74LVC574s on the card:
//   * input latches U2-U5 capture D0-31 at every bus CLK rising edge (the
//     edge every register on the bus clocks on); SELLO_N / SELHI_N put the low
//     or high half on FD
//   * output latches U6-U9 take the result from FD, low half on CKLO, high
//     half on CKHI; they drive D0-31 while DOE = ALU (-ROE from GAL ALUDEC,
//     not from here: an unconfigured FPGA can never drive the bus)
//   * GAL ALUDEC also registers LDA / LDB = "this CLK edge loaded A / B"
//     (DLD = ALUA / ALUB at the edge), valid for the whole microcycle
//
// After each CLK rising edge (seen through a synchroniser) the interface reads
// both halves, then -- if LDA or LDB -- loads the core. Whenever the result
// differs from what the output latches hold it writes it out. Between bus
// events, with nothing to write or compute, the low input half is left on FD
// so the next read starts at once (the fast path: the operand is in the core
// about 6 fclk after the edge). The turn-around rules on FD, checked by the
// testbench: the FPGA never drives FD in the same fclk cycle as, or the cycle
// after, a latch is enabled, and the two halves are never enabled together or
// back to back.
`default_nettype none
module p32alu_top (
    input  wire        SYS_CLK,     // 27 MHz crystal on the module
    input  wire        BCLK,        // bus CLK (via 74LVC244)
    input  wire        RES_N,       // bus -RES (via 74LVC244)
    input  wire        LDA,         // from GAL ALUDEC (via 74LVC244)
    input  wire        LDB,
    input  wire [4:0]  ALUF,        // bus ALUF0-4 (via 74LVC244)
    inout  wire [15:0] FD,          // the local bus
    output reg         SELLO_N,     // input latches, low half -> FD
    output reg         SELHI_N,     // input latches, high half -> FD
    output reg         CKLO,        // output latches, low half <- FD
    output reg         CKHI,        // output latches, high half <- FD
    output wire        EQ,          // compares, to the bus via 74LVC244
    output wire        LT,
    output wire        LTU
);
    wire fclk, lock;
`ifdef SIMULATION
    assign fclk = SYS_CLK;                  // the testbench drives the 108 MHz clock directly
    assign lock = 1'b1;
`else
    // Gowin rPLL: CLKOUT = 27 MHz * (FBDIV_SEL + 1) / (IDIV_SEL + 1) = 108 MHz,
    // VCO = CLKOUT * ODIV_SEL = 864 MHz (GW2A: 500-1250 MHz). Check the
    // parameters with Gowin's IP generator for the exact device.
    rPLL #(.FCLKIN("27"), .DEVICE("GW2AR-18C"), .IDIV_SEL(0), .FBDIV_SEL(3), .ODIV_SEL(8)) pll (
        .CLKIN(SYS_CLK), .CLKOUT(fclk), .LOCK(lock), .CLKOUTP(), .CLKOUTD(), .CLKOUTD3(),
        .RESET(1'b0), .RESET_P(1'b0), .CLKFB(1'b0), .FBDSEL(6'd0), .IDSEL(6'd0),
        .ODSEL(6'd0), .PSDA(4'd0), .DUTYDA(4'd0), .FDLY(4'd0));
`endif

    // ------------------------------------------------------------ synchronisers
    reg [2:0] bclk_s;
    reg [1:0] res_s;
    reg [4:0] aluf_1, aluf_s;
    always @(posedge fclk) begin
        bclk_s <= {bclk_s[1:0], BCLK};
        res_s  <= {res_s[0], RES_N & lock};
        aluf_1 <= ALUF; aluf_s <= aluf_1;
    end
    wire rst_n = res_s[1];
    wire rise  = bclk_s[1] & ~bclk_s[2];

    // ------------------------------------------------------------ the core
    reg  [15:0] lo, hi;
    reg         lda_q, ldb_q, load;
    wire [31:0] r;
    wire        busy;
    p32alu core (.fclk(fclk), .res_n(rst_n), .load_a(load & lda_q), .load_b(load & ldb_q),
                 .d({hi, lo}), .aluf(aluf_s), .r(r), .eq(EQ), .lt(LT), .ltu(LTU), .busy(busy));

    // ------------------------------------------------------------ the local bus
    reg        fd_oe;
    reg [15:0] fd_out;
    assign FD = fd_oe ? fd_out : 16'bz;

    localparam R_IDLE = 3'd0, R_LOEN = 3'd1, R_LOW = 3'd2, R_LOS = 3'd3,
               R_HIEN = 3'd4, R_HIW = 3'd5, R_HIS = 3'd6, R_GAP = 3'd7;
    localparam W_IDLE = 3'd0, W_CKLO = 3'd1, W_HI = 3'd2, W_CKHI = 3'd3, W_END = 3'd4;
    reg [2:0]  rd, wph;
    reg [31:0] rr;                  // what the output latches hold (or are being given)
    reg        dirty;               // rewrite: a read cut a write short
    reg [1:0]  lo_age;              // fclk cycles the low half has been on FD
    always @(posedge fclk) begin
        load <= 1'b0;
        if (!rst_n) begin
            SELLO_N <= 1'b1; SELHI_N <= 1'b1; CKLO <= 1'b0; CKHI <= 1'b0;
            fd_oe <= 1'b0; fd_out <= 16'd0; rd <= R_IDLE; wph <= W_IDLE;
            rr <= 32'd0; dirty <= 1'b1; lo_age <= 2'd0; lo <= 16'd0; hi <= 16'd0;
            lda_q <= 1'b0; ldb_q <= 1'b0;
        end else if (rise && rd == R_IDLE) begin
            // a bus CLK edge: the input latches hold this edge's D0-31
            CKLO <= 1'b0; CKHI <= 1'b0;
            if (wph != W_IDLE) dirty <= 1'b1;
            wph <= W_IDLE;
            if (!SELLO_N && lo_age == 2'd2 && !fd_oe) begin      // fast path: low half already on FD
                lo <= FD; SELLO_N <= 1'b1; rd <= R_HIEN;
            end else begin
                fd_oe <= 1'b0; SELLO_N <= 1'b1; rd <= R_LOEN;
            end
        end else if (rd != R_IDLE) begin
            case (rd)
            R_LOEN: begin SELLO_N <= 1'b0; rd <= R_LOW; end
            R_LOW:  rd <= R_LOS;
            R_LOS:  begin lo <= FD; SELLO_N <= 1'b1; rd <= R_HIEN; end
            R_HIEN: begin SELHI_N <= 1'b0; rd <= R_HIW; end
            R_HIW:  rd <= R_HIS;
            R_HIS:  begin hi <= FD; SELHI_N <= 1'b1; lda_q <= LDA; ldb_q <= LDB; load <= 1'b1; rd <= R_GAP; end
            default: rd <= R_IDLE;                                   // R_GAP: the high half lets go
            endcase
        end else if (r != rr || dirty) begin
            // (re)start writing the result: low half, CKLO, high half, CKHI
            CKLO <= 1'b0; CKHI <= 1'b0;
            if (!SELLO_N) begin
                SELLO_N <= 1'b1; wph <= W_IDLE;                      // let go of FD first
            end else begin
                rr <= r; dirty <= 1'b0; fd_oe <= 1'b1; fd_out <= r[15:0]; wph <= W_CKLO;
            end
        end else begin
            case (wph)
            W_CKLO: begin CKLO <= 1'b1; wph <= W_HI; end
            W_HI:   begin CKLO <= 1'b0; fd_out <= rr[31:16]; wph <= W_CKHI; end
            W_CKHI: begin CKHI <= 1'b1; wph <= W_END; end
            W_END:  begin CKHI <= 1'b0; wph <= W_IDLE; end
            default:                                                 // idle: park the low half on FD
                if (fd_oe) fd_oe <= 1'b0;
                else if (busy) SELLO_N <= 1'b1;
                else SELLO_N <= 1'b0;
            endcase
        end
        if (!rst_n || SELLO_N) lo_age <= 2'd0;
        else if (lo_age != 2'd2) lo_age <= lo_age + 2'd1;
    end
endmodule
`default_nettype wire
