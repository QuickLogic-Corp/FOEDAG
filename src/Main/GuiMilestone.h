/*
Copyright 2026 The Foedag team

GPL License

Copyright (c) 2026 The Open-Source FPGA Foundation

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once

#include <QString>

class QObject;

// Header-only so that libraries linked after foedagcore (compiler, tools) can
// raise milestones; GuiTestHub fills the sinks in when it is enabled.
namespace FOEDAG::GuiTest {
using MilestoneSink = void (*)(const QString &name, const QString &arg);
using BridgeSink = void (*)(QObject *object);

inline bool milestonesEnabled{false};
inline MilestoneSink milestoneSink{nullptr};
inline BridgeSink bridgeSink{nullptr};
}  // namespace FOEDAG::GuiTest

#define GUI_MILESTONE(name, arg)                    \
  do {                                              \
    if (FOEDAG::GuiTest::milestonesEnabled)         \
      FOEDAG::GuiTest::milestoneSink((name), (arg)); \
  } while (0)

// Connects the object's signals listed in the hub's bridge table.
#define GUI_TEST_BRIDGE(object)                  \
  do {                                           \
    if (FOEDAG::GuiTest::milestonesEnabled)      \
      FOEDAG::GuiTest::bridgeSink(object);       \
  } while (0)
