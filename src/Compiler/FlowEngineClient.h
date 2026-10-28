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

#include "nlohmann_json/json.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace FOEDAG {

// Runs one-shot aurora_engine commands with Aurora's bundled interpreter
// (aurora2 docs/specs/aurora-flow-engine, design §1.4, row 1.6; REQ-140, REQ-143).
class FlowEngineClient {
public:
  enum class Mode { Cpp, Engine };
  struct ModeResult {
    Mode mode = Mode::Cpp;
    std::string warning;  // non-empty when an unusable value fell back to cpp
  };
  struct Result {
    bool ok = false;
    nlohmann::json doc;
    std::string error;
  };

  static constexpr int DEFAULT_TIMEOUT_MS = 120000;

  // AURORA_INCREMENTAL if set (envValue, nullptr when unset), else defaults.json
  // for this platform; anything unusable keeps cpp. Reads a file, starts nothing.
  static ModeResult incrementalMode(const char* envValue,
                                    const std::filesystem::path& defaultsFile,
                                    const std::string& platform = currentPlatform());
  static std::string currentPlatform();

  // The install holding scripts/aurora_engine.py, probed from FOEDAG's data path
  // like the other installed scripts; empty when none.
  static std::filesystem::path findInstallRoot(const std::filesystem::path& dataPath);
  static std::filesystem::path defaultsFile(const std::filesystem::path& installRoot);
  // envs/python3.8/bin/python3.8 (not the bin/python3 wrapper) or envs/python.exe;
  // empty when missing. There is no system-Python fallback.
  static std::filesystem::path bundledInterpreter(const std::filesystem::path& installRoot);
  // "sha256:<hex>" of a file's bytes, the form of a host digest; empty if unreadable.
  static std::string sha256File(const std::filesystem::path& path);

  FlowEngineClient() = default;
  explicit FlowEngineClient(const std::filesystem::path& installRoot,
                            int timeoutMs = DEFAULT_TIMEOUT_MS);
  // Test seam: an explicit interpreter and launcher script.
  FlowEngineClient(const std::filesystem::path& interpreter,
                   const std::filesystem::path& script,
                   const std::filesystem::path& osLibs, int timeoutMs);

  bool usable() const { return !m_interpreter.empty() && !m_script.empty(); }
  const std::filesystem::path& interpreter() const { return m_interpreter; }

  // `<python> -I -X utf8 aurora_engine.py <args...> --json`, request on stdin.
  // Any failure (no interpreter, timeout, non-zero exit, bad JSON, an error
  // document) is reported, never thrown: the caller then runs the stage.
  Result call(const std::vector<std::string>& args,
              const nlohmann::json* request = nullptr) const;

private:
  std::filesystem::path m_interpreter;
  std::filesystem::path m_script;
  std::filesystem::path m_osLibs;  // prepended to the engine's LD_LIBRARY_PATH (Linux)
  int m_timeoutMs = DEFAULT_TIMEOUT_MS;
};

}  // namespace FOEDAG
