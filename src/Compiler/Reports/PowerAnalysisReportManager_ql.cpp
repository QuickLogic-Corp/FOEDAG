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

#include "PowerAnalysisReportManager_ql.h"

#include <iostream>

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#include "CompilerDefines.h"
#include "DefaultTaskReport.h"
#include "TableReport.h"

namespace {
// Report strings
static constexpr const char *REPORT_NAME{"Power Analysis report"};
// static constexpr const char *MAX_LVL_STR{"Maximum logic level"};
// static constexpr const char *AVG_LVL_STR{"Average logic level"};

// // Messages regexp
// static const QRegExp VERIFIC_ERR_REGEXP{"VERIFIC-ERROR.*"};
// static const QRegExp VERIFIC_WARN_REGEXP{"VERIFIC-WARNING.*"};
// static const QRegExp VERIFIC_INFO_REGEXP{
//     "Executing synth_rs pass.*|Executing RS_DSP_MACC.*"};
}  // namespace

namespace FOEDAG {

PowerAnalysisReportManager::PowerAnalysisReportManager(const TaskManager &taskManager)
    : AbstractReportManager(taskManager) {}

QStringList PowerAnalysisReportManager::getAvailableReportIds() const {
  return {QString(REPORT_NAME)};
}


std::unique_ptr<ITaskReport> PowerAnalysisReportManager::createReport(
    const QString &reportId, const QString& profile) {
  if (!isFileParsed()) parseLogFile();

  ITaskReport::DataReports dataReports;
  
  std::unique_ptr<TableReport> power_estimate_table = std::make_unique<TableReport>(power_estimate_cols,
                                                       power_estimate_data,
                                                       QString{"Power Estimates"});
  dataReports.push_back(std::move(power_estimate_table));
#ifdef LEGACY_POWER_CALCULATOR
  std::unique_ptr<TableReport> power_debug_table = std::make_unique<TableReport>(power_debug_cols,
                                                    power_debug_data,
                                                    QString{"Power Debug Inputs"});
  dataReports.push_back(std::move(power_debug_table));
#endif
  emit reportCreated(QString(REPORT_NAME));

  return std::make_unique<DefaultTaskReport>(std::move(dataReports),
                                             "Power Analysis report");
}

void PowerAnalysisReportManager::parseLogFile() {
  clearDataProfiles();

  // read power analysis rpt
  auto logFile = createLogFile(QString(POWER_ANALYSIS_LOG));
  if (!logFile) return;

  auto fileStr = QTextStream(logFile.get()).readAll();
  logFile->close();

  QRegularExpression dynamic_power_regex{"Dynamic Power\\s*=\\s*([+-]?[0-9]*\\.[0-9]*)\\s+[^\\s]+"};
  QString dynamic_power_value;
  if (auto m = lastMatch(dynamic_power_regex, fileStr); m.hasMatch())
    dynamic_power_value = m.captured(1);

  QRegularExpression leakage_power_regex{"Leakage Power\\s*=\\s*([+-]?[0-9]*\\.[0-9]*)\\s+[^\\s]+"};
  QString leakage_power_value;
  if (auto m = lastMatch(leakage_power_regex, fileStr); m.hasMatch())
    leakage_power_value = m.captured(1);

  QRegularExpression total_power_regex{"Total Power\\s*=\\s*([+-]?[0-9]*\\.[0-9]*)\\s+[^\\s]+"};
  QString total_power_value;
  if (auto m = lastMatch(total_power_regex, fileStr); m.hasMatch())
    total_power_value = m.captured(1);


  // create power analysis report
  power_estimate_cols.clear();
  power_estimate_cols.push_back(ReportColumn{"Cateogory"});
  power_estimate_cols.push_back(ReportColumn{"Power (mW)", Qt::AlignCenter});

  power_estimate_data.clear();
  if (!dynamic_power_value.isEmpty()) {
    power_estimate_data.push_back(QStringList{"Dynamic", dynamic_power_value});
  }
  if (!leakage_power_value.isEmpty()) {
    power_estimate_data.push_back(QStringList{"Leakage",leakage_power_value});
  }
  power_estimate_data.push_back(QStringList{"Total",total_power_value});

 #ifdef LEGACY_POWER_CALCULATOR
  // read power analysis debug rpt
  auto logFile_debug = createLogFile(QString("power_analysis_debug.rpt"));
  if (!logFile_debug) return;

  auto fileStr_debug = QTextStream(logFile_debug.get()).readAll();
  logFile_debug->close();

  if(fileStr_debug.isEmpty()) return;

  // std::cout << "parsing debug report" << std::endl;

  QVector<QStringList> user_input_lines;
  // calculator_d6  : 4              [Array X]
  QRegularExpression user_inputs_regex{"(\\S+)\\s+:\\s+(\\S+)\\s+\\[(.+?)\\]"};
  {
    auto it = user_inputs_regex.globalMatch(fileStr_debug);
    while (it.hasNext()) {
      auto m = it.next();
      QStringList user_input;

      // std::cout << "1: " << m.captured(1).toStdString() << std::endl;
      // std::cout << "2: " << m.captured(2).toStdString() << std::endl;
      // std::cout << "3: "<< m.captured(3).toStdString() << std::endl;

      user_input << m.captured(1);
      user_input << m.captured(2);
      user_input << m.captured(3);

      user_input_lines.push_back(std::move(user_input));
    }
  }


  QRegularExpression user_inputs_def_regex{"(\\S+)\\s+:\\s+(\\S+)\\s+(V|MHz|%)\\s+\\[(.+?)\\]"};
  {
    auto it = user_inputs_def_regex.globalMatch(fileStr_debug);
    while (it.hasNext()) {
      auto m = it.next();
      QStringList user_input;

      // std::cout << "1: " << m.captured(1).toStdString() << std::endl;
      // std::cout << "2: " << m.captured(2).toStdString() << std::endl;
      // std::cout << "3: "<< m.captured(3).toStdString() << std::endl;

      user_input << m.captured(1);
      user_input << m.captured(2) + " " + m.captured(3);
      user_input << m.captured(4);

      user_input_lines.push_back(std::move(user_input));
    }
  }


  // create power analysis debug report
  power_debug_cols.clear();
  power_debug_cols.push_back(ReportColumn{"Spreadsheet Cell"});
  power_debug_cols.push_back(ReportColumn{"Label", Qt::AlignCenter});
  power_debug_cols.push_back(ReportColumn{"Value"});

  power_debug_data.clear();
  for (QStringList user_input : user_input_lines) {
    // std::cout << "data\n" << std::endl;
    // std::cout << user_input[0].toStdString() << std::endl;
    // std::cout << user_input[1].toStdString() << std::endl;
    // std::cout << user_input[2].toStdString() << std::endl;
    power_debug_data.push_back(std::move(user_input));
  }
 #endif // LEGACY_POWER_CALCULATOR
  setFileParsed(true);
}

QString PowerAnalysisReportManager::getTimingLogFileName() const {
  // Current power analysis log implementation doesn't contain timing info
  return {};
}

void PowerAnalysisReportManager::splitTimingData(const QString &timingStr) {
  // Current power analysis log implementation doesn't contain timing info
}
}  // namespace FOEDAG
