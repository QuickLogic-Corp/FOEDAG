#include "Compiler/QLDevicePath.h"

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

#include "gtest/gtest.h"

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

}  // namespace
