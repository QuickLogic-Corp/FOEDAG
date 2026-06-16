/*
Copyright 2022 The Foedag team

GPL License

Copyright (c) 2022 The Open-Source FPGA Foundation

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

#include "IPGenerate/IPCatalog.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <QDebug>
#include <QFile>
#include <QProcess>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include "Compiler/Log.h"
#include "Compiler/WorkerThread.h"
#include "MainWindow/Session.h"
#include "Utils/FileUtils.h"
#include "Utils/StringUtils.h"
#include "nlohmann_json/json.hpp"
using json = nlohmann::ordered_json;

extern FOEDAG::Session* GlobalSession;
using namespace FOEDAG;
using Time = std::chrono::high_resolution_clock;
using ms = std::chrono::milliseconds;

std::string Constant::m_name;

namespace {
// Parse the entire string as a base-10 integer (optional sign, trailing
// whitespace allowed). Returns true only if the whole string is consumed.
bool parseFullInt(const std::string& s, long long& out) {
  if (s.empty()) return false;
  try {
    size_t pos = 0;
    out = std::stoll(s, &pos, 10);
    while (pos < s.size() &&
           std::isspace(static_cast<unsigned char>(s[pos]))) {
      ++pos;
    }
    return pos == s.size();
  } catch (...) {
    return false;
  }
}

// Parse the entire string as a floating point value (trailing whitespace
// allowed). Returns true only if the whole string is consumed.
bool parseFullDouble(const std::string& s, double& out) {
  if (s.empty()) return false;
  try {
    size_t pos = 0;
    out = std::stod(s, &pos);
    while (pos < s.size() &&
           std::isspace(static_cast<unsigned char>(s[pos]))) {
      ++pos;
    }
    return pos == s.size();
  } catch (...) {
    return false;
  }
}

// True if `s` is a recognized boolean literal (case-insensitive):
// 0 / 1 / true / false.
bool isBool(const std::string& s) {
  const std::string v = StringUtils::toLower(s);
  return v == "0" || v == "1" || v == "true" || v == "false";
}

// Interpret a boolean-ish string: true for "1" / "true" (case-insensitive),
// false for anything else (including unrecognized values).
bool toBool(const std::string& s) {
  const std::string v = StringUtils::toLower(s);
  return v == "1" || v == "true";
}
}  // namespace

bool IPParameter::Validate(const std::string& value,
                           std::string& errorMsg) const {
  errorMsg.clear();

  // An empty value means "fall back to the parameter default", which is assumed
  // valid (the same way the GUI leaves an untouched field at its default).
  if (value.empty()) return true;

  // Mirror the GUI widget-selection precedence in
  // IPDialogBox::CreateParamFields():
  //   Bool -> FilePath -> options (combobox) -> range (validated input) -> input

  // Bool: accept common boolean literals only.
  if (m_paramType == ParamType::Bool) {
    if (isBool(value)) return true;
    errorMsg =
        "'" + value + "' is not a valid boolean (expected 0/1/true/false)";
    return false;
  }

  // FilePath: the GUI does not validate path contents, so neither do we.
  if (m_paramType == ParamType::FilePath) return true;

  // Enum (combobox): value must be one of the declared options.
  if (!m_options.empty()) {
    if (std::find(m_options.begin(), m_options.end(), value) !=
        m_options.end()) {
      return true;
    }
    std::string opts;
    for (size_t i = 0; i < m_options.size(); ++i) {
      opts += (i ? ", " : "") + m_options[i];
    }
    errorMsg = "'" + value +
               "' is not a valid option (expected one of: " + opts + ")";
    return false;
  }

  // Ranged numeric input: must be numeric AND within [min, max]. Range option
  // only applies to Int/Float (matching the GUI, which ignores it otherwise).
  if (m_range.size() == 2 &&
      (m_paramType == ParamType::Int || m_paramType == ParamType::Float)) {
    if (m_paramType == ParamType::Int) {
      long long v{};
      if (!parseFullInt(value, v)) {
        errorMsg = "'" + value + "' is not a valid integer";
        return false;
      }
      long long lo{}, hi{};
      // If the catalog range metadata itself doesn't parse, skip the bounds
      // check rather than rejecting the value over bad metadata.
      if (parseFullInt(m_range[0], lo) && parseFullInt(m_range[1], hi) &&
          (v < lo || v > hi)) {
        errorMsg = "value " + value + " is out of range [" + m_range[0] + ", " +
                   m_range[1] + "]";
        return false;
      }
      return true;
    }
    double v{};
    if (!parseFullDouble(value, v)) {
      errorMsg = "'" + value + "' is not a valid number";
      return false;
    }
    double lo{}, hi{};
    if (parseFullDouble(m_range[0], lo) && parseFullDouble(m_range[1], hi) &&
        (v < lo || v > hi)) {
      errorMsg = "value " + value + " is out of range [" + m_range[0] + ", " +
                 m_range[1] + "]";
      return false;
    }
    return true;
  }

  // Plain input: numeric types must still parse; strings accept anything.
  if (m_paramType == ParamType::Int) {
    long long v{};
    if (!parseFullInt(value, v)) {
      errorMsg = "'" + value + "' is not a valid integer";
      return false;
    }
  } else if (m_paramType == ParamType::Float) {
    double v{};
    if (!parseFullDouble(value, v)) {
      errorMsg = "'" + value + "' is not a valid number";
      return false;
    }
  }

  return true;
}

bool IPParameter::IsActive(
    const std::map<std::string, std::string>& paramValues) const {
  // Statically disabled fields are never editable.
  if (toBool(m_disable)) return false;

  // A dependency-gated field is active only when ALL of its controlling boolean
  // parameters are true. (The catalog "dependency" key lists those bools; e.g.
  // field_with_dep depends on bool_for_dep.) A dependency missing from the map
  // is treated as false.
  for (const auto& depName : m_dependencies) {
    auto it = paramValues.find(depName);
    if (it == paramValues.end() || !toBool(it->second)) return false;
  }
  return true;
}

bool IPCatalog::addIP(IPDefinition* def) {
  if (m_definitionMap.find(def->Name()) == m_definitionMap.end()) {
    m_definitionMap.emplace(def->Name(), def);
    m_definitions.push_back(def);
    return true;
  } else {
    return false;
  }
}

IPDefinition* IPCatalog::Definition(const std::string& name) {
  std::map<std::string, IPDefinition*>::iterator itr =
      m_definitionMap.find(name);
  if (itr == m_definitionMap.end()) {
    return nullptr;
  } else {
    return (*itr).second;
  }
}

void IPCatalog::WriteCatalog(std::ostream& out) {
  for (auto def : m_definitions) {
    out << "IP Name: " << def->Name() << std::endl;
  }
}

// This takes a path to an IP and parses the vendor, library, name, and version
// info from the parent directories. This assumes that the directory format is
// Vendor/Library/Name/Version
VLNV FOEDAG::getIpInfoFromPath(std::filesystem::path path) {
  std::string vendor, library, name, version;

  std::string separator =
      std::string(1, std::filesystem::path::preferred_separator);

  // Specify our container variables in the order we will read into them
  // Note we will read from the back of the path first
  std::vector<std::string*> values = {&version, &name, &library, &vendor};

  // split the path into tokens
  std::vector<std::string> tokens;
  StringUtils::tokenize(path.string(), separator, tokens);

  // Take off the child node if it is a python file
  if (StringUtils::endsWith(tokens.back(), ".py")) {
    tokens.pop_back();
  }

  // Read each path section into a variable, starting from the back of the path
  for (std::string* value : values) {
    if (!tokens.empty()) {
      *value = tokens.back();
      tokens.pop_back();
    }
  }

  return VLNV{vendor, library, name, version};
}

// This will return a path to the litex enabled python interpreter used by the
// ip catalog. This will cache the return value the first time it's not empty
std::filesystem::path IPCatalog::getPythonPath(const std::filesystem::path& envsPath) {
  static std::filesystem::path s_pythonPath{};
  if (s_pythonPath.empty()) {
#ifdef _WIN32
    std::string pythonExecName = "python.exe";
    std::filesystem::path searchPath = envsPath;
#else
    std::string pythonExecName = "python";
    std::filesystem::path searchPath = envsPath / "litex";
#endif
    searchPath = FileUtils::GetFullPath(searchPath);
    auto candidate = FileUtils::LocateFileRecursive(searchPath, pythonExecName);
    if (!candidate.empty()) {
      std::error_code ec;
      auto perms = std::filesystem::status(candidate, ec).permissions();
      bool isUsable = !ec &&
          (perms & std::filesystem::perms::owner_exec) !=
              std::filesystem::perms::none;
#ifdef __APPLE__
      // Reject Linux ELF binaries that cannot run on macOS.
      // The bundled envs/litex/ Python is built for Linux x86-64; on macOS
      // QProcess would fail with "execve: Exec format error".
      if (isUsable) {
        std::ifstream f(candidate, std::ios::binary);
        char magic[4]{};
        f.read(magic, 4);
        if (magic[0] == 0x7f && magic[1] == 'E' &&
            magic[2] == 'L' && magic[3] == 'F') {
          isUsable = false;
        }
      }
#endif
      if (isUsable) {
        s_pythonPath = candidate;
      }
    }
  }
  return s_pythonPath;
}

IPDetails FOEDAG::readIpDetails(const std::filesystem::path& path) {
  IPDetails details;
  QFile file{QString::fromStdString(path.string())};
  if (!file.open(QFile::ReadOnly)) return details;
  QString content = file.readAll();
  json object;
  try {
    object = json::parse(content.toStdString());
  } catch (std::exception& e) {
    return details;
  }

  if (object.contains("IP details")) {
    auto ip_details = object.at("IP details");
    details.name = ip_details["Name"];
    details.description = ip_details["Description"];
    details.interface_str = ip_details["Interface"];
    details.version = ip_details["Version"];
  }
  return details;
}
