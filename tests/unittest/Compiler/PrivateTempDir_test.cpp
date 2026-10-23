#include "Compiler/PrivateTempDir.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include "gtest/gtest.h"

#ifndef _WIN32
#include <spawn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <vector>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
extern char** environ;
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

// The child is a fresh run of this binary, not a fork of it: exit() in a fork of a
// process that has threads (Qt's X connection reader) can block on locks they held.
constexpr const char* kChildEnv = "FOEDAG_PRIVATE_TEMP_DIR_CHILD";
constexpr int kExitedWithHandlers = 3;
constexpr int kDiedWithoutHandlers = 4;

static fs::path thisExecutable() {
#ifdef __APPLE__
  char buf[4096];
  uint32_t size = sizeof(buf);
  return _NSGetExecutablePath(buf, &size) == 0 ? fs::path(buf) : fs::path();
#else
  return fs::read_symlink("/proc/self/exe");
#endif
}

static void childCreatesDirThen(const fs::path& base, bool runExitHandlers) {
  const std::string exe = thisExecutable().string();
  ASSERT_FALSE(exe.empty());
  std::vector<std::string> env;
  for (char** e = environ; *e; ++e) {
    const std::string kv = *e;
    if (kv.rfind("TMPDIR=", 0) != 0 && kv.rfind("QT_QPA_PLATFORM=", 0) != 0) env.push_back(kv);
  }
  env.push_back("TMPDIR=" + base.string());
  env.push_back("QT_QPA_PLATFORM=offscreen");
  env.push_back(std::string(kChildEnv) + "=" + (runExitHandlers ? "exit" : "die"));
  std::vector<char*> envp;
  for (auto& kv : env) envp.push_back(kv.data());
  envp.push_back(nullptr);
  std::string filter = "--gtest_filter=PrivateTempDirChild.*";
  char* argv[] = {const_cast<char*>(exe.c_str()), filter.data(), nullptr};

  pid_t child = 0;
  ASSERT_EQ(::posix_spawn(&child, exe.c_str(), nullptr, nullptr, argv, envp.data()), 0);
  int status = 0;
  ASSERT_EQ(::waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), runExitHandlers ? kExitedWithHandlers : kDiedWithoutHandlers);
}

// Runs only as the child above; the parent checks its exit code, so a skip here
// (exit 0) cannot pass for the real work.
TEST(PrivateTempDirChild, CreatesItsDirectoryThenEnds) {
  const char* how = std::getenv(kChildEnv);
  if (how == nullptr) GTEST_SKIP() << "runs only as the child of the exit tests";
  const fs::path dir = PrivateTempDir::path();
  const char* tmp = std::getenv("TMPDIR");
  if (dir.empty() || tmp == nullptr || dir.parent_path() != fs::path(tmp)) ::_exit(2);
  if (std::string(how) == "exit") std::exit(kExitedWithHandlers);
  ::_exit(kDiedWithoutHandlers);
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
