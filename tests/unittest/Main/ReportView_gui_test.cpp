/*
Copyright 2026 The Foedag team

GPL License

Copyright (c) 2026 The Open-Source FPGA Foundation

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

#include <QComboBox>
#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "Compiler/Reports/DefaultTaskReport.h"
#include "Compiler/Reports/TableReport.h"
#include "Main/Tasks.h"
#include "gtest/gtest.h"

using namespace FOEDAG;

namespace {

std::unique_ptr<IDataReport> table(const QString &name,
                                   const IDataReport::TableData &rows) {
  return std::make_unique<TableReport>(
      IDataReport::ColumnValues{ReportColumn{"Statistics"}, ReportColumn{"Value"}},
      rows, name);
}

}  // namespace

// U-009: every "Select Data Table" entry shows its own page, an empty table
// included (D-001).
TEST(ReportView, EachSelectorEntryShowsItsOwnPage) {
  ITaskReport::DataReports tables;
  tables.push_back(table("Static Timing", {{"Setup WNS", "-1.6 ns"}}));
  tables.push_back(table("Empty", {}));
  tables.push_back(table("Histogram", {{"-2", "-1"}, {"-1", "0"}}));
  DefaultTaskReport report{std::move(tables), "Post placement - Report Static Timing"};

  QWidget page;
  auto layout = new QVBoxLayout(&page);
  generateReport(report, layout);
  auto selector = page.findChild<QComboBox *>("reportTableSelector");
  auto pages = page.findChild<QStackedWidget *>("reportTables");
  ASSERT_NE(selector, nullptr);
  ASSERT_NE(pages, nullptr);

  ASSERT_EQ(selector->count(), 3);
  EXPECT_EQ(pages->count(), selector->count());
  for (int i = 0; i < selector->count(); ++i) {
    selector->setCurrentIndex(i);
    EXPECT_EQ(pages->currentIndex(), i) << selector->itemText(i).toStdString();
  }
  EXPECT_EQ(selector->itemText(1), "Empty");
  EXPECT_EQ(pages->widget(1)->objectName(), "reportNoData");
}

// Opening a report whose tab is open refills that tab: the second report
// replaces the first, its "Select Data Table" row included.
TEST(ReportView, RefillReplacesThePreviousReport) {
  ITaskReport::DataReports first;
  first.push_back(table("Timing Data", {{"Setup WNS", "-1.6 ns"}}));
  first.push_back(table("Histogram", {{"-2", "-1"}}));
  DefaultTaskReport corner1{std::move(first), "STA - Report Static Timing"};
  ITaskReport::DataReports second;
  second.push_back(table("Timing Data", {{"Setup WNS", "-2.4 ns"}}));
  DefaultTaskReport corner2{std::move(second), "STA - Report Static Timing"};

  QWidget page;
  auto layout = new QVBoxLayout(&page);
  generateReport(corner1, layout);
  generateReport(corner2, layout);

  const auto selectors = page.findChildren<QComboBox *>("reportTableSelector");
  const auto pages = page.findChildren<QStackedWidget *>("reportTables");
  ASSERT_EQ(selectors.size(), 1);
  ASSERT_EQ(pages.size(), 1);
  EXPECT_EQ(selectors.first()->count(), 1);
  EXPECT_EQ(pages.first()->count(), 1);
  EXPECT_EQ(page.findChildren<QLabel *>().size(), 1) << "one Select Data Table label";
}
