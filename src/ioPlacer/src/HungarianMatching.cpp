/////////////////////////////////////////////////////////////////////////////
//
// BSD 3-Clause License
//
// Copyright (c) 2019, University of California, San Diego.
// All rights reserved.
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

#include "HungarianMatching.h"
#include "openroad/Error.hh"

namespace pin_placer {

using ord::warn;

HungarianMatching::HungarianMatching(Section_t& section, slotVector_t& slots)
    : _netlist(section.net), _slots(slots)
{
  _numIOPins = _netlist.numIOPins();
  _beginSlot = section.beginSlot;
  _endSlot = section.endSlot;
  _numSlots = _endSlot - _beginSlot;
  _nonBlockedSlots = section.numSlots;
  _edge = section.edge;
}

void HungarianMatching::findAssignment(std::vector<Constraint>& constraints)
{
  createMatrix(constraints);
  _hungarianSolver.Solve(_hungarianMatrix, _assignment);
}

void HungarianMatching::createMatrix(std::vector<Constraint>& constraints)
{
  _hungarianMatrix.resize(_nonBlockedSlots);
  int slotIndex = 0;
  for (int i = _beginSlot; i < _endSlot; ++i) {
    int pinIndex = 0;
    Point newPos = _slots[i].pos;
    if (_slots[i].blocked) {
      continue;
    }
    _hungarianMatrix[slotIndex].resize(_numIOPins);
    _netlist.forEachIOPin([&](int idx, IOPin& ioPin) {
      int hpwl = _netlist.computeIONetHPWL(idx, newPos, _edge, constraints);
      _hungarianMatrix[slotIndex][pinIndex] = hpwl;
      pinIndex++;
    });
    slotIndex++;
  }
}

inline bool samePos(Point& a, Point& b)
{
  return (a.x() == b.x() && a.y() == b.y());
}

void HungarianMatching::getFinalAssignment(std::vector<IOPin>& assigment)
{
  size_t rows = _nonBlockedSlots;
  size_t col = 0;
  int slotIndex = 0;
  _netlist.forEachIOPin([&](int idx, IOPin& ioPin) {
    slotIndex = _beginSlot;
    for (size_t row = 0; row < rows; row++) {
      while (_slots[slotIndex].blocked && slotIndex < _slots.size())
        slotIndex++;
      if (_assignment[row] != col) {
        slotIndex++;
        continue;
      }
      if (_hungarianMatrix[row][col] == hungarian_fail) {
        warn("I/O pin %s cannot be placed in the specified region. Not enough space",
             ioPin.getName().c_str());
      }
      ioPin.setPos(_slots[slotIndex].pos);
      assigment.push_back(ioPin);
      Point sPos = _slots[slotIndex].pos;
      for (int i = 0; i < _slots.size(); i++) {
        if (samePos(_slots[i].pos, sPos)) {
          _slots[i].used = true;
          break;
        }
      }
      break;
    }
    col++;
  });
}

}  // namespace pin_placer
