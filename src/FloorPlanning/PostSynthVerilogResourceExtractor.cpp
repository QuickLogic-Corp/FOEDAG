#include "PostSynthVerilogResourceExtractor.h"

#include <cctype>
#include <fstream>
#include <set>

#include "Utils/FileUtils.h"
#include "Utils/StringUtils.h"

namespace fp {

namespace {

// Keywords that open a declaration/statement this extractor never needs to
// look inside — real leaf-cell identity lives in instantiation statements,
// not these. Post-synthesis Verilog is a flat, already-elaborated netlist
// (flatten has already run), so RTL-only constructs like generate/genvar
// never appear here.
const std::set<std::string> kSkipKeywords{
  "module", "endmodule", "input", "output", "inout", "wire", "reg", "assign",
  "parameter", "localparam",
};

bool isIdentStart(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}
bool isIdentChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

std::string parsePlainIdent(const std::string& line, size_t& pos) {
  while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
  if (pos >= line.size() || !isIdentStart(line[pos])) return {};
  const size_t start = pos;
  while (pos < line.size() && isIdentChar(line[pos])) ++pos;
  return line.substr(start, pos - start);
}

// Escaped identifier: '\' followed by any non-whitespace characters, per the
// Verilog escaped-identifier rule — internal '.', '[', ']' are just part of
// the token and must not be treated specially here. Returns the name without
// the leading backslash. Assumes line[pos] == '\\' on entry.
std::string parseEscapedIdent(const std::string& line, size_t& pos) {
  ++pos;
  const size_t start = pos;
  while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t') ++pos;
  return line.substr(start, pos - start);
}

// Yosys-internal optimizer temporaries have no user-meaningful RTL identity
// (e.g. "$abc$1234$auto$blah.cc:5678:some_pass$99") and must be excluded.
bool isAnonymousCell(const std::string& name) {
  if (name.empty()) return false;
  if (name[0] == '$') return true;
  if (name.find("$abc$") != std::string::npos) return true;
  if (name.find("$auto$") != std::string::npos) return true;
  return false;
}

}  // namespace

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

// Parses a flat, post-synthesis (post-flatten, post-techmap) Verilog file and
// collects real leaf-cell instance names — i.e. the identifiers on module
// instantiation statements:
//   <celltype> \<escaped-instance-name>  (<port connections>);
//   <celltype> <plain-instance-name> (<port connections>);
//   <celltype> #(<params>) <instance-name> (<port connections>);   (params may span lines)
//
// Real leaf-cell identity in synthesized output lives almost exclusively in
// these instantiation identifiers, not in wire/input/output declarations —
// intermediate nets are frequently optimized straight through without ever
// getting a standalone declaration. Attribute lines
// ("(* src = "..." *)", "(* module_not_derived = ... *)") are skipped, not
// parsed for source-location data — this extractor keeps a single namespace
// (the raw synthesized instance name), per the project's naming design.
bool PostSynthVerilogResourceExtractor::parseAtomNamesFromVerilogFileContent(const std::string& fileContent)
{
  m_error.clear();
  m_elements.clear();

  std::string content{fileContent};
  FOEDAG::StringUtils::replaceAllInPlace(content, "\r\n", "\n");
  FOEDAG::StringUtils::replaceAllInPlace(content, "\r", "\n");

  const auto lines = FOEDAG::StringUtils::tokenize(content, "\n");

  // Scans forward from `pos`, tracking paren depth (already `depth` deep on
  // entry). Returns true and leaves `pos` just past the closing ')' if depth
  // returns to 0 on this line; otherwise returns false, leaving `depth` set
  // to resume on a later line. '[' / ']' (bit-select brackets inside port
  // expressions, or mid-path brackets inside an escaped instance name) never
  // affect paren depth.
  auto skipParens = [](const std::string& line, size_t& pos, int& depth) -> bool {
    for (; pos < line.size(); ++pos) {
      if (line[pos] == '(') ++depth;
      else if (line[pos] == ')') { if (--depth == 0) { ++pos; return true; } }
    }
    return false;
  };

  enum class State { Normal, AttrBlock, ParamBlock, AwaitInstName, PortBlock };
  State state = State::Normal;
  int depth = 0;

  // Tries to parse an instance name at `pos` (skipping leading whitespace
  // first). On success (name found, followed by '('), records it (subject to
  // the anonymous-cell filter) and transitions to PortBlock/Normal depending
  // on whether the port-connection list closes on this same line. On
  // failure (no name, or no '(' following it), gives up on this statement
  // and returns to Normal — malformed/unrecognized lines are skipped rather
  // than treated as an error.
  auto tryParseInstance = [&](const std::string& line, size_t& pos) {
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
    if (pos >= line.size()) { state = State::AwaitInstName; return; }
    const std::string instName =
        (line[pos] == '\\') ? parseEscapedIdent(line, pos) : parsePlainIdent(line, pos);
    if (instName.empty()) { state = State::Normal; return; }
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
    if (pos >= line.size() || line[pos] != '(') { state = State::Normal; return; }
    if (!isAnonymousCell(instName)) m_elements.insert(instName);
    depth = 0;
    state = skipParens(line, pos, depth) ? State::Normal : State::PortBlock;
  };

  for (const auto& line : lines) {
    size_t pos = 0;

    if (state == State::AttrBlock) {
      while (pos < line.size()) {
        if (line[pos] == '*' && pos + 1 < line.size() && line[pos + 1] == ')') {
          pos += 2;
          state = State::Normal;
          break;
        }
        ++pos;
      }
      continue;  // observed attribute lines are self-contained; nothing else to parse on this line
    }

    if (state == State::PortBlock) {
      if (!skipParens(line, pos, depth)) continue;
      state = State::Normal;
      continue;  // ignore trailing ';'
    }

    if (state == State::ParamBlock) {
      if (!skipParens(line, pos, depth)) continue;
      state = State::AwaitInstName;
      // fall through: look for the instance name in the remainder of this line
    }

    if (state == State::AwaitInstName) {
      tryParseInstance(line, pos);
      continue;
    }

    // state == State::Normal
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
    if (pos >= line.size()) continue;

    // Attribute line: (* ... *), possibly spanning multiple lines.
    if (line[pos] == '(' && pos + 1 < line.size() && line[pos + 1] == '*') {
      pos += 2;
      bool closed = false;
      while (pos < line.size()) {
        if (line[pos] == '*' && pos + 1 < line.size() && line[pos + 1] == ')') {
          closed = true;
          pos += 2;
          break;
        }
        ++pos;
      }
      if (!closed) state = State::AttrBlock;
      continue;
    }

    if (line[pos] == '/') continue;  // comment line

    const std::string first = parsePlainIdent(line, pos);
    if (first.empty() || kSkipKeywords.count(first)) continue;

    // `first` is a candidate cell type. Optional #(...) parameter block,
    // then the instance name.
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;

    if (pos < line.size() && line[pos] == '#') {
      ++pos;
      while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
      if (pos >= line.size() || line[pos] != '(') continue;  // malformed; give up on this line
      depth = 0;
      if (skipParens(line, pos, depth)) {
        tryParseInstance(line, pos);
      } else {
        state = State::ParamBlock;  // parameter block continues on later lines
      }
      continue;
    }

    tryParseInstance(line, pos);
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
