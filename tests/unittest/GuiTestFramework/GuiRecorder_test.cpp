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
#include "GuiTestFramework/GuiRecorder.h"

#include <QAction>
#include <QBuffer>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTabBar>
#include <QTabWidget>
#include <QTest>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <iostream>
#include <sstream>

#include "GuiTestFramework/GuiMilestone.h"
#include "GuiTestFramework/GuiTestHub.h"
#include "gtest/gtest.h"

using namespace FOEDAG;

namespace {

class GuiRecorderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    GuiTestHub::instance()->enable();
    window.setObjectName("win");
    window.setLayout(new QVBoxLayout);
  }
  void TearDown() override {
    GuiRecorder::instance()->stop();
    GuiTestHub::instance()->disable();
  }

  template <typename W>
  W *add(W *widget, const QString &name) {
    widget->setObjectName(name);
    window.layout()->addWidget(widget);
    return widget;
  }

  // Call once the widgets are shown, so their window.shown is not recorded.
  void record() {
    buffer.open(QIODevice::ReadWrite);
    GuiRecorder *r = GuiRecorder::instance();
    r->setDirsForTest("/work/run", "/opt/aurora");
    r->setClockForTest([this]() { return clock; });
    r->start(&buffer);
  }

  // The recorded lines after the header, without waits unless asked.
  std::string lines(bool waits = false) {
    GuiRecorder::instance()->flush();
    QStringList all =
        QString::fromUtf8(buffer.data()).split('\n', Qt::SkipEmptyParts);
    EXPECT_EQ(all.value(0), "# gui-test: sync");
    EXPECT_EQ(all.value(1), "gui_start");
    QStringList result;
    for (const QString &line : all.mid(2))
      if (waits || !line.startsWith("gui_wait_event")) result << line;
    return result.join("\n").toStdString();
  }

  static std::string join(const QStringList &expected) {
    return expected.join("\n").toStdString();
  }

  QWidget window;
  QBuffer buffer;
  qint64 clock{0};
};

}  // namespace

// U-011: each widget type maps to its command.
TEST_F(GuiRecorderTest, WidgetTypesMapToCommands) {
  auto button = add(new QPushButton("Run"), "runButton");
  auto combo = add(new QComboBox, "modeCombo");
  combo->addItems({"fast", "slow mode"});
  auto tree = add(new QTreeWidget, "taskTree");
  tree->setColumnCount(2);
  auto item = new QTreeWidgetItem(tree, {"synth", "done"});
  auto tabs = add(new QTabWidget, "pages");
  tabs->addTab(new QWidget, "General");
  tabs->addTab(new QWidget, "Advanced");
  auto edit = add(new QLineEdit, "nameEdit");
  QAction action{"Exit"};
  action.setObjectName("actionExit");
  window.addAction(&action);
  window.show();
  record();

  QTest::mouseClick(button, Qt::LeftButton);
  QTest::keyClick(combo, Qt::Key_Down);
  QTest::mouseClick(tree->viewport(), Qt::LeftButton, {},
                    tree->visualItemRect(item).center() +
                        QPoint(tree->columnWidth(0), 0));
  QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, {},
                    tabs->tabBar()->tabRect(1).center());
  QTest::keyClicks(edit, "two words");
  QTest::keyClick(edit, Qt::Key_Return);
  {
    GuiRecorder::Frame frame{&action};  // as gui_trigger_action does it
    action.trigger();
  }

  QDialog dialog{&window};
  dialog.setModal(true);
  auto box = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  dialog.show();
  QTest::mouseClick(box->button(QDialogButtonBox::Ok), Qt::LeftButton);
  dialog.show();
  QTest::mouseClick(box->button(QDialogButtonBox::Cancel), Qt::LeftButton);

  EXPECT_EQ(lines(), join({"gui_click runButton",
                                  "gui_select modeCombo {slow mode}",
                                  "gui_select taskTree done -column 1",
                                  "gui_select pages Advanced",
                                  "gui_set_text nameEdit {two words}",
                                  "gui_trigger_action actionExit",
                                  "gui_dialog accept", "gui_dialog reject"}));
}

// U-012: an unnamed widget gives a comment and a warning, no gui_* line.
TEST_F(GuiRecorderTest, UnnamedWidgetIsReported) {
  auto button = new QPushButton("Press");
  window.layout()->addWidget(button);
  window.show();
  record();

  std::ostringstream err;
  std::streambuf *old = std::cerr.rdbuf(err.rdbuf());
  QTest::mouseClick(button, Qt::LeftButton);
  const QString recorded = QString::fromStdString(lines());
  std::cerr.rdbuf(old);

  EXPECT_FALSE(recorded.contains('\n')) << recorded.toStdString();
  EXPECT_TRUE(recorded.startsWith("# UNRECORDED: QPushButton Press < "))
      << recorded.toStdString();
  EXPECT_TRUE(recorded.contains("QWidget(win)"));
  EXPECT_NE(err.str().find("WARNING"), std::string::npos);
}

// U-013: paths under the project or install dir use their variables.
TEST_F(GuiRecorderTest, PathsAreRewritten) {
  auto edit = add(new QLineEdit, "pathEdit");
  window.show();
  record();

  for (const char *path : {"/work/run", "/work/run/sub/a b.v",
                           "/opt/aurora/share/x", "/elsewhere/f"}) {
    edit->clear();
    QTest::keyClicks(edit, path);
    QTest::keyClick(edit, Qt::Key_Return);
  }
  EXPECT_EQ(lines(), join({
                "gui_set_text pathEdit $::gui_test::project_dir",
                "gui_set_text pathEdit \"$::gui_test::project_dir/sub/a b.v\"",
                "gui_set_text pathEdit \"$::gui_test::install_dir/share/x\"",
                "# absolute path, not under the project or install directory",
                "gui_set_text pathEdit /elsewhere/f"}));
}

// U-016: one wait per sync milestone kind, for its last occurrence.
TEST_F(GuiRecorderTest, WaitsAreInsertedFromMilestones) {
  auto button = add(new QPushButton("Run"), "runButton");
  window.show();
  record();

  QTest::mouseClick(button, Qt::LeftButton);  // clock 0
  clock = 100;
  GUI_MILESTONE("x.done", "a");
  clock = 200;
  GUI_MILESTONE("y.done", "");
  GUI_MILESTONE("task.started", "");  // info: never waited for
  clock = 4000;
  GUI_MILESTONE("x.done", "a");
  clock = 5000;
  QTest::mouseClick(button, Qt::LeftButton);

  EXPECT_EQ(lines(true), join({"gui_click runButton",
                                      "gui_wait_event y.done -timeout 10000",
                                      "gui_wait_event x.done a -timeout 12000",
                                      "gui_click runButton"}));
}

// Checkpoints (REQ-026) write asserts for the focused widget's value.
TEST_F(GuiRecorderTest, CheckpointWritesAsserts) {
  auto edit = add(new QLineEdit("a \"b\""), "nameEdit");
  auto check = add(new QPushButton("Opt"), "optButton");
  check->setCheckable(true);
  window.show();
  record();

  GuiRecorder::instance()->checkpoint(edit);
  GuiRecorder::instance()->checkpoint(check);
  EXPECT_EQ(lines(), join({"gui_assert {[gui_get nameEdit -text] eq \"a \\\"b\\\"\"}",
                         "gui_assert {![gui_get optButton checked]}"}));
}
