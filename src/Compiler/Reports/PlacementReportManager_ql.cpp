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

#include "PlacementReportManager.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#include <fstream>

#include "CompilerDefines.h"
#include "DefaultTaskReport.h"
#include "NewProject/ProjectManager/project.h"
#include "TableReport.h"
#include "nlohmann_json/json.hpp"

namespace {
static constexpr const char *RESOURCE_REPORT_NAME{
    "Post placement - Report Resource Utilization"};
static constexpr const char *TIMING_REPORT_NAME{
    "Post placement - Report Static Timing"};
// Clock-aware placement: listed only while the driver's report data exists in the
// project, so the report disappears with Placement Clean.
static constexpr const char *CLOCK_REGIONS_REPORT_NAME{
    "Post placement - Clock Regions"};
static const QString CLOCK_REGIONS_JSON{"clock_regions/clock_regions.json"};

QString clockRegionsJsonPath() {
  return QString("%1/%2").arg(FOEDAG::Project::Instance()->projectPath(),
                              CLOCK_REGIONS_JSON);
}

struct ClockRegionTable {
  QString name;
  FOEDAG::IDataReport::ColumnValues columns;
  FOEDAG::IDataReport::TableData rows;
};

QString jsonText(const nlohmann::json &value) {
  if (value.is_string()) return QString::fromStdString(value.get<std::string>());
  if (value.is_null()) return QString{};
  return QString::fromStdString(value.dump());
}

// The region table of clock_regions.json: one row per region, the clocks with
// their cluster counts in the last column.
ClockRegionTable regionTable(const QString &name, const nlohmann::json &rows) {
  ClockRegionTable table{name,
                         {{"Region"}, {"Limit"}, {"Clocks"}, {"Status"},
                          {"Clocks (clusters)"}},
                         {}};
  if (!rows.is_array()) return table;
  for (const auto &row : rows) {
    if (!row.is_object()) continue;
    QStringList clocks;
    if (row.contains("clusters") && row["clusters"].is_object()) {
      for (const auto &[clock, count] : row["clusters"].items()) {
        clocks << QString("%1(%2)").arg(QString::fromStdString(clock),
                                        jsonText(count));
      }
    }
    table.rows.push_back({jsonText(row.value("region", nlohmann::json{})),
                          jsonText(row.value("limit", nlohmann::json{})),
                          jsonText(row.value("clocks", nlohmann::json{})),
                          jsonText(row.value("status", nlohmann::json{})),
                          clocks.join(", ")});
  }
  return table;
}

// The three tables of the Clock Regions report, from clock_regions.json; one
// table with the problem when the file cannot be read.
QVector<ClockRegionTable> clockRegionTables(const QString &path) {
  const QVector<ClockRegionTable> unreadable{
      {"Clock Regions",
       {{"Item"}, {"Value"}},
       {{"Report", QString("%1 could not be read").arg(path)}}}};
  std::ifstream in(path.toStdString());
  const nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
  if (!doc.is_object() || !doc.value("legal", nlohmann::json{}).is_boolean() ||
      !doc.value("regions", nlohmann::json{}).is_object()) {
    return unreadable;
  }
  const bool legal = doc["legal"].get<bool>();
  ClockRegionTable summary{"Summary", {{"Item"}, {"Value"}}, {}};
  summary.rows.push_back(
      {"Verdict", legal ? "LEGAL: every clock region within its limit"
                        : "NOT LEGAL: no legal placement was found"});
  summary.rows.push_back({"Placements run", jsonText(doc.value("placements", nlohmann::json{}))});
  summary.rows.push_back({"Clusters moved", jsonText(doc.value("moved_clusters", nlohmann::json{}))});
  if (doc.contains("plan") && doc["plan"].is_object()) {
    summary.rows.push_back({"Planning", jsonText(doc["plan"].value("summary", nlohmann::json{}))});
  }
  if (!legal) {
    summary.rows.push_back({"Reason", jsonText(doc.value("reason", nlohmann::json{}))});
  }
  if (doc.contains("warnings") && doc["warnings"].is_array()) {
    for (const auto &warning : doc["warnings"]) {
      summary.rows.push_back({"Warning", jsonText(warning)});
    }
  }
  const nlohmann::json regions = doc.value("regions", nlohmann::json::object());
  return {summary,
          regionTable("Regions, final placement", regions.value("final", nlohmann::json{})),
          regionTable("Regions, first placement", regions.value("first", nlohmann::json{}))};
}

// Messages regexps
static const QRegularExpression FIND_PLACEMENT_TIMINGS{
    "Placement estimated.*(Slack|MHz).*"};
// Anchors replace QRegExp::exactMatch (removed in Qt6): matches full line only.
static const QRegularExpression LOAD_PACKING_REGEXP{"^# Load packing$"};
static const QRegularExpression DEVICE_UTIL_REGEXP{"Device Utilization.*"};
static const QRegularExpression TILEABLE_GRAPH_REGEXP{
    "Build tileable routing resource graph"};
static const QRegularExpression INIT_PLACEMENT_HISTOGRAM_REGEXP{
    "Initial placement estimated setup slack histogram"};
static const QRegularExpression PLACEMENT_HISTOGRAM_REGEXP{
    "Placement estimated setup slack histogram"};
static const QRegularExpression PLACEMENT_RESOURCE_REGEXP{"Placement resource usage"};

static const QString CREATE_DEVICE_SECTION{"# Create Device"};
static const QString PLACEMENT_SECTION{"# Placement"};

static const QRegularExpression FIND_STAT_TIMING{
    "([-]?(([0-9]*[.])?[0-9]+) (ns?(?=,)|.*|MHz))"};

static const QStringList TIMING_FIELDS{"Critical path delay (least slack)",
                                       "FMax",
                                       "Setup WNS",
                                       "Setup TNS",
                                       "Intra-domain period",
                                       "Fanout-weighted intra-domain period"};
}  // namespace

namespace FOEDAG {

PlacementReportManager::PlacementReportManager(const TaskManager &taskManager)
    : AbstractReportManager(taskManager) {
  m_createDeviceKeys = {AbstractReportManager::FIND_RESOURCES,
                        DEVICE_UTIL_REGEXP, TILEABLE_GRAPH_REGEXP};

  m_placementKeys = {INIT_PLACEMENT_HISTOGRAM_REGEXP,
                     PLACEMENT_HISTOGRAM_REGEXP, PLACEMENT_RESOURCE_REGEXP};
}

QStringList PlacementReportManager::getAvailableReportIds() const {
  QStringList ids{QString(RESOURCE_REPORT_NAME), QString(TIMING_REPORT_NAME)};
  if (QFile::exists(clockRegionsJsonPath())) {
    ids << QString(CLOCK_REGIONS_REPORT_NAME);
  }
  return ids;
}

std::unique_ptr<ITaskReport> PlacementReportManager::createReport(
    const QString &reportId, const QString& profile) {
  ITaskReport::DataReports dataReports;

  if (reportId == QString(CLOCK_REGIONS_REPORT_NAME)) {
    QVector<ClockRegionTable> tables;
    try {
      tables = clockRegionTables(clockRegionsJsonPath());
    } catch (const nlohmann::json::exception &e) {
      // a value of an unexpected type somewhere in the file
      tables = {{"Clock Regions",
                 {{"Item"}, {"Value"}},
                 {{"Report", QString("%1 could not be read: %2")
                                 .arg(clockRegionsJsonPath(), e.what())}}}};
    }
    for (const auto &table : tables) {
      dataReports.push_back(
          std::make_unique<TableReport>(table.columns, table.rows, table.name));
    }
    emit reportCreated(reportId);
    return std::make_unique<DefaultTaskReport>(std::move(dataReports), reportId);
  }

  if (!isFileParsed()) parseLogFile();

  if (reportId == QString(RESOURCE_REPORT_NAME)) {
    dataReports.push_back(std::make_unique<TableReport>(
        m_resourceColumns, resourceData(), QString{"Resource Utilization"}));
  } else {
    dataReports.push_back(std::make_unique<TableReport>(
        m_timingColumns, timingData(), QString{"Static Timing"}));
    for (auto &hgrm : histograms())
      dataReports.push_back(std::make_unique<TableReport>(
          m_histogramColumns, hgrm.second, hgrm.first));
  }

  emit reportCreated(reportId);

  return std::make_unique<DefaultTaskReport>(std::move(dataReports), reportId);
}

void PlacementReportManager::parseLogFile() {
  clearDataProfiles();

  auto logFile = createLogFile(QString(PLACEMENT_LOG));
  if (!logFile) return;

  auto in = QTextStream(logFile.get());
  if (in.atEnd()) return;

  QString line;
  auto lineNr = 0;
  while (in.readLineInto(&line)) {
    if (FIND_RESOURCES.match(line).hasMatch())
      parseResourceUsage(in, lineNr);
    else if (LOAD_PACKING_REGEXP.match(line).hasMatch())
      messages().insert(lineNr, TaskMessage{lineNr,
                                            MessageSeverity::INFO_MESSAGE,
                                            line.remove('#').simplified(),
                                            {}});
    else if (line.startsWith(CREATE_DEVICE_SECTION))
      lineNr = parseErrorWarningSection(in, lineNr, CREATE_DEVICE_SECTION,
                                        m_createDeviceKeys);
    else if (line.startsWith(PLACEMENT_SECTION))
      lineNr = parseErrorWarningSection(in, lineNr, PLACEMENT_SECTION,
                                        m_placementKeys);
    ++lineNr;
  }
  logFile->close();

  setFileParsed(true);
}

QString PlacementReportManager::getTimingLogFileName() const {
  return QString{PLACEMENT_TIMING_LOG};
}

bool PlacementReportManager::isStatisticalTimingLine(const QString &line) {
  return FIND_PLACEMENT_TIMINGS.match(line).hasMatch();
}

bool PlacementReportManager::isStatisticalTimingHistogram(const QString &line) {
  return PLACEMENT_HISTOGRAM_REGEXP.match(line).hasMatch();
}

void PlacementReportManager::splitTimingData(const QString &timingStr) {
  auto matchIt = FIND_STAT_TIMING.globalMatch(timingStr);
  auto valueIndex = 0;
  while (matchIt.hasNext() && valueIndex < TIMING_FIELDS.size()) {
    auto match = matchIt.next();
    timingData().push_back({TIMING_FIELDS[valueIndex++], match.captured()});
  }
}

}  // namespace FOEDAG
