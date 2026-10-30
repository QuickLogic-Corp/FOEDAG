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
#include "EngineBackend.h"

#include <QStandardPaths>

#include <algorithm>
#include <cctype>
#include <cstdlib>
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
  if (taskId == FLOORPLAN_TASK) return "floorplan";
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
  return stage == "synth" || stage == "floorplan" || stage == "pack" || stage == "place" ||
         stage == "route" || stage == "sta" || stage == "power" || stage == "bitstream";
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
  if (stage == "floorplan") {
    return {name + "_repack_design_constraint.xml", name + "_constraints.xml",
            name + "_rpm_constraints.xml"};
  }
  if (stage == "pack") {
    std::vector<std::string> outputs{name + "_post_synth.net"};
    if (ctx.layoutResolvedInPack) {
      // Pack deletes the plaintext generated arch once it is encrypted.
      for (const char* file : {"device_layout.json", "pack_layout.json", "vpr_generated.xml.en",
                               "sb_maps_generated.yml", "sb_maps_generated.yml.en"}) {
        outputs.emplace_back(file);
      }
    }
    return outputs;
  }
  if (stage == "place") return {name + "_post_synth.place", name + "_place2pcf.pcf"};
  if (stage == "route") return {name + "_post_synth.route"};
  if (stage == "bitstream") {
    if (profile == ENCODE_PROFILE) {
      return {name + "_fabric_bitstream.bch", name + "_fabric_bitstream.ascon",
              name + "_fabric_bitstream.tag"};
    }
    return {"fabric_bitstream.bit"};
  }
  return {};
}

CommandWrapperPtr EngineBackend::wrapCommandLine(const std::vector<std::string>& tokens,
                                                 size_t positional,
                                                 const std::set<std::string>& pathOptions,
                                                 const std::map<std::string, std::string>& masks) {
  auto command = std::make_shared<CommandWrapper>();
  auto mask = [&masks](const std::string& token) {
    auto it = masks.find(token);
    return it == masks.end() ? std::string{} : it->second;
  };
  for (size_t i = 0; i < tokens.size(); ++i) {
    if (i > 0 && i <= positional) {
      command->appendPath(fs::path{tokens[i]}, mask(tokens[i]));
    } else if (i > positional && pathOptions.count(tokens[i]) && i + 1 < tokens.size()) {
      command->appendPath(tokens[i], fs::path{tokens[i + 1]}, mask(tokens[i + 1]));
      ++i;
    } else {
      command->append(tokens[i]);
    }
  }
  return command;
}

void EngineBackend::declarePackLayoutInputs(CommandWrapper& command,
                                            const fs::path& customLayoutYml,
                                            const fs::path& deviceConfig) {
  if (!customLayoutYml.empty()) command.addProbe("custom_layout", {customLayoutYml});
  if (!deviceConfig.empty()) command.addProbe("device_config", {deviceConfig});
}

std::vector<std::string> EngineBackend::floorplanStaleOutputs(const std::string& projectName) {
  return {projectName + "_constraints.xml", projectName + "_repack_design_constraint.xml"};
}

fs::path EngineBackend::pathPython() {
#ifdef _WIN32
  const QString python = QStandardPaths::findExecutable("python.exe");
#else
  const QString python = QStandardPaths::findExecutable("python3");
#endif
  return python.isEmpty() ? fs::path{} : fs::path(python.toStdWString());
}

const std::vector<std::string>& EngineBackend::keyOptions() {
  static const std::vector<std::string> options{"--crypt_key_db", "--ascon_key", "--ascon_nonce",
                                                "--ascon_ad"};
  return options;
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

bool EngineBackend::faultRequested(const std::string& stage) {
  const char* value = std::getenv(FAULT_ENV);
  return value && !stage.empty() && stage == value;
}

std::string EngineBackend::faultMessage(const std::string& stage) {
  return std::string(FAULT_ENV) + ": failing " + stage + " after its tool succeeded";
}

nlohmann::json EngineBackend::request(const std::string& op, const std::string& stage,
                                      const std::string& profile, const CommandWrapper& command,
                                      const EngineStageContext& ctx) const {
  nlohmann::json req{{"v", PROTOCOL_VERSION}, {"op", op},      {"project", utf8(ctx.project)},
                     {"stage", stage},        {"profile", profile}, {"argv", command.argv()}};
  if (!ctx.deviceDir.empty()) req["device_dir"] = utf8(ctx.deviceDir);

  std::vector<std::string> outputs;
  std::set<fs::path> owned;
  nlohmann::json hostDigests = nlohmann::json::object();
  for (const std::string& output : ownedOutputs(stage, profile, ctx)) {
    const fs::path path = absolute(output, ctx.project);
    outputs.push_back(utf8(path));
    owned.insert(path);
    // A host digest stands for a file; a missing one must stay missing.
    auto digest = ctx.outputHostDigests.find(output);
    std::error_code ec;
    if (digest != ctx.outputHostDigests.end() && fs::exists(path, ec)) {
      hostDigests[utf8(path)] = digest->second;
    }
  }
  for (const fs::path& output : command.declaredOutputs()) {
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
  for (size_t i = 0; i < argv.size(); ++i) {
    for (const std::string& option : keyOptions()) {
      if (argv[i] == option && i + 1 < argv.size()) {
        keys.insert(absolute(fs::u8path(argv[i + 1]), ctx.project));
      } else if (argv[i].rfind(option + "=", 0) == 0) {
        keys.insert(absolute(fs::u8path(argv[i].substr(option.size() + 1)), ctx.project));
      }
    }
  }
  // The engine refuses to read key material (REQ-012), so a key input such as
  // VPR's --crypt_key_db or an encoder key file reaches it as a host digest.
  for (const std::string& input : inputs) {
    const fs::path path = fs::u8path(input);
    if (hostDigests.contains(input) || !(keys.count(path) || isKeyDatabase(path))) continue;
    const std::string digest = FlowEngineClient::sha256File(path);
    if (!digest.empty()) hostDigests[input] = digest;
  }

  nlohmann::json probes = nlohmann::json::array();
  for (const auto& [role, candidates] : command.probes()) {
    std::vector<std::string> paths;
    for (const fs::path& candidate : candidates) paths.push_back(utf8(absolute(candidate, ctx.project)));
    probes.push_back({{"role", role}, {"candidates", paths}});
  }
  if (!probes.empty()) req["probes"] = probes;
  req["inputs"] = inputs;
  req["outputs"] = outputs;
  req["scripts"] = scripts;
  req["temps"] = temps;
  req["host_digests"] = hostDigests;
  req["key_paths"] = keyPaths;
  req["session_state"] = ctx.sessionState;
  req["external"] = ctx.external;
  if (!ctx.helperPython.empty()) req["helper_python"] = utf8(ctx.helperPython);
  if (!ctx.metrics.is_null()) {
    // The catalogue always goes with the metrics: without it the engine runs.
    req["metrics"] = ctx.metrics;
    req["metrics_catalogue"] = ctx.metricsCatalogue;
  }
  return req;
}

nlohmann::json EngineBackend::planRequest(const std::vector<UpstreamCommand>& stages,
                                          std::vector<std::string>* unbuilt) const {
  const EngineStageContext ctx = m_context("", "");
  nlohmann::json plan{{"v", PROTOCOL_VERSION},
                      {"op", "plan"},
                      {"project", utf8(ctx.project)},
                      {"layout_resolved_in_pack", ctx.layoutResolvedInPack},
                      {"stages", nlohmann::json::array()}};
  if (!ctx.deviceDir.empty()) plan["device_dir"] = utf8(ctx.deviceDir);
  for (const UpstreamCommand& up : stages) {
    const std::string name = stageName(up.taskId);
    if (!up.command) {
      if (unbuilt) unbuilt->push_back(name);
      continue;
    }
    const std::string profile = profileName(up.taskId, up.profile);
    nlohmann::json item = request("plan", name, profile, *up.command, m_context(name, profile));
    for (const char* key : {"v", "op", "project", "device_dir"}) item.erase(key);
    plan["stages"].push_back(std::move(item));
  }
  return plan;
}

bool EngineBackend::upstreamStale(const std::string& stage,
                                  const EngineStageContext& /*ctx*/) const try {
  if (!m_upstream) return false;
  const std::vector<UpstreamCommand> upstream = m_upstream(stage);
  if (upstream.empty()) return false;
  std::vector<std::string> unbuilt;
  const nlohmann::json plan = planRequest(upstream, &unbuilt);
  if (!unbuilt.empty()) return true;  // an upstream command that cannot be built is stale
  const FlowEngineClient::Result result = m_client.call({"plan"}, &plan);
  if (!result.ok) return true;
  for (const nlohmann::json& decision : result.doc.value("stages", nlohmann::json::array())) {
    const std::string verdict = decision.value("verdict", std::string{"run"});
    if (verdict != "skip" && verdict != "external") return true;
  }
  return false;
} catch (const std::exception& e) {
  warnOnce(std::string("cannot check upstream stages (") + e.what() + "); running the stage");
  return true;
}

void EngineBackend::warnOnce(const std::string& message) const {
  if (m_warned.exchange(true)) return;
  m_compiler->Message("Warning: flow engine: " + message);
}

// REQ-006: anything that goes wrong while deciding means run, never a crash.
bool EngineBackend::isCompilationRequired(int taskId, const std::string& profile,
                                          const CommandWrapperPtr& command) const {
  try {
    return decide(taskId, profile, command);
  } catch (const std::exception& e) {
    const std::string what = "cannot decide " + stageName(taskId) + " (" + e.what() + ")";
    if (inTask()) {
      m_compiler->Message("Warning: flow engine " + what + "; running it");
    } else {
      warnOnce(what + "; stages show as not up to date");
    }
    return true;
  }
}

bool EngineBackend::decide(int taskId, const std::string& profile,
                           const CommandWrapperPtr& command) const {
  const std::string stage = stageName(taskId);
  if (!command || !decides(stage)) return true;
  const std::string engineProfile = profileName(taskId, profile);
  const EngineStageContext ctx = m_context(stage, engineProfile);
  if (!m_client.usable()) {
    warnOnce("no bundled interpreter or engine in this install; every stage runs");
    return true;
  }

  const bool inTask = this->inTask();
  nlohmann::json req = request("check", stage, engineProfile, *command, ctx);
  if (inTask) {
    req["begin"] = true;  // a stage about to run loses its record first
    req["upstream_stale"] = upstreamStale(stage, ctx);
  }
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
  const bool runs = verdict != "skip" && verdict != "external";
  if (inTask && runs) {
    m_compiler->Message("Flow engine: " + stage + " runs: " +
                        join(result.doc.value("reasons", nlohmann::json::array())));
  }
  return runs;
}

void EngineBackend::storeTaskCommand(int taskId, const std::string& profile,
                                     const CommandWrapperPtr& command) {
  const std::string stage = stageName(taskId);
  if (!command || !decides(stage) || !m_client.usable()) return;
  Pending pending{taskId, {}};
  try {
    const std::string engineProfile = profileName(taskId, profile);
    pending.request =
        request("record", stage, engineProfile, *command, m_context(stage, engineProfile));
  } catch (const std::exception& e) {
    m_compiler->Message("Warning: flow engine cannot record " + stage + " (" + e.what() +
                        "); it runs next time");
    return;
  }
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_pending.find(std::this_thread::get_id());
    if (it != m_pending.end()) {
      it->second.back().push_back(std::move(pending));
      return;
    }
  }
  record(pending);
}

void EngineBackend::record(const Pending& pending) {
  const std::string stage = stageName(pending.taskId);
  try {
    const FlowEngineClient::Result result = m_client.call({"record"}, &pending.request);
    if (!result.ok) {
      m_compiler->Message("Warning: flow engine cannot record " + stage + " (" + result.error +
                          "); it runs next time");
      return;
    }
    // REQ-125: the C++ cache's entries for this task and every later one are
    // stale; the C++ cache folds floorplanning into pack.
    const int from =
        pending.taskId == FLOORPLAN_TASK ? static_cast<int>(Action::Pack) : pending.taskId;
    CppBackend::forgetTasks(m_projectPath, from, static_cast<int>(Action::Bitstream));
  } catch (const std::exception& e) {
    m_compiler->Message("Warning: flow engine cannot record " + stage + " (" + e.what() +
                        "); it runs next time");
  }
}

void EngineBackend::forget(const std::string& stage, const std::string& profile) {
  if (!hasRecords(m_projectPath)) return;
  std::vector<std::string> args{"invalidate", "--stage", stage};
  if (!profile.empty()) args.insert(args.end(), {"--profile", profile});
  args.insert(args.end(), {"--project", utf8(m_projectPath)});
  FlowEngineClient::Result result;
  try {
    result = m_client.call(args);
  } catch (const std::exception& e) {
    result.error = e.what();
  }
  if (!result.ok) {
    m_compiler->Message("Warning: flow engine cannot clear the records of " + stage + ": " +
                        result.error);
  }
}

void EngineBackend::taskStarted() {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_pending[std::this_thread::get_id()].emplace_back();
}

void EngineBackend::taskFinished(bool ok) {
  std::vector<Pending> records;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_pending.find(std::this_thread::get_id());
    if (it == m_pending.end()) return;
    records = std::move(it->second.back());
    it->second.pop_back();
    if (it->second.empty()) m_pending.erase(it);
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
  return m_pending.count(std::this_thread::get_id()) != 0;
}

bool EngineBackend::isEmpty() const { return !hasRecords(m_projectPath); }

void EngineBackend::clear() {
  if (!hasRecords(m_projectPath)) return;
  FlowEngineClient::Result result;
  try {
    result = m_client.call({"invalidate", "--all", "--project", utf8(m_projectPath)});
  } catch (const std::exception& e) {
    result.error = e.what();
  }
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
