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

// root ignores 0500, and Windows ignores the read-only attribute for directories, so on
// both the "read-only" setup silently still permits writes. Tests that depend on the
// denial check whether it actually took effect rather than asserting blindly.
bool readOnlyDirsAreEnforced(const std::filesystem::path& dir_path) {
#ifdef _WIN32
  (void)dir_path;
  return false;
#else
  if (geteuid() == 0) {
    return false;
  }
  std::error_code ec;
  const std::filesystem::path probe = dir_path / "enforcement_probe";
  const bool created = std::filesystem::create_directory(probe, ec);
  if (created && !ec) {
    std::filesystem::remove_all(probe, ec);
    return false;
  }
  return true;
#endif
}

const char* const kFamily = "QLF_K6N10";
const char* const kFoundry = "GF";
const char* const kNode = "12nm";
const char* const kSourceDevice = "EVAL-MSC-CUSTOM";
const char* const kGeneratedDevice = "EVAL-MSC-CUSTOM-AUTOFPGA12x10_abc123";

std::optional<std::string> readEnv(const char* name) {
  const char* const value = std::getenv(name);
  return value ? std::optional<std::string>(value) : std::nullopt;
}
void setEnv(const char* name, const std::string& value) {
#ifdef _WIN32
  _putenv_s(name, value.c_str());
#else
  setenv(name, value.c_str(), 1);
#endif
}
void unsetEnv(const char* name) {
#ifdef _WIN32
  _putenv_s(name, "");
#else
  unsetenv(name);
#endif
}

class GeneratedDevicePath : public ::testing::Test {
 protected:
  void SetUp() override {
    // namespaced by user and pid: a fixed name collides between two users, or two
    // worktrees, on a shared build host - and SetUp would then delete the other's tree.
    std::error_code ec;
    scratch_dir_path = std::filesystem::temp_directory_path(ec) /
                       ("foedag_qldevicepath_" + std::to_string(
#ifdef _WIN32
                            0
#else
                            static_cast<long>(getpid())
#endif
                            ));
    std::filesystem::remove_all(scratch_dir_path, ec);   // non-throwing: a poisoned leftover
    ec.clear();                                          // must not break every later run

    source_node_dir_path = scratch_dir_path / "install" / kFamily / kFoundry / kNode;
    source_device_dir_path = source_node_dir_path / kSourceDevice;
    std::filesystem::create_directories(source_device_dir_path, ec);

    working_dir_path = scratch_dir_path / "work";
    std::filesystem::create_directories(working_dir_path, ec);

    home_dir_path = scratch_dir_path / "home";
    std::filesystem::create_directories(home_dir_path, ec);

    saved_home = readEnv("HOME");
    saved_userprofile = readEnv("USERPROFILE");
    saved_override = readEnv("AURORA2_GENERATED_DEVICE_DIR");
    // both, because userHomeDirPath() reads USERPROFILE on Windows and HOME elsewhere
    setEnv("HOME", home_dir_path.string());
    setEnv("USERPROFILE", home_dir_path.string());
    unsetEnv("AURORA2_GENERATED_DEVICE_DIR");
  }

  void TearDown() override {
    saved_home ? setEnv("HOME", *saved_home) : unsetEnv("HOME");
    saved_userprofile ? setEnv("USERPROFILE", *saved_userprofile) : unsetEnv("USERPROFILE");
    saved_override ? setEnv("AURORA2_GENERATED_DEVICE_DIR", *saved_override)
                   : unsetEnv("AURORA2_GENERATED_DEVICE_DIR");

    std::error_code ec;
    std::filesystem::permissions(source_node_dir_path, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::add, ec);
    std::filesystem::remove_all(scratch_dir_path, ec);
  }

  GeneratedDeviceDestination claim(std::uintmax_t required_bytes = 0) {
    return claimGeneratedDeviceDir(source_device_dir_path, working_dir_path, kFamily, kFoundry,
                                   kNode, kGeneratedDevice, required_bytes);
  }

  void makeSourceRootReadOnly() {
    std::error_code ec;
    std::filesystem::permissions(source_node_dir_path,
                                 std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_exec,
                                 std::filesystem::perm_options::replace, ec);
  }

  static std::filesystem::path canonical(const std::filesystem::path& p) {
    std::error_code ec;
    const std::filesystem::path c = std::filesystem::weakly_canonical(p, ec);
    return ec ? p : c;
  }

  std::filesystem::path scratch_dir_path;
  std::filesystem::path source_node_dir_path;
  std::filesystem::path source_device_dir_path;
  std::filesystem::path working_dir_path;
  std::filesystem::path home_dir_path;
  std::optional<std::string> saved_home;
  std::optional<std::string> saved_userprofile;
  std::optional<std::string> saved_override;
};

// The guarantee that nothing changes where the write already worked.
TEST_F(GeneratedDevicePath, WritableSourceRootIsUsedAndNotRegistered) {
  const GeneratedDeviceDestination destination = claim();

  EXPECT_EQ(destination.device_dir_path, canonical(source_node_dir_path / kGeneratedDevice));
  EXPECT_TRUE(destination.root_dir_path.empty());
  EXPECT_TRUE(destination.error.empty());
  EXPECT_TRUE(std::filesystem::is_directory(destination.device_dir_path));
}

// The claim marker is what lets a failed copy tell its own tree from another run's.
TEST_F(GeneratedDevicePath, ClaimDropsAnIncompleteMarker) {
  const GeneratedDeviceDestination destination = claim();
  ASSERT_FALSE(destination.device_dir_path.empty());

  EXPECT_TRUE(std::filesystem::exists(
      generatedDeviceClaimMarkerPath(destination.device_dir_path)));
}

// Nothing may be left beside the claimed directory: the node dir is enumerated by the
// featuretests, and any extra entry is counted.
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

TEST_F(GeneratedDevicePath, ReadOnlySourceRootFallsBackToHome) {
  makeSourceRootReadOnly();
  if (!readOnlyDirsAreEnforced(source_node_dir_path)) {
    GTEST_SKIP() << "this platform/user ignores a read-only directory";
  }

  const GeneratedDeviceDestination destination = claim();

  EXPECT_EQ(destination.device_dir_path,
            canonical(home_dir_path / "aurora_devices" / kFamily / kFoundry / kNode /
                      kGeneratedDevice));
  EXPECT_EQ(destination.root_dir_path, canonical(home_dir_path / "aurora_devices"));
  EXPECT_TRUE(std::filesystem::is_directory(destination.device_dir_path));
}

TEST_F(GeneratedDevicePath, OverrideWinsOverWritableSourceRootAndIsCreated) {
  const std::filesystem::path override_root_dir_path = scratch_dir_path / "elsewhere";
  setEnv("AURORA2_GENERATED_DEVICE_DIR", override_root_dir_path.string());

  const GeneratedDeviceDestination destination = claim();

  EXPECT_EQ(destination.device_dir_path,
            canonical(override_root_dir_path / kFamily / kFoundry / kNode / kGeneratedDevice));
  EXPECT_EQ(destination.root_dir_path, canonical(override_root_dir_path));
}

// A trailing slash must not change the root that is reported: it is compared against the
// canonicalised roots QLDeviceManager hands out, and a mismatch there made the caller
// report the device it had just written as shadowing itself.
TEST_F(GeneratedDevicePath, OverrideWithTrailingSlashYieldsACanonicalRoot) {
  const std::filesystem::path override_root_dir_path = scratch_dir_path / "elsewhere";
  setEnv("AURORA2_GENERATED_DEVICE_DIR", override_root_dir_path.string() + "/");

  const GeneratedDeviceDestination destination = claim();

  EXPECT_EQ(destination.root_dir_path, canonical(override_root_dir_path));
  EXPECT_EQ(destination.root_dir_path.string(), override_root_dir_path.string())
      << "trailing slash survived into the reported root";
}

// A mistyped override must not cost a run whose pack already succeeded.
TEST_F(GeneratedDevicePath, OverridePointingAtAFileIsWarnedAboutAndSkipped) {
  const std::filesystem::path override_file_path = scratch_dir_path / "not-a-dir";
  { std::ofstream out(override_file_path.string()); out << "x\n"; }
  setEnv("AURORA2_GENERATED_DEVICE_DIR", override_file_path.string());

  const GeneratedDeviceDestination destination = claim();

  EXPECT_EQ(destination.device_dir_path, canonical(source_node_dir_path / kGeneratedDevice));
  EXPECT_TRUE(destination.error.empty());
  EXPECT_FALSE(destination.warnings.empty());
}

// The runaway self-copy: nothing may be created inside the package being copied.
TEST_F(GeneratedDevicePath, OverrideInsideTheSourceDeviceIsRefused) {
  setEnv("AURORA2_GENERATED_DEVICE_DIR", source_device_dir_path.string());

  const GeneratedDeviceDestination destination = claim();

  EXPECT_EQ(destination.device_dir_path, canonical(source_node_dir_path / kGeneratedDevice));
  EXPECT_TRUE(std::filesystem::is_empty(source_device_dir_path))
      << "the refused candidate still created something inside the source package";
}

TEST_F(GeneratedDevicePath, FallsBackToWorkingDirectoryWhenNoHomeAndSourceIsReadOnly) {
  makeSourceRootReadOnly();
  if (!readOnlyDirsAreEnforced(source_node_dir_path)) {
    GTEST_SKIP() << "this platform/user ignores a read-only directory";
  }
  unsetEnv("HOME");
  unsetEnv("USERPROFILE");

  const GeneratedDeviceDestination destination = claim();

  EXPECT_EQ(destination.device_dir_path,
            canonical(working_dir_path / "aurora_devices" / kFamily / kFoundry / kNode /
                      kGeneratedDevice));
  EXPECT_TRUE(destination.error.empty());
}

TEST_F(GeneratedDevicePath, ExistingGeneratedDirectoryIsReplacedAndReported) {
  const std::filesystem::path existing_dir_path = source_node_dir_path / kGeneratedDevice;
  std::filesystem::create_directories(existing_dir_path / "stale");

  const GeneratedDeviceDestination destination = claim();

  EXPECT_EQ(destination.device_dir_path, canonical(existing_dir_path));
  EXPECT_TRUE(destination.replaced_existing);
  EXPECT_FALSE(std::filesystem::exists(existing_dir_path / "stale"));
}

// Every candidate is short here, because they all share one filesystem in a test - so this
// pins the reporting, not the fall-through. The fall-through on a rejected candidate is
// covered by ReadOnlySourceRootFallsBackToHome, which rejects for a reason a test can aim
// at one candidate.
TEST_F(GeneratedDevicePath, NoCandidateWithRoomIsReportedNotThrown) {
  const GeneratedDeviceDestination destination = claim(~std::uintmax_t(0) / 2);

  EXPECT_TRUE(destination.device_dir_path.empty());
  ASSERT_FALSE(destination.error.empty());
  EXPECT_NE(destination.error.find("not enough space"), std::string::npos)
      << "the error must say why each candidate was rejected: " << destination.error;
}

// A rejected candidate must not leave a phantom device root behind in the user's home or
// project directory.
TEST_F(GeneratedDevicePath, RejectedCandidatesLeaveNoEmptyTreeBehind) {
  const GeneratedDeviceDestination destination = claim(~std::uintmax_t(0) / 2);
  ASSERT_TRUE(destination.device_dir_path.empty());

  EXPECT_FALSE(std::filesystem::exists(home_dir_path / "aurora_devices"))
      << "an empty device-root skeleton was left in the home directory";
  EXPECT_FALSE(std::filesystem::exists(working_dir_path / "aurora_devices"))
      << "an empty device-root skeleton was left in the working directory";
}

TEST(GeneratedDevicePathHelpers, ExpandLeadingTildeUsesHomeAndLeavesOtherPathsAlone) {
  const std::optional<std::string> saved = readEnv("HOME");
  setEnv("HOME", "/home/someone");

  EXPECT_EQ(expandLeadingTilde("~/devs"), std::filesystem::path("/home/someone/devs"));
  EXPECT_EQ(expandLeadingTilde("/abs/devs"), std::filesystem::path("/abs/devs"));
  EXPECT_EQ(expandLeadingTilde("~user/devs"), std::filesystem::path("~user/devs"));
  EXPECT_EQ(expandLeadingTilde("relative/devs"), std::filesystem::path("relative/devs"));

  saved ? setEnv("HOME", *saved) : unsetEnv("HOME");
}

TEST(GeneratedDevicePathHelpers, PathIsInsideTreatsAnEmptyAncestorAsContainingNothing) {
  // real directories: pathIsInside resolves the candidate against what exists on disk,
  // so imaginary paths would collapse to '/' and assert nothing.
  std::error_code ec;
  const std::filesystem::path base =
      std::filesystem::temp_directory_path(ec) / "foedag_qldevicepath_inside";
  std::filesystem::remove_all(base, ec);
  std::filesystem::create_directories(base / "a" / "b" / "c", ec);
  std::filesystem::create_directories(base / "a" / "bc", ec);

  EXPECT_FALSE(pathIsInside(base / "a" / "b" / "c", std::filesystem::path()));
  EXPECT_TRUE(pathIsInside(base / "a" / "b" / "c", base / "a" / "b"));
  EXPECT_TRUE(pathIsInside(base / "a" / "b", base / "a" / "b"));
  EXPECT_FALSE(pathIsInside(base / "a" / "bc", base / "a" / "b"));
  // a target that does not exist yet still resolves against its existing parent
  EXPECT_TRUE(pathIsInside(base / "a" / "b" / "not-created-yet", base / "a" / "b"));

  std::filesystem::remove_all(base, ec);
}

TEST(GeneratedDevicePathHelpers, DirectoryContentSizeCountsFileBytes) {
  std::error_code ec;
  const std::filesystem::path dir = std::filesystem::temp_directory_path(ec) /
                                    "foedag_qldevicepath_size";
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir / "sub", ec);
  { std::ofstream out((dir / "sub" / "a.bin").string()); out << std::string(1024, 'x'); }

  EXPECT_EQ(directoryContentSize(dir), 1024u);
  EXPECT_EQ(directoryContentSize(dir / "does-not-exist"), 0u);

  std::filesystem::remove_all(dir, ec);
}

// An unreadable subtree must cost only its own bytes, not every sibling after it: a
// truncated total silently weakens the space check instead of disabling it.
TEST(GeneratedDevicePathHelpers, DirectoryContentSizeSkipsUnreadableSubtrees) {
#ifndef _WIN32
  if (geteuid() == 0) {
    GTEST_SKIP() << "root ignores 0000";
  }
  std::error_code ec;
  const std::filesystem::path dir = std::filesystem::temp_directory_path(ec) /
                                    "foedag_qldevicepath_denied";
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir / "a_locked", ec);
  std::filesystem::create_directories(dir / "z_readable", ec);
  { std::ofstream out((dir / "z_readable" / "big.bin").string()); out << std::string(2048, 'x'); }
  std::filesystem::permissions(dir / "a_locked", std::filesystem::perms::none,
                               std::filesystem::perm_options::replace, ec);

  EXPECT_EQ(directoryContentSize(dir), 2048u)
      << "the readable sibling was skipped along with the unreadable one";

  std::filesystem::permissions(dir / "a_locked", std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::add, ec);
  std::filesystem::remove_all(dir, ec);
#else
  GTEST_SKIP() << "POSIX permissions only";
#endif
}

}  // namespace
