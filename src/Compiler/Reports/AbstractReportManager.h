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

#include <QObject>
#include <QVector>
#include <map>

#include "IDataReport.h"
#include "ITaskReportManager.h"
#include "DataProfiles.h"

class QFile;
class QRegExp;
class QTextStream;

namespace FOEDAG {

class TaskManager;

/* Abstract implementation holding common logic for report managers.
 *
 */
class AbstractReportManager : public QObject, public ITaskReportManager {
  Q_OBJECT

  struct DataProfile {
    IDataReport::TableData resourceData;
    IDataReport::TableData timingData;
    QVector<QPair<QString, IDataReport::TableData>> histograms;
    Messages messages;
  };
  
 public:
  AbstractReportManager(const TaskManager &taskManager);

 protected:
  // Launches file parsing, if needed. Returns messages.
  const Messages &getMessages() override;
  // Parses corresponding log file
  virtual void parseLogFile() = 0;
  // Getter for timing report file name
  virtual QString getTimingLogFileName() const = 0;
  // Returns true if given line holds statistical timing info.
  virtual bool isStatisticalTimingLine(const QString &line);
  // Returns true if given line holds is a header of statistical histogram.
  virtual bool isStatisticalTimingHistogram(const QString &line);
  // Parses in stream line by line till empty one occurs and creates table data.
  // Fills parsed data into 'm_resourceColumns' and 'm_resourceData'
  void parseResourceUsage(QTextStream &in, int &lineNr);

  // Creates and opens log file instance. returns nullptr if file doesn't exist.
  std::unique_ptr<QFile> createLogFile(const QString &fileName) const;

  using SectionKeys = QVector<QRegExp>;
  int parseErrorWarningSection(QTextStream &in, int lineNr,
                               const QString &sectionLine, SectionKeys keys,
                               bool stopEmptyLine = false);

  IDataReport::TableData parseCircuitStats(QTextStream &in, int &lineNr);

  using MessagesLines = std::map<int, QString>;
  // Creates parent item for either warnings or messages. Clears msgs
  // afterwards.
  TaskMessage createWarningErrorItem(MessageSeverity severity,
                                     MessagesLines &msgs) const;

  // Fills the values from given 'timingData' to m_timingData.
  void fillTimingData(const QStringList &timingData);
  // Create the file with 'timingData' content.
  void createTimingDataFile(const QStringList &timingData);
  // Splits histogram lines into table data till reaching empty line.
  IDataReport::TableData parseHistogram(QTextStream &in, int &lineNr);

  // Timing data is task specific and can't be split on generic level
  virtual void splitTimingData(const QString &timingStr) = 0;

  // Keyword to recognize the start of resource usage section
  static const QRegExp FIND_RESOURCES;
  static const QRegExp FIND_CIRCUIT_STAT;

  bool isFileParsed() const;
  void setFileParsed(bool parsed);

 signals:
  void reportCreated(QString reportName);

 protected:
  IDataReport::ColumnValues m_resourceColumns;
  IDataReport::ColumnValues m_timingColumns;
  IDataReport::ColumnValues m_histogramColumns;

// new interface
  void setActiveProfile(const std::string& profile) {
    m_dataProfiles.setCurrentKey(profile);
  }

  void setResourceData(const IDataReport::TableData& resourceData) {
    m_dataProfiles.current()->resourceData = resourceData;
  }

  IDataReport::TableData& resourceData() {
    return m_dataProfiles.current()->resourceData;
  }
  IDataReport::TableData& resourceData(const std::string& profile) {
    return m_dataProfiles.get(profile)->resourceData;
  }
  IDataReport::TableData& timingData() {
    return m_dataProfiles.current()->timingData;
  }
  IDataReport::TableData& timingData(const std::string& profile) {
    return m_dataProfiles.get(profile)->timingData;
  }
  QVector<QPair<QString, IDataReport::TableData>>& histograms() {
    return m_dataProfiles.current()->histograms;
  }
  QVector<QPair<QString, IDataReport::TableData>>& histograms(const std::string& profile) {
    return m_dataProfiles.get(profile)->histograms;
  }
  Messages& messages() {
    return m_dataProfiles.current()->messages;
  }
  std::vector<std::string> profiles() const { return m_dataProfiles.keys(); }
  //

 private:
  bool m_fileParsed{false};

  DataProfiles<DataProfile> m_dataProfiles;
};

}  // namespace FOEDAG
