/////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2019, OpenROAD
// All rights reserved.
//
// BSD 3-Clause License
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <array>
#include "db_sta/dbSta.hh"
#include "sta/UnorderedSet.hh"
#include "SteinerTree.hh"

namespace sta {

using std::array;
using odb::Rect;

class RebufferOption;

class NetHash
{
public:
  size_t operator()(const Net *net) const { return hashPtr(net); }
};

typedef Map<LibertyCell*, float> CellTargetLoadMap;
typedef Map<Vertex*, float> VertexWeightMap;
typedef Vector<Vector<Pin*>> GroupedPins;
typedef array<Required, RiseFall::index_count> Requireds;
typedef array<Slew, RiseFall::index_count> TgtSlews;
typedef Slack Slacks[RiseFall::index_count][MinMax::index_count];
typedef Vector<RebufferOption*> RebufferOptionSeq;
enum class RebufferOptionType { sink, junction, wire, buffer };

class Resizer : public StaState
{
public:
  Resizer();
  void init(Tcl_Interp *interp,
            dbDatabase *db,
            dbSta *sta);

  // Remove all buffers from the netlist.
  void removeBuffers();
  // Set the resistance and capacitance used for parasitics.
  void setWireRC(float wire_res, // ohms/meter
                 float wire_cap, // farads/meter
                 Corner *corner);
  // Set the resistance and capacitance used for parasitics on clock nets.
  void setWireClkRC(float wire_res, // ohms/meter
                    float wire_cap, // farads/meter
                    Corner *corner);
  // ohms/meter
  float wireResistance() { return wire_res_; }
  float wireClkResistance() { return wire_clk_res_; }
  // farads/meter
  float wireCapacitance() { return wire_cap_; }
  float wireClkCapacitance() { return wire_clk_cap_; }
  void estimateWireParasitics();
  void estimateWireParasitic(const Net *net);
  bool haveEstimatedParasitics() const { return have_estimated_parasitics_; }
  void parasiticsInvalid(const Net *net);
  void parasiticsInvalid(const dbNet *net);

  // Core area (meters).
  double coreArea() const;
  // 0.0 - 1.0 (100%) of core size.
  double utilization();
  // Maximum utilizable area (core area * utilization)
  double maxArea() const;

  void setDontUse(LibertyCellSeq *dont_use);  
  bool dontUse(LibertyCell *cell);

  void setMaxUtilization(double max_utilization);
  void resizePreamble(LibertyLibrarySeq *resize_libs);
  void bufferInputs();
  void bufferOutputs();
  // Resize all instances in the network.
  // resizerPreamble() required.
  void resizeToTargetSlew();
  // Resize inst to target slew (public for testing).
  // resizerPreamble() required.
  // Return true if resized.
  bool resizeToTargetSlew(const Pin *drvr_pin);

  Slew targetSlew(const RiseFall *tr);
  float targetLoadCap(LibertyCell *cell);
  void repairHold(bool allow_setup_violations);
  void repairHold(Pin *end_pin,
                  LibertyCell *buffer_cell,
                  bool allow_setup_violations);
  void repairSetup();
  void repairSetup(Pin *drvr_pin);
  // Area of the design in meter^2.
  double designArea();
  // Increment design_area
  void designAreaIncr(float delta);
  // Caller owns return value.
  NetSeq *findFloatingNets();
  void repairTieFanout(LibertyPort *tie_port,
                       double separation, // meters
                       bool verbose);
  void bufferWireDelay(LibertyCell *buffer_cell,
                       double wire_length, // meters
                       Delay &delay,
                       Slew &slew);
  void cellWireDelay(LibertyPort *drvr_port,
                     LibertyPort *load_port,
                     double wire_length, // meters
                     // Return values.
                     Delay &delay,
                     Slew &slew);
  // Repair long wires, max fanout violations.
  void repairDesign(double max_wire_length); // zero for none (meters)
  // repairDesign but restricted to clock network and
  // no max_fanout/max_cap checks.
  void repairClkNets(double max_wire_length); // meters
  // Clone inverters next to the registers they drive to remove them
  // from the clock network.
  // yosys is too stupid to use the inverted clock registers
  // and TritonCTS is too stupid to balance clock networks with inverters.
  void repairClkInverters();
  // for debugging
  void repairNet(Net *net,
                 double max_wire_length); // meters
  void reportLongWires(int count,
                       int digits);
  // Find the max wire length before it is faster to split the wire
  // in half with a buffer (in meters).
  double findMaxWireLength();
  double findMaxWireLength(LibertyCell *buffer_cell);
  double findMaxWireLength(LibertyPort *drvr_port);
  // Find the max wire length with load slew < max_slew (in meters).
  double findMaxSlewWireLength(LibertyPort *drvr_port,
                               LibertyPort *load_port,
                               double max_slew);
  double findSlewLoadCap(LibertyPort *drvr_port,
                         double slew);
  float bufferDelay(LibertyCell *buffer_cell);
  float bufferDelay(LibertyCell *buffer_cell,
                    const RiseFall *rf);
  // Longest driver to load wire (in meters).
  double maxLoadManhattenDistance(const Net *net);
  void writeNetSVG(Net *net,
                   const char *filename);
  dbNetwork *getDbNetwork() { return db_network_; }
  double dbuToMeters(int dist) const;
  int metersToDbu(double dist) const;

  void rebuffer(const Pin *drvr_pin);
  // Rebuffer net (for testing).
  // resizerPreamble() required.
  void rebuffer(Net *net);

protected:
  void init();
  void ensureBlock();
  void ensureDesignArea();
  void ensureCorner();
  void initCorner(Corner *corner);
  void ensureLevelDrvrVerticies();
  void bufferInput(Pin *top_pin,
                   LibertyCell *buffer_cell);
  void bufferOutput(Pin *top_pin,
                    LibertyCell *buffer_cell);
  void makeEquivCells(LibertyLibrarySeq *resize_libs);
  void findBuffers(LibertyLibrarySeq *resize_libs);
  void findTargetLoads(LibertyLibrarySeq *resize_libs);
  void findTargetLoads(LibertyLibrary *library,
                       TgtSlews &slews);
  void findTargetLoad(LibertyCell *cell,
                      TgtSlews &slews);
  float findTargetLoad(LibertyCell *cell,
                       TimingArc *arc,
                       Slew in_slew,
                       Slew out_slew);
  void findBufferTargetSlews(LibertyLibrarySeq *resize_libs);
  void findBufferTargetSlews(LibertyLibrary *library,
                             // Return values.
                             Slew slews[],
                             int counts[]);
  bool hasMultipleOutputs(const Instance *inst);
  ParasiticNode *findParasiticNode(SteinerTree *tree,
                                   Parasitic *parasitic,
                                   const Net *net,
                                   const Pin *pin,
                                   SteinerPt steiner_pt);
  void findLongWires(VertexSeq &drvrs);
  void findLongWiresSteiner(VertexSeq &drvrs);
  int findMaxSteinerDist(Vertex *drvr);
  int findMaxSteinerDist(Vertex *drvr,
                         SteinerTree *tree);
  int findMaxSteinerDist(SteinerTree *tree,
                         SteinerPt pt,
                         int dist_from_drvr);
  void repairNet(Net *net,
                 Vertex *drvr,
                 bool check_slew,
                 bool check_cap,
                 bool check_fanout,
                 int max_length, // dbu
                 bool resize_drvr,
                 int &repair_count,
                 int &slew_violations,
                 int &cap_violations,
                 int &fanout_violations,
                 int &length_violations);
  void checkSlew(const Pin *drvr_pin,
                 // Return values.
                 Slew &slew,
                 float &limit,
                 float &slack);
  void repairNet(SteinerTree *tree,
                 SteinerPt pt,
                 SteinerPt prev_pt,
                 Net *net,
                 float max_cap,
                 float max_fanout,
                 int max_length,
                 int level,
                 // Return values.
                 int &wire_length,
                 float &pin_cap,
                 float &fanout,
                 PinSeq &load_pins);
  void makeRepeater(const char *where,
                    SteinerTree *tree,
                    SteinerPt pt,
                    Net *in_net,
                    LibertyCell *buffer_cell,
                    int level,
                    int &wire_length,
                    float &pin_cap,
                    float &fanout,
                    PinSeq &load_pins);
  void makeRepeater(const char *where,
                    int x,
                    int y,
                    Net *in_net,
                    LibertyCell *buffer_cell,
                    int level,
                    int &wire_length,
                    float &pin_cap,
                    float &fanout,
                    PinSeq &load_pins);
  double gateSlewDiff(LibertyPort *drvr_port,
                      double load_cap,
                      double slew);
  // Max distance from driver to load (in dbu).
  int maxLoadManhattenDistance(Vertex *drvr);

  float portCapacitance(const LibertyPort *port);
  float portFanoutLoad(LibertyPort *port);
  float pinCapacitance(const Pin *pin);
  float bufferInputCapacitance(LibertyCell *buffer_cell);
  Requireds pinRequireds(const Pin *pin);
  void gateDelays(LibertyPort *drvr_port,
                  float load_cap,
                  // Return values.
                  ArcDelay delays[RiseFall::index_count],
                  Slew slews[RiseFall::index_count]);
  ArcDelay gateDelay(LibertyPort *drvr_port,
                     float load_cap);
  float bufferDelay(LibertyCell *buffer_cell,
                    RiseFall *rf,
                    float load_cap);
  float bufferDelay(LibertyCell *buffer_cell,
                    float load_cap);
  Parasitic *makeWireParasitic(Net *net,
                               Pin *drvr_pin,
                               Pin *load_pin,
                               double wire_length); // meters
  string makeUniqueNetName();
  string makeUniqueInstName(const char *base_name);
  string makeUniqueInstName(const char *base_name,
                            bool underscore);
  bool overMaxArea();
  bool hasTopLevelPort(const Net *net);
  Point location(Instance *inst);
  void setLocation(Instance *inst,
                   Point pt);
  double area(dbMaster *master);
  double area(Cell *cell);
  double splitWireDelayDiff(double wire_length,
                            LibertyCell *buffer_cell);
  double maxSlewWireDiff(LibertyPort *drvr_port,
                         LibertyPort *load_port,
                         double wire_length,
                         double max_slew);
  LibertyCell *findHoldBuffer();
  void repairHold(VertexSet *ends,
                  LibertyCell *buffer_cell,
                  bool allow_setup_violations);
  int repairHoldPass(VertexSet &ends,
                     LibertyCell *buffer_cell,
                     float buffer_delay,
                     bool allow_setup_violations);
  void findHoldViolations(VertexSet *ends,
                          // Return values.
                          Slack &worst_slack,
                          VertexSet &hold_violations);
  VertexSet findHoldFanins(VertexSet &ends);
  VertexSeq sortHoldFanins(VertexSet &fanins);
  void makeHoldDelay(Vertex *drvr,
                     int buffer_count,
                     PinSeq &load_pins,
                     LibertyCell *buffer_cell);
  Point findCenter(PinSeq &pins);
  Slack holdSlack(Slacks &slacks);
  Slack setupSlack(Slacks &slacks);
  Slack slackGap(Vertex *vertex);
  Slack slackGap(Slacks &slacks);
  int fanout(Vertex *vertex);
  void findCellInstances(LibertyCell *cell,
                         // Return value.
                         InstanceSeq &insts);
  int fanout(Pin *drvr_pin);
  void findLoads(Pin *drvr_pin,
                 PinSeq &loads);
  bool isFuncOneZero(const Pin *drvr_pin);
  bool isSpecial(Net *net);
  Point tieLocation(Pin *load,
                    int separation);
  bool hasFanout(Vertex *drvr);
  InstanceSeq findClkInverters();
  void cloneClkInverter(Instance *inv);
  void setWireCorner(Corner *corner);
  void ensureWireParasitic(const Net *net);
  void ensureWireParasitic(const Pin *drvr_pin);
  void ensureWireParasitic(const Pin *drvr_pin,
                           const Net *net);
  void ensureWireParasitics();
  void repairSetup(PathRef &path,
                   Slack path_slack);
  void splitLoads(PathRef *drvr_path,
                  Slack drvr_slack);
  LibertyCell *upsizeCell(LibertyPort *in_port,
                          LibertyPort *drvr_port,
                          float load_cap,
                          float prev_drive);
  bool replaceCell(Instance *inst,
                   LibertyCell *cell);

  RebufferOptionSeq rebufferBottomUp(SteinerTree *tree,
                                     SteinerPt k,
                                     SteinerPt prev,
                                     int level);
  void rebufferTopDown(RebufferOption *choice,
                       Net *net,
                       int level);
  RebufferOptionSeq
  addWireAndBuffer(RebufferOptionSeq Z,
                   SteinerTree *tree,
                   SteinerPt k,
                   SteinerPt prev,
                   int level);
  // RebufferOption factory.
  RebufferOption *makeRebufferOption(RebufferOptionType type,
                                     float cap,
                                     Requireds requireds,
                                     Pin *load_pin,
                                     Point location,
                                     LibertyCell *buffer_cell,
                                     RebufferOption *ref,
                                     RebufferOption *ref2);
  void deleteRebufferOptions();
  bool hasTopLevelOutputPort(Net *net);

  int rebuffer_net_count_;
  RebufferOptionSeq rebuffer_options_;
  friend class RebufferOption;

  // These are command args
  float wire_res_;
  float wire_cap_;
  float wire_clk_res_;
  float wire_clk_cap_;
  Corner *corner_;
  LibertyCellSet dont_use_;
  double max_area_;

  dbSta *sta_;
  dbNetwork *db_network_;
  dbDatabase *db_;
  dbBlock *block_;
  Rect core_;
  bool core_exists_;
  double design_area_;
  const MinMax *min_max_;
  const DcalcAnalysisPt *dcalc_ap_;
  const Pvt *pvt_;
  const ParasiticAnalysisPt *parasitics_ap_;
  LibertyCellSeq buffer_cells_;
  LibertyCell *buffer_lowest_drive_;
  bool have_estimated_parasitics_;
  UnorderedSet<const Net*, NetHash> parasitics_invalid_;
  CellTargetLoadMap *target_load_map_;
  VertexSeq level_drvr_verticies_;
  bool level_drvr_verticies_valid_;
  TgtSlews tgt_slews_;
  // Instances with multiple output ports that have been resized.
  InstanceSet resized_multi_output_insts_;
  int unique_net_index_;
  int unique_inst_index_;
  int resize_count_;
  int inserted_buffer_count_;
};

} // namespace
