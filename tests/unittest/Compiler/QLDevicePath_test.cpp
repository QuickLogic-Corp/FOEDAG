#include "Compiler/QLDevicePath.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "gtest/gtest.h"

#ifndef _WIN32
#include <unistd.h>
#endif

using namespace FOEDAG;

namespace {

// root ignores 0500, so the read-only cases prove nothing there.
bool runningAsRoot() {
#ifndef _WIN32
  return geteuid() == 0;
#else
  return false;
#endif
}

const char* const kFamily = "QLF_K6N10";
const char* const kFoundry = "GF";
const char* const kNode = "12nm";
const char* const kSourceDevice = "EVAL-MSC-CUSTOM";
const char* const kGeneratedDevice = "EVAL-MSC-CUSTOM-AUTOFPGA12x10_abc123";

class GeneratedDevicePath : public ::testing::Test {
 protected:
  void SetUp() override {
    scratch_dir_path = std::filesystem::temp_directory_path() / "foedag_qldevicepath_test";
    std::filesystem::remove_all(scratch_dir_path);

    source_node_dir_path =
        scratch_dir_path / "install" / kFamily / kFoundry / kNode;
    source_device_dir_path = source_node_dir_path / kSourceDevice;
    std::filesystem::create_directories(source_device_dir_path);

    working_dir_path = scratch_dir_path / "work";
    std::filesystem::create_directories(working_dir_path);

    home_dir_path = scratch_dir_path / "home";
    std::filesystem::create_directories(home_dir_path);

    saved_home = readEnv("HOME");
    saved_override = readEnv("AURORA2_GENERATED_DEVICE_DIR");
    setEnv("HOME", home_dir_path.string());
    unsetEnv("AURORA2_GENERATED_DEVICE_DIR");
  }

  void TearDown() override {
    // restore before deleting, so a failed test cannot leak a bogus HOME
    saved_home ? setEnv("HOME", *saved_home) : unsetEnv("HOME");
    saved_override ? setEnv("AURORA2_GENERATED_DEVICE_DIR", *saved_override)
                   : unsetEnv("AURORA2_GENERATED_DEVICE_DIR");

    std::filesystem::permissions(source_node_dir_path, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::add);
    std::filesystem::remove_all(scratch_dir_path);
  }

  static std::optional<std::string> readEnv(const char* name) {
    const char* const value = std::getenv(name);
    return value ? std::optional<std::string>(value) : std::nullopt;
  }
  static void setEnv(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
  }
  static void unsetEnv(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
  }

  GeneratedDeviceDestination claim(std::uintmax_t required_bytes = 0) {
    return claimGeneratedDeviceDir(source_device_dir_path, working_dir_path, kFamily, kFoundry,
                                   kNode, kGeneratedDevice, required_bytes);
  }

  void makeSourceRootReadOnly() {
    std::filesystem::permissions(source_node_dir_path,
                                 std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_exec,
                                 std::filesystem::perm_options::replace);
  }

  std::filesystem::path scratch_dir_path;
  std::filesystem::path source_node_dir_path;
  std::filesystem::path source_device_dir_path;
  std::filesystem::path working_dir_path;
  std::filesystem::path home_dir_path;
  std::optional<std::string> saved_home;
  std::optional<std::string> saved_override;
};

// The guarantee that nothing changes where the write already worked.
TEST_F(GeneratedDevicePath, WritableSourceRootIsUsedAndNotRegistered) {
  const GeneratedDeviceDestination destination = claim();

  EXPECT_EQ(destination.device_dir_path, source_node_dir_path / kGeneratedDevice);
  EXPECT_TRUE(destination.root_dir_path.empty());
  EXPECT_TRUE(destination.error.empty());
  EXPECT_TRUE(std::filesystem::is_directory(destination.device_dir_path));
}

// No probe directory may survive: anything enumerating the node dir counts it.
TEST_F(GeneratedDevicePath, LeavesNothingBesideTheClaimedDirectory) {
  const GeneratedDeviceDestination destination = claim();
  ASSERT_FALSE(destination.device_dir_path.empty());

  int entry_count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(source_node_dir_path)) {
    ++entry_count;
    EXPECT_TRUE(entry.path().filename() == kSourceDevice ||
                entry.path().filename() == kGeneratedDevice)
        << "unexpected entry left behind: " << entry.path();
  }
  EXPECT_EQ(entry_count, 2);
}

TEST_F(GeneratedDevicePath, ReadOnlySourceRootFallsBackToHomeAndRegistersIt) {
  if (runningAsRoot()) {
    GTEST_SKIP() << "root ignores 0500";
  }
  makeSourceRootReadOnly();

  const GeneratedDeviceDestination destination = claim();

  EXPECT_EQ(destination.device_dir_path,
            home_dir_path / "aurora_devices" / kFamily / kFoundry / kNode / kGeneratedDevice);
  EXPECT_EQ(destination.root_dir_path, home_dir_path / "aurora_devices");
  EXPECT_TRUE(std::filesystem::is_directory(destination.device_dir_path));
}

TEST_F(GeneratedDevicePath, OverrideWinsOverWritableSourceRootAndIsCreated) {
  const std::filesystem::path override_root_dir_path = scratch_dir_path / "elsewhere";
  setEnv("AURORA2_GENERATED_DEVICE_DIR", override_root_dir_path.string());

  const GeneratedDeviceDestination destination = claim();

  EXPECT_EQ(destination.device_dir_path,
            override_root_dir_path / kFamily / kFoundry / kNode / kGeneratedDevice);
  EXPECT_EQ(destination.root_dir_path, override_root_dir_path);
  EXPECT_TRUE(std::filesystem::is_directory(destination.device_dir_path));
}

// A mistyped override must not cost a run whose pack already succeeded.
TEST_F(GeneratedDevicePath, OverridePointingAtAFileIsWarnedAboutAndSkipped) {
  const std::filesystem::path override_file_path = scratch_dir_path / "not-a-dir";
  { std::ofstream out(override_file_path.string()); out << "x\n"; }
  setEnv("AURORA2_GENERATED_DEVICE_DIR", override_file_path.string());

  const GeneratedDeviceDestination destination = claim();

  EXPECT_EQ(destination.device_dir_path, source_node_dir_path / kGeneratedDevice);
  EXPECT_TRUE(destination.error.empty());
  EXPECT_FALSE(destination.warnings.empty());
}

// The runaway self-copy: a target inside the package being copied is refused.
TEST_F(GeneratedDevicePath, OverrideInsideTheSourceDeviceIsRefused) {
  setEnv("AURORA2_GENERATED_DEVICE_DIR", source_device_dir_path.string());

  const GeneratedDeviceDestination destination = claim();

  EXPECT_FALSE(pathIsInside(destination.device_dir_path, source_device_dir_path));
  EXPECT_EQ(destination.device_dir_path, source_node_dir_path / kGeneratedDevice);
}

TEST_F(GeneratedDevicePath, FallsBackToWorkingDirectoryWhenNoHomeAndSourceIsReadOnly) {
  if (runningAsRoot()) {
    GTEST_SKIP() << "root ignores 0500";
  }
  makeSourceRootReadOnly();
  unsetEnv("HOME");

  const GeneratedDeviceDestination destination = claim();

  EXPECT_EQ(destination.device_dir_path, working_dir_path / "aurora_devices" / kFamily /
                                             kFoundry / kNode / kGeneratedDevice);
  EXPECT_EQ(destination.root_dir_path, working_dir_path / "aurora_devices");
  EXPECT_TRUE(destination.error.empty());
}

TEST_F(GeneratedDevicePath, ExistingGeneratedDirectoryIsReplacedAndReported) {
  const std::filesystem::path existing_dir_path = source_node_dir_path / kGeneratedDevice;
  std::filesystem::create_directories(existing_dir_path / "stale");

  const GeneratedDeviceDestination destination = claim();

  EXPECT_EQ(destination.device_dir_path, existing_dir_path);
  EXPECT_TRUE(destination.replaced_existing);
  EXPECT_FALSE(std::filesystem::exists(existing_dir_path / "stale"));
}

// A candidate without room is passed over, not failed on.
TEST_F(GeneratedDevicePath, CandidateWithoutSpaceIsSkipped) {
  const GeneratedDeviceDestination destination = claim(/*required_bytes*/ ~std::uintmax_t(0) / 2);

  // every candidate is too small, so nothing can be claimed - but it is reported, not thrown
  EXPECT_TRUE(destination.device_dir_path.empty());
  EXPECT_FALSE(destination.error.empty());
}

TEST(GeneratedDevicePathHelpers, ExpandLeadingTildeUsesHomeAndLeavesOtherPathsAlone) {
  const char* const home = std::getenv("HOME");
  if (home == nullptr || *home == '\0') {
    GTEST_SKIP() << "no HOME to expand against";
  }
  EXPECT_EQ(expandLeadingTilde("~/devs"), std::filesystem::path(home) / "devs");
  EXPECT_EQ(expandLeadingTilde("/abs/devs"), std::filesystem::path("/abs/devs"));
  EXPECT_EQ(expandLeadingTilde("~user/devs"), std::filesystem::path("~user/devs"));
}

TEST(GeneratedDevicePathHelpers, DirectoryContentSizeCountsFileBytes) {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "foedag_qldevicepath_size";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir / "sub");
  { std::ofstream out((dir / "sub" / "a.bin").string()); out << std::string(1024, 'x'); }

  EXPECT_EQ(directoryContentSize(dir), 1024u);
  EXPECT_EQ(directoryContentSize(dir / "does-not-exist"), 0u);

  std::filesystem::remove_all(dir);
}

}  // namespace
