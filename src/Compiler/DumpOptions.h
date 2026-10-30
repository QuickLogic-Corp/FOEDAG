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

#include <filesystem>
#include <string>
#include <vector>

namespace FOEDAG {

/// Splits a command line the way Compiler::ExecuteAndMonitorSystemCommand
/// does (on whitespace), dropping empty tokens.
std::vector<std::string> splitCommandTokens(const std::string& command);

/// The VPR options in `tokens` that write decrypted architecture data to disk
/// (REQ-131 of aurora2's docs/specs/aurora-flow-engine). Accepts `--opt v` and
/// `--opt=v`. `--echo_file off` is allowed; any other `--echo_file` value is
/// reported. Each hit is named as given: "--write_rr_graph" or
/// "--echo_file on".
std::vector<std::string> findDumpOptions(const std::vector<std::string>& tokens);

/// The dump options `command` would hand to VPR, which an encrypted device must
/// refuse: the command's own arguments when its program is vpr, or those of
/// every `vpr` line of the script when it is `openfpga -f|--file <script>`
/// (a relative script path is resolved against `workDir`, where the command
/// runs). Any other program finds nothing.
///
/// `rpmAuthoringActive` exempts "--echo_file on", and nothing else, on a vpr
/// place-only command (--place or --analytical_place, without --pack, --route
/// or --analysis): the RPM-IP authoring place run needs the echo files.
std::vector<std::string> findRefusedDumpOptions(const std::string& command,
                                                const std::filesystem::path& workDir,
                                                bool rpmAuthoringActive);

}  // namespace FOEDAG
