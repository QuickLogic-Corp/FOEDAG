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

#include <QAbstractScrollArea>
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <iostream>
#include <sstream>

#include "GuiTestFramework/GuiMilestone.h"
#include "GuiTestFramework/GuiTestHub.h"
#include "TextEditor/editor.h"
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
  clock = 10000;
  GUI_MILESTONE("x.done", "a");
  clock = 11000;
  QTest::mouseClick(button, Qt::LeftButton);

  EXPECT_EQ(lines(true), join({"gui_click runButton",
                                      "gui_wait_event y.done -timeout 60000",
                                      "gui_wait_event x.done a -timeout 100000",
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
  EXPECT_EQ(lines(),
            join({"gui_assert {[gui_get nameEdit -text] eq \"a \\\"b\\\"\"}",
                  "gui_screenshot recording/01 -widget win",
                  "gui_assert {![gui_get optButton checked]}",
                  "gui_screenshot recording/02 -widget win"}));
}

// Ctrl+Shift+S in a window with no focused widget, such as Floor Planning,
// takes a screenshot of that window.
TEST_F(GuiRecorderTest, CheckpointWithoutFocusShotsActiveWindow) {
  window.show();
  QWidget other;
  other.setObjectName("otherWindow");
  other.show();
  QApplication::setActiveWindow(&other);
  ASSERT_EQ(QApplication::activeWindow(), &other);
  ASSERT_EQ(QApplication::focusWidget(), nullptr);
  record();

  QTest::keyClick(&other, Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
  EXPECT_EQ(lines(),
            join({"gui_screenshot recording/01 -widget otherWindow"}));
}

// The chosen text is recorded even when a handler rebuilds the items, as the
// Pin Planner does when a pin is taken.
TEST_F(GuiRecorderTest, ComboRecordsChosenTextAfterRebuild) {
  auto combo = add(new QComboBox, "pinCombo");
  combo->addItems({"", "p1", "p2", "p3"});
  QObject::connect(combo, &QComboBox::activated, combo, [combo]() {
    const QString chosen = combo->currentText();
    combo->clear();
    combo->addItems({"", "p3", chosen, "p2"});
  });
  window.show();
  record();

  QTest::keyClick(combo, Qt::Key_Down);  // "" -> p1, then p3 is at its index
  EXPECT_EQ(lines(), join({"gui_select pinCombo p1"}));
}

// A double-click on an item replays as one gui_click, not as a select first.
TEST_F(GuiRecorderTest, DoubleClickIsOneLine) {
  auto tree = add(new QTreeWidget, "taskTree");
  auto synth = new QTreeWidgetItem(tree, {"synth"});
  auto place = new QTreeWidgetItem(tree, {"place"});
  window.show();
  record();

  QTest::mouseClick(tree->viewport(), Qt::LeftButton, {},
                    tree->visualItemRect(synth).center());
  QTest::mouseDClick(tree->viewport(), Qt::LeftButton, {},
                     tree->visualItemRect(place).center());
  EXPECT_EQ(lines(), join({"gui_select taskTree synth",
                           "gui_click taskTree -item place -double"}));
}

// The preview window shows the script as it is written, raises no window
// milestone, and input in it is not recorded.
TEST_F(GuiRecorderTest, PreviewShowsScriptAndIsNotRecorded) {
  auto tree = add(new QTreeWidget, "taskTree");
  auto synth = new QTreeWidgetItem(tree, {"synth"});
  window.show();
  QTemporaryDir dir;
  const QString file = dir.filePath("rec.tcl");
  const int shown = GuiTestHub::instance()->count("window.shown", {});
  QString error;
  ASSERT_TRUE(GuiRecorder::instance()->start(file, &error))
      << error.toStdString();
  QWidget *preview = nullptr;
  for (QWidget *w : QApplication::topLevelWidgets())
    if (w->isVisible() && w->windowTitle() == "Recording rec.tcl")
      preview = w;
  ASSERT_NE(preview, nullptr);
  EXPECT_EQ(GuiTestHub::instance()->count("window.shown", {}), shown);
  auto script = preview->findChild<QPlainTextEdit *>();
  auto pending = preview->findChild<QLabel *>();
  ASSERT_TRUE(script && pending);

  QTest::mouseClick(tree->viewport(), Qt::LeftButton, {},
                    tree->visualItemRect(synth).center());
  EXPECT_TRUE(pending->isVisible());
  EXPECT_EQ(pending->text().toStdString(), "gui_select taskTree synth");
  QTest::keyClick(script, Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
  QTest::mouseClick(script->viewport(), Qt::LeftButton);
  EXPECT_TRUE(pending->isVisible()) << "a click in the preview is no new input";

  GuiRecorder::instance()->flush();
  EXPECT_FALSE(pending->isVisible());
  QFile written(file);
  ASSERT_TRUE(written.open(QIODevice::ReadOnly | QIODevice::Text));
  const QString text = QString::fromUtf8(written.readAll());
  EXPECT_EQ(text.toStdString(),
            "# gui-test: sync\ngui_start\ngui_select taskTree synth\n");
  EXPECT_EQ((script->toPlainText() + "\n").toStdString(), text.toStdString());
}

// accept/reject name a button only when no other button has that answer.
TEST_F(GuiRecorderTest, AmbiguousDialogButtonsUseTheirText) {
  window.show();
  record();

  QMessageBox box{QMessageBox::Question, "Save", "Save changes?",
                  QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
                  &window};
  box.setModal(true);
  for (auto button : {QMessageBox::Yes, QMessageBox::No, QMessageBox::Cancel}) {
    box.show();
    QTest::mouseClick(box.button(button), Qt::LeftButton);
  }
  // As gui_dialog clicks: the dialog closes before the recorder sees clicked.
  QDialog dialog{&window};
  dialog.setModal(true);
  auto buttons = new QDialogButtonBox(
      QDialogButtonBox::Open | QDialogButtonBox::Cancel, &dialog);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                   &QDialog::accept);
  dialog.show();
  {
    GuiRecorder::Frame frame{&dialog};
    buttons->button(QDialogButtonBox::Open)->click();
  }
  EXPECT_EQ(lines(), join({"gui_dialog accept", "gui_dialog button No",
                           "gui_dialog button Cancel", "gui_dialog accept"}));
}

// Typing in the text editor is one gui_set_text with the final text; loading
// the file is not recorded.
TEST_F(GuiRecorderTest, EditorTypingIsOneSetText) {
  QTemporaryDir dir;
  const QString file = dir.filePath("x.pcf");
  {
    QFile f{file};
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("a\n");
  }
  auto editor = new Editor(file, FILE_TYPE_UNKOWN);
  window.layout()->addWidget(editor);
  auto button = add(new QPushButton("Run"), "runButton");
  window.show();
  record();

  auto text = editor->findChild<QAbstractScrollArea *>();
  ASSERT_NE(text, nullptr);
  text->setFocus();
  QTest::keyClicks(text, "b[c]");
  editor->reload();
  QTest::mouseClick(button, Qt::LeftButton);
  {
    GuiRecorder::Frame frame{editor};  // as gui_set_text does it
    editor->setText("x\ty\n");
  }
  EXPECT_EQ(lines(), join({"gui_set_text editor_x.pcf \"b\\[c\\]a\\n\"",
                           "gui_click runButton",
                           "gui_set_text editor_x.pcf \"x\\ty\\n\""}));
}

// A click that takes focus from a field finishes the text typed in it. The
// text is written first, as the user typed it first.
TEST_F(GuiRecorderTest, TextFinishedByAClickIsWrittenBeforeIt) {
  auto edit = add(new QLineEdit, "depth");
  auto button = add(new QPushButton("Generate"), "generate");
  window.show();
  record();

  edit->setText("1");
  {
    GuiRecorder::Frame frame{button};  // as gui_click does it
    GuiRecorder::instance()->aboutToClick(button);
    emit edit->editingFinished();
  }
  EXPECT_EQ(lines(), join({"gui_set_text depth 1", "gui_click generate"}));
}

// The trace writes a command the way the recorder would (G-013).
TEST(GuiRecorderTrace, WritesCommandsAsRecorded) {
  const QByteArray path = (GuiRecorder::projectDir() + "/sub/a b.v").toUtf8();
  const char *argv[] = {"gui_set_text", "editor_x.pcf", "a\nb [c]",
                        path.constData(), "two words"};
  EXPECT_EQ(traceLine(5, argv).toStdString(),
            "gui_set_text editor_x.pcf \"a\\nb \\[c\\]\" "
            "\"$::gui_test::project_dir/sub/a b.v\" {two words}");
}

// A right-click names the item a context menu is for; replay selects it the
// same way before the menu's action runs.
TEST_F(GuiRecorderTest, RightClickOnItemIsRecorded) {
  auto tree = add(new QTreeWidget, "taskTree");
  new QTreeWidgetItem(tree, {"synth"});
  auto place = new QTreeWidgetItem(tree, {"place"});
  window.show();
  record();

  QTest::mouseClick(tree->viewport(), Qt::RightButton, {},
                    tree->visualItemRect(place).center());
  EXPECT_EQ(lines(), join({"gui_click taskTree -item place -button right"}));
}

// A context-menu action is a child of its view and is added to a widget only
// while its menu is open. Triggered without the menu, as in a replay, it is
// still recorded.
TEST_F(GuiRecorderTest, ContextMenuActionWithoutMenuIsRecorded) {
  auto tree = add(new QTreeWidget, "taskTree");
  auto action = new QAction("Remove", tree);
  action->setObjectName("actionRemove");
  window.show();
  record();

  {
    GuiRecorder::Frame frame{action};  // as gui_trigger_action does it
    action->trigger();
  }
  EXPECT_EQ(lines(), join({"gui_trigger_action actionRemove"}));
}
