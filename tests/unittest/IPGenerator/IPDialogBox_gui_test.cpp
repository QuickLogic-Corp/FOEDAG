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
#include "IpConfigurator/IPDialogBox.h"

#include <QApplication>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <filesystem>
#include <fstream>

#include "Compiler/Compiler.h"
#include "IPGenerate/IPCatalogBuilder.h"
#include "IPGenerate/IPGenerator.h"
#include "MainWindow/Session.h"
#include "NewProject/ProjectManager/project.h"
#include "NewProject/ProjectManager/project_manager.h"
#include "ProjNavigator/tcl_command_integration.h"
#include "Tcl/TclInterpreter.h"
#include "Utils/FileUtils.h"
#include "Utils/PathUtils.h"
#include "gtest/gtest.h"

extern FOEDAG::Session *GlobalSession;

using namespace FOEDAG;

namespace {

// The template a generator prints for --json_template: one int parameter
// with a range, so its field gets a range validator.
constexpr auto kTemplate = R"({
  "build_name": "demo",
  "parameters": [
    {"parameter": "depth", "title": "DEPTH", "type": "int",
     "default": "16", "range": ["4", "64"]}
  ]
})";

int countConfigureIp(ClientData data, Tcl_Interp *, int argc,
                     const char *argv[]) {
  auto calls = static_cast<std::vector<std::string> *>(data);
  std::string call;
  for (int i = 0; i < argc; ++i) call += std::string{argv[i]} + " ";
  calls->push_back(call);
  return TCL_OK;
}

}  // namespace

// U-006: an out-of-range value is marked invalid, and Generate refuses it with
// the warning and issues no configure_ip. The dialog's generator runs with the
// system python3; it needs no litex.
TEST(IPDialogBox, OutOfRangeValueIsRefused) {
  if (FileUtils::LocateExecFile("python3").empty())
    GTEST_SKIP() << "no python3 to run the fixture generator";

  QTemporaryDir dir;
  const std::filesystem::path root{dir.path().toStdString()};
  const auto script = root / "catalog/quicklogic/ip/demo/v1_0/demo_gen.py";
  std::filesystem::create_directories(script.parent_path());
  std::ofstream{script} << "import sys\nprint('''" << kTemplate << "''')\n";
  Project::Instance()->setProjectPath(
      QString::fromStdString((root / "project").string()));

  auto interp = new TclInterpreter();
  auto catalog = new IPCatalog();
  auto compiler = new Compiler();
  auto generator =
      new IPGenerator(PathUtils::instance().installDir(), catalog, compiler);
  compiler->SetIPGenerator(generator);
  auto projectManager = new ProjectManager{};
  compiler->setGuiTclSync(new TclCommandIntegration{projectManager, nullptr});
  // Not deleted: ~Session expects a main window.
  auto session =
      new Session{nullptr, interp, nullptr, nullptr, nullptr, compiler, nullptr};
  Session *previousSession = GlobalSession;
  GlobalSession = session;
  std::vector<std::string> calls;
  interp->registerCmd("configure_ip", countConfigureIp, &calls, nullptr);

  IPCatalogBuilder builder{compiler};
  ASSERT_TRUE(builder.buildLiteXIPFromJson(catalog, script, kTemplate));
  const std::string ip = IPCatalogBuilder::ipNameFromGeneratorPath(script);
  auto definition = catalog->Definition(ip);
  ASSERT_NE(definition, nullptr);
  std::vector<SParameter> noValues;
  generator->AddIPInstance(new IPInstance(ip, "v1_0", definition, noValues,
                                          "demo", (root / "out").string()));

  IPDialogBox dialog{DeviceParameters{}, nullptr, QString::fromStdString(ip),
                     {}, {}};
  dialog.show();
  QLineEdit *depth{nullptr};
  for (auto edit : dialog.findChildren<QLineEdit *>())
    if (edit->property("customId").toString() == "depth") depth = edit;
  ASSERT_NE(depth, nullptr) << "no field with customId depth";
  EXPECT_EQ(depth->text(), "16");

  depth->clear();
  QTest::keyClicks(depth, "1");
  EXPECT_TRUE(depth->property("invalid").toBool());

  QStringList warnings;
  QTimer answer;
  QObject::connect(&answer, &QTimer::timeout, [&]() {
    if (auto box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
      warnings << box->text();
      box->accept();
    }
  });
  answer.start(20);
  const size_t callsBefore = calls.size();
  dialog.findChild<QPushButton *>("generateIp")->click();
  answer.stop();

  EXPECT_EQ(warnings,
            QStringList{"Atleast one invalid (red) parameter value found. "
                        "Reevaluate parameters before generating the IP."});
  EXPECT_EQ(calls.size(), callsBefore) << "no configure_ip is issued";
  EXPECT_TRUE(dialog.isVisible()) << "the dialog stays open";

  GlobalSession = previousSession;
}
