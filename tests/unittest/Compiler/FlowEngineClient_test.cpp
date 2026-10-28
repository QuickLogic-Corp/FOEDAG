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

#include "Compiler/FlowEngineClient.h"

#include <QStandardPaths>

#include "Utils/FileUtils.h"
#include "gtest/gtest.h"

#include <filesystem>
#include <string>

using namespace FOEDAG;
namespace fs = std::filesystem;

namespace {

class FlowEngineClientTest : public ::testing::Test {
protected:
  void SetUp() override {
    m_dir = fs::temp_directory_path() /
            ("fec_test_" +
             std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
    fs::remove_all(m_dir);
    fs::create_directories(m_dir);
  }
  void TearDown() override { fs::remove_all(m_dir); }

  fs::path write(const fs::path& relative, const std::string& content) {
    const fs::path path = m_dir / relative;
    fs::create_directories(path.parent_path());
    FileUtils::WriteToFile(path, content, /*newLine*/ false);
    return path;
  }

  // A client running `body` with the system python3 in place of the bundled one.
  FlowEngineClient fake(const std::string& body, int timeoutMs = 20000,
                        const fs::path& osLibs = {}) {
    const fs::path script = write("aurora_engine.py", "import json, os, sys\n" + body);
    return FlowEngineClient(m_python, script, osLibs, timeoutMs);
  }

  bool havePython() {
    m_python = QStandardPaths::findExecutable("python3").toStdString();
    return !m_python.empty();
  }

  fs::path m_dir;
  fs::path m_python;
};

TEST_F(FlowEngineClientTest, switch_overrides_defaults) {
  const fs::path missing = m_dir / "none.json";
  EXPECT_EQ(FlowEngineClient::incrementalMode("engine", missing).mode,
            FlowEngineClient::Mode::Engine);
  const auto cpp = FlowEngineClient::incrementalMode("cpp", missing);
  EXPECT_EQ(cpp.mode, FlowEngineClient::Mode::Cpp);
  EXPECT_TRUE(cpp.warning.empty());
  const auto bad = FlowEngineClient::incrementalMode("Engine", missing);
  EXPECT_EQ(bad.mode, FlowEngineClient::Mode::Cpp);
  EXPECT_NE(bad.warning.find("AURORA_INCREMENTAL"), std::string::npos);
}

TEST_F(FlowEngineClientTest, defaults_pick_the_platform_value) {
  const fs::path file = write(
      "defaults.json",
      R"({"v": 1, "AURORA_INCREMENTAL": {"linux": "engine", "windows": "cpp", "macos": "cpp"}})");
  const auto onLinux = FlowEngineClient::incrementalMode(nullptr, file, "linux");
  EXPECT_EQ(onLinux.mode, FlowEngineClient::Mode::Engine);
  EXPECT_TRUE(onLinux.warning.empty());
  EXPECT_EQ(FlowEngineClient::incrementalMode(nullptr, file, "windows").mode,
            FlowEngineClient::Mode::Cpp);
}

TEST_F(FlowEngineClientTest, unusable_defaults_keep_cpp_with_a_warning) {
  const std::vector<std::string> bodies{
      "not json",
      R"({"v": 2, "AURORA_INCREMENTAL": {"linux": "engine"}})",
      R"({"v": 1, "AURORA_INCREMENTAL": {"windows": "engine"}})",
      R"({"v": 1, "AURORA_INCREMENTAL": {"linux": "fast"}})",
      R"([1])",
  };
  for (const std::string& body : bodies) {
    const fs::path file = write("defaults.json", body);
    const auto mode = FlowEngineClient::incrementalMode(nullptr, file, "linux");
    EXPECT_EQ(mode.mode, FlowEngineClient::Mode::Cpp) << body;
    EXPECT_FALSE(mode.warning.empty()) << body;
  }
  const auto missing = FlowEngineClient::incrementalMode(nullptr, m_dir / "none.json", "linux");
  EXPECT_EQ(missing.mode, FlowEngineClient::Mode::Cpp);
  EXPECT_FALSE(missing.warning.empty());
}

TEST_F(FlowEngineClientTest, install_root_and_interpreter_are_found_or_empty) {
  const fs::path dataPath = m_dir / "share" / "aurora";
  fs::create_directories(dataPath);
  EXPECT_TRUE(FlowEngineClient::findInstallRoot(dataPath).empty());
  EXPECT_TRUE(FlowEngineClient::findInstallRoot({}).empty());

  write("scripts/aurora_engine.py", "");
  EXPECT_TRUE(fs::equivalent(FlowEngineClient::findInstallRoot(dataPath), m_dir));
  // Aurora's data path is <install>/device_data, and the install may sit inside a
  // checkout that has its own scripts/aurora_engine.py.
  const fs::path install = m_dir / "dev";
  write("dev/scripts/aurora_engine.py", "");
  fs::create_directories(install / "device_data");
  EXPECT_TRUE(fs::equivalent(FlowEngineClient::findInstallRoot(install / "device_data"), install));
  EXPECT_EQ(FlowEngineClient::defaultsFile(m_dir),
            m_dir / "scripts" / "flow_engine" / "data" / "defaults.json");

  EXPECT_TRUE(FlowEngineClient::bundledInterpreter(m_dir).empty());
  EXPECT_FALSE(FlowEngineClient(m_dir).usable());
  write("envs/python.exe", "");
  EXPECT_EQ(FlowEngineClient::bundledInterpreter(m_dir), m_dir / "envs" / "python.exe");
  const fs::path posixPython = write("envs/python3.8/bin/python3.8", "");
  EXPECT_EQ(FlowEngineClient::bundledInterpreter(m_dir), posixPython);
  EXPECT_TRUE(FlowEngineClient(m_dir).usable());
}

TEST_F(FlowEngineClientTest, sha256_of_a_file) {
  const fs::path file = write("abc.txt", "abc");
  EXPECT_EQ(FlowEngineClient::sha256File(file),
            "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_TRUE(FlowEngineClient::sha256File(m_dir / "none").empty());
}

TEST_F(FlowEngineClientTest, unusable_client_fails_without_starting_anything) {
  const FlowEngineClient client;
  EXPECT_FALSE(client.usable());
  const auto result = client.call({"version"});
  EXPECT_FALSE(result.ok);
  EXPECT_FALSE(result.error.empty());
}

TEST_F(FlowEngineClientTest, call_sends_request_and_reads_answer) {
  if (!havePython()) GTEST_SKIP() << "no python3";
  const FlowEngineClient client = fake(
      "req = json.loads(sys.stdin.read())\n"
      "print(json.dumps({'argv': sys.argv[1:], 'req': req, 'isolated': sys.flags.isolated,"
      " 'utf8': sys.flags.utf8_mode, 'libs': os.environ.get('LD_LIBRARY_PATH', '')}))\n",
      20000, m_dir / "os_libs");
  const nlohmann::json request{{"stage", "pack"}, {"path", "caf\xc3\xa9"}};
  const auto result = client.call({"check", "--begin"}, &request);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(result.doc["argv"], nlohmann::json({"check", "--begin", "--json"}));
  EXPECT_EQ(result.doc["req"], request);
  EXPECT_EQ(result.doc["isolated"], 1);
  EXPECT_EQ(result.doc["utf8"], 1);
  EXPECT_EQ(result.doc["libs"].get<std::string>().rfind((m_dir / "os_libs").string(), 0), 0u);
}

TEST_F(FlowEngineClientTest, failures_are_reported_not_thrown) {
  if (!havePython()) GTEST_SKIP() << "no python3";
  const auto errorDoc = fake(
      "print(json.dumps({'v': 1, 'error': {'code': 'lock-held', 'message': 'busy'}}))\n"
      "sys.exit(4)\n").call({"check"});
  EXPECT_FALSE(errorDoc.ok);
  EXPECT_EQ(errorDoc.error, "lock-held: busy");

  const auto garbage = fake("print('not json')\n").call({"check"});
  EXPECT_FALSE(garbage.ok);
  EXPECT_NE(garbage.error.find("unreadable"), std::string::npos);

  const auto exitCode = fake("print('{}')\nsys.exit(3)\n").call({"check"});
  EXPECT_FALSE(exitCode.ok);
  EXPECT_NE(exitCode.error.find("exit 3"), std::string::npos);

  const nlohmann::json notUtf8{{"path", "caf\xe9"}};
  const auto unsendable = fake("print('{}')\n").call({"check"}, &notUtf8);
  EXPECT_FALSE(unsendable.ok);
  EXPECT_NE(unsendable.error.find("cannot send"), std::string::npos);

  const auto slow = fake("import time\ntime.sleep(30)\n", 300).call({"check"});
  EXPECT_FALSE(slow.ok);
  EXPECT_NE(slow.error.find("no answer"), std::string::npos);
}

}  // namespace
