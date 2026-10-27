/*
Copyright 2026 The Foedag team

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
#include "EngineBackend.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace fs = std::filesystem;

namespace FOEDAG {

namespace {

constexpr int PROTOCOL_VERSION = 1;
constexpr const char* SYNPLIFY_PROFILE = "synplify";

using Action = Compiler::Action;

// Matches SynthesisTool in project_manager.h, which this file does not include.
const std::string YOSYS_TOOL_ID = "0";
const std::string SYNPLIFY_TOOL_ID = "1";

fs::path absolute(const fs::path& path, const fs::path& project) {
  return (path.is_relative() ? project / path : path).lexically_normal();
}

std::string utf8(const fs::path& path) { return path.u8string(); }

// The engine's own key-material pattern, *_supp.db, case-insensitive.
bool isKeyDatabase(const fs::path& path) {
  std::string name = path.filename().string();
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const std::string suffix = "_supp.db";
  return name.size() >= suffix.size() &&
         name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string join(const nlohmann::json& reasons) {
  std::string text;
  for (const auto& reason : reasons) {
    if (!text.empty()) text += "; ";
    text += reason.is_string() ? reason.get<std::string>() : reason.dump();
  }
  return text;
}

}  // namespace

EngineBackend::EngineBackend(Compiler* compiler, FlowEngineClient client,
                             EngineContextProvider context)
    : m_compiler(compiler), m_client(std::move(client)), m_context(std::move(context)) {}

std::string EngineBackend::stageName(int taskId) {
  switch (static_cast<Action>(taskId)) {
    case Action::Synthesis:
      return "synth";
    case Action::Pack:
      return "pack";
    case Action::Detailed:
      return "place";
    case Action::Routing:
      return "route";
    case Action::STA:
      return "sta";
    case Action::Power:
      return "power";
    case Action::Bitstream:
      return "bitstream";
    default:
      return {};
  }
}

bool EngineBackend::decides(const std::string& stage) {
  // Floorplan, STA, power and bitstream join with row 1.11.
  return stage == "synth" || stage == "pack" || stage == "place" || stage == "route";
}

std::string EngineBackend::profileName(int taskId, const std::string& profile) {
  if (static_cast<Action>(taskId) == Action::Synthesis) {
    if (profile == SYNPLIFY_TOOL_ID) return SYNPLIFY_PROFILE;
    if (profile == YOSYS_TOOL_ID) return {};
  }
  return profile;
}

std::vector<std::string> EngineBackend::ownedOutputs(const std::string& stage,
                                                     const std::string& profile,
                                                     const EngineStageContext& ctx) {
  const std::string& name = ctx.projectName;
  if (stage == "synth") {
    if (profile == SYNPLIFY_PROFILE) {
      // Synplify's netlist is the Yosys pass's input.
      return ctx.top.empty() ? std::vector<std::string>{}
                             : std::vector<std::string>{ctx.top + "/" + ctx.top + ".vm"};
    }
    return {name + "_post_synth.blif", name + "_post_synth.eblif",
            name + "_post_synth.v",    name + "_post_synth.edif",
            name + "_post_synth.vhd",  name + ".clocks"};
  }
  if (stage == "pack") {
    std::vector<std::string> outputs{name + "_post_synth.net"};
    if (ctx.layoutResolvedInPack) {
      for (const char* file : {"device_layout.json", "pack_layout.json", "vpr_generated.xml",
                               "sb_maps_generated.yml"}) {
        outputs.emplace_back(file);
      }
    }
    return outputs;
  }
  if (stage == "place") return {name + "_post_synth.place", name + "_place2pcf.pcf"};
  if (stage == "route") return {name + "_post_synth.route"};
  return {};
}

bool EngineBackend::hasRecords(const fs::path& project) {
  std::error_code ec;
  const fs::path state = project / ".aurora" / "state";
  for (fs::directory_iterator it(state, ec), end; !ec && it != end; it.increment(ec)) {
    const fs::path& file = it->path();
    if (file.extension() == ".json" && file.filename() != "digests.json") return true;
  }
  return false;
}

nlohmann::json EngineBackend::request(const std::string& op, const std::string& stage,
                                      const std::string& profile, const CommandWrapper& command,
                                      const EngineStageContext& ctx) const {
  nlohmann::json req{{"v", PROTOCOL_VERSION}, {"op", op},      {"project", utf8(ctx.project)},
                     {"stage", stage},        {"profile", profile}, {"argv", command.argv()}};
  if (!ctx.deviceDir.empty()) req["device_dir"] = utf8(ctx.deviceDir);

  std::vector<std::string> outputs;
  std::set<fs::path> owned;
  for (const std::string& output : ownedOutputs(stage, profile, ctx)) {
    const fs::path path = absolute(output, ctx.project);
    outputs.push_back(utf8(path));
    owned.insert(path);
  }

  std::vector<fs::path> declared = command.declaredPaths();
  nlohmann::json temps = nlohmann::json::object();
  nlohmann::json scripts = nlohmann::json::array();
  if (const ScriptRendererPtr& renderer = command.scriptRenderer()) {
    declared.insert(declared.end(), renderer->files().begin(), renderer->files().end());
    for (const auto& [path, mask] : renderer->maskedFiles()) temps[path] = mask;
    scripts.push_back({{"name", command.scriptName().empty() ? stage + ".script"
                                                              : command.scriptName()},
                       {"text", renderer->renderForEngine()}});
  }
  for (const auto& [path, mask] : command.temps()) temps[path] = mask;

  // Every declared path, existing or not; a directory stands for its files.
  // Sorted, so the key does not depend on the order a builder declares them.
  std::set<std::string> inputs;
  auto addInput = [&](const fs::path& path) {
    if (!owned.count(path)) inputs.insert(utf8(path));
  };
  for (const fs::path& path : declared) {
    const fs::path resolved = absolute(path, ctx.project);
    std::error_code ec;
    if (fs::is_directory(resolved, ec)) {
      std::vector<fs::path> files = FileUtils::FindAbsoluteFilePathsRecursively(resolved);
      std::sort(files.begin(), files.end());
      for (const fs::path& file : files) addInput(file.lexically_normal());
    } else {
      addInput(resolved);
    }
  }

  nlohmann::json hostDigests = nlohmann::json::object();
  for (const auto& [path, digest] : command.hostDigests()) {
    hostDigests[utf8(absolute(path, ctx.project))] = digest;
  }
  std::vector<std::string> keyPaths;
  std::set<fs::path> keys;
  for (const fs::path& path : command.keyPaths()) {
    keyPaths.push_back(utf8(path));
    keys.insert(absolute(path, ctx.project));
  }
  const std::vector<std::string>& argv = command.argv();
  for (size_t i = 0; i + 1 < argv.size(); ++i) {
    if (argv[i] == "--crypt_key_db") keys.insert(absolute(fs::u8path(argv[i + 1]), ctx.project));
  }
  // The engine refuses to read key material (REQ-012), so a key input such as
  // VPR's --crypt_key_db reaches it as a host digest.
  for (const std::string& input : inputs) {
    const fs::path path = fs::u8path(input);
    if (hostDigests.contains(input) || !(keys.count(path) || isKeyDatabase(path))) continue;
    const std::string digest = FlowEngineClient::sha256File(path);
    if (!digest.empty()) hostDigests[input] = digest;
  }

  req["inputs"] = inputs;
  req["outputs"] = outputs;
  req["scripts"] = scripts;
  req["temps"] = temps;
  req["host_digests"] = hostDigests;
  req["key_paths"] = keyPaths;
  req["session_state"] = ctx.sessionState;
  req["external"] = ctx.external;
  return req;
}

void EngineBackend::warnOnce(const std::string& message) const {
  if (m_warned) return;
  m_warned = true;
  m_compiler->Message("Warning: flow engine: " + message);
}

bool EngineBackend::isCompilationRequired(int taskId, const std::string& profile,
                                          const CommandWrapperPtr& command) const {
  const std::string stage = stageName(taskId);
  if (!command || !decides(stage)) return true;
  const EngineStageContext ctx = m_context(stage);
  // Pack sets the generated-layout members only when it runs; restoring them
  // on a skip is row 1.10.
  if (stage == "pack" && ctx.layoutResolvedInPack) return true;
  if (!m_client.usable()) {
    warnOnce("no bundled interpreter or engine in this install; every stage runs");
    return true;
  }

  const bool inTask = this->inTask();
  nlohmann::json req = request("check", stage, profileName(taskId, profile), *command, ctx);
  if (inTask) req["begin"] = true;  // a stage about to run loses its record first
  const FlowEngineClient::Result result = m_client.call({"check"}, &req);
  if (!result.ok) {
    if (inTask) {
      m_compiler->Message("Warning: flow engine cannot decide " + stage + " (" +
                          result.error + "); running it");
    } else {
      warnOnce("status query failed (" + result.error + "); stages show as not up to date");
    }
    return true;
  }
  const std::string verdict = result.doc.value("verdict", std::string{"run"});
  if (inTask && verdict == "run") {
    m_compiler->Message("Flow engine: " + stage + " runs: " +
                        join(result.doc.value("reasons", nlohmann::json::array())));
  }
  return verdict != "skip" && verdict != "external";
}

void EngineBackend::storeTaskCommand(int taskId, const std::string& profile,
                                     const CommandWrapperPtr& command) {
  const std::string stage = stageName(taskId);
  if (!command || !decides(stage) || !m_client.usable()) return;
  Pending pending{taskId, request("record", stage, profileName(taskId, profile), *command,
                                  m_context(stage))};
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_pending.empty() && std::this_thread::get_id() == m_taskThread) {
      m_pending.back().push_back(std::move(pending));
      return;
    }
  }
  record(pending);
}

void EngineBackend::record(const Pending& pending) {
  const FlowEngineClient::Result result = m_client.call({"record"}, &pending.request);
  if (!result.ok) {
    m_compiler->Message("Warning: flow engine cannot record " +
                        pending.request.value("stage", std::string{}) + " (" + result.error +
                        "); it runs next time");
    return;
  }
  // REQ-125: the C++ cache's entries for this task and every later one are stale.
  CppBackend::forgetTasks(m_projectPath, pending.taskId, static_cast<int>(Action::Bitstream));
}

void EngineBackend::taskStarted() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_pending.empty()) m_taskThread = std::this_thread::get_id();
  m_pending.emplace_back();
}

void EngineBackend::taskFinished(bool ok) {
  std::vector<Pending> records;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_pending.empty()) return;
    records = std::move(m_pending.back());
    m_pending.pop_back();
  }
  if (!ok) return;
  for (const Pending& pending : records) record(pending);
}

bool EngineBackend::taskRunning() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return !m_pending.empty();
}

bool EngineBackend::inTask() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return !m_pending.empty() && std::this_thread::get_id() == m_taskThread;
}

bool EngineBackend::isEmpty() const { return !hasRecords(m_projectPath); }

void EngineBackend::clear() {
  if (!hasRecords(m_projectPath)) return;
  const FlowEngineClient::Result result =
      m_client.call({"invalidate", "--all", "--project", utf8(m_projectPath)});
  if (!result.ok) {
    m_compiler->ErrorMessage("flow engine cannot clear the compilation cache: " + result.error);
  }
}

void EngineBackend::setProjectPath(const fs::path& path) {
  m_projectPath = path;
  ScriptRenderer::setProjectPath(path);
  CommandWrapper::setProjectPath(path);
}

}  // namespace FOEDAG
