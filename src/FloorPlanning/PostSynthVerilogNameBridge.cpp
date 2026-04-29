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
        if (paramDepth > 0) continue;  // still inside params — keep going
        // fall through: params just closed, try inst name on the rest of this line
      }

      // ── EXPECT_INST_NAME state: param block done, look for "InstName (" ──
      if (!pendingModuleType.empty() && paramDepth == 0) {
        while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
        if (pos >= line.size() || line[pos] == '/' || line[pos] == '*') continue;
        // Guard: if we hit a module boundary, abandon pending state
        const size_t savedPos = pos;
        const std::string tok = parseIdent(line, pos);
        if (tok == "module" || tok == "endmodule") {
          pendingModuleType.clear();
          pos = savedPos;  // reprocess this line in normal state below
        } else if (!tok.empty()) {
          while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
          if (pos < line.size() && (line[pos] == '(' || line[pos] == '#' || line[pos] == ';')) {
            modules[currentModule].emplace_back(tok, pendingModuleType);
          }
          pendingModuleType.clear();
          continue;
        } else {
          continue;  // nothing yet, wait for next line
        }
      }

      // ── NORMAL state ──
      while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
      if (pos >= line.size() || line[pos] == '/' || line[pos] == '*') continue;

      const std::string first = parseIdent(line, pos);
      if (first.empty()) continue;

      if (first == "module") {
        currentModule = parseIdent(line, pos);
        if (!currentModule.empty())
          modules.emplace(currentModule, std::vector<std::pair<std::string, std::string>>{});
        continue;
      }
      if (first == "endmodule") { currentModule.clear(); continue; }
      if (currentModule.empty() || kVerilogKeywords.count(first)) continue;

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
            // #(...) spans multiple lines — enter SKIP_PARAMS
            pendingModuleType = first;
            continue;
          }
        }
      }

      const std::string second = parseIdent(line, pos);
      if (second.empty()) {
        if (hadParamBlock) {
          // Params closed on this line but inst name is on the next line
          pendingModuleType = first;
          paramDepth = 0;
        }
        continue;
      }

      while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
      if (pos >= line.size()) continue;
      if (line[pos] != '(' && line[pos] != '#' && line[pos] != ';') continue;

      modules[currentModule].emplace_back(second, first);  // (inst_name, module_type)
    }
  }

  // Top module = the one not instantiated by any other
  std::set<std::string> instantiated;
  for (const auto& [mod, insts] : modules)
    for (const auto& [iname, itype] : insts)
      instantiated.insert(itype);

  // Top module = the non-instantiated module with the most direct children.
  // Picking alphabetically-first breaks when leaf/utility modules (0 instances)
  // sort before the real top module.
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

  fprintf(stderr, "[NameBridge] topModule=%s  modules=%zu  instPaths=%zu\n",
          topModule.c_str(), modules.size(), m_instPaths.size());
  for (const auto& [path, type] : m_instPaths)
    fprintf(stderr, "  instPath: %s  (type: %s)\n", path.c_str(), type.c_str());

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

std::set<std::string> PostSynthVerilogNameBridge::resolveToVprNames(const std::string& userPath) const
{
  std::set<std::string> result;

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
