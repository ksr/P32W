// p32alu.v -- the P32W ALU in the CPU card's FPGA module (D21, D50).
//
// The core runs entirely on the fast clock fclk (108 MHz). The card's bus
// interface (p32alu_top.v) reads each operand out of the card's input latches
// after the bus CLK edge that captured it and hands it over with a one-fclk
// load_a / load_b pulse; the result r goes back out through the card's output
// latches. Functions and codes: microcode/p32ucode.py (p32alu_codes.vh is
// generated from it); semantics: emulator/p32uemu.c alu(), which
// tb_p32alu.v checks against every function with gen_alu.py's vectors,
// through models of the card's chips and with the bus timing of D17.
//
// Every function is precomputed into a register one fclk after an operand
// changes (the multiplier two: the GW2A's DSP blocks, pipelined), so r is a
// multiplexer of registers selected by aluf. Division is sequential: two
// dividers (unsigned, and signed magnitudes) start the moment an operand is
// loaded and retire two quotient bits per fclk: 16 cycles, then one to apply
// signs and the special cases. `busy` says the result is still settling.
`default_nettype none
module p32alu (
    input  wire        fclk,
    input  wire        res_n,       // synchronous to fclk
    input  wire        load_a,      // one fclk: a <= d
    input  wire        load_b,      // one fclk: b <= d
    input  wire [31:0] d,
    input  wire [4:0]  aluf,        // synchronised function code
    output reg  [31:0] r,           // combinational: a multiplexer of registers
    output reg         eq,
    output reg         lt,
    output reg         ltu,
    output wire        busy
);
`include "p32alu_codes.vh"

    // ------------------------------------------------------------ operands
    reg [31:0] a, b;
    wire [31:0] na = load_a ? d : a, nb = load_b ? d : b;    // the operands from this edge on
    wire start = load_a | load_b;
    always @(posedge fclk)
        if (!res_n) begin a <= 32'd0; b <= 32'd0; end
        else begin
            if (load_a) a <= d;
            if (load_b) b <= d;
        end

    // ------------------------------------------------------------ precomputed functions
    reg [31:0] f_add, f_sub, f_and, f_or, f_xor, f_nor, f_sll, f_srl, f_sra;
    reg [63:0] mu1, mu2;
    reg signed [63:0] ms1, ms2;
    always @(posedge fclk) begin
        f_add <= a + b;           f_sub <= a - b;
        f_and <= a & b;           f_or  <= a | b;
        f_xor <= a ^ b;           f_nor <= ~(a | b);
        f_sll <= a << b[4:0];     f_srl <= a >> b[4:0];
        f_sra <= $signed(a) >>> b[4:0];
        eq  <= a == b;
        lt  <= $signed(a) < $signed(b);
        ltu <= a < b;
        mu1 <= a * b;             mu2 <= mu1;             // DSP pipeline registers
        ms1 <= $signed(a) * $signed(b);  ms2 <= ms1;
    end

    // ------------------------------------------------------------ divide
    wire        neg_a = na[31], neg_b = nb[31];
    wire [31:0] qu, ru, qm, rm;
    wire        run_u, run_m;
    p32div du (.clk(fclk), .res_n(res_n), .start(start), .n(na), .d(nb),
               .q(qu), .r(ru), .running(run_u));
    p32div dm (.clk(fclk), .res_n(res_n), .start(start), .n(neg_a ? -na : na), .d(neg_b ? -nb : nb),
               .q(qm), .r(rm), .running(run_m));
    // signs and special cases, one register stage after the dividers
    reg [31:0] f_div, f_divu, f_rem, f_remu;
    reg        run_q;
    wire       dz  = b == 32'd0;
    wire       ovf = a == 32'h8000_0000 && b == 32'hFFFF_FFFF;
    always @(posedge fclk) begin
        f_divu <= dz ? 32'hFFFF_FFFF : qu;
        f_remu <= dz ? a : ru;
        f_div  <= dz ? 32'hFFFF_FFFF : ovf ? 32'h8000_0000 : (a[31] ^ b[31]) ? -qm : qm;  // toward zero
        f_rem  <= dz ? a : ovf ? 32'd0 : a[31] ? -rm : rm;                              // dividend's sign
        run_q  <= run_u | run_m;
    end

    // ------------------------------------------------------------ settling
    reg [1:0] settle;                // the precompute registers (products: 2 stages) after a load
    always @(posedge fclk)
        if (!res_n) settle <= 2'd0;
        else if (start) settle <= 2'd3;
        else if (settle != 2'd0) settle <= settle - 2'd1;
    assign busy = settle != 2'd0 || run_u || run_m || run_q;

    // ------------------------------------------------------------ result
    always @* begin
        case (aluf)
        ALU_ADD:   r = f_add;
        ALU_SUB:   r = f_sub;
        ALU_AND:   r = f_and;
        ALU_OR:    r = f_or;
        ALU_XOR:   r = f_xor;
        ALU_NOR:   r = f_nor;
        ALU_SLL:   r = f_sll;
        ALU_SRL:   r = f_srl;
        ALU_SRA:   r = f_sra;
        ALU_SLT:   r = {31'd0, lt};
        ALU_SLTU:  r = {31'd0, ltu};
        ALU_MUL:   r = mu2[31:0];
        ALU_MULH:  r = ms2[63:32];
        ALU_MULHU: r = mu2[63:32];
        ALU_DIV:   r = f_div;
        ALU_DIVU:  r = f_divu;
        ALU_REM:   r = f_rem;
        ALU_REMU:  r = f_remu;
        ALU_PASSA: r = a;
        default:   r = 32'd0;
        endcase
    end
endmodule

// Unsigned 32-bit restoring divider, two quotient bits per clock (16 clocks).
// Takes n and d at start; q and r hold the result once running falls
// (division by zero gives all-ones and n, which the ALU overrides anyway).
module p32div (
    input  wire        clk,
    input  wire        res_n,
    input  wire        start,
    input  wire [31:0] n,
    input  wire [31:0] d,
    output reg  [31:0] q,
    output reg  [31:0] r,
    output wire        running
);
    reg [4:0]  cnt;                 // 16 = idle
    reg [31:0] dq;                  // dividend shifting out / quotient shifting in
    reg [32:0] rem;
    reg [31:0] dv;
    assign running = cnt != 5'd16;
    // one radix-2 step
    function [64:0] step(input [32:0] rm, input [31:0] nq, input [31:0] dd);
        reg [32:0] t;
        begin
            t = {rm[31:0], nq[31]};
            if (t >= {1'b0, dd}) step = {t - {1'b0, dd}, nq[30:0], 1'b1};
            else                 step = {t, nq[30:0], 1'b0};
        end
    endfunction
    reg [64:0] s1, s2;
    always @* begin
        s1 = step(rem, dq, dv);
        s2 = step(s1[64:32], s1[31:0], dv);
    end
    always @(posedge clk)
        if (!res_n) begin
            cnt <= 5'd16; q <= 32'd0; r <= 32'd0; dq <= 32'd0; rem <= 33'd0; dv <= 32'd0;
        end else if (start) begin
            cnt <= 5'd0; dq <= n; rem <= 33'd0; dv <= d;
        end else if (cnt < 5'd16) begin
            rem <= s2[64:32]; dq <= s2[31:0]; cnt <= cnt + 5'd1;
            if (cnt == 5'd15) begin q <= s2[31:0]; r <= s2[63:32]; end
        end
endmodule
`default_nettype wire
