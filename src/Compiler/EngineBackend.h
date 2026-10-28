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
#include "TaskCompilationStateManager.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace FOEDAG {

// What the host knows about the project and a stage that no command shows.
struct EngineStageContext {
  std::filesystem::path project;
  std::string projectName;
  std::string top;
  std::filesystem::path deviceDir;
  bool layoutResolvedInPack = false;
  bool external = false;  // the stage's output is a user-supplied file (REQ-018)
  nlohmann::json sessionState = nlohmann::json::object();  // REQ-017
};
using EngineContextProvider = std::function<EngineStageContext(const std::string& stage)>;
struct UpstreamCommand {
  int taskId;
  std::string profile;  // as the call sites pass it
  CommandWrapperPtr command;
};
// The describe-form commands of a stage's upstream stages, in flow order.
using UpstreamProvider = std::function<std::vector<UpstreamCommand>(const std::string& stage)>;

// The flow engine's decision (AURORA_INCREMENTAL=engine; aurora2
// docs/specs/aurora-flow-engine rows 1.9 and 1.13). Any engine failure means run.
class EngineBackend : public IncrementalBackend {
public:
  using IncrementalBackend::isCompilationRequired;
  using IncrementalBackend::storeTaskCommand;

  EngineBackend(Compiler* compiler, FlowEngineClient client, EngineContextProvider context);

  bool isCompilationRequired(int taskId, const std::string& profile,
                             const CommandWrapperPtr& command) const override;
  // The decision itself; isCompilationRequired turns any exception into run.
  bool decide(int taskId, const std::string& profile, const CommandWrapperPtr& command) const;
  void storeTaskCommand(int taskId, const std::string& profile,
                        const CommandWrapperPtr& command) override;
  bool isEmpty() const override;
  void clear() override;
  void load() override {}
  void setProjectPath(const std::filesystem::path& path) override;
  void taskStarted() override;
  void taskFinished(bool ok) override;
  bool taskRunning() const override;
  void setUpstreamProvider(UpstreamProvider upstream) { m_upstream = std::move(upstream); }

  // The engine's name for a task; empty for a task with no stage.
  static std::string stageName(int taskId);
  // True for the stages this backend decides; the rest always run.
  static bool decides(const std::string& stage);
  // The engine's profile: synthesis keys Yosys as the stage's only profile.
  static std::string profileName(int taskId, const std::string& profile);
  // Project-relative hand-off outputs the stage owns (aurora2 stages.outputs()).
  static std::vector<std::string> ownedOutputs(const std::string& stage,
                                               const std::string& profile,
                                               const EngineStageContext& ctx);
  // True when the project has engine decision records.
  static bool hasRecords(const std::filesystem::path& project);

  nlohmann::json request(const std::string& op, const std::string& stage,
                         const std::string& profile, const CommandWrapper& command,
                         const EngineStageContext& ctx) const;

private:
  Compiler* m_compiler = nullptr;
  FlowEngineClient m_client;
  EngineContextProvider m_context;
  std::filesystem::path m_projectPath;
  struct Pending {
    int taskId;
    nlohmann::json request;
  };
  // One list of records per running task, stacked per thread: a task runs on a
  // worker thread, a Tcl batch runs its tasks on further threads, and the GUI
  // thread may query statuses meanwhile.
  mutable std::mutex m_mutex;
  std::map<std::thread::id, std::vector<std::vector<Pending>>> m_pending;
  mutable std::atomic<bool> m_warned{false};

  // True when the calling thread is running a task.
  bool inTask() const;

  void warnOnce(const std::string& message) const;
  void record(const Pending& pending);
  // REQ-018: a stage requested on its own still runs when an upstream stage is
  // stale. One plan request over the upstream stages; any failure counts as stale.
  bool upstreamStale(const std::string& stage, const EngineStageContext& ctx) const;

  UpstreamProvider m_upstream;
};

}  // namespace FOEDAG
