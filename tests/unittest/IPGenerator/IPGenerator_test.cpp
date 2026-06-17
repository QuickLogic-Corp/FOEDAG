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

#include "IPGenerate/IPGenerator.h"

#include <algorithm>
#include <string>
#include <vector>

#include "Compiler/Compiler.h"
#include "NewProject/ProjectManager/project_manager.h"
#include "ProjNavigator/tcl_command_integration.h"
#include "Utils/PathUtils.h"
#include "gtest/gtest.h"

namespace FOEDAG {

TEST(IPGenerate, IpInstanceDupes) {
  // The goal of this test is to ensure that IPInstances are unique by
  // module_name If an instance is added with the same module_name, the old one
  // should be removed

  IPCatalog* ipCat = new IPCatalog();
  Compiler* compiler = new Compiler();
  IPGenerator* ipGen = new IPGenerator(PathUtils::instance().installDir(), ipCat, compiler);

  std::vector<Connector*> connections;
  std::vector<Value*> parameters;
  Constant* lrange = new Constant(0);
  Parameter* rrange = new Parameter("Width", 0);
  Range range(lrange, rrange);
  Port* port = new Port("clk", Port::Direction::Input, Port::Function::Clock,
                        Port::Polarity::High, range);
  connections.push_back(port);
  IPDefinition* def = new IPDefinition(IPDefinition::IPType::Other, "MOCK_IP",
                                       "MOCK_IP_wrapper", "path_to_nowhere",
                                       connections, parameters);
  IPDefinition* def2 = new IPDefinition(IPDefinition::IPType::Other, "MOCK_IP2",
                                        "MOCK_IP2_wrapper", "path_to_nowhere",
                                        connections, parameters);

  ipCat->addIP(def);

  EXPECT_EQ(ipGen->IPInstances().size(), 0)
      << "Ensure the IPInstances is empty so far";

  std::vector<SParameter> params;
  std::vector<SParameter> params2;

  // Add an ip instance
  IPInstance* instance = new IPInstance("duplicateName", "version_num", def,
                                        params, "module_name", "out_file");
  ipGen->AddIPInstance(instance);
  EXPECT_EQ(ipGen->IPInstances().size(), 1)
      << "Ensure the IPInstances count is now 1";

  // Add exact same IP
  ipGen->AddIPInstance(instance);
  EXPECT_EQ(ipGen->IPInstances().size(), 1)
      << "Ensure the IPInstances count is still 1";

  // Add IP with similar values
  IPInstance* instance2 = new IPInstance("duplicateName", "version_num", def,
                                         params, "module_name", "out_file");
  ipGen->AddIPInstance(instance2);
  EXPECT_EQ(ipGen->IPInstances().size(), 1)
      << "Ensure the IPInstances count is still 1";

  // Add IP with different values, but same module name
  IPInstance* instance3 = new IPInstance("newName", "newVersion", def2, params2,
                                         "module_name", "newPath");
  ipGen->AddIPInstance(instance3);
  EXPECT_EQ(ipGen->IPInstances().size(), 2)
      << "Ensure the IPInstances count is 2 bacause different versions";

  // Add IP same values, but diff module name
  IPInstance* instance4 = new IPInstance("duplicateName", "version_num", def,
                                         params, "NEW_MODULE_NAME", "out_file");
  ipGen->AddIPInstance(instance4);
  EXPECT_EQ(ipGen->IPInstances().size(), 3)
      << "Ensure the IPInstances count is now 3";
}

TEST(IPGenerate, CheckAllIPPath) {
  IPCatalog* ipCat = new IPCatalog();
  Compiler* compiler = new Compiler();
  IPGenerator* ipGen = new IPGenerator(PathUtils::instance().installDir(), ipCat, compiler);
  ProjectManager* pm = new ProjectManager{};
  Project::Instance()->setProjectName("testProject");
  compiler->setGuiTclSync(new TclCommandIntegration{pm, nullptr});

  std::vector<Connector*> connections;
  std::vector<Value*> parameters;
  Constant* lrange = new Constant(0);
  Parameter* rrange = new Parameter("Width", 0);
  Range range(lrange, rrange);
  Port* port = new Port("clk", Port::Direction::Input, Port::Function::Clock,
                        Port::Polarity::High, range);
  connections.push_back(port);
  IPDefinition* def = new IPDefinition(IPDefinition::IPType::Other, "MOCK_IP",
                                       "MOCK_IP_wrapper", "path_to_nowhere",
                                       connections, parameters);

  ipCat->addIP(def);

  EXPECT_EQ(ipGen->IPInstances().size(), 0)
      << "Ensure the IPInstances is empty so far";

  std::vector<SParameter> params;
  std::vector<SParameter> params2;

  // Add an ip instance
  IPInstance* instance = new IPInstance("duplicateName", "version_num", def,
                                        params, "module_name", "out_file");
  ipGen->AddIPInstance(instance);

  EXPECT_EQ(ipGen->GetBuildDir(instance),
            "run_1/IPs/path_to_nowhere/module_name");

  EXPECT_EQ(ipGen->GetSimDir(instance),
            "run_1/IPs/path_to_nowhere/module_name/sim");

  EXPECT_EQ(ipGen->GetSimArtifactsDir(instance),
            "run_1/IPs/simulation/path_to_nowhere/module_name");

  EXPECT_EQ(ipGen->GetCachePath(instance),
            "run_1/IPs/path_to_nowhere/module_name/MOCK_IP_module_name.json");

  EXPECT_EQ(ipGen->GetTmpCachePath(instance),
            "run_1/IPs/.tmp/path_to_nowhere/module_name/"
            "MOCK_IP_module_name.json");

  EXPECT_EQ(ipGen->GetTmpPath(), "run_1/IPs/.tmp");

  EXPECT_EQ(ipGen->GetProjectIPsPath(), "run_1/IPs");
}

TEST(IPParameterValidate, IntRange) {
  IPParameter p("Width", "Width", "8", IPParameter::ParamType::Int);
  p.SetRange({"1", "16"});
  std::string err;

  EXPECT_TRUE(p.Validate("8", err)) << err;
  EXPECT_TRUE(p.Validate("1", err)) << err;   // inclusive lower bound
  EXPECT_TRUE(p.Validate("16", err)) << err;  // inclusive upper bound
  EXPECT_TRUE(p.Validate("", err)) << err;    // empty -> default

  EXPECT_FALSE(p.Validate("0", err));
  EXPECT_FALSE(p.Validate("17", err));
  EXPECT_FALSE(p.Validate("abc", err));
  EXPECT_FALSE(p.Validate("8.5", err));  // not an integer
}

TEST(IPParameterValidate, FloatRange) {
  IPParameter p("Freq", "Freq", "1.0", IPParameter::ParamType::Float);
  p.SetRange({"0.5", "2.5"});
  std::string err;

  EXPECT_TRUE(p.Validate("1.5", err)) << err;
  EXPECT_TRUE(p.Validate("0.5", err)) << err;
  EXPECT_TRUE(p.Validate("2.5", err)) << err;

  EXPECT_FALSE(p.Validate("0.4", err));
  EXPECT_FALSE(p.Validate("2.6", err));
  EXPECT_FALSE(p.Validate("notanumber", err));
}

TEST(IPParameterValidate, Bool) {
  IPParameter p("Enable", "Enable", "0", IPParameter::ParamType::Bool);
  std::string err;

  EXPECT_TRUE(p.Validate("0", err)) << err;
  EXPECT_TRUE(p.Validate("1", err)) << err;
  EXPECT_TRUE(p.Validate("true", err)) << err;
  EXPECT_TRUE(p.Validate("False", err)) << err;  // case-insensitive

  EXPECT_FALSE(p.Validate("2", err));
  EXPECT_FALSE(p.Validate("yes", err));
}

TEST(IPParameterValidate, Options) {
  IPParameter p("Mode", "Mode", "fast", IPParameter::ParamType::String);
  p.SetOptions({"fast", "slow", "auto"});
  std::string err;

  EXPECT_TRUE(p.Validate("fast", err)) << err;
  EXPECT_TRUE(p.Validate("auto", err)) << err;

  EXPECT_FALSE(p.Validate("turbo", err));
  EXPECT_FALSE(p.Validate("FAST", err));  // options are case-sensitive
}

TEST(IPParameterValidate, PlainTypes) {
  std::string err;

  // Int without a range still must be numeric.
  IPParameter i("Count", "Count", "0", IPParameter::ParamType::Int);
  EXPECT_TRUE(i.Validate("42", err)) << err;
  EXPECT_TRUE(i.Validate("-7", err)) << err;
  EXPECT_FALSE(i.Validate("x", err));

  // String accepts anything.
  IPParameter s("Label", "Label", "", IPParameter::ParamType::String);
  EXPECT_TRUE(s.Validate("anything goes", err)) << err;

  // FilePath content is not validated (mirrors the GUI).
  IPParameter f("File", "File", "", IPParameter::ParamType::FilePath);
  EXPECT_TRUE(f.Validate("/no/such/path", err)) << err;
}

TEST(IPParameterValidate, DependencyGating) {
  // field_with_dep depends on the boolean bool_for_dep, and has a [1,10] range.
  IPParameter dep("field_with_dep", "field_with_dep", "27",
                  IPParameter::ParamType::Int);
  dep.SetRange({"1", "10"});
  dep.SetDependencies({"bool_for_dep"});

  // Dependency off -> inactive (so its out-of-range default 27 is ignored);
  // dependency on -> active.
  EXPECT_FALSE(dep.IsActive({{"bool_for_dep", "0"}}));
  EXPECT_TRUE(dep.IsActive({{"bool_for_dep", "1"}}));

  // A dependency missing from the map is treated as false -> inactive.
  EXPECT_FALSE(dep.IsActive({}));

  // Statically disabled fields are inactive regardless of dependencies.
  IPParameter sd("x", "x", "5", IPParameter::ParamType::Int);
  sd.SetRange({"1", "10"});
  sd.SetDisable("true");
  EXPECT_FALSE(sd.IsActive({{"bool_for_dep", "1"}}));

  // "1" is treated as disabled too (same boolean interpretation as deps).
  IPParameter sd1("x1", "x1", "5", IPParameter::ParamType::Int);
  sd1.SetDisable("1");
  EXPECT_FALSE(sd1.IsActive({{"bool_for_dep", "1"}}));

  IPParameter en("y", "y", "5", IPParameter::ParamType::Int);
  en.SetDisable("false");
  EXPECT_TRUE(en.IsActive({{"bool_for_dep", "1"}}));

  // A field with no dependencies and no static disable is always active.
  IPParameter plain("z", "z", "5", IPParameter::ParamType::Int);
  EXPECT_TRUE(plain.IsActive({}));

  // Multiple dependencies: active only when ALL controlling bools are true.
  IPParameter md("multi_dep", "multi_dep", "0", IPParameter::ParamType::Int);
  md.SetDependencies({"a", "b"});
  EXPECT_FALSE(md.IsActive({{"a", "1"}, {"b", "0"}}));
  EXPECT_TRUE(md.IsActive({{"a", "1"}, {"b", "true"}}));  // case-insensitive
}

TEST(IPParameterValidate, Describe) {
  IPParameter eq("equation", "Equation", "AxB", IPParameter::ParamType::String);
  eq.SetOptions({"AxB", "AxB+CxD"});
  IPParameter aw("a_width", "A width", "32", IPParameter::ParamType::Int);
  aw.SetRange({"1", "32"});
  IPParameter rin("reg_in", "Reg In", "0", IPParameter::ParamType::Bool);

  EXPECT_EQ(eq.ParamTypeStr(), "string");
  EXPECT_EQ(aw.ParamTypeStr(), "int");
  EXPECT_EQ(rin.ParamTypeStr(), "bool");

  EXPECT_EQ(eq.ConstraintStr(), "choices: AxB, AxB+CxD");
  EXPECT_EQ(aw.ConstraintStr(), "range: [1, 32]");
  EXPECT_EQ(rin.ConstraintStr(), "");  // unconstrained bool

  std::vector<Value*> params{&eq, &aw, &rin};
  const std::string table = DescribeIPParameters(params);
  EXPECT_NE(table.find("equation"), std::string::npos);
  EXPECT_NE(table.find("(default: 32)"), std::string::npos);
  EXPECT_NE(table.find("range: [1, 32]"), std::string::npos);
  EXPECT_NE(table.find("choices: AxB, AxB+CxD"), std::string::npos);
  // One line per parameter (3 params -> 2 newline separators).
  EXPECT_EQ(std::count(table.begin(), table.end(), '\n'), 2);
}

}  // namespace FOEDAG
