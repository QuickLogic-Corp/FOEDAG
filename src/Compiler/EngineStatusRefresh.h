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
#pragma once

#include "nlohmann_json/json.hpp"

#include <QObject>
#include <QTimer>
#include <functional>
#include <string>

namespace FOEDAG {

class TclInterpreter;

// GUI task statuses under the flow engine (aurora2 docs/specs/aurora-flow-engine
// row 1.12, REQ-124): events within the debounce window make one query; the
// commands are described on the GUI thread, the engine's plan runs on a worker,
// and the answer is applied back on the GUI thread unless a newer query or a
// task made it stale. A query due while a task runs waits for the task to end.
class EngineStatusRefresh : public QObject {
 public:
  static constexpr int DEBOUNCE_MS = 300;
  // GUI thread: the work to run off it; an empty function asks nothing.
  using Describe = std::function<std::function<nlohmann::json()>()>;
  // GUI thread: what the work returned.
  using Apply = std::function<void(const nlohmann::json&)>;
  using Busy = std::function<bool()>;

  EngineStatusRefresh(Describe describe, Apply apply, Busy busy,
                      int debounceMs = DEBOUNCE_MS, QObject* parent = nullptr);

  // GUI thread. Each call restarts the debounce window.
  void request();
  // Any thread: a task ended; a query skipped meanwhile runs now.
  void taskFinished();

  // Bumped by every request and query; an answer from an older one is dropped.
  quint64 generation() const { return m_generation; }

  // The test-only Tcl command for aurora2's T-024 events script, registered only
  // when AURORA_EVENT_LOOP_PROBE is set: `flow_engine_test_event settings |
  // design_files | pcf_saved`. `handler` returns false for an unknown event.
  // True when the command was registered.
  static bool registerTestEventCommand(TclInterpreter* interp,
                                       std::function<bool(const std::string&)> handler);

 private:
  void fire();
  void deliver(quint64 generation, const nlohmann::json& answer);

  Describe m_describe;
  Apply m_apply;
  Busy m_busy;
  QTimer m_timer;
  quint64 m_generation = 0;
  bool m_skippedForTask = false;
};

}  // namespace FOEDAG
