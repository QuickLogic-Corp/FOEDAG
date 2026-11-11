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

// Reads the first module table after "Printing statistics." in either Yosys
// layout and returns rows {"Number of <item>", value}; cell types under an item
// become {"Number of <item>:<type>", value}.
//   <= 0.66:  "   Number of cells:   62356"  then  "     $lut   26652"
//   >= 0.67:  "    26684 cells"               then  "    26661   $lut"
IDataReport::TableData SynthesisReportManager::getStatistics(
    const QString &statsStr) const {
  static const QRegularExpression oldItem{
      R"(^\s*Number of ([^:]+):\s*(\S+)\s*$)"};
  static const QRegularExpression oldChild{R"(^\s{5,}(\S+)\s+(\d+)\s*$)"};
  static const QRegularExpression newItem{R"(^\s*(\d+|-) (\S.*?)\s*$)"};
  static const QRegularExpression newChild{R"(^\s*(\d+)\s{2,}(\S.*?)\s*$)"};

  auto res = IDataReport::TableData{};
  auto parentItem = QString{};
  bool inTable = false, inRows = false;

  QString text = statsStr;
  QTextStream in(&text);
  auto line = QString{};
  while (in.readLineInto(&line)) {
    auto trimmed = line.trimmed();
    if (!inTable) {
      inTable = trimmed.startsWith("===") && trimmed.endsWith("===");
      continue;
    }
    if (trimmed.isEmpty()) {
      if (inRows) break;
      continue;
    }
    if (trimmed.startsWith('+') || trimmed.startsWith('|')) continue;
    inRows = true;

    QRegularExpressionMatch m;
    if ((m = oldItem.match(line)).hasMatch()) {
      parentItem = "Number of " + m.captured(1).trimmed();
      res.push_back({parentItem, m.captured(2)});
    } else if ((m = oldChild.match(line)).hasMatch() && !parentItem.isEmpty()) {
      res.push_back({parentItem + ":" + m.captured(1), m.captured(2)});
    } else if ((m = newChild.match(line)).hasMatch() && !parentItem.isEmpty()) {
      res.push_back({parentItem + ":" + m.captured(2), m.captured(1)});
    } else if ((m = newItem.match(line)).hasMatch()) {
      parentItem = "Number of " + m.captured(2);
      auto value = m.captured(1) == "-" ? QString{"0"} : m.captured(1);
      res.push_back({parentItem, value});
    }
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

  if (auto i = fileStr.lastIndexOf("Printing statistics."); i >= 0)
    setResourceData(getStatistics(fileStr.mid(i)));

  QRegularExpression findLvls{"DE:([^\n]+)"};
  if (auto m = lastMatch(findLvls, fileStr); m.hasMatch())
    fillLevels(m.captured(0), resourceData());

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
