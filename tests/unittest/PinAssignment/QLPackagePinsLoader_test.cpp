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

#include "PinAssignment/QLPackagePinsLoader.h"

#include <QFile>

#include "PinAssignment/IODirection.h"
#include "PinAssignment/PackagePinsModel.h"
#include "gtest/gtest.h"

using namespace FOEDAG;

// QLPackagePinsLoader auto-detects the CSV flavour from the header: the
// presence of "customer_pin_alias"/"pin_direction" selects the new format,
// otherwise the old "orientation"/"port_name"/"mapped_pin" format is used.
//
// Both fixtures describe two pins per side (one input, one output). In the new
// fixture a couple of pins (b_in, t_out) take their name from
// "customer_pin_alias" and the rest from "netlist_name"; in the old fixture a
// couple of rows leave "mapped_pin" empty and so take their name from
// "netlist_name". Each format has its own expected model below.

namespace {

struct PinExp {
  QString name;
  QString direction;
};

struct GroupExp {
  QString name;
  QVector<PinExp> pins;
};

void checkPinoutsMatchExpected(const PackagePinsModel &model,
                          const QVector<GroupExp> &expected) {
  ASSERT_EQ(model.pinData().count(), expected.size());
  for (int g = 0; g < expected.size(); ++g) {
    const auto &group = model.pinData().at(g);
    const auto &exp = expected.at(g);
    EXPECT_EQ(group.name, exp.name);
    ASSERT_EQ(group.pinData.count(), exp.pins.size())
        << "group " << exp.name.toStdString();
    for (int p = 0; p < exp.pins.size(); ++p) {
      const auto &data = group.pinData.at(p).data;
      EXPECT_EQ(data.at(PinName), exp.pins.at(p).name);
      EXPECT_EQ(data.at(BallName), exp.pins.at(p).name);
      EXPECT_EQ(data.at(BallId), exp.pins.at(p).name);
      EXPECT_EQ(data.at(Direction), exp.pins.at(p).direction);
    }
  }
}

// Return the comma-split header (first line) of a CSV resource.
QStringList csvHeader(const QString &resourcePath) {
  QFile file(resourcePath);
  EXPECT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text))
      << "cannot open " << resourcePath.toStdString();
  return QString::fromUtf8(file.readLine()).trimmed().split(',');
}

}  // namespace

// The fixtures must keep the exact column titles and ordering the loader and
// downstream tooling rely on; pin the header rows here.
TEST(QLPackagePinsLoader, OldFixtureHeader) {
  // Mirrors the real device_data old pin_table.csv column set/order.
  const QStringList expected = {
      "Orientation", "row",          "col",       "pin_number_in_cell",
      "port_name",   "mapped_pin",   "netlist_name", "GPIO_type",
      "Associated Clock", "Clock edge"};
  EXPECT_EQ(csvHeader(":/PinAssignment/qlpin_table_old_sample.csv"), expected);
}

TEST(QLPackagePinsLoader, NewFixtureHeader) {
  // Mirrors the real efpga_pinmap.csv column set/order.
  const QStringList expected = {
      "side",     "pin_direction",      "pin_type",    "row", "col",
      "subtile",  "physical_offset",    "customer_pin_alias", "netlist_name"};
  EXPECT_EQ(csvHeader(":/PinAssignment/qlpin_table_new_sample.csv"), expected);
}

// Old format: groups come from the "orientation" column, the pin name is the
// "mapped_pin" value (falling back to "netlist_name" when empty) and the
// direction is derived from the A2F/F2A pattern in "port_name".
TEST(QLPackagePinsLoader, LoadOldFormat) {
  PackagePinsModel model;
  QLPackagePinsLoader loader{&model};

  auto [res, error] = loader.load(":/PinAssignment/qlpin_table_old_sample.csv");
  EXPECT_EQ(res, true) << error.toStdString();

  // The TOP input and RIGHT output rows leave "mapped_pin" empty, so they are
  // named after their "netlist_name" column instead.
  const QVector<GroupExp> expected = {
      {"BOTTOM", {{"b_in", IODirection::INPUT}, {"b_out", IODirection::OUTPUT}}},
      {"TOP", {{"A2F_T_i[2]", IODirection::INPUT}, {"t_out", IODirection::OUTPUT}}},
      {"LEFT", {{"l_in", IODirection::INPUT}, {"l_out", IODirection::OUTPUT}}},
      {"RIGHT", {{"r_in", IODirection::INPUT}, {"F2A_R_o[3]", IODirection::OUTPUT}}},
  };
  checkPinoutsMatchExpected(model, expected);
}

// New format: groups come from the "side" column, the pin name is the
// "customer_pin_alias" when present and falls back to "netlist_name" otherwise,
// and the direction is read directly from "pin_direction".
TEST(QLPackagePinsLoader, LoadNewFormat) {
  PackagePinsModel model;
  QLPackagePinsLoader loader{&model};

  auto [res, error] = loader.load(":/PinAssignment/qlpin_table_new_sample.csv");
  EXPECT_EQ(res, true) << error.toStdString();

  // Pins are named after "netlist_name" except where a "customer_pin_alias" is
  // given (b_in on BOTTOM, t_out on TOP).
  const QVector<GroupExp> expected = {
      {"BOTTOM",
       {{"b_in", IODirection::INPUT}, {"io_f2a_b0", IODirection::OUTPUT}}},
      {"TOP",
       {{"io_a2f_t0", IODirection::INPUT}, {"t_out", IODirection::OUTPUT}}},
      {"LEFT",
       {{"io_a2f_l0", IODirection::INPUT}, {"io_f2a_l0", IODirection::OUTPUT}}},
      {"RIGHT",
       {{"io_a2f_r0", IODirection::INPUT}, {"io_f2a_r0", IODirection::OUTPUT}}},
  };
  checkPinoutsMatchExpected(model, expected);
}

// The new format carries the physical pin location in the row/col/subtile
// columns. load() records it in pinToLocationMap() as "x:y:z" == "col:row:sub".
TEST(QLPackagePinsLoader, LoadNewFormatPinLocations) {
  PackagePinsModel model;
  QLPackagePinsLoader loader{&model};

  auto [res, error] = loader.load(":/PinAssignment/qlpin_table_new_sample.csv");
  EXPECT_EQ(res, true) << error.toStdString();

  const auto &loc = loader.pinToLocationMap();
  EXPECT_EQ(loc.size(), 8);
  EXPECT_EQ(loc.value("b_in"), "3:0:0");
  EXPECT_EQ(loc.value("io_f2a_b0"), "4:0:1");
  EXPECT_EQ(loc.value("io_a2f_t0"), "3:18:0");
  EXPECT_EQ(loc.value("t_out"), "4:18:1");
  EXPECT_EQ(loc.value("io_a2f_l0"), "0:5:0");
  EXPECT_EQ(loc.value("io_f2a_l0"), "0:6:1");
  EXPECT_EQ(loc.value("io_a2f_r0"), "20:5:0");
  EXPECT_EQ(loc.value("io_f2a_r0"), "20:6:1");
}

TEST(QLPackagePinsLoader, LoadWrongFilePath) {
  PackagePinsModel model;
  QLPackagePinsLoader loader{&model};
  auto [res, error] = loader.load("wrong/path/file.csv");
  EXPECT_EQ(res, false);
  EXPECT_NE(error, QString());
}
