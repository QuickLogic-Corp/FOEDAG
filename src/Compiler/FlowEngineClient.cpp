/*
Copyright 2026 The Foedag team

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
#include "FlowEngineClient.h"

#include <QCryptographicHash>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <fstream>

namespace fs = std::filesystem;

namespace FOEDAG {

namespace {

constexpr int DEFAULTS_VERSION = 1;
constexpr const char* SWITCH = "AURORA_INCREMENTAL";

QString toQString(const fs::path& path) {
#ifdef _WIN32
  return QString::fromStdWString(path.wstring());
#else
  return QString::fromStdString(path.string());
#endif
}

bool isFile(const fs::path& path) {
  std::error_code ec;
  return fs::is_regular_file(path, ec);
}

std::string tail(const QByteArray& bytes, int limit = 400) {
  std::string text = bytes.toStdString();
  return text.size() > static_cast<size_t>(limit) ? "..." + text.substr(text.size() - limit) : text;
}

}  // namespace

std::string FlowEngineClient::currentPlatform() {
#if defined(_WIN32)
  return "windows";
#elif defined(__APPLE__)
  return "macos";
#else
  return "linux";
#endif
}

FlowEngineClient::ModeResult FlowEngineClient::incrementalMode(
    const char* envValue, const fs::path& defaultsFile, const std::string& platform) {
  if (envValue) {
    const std::string value{envValue};
    if (value == "cpp") return {Mode::Cpp, {}};
    if (value == "engine") return {Mode::Engine, {}};
    return {Mode::Cpp, std::string(SWITCH) + "='" + value +
                           "' is not one of cpp, engine; using cpp"};
  }
  const std::string where = defaultsFile.string() + ": ";
  std::ifstream in(defaultsFile, std::ios::binary);
  if (!in) {
    return {Mode::Cpp, where + "cannot be read; using cpp"};
  }
  try {
    const nlohmann::json data = nlohmann::json::parse(in);
    if (!data.is_object() || data.value("v", 0) != DEFAULTS_VERSION) {
      return {Mode::Cpp, where + "wrong version, expected " +
                             std::to_string(DEFAULTS_VERSION) + "; using cpp"};
    }
    const std::string value = data.at(SWITCH).at(platform).get<std::string>();
    if (value == "cpp") return {Mode::Cpp, {}};
    if (value == "engine") return {Mode::Engine, {}};
    return {Mode::Cpp, where + SWITCH + "." + platform + " is '" + value + "'; using cpp"};
  } catch (const std::exception& e) {
    return {Mode::Cpp, where + e.what() + "; using cpp"};
  }
}

fs::path FlowEngineClient::findInstallRoot(const fs::path& dataPath) {
  if (dataPath.empty()) return {};
  // <install>/share/<app> in the aurora layout, else DataPath()/..; macOS keeps
  // the install under the bundle's Contents/.
  for (const fs::path& root : {dataPath / ".." / "..", dataPath / "..",
                               dataPath / ".." / ".." / "Contents"}) {
    const fs::path candidate = root.lexically_normal();
    if (isFile(candidate / "scripts" / "aurora_engine.py")) return candidate;
  }
  return {};
}

fs::path FlowEngineClient::defaultsFile(const fs::path& installRoot) {
  return installRoot / "scripts" / "flow_engine" / "data" / "defaults.json";
}

fs::path FlowEngineClient::bundledInterpreter(const fs::path& installRoot) {
  const fs::path posixPython = installRoot / "envs" / "python3.8" / "bin" / "python3.8";
  if (isFile(posixPython)) return posixPython;
  const fs::path windows = installRoot / "envs" / "python.exe";
  if (isFile(windows)) return windows;
  return {};
}

std::string FlowEngineClient::sha256File(const fs::path& path) {
  QFile file(toQString(path));
  if (!file.open(QIODevice::ReadOnly)) return {};
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!hash.addData(&file)) return {};
  return "sha256:" + hash.result().toHex().toStdString();
}

FlowEngineClient::FlowEngineClient(const fs::path& installRoot, int timeoutMs)
    : m_timeoutMs(timeoutMs) {
  if (installRoot.empty()) return;
  m_interpreter = bundledInterpreter(installRoot);
  const fs::path script = installRoot / "scripts" / "aurora_engine.py";
  if (isFile(script)) m_script = script;
#ifdef __linux__
  // _hashlib and ctypes need the bundled OS libraries; tools never inherit them.
  m_osLibs = installRoot / "envs" / "python3.8" / "lib" / "os_libs";
#endif
}

FlowEngineClient::FlowEngineClient(const fs::path& interpreter, const fs::path& script,
                                   const fs::path& osLibs, int timeoutMs)
    : m_interpreter(interpreter), m_script(script), m_osLibs(osLibs), m_timeoutMs(timeoutMs) {}

FlowEngineClient::Result FlowEngineClient::call(const std::vector<std::string>& args,
                                                const nlohmann::json* request) const {
  Result result;
  if (!usable()) {
    result.error = "no bundled interpreter or engine in the install";
    return result;
  }

  QProcess process;
  QStringList arguments{"-I", "-X", "utf8", toQString(m_script)};
  for (const std::string& arg : args) arguments << QString::fromStdString(arg);
  arguments << "--json";

  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  if (!m_osLibs.empty()) {
    const QString old = env.value("LD_LIBRARY_PATH");
    env.insert("LD_LIBRARY_PATH", old.isEmpty() ? toQString(m_osLibs)
                                                : toQString(m_osLibs) + ":" + old);
  }
  process.setProcessEnvironment(env);
  process.start(toQString(m_interpreter), arguments);
  if (!process.waitForStarted(m_timeoutMs)) {
    result.error = "cannot start " + m_interpreter.string() + ": " +
                   process.errorString().toStdString();
    return result;
  }
  if (request) {
    const std::string body = request->dump();
    process.write(body.data(), static_cast<qint64>(body.size()));
  }
  process.closeWriteChannel();
  if (!process.waitForFinished(m_timeoutMs)) {
    process.kill();
    process.waitForFinished(1000);
    result.error = "no answer within " + std::to_string(m_timeoutMs / 1000) + " s";
    return result;
  }

  const QByteArray out = process.readAllStandardOutput();
  try {
    result.doc = nlohmann::json::parse(out.constData(), out.constData() + out.size());
  } catch (const std::exception&) {
    result.error = "exit " + std::to_string(process.exitCode()) +
                   ", unreadable answer: " + tail(out + process.readAllStandardError());
    return result;
  }
  if (result.doc.is_object() && result.doc.contains("error")) {
    const nlohmann::json& err = result.doc["error"];
    result.error = err.value("code", std::string{"error"}) + ": " +
                   err.value("message", std::string{});
    return result;
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    result.error = "exit " + std::to_string(process.exitCode()) + ": " +
                   tail(process.readAllStandardError());
    return result;
  }
  result.ok = true;
  return result;
}

}  // namespace FOEDAG
