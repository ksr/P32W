// tb_p32alu.v -- the ALU core (p32alu.v, D21) against emulator/p32uemu.c's
// ALU: every function, with gen_alu.py's vectors (edge cases and random
// operands). Each vector: A and B loaded (one fclk each), the function
// selected, the result and EQ / LT / LTU read once `busy` has fallen; also
// the number of fclk cycles that took, the core's latency (the time budget
// of the CPU card's operand-to-result cycle: docs/microarch.md section 8).
`timescale 1ns/1ps
module tb_p32alu;
    reg fclk = 0, res_n = 0, load_a = 0, load_b = 0;
    reg [31:0] d = 0;
    reg [4:0] aluf = 0;
    wire [31:0] r;
    wire eq, lt, ltu, busy;
    p32alu dut (.fclk(fclk), .res_n(res_n), .load_a(load_a), .load_b(load_b), .d(d), .aluf(aluf),
                .r(r), .eq(eq), .lt(lt), .ltu(ltu), .busy(busy));
    always #4.63 fclk = !fclk;                          // 108 MHz

    reg [111:0] vec [0:20000];                          // op(8) a(32) b(32) r(32) flags(8)
    integer i, n, errors, k, worst;
    reg [7:0] op; reg [31:0] va, vb, vr; reg [7:0] vf;
    initial begin
        $readmemh("alu_vectors.hex", vec);
        errors = 0; n = 0; worst = 0;
        repeat (4) @(posedge fclk);
        res_n = 1;
        repeat (2) @(posedge fclk);
        for (i = 0; i <= 20000 && vec[i] !== 112'bx; i = i + 1) begin
            {op, va, vb, vr, vf} = vec[i];
            @(negedge fclk) begin d = va; load_a = 1; aluf = op[4:0]; end
            @(negedge fclk) begin load_a = 0; d = vb; load_b = 1; end
            @(negedge fclk) load_b = 0;
            k = 1;
            while (busy || k < 2) begin @(negedge fclk); k = k + 1; end
            if (k > worst) worst = k;
            if (r !== vr || {ltu, lt, eq} !== vf[2:0]) begin
                if (errors < 5)
                    $display("op %0d a %08x b %08x: r %08x flags %b, want %08x %b", op, va, vb, r, {ltu, lt, eq}, vr, vf[2:0]);
                errors = errors + 1;
            end
            n = n + 1;
        end
        if (errors == 0)
            $display("PASS p32alu core: %0d vectors, all 19 functions; at most %0d fclk (%0.0f ns) from the second operand to the result",
                     n, worst, worst * 9.26);
        else $display("FAIL p32alu core: %0d of %0d vectors wrong", errors, n);
        $finish;
    end
endmodule
