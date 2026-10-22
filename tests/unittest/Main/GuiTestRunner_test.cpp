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
#include "Main/GuiTestRunner.h"

#include <QKeyEvent>
#include <QPushButton>
#include <QTest>

#include "gtest/gtest.h"

using namespace FOEDAG;

namespace {

class KeyCounter : public QObject {
 public:
  int presses{0};

 protected:
  bool eventFilter(QObject *, QEvent *event) override {
    if (event->type() == QEvent::KeyPress) ++presses;
    return false;
  }
};

}  // namespace

// U-019
TEST(GuiTestSpaceGate, OpensOnlyOnSpaceAndSwallowsIt) {
  QPushButton button{"button"};
  button.show();
  button.setFocus();
  int clicks = 0;
  QObject::connect(&button, &QPushButton::clicked, [&clicks]() { ++clicks; });
  KeyCounter counter;
  button.installEventFilter(&counter);

  GuiTestSpaceGate gate;
  gate.arm();
  QTest::keyClick(&button, Qt::Key_A);
  EXPECT_FALSE(gate.isOpen());
  EXPECT_EQ(counter.presses, 1) << "another key reaches the widget";

  QTest::keyClick(&button, Qt::Key_Space);
  EXPECT_TRUE(gate.isOpen());
  EXPECT_EQ(counter.presses, 1) << "the Space is swallowed";

  gate.arm();
  EXPECT_FALSE(gate.isOpen());
  QTest::keyClick(&button, Qt::Key_Space);
  EXPECT_TRUE(gate.isOpen());
  EXPECT_EQ(clicks, 0);

  // Not armed: Space reaches the button again, so the checks above can fail.
  QTest::keyClick(&button, Qt::Key_Space);
  EXPECT_EQ(clicks, 1);
}

// U-019, timeout
TEST(GuiTestSpaceGate, OpensAfterTimeoutWithoutSpace) {
  GuiTestSpaceGate gate{200};
  gate.arm();
  EXPECT_FALSE(gate.isOpen());
  QTest::qWait(100);
  EXPECT_FALSE(gate.isOpen());
  QTest::qWait(150);
  EXPECT_TRUE(gate.isOpen());
  gate.arm();
  EXPECT_FALSE(gate.isOpen()) << "each arming starts a new wait";

  GuiTestSpaceGate spaceOnly;  // default -1
  spaceOnly.arm();
  QTest::qWait(250);
  EXPECT_FALSE(spaceOnly.isOpen());

  GuiTestSpaceGate immediate{0};
  immediate.arm();
  EXPECT_TRUE(immediate.isOpen());
}
