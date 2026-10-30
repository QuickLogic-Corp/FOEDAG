/*
Copyright 2026 QuickLogic team

GPL License

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

#include "Compiler/EngineStatusRefresh.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <string>
#include <vector>

#include "Main/EventLoopProbe.h"
#include "Tcl/TclInterpreter.h"
#include "gtest/gtest.h"

using namespace FOEDAG;

namespace {

void runEventLoop(int ms) {
  QEventLoop loop;
  QTimer::singleShot(ms, &loop, &QEventLoop::quit);
  loop.exec();
}

// Counts describes and applies; each answer names the describe that asked.
struct Probe {
  int describes = 0;
  std::vector<int> applied;
  bool busy = false;
  int workMs = 0;
  std::atomic<bool> workOnGuiThread{false};
  std::function<void()> onDescribe;  // GUI thread, as the query is described

  std::unique_ptr<EngineStatusRefresh> make(int debounceMs = 50) {
    QThread* gui = QThread::currentThread();
    return std::make_unique<EngineStatusRefresh>(
        [this, gui] {
          const int n = ++describes;
          if (onDescribe) onDescribe();
          const int ms = workMs;
          return std::function<nlohmann::json()>([this, n, ms, gui] {
            if (QThread::currentThread() == gui) workOnGuiThread = true;
            QThread::msleep(ms);
            return nlohmann::json(n);
          });
        },
        [this](const nlohmann::json& answer) { applied.push_back(answer.get<int>()); },
        [this] { return busy; }, debounceMs);
  }
};

TEST(EngineStatusRefresh, events_in_the_window_make_one_query_off_the_gui_thread) {
  Probe probe;
  auto refresh = probe.make(100);
  refresh->request();
  runEventLoop(30);
  refresh->request();
  runEventLoop(30);
  refresh->request();
  EXPECT_EQ(probe.describes, 0);  // still inside the window
  runEventLoop(400);
  EXPECT_EQ(probe.describes, 1);
  EXPECT_EQ(probe.applied, std::vector<int>{1});
  EXPECT_FALSE(probe.workOnGuiThread);
}

// An answer that arrives after a newer event is dropped; the newer one applies.
TEST(EngineStatusRefresh, a_stale_answer_is_dropped) {
  Probe probe;
  probe.workMs = 300;
  auto refresh = probe.make(20);
  refresh->request();
  runEventLoop(100);  // the first query is now running on the worker
  EXPECT_EQ(probe.describes, 1);
  const quint64 before = refresh->generation();
  refresh->request();
  EXPECT_GT(refresh->generation(), before);
  runEventLoop(900);
  EXPECT_EQ(probe.describes, 2);
  EXPECT_EQ(probe.applied, std::vector<int>{2});
}

// No query while a task runs; the one due runs once the task has finished.
TEST(EngineStatusRefresh, a_query_due_during_a_task_runs_after_it) {
  Probe probe;
  auto refresh = probe.make(20);
  probe.busy = true;
  refresh->request();
  runEventLoop(150);
  EXPECT_EQ(probe.describes, 0);
  probe.busy = false;
  refresh->taskFinished();
  runEventLoop(300);
  EXPECT_EQ(probe.describes, 1);
  EXPECT_EQ(probe.applied, std::vector<int>{1});

  refresh->taskFinished();  // nothing was skipped: nothing to do
  runEventLoop(150);
  EXPECT_EQ(probe.describes, 1);
}

// An answer that comes back while a task runs is stale once the task records.
TEST(EngineStatusRefresh, an_answer_arriving_during_a_task_waits_for_it) {
  Probe probe;
  probe.workMs = 200;
  auto refresh = probe.make(20);
  refresh->request();
  runEventLoop(80);
  probe.busy = true;
  runEventLoop(300);
  EXPECT_TRUE(probe.applied.empty());
  probe.busy = false;
  refresh->taskFinished();
  runEventLoop(500);
  EXPECT_EQ(probe.applied, std::vector<int>{2});
}

// A short task that starts and ends while a query is out: the query saw the
// state before the task, so its answer is dropped and it is asked again.
TEST(EngineStatusRefresh, a_task_inside_a_running_query_makes_it_ask_again) {
  Probe probe;
  probe.workMs = 300;
  auto refresh = probe.make(20);
  refresh->request();
  runEventLoop(100);  // the first query is now running on the worker
  ASSERT_EQ(probe.describes, 1);
  refresh->taskStarted();
  refresh->taskFinished();
  runEventLoop(1000);
  EXPECT_EQ(probe.describes, 2);
  EXPECT_EQ(probe.applied, std::vector<int>{2});

  refresh->taskStarted();  // nothing out: nothing to ask again
  refresh->taskFinished();
  runEventLoop(200);
  EXPECT_EQ(probe.describes, 2);
}

// The answer is already posted when a task starts and ends, before the GUI
// thread gets to it: it still describes the state before the task.
TEST(EngineStatusRefresh, an_answer_queued_before_a_task_is_dropped) {
  Probe probe;
  auto refresh = probe.make(20);
  probe.onDescribe = [&] {
    QTimer::singleShot(0, [&] {
      QThread::msleep(150);  // the worker posts its answer meanwhile
      probe.onDescribe = nullptr;
      refresh->taskStarted();
      refresh->taskFinished();
    });
  };
  refresh->request();
  runEventLoop(600);
  EXPECT_EQ(probe.describes, 2);
  EXPECT_EQ(probe.applied, std::vector<int>{2});
}

bool hasCommand(TclInterpreter& interp, const std::string& name) {
  return interp.evalCmd("info commands " + name) == name;
}

TEST(EngineStatusRefresh, the_test_event_command_needs_the_probe) {
  qunsetenv(EventLoopProbe::ENV);
  TclInterpreter without;
  EXPECT_FALSE(EngineStatusRefresh::registerTestEventCommand(
      &without, [](const std::string&) { return true; }));
  EXPECT_FALSE(hasCommand(without, "flow_engine_test_event"));

  qputenv(EventLoopProbe::ENV, "");
  EXPECT_FALSE(EngineStatusRefresh::registerTestEventCommand(
      &without, [](const std::string&) { return true; }));
  EXPECT_FALSE(hasCommand(without, "flow_engine_test_event"));
  qunsetenv(EventLoopProbe::ENV);
}

TEST(EngineStatusRefresh, the_test_event_command_passes_known_events_on) {
  qputenv(EventLoopProbe::ENV, "/tmp/probe.jsonl");
  TclInterpreter interp;
  std::vector<std::string> events;
  EXPECT_TRUE(EngineStatusRefresh::registerTestEventCommand(
      &interp, [&events](const std::string& event) {
        events.push_back(event);
        return event == "settings" || event == "design_files" || event == "pcf_saved";
      }));
  qunsetenv(EventLoopProbe::ENV);
  ASSERT_TRUE(hasCommand(interp, "flow_engine_test_event"));
  int rc = -1;
  interp.evalCmd("flow_engine_test_event settings", &rc);
  EXPECT_EQ(rc, TCL_OK);
  interp.evalCmd("flow_engine_test_event design_files", &rc);
  EXPECT_EQ(rc, TCL_OK);
  interp.evalCmd("flow_engine_test_event pcf_saved", &rc);
  EXPECT_EQ(rc, TCL_OK);
  interp.evalCmd("flow_engine_test_event bogus", &rc);
  EXPECT_EQ(rc, TCL_ERROR);
  interp.evalCmd("flow_engine_test_event", &rc);
  EXPECT_EQ(rc, TCL_ERROR);
  EXPECT_EQ(events, (std::vector<std::string>{"settings", "design_files", "pcf_saved", "bogus"}));
}

}  // namespace
