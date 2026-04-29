// ═══════════════════════════════════════════════════════════════════════════
// PostSynthVerilogNameBridge — mapping algorithm
// ═══════════════════════════════════════════════════════════════════════════
//
// Two independent name spaces must be bridged:
//
//   RTL netlist  — names the designer wrote in src/*.v
//                  (module instances, ports, bus signals)
//
//   VPR atoms    — names used internally by VPR after synthesis and
//                  technology mapping (from atom_netlist.cleaned.echo.blif)
//
// The synthesis tool (Yosys) preserves hierarchy in VPR atom names using
// dot-separated paths, e.g.:
//   dut.instPerm19977.inCount_sdffre_Q_R
//   dut.instPerm19977.in_sw_0_0.x0[3]
//
// This allows prefix matching: the RTL path "dut.instPerm19977" covers every
// VPR atom whose name starts with "dut.instPerm19977.".
//
// ── Step 1  loadRtlSources() ─────────────────────────────────────────────
//   Parses src/*.v and builds:
//     m_instPaths  — all hierarchical instance paths reachable from the top
//                    module (e.g. "dut", "dut.instPerm19977")
//     top-module signals — individual port/wire/reg names of the top module,
//                    bus-expanded (e.g. "din[0]"…"din[63]", "next_out")
//                    These are merged into m_instPaths with empty module type.
//
// ── Step 2  setVprNetlist() ──────────────────────────────────────────────
//   Stores all VPR atom names in m_vprNames (Yosys constants excluded).
//
// ── Step 3  resolveToVprNames(userPath) ──────────────────────────────────
//   Given one entry from m_instPaths, returns the VPR atoms it covers:
//     • exact match      "next_out"           → {"next_out"}
//     • dot prefix       "dut.instPerm19977"  → all atoms starting with
//                                               "dut.instPerm19977."
//     • bracket prefix   "din"                → all atoms starting with
//                                               "din["  (i.e. din[0]…din[63])
//
// ── What is shown in the UI ──────────────────────────────────────────────
//   Column 1 (Netlist)   — every entry in m_instPaths; bus signals are
//                          grouped under a collapsible parent (e.g. din →
//                          [0], [1], …) so each bit is checkable individually.
//   Column 2 (VPR Names) — filled only for LEAF netlist items (instances or
//                          bus bits with no RTL children).  Non-leaf items
//                          leave column 2 empty to avoid unreadable lists.
//
// ── Unmapped cases ───────────────────────────────────────────────────────
//   Unmapped VPR atom — a VPR name that is not reachable via any prefix in
//                       m_instPaths.  Typical cause: synthesis-generated
//                       internal net (e.g. "reset_$lut_A_Y").  These atoms
//                       are never added to m_instPaths and therefore never
//                       appear in column 1.  Logged to stderr as:
//                         [unmapped VPR] reset_$lut_A_Y
//
//   Unmapped netlist  — an RTL leaf whose resolveToVprNames() returns empty.
//                       Typical cause: the signal was optimised away during
//                       synthesis (constant folded, dead logic removed) or
//                       was renamed in a way the prefix rule cannot follow.
//                       The row is hidden from the UI so the user only sees
//                       placeable items.  Logged to stderr as:
//                         [unmapped netlist] some_signal
// ═══════════════════════════════════════════════════════════════════════════

#include "PostSynthVerilogNameBridge.h"

#include <cctype>
#include <set>
#include <string>

#include "Utils/FileUtils.h"
#include "Utils/StringUtils.h"

namespace fp {

namespace {

const std::set<std::string> kVprConstants{"$false", "$true", "$undef"};

const std::set<std::string> kVerilogKeywords{
  "module", "endmodule", "input", "output", "inout", "wire", "reg", "logic",
  "always", "assign", "begin", "end", "if", "else", "case", "casex", "casez",
  "endcase", "for", "initial", "parameter", "localparam", "integer", "genvar",
  "generate", "endgenerate", "function", "endfunction", "task", "endtask",
  "posedge", "negedge", "or", "and", "not", "buf", "nor", "nand", "xor",
  "specify", "endspecify", "default",
};

std::string parseIdent(const std::string& s, size_t& pos)
{
  while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) ++pos;
  if (pos >= s.size()) return {};
  const char c = s[pos];
  if (!std::isalpha(static_cast<unsigned char>(c)) && c != '_') return {};
  const size_t start = pos;
  while (pos < s.size() &&
         (std::isalnum(static_cast<unsigned char>(s[pos])) || s[pos] == '_'))
    ++pos;
  return s.substr(start, pos - start);
}

}  // namespace

void PostSynthVerilogNameBridge::setVprNetlist(const std::set<std::string>& elements)
{
  m_vprNames.clear();
  for (const auto& e : elements) {
    if (!kVprConstants.count(e))
      m_vprNames.insert(e);
  }
}

bool PostSynthVerilogNameBridge::loadVprNetlist(const std::filesystem::path& filePath)
{
  if (!std::filesystem::exists(filePath)) {
    m_error = "File not found: " + filePath.string();
    return false;
  }
  m_vprNames.clear();
  const auto lines = FOEDAG::FileUtils::GetFileContentLines(filePath);
  for (const auto& line : lines) {
    if (!line.empty() && !kVprConstants.count(line))
      m_vprNames.insert(line);
  }
  return true;
}

// loadRtlSources — RTL name extraction
//
// Goal: build two tables from the user's Verilog source files:
//   m_instPaths  — hierarchical instance paths (e.g. "dut", "dut.instPerm19977")
//   top-module signals — port/wire/reg names of the top module
//                        (e.g. "clk", "din[0]"…"din[63]", "next_out")
//
// These become the items shown in column 1 of the floor-planning netlist view.
// Only names that appear in BOTH the RTL source and the VPR netlist are
// ultimately displayed; synthesis-generated names (e.g. "reset_$lut_A_Y")
// never end up in these tables.
//
// ── Pattern 1: module instantiation (plain) ──────────────────────────────
//   ModuleType InstName ( ...
//   e.g.  statementList19975 dut ( .clk(clk), ... );
//   Produces instPath "dut" → type "statementList19975".
//
// ── Pattern 2: module instantiation (parameterised, single-line) ─────────
//   ModuleType #( .P(v) ) InstName ( ...
//   e.g.  MyFifo #(.DEPTH(16)) fifo0 ( ... );
//   The #(…) block is skipped; "fifo0" is extracted as the instance name.
//
// ── Pattern 3: module instantiation (parameterised, multi-line) ──────────
//   ModuleType #(          ← line ends inside #(…)
//       .P(v)
//   ) InstName (           ← ) closes the param block; InstName follows
//   A stateful SKIP_PARAMS / EXPECT_INST_NAME scanner tracks depth across
//   lines so the instance name is found even when spread over many lines.
//
// ── Pattern 4: ANSI port list in module header ───────────────────────────
//   module top ( input wire clk, output wire [63:0] dout, ... );
//   Each "input / output / inout" token in the header's ( … ) is parsed for
//   an optional modifier (wire/reg/logic), an optional range [hi:lo], and
//   then the port name.  Bus ranges are expanded: [63:0] → [0]…[63].
//
// ── Pattern 5: non-ANSI port / signal declarations in module body ─────────
//   input  clk, reset;
//   output reg next_out;
//   wire   [63:0] din;
//   reg    [7:0]  addr;
//   The leading keyword (input/output/inout/wire/reg/logic) is consumed,
//   followed by an optional modifier, an optional range, and then one or
//   more comma-separated identifiers.  Bus ranges expand as in Pattern 4.
//
// Only signals belonging to the TOP module (the one not instantiated by any
// other parsed module) are promoted to m_instPaths.  Signals of sub-modules
// are already covered by their parent's instPath prefix matching in
// resolveToVprNames().
bool PostSynthVerilogNameBridge::loadRtlSources(const std::vector<std::filesystem::path>& files)
{
  m_instPaths.clear();

  // module name → list of (inst_name, module_type)
  std::map<std::string, std::vector<std::pair<std::string, std::string>>> modules;
  // module name → set of declared signal/port base names
  std::map<std::string, std::set<std::string>> moduleSignals;

  // Signal-declaration keywords: when seen as the first token inside a module
  // body they introduce a port/wire/reg declaration, not a module instantiation.
  static const std::set<std::string> kDeclKeywords{
    "input", "output", "inout", "wire", "reg", "logic"
  };

  // Parse comma-separated signal names from a declaration after the leading
  // keyword has been consumed.  Expands bus ranges: "reg [63:0] din" adds
  // din[0]…din[63] individually.  Stops at a port-direction keyword so the
  // ANSI port list scanner can call this per-port.
  static const std::set<std::string> kDirKeywords{"input","output","inout"};
  static const std::set<std::string> kModifiers{"wire","reg","logic","signed","unsigned"};

  auto parseSigNames = [](const std::string& line, size_t pos,
                           std::set<std::string>& dest) {
    size_t p = pos;
    // Skip optional modifier keyword
    while (p < line.size() && (line[p]==' '||line[p]=='\t')) ++p;
    if (p < line.size() && std::isalpha(static_cast<unsigned char>(line[p]))) {
      size_t p2 = p;
      const std::string mod = parseIdent(line, p2);
      static const std::set<std::string> kMod{"wire","reg","logic","signed","unsigned"};
      if (kMod.count(mod)) p = p2;
    }
    // Parse optional packed range [hi:lo] — remember the bounds for bus expansion
    while (p < line.size() && (line[p]==' '||line[p]=='\t')) ++p;
    int rangeHi = -1, rangeLo = -1;
    if (p < line.size() && line[p] == '[') {
      ++p;
      size_t ns = p;
      while (p < line.size() && line[p] >= '0' && line[p] <= '9') ++p;
      if (ns != p && p < line.size() && line[p] == ':') {
        rangeHi = std::stoi(line.substr(ns, p - ns));
        ++p;
        ns = p;
        while (p < line.size() && line[p] >= '0' && line[p] <= '9') ++p;
        if (ns != p && p < line.size() && line[p] == ']')
          rangeLo = std::stoi(line.substr(ns, p - ns));
      }
      while (p < line.size() && line[p] != ']') ++p;
      if (p < line.size()) ++p;
    }
    // Comma-separated identifiers until ';', ')', comment, or a direction keyword
    while (p < line.size()) {
      while (p < line.size() && (line[p]==' '||line[p]=='\t'||line[p]==',')) ++p;
      if (p >= line.size() || line[p]==';' || line[p]==')' || line[p]=='/' || line[p]=='*') break;
      size_t namStart = p;
      const std::string name = parseIdent(line, p);
      if (name.empty()) break;
      // Stop if we hit a direction keyword (ANSI port list boundary)
      static const std::set<std::string> kStop{"input","output","inout","parameter","module","endmodule"};
      if (kStop.count(name)) { p = namStart; break; }
      // Expand bus or add scalar
      if (rangeHi >= 0 && rangeLo >= 0 && rangeHi != rangeLo) {
        const int lo = std::min(rangeHi, rangeLo);
        const int hi = std::max(rangeHi, rangeLo);
        for (int i = lo; i <= hi; ++i)
          dest.insert(name + "[" + std::to_string(i) + "]");
      } else {
        dest.insert(name);
      }
      // Skip optional unpacked range after name
      while (p < line.size() && (line[p]==' '||line[p]=='\t')) ++p;
      if (p < line.size() && line[p]=='[') {
        while (p < line.size() && line[p]!=']') ++p;
        if (p < line.size()) ++p;
      }
    }
  };

  for (const auto& file : files) {
    if (!std::filesystem::exists(file)) {
      m_error = "File not found: " + file.string();
      return false;
    }
    std::string content = FOEDAG::FileUtils::GetFileContent(file);
    FOEDAG::StringUtils::replaceAllInPlace(content, "\r\n", "\n");
    FOEDAG::StringUtils::replaceAllInPlace(content, "\r", "\n");

    std::string currentModule;
    // State for multi-line instantiation patterns:
    //   pendingModuleType non-empty  →  we saw "ModType" but haven't found inst name yet
    //   paramDepth > 0               →  still inside the #(...) parameter block
    //   paramDepth == 0              →  parameter block closed, looking for inst name
    std::string pendingModuleType;
    int paramDepth = 0;

    for (const auto& line : FOEDAG::StringUtils::tokenize(content, "\n")) {
      size_t pos = 0;

      // ── SKIP_PARAMS state: scan for the closing ) of a multi-line #(…) ──
      if (!pendingModuleType.empty() && paramDepth > 0) {
        for (; pos < line.size(); ++pos) {
          if      (line[pos] == '(') ++paramDepth;
          else if (line[pos] == ')') { if (--paramDepth == 0) { ++pos; break; } }
        }
        if (paramDepth > 0) continue;
        // fall through: params just closed, try inst name on the rest of this line
      }

      // ── EXPECT_INST_NAME state: param block done, look for "InstName (" ──
      if (!pendingModuleType.empty() && paramDepth == 0) {
        while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
        if (pos >= line.size() || line[pos] == '/' || line[pos] == '*') continue;
        const size_t savedPos = pos;
        const std::string tok = parseIdent(line, pos);
        if (tok == "module" || tok == "endmodule") {
          pendingModuleType.clear();
          pos = savedPos;
        } else if (!tok.empty()) {
          while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
          if (pos < line.size() && (line[pos] == '(' || line[pos] == '#' || line[pos] == ';'))
            modules[currentModule].emplace_back(tok, pendingModuleType);
          pendingModuleType.clear();
          continue;
        } else {
          continue;
        }
      }

      // ── NORMAL state ──
      while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
      if (pos >= line.size() || line[pos] == '/' || line[pos] == '*') continue;

      const std::string first = parseIdent(line, pos);
      if (first.empty()) continue;

      if (first == "module") {
        currentModule = parseIdent(line, pos);
        if (!currentModule.empty()) {
          modules.emplace(currentModule, std::vector<std::pair<std::string, std::string>>{});
          moduleSignals.emplace(currentModule, std::set<std::string>{});
          // Also extract ANSI-style port names from the module header line:
          // module name ( input wire clk, output wire q, ... );
          while (pos < line.size() && line[pos] != '(' && line[pos] != ';') ++pos;
          if (pos < line.size() && line[pos] == '(') {
            ++pos;
            while (pos < line.size() && line[pos] != ')' && line[pos] != ';') {
              while (pos < line.size() && !std::isalpha(static_cast<unsigned char>(line[pos]))
                     && line[pos] != ')') ++pos;
              if (pos >= line.size() || line[pos] == ')') break;
              const std::string tok = parseIdent(line, pos);
              if (tok == "input" || tok == "output" || tok == "inout")
                parseSigNames(line, pos, moduleSignals[currentModule]);
              else {
                while (pos < line.size() && line[pos] != ',' && line[pos] != ')') ++pos;
              }
              if (pos < line.size() && line[pos] == ',') ++pos;
            }
          }
        }
        continue;
      }
      if (first == "endmodule") { currentModule.clear(); continue; }
      if (currentModule.empty()) continue;

      // ── Port/signal declarations inside a module body ──
      if (kDeclKeywords.count(first)) {
        parseSigNames(line, pos, moduleSignals[currentModule]);
        continue;
      }

      if (kVerilogKeywords.count(first)) continue;

      // <ModuleType> [#(...)] <instName>(
      while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
      bool hadParamBlock = false;
      if (pos < line.size() && line[pos] == '#') {
        hadParamBlock = true;
        ++pos;
        while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
        if (pos < line.size() && line[pos] == '(') {
          paramDepth = 0;
          for (; pos < line.size(); ++pos) {
            if      (line[pos] == '(') ++paramDepth;
            else if (line[pos] == ')') { if (--paramDepth == 0) { ++pos; break; } }
          }
          if (paramDepth > 0) {
            pendingModuleType = first;
            continue;
          }
        }
      }

      const std::string second = parseIdent(line, pos);
      if (second.empty()) {
        if (hadParamBlock) { pendingModuleType = first; paramDepth = 0; }
        continue;
      }

      while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
      if (pos >= line.size()) continue;
      if (line[pos] != '(' && line[pos] != '#' && line[pos] != ';') continue;

      modules[currentModule].emplace_back(second, first);  // (inst_name, module_type)
    }
  }

  // Top module = non-instantiated module with the most direct children.
  std::set<std::string> instantiated;
  for (const auto& [mod, insts] : modules)
    for (const auto& [iname, itype] : insts)
      instantiated.insert(itype);

  std::string topModule;
  int topChildCount = -1;
  for (const auto& [mod, insts] : modules) {
    if (!instantiated.count(mod)) {
      const int n = static_cast<int>(insts.size());
      if (n > topChildCount) { topChildCount = n; topModule = mod; }
    }
  }
  if (topModule.empty() && !modules.empty())
    topModule = modules.begin()->first;

  std::set<std::string> callStack;
  buildInstPaths(topModule, "", modules, callStack);

  // Add top-module port/signal declarations that aren't already covered by an
  // instance path.  Only RTL-declared names are added — synthesis-generated
  // names (e.g. reset_$lut_A_Y) are never in moduleSignals and stay unmapped.
  if (moduleSignals.count(topModule)) {
    for (const auto& sig : moduleSignals.at(topModule)) {
      if (!m_instPaths.count(sig) && !kVerilogKeywords.count(sig))
        m_instPaths.emplace(sig, "");  // empty type = top-level signal
    }
  }

  fprintf(stderr, "[NameBridge] topModule=%s  modules=%zu  instPaths=%zu\n",
          topModule.c_str(), modules.size(), m_instPaths.size());
  for (const auto& [path, type] : m_instPaths)
    fprintf(stderr, "  instPath: %s  (type: %s)\n", path.c_str(), type.c_str());

  // VPR names not covered by any instPath prefix (unmapped = no RTL counterpart found)
  auto isMapped = [&](const std::string& vprName) -> bool {
    if (m_instPaths.count(vprName)) return true;
    size_t pos = 0;
    while ((pos = vprName.find('.', pos)) != std::string::npos) {
      if (m_instPaths.count(vprName.substr(0, pos))) return true;
      ++pos;
    }
    const size_t bracket = vprName.find('[');
    if (bracket != std::string::npos && m_instPaths.count(vprName.substr(0, bracket))) return true;
    return false;
  };

  size_t unmappedCount = 0;
  for (const auto& name : m_vprNames) {
    if (!isMapped(name)) {
      fprintf(stderr, "  unmapped VPR: %s\n", name.c_str());
      ++unmappedCount;
    }
  }
  fprintf(stderr, "[NameBridge] unmapped VPR names: %zu / %zu\n", unmappedCount, m_vprNames.size());

  return true;
}

void PostSynthVerilogNameBridge::buildInstPaths(
    const std::string& moduleName,
    const std::string& pathPrefix,
    const std::map<std::string, std::vector<std::pair<std::string, std::string>>>& modules,
    std::set<std::string>& callStack)
{
  if (callStack.count(moduleName)) return;  // guard against circular instantiation
  callStack.insert(moduleName);

  auto it = modules.find(moduleName);
  if (it != modules.end()) {
    for (const auto& [instName, moduleType] : it->second) {
      const std::string fullPath =
          pathPrefix.empty() ? instName : pathPrefix + "." + instName;
      m_instPaths[fullPath] = moduleType;
      buildInstPaths(moduleType, fullPath, modules, callStack);
    }
  }

  callStack.erase(moduleName);
}

PostSynthVerilogNameBridge::NaturalStringSet
PostSynthVerilogNameBridge::resolveToVprNames(const std::string& userPath) const
{
  NaturalStringSet result;

  auto addWithPrefix = [&](const std::string& prefix) {
    for (auto it = m_vprNames.lower_bound(prefix);
         it != m_vprNames.end() && it->size() >= prefix.size() &&
         it->compare(0, prefix.size(), prefix) == 0;
         ++it) {
      result.insert(*it);
    }
  };

  if (m_vprNames.count(userPath)) result.insert(userPath);  // exact
  addWithPrefix(userPath + ".");                             // instance subtree
  addWithPrefix(userPath + "[");                            // bus bits

  return result;
}

}  // namespace fp
