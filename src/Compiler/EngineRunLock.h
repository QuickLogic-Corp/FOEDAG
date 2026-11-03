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

#include "FlowEngineClient.h"

#include <QtGlobal>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>

namespace FOEDAG {

// The project's run-wide writer lock (aurora2 REQ-015, design row 1.15a), counted:
// the first hold runs `aurora_engine lock`, the last drop runs `unlock`. The
// token lets this run's own check, record and invalidate calls pass the lock.
class EngineRunLock {
public:
  enum class Outcome {
    Held,         // this process holds the lock (or already did)
    Refused,      // another run holds it: the caller must not run
    Unavailable,  // the engine could not answer: run without the lock (REQ-006)
  };
  struct Result {
    Outcome outcome = Outcome::Unavailable;
    std::string message;  // the engine's refusal or failure
  };

  // The process-wide lock FOEDAG's compiler uses.
  static EngineRunLock& instance();

  Result hold(const FlowEngineClient& client, const std::filesystem::path& project,
              qint64 ownerPid);
  void drop();
  // Process exit: removes the lock this process holds, whatever the count. It
  // reads the file instead of starting the engine, so it is safe in atexit.
  void releaseAtExit();
  // The token to present for `project`; empty when this process holds no lock there.
  std::string token(const std::filesystem::path& project) const;
  int count() const;

private:
  // With m_callMutex held. False when the engine could not remove the lock.
  bool unlockCalling();

  // One lock or unlock call at a time. The engine runs under this one only, so
  // token(), count() and releaseAtExit() never wait for an engine process.
  std::mutex m_callMutex;
  mutable std::mutex m_stateMutex;  // the fields below
  int m_count = 0;
  std::filesystem::path m_project;
  std::string m_token;
  FlowEngineClient m_client;
};

// What FOEDAG holds: each compile task, plus one hold over a GUI task sequence
// or a whole batch script, so no other run starts between their stages.
class EngineRunHolds {
public:
  explicit EngineRunHolds(EngineRunLock& lock): m_lock(lock) {}

  // One task starts. `batchScript`: the span lasts until process exit. Nothing
  // is held when the result is Refused.
  EngineRunLock::Result begin(const FlowEngineClient& client,
                              const std::filesystem::path& project, qint64 ownerPid,
                              bool batchScript);
  void end();
  void sequenceStarted();
  // True when the caller must end() the sequence's span; the GUI calls it on its
  // own thread, so it leaves the engine call to the caller.
  bool sequenceDone();

private:
  EngineRunLock& m_lock;
  std::atomic<bool> m_spanWanted{false};
  std::atomic<bool> m_spanHeld{false};
  std::atomic<bool> m_spanUntilExit{false};
};

}  // namespace FOEDAG
