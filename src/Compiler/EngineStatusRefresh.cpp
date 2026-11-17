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
#include "EngineStatusRefresh.h"

#include <QCoreApplication>
#include <QPointer>
#include <QThreadPool>
#include <cstdlib>

#include "Main/EventLoopProbe.h"
#include "Tcl/TclInterpreter.h"

namespace FOEDAG {

EngineStatusRefresh::EngineStatusRefresh(Describe describe, Apply apply, Busy busy,
                                         int debounceMs, QObject* parent)
    : QObject(parent),
      m_describe(std::move(describe)),
      m_apply(std::move(apply)),
      m_busy(std::move(busy)) {
  m_timer.setSingleShot(true);
  m_timer.setInterval(debounceMs);
  QObject::connect(&m_timer, &QTimer::timeout, this, [this] { fire(); });
}

void EngineStatusRefresh::request() {
  ++m_generation;
  m_timer.start();
}

void EngineStatusRefresh::taskStarted() {
  ++m_generation;
  if (m_inFlight > 0) {
    m_skippedForTask = true;
  }
}

void EngineStatusRefresh::taskFinished() {
  QPointer<EngineStatusRefresh> self(this);
  QMetaObject::invokeMethod(
      qApp,
      [self] {
        if (self && self->m_skippedForTask.exchange(false)) {
          self->request();
        }
      },
      Qt::QueuedConnection);
}

void EngineStatusRefresh::fire() {
  if (m_busy()) {
    m_skippedForTask = true;
    return;
  }
  const quint64 generation = ++m_generation;
  // Out from here: a task that starts while the query is described makes it ask again.
  ++m_inFlight;
  std::function<nlohmann::json()> work = m_describe();
  if (!work) {
    --m_inFlight;
    return;
  }
  QPointer<EngineStatusRefresh> self(this);
  QThreadPool::globalInstance()->start([self, generation, work = std::move(work)] {
    nlohmann::json answer = work();
    // Delivered through the application object, which outlives this one.
    QMetaObject::invokeMethod(
        qApp,
        [self, generation, answer = std::move(answer)] {
          if (self) self->deliver(generation, answer);
        },
        Qt::QueuedConnection);
  });
}

void EngineStatusRefresh::deliver(quint64 generation, const nlohmann::json& answer) {
  --m_inFlight;
  if (generation != m_generation) {
    return;  // a newer query is on its way
  }
  if (m_busy()) {
    m_skippedForTask = true;  // the task's records make this answer stale
    return;
  }
  if (answer.is_object() && answer.contains("error")) {
    if (m_onError) m_onError(answer["error"].is_string() ? answer["error"].get<std::string>()
                                                         : answer["error"].dump());
    return;
  }
  m_apply(answer);
}

bool EngineStatusRefresh::registerTestEventCommand(
    TclInterpreter* interp, std::function<bool(const std::string&)> handler) {
  const char* probe = std::getenv(EventLoopProbe::ENV);
  if (!probe || !*probe) {
    return false;
  }
  auto event = [](void* clientData, Tcl_Interp* interp, int argc, const char* argv[]) -> int {
    auto* handler = static_cast<std::function<bool(const std::string&)>*>(clientData);
    if (argc != 2 || !(*handler)(argv[1])) {
      Tcl_AppendResult(interp,
                       "usage: flow_engine_test_event settings|design_files|pcf_saved",
                       nullptr);
      return TCL_ERROR;
    }
    return TCL_OK;
  };
  auto release = [](void* clientData) {
    delete static_cast<std::function<bool(const std::string&)>*>(clientData);
  };
  interp->registerCmd("flow_engine_test_event", event,
                      new std::function<bool(const std::string&)>(std::move(handler)), release);
  return true;
}

}  // namespace FOEDAG
