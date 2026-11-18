/*
Copyright 2026 QuickLogic team

GPL License

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

#include "Compiler/QLMetricsManager.h"

#include "gtest/gtest.h"

using namespace FOEDAG;

// The flow engine keys power on exactly the metrics it reads (REQ-122): a
// metric not found is null, and metrics not asked for stay out.
TEST(QLMetricsManager, snapshot_holds_the_named_metrics_only) {
  QLMetricsManager::MetricsMap metrics;
  AuroraMetrics found;
  found.found = true;
  found.string_value = "42";
  metrics["routing"]["num_input"] = found;
  AuroraMetrics missing;
  missing.string_value = "stale";
  metrics["routing"]["num_output"] = missing;
  AuroraMetrics other = found;
  other.string_value = "1.5";
  metrics["routing"]["time"] = other;

  const nlohmann::json values = QLMetricsManager::snapshot(
      metrics, {{"routing", "num_input"}, {"routing", "num_output"}, {"synthesis", "num_dff"}});
  EXPECT_EQ(values, nlohmann::json({{"routing.num_input", "42"},
                                    {"routing.num_output", nullptr},
                                    {"synthesis.num_dff", nullptr}}));
}
