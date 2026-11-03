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

EngineRunLock& EngineRunLock::instance() {
  static EngineRunLock lock;
  return lock;
}

EngineRunLock::Result EngineRunLock::hold(const FlowEngineClient& client, const fs::path& project,
                                          qint64 ownerPid) {
  std::lock_guard<std::mutex> guard(m_mutex);
  // A lock already held here, or kept after an unlock that failed.
  if (normal(project) == m_project && (m_count > 0 || !m_token.empty())) {
    ++m_count;
    return {Outcome::Held, {}};
  }
  // The project changed under a hold: its old lock goes, the new one is taken.
  unlockLocked();
  m_token.clear();
  m_project.clear();

  const FlowEngineClient::Result result =
      client.call({"lock", "--project", project.u8string(), "--owner-pid", std::to_string(ownerPid)});
  Result out;
  if (result.ok) {
    const auto token = result.doc.find("token");
    m_token = token != result.doc.end() && token->is_string() ? token->get<std::string>()
                                                              : std::string{};
    out.outcome = m_token.empty() ? Outcome::Unavailable : Outcome::Held;
    if (m_token.empty()) out.message = "the engine's lock answer has no token";
  } else if (result.error.rfind(LOCK_HELD, 0) == 0) {
    return {Outcome::Refused, result.error.substr(LOCK_HELD.size())};
  } else {
    out.message = result.error;
  }
  m_project = normal(project);
  m_client = client;
  ++m_count;
  return out;
}

void EngineRunLock::drop() {
  std::lock_guard<std::mutex> guard(m_mutex);
  if (m_count == 0 || --m_count > 0) return;
  // A lock that could not be removed is kept: the next hold here adopts it.
  if (unlockLocked()) m_project.clear();
}

bool EngineRunLock::unlockLocked() {
  if (m_token.empty()) return true;
  const FlowEngineClient::Result result =
      m_client.call({"unlock", "--project", m_project.u8string(), "--token", m_token});
  if (!result.ok) return false;
  m_token.clear();
  return true;
}

void EngineRunLock::releaseAtExit() {
  std::lock_guard<std::mutex> guard(m_mutex);
  if (!m_token.empty()) {
    const fs::path file = m_project / ".aurora" / "lock";
    std::ifstream in(file);
    const nlohmann::json held = nlohmann::json::parse(in, nullptr, false);
    in.close();
    // The engine's unlock rule: only a lock carrying this run's token is removed.
    const auto token = held.is_object() ? held.find("token") : held.end();
    const bool mine =
        token != held.end() && token->is_string() && token->get<std::string>() == m_token;
    if (mine) {
      std::error_code ec;
      fs::remove(file, ec);
    }
  }
  m_token.clear();
  m_project.clear();
  m_count = 0;
}

std::string EngineRunLock::token(const fs::path& project) const {
  std::lock_guard<std::mutex> guard(m_mutex);
  return normal(project) == m_project ? m_token : std::string{};
}

int EngineRunLock::count() const {
  std::lock_guard<std::mutex> guard(m_mutex);
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
void EngineRunHolds::sequenceDone() {
  m_spanWanted = false;
  if (!m_spanUntilExit && m_spanHeld.exchange(false)) m_lock.drop();
}

}  // namespace FOEDAG
