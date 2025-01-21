/**
  * @file VprProcess.cpp
  * @author Oleksandr Pyvovarov (APivovarov@quicklogic.com or
  aleksandr.pivovarov.84@gmail.com or
  * https://github.com/w0lek)
  * @date 2024-03-12
  * @copyright Copyright 2021 The Foedag team

  * GPL License

  * Copyright (c) 2021 The Open-Source FPGA Foundation

  * This program is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * (at your option) any later version.

  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.

  * You should have received a copy of the GNU General Public License
  * along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "QLIpConfiguratorProcess.h"

#include "Compiler/QLSettingsManager.h"
#include "Compiler/QLDeviceManager.h"
#include "Utils/FileUtils.h"

#include <QStandardPaths>
#include <QUuid>
#include <QDebug>

#include "nlohmann_json/json.hpp"

#include <fstream>

#ifdef _WIN32
//#define ATTACH_CONSOLE_FOR_DEBUGGING
#ifdef ATTACH_CONSOLE_FOR_DEBUGGING

#include <windows.h>
#include <cstdio>

void attachConsole() {
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
}

#endif // ATTACH_CONSOLE_FOR_DEBUGGING
#endif // _WIN32

namespace FOEDAG {

QLIpConfiguratorProcess::QLIpConfiguratorProcess() {

  // supress errors happens via docker run
  m_bypassErrors.append("libGL error: MESA-LOADER: failed to retrieve device information");
  m_bypassErrors.append("QStandardPaths: XDG_RUNTIME_DIR not set, defaulting to");

#ifdef _WIN32
#ifdef ATTACH_CONSOLE_FOR_DEBUGGING
  attachConsole();
#endif
#endif

  connect(this, &QProcess::readyReadStandardOutput, this, [this]() {
    QByteArray output = readAllStandardOutput();
    QList<QByteArray> lines = output.split('\n');
    for (const auto line: lines) {
      if (line.isEmpty()) {
        continue;
      }
      m_resultWatcher.process(QString(line));
    }
    if (m_resultWatcher.isReady()) {
      emit resultReady(m_resultWatcher.ipFiles());
      m_resultWatcher.clear();
    }
  });

  connect(this, &QProcess::readyReadStandardError, this, [this]() {
    QString msg{QString::fromUtf8(readAllStandardError())};
    bool notify = true;
    for (const QString& error: m_bypassErrors) {
      if (msg.contains(error)) {
        notify = false;
        break;
      }
    }

    if (notify) {
      emit error("IP Configurator", msg);
    }
  });

  connect(this, &QProcess::stateChanged, this, [this](QProcess::ProcessState state) {
    //qDebug() << "~~~ QProcess::ProcessState" << state;
    if (state == QProcess::NotRunning) {
      if (FileUtils::FileExists(m_ipBuildPath.toStdString())) {
        FileUtils::RmDirRecursively(m_ipBuildPath.toStdString());
      }
      // if (exitStatus() == QProcess::CrashExit) {
      //   qDebug() << "Process crashed.";
      // } else if (exitStatus() == QProcess::NormalExit) {
      //   qDebug() << "Process exited normally with code:" << exitCode();
      // }

      // Check for specific error if the process terminated unexpectedly
      // if (error() != QProcess::UnknownError) {
      //     qDebug() << "Process error:" << errorString();
      // }

      emit closed();
    }
  });
}

QLIpConfiguratorProcess::~QLIpConfiguratorProcess() { stopAndWaitProcess(); }

void QLIpConfiguratorProcess::stopAndWaitProcess() {
  m_resultWatcher.clear();
  terminate();
  if (!waitForFinished(PROCESS_FINISH_TIMOUT_MS)) {
    kill();
    waitForFinished();
  }
}

bool QLIpConfiguratorProcess::start() {
  if (isRunning()) {
    stop();
  }
  m_resultWatcher.clear();

  // check is it possible to access it in easier way, maybe already baked method exists
  // std::filesystem::path dataRoot = QLDeviceManager::getInstance()->deviceDataRootDirPath();
  // std::filesystem::path deviceFilePath = dataRoot / family / foundry / node / device / voltage_threshold / p_v_t_corner / "vpr.xml";
  // if (!std::filesystem::exists(deviceFilePath)) {
  //  deviceFilePath = dataRoot / family / foundry / node / device / voltage_threshold / p_v_t_corner / "vpr.xml.en";
  // }

  // QString randomFolderName = QUuid::createUuid().toString(QUuid::Id128).remove('{').remove('}');

  // m_ipBuildPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/" + randomFolderName;
  // FileUtils::MkDirs(m_ipBuildPath.toStdString());

  QList<QString> args;
  args << "--build_path" << m_ipBuildPath;
  // args << "--arch_file_path" << QString::fromStdString(deviceFilePath.string());

  //qDebug() << "~~~run following command: " << m_executableName << " " << args.join(" ");
  setProgram(m_executableName);
  setArguments(args);
  QProcess::start();
  return isRunning();
}

void QLIpConfiguratorProcess::stop() {
  if (isRunning()) {
    stopAndWaitProcess();
  }
}

}  // namespace FOEDAG
