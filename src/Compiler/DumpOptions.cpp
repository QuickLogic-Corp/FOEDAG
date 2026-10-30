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
#include <fstream>
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

// "vpr", "/opt/bin/vpr" and "vpr.exe" are vpr; "vpr_helper" is not.
bool isProgram(const std::string& token, std::string_view name) {
  return std::filesystem::path(token).stem() == name;
}

bool hasToken(const std::vector<std::string>& tokens, std::string_view token) {
  return std::find(tokens.begin(), tokens.end(), token) != tokens.end();
}

bool isPlaceOnly(const std::vector<std::string>& tokens) {
  return (hasToken(tokens, "--place") || hasToken(tokens, "--analytical_place")) &&
         !hasToken(tokens, "--pack") && !hasToken(tokens, "--route") &&
         !hasToken(tokens, "--analysis");
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
  const std::filesystem::path path(script);
  return path.is_absolute() ? path : workDir / path;
}

}  // namespace

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
    // A script that cannot be read cannot be run either, so it dumps nothing.
    std::ifstream script(openFpgaScript(tokens, workDir));
    std::vector<std::string> found;
    for (std::string line; std::getline(script, line);) {
      const std::vector<std::string> lineTokens = splitCommandTokens(line);
      if (lineTokens.empty() || lineTokens.front() != "vpr") continue;
      for (std::string& hit : findDumpOptions(lineTokens)) found.push_back(std::move(hit));
    }
    return found;
  }

  return {};
}

}  // namespace FOEDAG
