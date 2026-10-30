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
#include "Compiler/DumpOptions.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <string_view>

namespace FOEDAG {

namespace {

// Reviewed deny-list (plan Q7). Each writes data VPR built from the decrypted
// architecture, whatever its value.
constexpr std::array<std::string_view, 4> kAlwaysDumps = {
    "--write_rr_graph",
    "--write_router_lookahead",
    "--write_intra_cluster_router_lookahead",
    "--write_placement_delay_lookup",
};

constexpr std::string_view kEchoFile = "--echo_file";
constexpr std::string_view kEchoFileOn = "--echo_file on";

// "vpr", "/opt/bin/vpr", "vpr.exe" and "VPR.EXE" are vpr; "vpr_helper" is not.
// Case-insensitive everywhere: Windows needs it, and elsewhere it only scans more.
bool isProgram(const std::string& token, std::string_view name) {
  std::string stem = std::filesystem::path(token).stem().string();
  std::transform(stem.begin(), stem.end(), stem.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return stem == name;
}

bool hasToken(const std::vector<std::string>& tokens, std::string_view token) {
  return std::find(tokens.begin(), tokens.end(), token) != tokens.end();
}

bool isPlaceOnly(const std::vector<std::string>& tokens) {
  return (hasToken(tokens, "--place") || hasToken(tokens, "--analytical_place")) &&
         !hasToken(tokens, "--pack") && !hasToken(tokens, "--route") &&
         !hasToken(tokens, "--analysis");
}

// OpenFPGA opens a script path as given, so a relative one is relative to
// where openfpga runs.
std::filesystem::path resolve(const std::string& file, const std::filesystem::path& workDir) {
  const std::filesystem::path path(file);
  return path.is_absolute() ? path : workDir / path;
}

// The script of `openfpga ... -f <script>`, or empty when there is none.
std::filesystem::path openFpgaScript(const std::vector<std::string>& tokens,
                                     const std::filesystem::path& workDir) {
  std::string script;
  for (size_t i = 1; i < tokens.size(); ++i) {
    const std::string& token = tokens[i];
    if ((token == "-f" || token == "--file") && i + 1 < tokens.size()) {
      script = tokens[i + 1];
    } else if (token.rfind("--file=", 0) == 0) {
      script = token.substr(7);
    }
  }
  if (script.empty()) return {};
  return resolve(script, workDir);
}

std::string rtrimSpaces(std::string text) {
  const size_t end = text.find_last_not_of(' ');
  return end == std::string::npos ? std::string{} : text.substr(0, end + 1);
}

std::string ltrimSpaces(const std::string& text) {
  const size_t start = text.find_first_not_of(' ');
  return start == std::string::npos ? std::string{} : text.substr(start);
}

// Shell<T>::execute_command: split on ' ', except that text between a pair of
// double quotes is one token, without its quotes.
std::vector<std::string> shellTokens(const std::string& line) {
  std::vector<std::string> tokens;
  const auto splitSpaces = [&tokens](const std::string& text) {
    std::istringstream in(text);
    for (std::string token; std::getline(in, token, ' ');) {
      if (!token.empty()) tokens.push_back(token);
    }
  };
  std::istringstream chunks(line);
  bool quoted = false;
  for (std::string chunk; std::getline(chunks, chunk, '"'); quoted = !quoted) {
    if (quoted) {
      if (!chunk.empty()) tokens.push_back(chunk);
    } else {
      splitSpaces(chunk);
    }
  }
  return tokens;
}

// Deeper than this, a chain of distinct sourced files is refused outright.
constexpr int kMaxSourceDepth = 16;

struct ShellScan {
  std::filesystem::path workDir;
  std::set<std::filesystem::path> scripts;  // scanned once: a cycle repeats nothing new
  std::vector<std::string> found;

  void script(const std::filesystem::path& file, int depth) {
    if (depth > kMaxSourceDepth) {
      found.push_back("source nested deeper than " + std::to_string(kMaxSourceDepth));
      return;
    }
    std::error_code ec;
    const std::filesystem::path key = std::filesystem::weakly_canonical(file, ec);
    if (!scripts.insert(ec ? file : key).second) return;
    // A script that cannot be read cannot be run either, so it dumps nothing.
    std::ifstream in(file);
    std::ostringstream text;
    text << in.rdbuf();
    for (const std::string& line : openFpgaScriptCommands(text.str())) command(line, depth);
  }

  void command(const std::string& line, int depth) {
    const std::vector<std::string> tokens = shellTokens(line);
    if (tokens.empty()) return;
    if (tokens.front() == "vpr") {
      for (std::string& hit : findDumpOptions(tokens)) found.push_back(std::move(hit));
    } else if (tokens.front() == "source") {
      source(tokens, depth);
    }
  }

  // source --command_stream <stream> [--from_file] [--batch_mode]
  // (openfpga_basic.cpp source_existing_command)
  void source(const std::vector<std::string>& tokens, int depth) {
    std::string stream;
    bool fromFile = false;
    for (size_t i = 1; i < tokens.size(); ++i) {
      if (tokens[i] == "--command_stream" && i + 1 < tokens.size()) {
        stream = tokens[++i];
      } else if (tokens[i] == "--from_file") {
        fromFile = true;
      }
    }
    if (fromFile) {
      script(resolve(stream, workDir), depth + 1);
      return;
    }
    std::istringstream in(stream);
    for (std::string part; std::getline(in, part, ';');) {
      command(rtrimSpaces(part), depth + 1);
    }
  }
};

}  // namespace

std::vector<std::string> openFpgaScriptCommands(const std::string& script) {
  std::vector<std::string> commands;
  std::string pending;
  std::istringstream in(script);
  for (std::string line; std::getline(in, line);) {
    if (line.empty() || line.front() == '#') continue;
    std::string part = rtrimSpaces(line.substr(0, line.find('#')));
    if (!part.empty() && part.back() == '\\') {
      part.pop_back();
      pending += part;
      continue;
    }
    pending += part;
    const std::string command = ltrimSpaces(pending);
    pending.clear();
    if (!command.empty()) commands.push_back(command);
  }
  return commands;
}

std::vector<std::string> splitCommandTokens(const std::string& command) {
  std::vector<std::string> tokens;
  std::istringstream in(command);
  for (std::string token; in >> token;) tokens.push_back(token);
  return tokens;
}

std::vector<std::string> findDumpOptions(const std::vector<std::string>& tokens) {
  std::vector<std::string> found;
  for (size_t i = 0; i < tokens.size(); ++i) {
    const std::string_view token = tokens[i];
    const size_t eq = token.find('=');
    const std::string_view name = token.substr(0, eq);
    const bool inlineValue = eq != std::string_view::npos;

    bool alwaysDumps = false;
    for (std::string_view option : kAlwaysDumps) alwaysDumps |= (name == option);
    if (alwaysDumps) {
      found.emplace_back(name);
      continue;
    }

    if (name == kEchoFile) {
      std::string_view value;
      if (inlineValue) {
        value = token.substr(eq + 1);
      } else if (i + 1 < tokens.size()) {
        value = tokens[i + 1];
      }
      // Fail closed: only an explicit "off" is known not to write echo files.
      if (value != "off") {
        std::string hit(kEchoFile);
        if (!value.empty()) hit += " " + std::string(value);
        found.push_back(hit);
      }
    }
  }
  return found;
}

std::vector<std::string> findRefusedDumpOptions(const std::string& command,
                                                const std::filesystem::path& workDir,
                                                bool rpmAuthoringActive) {
  const std::vector<std::string> tokens = splitCommandTokens(command);
  if (tokens.empty()) return {};

  if (isProgram(tokens.front(), "vpr")) {
    std::vector<std::string> found = findDumpOptions(tokens);
    if (rpmAuthoringActive && isPlaceOnly(tokens)) {
      // getPlacementCommand() adds this for RPM-IP authoring. The same token
      // from user options on that command cannot be told apart, so it passes too.
      found.erase(std::remove(found.begin(), found.end(), kEchoFileOn), found.end());
    }
    return found;
  }

  if (isProgram(tokens.front(), "openfpga")) {
    const std::filesystem::path script = openFpgaScript(tokens, workDir);
    if (script.empty()) return {};
    ShellScan scan{workDir, {}, {}};
    scan.script(script, 0);
    return scan.found;
  }

  return {};
}

}  // namespace FOEDAG
