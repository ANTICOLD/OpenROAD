source helpers.tcl
psn::set_log_pattern "\[%^%l%$\] %v"
puts [psn::has_transform repair_timing]
read_lef ../test/data/libraries/Nangate45/NangateOpenCellLibrary.mod.lef
read_def repairtiming1.def
read_liberty ../test/data/libraries/Nangate45/NangateOpenCellLibrary_typical.lib
create_clock -name core_clock -period 10.0 [get_ports {clk_i}]
set_wire_rc -resistance 1.0e-03 -capacitance 1.0e-03
report_check_types -max_slew -violators
psn_repair_timing -transition_violations -iterations 1 -auto_buffer_library small -resize_disabled
report_check_types -max_slew -violators
set def_file [make_result_file repairtiming1.def]
write_def $def_file
diff_file $def_file repairtiming1.defok
