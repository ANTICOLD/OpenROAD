############################################################################
##
## Copyright (c) 2019, OpenROAD
## All rights reserved.
##
## BSD 3-Clause License
##
## Redistribution and use in source and binary forms, with or without
## modification, are permitted provided that the following conditions are met:
##
## * Redistributions of source code must retain the above copyright notice, this
##   list of conditions and the following disclaimer.
##
## * Redistributions in binary form must reproduce the above copyright notice,
##   this list of conditions and the following disclaimer in the documentation
##   and/or other materials provided with the distribution.
##
## * Neither the name of the copyright holder nor the names of its
##   contributors may be used to endorse or promote products derived from
##   this software without specific prior written permission.
##
## THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
## AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
## IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
## ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
## LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
## CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
## SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
## INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
## CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
## ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
## POSSIBILITY OF SUCH DAMAGE.
##
############################################################################

namespace eval sta {

define_cmd_args "remove_buffers" {}

proc remove_buffers { args } {
  check_argc_eq0 "remove_buffers" $args
  remove_buffers_cmd
}

define_cmd_args "set_wire_rc" {[-clock] [-data]\
                                 [-layer layer_name]\
                                 [-resistance res ][-capacitance cap]\
                                 [-corner corner_name]}

proc set_wire_rc { args } {
   parse_key_args "set_wire_rc" args \
     keys {-layer -resistance -capacitance -corner} \
     flags {-clock -data}

  set wire_res 0.0
  set wire_cap 0.0

  if { [info exists keys(-layer)] } {
    if { [info exists keys(-resistance)] \
           || [info exists keys(-capacitance)] } {
      ord::error "Use -layer or -resistance/-capacitance but not both."
    }
    set layer_name $keys(-layer)
    set layer [[[ord::get_db] getTech] findLayer $layer_name]
    if { $layer == "NULL" } {
      ord::error "layer $layer_name not found."
    }
    set layer_width_dbu [$layer getWidth]
    set layer_width_micron [ord::dbu_to_microns $layer_width_dbu]
    set res_ohm_per_sq [$layer getResistance]
    set res_ohm_per_micron [expr $res_ohm_per_sq / $layer_width_micron]
    set cap_area_pf_per_sq_micron [$layer getCapacitance]
    set cap_edge_pf_per_micron [$layer getEdgeCapacitance]
    set cap_pf_per_micron [expr 1 * $layer_width_micron * $cap_area_pf_per_sq_micron \
                             + $cap_edge_pf_per_micron * 2]
    # ohms/meter
    set wire_res [expr $res_ohm_per_micron * 1e+6]
    # farads/meter
    set wire_cap [expr $cap_pf_per_micron * 1e-12 * 1e+6]
    
    if { $wire_res == 0.0 } {
      ord::warn "layer resistance is 0.0"
    }
    if { $wire_cap == 0.0 } {
      ord::warn "layer capacitance is 0.0"
    }
  } else {
    ord::ensure_units_initialized
    if { [info exists keys(-resistance)] } {
      set res $keys(-resistance)
      check_positive_float "-resistance" $res
      set wire_res [expr [resistance_ui_sta $res] / [distance_ui_sta 1.0]]
    }
    
    if { [info exists keys(-capacitance)] } {
      set cap $keys(-capacitance)
      check_positive_float "-capacitance" $cap
      set wire_cap [expr [capacitance_ui_sta $cap] / [distance_ui_sta 1.0]]
    }
  }
  
  set corner [cmd_corner]
  if { [info exists keys(-corner)] } {
    ord::warn "-corner argument ignored."
  }
  check_argc_eq0 "set_wire_rc" $args
  
  if { [info exists flags(-clock)] && [info exists flags(-clock)] \
         || ![info exists flags(-clock)] && ![info exists flags(-clock)] } {
    set data 1
    set clk 1
  } elseif { [info exists flags(-clock)] && ![info exists flags(-clock)] } {
    set data 0
    set clk 1
  } elseif { ![info exists flags(-clock)] && [info exists flags(-clock)] } {
    set data 1
    set clk 0
  }
  if { $data } {
    set_wire_rc_cmd $wire_res $wire_cap $corner
  }
  if { $clk } {
    set_wire_clk_rc_cmd $wire_res $wire_cap $corner
  }
}

define_cmd_args "estimate_parasitics" { -placement|-global_routing }

proc estimate_parasitics { args } {
  parse_key_args "estimate_parasitics" args \
    keys {} flags {-placement -global_routing}

  check_argc_eq0 "estimate_parasitics" $args
  if { [info exists flags(-placement)] } {
    if { [wire_capacitance] == 0.0 } {
      ord::warn "wire capacitance is zero. Use the set_wire_rc command to set wire resistance and capacitance."
    } else {
      estimate_parasitics_cmd
    }
  } elseif { [info exists flags(-global_routing)] } {
    FastRoute::estimate_rc_cmd
  } else {
    ord::error "missing -placement or -global_routing flag."
  }
}

define_cmd_args "set_dont_use" {lib_cells}

proc set_dont_use { args } {
  check_argc_eq1 "set_dont_use" $args
  set_dont_use_cmd [get_lib_cells_arg "-dont_use" [lindex $args 0] ord::warn]
}

# for testing resizing separately
proc resize { args } {
  check_argc_eq0 "resize" $args
  check_parasitics
  resizer_preamble [get_libs *]
  resize_to_target_slew
}

proc parse_max_util { keys_var } {
  upvar 1 $keys_var keys
  set max_util 0.0
  if { [info exists keys(-max_utilization)] } {
    set max_util $keys(-max_utilization)
    if {!([string is double $max_util] && $max_util >= 0.0 && $max_util <= 100)} {
      ord::error "-max_utilization must be between 0 and 100%."
    }
    set max_util [expr $max_util / 100.0]
  }
  return $max_util
}

proc parse_buffer_cell { keys_var required } {
  upvar 1 $keys_var keys
  set buffer_cell "NULL"
  if { [info exists keys(-buffer_cell)] } {
    set buffer_cell_name $keys(-buffer_cell)
    # check for -buffer_cell [get_lib_cell arg] return ""
    if { $buffer_cell_name != "" } {
      set buffer_cell [get_lib_cell_error "-buffer_cell" $buffer_cell_name]
      if { $buffer_cell != "NULL" } {
        if { ![get_property $buffer_cell is_buffer] } {
          ord::error "[get_name $buffer_cell] is not a buffer."
        }
      }
    }
  } elseif { $required } {
    ord::error "-buffer_cell required for buffer insertion."
  }
  if { $buffer_cell == "NULL" && $required } {
    ord::error "-buffer_cell required for buffer insertion."    
  }
  return $buffer_cell
}

define_cmd_args "buffer_ports" {[-inputs] [-outputs]\
                                  [-max_utilization util]}

proc buffer_ports { args } {
  parse_key_args "buffer_ports" args \
    keys {-buffer_cell -max_utilization} \
    flags {-inputs -outputs}
  
  if { [info exists keys(-buffer_cell)] } {
    ord::warn "-buffer_cell is deprecated."
  }

  set buffer_inputs [info exists flags(-inputs)]
  set buffer_outputs [info exists flags(-outputs)]
  if { !$buffer_inputs && !$buffer_outputs } {
    set buffer_inputs 1
    set buffer_outputs 1
  }
  check_argc_eq0 "buffer_ports" $args
  
  set_max_utilization [parse_max_util keys]
  resizer_preamble [get_libs *]
  if { $buffer_inputs } {
    buffer_inputs
  }
  if { $buffer_outputs } {
    buffer_outputs
  }
}

define_cmd_args "repair_design" {[-max_wire_length max_wire_length]\
                                   [-libraries resize_libs]}

proc repair_design { args } {
  parse_key_args "repair_design" args \
    keys {-max_wire_length -buffer_cell -libraries} \
    flags {}
  
  if { [info exists keys(-buffer_cell)] } {
    ord::warn "-buffer_cell is deprecated."
  }
  set max_wire_length 0
  if { [info exists keys(-max_wire_length)] } {
    set max_wire_length $keys(-max_wire_length)
    check_positive_float "-max_wire_length" $max_wire_length
    set max_wire_length [sta::distance_ui_sta $max_wire_length]
    if { [sta::wire_resistance] > 0 } {
      set min_delay_max_wire_length [find_max_wire_length]
      if { $max_wire_length < $min_delay_max_wire_length } {
        ord::warn "max wire length less than [format %.0fu [expr $min_delay_max_wire_length * 1e+6]] increases wire delays."
      }
    }
  }
  
  if { [info exists keys(-libraries)] } {
    set resize_libs [get_liberty_error "-libraries" $keys(-libraries)]
  } else {
    set resize_libs [get_libs *]
    if { $resize_libs == {} } {
      ord::error "No liberty libraries found."
    }
  }

  check_argc_eq0 "repair_design" $args
  check_parasitics
  resizer_preamble $resize_libs
  repair_design_cmd $max_wire_length
}

define_cmd_args "repair_clock_nets" {[-max_wire_length max_wire_length]}

proc repair_clock_nets { args } {
  parse_key_args "repair_clock_nets" args \
    keys {-max_wire_length -buffer_cell} \
    flags {}
  
  if { [info exists keys(-buffer_cell)] } {
    ord::warn "-buffer_cell is deprecated."
  }
  set max_wire_length 0
  if { [info exists keys(-max_wire_length)] } {
    set max_wire_length $keys(-max_wire_length)
    check_positive_float "-max_wire_length" $max_wire_length
    set max_wire_length [sta::distance_ui_sta $max_wire_length]
  }
  
  check_argc_eq0 "repair_clock_nets" $args
  check_parasitics
  resizer_preamble [get_libs *]
  repair_clk_nets_cmd $max_wire_length
}

define_cmd_args "repair_clock_inverters" {-buffer_cell buffer_cell}

proc repair_clock_inverters { args } {
  check_argc_eq0 "repair_clock_inverters" $args
  repair_clk_inverters_cmd
}

define_cmd_args "repair_tie_fanout" {lib_port [-separation dist] [-verbose]}

proc repair_tie_fanout { args } {
  parse_key_args "repair_tie_fanout" args keys {-separation -max_fanout} \
    flags {-verbose}
  
  set separation 0
  if { [info exists keys(-separation)] } {
    set separation $keys(-separation)
    check_positive_float "-separation" $separation
    set separation [sta::distance_ui_sta $separation]
  }
  set verbose [info exists flags(-verbose)]
  
  check_argc_eq1 "repair_tie_fanout" $args
  set lib_port [lindex $args 0]
  if { ![is_object $lib_port] } {
    set lib_port [get_lib_pins [lindex $args 0]]
  }
  if { $lib_port != "" } {
    repair_tie_fanout_cmd $lib_port $separation $verbose
  }
}

define_cmd_args "repair_hold_violations" {[-allow_setup_violations]}

proc repair_hold_violations { args } {
  parse_key_args "repair_hold_violations" args \
    keys {-buffer_cell} \
    flags {-allow_setup_violations}
  
  set allow_setup_violations [info exists flags(-allow_setup_violations)]
  check_argc_eq0 "repair_hold_violations" $args
  ord::warn "repair_hold_violations is deprecated. Use repair_timing -hold"
  repair_hold $allow_setup_violations
}

define_cmd_args "repair_timing" {[-setup] [-hold]\
                                   [-allow_setup_violations]\
                                   [-libraries resize_libs]}

proc repair_timing { args } {
  parse_key_args "repair_timing" args \
    keys {-libraries} \
    flags {-setup -hold -allow_setup_violations}

  set setup [info exists flags(-setup)]
  set hold [info exists flags(-hold)]
  if { !$setup && !$hold } {
    set setup 1
    set hold 1
  }

  if { [info exists keys(-libraries)] } {
    set resize_libs [get_liberty_error "-libraries" $keys(-libraries)]
  } else {
    set resize_libs [get_libs *]
    if { $resize_libs == {} } {
      ord::error "No liberty libraries found."
    }
  }
  set allow_setup_violations [info exists flags(-allow_setup_violations)]

  check_argc_eq0 "repair_timing" $args
  check_parasitics
  resizer_preamble $resize_libs
  if { $setup } {
    repair_setup
  }
  if { $hold } {
    repair_hold $allow_setup_violations
  }
}

# for testing
proc repair_setup_pin { end_pin } {
  check_parasitics
  resizer_preamble [get_libs *]
  repair_setup_pin_cmd $end_pin
}

################################################################

define_cmd_args "report_design_area" {}

proc report_design_area {} {
  set util [format %.0f [expr [utilization] * 100]]
  set area [format_area [design_area] 0]
  puts "Design area ${area} u^2 ${util}% utilization."
}

define_cmd_args "report_floating_nets" {[-verbose]}

proc report_floating_nets { args } {
  parse_key_args "report_floating_nets" args keys {} flags {-verbose}
  
  set verbose [info exists flags(-verbose)]
  set floating_nets [find_floating_nets]
  set floating_net_count [llength $floating_nets]
  if { $floating_net_count > 0 } {
    ord::warn "found $floating_net_count floatiing nets."
    if { $verbose } {
      foreach net $floating_nets {
        puts " [get_full_name $net]"
      }
    }
  }
}

define_cmd_args "report_long_wires" {count}

proc_redirect report_long_wires {
  global sta_report_default_digits

  parse_key_args "report_long_wires" args keys {-digits} flags {}
  
  set digits $sta_report_default_digits
  if { [info exists keys(-digits)] } {
    set digits $keys(-digits)
  }

  sta::check_argc_eq1 "report_long_wires" $args
  set count [lindex $args 0]
  report_long_wires_cmd $count $digits
}

proc check_parasitics { } {
  if { ![have_estimated_parasitics] } {
    ord::warn "no estimated parasitics. Using wire load models."
  }
}

# sta namespace end
}
