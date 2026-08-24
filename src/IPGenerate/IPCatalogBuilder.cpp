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

#include "IPGenerate/IPCatalogBuilder.h"

#include <cctype>

#include <sys/stat.h>
#include <sys/types.h>

#include <QDebug>
#include <QProcess>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <regex>
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

void buildMockUpIPDef(IPCatalog* catalog) {
  std::vector<Connector*> connections;
  std::vector<Value*> parameters;
  Constant* lrange = new Constant(0);
  Parameter* rrange = new Parameter("Width", 0);
  Range range(lrange, rrange);
  Port* port = new Port("clk", Port::Direction::Input, Port::Function::Clock,
                        Port::Polarity::High, range);
  connections.push_back(port);
  IPDefinition* def = new IPDefinition(IPDefinition::IPType::Other, "MOCK_IP",
                                       "MOCK_IP_wrapper", "path_to_nowhere",
                                       connections, parameters);
  catalog->addIP(def);
  // catalog->WriteCatalog(std::cout);
}

bool IPCatalogBuilder::buildLiteXCatalog(
    IPCatalog* catalog, const std::filesystem::path& litexIPgenPath,
    bool namesOnly) {
  bool result = true;
  if (FileUtils::FileExists(litexIPgenPath)) {
    int foundCount = 0;
    std::filesystem::path execPath = litexIPgenPath;
    if (!std::filesystem::is_directory(execPath)) {
      execPath = execPath.parent_path();
    }
    m_compiler->Message("IP Catalog, browsing directory for IP generator(s): " +
                        execPath.string());
    for (const std::filesystem::path& entry :
         std::filesystem::recursive_directory_iterator(
             execPath,
             std::filesystem::directory_options::follow_directory_symlink)) {
      const std::string& exec_name = entry.string();
      if (exec_name.find("__init__.py") != std::string::npos) continue;
      if (exec_name.find("_gen.py") != std::string::npos) {
        foundCount++;
        m_compiler->GetIPGenerator()->shareContext();
        // Read the availability manifest here, in the discovery walk, where
        // the generator path is already in hand: this covers both the
        // names-only (GUI) path and the full path with no extra traversal and
        // without spawning python.
        const IPAvailability availability = readIPManifest(entry);
        bool res = namesOnly ? buildLiteXIPFromGeneratorInternal(catalog, entry)
                             : buildLiteXIPFromGenerator(catalog, entry);
        if (res == false) {
          result = false;
        }
        if (IPDefinition* def =
                catalog->Definition(ipNameFromGeneratorPath(entry))) {
          def->Availability(availability);
        }
      }
    }
    std::string msg =
        std::string("IP Catalog, found ") + std::to_string(foundCount) + " IPs";
    m_compiler->Message(msg);
  } else {
    result = false;
    m_compiler->ErrorMessage("IP Catalog, directory does not exist: " +
                             litexIPgenPath.string());
  }
  return result;
}

static std::string& rtrim(std::string& str, char c) {
  auto it1 = std::find_if(str.rbegin(), str.rend(),
                          [c](char ch) { return (ch == c); });
  if (it1 != str.rend()) str.erase(it1.base() - 1, str.end());
  return str;
}

std::string IPCatalogBuilder::ipNameFromGeneratorPath(
    const std::filesystem::path& pythonConverterScript) {
  std::filesystem::path basepath = FileUtils::Basename(pythonConverterScript);
  std::string basename = basepath.string();
  std::string IPName = rtrim(basename, '.');

  // Remove _gen from IPName
  static const std::string suffix = "_gen";
  if (StringUtils::endsWith(IPName, suffix)) {
    IPName.erase(IPName.length() - suffix.length());
  }

  // Add version number to IPName
  auto info = FOEDAG::getIpInfoFromPath(pythonConverterScript);
  IPName += "_" + info.version;
  return IPName;
}

// True for "<digits>_<digits>", the form QLDeviceManager::deviceDSPVersion()
// produces and the only form the gate can compare against. A plausible typo -
// "2.0", copying the DSPV1.1 spelling from DSP_TYPE - would otherwise be
// accepted verbatim and then match no device at all, hiding the IP from the
// very fabric it targets. That is fail-closed, which the model forbids.
static bool isDspVersionWellFormed(const std::string& value) {
  const size_t separator = value.find('_');
  if (separator == std::string::npos || separator == 0 ||
      separator + 1 == value.size())
    return false;
  if (value.find('_', separator + 1) != std::string::npos) return false;
  for (size_t i = 0; i < value.size(); ++i) {
    if (i == separator) continue;
    if (!std::isdigit(static_cast<unsigned char>(value[i]))) return false;
  }
  return true;
}

// Returns obj[key] when it is present and a string, "" otherwise. Manifests are
// hand written, so a wrong type must degrade to "unset", never throw.
static std::string manifestString(const json& obj, const char* key) {
  if (obj.contains(key) && obj[key].is_string())
    return obj[key].get<std::string>();
  return {};
}

IPAvailability IPCatalogBuilder::readIPManifest(
    const std::filesystem::path& pythonConverterScript) {
  IPAvailability availability{};

  const std::filesystem::path manifestPath =
      pythonConverterScript.parent_path() / "ip_manifest.json";
  if (!FileUtils::FileExists(manifestPath)) {
    // Defaulting when the file is absent is a robustness rule for third-party
    // and field installs - it is NOT the shipping configuration. Every IP we
    // ship is expected to carry a manifest, so `ip_catalog -all` reports a
    // defaulted IP differently from one the file declares as production; that
    // is what makes a bad catalog pin visible instead of silently ungating.
    return availability;
  }
  availability.manifestPresent = true;

  // Every problem is printed and every problem is carried in the reason
  // string, so nothing about a half-usable manifest is invisible.
  auto warn = [&](const std::string& text) {
    if (!availability.manifestWarning.empty())
      availability.manifestWarning += " ";
    availability.manifestWarning += text;
    m_compiler->WarningMessage("IP Catalog, " + manifestPath.string() + ": " +
                               text);
  };

  json manifest;
  try {
    std::ifstream ifs(manifestPath.string());
    manifest = json::parse(ifs);
  } catch (const std::exception& e) {
    // Fail open on visibility, never on the fabric gate (IPAvailability
    // invariant 3): we cannot see whether this IP declared a requires block,
    // so the requirement is recorded as unverifiable, not as absent.
    availability.requirementUnverifiable = true;
    warn("Manifest could not be parsed (" + std::string(e.what()) +
         "); its fabric requirement cannot be read.");
    return availability;
  }

  if (!manifest.is_object()) {
    availability.requirementUnverifiable = true;
    warn(
        "Manifest is not a JSON object; its fabric requirement cannot be "
        "read.");
    return availability;
  }

  // Forward compatibility: a newer schema is read for the fields we do know
  // and the rest is ignored. Reported once per catalog walk, not per IP.
  if (manifest.contains("schema")) {
    if (!manifest["schema"].is_number_integer()) {
      warn("Manifest \"schema\" is not an integer; ignored.");
    } else {
      const int schema = manifest["schema"].get<int>();
      if (schema > kIPManifestSchema && !m_newerSchemaReported) {
        m_newerSchemaReported = true;
        m_compiler->Message(
            "IP Catalog, ip_manifest.json declares schema " +
            std::to_string(schema) + " but this build understands schema " +
            std::to_string(kIPManifestSchema) +
            "; known fields are honoured and the rest ignored");
      }
    }
  }

  const std::string maturity = manifestString(manifest, "maturity");
  if (maturity == "preview") {
    availability.maturity = IPAvailability::Maturity::Preview;
  } else if (!maturity.empty() && maturity != "production") {
    warn("Manifest declares unknown maturity \"" + maturity +
         "\"; treated as production.");
  }
  availability.maturityNote = manifestString(manifest, "maturity_note");

  // "requires" is a CLOSED set, unlike the top level: an unrecognised key
  // there is most likely a misspelt requirement, and ignoring it would drop a
  // fabric gate on the floor. Anything we cannot read as written makes the
  // requirement unverifiable rather than absent.
  if (manifest.contains("requires")) {
    const json& requiresNode = manifest["requires"];
    if (!requiresNode.is_object()) {
      availability.requirementUnverifiable = true;
      warn(
          "Manifest \"requires\" is not an object; the fabric requirement "
          "cannot be read.");
    } else {
      for (const auto& item : requiresNode.items()) {
        if (item.key() == "dsp_version") {
          if (!item.value().is_string()) {
            availability.requirementUnverifiable = true;
            warn(
                "Manifest \"requires.dsp_version\" is not a string; the fabric "
                "requirement cannot be read.");
          } else {
            const std::string value = item.value().get<std::string>();
            if (isDspVersionWellFormed(value)) {
              availability.requiredDspVersion = value;
            } else {
              // Do not gate on a value we cannot compare: it would match no
              // device and hide the IP everywhere, including on its own.
              availability.requirementUnverifiable = true;
              warn("Manifest \"requires.dsp_version\" value \"" + value +
                   "\" is not of the form <major>_<minor>; the fabric "
                   "requirement cannot be read.");
            }
          }
        } else {
          availability.requirementUnverifiable = true;
          warn("Manifest \"requires\" contains unknown key \"" + item.key() +
               "\"; the fabric requirement cannot be read.");
        }
      }
    }
  }

  return availability;
}

std::vector<std::string> JsonArrayToStringVector(
    const json& jsonArray, bool removeOuterQuotes = true) {
  std::vector<std::string> vals{};
  for (auto& item : jsonArray.items()) {
    // Generically convert the value to a string
    std::ostringstream stream;
    stream << item.value();
    std::string val(stream.str());

    // if this is wrapped in quotes
    if (removeOuterQuotes && val.front() == '"' && val.back() == '"') {
      // Remove first and last chars
      val.erase(0, 1);
      val.pop_back();
    }

    // add value to the list
    vals.push_back(val);
  }

  return vals;
}

bool IPCatalogBuilder::buildLiteXIPFromGenerator(
    IPCatalog* catalog, const std::filesystem::path& pythonConverterScript) {
  // Find path to litex enabled python interpreter
  std::filesystem::path pythonPath = IPCatalog::getPythonPath(m_compiler->GetIPGenerator()->EnvsPath());
  if (pythonPath.empty()) {
    std::filesystem::path python3Path = FileUtils::LocateExecFile("python3");
    if (python3Path.empty()) {
      m_compiler->ErrorMessage(
          "IP Catalog, unable to find python interpreter in local "
          "environment, trying to use system copy 'python3'. Some IP Catalog "
          "features might not work with this "
          "interpreter.\n");

      // don't specify a path and hope the system finds something in its path
      pythonPath = "python3";
    } else {
      pythonPath = python3Path;
      m_compiler->ErrorMessage(
          "IP Catalog, unable to find python interpreter in local "
          "environment, using system copy '" +
          python3Path.string() +
          "'. Some IP Catalog features might not work with this "
          "interpreter.\n");
    }
  }

  std::ostringstream help;
  std::string command = pythonPath.string() + " " +
                        pythonConverterScript.string() + " --json_template";
  StringVector args{pythonConverterScript.string(), "--json_template"};
  if (FileUtils::ExecuteSystemCommand(pythonPath.string(), args, &help, m_compiler->GetIPGenerator()->environment()).code) {
    m_compiler->ErrorMessage("IP Catalog, no IP information for " +
                             pythonConverterScript.string() + "\n" +
                             help.str());
    return false;
  }

  return buildLiteXIPFromJson(catalog, pythonConverterScript, help.str(),
                              command);
}

bool IPCatalogBuilder::buildLiteXIPFromJson(
    IPCatalog* catalog, const std::filesystem::path& pythonConverterScript,
    const std::string& jsonStr, const std::string& command) {
  // Treat command's output as json and parse it
  std::stringstream buffer;
  buffer << jsonStr;
  json jopts;
  try {
    jopts = json::parse(buffer);
  } catch (json::parse_error& e) {
    std::string msg = "Json Parse Error: " + std::string(e.what()) + "\n" +
                      "\tfilePath: " + pythonConverterScript.string() + "\n" +
                      "\tgenCmd: " + command + "\n" +
                      "\treturned json: " + buffer.str();
    m_compiler->ErrorMessage(msg);
    return false;
  }

  // Error out if empty json was returned
  if (jopts.empty()) {
    m_compiler->ErrorMessage(
        "IP Catalog, failed to load IP because the following command returned "
        "an empty json object: " +
        command + "\n");
    return false;
  }

  const std::string IPName = ipNameFromGeneratorPath(pythonConverterScript);

  std::vector<Value*> parameters;
  std::vector<Connector*> connections;

  auto params = jopts.value("parameters", json::array());
  for (const auto& param : params) {
    auto paramName = param.value("parameter", std::string{});
    auto title = param.value("title", paramName);
    auto options = param.value("options", json::array());
    auto range = param.value("range", json::array());
    auto type = param.value("type", std::string{});
    auto description = param.value("description", std::string{});
    auto disable = param.value("disable", std::string{});

    std::string defaultVal{};
    try {
      defaultVal = param.value("default", std::string{});
    } catch (json::type_error& error) {
      // Default value has potential to be passed non-string values so we'll
      // check for it here
      std::string msg =
          "IP Catalog, \"default\" key expects a string value. Default param "
          "set to \"\". Json error: " +
          std::string(error.what());
      m_compiler->ErrorMessage(msg);
    }

    // Dependency is currently a single variable field, but it sounds like there
    // could be multiple in the future. To support future scenarios we'll check
    // for strings and arrays and store the values accordingly
    auto dependency = param.value("dependency", json::array());
    std::vector<std::string> deps{};
    if (dependency.is_string()) {
      deps.push_back(dependency.get<std::string>());
    } else if (dependency.is_array()) {
      for (const auto& dep : dependency) {
        deps.push_back(dep.get<std::string>());
      }
    }

    IPParameter::ParamType paramType;
    type = StringUtils::toLower(type);
    if (type == "int") {
      paramType = IPParameter::ParamType::Int;
    } else if (type == "float") {
      paramType = IPParameter::ParamType::Float;
    } else if (type == "bool") {
      paramType = IPParameter::ParamType::Bool;
    } else if (type == "filepath") {
      paramType = IPParameter::ParamType::FilePath;
    } else {
      paramType = IPParameter::ParamType::String;
    }

    IPParameter* parameter =
        new IPParameter(paramName, title, defaultVal, paramType);
    parameter->SetOptions(JsonArrayToStringVector(options));
    parameter->SetDependencies(JsonArrayToStringVector(deps));
    parameter->SetRange(JsonArrayToStringVector(range));
    parameter->SetDescription(description);
    parameter->SetDisable(disable);

    parameters.push_back(parameter);
  }

  // get default build_name which is used during ip configuration
  std::string build_name = jopts.value("build_name", std::string{});

  auto def = catalog->Definition(IPName);
  if (def) {
    def->apply(IPDefinition::IPType::LiteXGenerator, IPName, build_name,
               pythonConverterScript, connections, parameters);
    def->Valid(true);
  } else {
    IPDefinition* def = new IPDefinition(
        IPDefinition::IPType::LiteXGenerator, IPName, build_name,
        pythonConverterScript, connections, parameters);
    catalog->addIP(def);
  }
  return true;
}

bool IPCatalogBuilder::buildLiteXIPFromGeneratorInternal(
    IPCatalog* catalog, const std::filesystem::path& pythonConverterScript) {
  bool result = true;

  std::ostringstream help;
  std::string command;

  const std::string IPName = ipNameFromGeneratorPath(pythonConverterScript);

  IPDefinition* def =
      new IPDefinition(IPDefinition::IPType::LiteXGenerator, IPName,
                       std::string{}, pythonConverterScript, {}, {});
  def->Valid(false);
  catalog->addIP(def);
  return result;
}
