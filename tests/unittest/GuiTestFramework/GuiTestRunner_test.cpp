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
#include "GuiTestFramework/GuiTestRunner.h"

#include <tcl.h>

#include <QElapsedTimer>
#include <QKeyEvent>
#include <QPushButton>
#include <QTest>
#include <QTextBlock>
#include <QTimer>

#include "GuiTestFramework/GuiTestCommands.h"
#include "GuiTestFramework/GuiTestHub.h"
#include "GuiTestFramework/GuiTestScriptView.h"
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

// An action waits for its widget when it exists but is not shown yet, as a
// dock opened by the previous action is; an unknown name fails at once.
TEST(GuiTestCommands, ActionWaitsForHiddenWidget) {
  QWidget window;
  auto button = new QPushButton("Late", &window);
  button->setObjectName("lateButton");
  button->hide();
  window.show();
  int clicks = 0;
  QObject::connect(button, &QPushButton::clicked, [&]() { ++clicks; });
  QTimer::singleShot(100, button, &QWidget::show);

  Tcl_Interp *interp = Tcl_CreateInterp();
  registerGuiTestCommands(interp);
  EXPECT_EQ(Tcl_Eval(interp, "gui_click lateButton"), TCL_OK)
      << Tcl_GetStringResult(interp);
  EXPECT_EQ(clicks, 1);

  QElapsedTimer elapsed;
  elapsed.start();
  EXPECT_EQ(Tcl_Eval(interp, "gui_click noSuchButton"), TCL_ERROR);
  EXPECT_LT(elapsed.elapsed(), 1000);
  Tcl_DeleteInterp(interp);
}

// The script view grays finished commands, highlights running ones (all lines
// of a command that spans several), marks the next one, and is no test target.
TEST(GuiTestScriptView, ShowsProgressByCommand) {
  GuiTestScriptView view{"/x/demo.tcl",
                         "# gui-test: sync\na\n# note\nb {\n  c}\nd\ne"};
  EXPECT_EQ(view.windowTitle().toStdString(), "Replay demo.tcl");
  EXPECT_TRUE(GuiTestHub::isInternal(&view));
  EXPECT_EQ(view.document()->findBlockByNumber(1).text().toStdString(), "2  a");

  const QVector<GuiTestScriptView::Span> commands{
      {2, 1}, {4, 2}, {6, 1}, {7, 1}};
  view.showProgress(commands, {1}, 2);
  std::vector<std::pair<int, std::string>> got;
  for (const QTextEdit::ExtraSelection &s : view.extraSelections()) {
    const bool marked = s.format.hasProperty(QTextFormat::BackgroundBrush);
    const std::string kind =
        marked ? s.format.background().color().name().toStdString() : "done";
    got.emplace_back(s.cursor.blockNumber() + 1, kind);
  }
  const std::string running = QColor{255, 236, 140}.name().toStdString();
  const std::string next = QColor{205, 225, 255}.name().toStdString();
  EXPECT_EQ(got, (std::vector<std::pair<int, std::string>>{
                     {2, "done"}, {4, running}, {5, running}, {6, next}}));

  view.showProgress(commands, {}, 4);
  EXPECT_EQ(view.extraSelections().size(), 5);
  for (const QTextEdit::ExtraSelection &s : view.extraSelections())
    EXPECT_FALSE(s.format.hasProperty(QTextFormat::BackgroundBrush));
}
