#include "PostSynthVerilogResourceExtractor.h"

#include <cctype>
#include <fstream>

#include "Utils/FileUtils.h"
#include "Utils/StringUtils.h"

namespace fp {

// Parses a Yosys flat post-synthesis Verilog (.v) file and collects all wire/
// port signal names as atom identifiers.  Naming rules mirror VPR's atoms:
//   - Escaped identifiers (leading \) are stored without the backslash.
//   - A [N:M] bus expands to individual bits "name[lo]" ... "name[hi]".
//   - A single-bit bus [k:k] is stored as "name" (no index suffix).

bool PostSynthVerilogResourceExtractor::loadAtomNamesFromVerilogFile(const std::filesystem::path& filePath)
{
  if (!std::filesystem::exists(filePath)) {
    m_error = "File not found: " + filePath.string();
    return false;
  }
  if (filePath.extension() != ".v") {
    m_error = "Expected a .v file, got: " + filePath.string();
    return false;
  }
  return parseAtomNamesFromVerilogFileContent(FOEDAG::FileUtils::GetFileContent(filePath));
}

bool PostSynthVerilogResourceExtractor::parseAtomNamesFromVerilogFileContent(const std::string& fileContent)
{
  m_error.clear();
  m_elements.clear();

  std::string content{fileContent};
  FOEDAG::StringUtils::replaceAllInPlace(content, "\r\n", "\n");
  FOEDAG::StringUtils::replaceAllInPlace(content, "\r", "\n");

  const auto lines = FOEDAG::StringUtils::tokenize(content, "\n");

  for (const auto& line : lines) {
    // Identify declaration keyword (wire / input / output)
    static const struct { const char* kw; size_t len; } kKeywords[] = {
      {"wire ",   5},
      {"input ",  6},
      {"output ", 7},
    };

    size_t kwPos = std::string::npos;
    size_t kwLen = 0;
    for (const auto& k : kKeywords) {
      const size_t p = line.find(k.kw);
      if (p != std::string::npos) { kwPos = p; kwLen = k.len; break; }
    }
    if (kwPos == std::string::npos) continue;

    // Require only whitespace before the keyword (skip "input wire", concatenations, etc.)
    bool leadingOnly = true;
    for (size_t i = 0; i < kwPos; ++i) {
      if (line[i] != ' ' && line[i] != '\t') { leadingOnly = false; break; }
    }
    if (!leadingOnly) continue;

    size_t pos = kwPos + kwLen;

    // Skip whitespace after keyword
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;

    // Optional range specifier [hi:lo]
    int rangeHi = -1, rangeLo = -1;
    if (pos < line.size() && line[pos] == '[') {
      ++pos;
      size_t numStart = pos;
      while (pos < line.size() && (line[pos] >= '0' && line[pos] <= '9')) ++pos;
      if (pos == numStart || pos >= line.size() || line[pos] != ':') continue;
      rangeHi = std::stoi(line.substr(numStart, pos - numStart));
      ++pos; // skip ':'
      numStart = pos;
      while (pos < line.size() && (line[pos] >= '0' && line[pos] <= '9')) ++pos;
      if (pos == numStart || pos >= line.size() || line[pos] != ']') continue;
      rangeLo = std::stoi(line.substr(numStart, pos - numStart));
      ++pos; // skip ']'
      while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
    }

    // Parse identifier — escaped (\...) or plain
    std::string name;
    if (pos < line.size() && line[pos] == '\\') {
      ++pos;
      const size_t nameStart = pos;
      while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t' && line[pos] != ';') ++pos;
      name = line.substr(nameStart, pos - nameStart);
    } else {
      if (pos >= line.size()) continue;
      const char c = line[pos];
      if (!std::isalpha(static_cast<unsigned char>(c)) && c != '_' && c != '$') continue;
      const size_t nameStart = pos;
      while (pos < line.size() &&
             (std::isalnum(static_cast<unsigned char>(line[pos])) ||
              line[pos] == '_' || line[pos] == '$')) {
        ++pos;
      }
      name = line.substr(nameStart, pos - nameStart);
    }

    if (name.empty()) continue;

    // Require a terminating semicolon (declaration line, not a port/expression)
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
    if (pos >= line.size() || line[pos] != ';') continue;

    // Emit element(s)
    if (rangeHi < 0) {
      m_elements.insert(name);
    } else {
      const int lo = std::min(rangeHi, rangeLo);
      const int hi = std::max(rangeHi, rangeLo);
      if (lo == hi) {
        // [k:k] — single-bit bus, store without index
        m_elements.insert(name);
      } else {
        for (int i = lo; i <= hi; ++i) {
          m_elements.insert(name + '[' + std::to_string(i) + ']');
        }
      }
    }
  }

  return true;
}

bool PostSynthVerilogResourceExtractor::dumpElementsToFile(const std::filesystem::path& filePath) const
{
  std::ofstream out{filePath};
  if (!out.is_open()) {
    return false;
  }
  for (const auto& element : m_elements) {
    out << element << '\n';
  }
  return out.good();
}

}  // namespace fp
