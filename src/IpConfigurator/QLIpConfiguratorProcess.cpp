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

#include <QStandardPaths>
#include <QDebug>

namespace FOEDAG {

QLIpConfiguratorProcess::QLIpConfiguratorProcess() {
  connect(this, &QProcess::readyReadStandardOutput, this, [this]() {
    QByteArray output = readAllStandardOutput();
    QList<QByteArray> lines = output.split('\n');
    for (const auto line: lines) {
      m_resultWatcher.process(QString(line));
    }
    if (m_resultWatcher.isReady()) {
      emit resultReady(m_resultWatcher.ipFiles());
      m_resultWatcher.clear();
    }
  });

  connect(this, &QProcess::readyReadStandardError, this, [this]() {
    QString error{QString::fromUtf8(readAllStandardError())};
    qCritical() << QString("QLIpConfiguratorProcess: %1").arg(error);
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
    return false;
  }
  m_resultWatcher.clear();

  QString cmd{"ipgenerator"};
  QList<QString> args;

  QString ipBuildPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  args << "--build_path" << ipBuildPath;
  args << "--device" << QString::fromStdString(QLDeviceManager::getInstance()->getCurrentDeviceTarget().device_variant.family);
  args << "--arch_file_path" << QString::fromStdString(QLDeviceManager::getInstance()->deviceVPRArchitectureFile().string());

  setProgram(cmd);
  setArguments(args);
  QProcess::start();
  return isRunning();
}

void QLIpConfiguratorProcess::stop() {
  stopAndWaitProcess();
}

}  // namespace FOEDAG
