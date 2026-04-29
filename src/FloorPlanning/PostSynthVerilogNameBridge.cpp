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
