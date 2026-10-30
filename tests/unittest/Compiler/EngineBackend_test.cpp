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

#include "Compiler/EngineBackend.h"

#include <QStandardPaths>

#include "Compiler/Compiler.h"
#include "Utils/FileUtils.h"
#include "gtest/gtest.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace FOEDAG;
namespace fs = std::filesystem;

namespace {

constexpr int SYNTH = static_cast<int>(Compiler::Action::Synthesis);
constexpr int PACK = static_cast<int>(Compiler::Action::Pack);
constexpr int PLACE = static_cast<int>(Compiler::Action::Detailed);
constexpr int ROUTE = static_cast<int>(Compiler::Action::Routing);
constexpr int STA = static_cast<int>(Compiler::Action::STA);

class SilentCompiler : public Compiler {
public:
  void Message(const std::string& message) const override { messages.push_back(message); }
  void ErrorMessage(const std::string& message, bool = true) const override {
    messages.push_back(message);
  }
  mutable std::vector<std::string> messages;
};

// Answers like aurora_engine: the verdict comes from mode.txt, every call is
// appended to calls.jsonl.
const char* FAKE_ENGINE = R"(import json, pathlib, sys
here = pathlib.Path(__file__).parent
args = sys.argv[1:]
raw = sys.stdin.read()
req = json.loads(raw) if raw else None
with open(here / 'calls.jsonl', 'a') as f:
    f.write(json.dumps({'args': args, 'request': req}) + '\n')
mode_file = here / 'mode.txt'
mode = mode_file.read_text().strip() if mode_file.exists() else 'skip'
if mode == 'list':
    print('[1]')
    sys.exit(0)
if mode == 'error':
    print(json.dumps({'v': 1, 'error': {'code': 'io', 'message': 'boom'}}))
    sys.exit(4)
op = args[0]
if op == 'check':
    print(json.dumps({'v': 1, 'stage': req['stage'], 'profile': req.get('profile') or '',
                      'verdict': mode, 'reasons': ['input changed: a.v'], 'key': None}))
elif op == 'plan':
    plan_file = here / 'plan.txt'
    plan = plan_file.read_text().strip() if plan_file.exists() else 'skip'
    print(json.dumps({'v': 1, 'stages': [
        {'v': 1, 'stage': r['stage'], 'profile': r.get('profile') or '', 'verdict': plan,
         'reasons': [], 'key': None} for r in req['stages']]}))
elif op == 'record':
    print(json.dumps({'v': 1, 'stage': req['stage'], 'profile': '', 'recorded': True, 'key': 'k'}))
else:
    print(json.dumps({'v': 1, 'invalidated': []}))
)";

class EngineBackendTest : public ::testing::Test {
protected:
  void SetUp() override {
    const std::string name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
    m_root = fs::temp_directory_path() / ("engine_backend_test_" + name);
    fs::remove_all(m_root);
    m_project = m_root / "proj";
    m_engineDir = m_root / "engine";
    fs::create_directories(m_project);
    fs::create_directories(m_engineDir);
    m_ctx.project = m_project;
    m_ctx.projectName = "counter";
    m_ctx.top = "top";
    m_ctx.sessionState = {{"device", "dev"}};
  }
  void TearDown() override { fs::remove_all(m_root); }

  fs::path write(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    FileUtils::WriteToFile(path, content);
    return path;
  }

  FlowEngineClient fakeClient() {
    const std::string python = QStandardPaths::findExecutable("python3").toStdString();
    if (python.empty()) return {};
    write(m_engineDir / "aurora_engine.py", FAKE_ENGINE);
    return FlowEngineClient(python, m_engineDir / "aurora_engine.py", {}, 20000);
  }

  std::unique_ptr<EngineBackend> backend(FlowEngineClient client) {
    auto b = std::make_unique<EngineBackend>(&m_compiler, std::move(client),
                                             [this](const std::string&, const std::string&) { return m_ctx; });
    b->setProjectPath(m_project);
    return b;
  }

  void setVerdict(const std::string& mode) { write(m_engineDir / "mode.txt", mode); }

  std::vector<nlohmann::json> calls() {
    std::vector<nlohmann::json> result;
    std::ifstream in(m_engineDir / "calls.jsonl");
    for (std::string line; std::getline(in, line);) result.push_back(nlohmann::json::parse(line));
    return result;
  }

  CommandWrapperPtr vprCommand() {
    auto command = std::make_shared<CommandWrapper>();
    command->append("--device", "castor");
    command->appendPath("--net_file", fs::path{"counter_post_synth.net"});
    command->append("--route_chan_width 160 --pack");
    return command;
  }

  SilentCompiler m_compiler;
  EngineStageContext m_ctx;
  fs::path m_root;
  fs::path m_project;
  fs::path m_engineDir;
};

TEST_F(EngineBackendTest, stage_names_and_profiles) {
  EXPECT_EQ(EngineBackend::stageName(SYNTH), "synth");
  EXPECT_EQ(EngineBackend::stageName(PACK), "pack");
  EXPECT_EQ(EngineBackend::stageName(PLACE), "place");
  EXPECT_EQ(EngineBackend::stageName(ROUTE), "route");
  EXPECT_EQ(EngineBackend::stageName(STA), "sta");
  EXPECT_EQ(EngineBackend::stageName(static_cast<int>(Compiler::Action::Batch)), "");
  EXPECT_EQ(EngineBackend::stageName(EngineBackend::FLOORPLAN_TASK), "floorplan");
  EXPECT_TRUE(EngineBackend::decides("route"));
  EXPECT_TRUE(EngineBackend::decides("floorplan"));
  EXPECT_FALSE(EngineBackend::decides("sta"));
  EXPECT_EQ(EngineBackend::profileName(SYNTH, "0"), "");
  EXPECT_EQ(EngineBackend::profileName(SYNTH, "1"), "synplify");
  EXPECT_EQ(EngineBackend::profileName(STA, "fast"), "fast");
}

// aurora2 tests/flow_engine/fault_injection.py looks for this exact line.
TEST_F(EngineBackendTest, the_fault_hook_names_one_stage) {
  qunsetenv(EngineBackend::FAULT_ENV);
  EXPECT_FALSE(EngineBackend::faultRequested("route"));
  qputenv(EngineBackend::FAULT_ENV, "route");
  EXPECT_TRUE(EngineBackend::faultRequested("route"));
  EXPECT_FALSE(EngineBackend::faultRequested("place"));
  EXPECT_FALSE(EngineBackend::faultRequested(""));
  qunsetenv(EngineBackend::FAULT_ENV);
  EXPECT_EQ(EngineBackend::faultMessage("route"),
            "AURORA_FAULT_AFTER_TOOL: failing route after its tool succeeded");
}

TEST_F(EngineBackendTest, owned_outputs_follow_the_ownership_table) {
  EXPECT_EQ(EngineBackend::ownedOutputs("route", "", m_ctx),
            std::vector<std::string>{"counter_post_synth.route"});
  EXPECT_EQ(EngineBackend::ownedOutputs("synth", "synplify", m_ctx),
            std::vector<std::string>{"top/top.vm"});
  EXPECT_EQ(EngineBackend::ownedOutputs("pack", "", m_ctx).size(), 1u);
  m_ctx.layoutResolvedInPack = true;
  const auto pack = EngineBackend::ownedOutputs("pack", "", m_ctx);
  const auto owns = [&pack](const std::string& file) {
    return std::find(pack.begin(), pack.end(), file) != pack.end();
  };
  EXPECT_TRUE(owns("pack_layout.json"));
  EXPECT_TRUE(owns("vpr_generated.xml.en"));
  EXPECT_TRUE(owns("sb_maps_generated.yml"));
  EXPECT_TRUE(owns("sb_maps_generated.yml.en"));
  EXPECT_FALSE(owns("vpr_generated.xml"));  // deleted once encrypted
  EXPECT_EQ(EngineBackend::ownedOutputs("floorplan", "", m_ctx),
            (std::vector<std::string>{"counter_repack_design_constraint.xml",
                                      "counter_constraints.xml", "counter_rpm_constraints.xml"}));
  EXPECT_EQ(EngineBackend::ownedOutputs("bitstream", "", m_ctx),
            std::vector<std::string>{"fabric_bitstream.bit"});
  EXPECT_EQ(EngineBackend::ownedOutputs("bitstream", "encode", m_ctx).size(), 3u);
  EXPECT_TRUE(EngineBackend::ownedOutputs("sta", "fast", m_ctx).empty());
  EXPECT_TRUE(EngineBackend::ownedOutputs("power", "", m_ctx).empty());
}

// REQ-122: metrics always travel with their catalogue, null meaning the
// installed default; a stage without metrics sends neither.
TEST_F(EngineBackendTest, metrics_go_with_their_catalogue) {
  const auto b = backend({});
  nlohmann::json req = b->request("check", "power", "", *vprCommand(), m_ctx);
  EXPECT_FALSE(req.contains("metrics"));
  EXPECT_FALSE(req.contains("metrics_catalogue"));

  m_ctx.metrics = {{"routing.num_input", "3"}};
  req = b->request("check", "power", "", *vprCommand(), m_ctx);
  EXPECT_EQ(req["metrics"], m_ctx.metrics);
  ASSERT_TRUE(req.contains("metrics_catalogue"));
  EXPECT_TRUE(req["metrics_catalogue"].is_null());

  m_ctx.metricsCatalogue = (m_root / "my_metrics.json").string();
  req = b->request("check", "power", "", *vprCommand(), m_ctx);
  EXPECT_EQ(req["metrics_catalogue"], (m_root / "my_metrics.json").string());
}

// REQ-121: an encrypted owned output is known by its plaintext digest, but only
// while it exists; outputs outside the project are keyed by their mask.
TEST_F(EngineBackendTest, output_host_digests_and_private_outputs) {
  m_ctx.layoutResolvedInPack = true;
  const std::string plain = "sha256:" + std::string(64, 'b');
  m_ctx.outputHostDigests["vpr_generated.xml.en"] = plain;
  m_ctx.outputHostDigests["sb_maps_generated.yml.en"] = plain;
  write(m_project / "vpr_generated.xml.en", "cipher");
  const fs::path privateOut = m_root / "private" / "counter_repack.xml";
  auto command = vprCommand();
  command->declareOutput(privateOut, "repack_constraints_private");
  command->declarePath(m_root / "data.csv");

  const nlohmann::json req = backend({})->request("check", "pack", "", *command, m_ctx);
  EXPECT_EQ(req["host_digests"][(m_project / "vpr_generated.xml.en").string()], plain);
  EXPECT_FALSE(req["host_digests"].contains((m_project / "sb_maps_generated.yml.en").string()));
  const auto& outputs = req["outputs"];
  EXPECT_NE(std::find(outputs.begin(), outputs.end(), privateOut.string()), outputs.end());
  EXPECT_EQ(req["temps"][privateOut.string()], "repack_constraints_private");
  const auto& inputs = req["inputs"];
  EXPECT_NE(std::find(inputs.begin(), inputs.end(), (m_root / "data.csv").string()), inputs.end());
  EXPECT_EQ(std::find(inputs.begin(), inputs.end(), privateOut.string()), inputs.end());
  // declarePath adds no argv token.
  EXPECT_EQ(req["argv"], nlohmann::json(vprCommand()->argv()));
}

// The describe form: ordered argv, every declared path whether or not it
// exists, owned outputs never inputs, directories as their files.
TEST_F(EngineBackendTest, request_describes_the_command) {
  const fs::path templates = m_root / "sb_templates";
  write(templates / "b.csv", "b");
  write(templates / "a.csv", "a");
  const fs::path arch = m_root / "tmp" / "vpr_arch_file_mask";
  auto command = vprCommand();
  command->appendPath("--sb_templates", templates);
  command->appendPath("--sdc_file", fs::path{"missing.sdc"});
  command->prependFile(arch, VPR_ARCH_FILE_MASK);
  command->prepend("vpr");
  command->setHostDigest(arch, "sha256:" + std::string(64, 'a'));
  command->addKeyPath(m_root / "dev_Supp.db");
  m_ctx.external = true;

  const auto b = backend({});
  const nlohmann::json req = b->request("check", "pack", "", *command, m_ctx);
  EXPECT_EQ(req["v"], 1);
  EXPECT_EQ(req["op"], "check");
  EXPECT_EQ(req["stage"], "pack");
  EXPECT_EQ(req["argv"],
            nlohmann::json({"vpr", arch.string(), "--device", "castor", "--net_file",
                            "counter_post_synth.net", "--route_chan_width", "160", "--pack",
                            "--sb_templates", templates.string(), "--sdc_file", "missing.sdc"}));
  const fs::path net = m_project / "counter_post_synth.net";
  EXPECT_EQ(req["outputs"], nlohmann::json({net.string()}));
  EXPECT_EQ(req["inputs"], nlohmann::json({(m_project / "missing.sdc").string(),
                                           (templates / "a.csv").string(),
                                           (templates / "b.csv").string(), arch.string()}));
  EXPECT_EQ(req["temps"], nlohmann::json({{arch.string(), VPR_ARCH_FILE_MASK}}));
  EXPECT_EQ(req["host_digests"][arch.string()], "sha256:" + std::string(64, 'a'));
  EXPECT_EQ(req["key_paths"], nlohmann::json({(m_root / "dev_Supp.db").string()}));
  EXPECT_EQ(req["session_state"], m_ctx.sessionState);
  EXPECT_EQ(req["external"], true);
  EXPECT_EQ(req["scripts"], nlohmann::json::array());
}

// The script's FILES_INFO hashes cover the task's own outputs, so the engine
// gets the script without them and the files as inputs.
TEST_F(EngineBackendTest, request_carries_the_script_without_file_hashes) {
  const fs::path design = write(m_project / "counter.v", "module counter; endmodule");
  auto script = std::make_shared<ScriptRenderer>("read_verilog ${FILES}\nwrite_blif ${OUT}\n");
  script->apply("${FILES}", design.string());
  script->addFile(design);
  script->applyFile("${OUT}", fs::path{"counter_post_synth.blif"});
  auto command = std::make_shared<CommandWrapper>("yosys");
  command->setScriptRenderer(script);
  command->setScriptName("counter.ys");

  const nlohmann::json req = backend({})->request("record", "synth", "", *command, m_ctx);
  ASSERT_EQ(req["scripts"].size(), 1u);
  EXPECT_EQ(req["scripts"][0]["name"], "counter.ys");
  EXPECT_EQ(req["scripts"][0]["text"],
            "read_verilog " + design.string() + "\nwrite_blif counter_post_synth.blif\n");
  EXPECT_EQ(req["inputs"], nlohmann::json({design.string()}));
}

TEST_F(EngineBackendTest, verdicts_map_to_run_or_skip) {
  FlowEngineClient client = fakeClient();
  if (!client.usable()) GTEST_SKIP() << "no python3";
  const auto b = backend(client);
  auto command = vprCommand();

  setVerdict("skip");
  EXPECT_FALSE(b->isCompilationRequired(PACK, command));
  setVerdict("external");
  EXPECT_FALSE(b->isCompilationRequired(PACK, command));
  setVerdict("run");
  EXPECT_TRUE(b->isCompilationRequired(PACK, command));
  setVerdict("error");
  EXPECT_TRUE(b->isCompilationRequired(PACK, command));
  EXPECT_TRUE(b->isCompilationRequired(PACK, nullptr));

  const auto seen = calls();
  ASSERT_EQ(seen.size(), 4u);
  EXPECT_EQ(seen[0]["args"], nlohmann::json({"check", "--json"}));
  EXPECT_FALSE(seen[0]["request"].contains("begin"));  // a status query
}

TEST_F(EngineBackendTest, stages_not_yet_decided_always_run_and_ask_nothing) {
  FlowEngineClient client = fakeClient();
  if (!client.usable()) GTEST_SKIP() << "no python3";
  const auto b = backend(client);
  setVerdict("skip");
  EXPECT_TRUE(b->isCompilationRequired(STA, "fast", vprCommand()));
  b->storeTaskCommand(STA, "fast", vprCommand());
  // Pack on an AUTO/CUSTOM device restores no layout on a skip yet (row 1.10).
  m_ctx.layoutResolvedInPack = true;
  EXPECT_TRUE(b->isCompilationRequired(PACK, vprCommand()));
  EXPECT_TRUE(calls().empty());
}

TEST_F(EngineBackendTest, missing_interpreter_runs_every_stage_with_one_warning) {
  const auto b = backend({});
  EXPECT_TRUE(b->isCompilationRequired(PACK, vprCommand()));
  EXPECT_TRUE(b->isCompilationRequired(ROUTE, vprCommand()));
  b->storeTaskCommand(PACK, vprCommand());
  EXPECT_EQ(m_compiler.messages.size(), 1u);
}

// Inside a task the check removes the record first, and the record waits until
// RunCompileTask's guard has run; a failed task records nothing (row 1.9).
TEST_F(EngineBackendTest, records_wait_for_the_task_to_succeed) {
  FlowEngineClient client = fakeClient();
  if (!client.usable()) GTEST_SKIP() << "no python3";
  const auto b = backend(client);
  setVerdict("run");

  b->taskStarted();
  EXPECT_TRUE(b->isCompilationRequired(PACK, vprCommand()));
  b->storeTaskCommand(PACK, vprCommand());
  ASSERT_EQ(calls().size(), 1u);
  EXPECT_EQ(calls()[0]["request"]["begin"], true);
  b->taskFinished(false);
  EXPECT_EQ(calls().size(), 1u);

  b->taskStarted();
  b->storeTaskCommand(PACK, vprCommand());
  EXPECT_EQ(calls().size(), 1u);
  b->taskFinished(true);
  const auto seen = calls();
  ASSERT_EQ(seen.size(), 2u);
  EXPECT_EQ(seen[1]["args"], nlohmann::json({"record", "--json"}));
  EXPECT_EQ(seen[1]["request"]["op"], "record");
  EXPECT_EQ(seen[1]["request"]["stage"], "pack");
}

// REQ-125: an engine record makes the C++ cache's entries for that task and
// every later one stale.
TEST_F(EngineBackendTest, record_clears_later_cpp_cache_entries) {
  FlowEngineClient client = fakeClient();
  if (!client.usable()) GTEST_SKIP() << "no python3";
  const fs::path cache = m_project / COMPILATION_CACHE_FILENAME;
  nlohmann::json entries{{std::to_string(SYNTH) + "_0", 1}, {std::to_string(PACK), 1},
                         {std::to_string(ROUTE), 1}, {std::to_string(STA) + "_fast", 1}};
  write(cache, entries.dump());

  backend(client)->storeTaskCommand(PACK, vprCommand());
  const nlohmann::json left = nlohmann::json::parse(FileUtils::GetFileContent(cache));
  EXPECT_EQ(left, nlohmann::json({{std::to_string(SYNTH) + "_0", 1}}));
}

// The C++ cache has no floorplan entry; an engine floorplan record makes pack's
// and every later entry stale.
TEST_F(EngineBackendTest, a_floorplan_record_clears_cpp_entries_from_pack_on) {
  FlowEngineClient client = fakeClient();
  if (!client.usable()) GTEST_SKIP() << "no python3";
  const fs::path cache = m_project / COMPILATION_CACHE_FILENAME;
  nlohmann::json entries{{std::to_string(SYNTH) + "_0", 1}, {std::to_string(PACK), 1},
                         {std::to_string(ROUTE), 1}};
  write(cache, entries.dump());

  backend(client)->storeTaskCommand(EngineBackend::FLOORPLAN_TASK, vprCommand());
  EXPECT_EQ(calls().at(0)["request"]["stage"], "floorplan");
  const nlohmann::json left = nlohmann::json::parse(FileUtils::GetFileContent(cache));
  EXPECT_EQ(left, nlohmann::json({{std::to_string(SYNTH) + "_0", 1}}));
}

TEST_F(EngineBackendTest, records_on_disk_decide_empty_and_clear) {
  FlowEngineClient client = fakeClient();
  if (!client.usable()) GTEST_SKIP() << "no python3";
  const auto b = backend(client);
  write(m_project / ".aurora" / "state" / "digests.json", "{}");
  EXPECT_TRUE(b->isEmpty());
  b->clear();
  EXPECT_TRUE(calls().empty());  // nothing to clear, nothing started

  write(m_project / ".aurora" / "state" / "pack.json", "{}");
  EXPECT_FALSE(b->isEmpty());
  b->clear();
  const auto seen = calls();
  ASSERT_EQ(seen.size(), 1u);
  EXPECT_EQ(seen[0]["args"],
            nlohmann::json({"invalidate", "--all", "--project", m_project.string(), "--json"}));
}

// The engine refuses to read key material (REQ-012): VPR's --crypt_key_db and
// any *_Supp.db input reach it as host digests.
TEST_F(EngineBackendTest, key_material_inputs_carry_host_digests) {
  const fs::path keyDb = write(m_root / "dev" / "DEV_Supp.db", "keys");
  const fs::path other = write(m_root / "dev" / "other_SUPP.DB", "keys too");
  auto command = vprCommand();
  command->appendPath("--crypt_key_db", keyDb);
  command->appendPath("--extra", other);

  const nlohmann::json req = backend({})->request("check", "pack", "", *command, m_ctx);
  EXPECT_EQ(req["host_digests"][keyDb.string()], FlowEngineClient::sha256File(keyDb));
  EXPECT_EQ(req["host_digests"][other.string()], FlowEngineClient::sha256File(other));
  EXPECT_FALSE(req["host_digests"].contains((m_project / "counter_post_synth.net").string()));
}

// The encoder's ASCON key files are key material too, in either option form.
TEST_F(EngineBackendTest, ascon_key_files_carry_host_digests) {
  const fs::path key = write(m_root / "keys" / "key.txt", "k");
  const fs::path nonce = write(m_root / "keys" / "nonce.txt", "n");
  const fs::path ad = write(m_project / "ad.txt", "a");
  auto command = std::make_shared<CommandWrapper>("python3");
  command->append("--ascon_key", key.string());
  command->declarePath(key);
  command->append("--ascon_nonce=" + nonce.string());
  command->declarePath(nonce);
  command->append("--ascon_ad", "ad.txt");
  command->declarePath(fs::path{"ad.txt"});

  const nlohmann::json req =
      backend({})->request("check", "bitstream", "encode", *command, m_ctx);
  EXPECT_EQ(req["host_digests"][key.string()], FlowEngineClient::sha256File(key));
  EXPECT_EQ(req["host_digests"][nonce.string()], FlowEngineClient::sha256File(nonce));
  EXPECT_EQ(req["host_digests"][ad.string()], FlowEngineClient::sha256File(ad));
}

// A status query on another thread while a task runs neither removes records
// nor joins the task's pending records.
TEST_F(EngineBackendTest, other_threads_are_not_in_the_task) {
  FlowEngineClient client = fakeClient();
  if (!client.usable()) GTEST_SKIP() << "no python3";
  const auto b = backend(client);
  setVerdict("run");

  b->taskStarted();
  EXPECT_TRUE(b->taskRunning());
  std::thread gui([&b, this] {
    EXPECT_TRUE(b->isCompilationRequired(ROUTE, vprCommand()));
    b->storeTaskCommand(ROUTE, vprCommand());  // not part of the task: records now
  });
  gui.join();
  auto seen = calls();
  ASSERT_EQ(seen.size(), 2u);
  EXPECT_FALSE(seen[0]["request"].contains("begin"));
  EXPECT_EQ(seen[1]["args"][0], "record");

  b->taskFinished(true);
  EXPECT_FALSE(b->taskRunning());
  EXPECT_EQ(calls().size(), 2u);
}

// REQ-018: a stage requested on its own runs when an upstream stage is stale;
// the host asks with one plan request, and only inside a task.
TEST_F(EngineBackendTest, a_task_check_reports_stale_upstream_stages) {
  FlowEngineClient client = fakeClient();
  if (!client.usable()) GTEST_SKIP() << "no python3";
  const auto b = backend(client);
  b->setUpstreamProvider([this](const std::string& stage) {
    EXPECT_EQ(stage, "place");
    return std::vector<UpstreamCommand>{{SYNTH, "0", vprCommand()}, {PACK, "", vprCommand()}};
  });
  setVerdict("skip");

  EXPECT_FALSE(b->isCompilationRequired(PLACE, vprCommand()));  // status query: no plan
  ASSERT_EQ(calls().size(), 1u);

  b->taskStarted();
  write(m_engineDir / "plan.txt", "skip");
  b->isCompilationRequired(PLACE, vprCommand());
  write(m_engineDir / "plan.txt", "run");
  b->isCompilationRequired(PLACE, vprCommand());
  b->taskFinished(false);

  const auto seen = calls();
  ASSERT_EQ(seen.size(), 5u);
  EXPECT_EQ(seen[1]["args"], nlohmann::json({"plan", "--json"}));
  const nlohmann::json& plan = seen[1]["request"];
  EXPECT_EQ(plan["op"], "plan");
  ASSERT_EQ(plan["stages"].size(), 2u);
  EXPECT_EQ(plan["stages"][0]["stage"], "synth");
  EXPECT_EQ(plan["stages"][0]["profile"], "");
  EXPECT_FALSE(plan["stages"][0].contains("project"));
  EXPECT_EQ(seen[2]["request"]["upstream_stale"], false);
  EXPECT_EQ(seen[4]["request"]["upstream_stale"], true);
}

// A stage that runs because an upstream stage is stale says so, like any run.
TEST_F(EngineBackendTest, a_pending_upstream_stage_reports_that_it_runs) {
  FlowEngineClient client = fakeClient();
  if (!client.usable()) GTEST_SKIP() << "no python3";
  const auto b = backend(client);
  setVerdict("pending-upstream");
  b->taskStarted();
  EXPECT_TRUE(b->isCompilationRequired(PLACE, vprCommand()));
  b->taskFinished(false);
  ASSERT_EQ(m_compiler.messages.size(), 1u);
  EXPECT_EQ(m_compiler.messages[0].rfind("Flow engine: place runs: ", 0), 0u);
}

// REQ-006: a request that cannot be serialised or an answer that is not an
// object means run (and no record), never an exception out of the backend.
TEST_F(EngineBackendTest, failures_inside_the_backend_mean_run) {
  FlowEngineClient client = fakeClient();
  if (!client.usable()) GTEST_SKIP() << "no python3";
  const auto b = backend(client);
  auto notUtf8 = vprCommand();
  notUtf8->append("--title", "caf\xe9");

  setVerdict("skip");
  EXPECT_TRUE(b->isCompilationRequired(PACK, notUtf8));
  b->storeTaskCommand(PACK, notUtf8);
  EXPECT_TRUE(calls().empty());  // nothing reached the engine

  setVerdict("list");
  EXPECT_TRUE(b->isCompilationRequired(PACK, vprCommand()));
}

// An upstream provider that throws makes the stage run (upstream counted stale),
// rather than skipping it or ending the task.
TEST_F(EngineBackendTest, an_upstream_failure_counts_as_stale) {
  FlowEngineClient client = fakeClient();
  if (!client.usable()) GTEST_SKIP() << "no python3";
  const auto b = backend(client);
  b->setUpstreamProvider([](const std::string&) -> std::vector<UpstreamCommand> {
    throw std::runtime_error("builder failed");
  });
  setVerdict("skip");
  b->taskStarted();
  b->isCompilationRequired(PLACE, vprCommand());
  b->taskFinished(false);
  const auto seen = calls();
  ASSERT_EQ(seen.size(), 1u);
  EXPECT_EQ(seen[0]["request"]["upstream_stale"], true);
}

// Nested tasks on one thread: the inner task's records go with the inner task.
TEST_F(EngineBackendTest, nested_tasks_on_one_thread_keep_their_own_records) {
  FlowEngineClient client = fakeClient();
  if (!client.usable()) GTEST_SKIP() << "no python3";
  const auto b = backend(client);
  b->taskStarted();
  b->storeTaskCommand(PACK, vprCommand());
  b->taskStarted();
  b->storeTaskCommand(ROUTE, vprCommand());
  b->taskFinished(false);
  EXPECT_TRUE(calls().empty());
  b->taskFinished(true);
  const auto seen = calls();
  ASSERT_EQ(seen.size(), 1u);
  EXPECT_EQ(seen[0]["request"]["stage"], "pack");
}

// A Tcl batch runs its tasks on further threads: each task's records wait for
// that task, on its own thread, and a failed inner task records nothing.
TEST_F(EngineBackendTest, a_nested_task_on_another_thread_has_its_own_records) {
  FlowEngineClient client = fakeClient();
  if (!client.usable()) GTEST_SKIP() << "no python3";
  const auto b = backend(client);
  setVerdict("run");

  b->taskStarted();  // the batch
  auto inner = [&b, this](bool ok) {
    b->taskStarted();
    EXPECT_TRUE(b->isCompilationRequired(PACK, vprCommand()));
    b->storeTaskCommand(PACK, vprCommand());
    b->taskFinished(ok);
  };
  std::thread failed(inner, false);
  failed.join();
  std::thread succeeded(inner, true);
  succeeded.join();
  EXPECT_TRUE(b->taskRunning());
  b->taskFinished(true);
  EXPECT_FALSE(b->taskRunning());

  const auto seen = calls();
  ASSERT_EQ(seen.size(), 3u);
  EXPECT_EQ(seen[0]["request"]["begin"], true);  // the inner task is in a task
  EXPECT_EQ(seen[1]["request"]["begin"], true);
  EXPECT_EQ(seen[2]["args"][0], "record");  // only the task that succeeded
}

}  // namespace
