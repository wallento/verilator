// DESCRIPTION: Verilator: Verilog Test module
//
// This file ONLY is placed into the Public Domain, for any use,
// without warranty, 2010 by Wilson Snyder.

module A(
  input a, b,
  output c
);
  assign c = a & b;
endmodule

module B(
  input a, b,
  output c
);
  assign c = a ^ b;
endmodule

module C(
  input a, b,
  output c
);
  B mod_B0(
    .a(a),
    .b(b),
    .c(c)
  );
endmodule

module D(
  input clk,
  input d,
  output q /* verilator public_flat_rw @(clk) */
);
  reg d_q;
  assign q = d_q;
  always @(posedge clk)
    d_q <= d;
endmodule

module t(
  input clk,
  input a, b,
  output y
);

  wire x;

  A mod_A0(
    .a (0),
    .b (1),
    .c ()
  );

  A mod_A1(
    .a (0),
    .b (1),
    .c ()
  );

  C mod_C(
    .a(0),
    .b(0),
    .c(x)
  );

  D mod_D(
    .clk(clk),
    .d(x),
    .q(y)
  );

endmodule
