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
#include "GuiTestFramework/GuiTestHub.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QFileInfo>
#include <QProcess>
#include <thread>

#include "Compiler/TaskManager.h"
#include "GuiTestFramework/GuiMilestone.h"
#include "GuiTestFramework/GuiTestCommands.h"
#include "gtest/gtest.h"

using namespace FOEDAG;

namespace {

class GuiTestHubTest : public ::testing::Test {
 protected:
  void SetUp() override { GuiTestHub::instance()->enable(); }
  void TearDown() override { GuiTestHub::instance()->disable(); }

  static bool wait(const QString &name, const QString &arg = {},
                   QString *error = nullptr) {
    QString ignored;
    return waitForMilestone(name, arg, 300, error ? error : &ignored);
  }
};

}  // namespace

// U-014 (a): raised inside the action, before the wait runs.
TEST_F(GuiTestHubTest, MilestoneDuringActionSatisfiesWait) {
  GuiTestHub::instance()->takeSnapshot();
  GUI_MILESTONE("test.done", "");
  EXPECT_TRUE(wait("test.done"));
}

// U-014 (b): raised before the action is not counted.
TEST_F(GuiTestHubTest, MilestoneBeforeActionIsIgnored) {
  GUI_MILESTONE("test.done", "");
  GuiTestHub::instance()->takeSnapshot();
  EXPECT_FALSE(wait("test.done"));
}

// U-014 (c): raised later from a worker thread.
TEST_F(GuiTestHubTest, MilestoneFromWorkerThreadSatisfiesWait) {
  GuiTestHub::instance()->takeSnapshot();
  std::thread worker{[]() { GUI_MILESTONE("test.done", "worker"); }};
  worker.join();
  EXPECT_TRUE(wait("test.done", "worker"));
}

// U-014 (d): nothing raised; the message lists the recent milestones.
TEST_F(GuiTestHubTest, TimeoutListsRecentMilestones) {
  GUI_MILESTONE("test.other", "x");
  GuiTestHub::instance()->takeSnapshot();
  QString error;
  EXPECT_FALSE(wait("test.done", {}, &error));
  EXPECT_TRUE(error.contains("gui_wait_event test.done")) << error.toStdString();
  EXPECT_TRUE(error.contains("test.other x")) << error.toStdString();
}

TEST_F(GuiTestHubTest, ArgumentIsMatchedOnlyWhenGiven) {
  GuiTestHub::instance()->takeSnapshot();
  GUI_MILESTONE("test.done", "a");
  EXPECT_TRUE(wait("test.done"));
  EXPECT_TRUE(wait("test.done", "a"));
  EXPECT_FALSE(wait("test.done", "b"));
}

// U-015: automatic events.
TEST_F(GuiTestHubTest, DialogShowAndClose) {
  QDialog dialog;
  dialog.setObjectName("testDialog");
  GuiTestHub::instance()->takeSnapshot();
  dialog.show();
  EXPECT_TRUE(wait("dialog.shown", "testDialog"));
  GuiTestHub::instance()->takeSnapshot();
  dialog.close();
  EXPECT_TRUE(wait("dialog.closed", "testDialog"));
}

TEST_F(GuiTestHubTest, ProcessStartAndFinish) {
  QObject owner;
  auto process = new QProcess{&owner};
  QCoreApplication::processEvents();  // the hub connects on the next turn
  GuiTestHub::instance()->takeSnapshot();
  process->start(QCoreApplication::applicationFilePath(),
                 {"--gtest_list_tests", "--gtest_filter=-*"});
  const QString program =
      QFileInfo(QCoreApplication::applicationFilePath()).fileName();
  EXPECT_TRUE(wait("process.started", program));
  QString error;
  EXPECT_TRUE(waitForMilestone("process.finished", program, 10000, &error))
      << error.toStdString();
}

TEST_F(GuiTestHubTest, OverrideCursor) {
  GuiTestHub::instance()->takeSnapshot();
  QApplication::setOverrideCursor(Qt::WaitCursor);
  EXPECT_TRUE(wait("busy.on"));
  QApplication::restoreOverrideCursor();
  EXPECT_TRUE(wait("busy.off"));
}

// U-015: a bridged signal on an object created after the hub was enabled.
TEST_F(GuiTestHubTest, TaskManagerBridge) {
  TaskManager taskManager{nullptr};
  GuiTestHub::instance()->takeSnapshot();
  emit taskManager.started();
  emit taskManager.done();
  EXPECT_TRUE(wait("task.started"));
  EXPECT_TRUE(wait("task.done"));
}

TEST_F(GuiTestHubTest, DisabledHubCountsNothing) {
  GuiTestHub::instance()->disable();
  GUI_MILESTONE("test.done", "");
  EXPECT_EQ(GuiTestHub::instance()->count("test.done", {}), 0);
}

TEST(GuiTestHub, Kinds) {
  EXPECT_EQ(GuiTestHub::kindOf("task.done"), GuiTestHub::Kind::Sync);
  EXPECT_EQ(GuiTestHub::kindOf("dialog.shown"), GuiTestHub::Kind::Sync);
  EXPECT_EQ(GuiTestHub::kindOf("task.started"), GuiTestHub::Kind::Info);
  EXPECT_EQ(GuiTestHub::kindOf("busy.on"), GuiTestHub::Kind::Info);
}
