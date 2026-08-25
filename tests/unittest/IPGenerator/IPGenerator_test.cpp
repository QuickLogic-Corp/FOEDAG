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

#include <filesystem>

#include "Compiler/Compiler.h"
#include "IPGenerate/IPCatalogBuilder.h"
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

// ---------------------------------------------------------------------------
// IP availability model (aurora2 #2246)
//
// These exercise the real manifest reader against real ip_manifest.json files
// on disk, the real availability rule against real IPDefinitions, and the real
// wrapper stamp against a real generated file. Nothing here is mocked: the
// only thing handed to the rule instead of being looked up is the device's
// identity, so the rule can be checked on more than the one device that
// happens to be installed.
// ---------------------------------------------------------------------------

namespace {
const std::filesystem::path kDummyGenerators =
    std::filesystem::path{FOEDAG_TEST_DATA_DIR} / "Testcases" / "IPGenerate" /
    "IP_Catalog" / "dummy_generators";

// readIPManifest() is given a generator script path and reads the manifest
// beside it, so only the directory has to exist.
std::filesystem::path manifestFixture(const std::string& name) {
  return kDummyGenerators / "manifests" / name / (name + "_gen.py");
}

IPAvailability readFixture(const std::filesystem::path& generator) {
  static Compiler* compiler = new Compiler();
  IPCatalogBuilder builder{compiler};
  return builder.readIPManifest(generator);
}

IPDefinition* makeIP(const std::string& name,
                     const IPAvailability& availability) {
  auto* def = new IPDefinition(IPDefinition::IPType::LiteXGenerator, name,
                               name + "_wrapper", "path_to_nowhere", {}, {});
  def->Availability(availability);
  return def;
}

IPGenerator::DeviceFacts onDevice(const std::string& name,
                                  const std::string& dspVersion) {
  return IPGenerator::DeviceFacts{true, name, dspVersion};
}

const IPGenerator::DeviceFacts kNoDevice{};
const char* const kSmokeDevice = "IDAHO-FPGA0806_WLBL";
}  // namespace

TEST(IPManifest, AbsentManifestIsDefaultedAndSaysSo) {
  // An IP that ships no manifest. REQ-018: this is a robustness rule for
  // third-party and field installs, not the shipping configuration, so it has
  // to stay distinguishable from a manifest that says "production".
  const auto availability = readFixture(kDummyGenerators / "RapidSilicon" /
                                        "IP" / "axi_ram" / "V1_0" /
                                        "axi_ram_gen.py");
  EXPECT_FALSE(availability.manifestPresent);
  EXPECT_EQ(availability.maturity, IPAvailability::Maturity::Production);
  EXPECT_TRUE(availability.requiredDspVersion.empty());
  EXPECT_FALSE(availability.requirementUnverifiable);
  EXPECT_TRUE(availability.manifestWarning.empty());
}

TEST(IPManifest, ProductionManifest) {
  const auto availability = readFixture(manifestFixture("production"));
  EXPECT_TRUE(availability.manifestPresent);
  EXPECT_EQ(availability.maturity, IPAvailability::Maturity::Production);
  EXPECT_TRUE(availability.requiredDspVersion.empty());
  EXPECT_FALSE(availability.requirementUnverifiable);
  EXPECT_TRUE(availability.manifestWarning.empty());
}

TEST(IPManifest, PreviewManifestCarriesItsNote) {
  const auto availability = readFixture(manifestFixture("preview"));
  EXPECT_EQ(availability.maturity, IPAvailability::Maturity::Preview);
  EXPECT_EQ(availability.maturityNote,
            "characterisation is incomplete above 100 MHz");
  EXPECT_TRUE(availability.requiredDspVersion.empty());
  EXPECT_FALSE(availability.requirementUnverifiable);
  EXPECT_TRUE(availability.manifestWarning.empty());
}

TEST(IPManifest, PreviewWithoutANoteIsStillPreview) {
  const auto availability = readFixture(manifestFixture("preview_no_note"));
  EXPECT_EQ(availability.maturity, IPAvailability::Maturity::Preview);
  EXPECT_TRUE(availability.maturityNote.empty());
}

TEST(IPManifest, RequiresDspVersion) {
  const auto availability = readFixture(manifestFixture("requires_dsp2"));
  EXPECT_EQ(availability.requiredDspVersion, "2_0");
  EXPECT_FALSE(availability.requirementUnverifiable);
  EXPECT_EQ(availability.maturity, IPAvailability::Maturity::Production);
}

TEST(IPManifest, EmptyRequiresBlockGatesNothing) {
  // An explicit, well-formed "no requirements" - not the same as unreadable.
  const auto availability = readFixture(manifestFixture("empty_requires"));
  EXPECT_TRUE(availability.requiredDspVersion.empty());
  EXPECT_FALSE(availability.requirementUnverifiable);
  EXPECT_TRUE(availability.manifestWarning.empty());
}

TEST(IPManifest, MalformedJsonDoesNotErodeTheFabricGate) {
  // Fail open on visibility, never on the gate: we cannot see whether this
  // manifest declared a requirement, so it must not read as "ungated".
  const auto availability = readFixture(manifestFixture("malformed"));
  EXPECT_TRUE(availability.manifestPresent);
  EXPECT_TRUE(availability.requirementUnverifiable);
  EXPECT_EQ(availability.maturity, IPAvailability::Maturity::Production);
  EXPECT_FALSE(availability.manifestWarning.empty());
}

TEST(IPManifest, NonObjectManifestDoesNotErodeTheFabricGate) {
  const auto availability = readFixture(manifestFixture("not_an_object"));
  EXPECT_TRUE(availability.manifestPresent);
  EXPECT_TRUE(availability.requirementUnverifiable);
  EXPECT_EQ(availability.manifestWarning,
            "Manifest is not a JSON object; its fabric requirement cannot be "
            "read.");
}

TEST(IPManifest, RequiresThatIsNotAnObjectIsReported) {
  const auto availability = readFixture(manifestFixture("requires_not_object"));
  EXPECT_TRUE(availability.requirementUnverifiable);
  EXPECT_TRUE(availability.requiredDspVersion.empty());
  EXPECT_EQ(availability.manifestWarning,
            "Manifest \"requires\" is not an object; the fabric requirement "
            "cannot be read.");
}

TEST(IPManifest, RequiresDspVersionOfTheWrongTypeIsReported) {
  // "dsp_version": 2 must not quietly become "no requirement" - that is how a
  // DSPV2 IP ends up instantiated on a DSPV1 fabric.
  const auto availability = readFixture(manifestFixture("requires_wrong_type"));
  EXPECT_TRUE(availability.requirementUnverifiable);
  EXPECT_TRUE(availability.requiredDspVersion.empty());
  EXPECT_EQ(availability.manifestWarning,
            "Manifest \"requires.dsp_version\" is not a string; the fabric "
            "requirement cannot be read.");
}

TEST(IPManifest, UnknownRequiresKeyIsReported) {
  // "requires" is a closed set, so a typo'd key is a missing gate, not an
  // ignorable extension.
  const auto availability =
      readFixture(manifestFixture("requires_unknown_key"));
  EXPECT_TRUE(availability.requirementUnverifiable);
  EXPECT_TRUE(availability.requiredDspVersion.empty());
  EXPECT_EQ(availability.manifestWarning,
            "Manifest \"requires\" contains unknown key \"dsp_versoin\"; the "
            "fabric requirement cannot be read.");
}

TEST(IPManifest, UnknownRequiresKeyKeepsTheVersionItDidRead) {
  // The forward-compatibility case: a newer ipgenerator adds a second requires
  // key. The dsp_version we understood must survive, or the IP silently
  // becomes ungated on an older FOEDAG.
  const auto availability = readFixture(manifestFixture("requires_extra_key"));
  EXPECT_EQ(availability.requiredDspVersion, "2_0");
  EXPECT_TRUE(availability.requirementUnverifiable);
  EXPECT_EQ(availability.manifestWarning,
            "Manifest \"requires\" contains unknown key \"bram_version\"; the "
            "fabric requirement cannot be read.");
}

TEST(IPManifest, MalformedDspVersionValueIsRejected) {
  // "2.0" instead of "2_0" - the DSPV1.1 spelling copied from DSP_TYPE. Taken
  // literally it matches no device and hides the IP from its own fabric.
  const auto availability = readFixture(manifestFixture("requires_bad_version"));
  EXPECT_TRUE(availability.requirementUnverifiable);
  EXPECT_TRUE(availability.requiredDspVersion.empty());
  EXPECT_EQ(availability.manifestWarning,
            "Manifest \"requires.dsp_version\" value \"2.0\" is not of the "
            "form <major>_<minor>; the fabric requirement cannot be read.");
}

TEST(IPManifest, UnknownMaturityIsTreatedAsProduction) {
  const auto availability = readFixture(manifestFixture("unknown_maturity"));
  EXPECT_EQ(availability.maturity, IPAvailability::Maturity::Production);
  // Maturity is allowed to fail open; the fabric gate is not involved.
  EXPECT_FALSE(availability.requirementUnverifiable);
  EXPECT_EQ(availability.manifestWarning,
            "Manifest declares unknown maturity \"experimental\"; treated as "
            "production.");
}

TEST(IPManifest, NewerSchemaHonoursTheFieldsItKnows) {
  const auto availability = readFixture(manifestFixture("newer_schema"));
  EXPECT_EQ(availability.maturity, IPAvailability::Maturity::Preview);
  EXPECT_EQ(availability.maturityNote, "from the future");
  EXPECT_EQ(availability.requiredDspVersion, "2_0");
  // A newer schema is not an error: the IP stays fully usable.
  EXPECT_FALSE(availability.requirementUnverifiable);
  EXPECT_TRUE(availability.manifestWarning.empty());
}

TEST(IPAvailabilityGate, AbsentManifestIsFlaggedAsDefaulted) {
  IPCatalog catalog;
  IPDefinition* def = makeIP("axi_ram_V1_0", IPAvailability{});
  catalog.addIP(def);

  const auto status = IPGenerator::EvaluateAvailability(
      def, &catalog, onDevice(kSmokeDevice, "1_0"));
  EXPECT_TRUE(status.available);
  EXPECT_TRUE(status.listed);
  EXPECT_EQ(status.state, "production");
  EXPECT_EQ(status.reason,
            "no ip_manifest.json; defaulted to production with no fabric "
            "requirement.");
}

TEST(IPAvailabilityGate, UngatedManifestIsAvailableEverywhere) {
  IPCatalog catalog;
  IPDefinition* def =
      makeIP("axi_ram_V1_0", readFixture(manifestFixture("production")));
  catalog.addIP(def);

  const auto status = IPGenerator::EvaluateAvailability(
      def, &catalog, onDevice(kSmokeDevice, "1_0"));
  EXPECT_TRUE(status.available);
  EXPECT_EQ(status.reason, "available on all devices.");
}

TEST(IPAvailabilityGate, MatchingFabricIsAvailableAndSaysSo) {
  IPCatalog catalog;
  IPAvailability requiresV1;
  requiresV1.manifestPresent = true;
  requiresV1.requiredDspVersion = "1_0";
  IPDefinition* def = makeIP("dsp_generator_v1_0", requiresV1);
  catalog.addIP(def);

  const auto status = IPGenerator::EvaluateAvailability(
      def, &catalog, onDevice(kSmokeDevice, "1_0"));
  EXPECT_TRUE(status.available);
  EXPECT_TRUE(status.listed);
  EXPECT_EQ(status.reason,
            "requires DSPV1 fabric; device IDAHO-FPGA0806_WLBL provides "
            "DSPV1.");
}

TEST(IPAvailabilityGate, FabricMismatchNamesTheCauseAndTheAlternative) {
  IPCatalog catalog;
  IPAvailability requiresV1;
  requiresV1.manifestPresent = true;
  requiresV1.requiredDspVersion = "1_0";
  catalog.addIP(makeIP("dsp_generator_v1_0", requiresV1));
  // The v2 requirement comes off a real manifest file on disk.
  IPDefinition* v2 = makeIP("dsp_generator_v2_0",
                            readFixture(manifestFixture("requires_dsp2")));
  catalog.addIP(v2);

  const auto status = IPGenerator::EvaluateAvailability(
      v2, &catalog, onDevice(kSmokeDevice, "1_0"));
  EXPECT_FALSE(status.available);
  EXPECT_FALSE(status.listed);
  EXPECT_EQ(status.state, "unavailable");
  // This is the exact line ip_catalog <name> and configure_ip print.
  EXPECT_EQ(v2->Name() + " " + status.reason,
            "dsp_generator_v2_0 requires DSPV2 fabric; device "
            "IDAHO-FPGA0806_WLBL provides DSPV1. Use dsp_generator_v1_0 on "
            "this device.");
}

TEST(IPAvailabilityGate, AlternativeIsFoundWithAnUppercaseVersionMarker) {
  // Catalog directories use both "v1_0" and "V1_0"; the suggestion must not
  // depend on which.
  IPCatalog catalog;
  IPAvailability requiresV1;
  requiresV1.manifestPresent = true;
  requiresV1.requiredDspVersion = "1_0";
  catalog.addIP(makeIP("dsp_generator_V1_0", requiresV1));
  IPDefinition* v2 = makeIP("dsp_generator_V2_0",
                            readFixture(manifestFixture("requires_dsp2")));
  catalog.addIP(v2);

  const auto status = IPGenerator::EvaluateAvailability(
      v2, &catalog, onDevice(kSmokeDevice, "1_0"));
  EXPECT_EQ(v2->Name() + " " + status.reason,
            "dsp_generator_V2_0 requires DSPV2 fabric; device "
            "IDAHO-FPGA0806_WLBL provides DSPV1. Use dsp_generator_V1_0 on "
            "this device.");
}

TEST(IPAvailabilityGate, AlternativeLookupRespectsTheSuffixBounds) {
  // The "_v<version>" arithmetic is easy to get wrong at the edges. None of
  // these names carries the required version as a suffix, so none may claim a
  // sibling - even though a plausible one is sitting in the catalog.
  IPCatalog catalog;
  IPAvailability requiresV1;
  requiresV1.manifestPresent = true;
  requiresV1.requiredDspVersion = "1_0";
  catalog.addIP(makeIP("dsp_generator_v1_0", requiresV1));
  catalog.addIP(makeIP("_v1_0", requiresV1));
  catalog.addIP(makeIP("v1_0", requiresV1));

  for (const char* name : {
           "v2_0",                   // shorter than the suffix
           "_v2_0",                  // exactly the suffix, no stem left
           "dsp_generator_x2_0",     // right length, wrong marker character
           "dsp_generator_v2_0_ext"  // version is not at the end
       }) {
    IPDefinition* def =
        makeIP(name, readFixture(manifestFixture("requires_dsp2")));
    catalog.addIP(def);
    const auto status = IPGenerator::EvaluateAvailability(
        def, &catalog, onDevice(kSmokeDevice, "1_0"));
    EXPECT_FALSE(status.available) << name;
    EXPECT_NE(status.reason.find("No version of this IP targets this device."),
              std::string::npos)
        << name << ": " << status.reason;
  }
}

TEST(IPAvailabilityGate, FabricMismatchWithNoSiblingSaysThereIsNone) {
  IPCatalog catalog;
  IPDefinition* v2 = makeIP("dsp_generator_v2_0",
                            readFixture(manifestFixture("requires_dsp2")));
  catalog.addIP(v2);

  const auto status = IPGenerator::EvaluateAvailability(
      v2, &catalog, onDevice(kSmokeDevice, "1_0"));
  EXPECT_FALSE(status.available);
  EXPECT_EQ(v2->Name() + " " + status.reason,
            "dsp_generator_v2_0 requires DSPV2 fabric; device "
            "IDAHO-FPGA0806_WLBL provides DSPV1. No version of this IP targets "
            "this device.");
}

TEST(IPAvailabilityGate, MinorFabricVersionsAreSpelledOut) {
  IPCatalog catalog;
  IPAvailability requiresV11;
  requiresV11.manifestPresent = true;
  requiresV11.requiredDspVersion = "1_1";
  IPDefinition* def = makeIP("dsp_generator_v1_1", requiresV11);
  catalog.addIP(def);

  const auto status =
      IPGenerator::EvaluateAvailability(def, &catalog, onDevice("SOMEDEV", "2_0"));
  EXPECT_FALSE(status.available);
  EXPECT_EQ(def->Name() + " " + status.reason,
            "dsp_generator_v1_1 requires DSPV1.1 fabric; device SOMEDEV "
            "provides DSPV2. No version of this IP targets this device.");
}

TEST(IPAvailabilityGate, NoDeviceSelectedListsEverythingAndAnnotates) {
  // REQ-015: with no device selected the gate must not run against a
  // default-constructed target - the IP is listed and the situation is stated.
  IPCatalog catalog;
  IPDefinition* v2 = makeIP("dsp_generator_v2_0",
                            readFixture(manifestFixture("requires_dsp2")));
  catalog.addIP(v2);

  const auto status = IPGenerator::EvaluateAvailability(v2, &catalog, kNoDevice);
  EXPECT_TRUE(status.available);
  EXPECT_TRUE(status.listed);
  EXPECT_EQ(status.state, "production");
  EXPECT_EQ(status.reason,
            "requires DSPV2 fabric; no device is selected, so availability is "
            "not evaluated.");
}

TEST(IPAvailabilityGate, UnreadableRequirementIsReportedNotErased) {
  // The malformed manifest must not present as a clean, ungated production IP.
  IPCatalog catalog;
  IPDefinition* def =
      makeIP("dsp_generator_v2_0", readFixture(manifestFixture("malformed")));
  catalog.addIP(def);

  const auto status = IPGenerator::EvaluateAvailability(
      def, &catalog, onDevice(kSmokeDevice, "1_0"));
  // Nothing hidden, nothing that would build rejected...
  EXPECT_TRUE(status.available);
  EXPECT_TRUE(status.listed);
  // ...but the gate is not silently assumed to have passed.
  EXPECT_TRUE(status.unverified);
  EXPECT_NE(status.reason.find(
                "fabric requirement could not be read from ip_manifest.json "
                "and has not been checked against device IDAHO-FPGA0806_WLBL."),
            std::string::npos)
      << status.reason;
  EXPECT_NE(IPGenerator::UnverifiedRequirementNotice(def).find(
                "IP dsp_generator_v2_0: the fabric requirement in "
                "ip_manifest.json could not be read, so it has not been "
                "checked against this device."),
            std::string::npos);
}

TEST(IPAvailabilityGate, PartlyReadableRequiresStillEnforcesWhatItRead) {
  // The must-not-regress case: dsp_version parsed fine, another key did not.
  // The gate has to run on what was read AND report that more may exist.
  IPCatalog catalog;
  IPAvailability requiresV1;
  requiresV1.manifestPresent = true;
  requiresV1.requiredDspVersion = "1_0";
  catalog.addIP(makeIP("dsp_generator_v1_0", requiresV1));
  IPDefinition* v2 = makeIP("dsp_generator_v2_0",
                            readFixture(manifestFixture("requires_extra_key")));
  catalog.addIP(v2);

  const auto status = IPGenerator::EvaluateAvailability(
      v2, &catalog, onDevice(kSmokeDevice, "1_0"));
  // Enforced, not waved through.
  EXPECT_FALSE(status.available);
  EXPECT_FALSE(status.listed);
  EXPECT_EQ(status.state, "unavailable");
  EXPECT_TRUE(status.unverified);
  EXPECT_EQ(v2->Name() + " " + status.reason,
            "dsp_generator_v2_0 requires DSPV2 fabric; device "
            "IDAHO-FPGA0806_WLBL provides DSPV1. Use dsp_generator_v1_0 on "
            "this device. Part of the \"requires\" block could not be read, so "
            "this IP may have further requirements that were not checked. "
            "Manifest \"requires\" contains unknown key \"bram_version\"; the "
            "fabric requirement cannot be read.");
  EXPECT_EQ(IPGenerator::UnverifiedRequirementNotice(v2),
            "IP dsp_generator_v2_0: part of the \"requires\" block in "
            "ip_manifest.json could not be read, so this IP may have further "
            "requirements that were not checked. Manifest \"requires\" "
            "contains unknown key \"bram_version\"; the fabric requirement "
            "cannot be read.");
}

TEST(IPAvailabilityGate, PartlyReadableRequiresIsStillUsableWhenTheGatePasses) {
  // Same manifest on the device it targets: available, but still flagged.
  IPCatalog catalog;
  IPDefinition* v2 = makeIP("dsp_generator_v2_0",
                            readFixture(manifestFixture("requires_extra_key")));
  catalog.addIP(v2);

  const auto status = IPGenerator::EvaluateAvailability(
      v2, &catalog, onDevice("SOMEDEV", "2_0"));
  EXPECT_TRUE(status.available);
  EXPECT_TRUE(status.listed);
  EXPECT_TRUE(status.unverified);
  EXPECT_NE(status.reason.find(
                "requires DSPV2 fabric; device SOMEDEV provides DSPV2."),
            std::string::npos)
      << status.reason;
  EXPECT_NE(status.reason.find("further requirements that were not checked."),
            std::string::npos)
      << status.reason;
}

TEST(IPAvailabilityGate, MalformedVersionValueDoesNotHideTheIP) {
  // Fail-closed check: "2.0" must not gate at all, on any device, rather than
  // gating against a value nothing can ever match.
  IPCatalog catalog;
  IPDefinition* def = makeIP(
      "dsp_generator_v2_0", readFixture(manifestFixture("requires_bad_version")));
  catalog.addIP(def);

  for (const char* deviceVersion : {"1_0", "2_0"}) {
    const auto status = IPGenerator::EvaluateAvailability(
        def, &catalog, onDevice("SOMEDEV", deviceVersion));
    EXPECT_TRUE(status.available) << deviceVersion;
    EXPECT_TRUE(status.listed) << deviceVersion;
    EXPECT_TRUE(status.unverified) << deviceVersion;
    EXPECT_EQ(status.reason.find("requires DSPV2.0"), std::string::npos)
        << status.reason;
  }
}

TEST(IPAvailabilityGate, UnreadableRequirementWithNoDeviceSaysSoToo) {
  IPCatalog catalog;
  IPDefinition* def = makeIP("dsp_generator_v2_0",
                             readFixture(manifestFixture("requires_wrong_type")));
  catalog.addIP(def);

  const auto status = IPGenerator::EvaluateAvailability(def, &catalog, kNoDevice);
  EXPECT_TRUE(status.unverified);
  EXPECT_NE(status.reason.find(
                "fabric requirement could not be read from ip_manifest.json "
                "and has not been checked (no device is selected)."),
            std::string::npos)
      << status.reason;
}

TEST(IPAvailabilityGate, PreviewIsAvailableAndStatesWhyItIsFlagged) {
  IPCatalog catalog;
  IPDefinition* def =
      makeIP("preview_ip_V1_0", readFixture(manifestFixture("preview")));
  catalog.addIP(def);

  const auto status = IPGenerator::EvaluateAvailability(
      def, &catalog, onDevice(kSmokeDevice, "1_0"));
  EXPECT_TRUE(status.available);
  EXPECT_TRUE(status.listed);
  EXPECT_TRUE(status.preview);
  EXPECT_EQ(status.state, "preview");
  EXPECT_EQ(status.reason,
            "available on all devices. Preview IP, not production-qualified: "
            "characterisation is incomplete above 100 MHz.");
  EXPECT_EQ(IPGenerator::PreviewNotice(def),
            "IP preview_ip_V1_0 is a preview IP and is not "
            "production-qualified: characterisation is incomplete above 100 "
            "MHz.");
}

TEST(IPAvailabilityGate, PreviewWithoutANoteStillWarns) {
  IPCatalog catalog;
  IPDefinition* def = makeIP("preview_ip_V1_0",
                             readFixture(manifestFixture("preview_no_note")));
  catalog.addIP(def);

  EXPECT_EQ(IPGenerator::PreviewNotice(def),
            "IP preview_ip_V1_0 is a preview IP and is not "
            "production-qualified.");
}

TEST(IPAvailabilityGate, UnknownMaturityLeavesTheIPUsableAndSaysWhy) {
  IPCatalog catalog;
  IPDefinition* def = makeIP("broken_ip_V1_0",
                             readFixture(manifestFixture("unknown_maturity")));
  catalog.addIP(def);

  const auto status = IPGenerator::EvaluateAvailability(
      def, &catalog, onDevice(kSmokeDevice, "1_0"));
  EXPECT_TRUE(status.available);
  EXPECT_TRUE(status.listed);
  EXPECT_FALSE(status.unverified);
  EXPECT_EQ(status.state, "production");
  EXPECT_EQ(status.reason,
            "available on all devices. Manifest declares unknown maturity "
            "\"experimental\"; treated as production.");
}

// --- wrapper stamp ---------------------------------------------------------

TEST(IPWrapperStamp, StampsTheGeneratedWrapperInPlace) {
  // The generators write "<module>_<version>.v"; getting that name wrong made
  // the stamp an undetectable no-op, so pin both the hit and the miss.
  const auto dir = std::filesystem::temp_directory_path() /
                   "foedag_stamp_test" / "src";
  std::filesystem::remove_all(dir.parent_path());
  std::filesystem::create_directories(dir);
  const auto wrapper = dir / "inst1_v2_0.v";
  {
    std::ofstream out(wrapper.string());
    out << "module inst1();\nendmodule\n";
  }

  Compiler compiler;
  const std::string line = "// WARNING: IP dsp_generator_v2_0 is a preview IP.";
  EXPECT_TRUE(
      IPGenerator::StampWrapperComment(&compiler, dir, "inst1_v2_0", line));

  std::ifstream in(wrapper.string());
  std::stringstream body;
  body << in.rdbuf();
  EXPECT_EQ(body.str(), line + "\nmodule inst1();\nendmodule\n");

  // Re-stamping is a no-op rather than a second copy of the line.
  EXPECT_TRUE(
      IPGenerator::StampWrapperComment(&compiler, dir, "inst1_v2_0", line));
  std::ifstream again(wrapper.string());
  std::stringstream body2;
  body2 << again.rdbuf();
  EXPECT_EQ(body2.str(), line + "\nmodule inst1();\nendmodule\n");

  // The un-versioned name is what the first implementation looked for; it must
  // now report the miss instead of succeeding silently.
  EXPECT_FALSE(IPGenerator::StampWrapperComment(&compiler, dir, "inst1", line));

  std::filesystem::remove_all(dir.parent_path());
}

TEST(IPWrapperStamp, PreservesTheWrapperFileMode) {
  // The temporary is created 0666 & ~umask, so a naive rename widens a
  // read-only generated file.
  const auto dir =
      std::filesystem::temp_directory_path() / "foedag_stamp_mode" / "src";
  std::filesystem::remove_all(dir.parent_path());
  std::filesystem::create_directories(dir);
  const auto wrapper = dir / "inst1_v1_0.v";
  {
    std::ofstream out(wrapper.string());
    out << "module inst1();\nendmodule\n";
  }
  const auto mode = std::filesystem::perms::owner_read |
                    std::filesystem::perms::group_read |
                    std::filesystem::perms::others_read;
  std::filesystem::permissions(wrapper, mode,
                               std::filesystem::perm_options::replace);

  Compiler compiler;
  EXPECT_TRUE(IPGenerator::StampWrapperComment(&compiler, dir, "inst1_v1_0",
                                               "// WARNING: preview."));
  EXPECT_EQ(std::filesystem::status(wrapper).permissions(), mode);

  std::filesystem::permissions(wrapper, std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::add);
  std::filesystem::remove_all(dir.parent_path());
}

TEST(IPWrapperStamp, LeavesNoTemporaryBehindWhenItCannotWrite) {
  const auto dir =
      std::filesystem::temp_directory_path() / "foedag_stamp_ro" / "src";
  std::filesystem::remove_all(dir.parent_path());
  std::filesystem::create_directories(dir);
  const auto wrapper = dir / "inst1_v1_0.v";
  {
    std::ofstream out(wrapper.string());
    out << "module inst1();\nendmodule\n";
  }
  // Read-only directory: the wrapper is found but the temporary cannot be
  // created, which is the "could not rewrite" case rather than "not found".
  std::filesystem::permissions(dir,
                               std::filesystem::perms::owner_read |
                                   std::filesystem::perms::owner_exec,
                               std::filesystem::perm_options::replace);

  Compiler compiler;
  const bool stamped = IPGenerator::StampWrapperComment(
      &compiler, dir, "inst1_v1_0", "// WARNING: preview.");

  std::filesystem::permissions(dir, std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::add);
  if (!stamped) {  // skipped when the test runs as root, which ignores 0500
    EXPECT_FALSE(std::filesystem::exists(
        std::filesystem::path{wrapper.string() + ".stamp.tmp"}));
    std::ifstream in(wrapper.string());
    std::stringstream body;
    body << in.rdbuf();
    EXPECT_EQ(body.str(), "module inst1();\nendmodule\n");
  }
  std::filesystem::remove_all(dir.parent_path());
}

TEST(IPWrapperStamp, WrapperNameMatchesTheGeneratorConvention) {
  // Pins the derivation used at the call site: module name plus the catalog
  // version, which is where getIpInfoFromPath() gets the build directory too.
  const auto generator = kDummyGenerators / "RapidSilicon" / "IP" /
                         "preview_ip" / "V1_0" / "preview_ip_gen.py";
  const auto meta = FOEDAG::getIpInfoFromPath(generator);
  EXPECT_EQ(meta.version, "V1_0");
  EXPECT_EQ("inst1" + std::string("_") + meta.version, "inst1_V1_0");
}

}  // namespace FOEDAG
