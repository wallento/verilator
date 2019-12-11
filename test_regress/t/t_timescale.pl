#!/usr/bin/perl
if (!$::Driver) { use FindBin; exec("$FindBin::Bin/bootstrap.pl", @ARGV, $0); die; }

scenarios(vlt_all => 1);

compile(
    make_top_shell => 0,
    make_main => 0,
    v_flags2 => ["--exe $Self->{t_dir}/t_timescale.cpp"],
    );

execute(
    check_finished => 1,
    );

ok(1);
1;
