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
#include "EngineRunLock.h"

#include <fstream>

namespace fs = std::filesystem;

namespace FOEDAG {

static const std::string LOCK_HELD = "lock-held: ";

static fs::path normal(const fs::path& path) { return path.lexically_normal(); }

// The engine's unlock rule without the engine: remove the lock only if it carries
// `token`. For the exit path and for an unlock the engine could not do.
static void removeIfMine(const fs::path& project, const std::string& token) {
  if (token.empty()) return;
  const fs::path file = project / ".aurora" / "lock";
  std::ifstream in(file);
  const nlohmann::json held = nlohmann::json::parse(in, nullptr, false);
  in.close();
  const auto found = held.is_object() ? held.find("token") : held.end();
  if (found != held.end() && found->is_string() && found->get<std::string>() == token) {
    std::error_code ec;
    fs::remove(file, ec);
  }
}

EngineRunLock& EngineRunLock::instance() {
  static EngineRunLock lock;
  return lock;
}

EngineRunLock::Result EngineRunLock::hold(const FlowEngineClient& client, const fs::path& project,
                                          qint64 ownerPid) {
  std::lock_guard<std::mutex> call(m_callMutex);
  {
    std::lock_guard<std::mutex> state(m_stateMutex);
    // A lock already held here, or kept after an unlock that failed.
    if (normal(project) == m_project && (m_count > 0 || !m_token.empty())) {
      ++m_count;
      return {Outcome::Held, {}};
    }
  }
  // The project changed under a hold: its old lock goes, the new one is taken. A
  // lock left naming this live process would refuse every other run there.
  const bool unlocked = unlockCalling();
  {
    std::lock_guard<std::mutex> state(m_stateMutex);
    if (!unlocked) removeIfMine(m_project, m_token);
    m_token.clear();
    m_project.clear();
  }

  const FlowEngineClient::Result result =
      client.call({"lock", "--project", project.u8string(), "--owner-pid", std::to_string(ownerPid)});
  Result out;
  std::string token;
  if (result.ok) {
    const auto answer = result.doc.find("token");
    if (answer != result.doc.end() && answer->is_string()) token = answer->get<std::string>();
    out.outcome = token.empty() ? Outcome::Unavailable : Outcome::Held;
    if (token.empty()) out.message = "the engine's lock answer has no token";
  } else if (result.error.rfind(LOCK_HELD, 0) == 0) {
    return {Outcome::Refused, result.error.substr(LOCK_HELD.size())};
  } else {
    // A timed-out call may still have written a lock naming this pid; the
    // engine replaces it when this process next asks.
    out.message = result.error;
  }
  std::lock_guard<std::mutex> state(m_stateMutex);
  m_token = token;
  m_project = normal(project);
  m_client = client;
  ++m_count;
  return out;
}

void EngineRunLock::drop() {
  std::lock_guard<std::mutex> call(m_callMutex);
  {
    std::lock_guard<std::mutex> state(m_stateMutex);
    if (m_count == 0 || --m_count > 0) return;
  }
  // A lock that could not be removed is kept: the next hold here adopts it.
  if (unlockCalling()) {
    std::lock_guard<std::mutex> state(m_stateMutex);
    m_project.clear();
  }
}

bool EngineRunLock::unlockCalling() {
  std::string token;
  fs::path project;
  FlowEngineClient client;
  {
    std::lock_guard<std::mutex> state(m_stateMutex);
    token = m_token;
    project = m_project;
    client = m_client;
  }
  if (token.empty()) return true;
  const FlowEngineClient::Result result =
      client.call({"unlock", "--project", project.u8string(), "--token", token});
  if (!result.ok) return false;
  std::lock_guard<std::mutex> state(m_stateMutex);
  m_token.clear();
  return true;
}

void EngineRunLock::releaseAtExit() {
  std::lock_guard<std::mutex> state(m_stateMutex);
  removeIfMine(m_project, m_token);
  m_token.clear();
  m_project.clear();
  m_count = 0;
}

std::string EngineRunLock::token(const fs::path& project) const {
  std::lock_guard<std::mutex> state(m_stateMutex);
  return normal(project) == m_project ? m_token : std::string{};
}

int EngineRunLock::count() const {
  std::lock_guard<std::mutex> state(m_stateMutex);
  return m_count;
}

EngineRunLock::Result EngineRunHolds::begin(const FlowEngineClient& client,
                                            const fs::path& project, qint64 ownerPid,
                                            bool batchScript) {
  const EngineRunLock::Result result = m_lock.hold(client, project, ownerPid);
  if (result.outcome == EngineRunLock::Outcome::Refused) return result;
  // Already held, so the span's hold only counts.
  if ((m_spanWanted || batchScript) && !m_spanHeld.exchange(true)) {
    m_spanUntilExit = batchScript;
    m_lock.hold(client, project, ownerPid);
  }
  return result;
}

void EngineRunHolds::end() { m_lock.drop(); }

void EngineRunHolds::sequenceStarted() { m_spanWanted = true; }

// TaskManager also says done() after every batch task; a batch span ends at exit.
bool EngineRunHolds::sequenceDone() {
  m_spanWanted = false;
  return !m_spanUntilExit && m_spanHeld.exchange(false);
}

}  // namespace FOEDAG
