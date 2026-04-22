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

#include "SynthesisReportManager.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#include "CompilerDefines.h"
#include "DefaultTaskReport.h"
#include "TableReport.h"

namespace {
// Report strings
static constexpr const char *REPORT_NAME{"Synthesis - Report Statistics"};
static constexpr const char *MAX_LVL_STR{"Maximum logic level"};
static constexpr const char *AVG_LVL_STR{"Average logic level"};

// Messages regexp
static const QRegularExpression VERIFIC_ERR_REGEXP{"VERIFIC-ERROR.*"};
static const QRegularExpression VERIFIC_WARN_REGEXP{"VERIFIC-WARNING.*"};
static const QRegularExpression VERIFIC_INFO_REGEXP{
    "Executing synth_rs pass.*|Executing RS_DSP_MACC.*"};
}  // namespace

namespace FOEDAG {

SynthesisReportManager::SynthesisReportManager(const TaskManager &taskManager)
    : AbstractReportManager(taskManager) {}

QStringList SynthesisReportManager::getAvailableReportIds() const {
  return {QString(REPORT_NAME)};
}

IDataReport::TableData SynthesisReportManager::getStatistics(
    const QString &statsStr) const {
  auto res = IDataReport::TableData{};

  auto line = QString{};        // Unmodified line from the log file
  auto dataLine = QString{};    // Simplified line
  auto parentItem = QString{};  // Parent item for lines starting with tab

  QRegularExpression statTable{
      "Number.*"};  // Drop the beginning and start with stats
  auto statTableMatch = statTable.match(statsStr);
  if (!statTableMatch.hasMatch()) return res;

  QTextStream in(statTableMatch.captured(0).toLatin1());

  while (in.readLineInto(&line)) {
    if (line.startsWith("     ") && parentItem.isEmpty())
      parentItem = dataLine.left(dataLine.indexOf(':'));

    dataLine = line.simplified();
    if (dataLine.isEmpty()) break;

    auto reportLine = QStringList{};
    auto lineStrs = dataLine.contains(":") ? dataLine.split(":")
                                           : dataLine.split(QChar::Space);

    for (auto i = 0; i < lineStrs.size(); ++i) {
      auto lineStr = lineStrs[i];
      // The first string represents statistic name. Parent item should only be
      // added there.
      if (!i && !parentItem.isEmpty())
        lineStr = QString("%1:%2").arg(parentItem, lineStr);
      reportLine << lineStr;
    }

    res.push_back(std::move(reportLine));
  }

  return res;
}

void SynthesisReportManager::fillLevels(const QString &line,
                                        IDataReport::TableData &stats) const {
  const QRegularExpression findLvls{
      "^DE:.*Max Lvl =\\s*(([0-9]*[.])?[0-9]+)\\s*Avg Lvl "
      "=\\s*(([0-9]*[.])?[0-9]+)"};

  auto match = findLvls.match(line);
  if (match.hasMatch()) {
    stats.push_back({MAX_LVL_STR, match.captured(1)});
    stats.push_back({AVG_LVL_STR, match.captured(3)});
  }
}

std::unique_ptr<ITaskReport> SynthesisReportManager::createReport(
    const QString &reportId, const QString& profile) {
  if (!isFileParsed()) parseLogFile();

  emit reportCreated(QString(REPORT_NAME));

  IDataReport::ColumnValues cols;
  cols.push_back(ReportColumn{"Statistics"});
  cols.push_back(ReportColumn{"Value", Qt::AlignCenter});

  ITaskReport::DataReports dataReports;
  dataReports.push_back(
      std::make_unique<TableReport>(cols, resourceData(), QString{"Statistics"}));
  return std::make_unique<DefaultTaskReport>(std::move(dataReports),
                                             reportId);
}

void SynthesisReportManager::parseLogFile() {
  clearDataProfiles();

  auto logFile = createLogFile(QString(SYNTHESIS_LOG));
  if (!logFile) return;

  auto fileStr = QTextStream(logFile.get()).readAll();
  logFile->close();

  QRegularExpression findStats{"Printing statistics.*\n\n===.*===\n\n.*[^\n{2}]+"};

  {
    auto it = findStats.globalMatch(fileStr);
    QRegularExpressionMatch lastMatch;
    while (it.hasNext()) lastMatch = it.next();
    if (lastMatch.hasMatch())
      setResourceData(getStatistics(lastMatch.captured(0)));
  }

  QRegularExpression findLvls{"DE:([^\n]+)"};
  {
    auto it = findLvls.globalMatch(fileStr);
    QRegularExpressionMatch lastMatch;
    while (it.hasNext()) lastMatch = it.next();
    if (lastMatch.hasMatch())
      fillLevels(lastMatch.captured(0), resourceData());
  }

  auto line = QString{};
  QTextStream in(fileStr.toLatin1());
  auto lineNr = 0;
  AbstractReportManager::MessagesLines warnings, errors;
  auto fillErrorsWarnings = [&warnings, &errors, this]() {
    if (!warnings.empty()) {
      auto warningsItem =
          createWarningErrorItem(MessageSeverity::WARNING_MESSAGE, warnings);
      messages().insert(warningsItem.m_lineNr, warningsItem);
    }
    if (!errors.empty()) {
      auto errorsItem =
          createWarningErrorItem(MessageSeverity::ERROR_MESSAGE, errors);
      messages().insert(errorsItem.m_lineNr, errorsItem);
    }
  };

  while (in.readLineInto(&line)) {
    if (auto m = VERIFIC_INFO_REGEXP.match(line); m.hasMatch()) {
      messages().insert(lineNr,
                        TaskMessage{lineNr,
                                    MessageSeverity::INFO_MESSAGE,
                                    m.captured(0).simplified(),
                                    {}});
      fillErrorsWarnings();
    } else if (auto m = VERIFIC_ERR_REGEXP.match(line); m.hasMatch()) {
      errors.emplace(lineNr, m.captured(0).simplified());
      if (!warnings.empty()) {
        auto warningsItem =
            createWarningErrorItem(MessageSeverity::WARNING_MESSAGE, warnings);
        messages().insert(warningsItem.m_lineNr, warningsItem);
      }
    } else if (auto m = VERIFIC_WARN_REGEXP.match(line); m.hasMatch()) {
      warnings.emplace(lineNr, m.captured(0).simplified());

      if (!errors.empty()) {
        auto errorsItem =
            createWarningErrorItem(MessageSeverity::ERROR_MESSAGE, errors);
        messages().insert(errorsItem.m_lineNr, errorsItem);
      }
    }
    ++lineNr;
  }

  fillErrorsWarnings();

  setFileParsed(true);
}

QString SynthesisReportManager::getTimingLogFileName() const {
  // Current synthesis log implementation doesn't contain timing info
  return {};
}

void SynthesisReportManager::splitTimingData(const QString &timingStr) {
  // Current synthesis log implementation doesn't contain timing info
}
}  // namespace FOEDAG
