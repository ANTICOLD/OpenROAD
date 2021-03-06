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

// dbSta, OpenSTA on OpenDB

#include "db_sta/dbSta.hh"

#include <tcl.h>

#include "sta/StaMain.hh"
#include "sta/Graph.hh"
#include "sta/Clock.hh"
#include "sta/Sdc.hh"
#include "sta/Search.hh"
#include "sta/PathRef.hh"
#include "sta/PathExpanded.hh"
#include "sta/Bfs.hh"
#include "sta/EquivCells.hh"
#include "db_sta/dbNetwork.hh"
#include "db_sta/MakeDbSta.hh"
#include "opendb/db.h"
#include "openroad/OpenRoad.hh"
#include "openroad/Error.hh"
#include "gui/gui.h"
#include "dbSdcNetwork.hh"

namespace ord {

using sta::dbSta;
using sta::PathRef;
using sta::PathExpanded;

sta::dbSta *
makeDbSta()
{
  return new sta::dbSta;
}

void
initDbSta(OpenRoad *openroad)
{
  dbSta *sta = openroad->getSta();
  sta->init(openroad->tclInterp(), openroad->getDb(),
            // Broken gui api missing openroad accessor.
            gui::Gui::get());
  openroad->addObserver(sta);
}

void
deleteDbSta(sta::dbSta *sta)
{
  delete sta;
}

} // namespace ord

////////////////////////////////////////////////////////////////

namespace sta {

class PathRenderer : public gui::Renderer
{
public:
  PathRenderer(dbSta *sta);
  ~PathRenderer();
  void highlight(PathRef *path);
  virtual void drawObjects(gui::Painter& /* painter */) override;

private:
  dbSta *sta_;
  // Expanded path is owned by PathRenderer.
  PathExpanded *path_;
};

dbSta *
makeBlockSta(dbBlock *block)
{
  Sta *sta = Sta::sta();
  dbSta *sta2 = new dbSta;
  sta2->makeComponents();
  sta2->getDbNetwork()->setBlock(block);
  sta2->setTclInterp(sta->tclInterp());
  sta2->copyUnits(sta->units());
  return sta2;
}

extern "C" {
extern int Dbsta_Init(Tcl_Interp *interp);
}

extern const char *dbsta_tcl_inits[];

dbSta::dbSta() :
  Sta(),
  db_(nullptr),
  db_cbk_(this),
  path_renderer_(nullptr)
{
}

dbSta::~dbSta()
{
  delete path_renderer_;
}

void
dbSta::init(Tcl_Interp *tcl_interp,
	    dbDatabase *db,
            gui::Gui *gui)
{
  initSta();
  Sta::setSta(this);
  db_ = db;
  gui_ = gui;
  makeComponents();
  setTclInterp(tcl_interp);
  // Define swig TCL commands.
  Dbsta_Init(tcl_interp);
  // Eval encoded sta TCL sources.
  evalTclInit(tcl_interp, dbsta_tcl_inits);
}

// Wrapper to init network db.
void
dbSta::makeComponents()
{
  Sta::makeComponents();
  db_network_->setDb(db_);
}

void
dbSta::makeNetwork()
{
  db_network_ = new class dbNetwork();
  network_ = db_network_;
}

void
dbSta::makeSdcNetwork()
{
  sdc_network_ = new dbSdcNetwork(network_);
}

void
dbSta::postReadLef(dbTech* tech,
                   dbLib* library)
{
  if (library) {
    db_network_->readLefAfter(library);
  }
}

void
dbSta::postReadDef(dbBlock* block)
{
  db_network_->readDefAfter(block);
  db_cbk_.addOwner(block);
  db_cbk_.setNetwork(db_network_);
}

void
dbSta::postReadDb(dbDatabase* db)
{
  db_network_->readDbAfter(db);
  odb::dbChip *chip = db_->getChip();
  if (chip) {
    odb::dbBlock *block = chip->getBlock();
    if (block) {
      db_cbk_.addOwner(block);
      db_cbk_.setNetwork(db_network_);
    }
  }
}

Slack
dbSta::netSlack(const dbNet *db_net,
		const MinMax *min_max)
{
  const Net *net = db_network_->dbToSta(db_net);
  return netSlack(net, min_max);
}

std::set<dbNet*>
dbSta::findClkNets()
{
  ensureClkNetwork();
  std::set<dbNet*> clk_nets;
  for (Clock *clk : sdc_->clks()) {
    for (const Pin *pin : *pins(clk)) {
      Net *net = network_->net(pin);      
      if (net)
	clk_nets.insert(db_network_->staToDb(net));
    }
  }
  return clk_nets;
}

std::set<dbNet*>
dbSta::findClkNets(const Clock *clk)
{
  ensureClkNetwork();
  std::set<dbNet*> clk_nets;
  for (const Pin *pin : *pins(clk)) {
    Net *net = network_->net(pin);      
    if (net)
      clk_nets.insert(db_network_->staToDb(net));
  }
  return clk_nets;
}

////////////////////////////////////////////////////////////////

// Network edit functions.
// These override the default sta functions that call sta before/after
// functions because the db calls them via callbacks.

void
dbSta::deleteInstance(Instance *inst)
{
  NetworkEdit *network = networkCmdEdit();
  network->deleteInstance(inst);
}

void
dbSta::replaceCell(Instance *inst,
                   Cell *to_cell,
                   LibertyCell *to_lib_cell)
{
  NetworkEdit *network = networkCmdEdit();
  LibertyCell *from_lib_cell = network->libertyCell(inst);
  if (sta::equivCells(from_lib_cell, to_lib_cell)) {
    replaceEquivCellBefore(inst, to_lib_cell);
    network->replaceCell(inst, to_cell);
    replaceEquivCellAfter(inst);
  }
  else {
    replaceCellBefore(inst, to_lib_cell);
    network->replaceCell(inst, to_cell);
    replaceCellAfter(inst);
  }
}

void
dbSta::deleteNet(Net *net)
{
  NetworkEdit *network = networkCmdEdit();
  network->deleteNet(net);
}

void
dbSta::connectPin(Instance *inst,
                  Port *port,
                  Net *net)
{
  NetworkEdit *network = networkCmdEdit();
  Pin *pin = network->connect(inst, port, net);
}

void
dbSta::connectPin(Instance *inst,
                  LibertyPort *port,
                  Net *net)
{
  NetworkEdit *network = networkCmdEdit();
  network->connect(inst, port, net);
}

void
dbSta::disconnectPin(Pin *pin)
{
  NetworkEdit *network = networkCmdEdit();
  network->disconnectPin(pin);
}

////////////////////////////////////////////////////////////////
//
// OpenDB callbacks to notify OpenSTA of network edits
//
////////////////////////////////////////////////////////////////

dbStaCbk::dbStaCbk(dbSta *sta) :
  sta_(sta),
  network_(nullptr)  // not built yet
{
}

void
dbStaCbk::setNetwork(dbNetwork *network)
{
  network_ = network;
}

void
dbStaCbk::inDbInstCreate(dbInst* inst)
{
  sta_->makeInstanceAfter(network_->dbToSta(inst));
}

void
dbStaCbk::inDbInstDestroy(dbInst *inst)
{
  // This is called after the iterms have been destroyed
  // so it side-steps Sta::deleteInstanceAfter.
  sta_->deleteLeafInstanceBefore(network_->dbToSta(inst));
}

void
dbStaCbk::inDbInstSwapMasterBefore(dbInst *inst,
                                   dbMaster *master)
{
  LibertyCell *to_lib_cell = network_->libertyCell(network_->dbToSta(master));
  LibertyCell *from_lib_cell = network_->libertyCell(inst);
  Instance *sta_inst = network_->dbToSta(inst);
  if (sta::equivCells(from_lib_cell, to_lib_cell))
    sta_->replaceEquivCellBefore(sta_inst, to_lib_cell);
  else
    ord::error("instance %s swap master %s is not equivalent",
               inst->getConstName(),
               master->getConstName());
}

void
dbStaCbk::inDbInstSwapMasterAfter(dbInst *inst)
{
  sta_->replaceEquivCellAfter(network_->dbToSta(inst));
}

void
dbStaCbk::inDbNetDestroy(dbNet *db_net)
{
  Net *net = network_->dbToSta(db_net);
  sta_->deleteNetBefore(net);
  network_->deleteNetBefore(net);
}

void
dbStaCbk::inDbITermPostConnect(dbITerm *iterm)
{
  sta_->connectPinAfter(network_->dbToSta(iterm));
}

void
dbStaCbk::inDbITermPreDisconnect(dbITerm *iterm)
{
  sta_->disconnectPinBefore(network_->dbToSta(iterm));
}

void
dbStaCbk::inDbITermDestroy(dbITerm *iterm)
{
  sta_->deletePinBefore(network_->dbToSta(iterm));
}

void
dbStaCbk::inDbBTermPostConnect(dbBTerm *bterm)
{
  sta_->connectPinAfter(network_->dbToSta(bterm));
}

void
dbStaCbk::inDbBTermPreDisconnect(dbBTerm *bterm)
{
  sta_->disconnectPinBefore(network_->dbToSta(bterm));
}

void
dbStaCbk::inDbBTermDestroy(dbBTerm *bterm)
{
  sta_->deletePinBefore(network_->dbToSta(bterm));
}

////////////////////////////////////////////////////////////////

// Highlight path in the gui.
void
dbSta::highlight(PathRef *path)
{
  if (gui_) {
    if (path_renderer_ == nullptr) {
      path_renderer_ = new PathRenderer(this);
      gui_->register_renderer(path_renderer_);
    }
    path_renderer_->highlight(path);
  }
}

PathRenderer::PathRenderer(dbSta *sta) :
  sta_(sta),
  path_(nullptr)
{
}

PathRenderer::~PathRenderer()
{
  delete path_;
}

void
PathRenderer::highlight(PathRef *path)
{
  if (path_)
    delete path_;
  path_ = new PathExpanded(path, sta_);
}

void
PathRenderer::drawObjects(gui::Painter &painter)
{
  if (path_) {
    dbNetwork *network = sta_->getDbNetwork();
    Point prev_pt;
    for (int i = 0; i < path_->size(); i++) {
      PathRef *path = path_->path(i);
      TimingArc *prev_arc = path_->prevArc(i);
      // Draw lines for wires on the path.
      if (prev_arc && prev_arc->role()->isWire()) {
        PathRef *prev_path = path_->path(i - 1); 
        const Pin *pin = path->pin(sta_);
        const Pin *prev_pin = prev_path->pin(sta_);
        Point pt1 = network->location(pin);
        Point pt2 = network->location(prev_pin);
        gui::Painter::Color color = sta_->isClock(pin)
          ? gui::Painter::yellow
          : gui::Painter::red;
        painter.setPen(color, true);
        painter.drawLine(pt1, pt2);

        // Color in the instances to make them more visible.
        const Instance *inst = network->instance(pin);
        if (!network->isTopInstance(inst)) {
          dbInst *db_inst = network->staToDb(inst);
          odb::dbBox *bbox = db_inst->getBBox();
          odb::Rect rect;
          bbox->getBox(rect);
          painter.setBrush(color);
          painter.drawRect(rect);
        }
      }
    }
  }
}

} // namespace sta
