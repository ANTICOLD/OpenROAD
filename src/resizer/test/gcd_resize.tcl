# gcd full meal deal
read_liberty Nangate45/Nangate45_typ.lib
read_lef Nangate45/Nangate45.lef
read_def gcd_placed.def
read_sdc gcd.sdc

set_wire_rc -layer metal3
estimate_parasitics -placement
# flute results are not stable on nets with fanout > 9
set_load 20 _241_
# force consistent results on centos/gcc
set_load 4 [get_net -of [get_pin _888_/Q]]

report_worst_slack

set_dont_use {CLKBUF_* AOI211_X1 OAI211_X1}

buffer_ports

repair_design -max_wire_length 800

repair_tie_fanout LOGIC0_X1/Z
repair_tie_fanout LOGIC1_X1/Z
repair_timing

report_checks
report_check_types -max_slew -max_fanout -max_capacitance
report_worst_slack
report_long_wires 10

report_floating_nets -verbose
report_design_area
