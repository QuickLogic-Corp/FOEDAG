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
#pragma once

#include "AbstractReportManager.h"

#include <map>
#include <QDebug>

namespace FOEDAG {

class Compiler;

/* Report manager for timing analysis. It works with 'timing_analysis.rpt' log
 * file. As there are two timing engines supported: Tatum (Default) and OpenSTA,
 * this manager is responsible for recognizing messages of both.
 */
class TimingAnalysisReportManager final : public AbstractReportManager {
  struct DataProfile {
    IDataReport::TableData circuitData;
  };
  using DataProfilePtr = std::shared_ptr<DataProfile>;

  class DataProfiles {
  public:
    DataProfiles() {
      create(); // create default
    }
    DataProfilePtr getOrCreate(const std::string& key) {
      if (!contain(key)) {
        create(key);
      }
      return m_data[key];

      m_currentKey = key;
      DataProfilePtr profile = std::make_shared<DataProfile>();
      m_data[key] = profile;
      return profile;
    }

    bool contain(const std::string& key) {
      return m_data.find(key) != m_data.end();
    }

    void create(const std::string& key = "") {
      qDebug() << "~~~ create new key" << QString::fromStdString(key);
      m_data[key] = std::make_shared<DataProfile>();
      m_currentKey = key;
    }

    void setCurrentKey(const std::string& key) {
      if (m_data.find(key) != m_data.end()) {
        m_currentKey = key;
      } else {
        qDebug() << "~~~ attempt to use not registered key" << QString::fromStdString(key);
      }
    }

    DataProfilePtr current() const {
      return m_data.at(m_currentKey);
    }

  private:
    std::string m_currentKey;
    std::map<std::string, DataProfilePtr> m_data;
  };

 public:
  TimingAnalysisReportManager(const TaskManager &taskManager,
                              Compiler *compiler);

 private:
  QStringList getAvailableReportIds() const override;
  std::unique_ptr<ITaskReport> createReport(const QString &reportId) override;
  QString getTimingLogFileName() const override;
  bool isStatisticalTimingLine(const QString &line) override;
  bool isStatisticalTimingHistogram(const QString &line) override;
  void splitTimingData(const QString &timingStr) override;
  void parseLogFile() override;
  void parseLogFileHelper(const QString& logFileName);
  
  void parseOpenSTALog(const QString& logFileName);
  IDataReport::TableData parseOpenSTATimingTable(QTextStream &in,
                                                 int &lineNr) const;

  // interface
  void setCircuitData(const IDataReport::TableData& data, const std::string& key = "") {
    m_dataProfiles.getOrCreate(key)->circuitData = data;
  }

  IDataReport::TableData& circuitData() const {
    return m_dataProfiles.current()->circuitData;
  }
  //

  DataProfiles m_dataProfiles;

  SectionKeys m_createDeviceKeys;

  IDataReport::ColumnValues m_circuitColumns;
  IDataReport::ColumnValues m_openSTATimingColumns;

  Compiler *m_compiler;
};

}  // namespace FOEDAG
