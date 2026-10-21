#include "Compiler/PrivateTempDir.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include "gtest/gtest.h"

#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace FOEDAG;
namespace fs = std::filesystem;

namespace {

class PrivateTempDirTest : public ::testing::Test {
 protected:
  void SetUp() override {
    base = fs::temp_directory_path() /
           ("PrivateTempDir_test_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
            "_" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
    fs::remove_all(base);
    fs::create_directories(base);
  }
  void TearDown() override { fs::remove_all(base); }
  fs::path base;
};

#ifndef _WIN32
mode_t modeOf(const fs::path& p) {
  struct stat st {};
  EXPECT_EQ(::stat(p.c_str(), &st), 0) << p;
  return st.st_mode & 0777;
}

// A pid that certainly belongs to no running process: a child that has exited and been reaped.
pid_t deadPid() {
  const pid_t child = ::fork();
  if (child == 0) ::_exit(0);
  int status = 0;
  ::waitpid(child, &status, 0);
  return child;
}
#endif

}  // namespace

TEST_F(PrivateTempDirTest, DirectoryIsCreatedForThisProcessOnly) {
  const fs::path dir = PrivateTempDir::createIn(base);
  ASSERT_TRUE(fs::is_directory(dir));
  EXPECT_EQ(dir.parent_path(), base);
  EXPECT_EQ(PrivateTempDir::ownerPid(dir.filename().string()), PrivateTempDir::currentPid());
#ifndef _WIN32
  EXPECT_EQ(modeOf(dir), 0700u);
#endif
}

TEST_F(PrivateTempDirTest, NewFileNamesAreUniqueAndInsideTheDirectory) {
  const fs::path dir = PrivateTempDir::createIn(base);
  std::set<fs::path> names;
  for (int i = 0; i < 100; ++i) {
    const fs::path f = PrivateTempDir::newFileIn(dir);
    EXPECT_EQ(f.parent_path(), dir);
    EXPECT_FALSE(fs::exists(f));
    names.insert(f);
  }
  EXPECT_EQ(names.size(), 100u);
}

TEST_F(PrivateTempDirTest, WrittenFilesAreOwnerOnlyAndHoldTheData) {
  const fs::path dir = PrivateTempDir::createIn(base);
  const fs::path f = PrivateTempDir::newFileIn(dir);
#ifndef _WIN32
  const mode_t old = ::umask(0);  // even a permissive umask must not widen it
#endif
  const bool ok = PrivateTempDir::writePrivate(f, std::string("<architecture>\n", 15));
#ifndef _WIN32
  ::umask(old);
  EXPECT_EQ(modeOf(f), 0600u);
#endif
  ASSERT_TRUE(ok);
  std::ifstream in(f, std::ios::binary);
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  EXPECT_EQ(text, "<architecture>\n");
}

TEST_F(PrivateTempDirTest, WritingOverAnExistingFileKeepsItPrivate) {
  const fs::path f = base / "existing.xml";
  { std::ofstream(f) << "old"; }
#ifndef _WIN32
  ::chmod(f.c_str(), 0644);
#endif
  ASSERT_TRUE(PrivateTempDir::writePrivate(f, "new"));
#ifndef _WIN32
  EXPECT_EQ(modeOf(f), 0600u);
#endif
}

#ifndef _WIN32
TEST_F(PrivateTempDirTest, SweepRemovesOnlyDirectoriesOfDeadProcesses) {
  const fs::path mine = PrivateTempDir::createIn(base);
  const fs::path stale = base / PrivateTempDir::dirName(deadPid(), "abc123");
  fs::create_directories(stale);
  { std::ofstream(stale / "file1") << "<architecture>"; }
  const fs::path unrelated = base / "aurora-something-else";
  fs::create_directories(unrelated);
  const fs::path notADir = base / PrivateTempDir::dirName(deadPid(), "zzz999");
  { std::ofstream(notADir) << "x"; }

  EXPECT_EQ(PrivateTempDir::sweepStale(base), 1);
  EXPECT_FALSE(fs::exists(stale));
  EXPECT_TRUE(fs::exists(mine));
  EXPECT_TRUE(fs::exists(unrelated));
  EXPECT_TRUE(fs::exists(notADir));
}
#endif

#ifndef _WIN32
// `aurora --batch --script` ends with exit(), which runs no destructors; the
// directory must still go. A process that dies without exit handlers leaves it for
// the next start's sweep.
static int privateDirsIn(const fs::path& base) {
  int n = 0;
  for (const auto& e : fs::directory_iterator(base))
    if (PrivateTempDir::ownerPid(e.path().filename().string()) >= 0) ++n;
  return n;
}

static void childCreatesDirThen(const fs::path& base, bool runExitHandlers) {
  const pid_t child = ::fork();
  if (child == 0) {
    ::setenv("TMPDIR", base.c_str(), 1);
    const fs::path dir = PrivateTempDir::path();
    if (dir.empty() || dir.parent_path() != base) ::_exit(2);
    if (runExitHandlers) std::exit(0);
    ::_exit(0);
  }
  int status = 0;
  ::waitpid(child, &status, 0);
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

TEST_F(PrivateTempDirTest, ExitRemovesTheDirectory) {
  childCreatesDirThen(base, /*runExitHandlers*/ true);
  EXPECT_EQ(privateDirsIn(base), 0);
}

TEST_F(PrivateTempDirTest, DeathWithoutExitHandlersLeavesItForTheSweep) {
  childCreatesDirThen(base, /*runExitHandlers*/ false);
  EXPECT_EQ(privateDirsIn(base), 1);
  EXPECT_EQ(PrivateTempDir::sweepStale(base), 1);
  EXPECT_EQ(privateDirsIn(base), 0);
}
#endif

TEST_F(PrivateTempDirTest, SweepOfAMissingBaseIsHarmless) {
  EXPECT_EQ(PrivateTempDir::sweepStale(base / "does-not-exist"), 0);
}

TEST(PrivateTempDirNames, OwnerPidParsesOnlyOurNames) {
  EXPECT_EQ(PrivateTempDir::ownerPid(PrivateTempDir::dirName(4242, "k3y9xq")), 4242);
  EXPECT_EQ(PrivateTempDir::ownerPid("aurora-private-x-1"), -1);
  EXPECT_EQ(PrivateTempDir::ownerPid("file1234"), -1);
  EXPECT_EQ(PrivateTempDir::ownerPid("aurora-private-"), -1);
}
