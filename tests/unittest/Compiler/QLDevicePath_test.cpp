#include "Compiler/QLDevicePath.h"

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

#include "gtest/gtest.h"

#ifdef _WIN32
#include <windows.h>
#endif

using namespace FOEDAG;

namespace {

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

TEST(GeneratedDevicePathHelpers, PathIsInsideSeesThroughMissingDirsAndRelativePaths) {
  std::error_code ec;
  const std::filesystem::path base =
      std::filesystem::temp_directory_path(ec) / "foedag_qldevicepath_missing";
  std::filesystem::remove_all(base, ec);
  std::filesystem::create_directories(base / "install" / "share", ec);
  std::filesystem::create_directories(base / "outside", ec);

  // create_directories makes 'nx', then '../..' lands in install
  EXPECT_TRUE(pathIsInside(base / "outside" / "nx" / ".." / ".." / "install" / "x",
                           base / "install"));
  EXPECT_FALSE(pathIsInside(base / "outside" / "nx" / ".." / "y", base / "install"));

  // a relative target that does not exist yet is relative to the cwd
  const std::filesystem::path saved_cwd = std::filesystem::current_path(ec);
  std::filesystem::current_path(base / "install", ec);
  EXPECT_TRUE(pathIsInside("newdir/x", base / "install"));
  std::filesystem::current_path(base / "outside", ec);
  EXPECT_FALSE(pathIsInside("newdir/x", base / "install"));
  std::filesystem::current_path(saved_cwd, ec);

  std::filesystem::remove_all(base, ec);
}

// libstdc++ cannot create Windows symlinks, so ask the OS directly there.
bool makeDirectorySymlink(const std::filesystem::path& target,
                          const std::filesystem::path& link) {
#ifdef _WIN32
  return CreateSymbolicLinkW(link.wstring().c_str(), target.wstring().c_str(),
                             SYMBOLIC_LINK_FLAG_DIRECTORY |
                                 SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0;
#else
  std::error_code ec;
  std::filesystem::create_directory_symlink(target, link, ec);
  return !ec;
#endif
}

// remove_all cannot see a Windows symlink, so a leftover link would skip later runs.
void removeDirectorySymlink(const std::filesystem::path& link) {
#ifdef _WIN32
  RemoveDirectoryW(link.wstring().c_str());
#else
  std::error_code ec;
  std::filesystem::remove(link, ec);
#endif
}

TEST(GeneratedDevicePathHelpers, PathIsInsideFollowsASymlinkIntoTheAncestor) {
  std::error_code ec;
  const std::filesystem::path base =
      std::filesystem::temp_directory_path(ec) / "foedag_qldevicepath_symlink";
  removeDirectorySymlink(base / "outside" / "link");
  std::filesystem::remove_all(base, ec);
  std::filesystem::create_directories(base / "install" / "device_data", ec);
  std::filesystem::create_directories(base / "outside", ec);
  if(!makeDirectorySymlink(base / "install" / "device_data", base / "outside" / "link")) {
    std::filesystem::remove_all(base, ec);
    GTEST_SKIP() << "this host cannot create a directory symlink";
  }

  EXPECT_TRUE(pathIsInside(base / "outside" / "link", base / "install"));
  EXPECT_TRUE(pathIsInside(base / "outside" / "link" / "not-created-yet", base / "install"));
  EXPECT_FALSE(pathIsInside(base / "outside", base / "install"));
  // a '..' that cancels a missing dir steps back onto the link, which must still be followed
  std::filesystem::create_directories(base / "outside" / "real", ec);
  EXPECT_TRUE(pathIsInside(base / "outside" / "nx" / ".." / "link" / "x", base / "install"));
  EXPECT_TRUE(pathIsInside(base / "outside" / "real" / "nx" / ".." / ".." / "link" / "y",
                           base / "install"));
  EXPECT_FALSE(pathIsInside(base / "outside" / "nx" / ".." / "real" / "z", base / "install"));
  // POSIX follows the link before '..' (lands in install); Win32 collapses '..' as text first.
#ifdef _WIN32
  EXPECT_FALSE(pathIsInside(base / "outside" / "link" / ".." / "new", base / "install"));
#else
  EXPECT_TRUE(pathIsInside(base / "outside" / "link" / ".." / "new", base / "install"));
#endif

  removeDirectorySymlink(base / "outside" / "link");
  std::filesystem::remove_all(base, ec);
}

}  // namespace
