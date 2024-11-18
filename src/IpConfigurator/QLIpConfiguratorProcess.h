/**
  * @file VprProcess.h
  * @author Oleksandr Pyvovarov (APivovarov@quicklogic.com or
  aleksandr.pivovarov.84@gmail.com or
  * https://github.com/w0lek)
  * @date 2024-11-13
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

#pragma once

#include <QProcess>

#include <filesystem>

namespace FOEDAG {

class QLIpConfiguratorProcess : public QProcess {
  Q_OBJECT

  const int PROCESS_FINISH_TIMOUT_MS = 5000;

  class ResultWatcher {
    public:
      bool isReady() const { return m_isReady; }

      const std::vector<std::string>& ipFiles() const { return m_ipFiles; }

      void process(QString line) {
        line = line.trimmed();

        if (line == "INFO: IPG: begin listing generated IP files...") {
          m_isStartedCollectingFiles = true;
        }
        if (line == "INFO: IPG: ...end listing generated IP files") {
          m_isReady = true;
        }

        if (line.startsWith("INFO: IPG: generated IP file:")) {
          QString filePath = line.replace(QString("INFO: IPG: generated IP file:"), QString(""));
          m_ipFiles.push_back(filePath.toStdString());
        }
      }

      void clear() {
        m_ipFiles.clear();
        m_isReady = false;
        m_isStartedCollectingFiles = false;
      }

    private:
      bool m_isReady = false;
      bool m_isStartedCollectingFiles = false;
      std::vector<std::string> m_ipFiles;
  };

 public:
  QLIpConfiguratorProcess();
  ~QLIpConfiguratorProcess();

  QString executableName() const { return m_executableName; }

  bool isRunning() const { return (state() != QProcess::NotRunning); }

  bool start();
  void stop();

signals:
  void closed();
  void error(QString title, QString msg);
  void resultReady(std::filesystem::path workPath, std::vector<std::string> files);

private:
  void stopAndWaitProcess();

  QString m_ipBuildPath;
  QString m_executableName{"ipgenerator"};
  ResultWatcher m_resultWatcher;
};

}  // namespace FOEDAG
