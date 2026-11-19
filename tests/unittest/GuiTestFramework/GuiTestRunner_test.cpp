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

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QIntValidator>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <functional>

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

// A file dialog rewrites a typed path to the bare file name unless its field
// has focus. gui_set_text types with focus, as a user does, even when another
// window is active, such as the replay window after Run.
TEST(GuiTestCommands, SetTextInFileDialogKeepsPath) {
  QTemporaryDir dir;
  const QString file = dir.filePath("demo.ospr");
  QFile{file}.open(QIODevice::WriteOnly);
  QWidget other;
  other.show();
  QFileDialog dialog{nullptr, "Open", dir.path()};
  dialog.setOption(QFileDialog::DontUseNativeDialog);
  dialog.show();
  QTest::qWait(500);  // the dialog lists the directory in the background
  QApplication::setActiveWindow(&other);
  ASSERT_EQ(QApplication::activeWindow(), &other);

  Tcl_Interp *interp = Tcl_CreateInterp();
  registerGuiTestCommands(interp);
  const QByteArray command = "gui_set_text fileNameEdit " + file.toUtf8();
  EXPECT_EQ(Tcl_Eval(interp, command.constData()), TCL_OK)
      << Tcl_GetStringResult(interp);
  Tcl_DeleteInterp(interp);
}

// Each edit of the first field rebuilds the form, as the IP parameters do:
// the new fields are shown by a queued call, and the old ones deleted later.
// The next action must type into the new field, not the old one.
TEST(GuiTestCommands, SetTextFindsFieldOfRebuiltForm) {
  QWidget window;
  auto layout = new QVBoxLayout(&window);
  QLineEdit *second{nullptr};
  QWidget *form{nullptr};
  std::function<void()> build = [&]() {
    if (form) {
      layout->removeWidget(form);
      form->deleteLater();
    }
    form = new QWidget;
    auto formLayout = new QVBoxLayout(form);
    auto first = new QLineEdit;
    first->setObjectName("first");
    second = new QLineEdit;
    second->setObjectName("second");
    formLayout->addWidget(first);
    formLayout->addWidget(second);
    layout->addWidget(form);
    QObject::connect(first, &QLineEdit::editingFinished, &window,
                     [&]() { build(); });
  };
  build();
  window.show();
  QTest::qWait(50);

  Tcl_Interp *interp = Tcl_CreateInterp();
  registerGuiTestCommands(interp);
  ASSERT_EQ(Tcl_Eval(interp, "gui_set_text first 12"), TCL_OK)
      << Tcl_GetStringResult(interp);
  ASSERT_EQ(Tcl_Eval(interp, "gui_set_text second 500"), TCL_OK)
      << Tcl_GetStringResult(interp);
  EXPECT_EQ(second->text(), "500");
  Tcl_DeleteInterp(interp);
}

// Qt finishes an edit only when the validator accepts the text; a rejected
// value leaves the field invalid without editingFinished.
TEST(GuiTestCommands, SetTextFinishesOnlyAcceptableEdits) {
  QWidget window;
  auto edit = new QLineEdit(&window);
  edit->setObjectName("depth");
  edit->setValidator(new QIntValidator(4, 64, edit));
  int finished = 0;
  QObject::connect(edit, &QLineEdit::editingFinished, [&]() { ++finished; });
  window.show();

  Tcl_Interp *interp = Tcl_CreateInterp();
  registerGuiTestCommands(interp);
  ASSERT_EQ(Tcl_Eval(interp, "gui_set_text depth 1"), TCL_OK)
      << Tcl_GetStringResult(interp);
  EXPECT_EQ(finished, 0);
  ASSERT_EQ(Tcl_Eval(interp, "gui_set_text depth 16"), TCL_OK)
      << Tcl_GetStringResult(interp);
  EXPECT_EQ(finished, 1);
  Tcl_DeleteInterp(interp);
}

// A field that warns in a modal dialog when it loses focus, as the IP
// Configurator's invalid fields do.
class WarnOnFocusOut : public QObject {
 public:
  bool warn{false};
  int warnings{0};

 protected:
  bool eventFilter(QObject *, QEvent *event) override {
    if (event->type() == QEvent::FocusOut && warn) {
      ++warnings;
      QDialog box;
      QTimer::singleShot(0, &box, &QDialog::reject);
      box.exec();
    }
    return false;
  }
};

// A press moves focus before the click. A dialog the old focus widget opens on
// losing it takes the click, as it does with a real mouse.
TEST(GuiTestCommands, ClickMovesFocusAndADialogOnFocusOutTakesIt) {
  QWidget window;
  window.setLayout(new QVBoxLayout);
  auto edit = new QLineEdit;
  edit->setObjectName("depth");
  window.layout()->addWidget(edit);
  auto button = new QPushButton("Generate");
  button->setObjectName("generate");
  window.layout()->addWidget(button);
  int clicked = 0;
  QObject::connect(button, &QPushButton::clicked, [&]() { ++clicked; });
  WarnOnFocusOut field;
  edit->installEventFilter(&field);
  window.show();
  QApplication::setActiveWindow(&window);
  ASSERT_TRUE(QTest::qWaitForWindowActive(&window));

  Tcl_Interp *interp = Tcl_CreateInterp();
  registerGuiTestCommands(interp);
  edit->setFocus();
  ASSERT_EQ(Tcl_Eval(interp, "gui_click generate"), TCL_OK)
      << Tcl_GetStringResult(interp);
  EXPECT_EQ(clicked, 1);
  EXPECT_TRUE(button->hasFocus());

  edit->setFocus();
  field.warn = true;
  ASSERT_EQ(Tcl_Eval(interp, "gui_click generate"), TCL_OK)
      << Tcl_GetStringResult(interp);
  EXPECT_EQ(field.warnings, 1);
  EXPECT_EQ(clicked, 1);
  Tcl_DeleteInterp(interp);
}

// A context menu opened on press, as the Sources tree's is, needs the button
// held and the cursor on the item. It blocks the click until a later line
// picks from it; the pick closes it.
TEST(GuiTestCommands, RightClickOpensMenuThatALaterLinePicksFrom) {
  QTreeWidget tree;
  tree.setObjectName("sourcesTree");
  new QTreeWidgetItem(&tree, QStringList{"fifo"});
  tree.show();
  QAction add{"Add IP to Design", &tree};
  add.setObjectName("actionAddIpToDesign");
  int added = 0;
  QObject::connect(&add, &QAction::triggered, [&]() { ++added; });
  Qt::MouseButtons heldOnPress;
  QPoint cursorOnPress;
  QMenu *shown{nullptr};
  QObject::connect(&tree, &QTreeWidget::itemPressed, [&]() {
    heldOnPress = qApp->mouseButtons();
    cursorOnPress = QCursor::pos();
    QMenu menu{&tree};
    menu.addAction(&add);
    shown = &menu;
    menu.exec(QCursor::pos());
    shown = nullptr;
  });

  Tcl_Interp *interp = Tcl_CreateInterp();
  registerGuiTestCommands(interp);
  bool menuWasOpen{false};
  int picked = TCL_ERROR;
  QTimer pick;
  QObject::connect(&pick, &QTimer::timeout, [&]() {
    if (!shown || !shown->isVisible()) return;
    pick.stop();
    menuWasOpen = true;
    picked = Tcl_Eval(interp, "gui_trigger_action actionAddIpToDesign");
  });
  pick.start(10);
  QTimer::singleShot(5000, [&]() {
    if (shown) shown->close();
  });
  ASSERT_EQ(Tcl_Eval(interp, "gui_click sourcesTree -item fifo -button right"),
            TCL_OK)
      << Tcl_GetStringResult(interp);
  EXPECT_EQ(heldOnPress, Qt::RightButton);
  EXPECT_EQ(cursorOnPress,
            tree.viewport()->mapToGlobal(
                tree.visualItemRect(tree.topLevelItem(0)).center()));
  EXPECT_TRUE(menuWasOpen);
  EXPECT_EQ(picked, TCL_OK);
  EXPECT_EQ(added, 1);
  EXPECT_EQ(shown, nullptr) << "the pick closed the menu";
  EXPECT_EQ(qApp->mouseButtons(), Qt::NoButton);
  EXPECT_EQ(tree.currentItem(), tree.topLevelItem(0));
  Tcl_DeleteInterp(interp);
}

// A widget that asks for context menu requests gets one per right click.
TEST(GuiTestCommands, RightClickRequestsContextMenu) {
  QTreeWidget tree;
  tree.setObjectName("sourcesTree");
  new QTreeWidgetItem(&tree, QStringList{"fifo"});
  tree.setContextMenuPolicy(Qt::CustomContextMenu);
  int requests = 0;
  QObject::connect(&tree, &QWidget::customContextMenuRequested,
                   [&]() { ++requests; });
  tree.show();

  Tcl_Interp *interp = Tcl_CreateInterp();
  registerGuiTestCommands(interp);
  ASSERT_EQ(Tcl_Eval(interp, "gui_click sourcesTree -item fifo -button right"),
            TCL_OK)
      << Tcl_GetStringResult(interp);
  EXPECT_EQ(requests, 1);
  EXPECT_EQ(Tcl_Eval(interp, "gui_click sourcesTree -button right -double"),
            TCL_ERROR);
  Tcl_DeleteInterp(interp);
}

// The script view grays finished commands, highlights running ones (all lines
// of a command that spans several), marks the next one, lists each line's
// screenshots, and is no test target.
TEST(GuiTestScriptView, ShowsProgressByCommand) {
  const QVector<GuiTestScriptView::Span> commands{
      {2, 1}, {4, 2}, {6, 1}, {7, 1}};
  GuiTestScriptView view{"/x/demo.tcl",
                         "# gui-test: sync\na\n# note\nb {\n  c}\nd\ne",
                         commands};
  QTreeWidget *tree = view.tree();
  EXPECT_EQ(view.windowTitle().toStdString(), "Replay demo.tcl");
  EXPECT_TRUE(GuiTestHub::isInternal(tree));
  EXPECT_EQ(tree->topLevelItem(1)->text(0).toStdString(), "2  a");

  auto state = [tree](int line) -> std::string {
    const QTreeWidgetItem *item = tree->topLevelItem(line - 1);
    const QVariant fill = item->data(0, Qt::BackgroundRole);
    if (fill.isValid())
      return fill.value<QBrush>().color().name().toStdString();
    return item->data(0, Qt::ForegroundRole).isValid() ? "done" : "";
  };
  const std::string running = QColor{255, 236, 140}.name().toStdString();
  const std::string next = QColor{205, 225, 255}.name().toStdString();
  view.showProgress({1}, 2);
  EXPECT_EQ(std::vector<std::string>({state(1), state(2), state(3), state(4),
                                      state(5), state(6), state(7)}),
            std::vector<std::string>(
                {"", "done", "", running, running, next, ""}));

  view.showProgress({}, 4);
  EXPECT_EQ(std::vector<std::string>({state(2), state(4), state(5), state(6),
                                      state(7)}),
            std::vector<std::string>({"done", "done", "done", "done", "done"}));

  view.addScreenshot(6, "/out/shots/demo/01_a.png");
  view.addScreenshot(6, "/out/shots/demo/02_b.png");
  EXPECT_EQ(tree->topLevelItem(5)->text(1).toStdString(), "01_a, 02_b");
  EXPECT_EQ(view.screenshots(6), QStringList({"/out/shots/demo/01_a.png",
                                              "/out/shots/demo/02_b.png"}));
  EXPECT_TRUE(view.screenshots(2).isEmpty());
}

// A double click sets a breakpoint on the command of that line, or clears
// it; comment lines take none. Run is a button on top.
TEST(GuiTestScriptView, BreakpointsAndRun) {
  GuiTestScriptView view{"/x/demo.tcl",
                         "# gui-test: sync\na\n# note\nb {\n  c}",
                         {{2, 1}, {4, 2}}};
  QTreeWidget *tree = view.tree();
  emit tree->itemDoubleClicked(tree->topLevelItem(4), 0);  // line 5, in "b {"
  EXPECT_FALSE(view.hasBreakpoint(0));
  EXPECT_TRUE(view.hasBreakpoint(1));
  emit tree->itemDoubleClicked(tree->topLevelItem(2), 0);  // the comment
  EXPECT_FALSE(view.hasBreakpoint(0));
  EXPECT_TRUE(view.hasBreakpoint(1));
  emit tree->itemDoubleClicked(tree->topLevelItem(3), 0);
  EXPECT_FALSE(view.hasBreakpoint(1));

  QSignalSpy run{&view, &GuiTestScriptView::runRequested};
  view.runButton()->click();
  EXPECT_EQ(run.count(), 1);
}

// Selected lines copy as written, without their numbers, in script order,
// with Ctrl+C or the context menu's Copy.
TEST(GuiTestScriptView, CopiesSelectedLines) {
  GuiTestScriptView view{"/x/demo.tcl", "# gui-test: sync\na\nb {\n  c}",
                         {{2, 1}, {3, 2}}};
  QTreeWidget *tree = view.tree();
  tree->topLevelItem(3)->setSelected(true);
  tree->topLevelItem(1)->setSelected(true);
  EXPECT_EQ(view.selectedScript().toStdString(), "a\n  c}");

  QGuiApplication::clipboard()->clear();
  ASSERT_EQ(tree->actions().size(), 1);
  QAction *copy = tree->actions().first();
  EXPECT_EQ(copy->shortcut(), QKeySequence{QKeySequence::Copy});
  copy->trigger();
  EXPECT_EQ(QGuiApplication::clipboard()->text().toStdString(), "a\n  c}");
}

// At the end of a debug run the view marks the failing command red, shows
// the message, disables Run, and says when it is closed.
TEST(GuiTestScriptView, ShowsResult) {
  GuiTestScriptView view{"/x/demo.tcl", "# gui-test: sync\na\nb {\n  c}",
                         {{2, 1}, {3, 2}}};
  QTreeWidget *tree = view.tree();
  view.showResult(1, "boom");
  const QColor red{255, 170, 170};
  EXPECT_EQ(tree->topLevelItem(2)->background(0).color(), red);
  EXPECT_EQ(tree->topLevelItem(3)->background(0).color(), red);
  EXPECT_NE(tree->topLevelItem(1)->background(0).color(), red);
  EXPECT_FALSE(view.runButton()->isEnabled());
  bool shown = false;
  for (QLabel *label : view.findChildren<QLabel *>())
    shown = shown || label->text().startsWith("Failed at line 3: boom");
  EXPECT_TRUE(shown);

  QSignalSpy closed{&view, &GuiTestScriptView::closed};
  view.show();
  view.close();
  EXPECT_EQ(closed.count(), 1);
}

// Stepping holds every line; Run stops it, and a breakpoint starts it again.
TEST(GuiTestSpaceGate, RunsToBreakpoint) {
  GuiTestSpaceGate gate;
  gate.setStepping(false);
  EXPECT_FALSE(gate.arrive(false));
  EXPECT_TRUE(gate.isOpen());
  EXPECT_TRUE(gate.arrive(true));
  EXPECT_FALSE(gate.isOpen());
  EXPECT_TRUE(gate.isStepping());
  EXPECT_TRUE(gate.arrive(false));
  EXPECT_FALSE(gate.isOpen());

  gate.run();
  EXPECT_TRUE(gate.isOpen());
  EXPECT_FALSE(gate.arrive(false));
  EXPECT_TRUE(gate.isOpen());
}
