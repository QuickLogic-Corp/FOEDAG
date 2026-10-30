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
#include <set>
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
  // Parsed metric values the stage consumes (REQ-122); null when it consumes none.
  nlohmann::json metrics = nullptr;
  // The interpreter a stage's helper script runs under (helper_python).
  std::filesystem::path helperPython;
  // Absolute path of the metrics catalogue read; null for the installed default.
  nlohmann::json metricsCatalogue = nullptr;
  // Digests of owned outputs the engine must not read as they are: encrypted
  // files, identified by their plaintext (REQ-121). Project-relative keys.
  std::map<std::string, std::string> outputHostDigests;
};
using EngineContextProvider =
    std::function<EngineStageContext(const std::string& stage, const std::string& profile)>;
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
  // Floorplanning has no Compiler::Action; this id stands for it.
  static constexpr int FLOORPLAN_TASK = 1000;
  // Bitstream's encode sub-step is its own record (aurora2 stages.ENCODE_PROFILE).
  static constexpr const char* ENCODE_PROFILE = "encode";
  // Value options naming key material; the engine refuses to read their files.
  static const std::vector<std::string>& keyOptions();
  // True for the stages this backend decides; the rest always run.
  static bool decides(const std::string& stage);
  // The engine's profile: synthesis keys Yosys as the stage's only profile.
  static std::string profileName(int taskId, const std::string& profile);
  // Project-relative hand-off outputs the stage owns (aurora2 stages.outputs()).
  static std::vector<std::string> ownedOutputs(const std::string& stage,
                                               const std::string& profile,
                                               const EngineStageContext& ctx);
  // Floorplan outputs removed before generate_floorplanning.py runs under the
  // engine, project-relative: an exit 2 (no PCF, no QDC) writes nothing, so a
  // previous run's file must not survive it.
  static std::vector<std::string> floorplanStaleOutputs(const std::string& projectName);
  // Only exit 0 (written) and 2 (nothing to do) are results worth a record.
  static bool floorplanRecords(int status) { return status == 0 || status == 2; }
  // The python3 (python.exe on Windows) on PATH, absolute; empty when none.
  static std::filesystem::path pathPython();
  // A command line built as text, in describe form: the `positional` tokens after
  // the program and the value of each option in `pathOptions` are declared
  // paths, a path in `masks` under its mask; anything else is an argv token.
  static CommandWrapperPtr wrapCommandLine(const std::vector<std::string>& tokens,
                                           size_t positional,
                                           const std::set<std::string>& pathOptions,
                                           const std::map<std::string, std::string>& masks);
  // True when the project has engine decision records.
  static bool hasRecords(const std::filesystem::path& project);
  // Test-only fault hook: AURORA_FAULT_AFTER_TOOL=<stage> fails that stage after
  // its tool succeeded and before it records (aurora2 fault_injection.py).
  static constexpr const char* FAULT_ENV = "AURORA_FAULT_AFTER_TOOL";
  static bool faultRequested(const std::string& stage);
  static std::string faultMessage(const std::string& stage);

  nlohmann::json request(const std::string& op, const std::string& stage,
                         const std::string& profile, const CommandWrapper& command,
                         const EngineStageContext& ctx) const;
  // One plan request over `stages`; a stage whose command could not be built is
  // left out and named in `unbuilt`.
  nlohmann::json planRequest(const std::vector<UpstreamCommand>& stages,
                             std::vector<std::string>* unbuilt) const;
  const FlowEngineClient& client() const { return m_client; }

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
  // The decision itself; isCompilationRequired turns any exception into run.
  bool decide(int taskId, const std::string& profile, const CommandWrapperPtr& command) const;
  void record(const Pending& pending);
  // REQ-018: a stage requested on its own still runs when an upstream stage is
  // stale. One plan request over the upstream stages; any failure counts as stale.
  bool upstreamStale(const std::string& stage, const EngineStageContext& ctx) const;

  UpstreamProvider m_upstream;
};

}  // namespace FOEDAG
