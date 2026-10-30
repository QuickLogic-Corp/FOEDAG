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

#include <array>
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

}  // namespace FOEDAG
