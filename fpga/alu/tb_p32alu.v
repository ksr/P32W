// tb_p32alu.v -- the ALU module as wired (D21): p32alu_top in the Tang Nano 20K
// plus models of the card's chips, against emulator/p32uemu.c's ALU (vectors
// from gen_alu.py), with the bus timing of D17: a 333 ns microcycle, CLK
// high 133 ns (control-store lookup) then low 200 ns (datapath); registers
// take the bus at the CLK rising edge. Each vector: cycle 1 latches A
// (DLD = ALUA), cycle 2 latches B, cycle 3 reads the result (DOE = ALU) on
// the bus -- sampled 300 ns after B's latch edge (the destination's setup
// takes the rest), so the read-out protocol and the dividers must finish in
// time. The fields (DLD, DOE, ALUF) change 20 ns after CLK falls, as from
// the sequencer's pipeline register. EQ/LT/LTU are checked then too; they
// are ready ~90 ns after B's edge, so the microcode tests them a step later
// (D21). fclk (108 MHz) is not related to the
// bus clock, so its phase against CLK drifts across the vectors.
//
// Chip models (worst-case-ish 3.3 V figures; data-sheet maxima are a few ns
// more): 74LVC574 clock-to-Q 6 ns, enable/disable 7 ns; 74LVC244 5 ns;
// GAL ATF22V10C-15 clock-to-out 10 ns, combinational 15 ns. The local bus
// rules (no two drivers, a free fclk cycle at each turn-around) are checked
// on every fclk edge.
//   iverilog -g2012 -DSIMULATION -o tb tb_p32alu.v p32alu_top.v p32alu.v && vvp tb
`timescale 1ns/1ps
module tb_p32alu;
`include "p32alu_codes.vh"
    // ------------------------------------------------------------ the bus
    reg        CLK = 1, RES_N = 1;
    reg  [4:0] DOE = 0, DLD = 0, ALUF = 0;
    reg [31:0] d_drv = 0;  reg d_en = 0;               // whatever drives D (the register file, ...)
    wire [31:0] D;
    assign D = d_en ? d_drv : 32'bz;

    // ------------------------------------------------------------ GAL ALUDEC
    reg LDA_g = 0, LDB_g = 0;
    always @(posedge CLK or negedge RES_N)
        if (!RES_N) begin LDA_g <= #10 0; LDB_g <= #10 0; end
        else begin LDA_g <= #10 (DLD == DLD_ALUA); LDB_g <= #10 (DLD == DLD_ALUB); end
    wire ROE_N;
    assign #15 ROE_N = !(DOE == DOE_ALU);

    // ------------------------------------------------------------ 74LVC244s (5 V side -> FPGA)
    wire BCLK_f, RES_f, LDA_f, LDB_f; wire [4:0] ALUF_f;
    assign #5 BCLK_f = CLK;  assign #5 RES_f = RES_N;
    assign #5 LDA_f = LDA_g; assign #5 LDB_f = LDB_g;  assign #5 ALUF_f = ALUF;
    wire EQ_f, LT_f, LTU_f, EQ, LT, LTU;
    assign #5 EQ = EQ_f;  assign #5 LT = LT_f;  assign #5 LTU = LTU_f;

    // ------------------------------------------------------------ the FPGA
    reg fclk = 0;
    always #4.6296 fclk = ~fclk;                       // 108 MHz
    wire [15:0] FD;
    wire SELLO_N, SELHI_N, CKLO, CKHI;
    p32alu_top dut (.SYS_CLK(fclk), .BCLK(BCLK_f), .RES_N(RES_f), .LDA(LDA_f), .LDB(LDB_f), .ALUF(ALUF_f),
                    .FD(FD), .SELLO_N(SELLO_N), .SELHI_N(SELHI_N), .CKLO(CKLO), .CKHI(CKHI),
                    .EQ(EQ_f), .LT(LT_f), .LTU(LTU_f));

    // ------------------------------------------------------------ 74LVC574 input latches U2-U5
    reg [31:0] in_q;
    integer k;
    reg [31:0] d_now;
    always @(posedge CLK) begin                        // a floating bus line latches as 0 or 1, not z
        for (k = 0; k < 32; k = k + 1) d_now[k] = D[k] === 1'b1;
        in_q <= #6 d_now;
    end
    wire sello_on, selhi_on;
    assign #7 sello_on = !SELLO_N;
    assign #7 selhi_on = !SELHI_N;
    assign FD = sello_on ? in_q[15:0]  : 16'bz;
    assign FD = selhi_on ? in_q[31:16] : 16'bz;

    // ------------------------------------------------------------ 74LVC574 output latches U6-U9
    reg [31:0] out_q = 0;
    always @(posedge CKLO) out_q[15:0]  <= #6 FD;
    always @(posedge CKHI) out_q[31:16] <= #6 FD;
    wire roe_on;
    assign #7 roe_on = !ROE_N;
    assign D = roe_on ? out_q : 32'bz;

    // ------------------------------------------------------------ local-bus rules
    integer bus_errors = 0;
    reg p_oe = 0, p_lo = 0, p_hi = 0;
    always @(posedge fclk) begin
        if ((dut.fd_oe && (!SELLO_N || !SELHI_N || p_lo || p_hi)) ||
            (!SELLO_N && (!SELHI_N || p_hi || p_oe)) || (!SELHI_N && (p_lo || p_oe))) begin
            if (bus_errors < 5) $display("FAIL local bus turn-around at %0t: oe %b lo %b hi %b (before: %b %b %b)",
                                         $time, dut.fd_oe, !SELLO_N, !SELHI_N, p_oe, p_lo, p_hi);
            bus_errors = bus_errors + 1;
        end
        p_oe = dut.fd_oe; p_lo = !SELLO_N; p_hi = !SELHI_N;
    end
    // a sampled half must never be X (two drivers, or nobody)
    always @(posedge fclk)
        if ((dut.rd == 3 || dut.rd == 6 || (dut.rise && dut.rd == 0 && !SELLO_N && dut.lo_age == 2 && !dut.fd_oe))
            && ^FD === 1'bx && dut.rst_n) begin
            if (bus_errors < 5) $display("FAIL local bus sampled as %h at %0t", FD, $time);
            bus_errors = bus_errors + 1;
        end

    // ------------------------------------------------------------ result latency (B's edge -> D final)
    real t_edge, t_final, worst = 0, t_flags, worst_f = 0;
    reg [31:0] want; reg [2:0] want_f;
    reg watching = 0, watching_f = 0;
    always @(D) if (watching && D === want) t_final = $realtime;
    always @(EQ or LT or LTU) if (watching_f && {LTU, LT, EQ} === want_f) t_flags = $realtime;

    // ------------------------------------------------------------ vectors
    reg [111:0] vec [0:20000];                          // op(8) a(32) b(32) r(32) flags(8)
    integer i, n, errors;
    reg [7:0] op; reg [31:0] va, vb, vr; reg [7:0] vf;
    task cycle;                                         // from the rising edge: the fields were set then
        begin
            #133 CLK = 0;                               // datapath phase
            #200 CLK = 1;                               // registers clock
        end
    endtask
    initial begin
        $readmemh("alu_vectors.hex", vec);
        errors = 0; n = 0;
        #10 RES_N = 0;                                  // a reset pulse, as from the sequencer
        repeat (2) cycle;
        RES_N = 1;
        repeat (2) cycle;
        for (i = 0; vec[i] !== 112'bx && i < 20000; i = i + 1) begin
            {op, va, vb, vr, vf} = vec[i];
            // The fields come from the sequencer's pipeline register at CLK falling (D17:
            // the lookup is the high phase); a source drives D from ~50 ns into the
            // datapath phase until after the rising edge, where every register clocks.
            // cycle 1: A
            #133 CLK = 0;
            #20 DLD = DLD_ALUA; DOE = 0;
            #30 d_drv = va; d_en = 1;
            #150 CLK = 1;
            // cycle 2: B
            #3 d_en = 0;
            #130 CLK = 0;
            #20 DLD = DLD_ALUB;
            #30 d_drv = vb; d_en = 1;
            #150 CLK = 1;
            t_edge = $realtime;
            want_f = vf[2:0]; t_flags = ({LTU, LT, EQ} === vf[2:0]) ? t_edge : -1; watching_f = 1;
            // cycle 3: DOE = ALU and the function code, from the falling edge
            #3 d_en = 0;
            #130 CLK = 0;
            #20 DLD = 0; DOE = DOE_ALU; ALUF = op[4:0];
            want = vr; t_final = (D === vr) ? t_edge : -1; watching = 1;
            #147;                                       // 300 ns after B was latched
            if (D !== vr || {LTU, LT, EQ} !== vf[2:0]) begin
                if (errors < 10)
                    $display("FAIL op %0d a %08h b %08h: bus %08h flags %b, want %08h flags %b",
                             op, va, vb, D, {LTU, LT, EQ}, vr, vf[2:0]);
                errors = errors + 1;
            end else begin
                if (t_final >= 0 && t_final - t_edge > worst) worst = t_final - t_edge;
                if (t_flags >= 0 && t_flags - t_edge > worst_f) worst_f = t_flags - t_edge;
            end
            watching = 0; watching_f = 0;
            #33 CLK = 1;                                // the end of cycle 3 (DOE holds until the
            n = n + 1;                                  // next falling edge, as from the pipeline)
        end
        if (errors == 0 && bus_errors == 0)
            $display("PASS p32alu module: %0d vectors, all 19 functions on the bus by 300 ns (worst %0.0f ns; EQ/LT/LTU %0.0f ns)",
                     n, worst, worst_f);
        else $display("FAIL p32alu module: %0d of %0d vectors wrong, %0d local-bus violations", errors, n, bus_errors);
        $finish;
    end
endmodule
