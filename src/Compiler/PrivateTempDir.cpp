#include "Compiler/PrivateTempDir.h"

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <random>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace FOEDAG {

namespace {

const std::string kPrefix = "aurora-private-";

std::mutex& dirMutex() {
  static std::mutex m;
  return m;
}

fs::path& ownDir() {
  static fs::path dir;
  return dir;
}

std::string randomTag() {
  static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
  static std::mt19937_64 gen{std::random_device{}()};
  std::uniform_int_distribution<int> pick(0, sizeof(alphabet) - 2);
  std::string tag(6, 'x');
  for (char& c : tag) c = alphabet[pick(gen)];
  return tag;
}

bool processAlive(long pid) {
  if (pid <= 0) return false;
#ifdef _WIN32
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
  if (h == nullptr) return false;
  DWORD code = 0;
  const BOOL ok = GetExitCodeProcess(h, &code);
  CloseHandle(h);
  return ok && code == STILL_ACTIVE;
#else
  // EPERM means it exists but belongs to someone else: not ours to sweep.
  return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
#endif
}

bool ownedByMe(const fs::path& dir) {
#ifdef _WIN32
  (void)dir;  // the per-user %TEMP% holds only this user's directories
  return true;
#else
  struct stat st {};
  return ::lstat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == ::getuid();
#endif
}

}  // namespace

long PrivateTempDir::currentPid() {
#ifdef _WIN32
  return static_cast<long>(_getpid());
#else
  return static_cast<long>(::getpid());
#endif
}

std::string PrivateTempDir::dirName(long pid, const std::string& tag) {
  return kPrefix + std::to_string(pid) + "-" + tag;
}

long PrivateTempDir::ownerPid(const std::string& name) {
  if (name.rfind(kPrefix, 0) != 0) return -1;
  const std::string rest = name.substr(kPrefix.size());
  const size_t dash = rest.find('-');
  if (dash == 0 || dash == std::string::npos) return -1;
  long pid = 0;
  for (size_t i = 0; i < dash; ++i) {
    if (rest[i] < '0' || rest[i] > '9') return -1;
    pid = pid * 10 + (rest[i] - '0');
  }
  return pid;
}

fs::path PrivateTempDir::createIn(const fs::path& base) {
  for (int attempt = 0; attempt < 100; ++attempt) {
    const fs::path dir = base / dirName(currentPid(), randomTag());
#ifdef _WIN32
    if (CreateDirectoryW(dir.wstring().c_str(), nullptr)) return dir;
    if (GetLastError() != ERROR_ALREADY_EXISTS) break;
#else
    // mkdir with 0700 is atomic: the directory is never visible with wider rights.
    if (::mkdir(dir.c_str(), 0700) == 0) return dir;
    if (errno != EEXIST) break;
#endif
  }
  return {};
}

fs::path PrivateTempDir::path() {
  std::lock_guard<std::mutex> lock(dirMutex());
  fs::path& dir = ownDir();
  if (dir.empty() || !fs::is_directory(dir)) {
    std::error_code ec;
    dir = createIn(fs::temp_directory_path(ec));
    // Batch runs end in exit(), which skips the compiler's destructor.
    static const bool registered = std::atexit([] { PrivateTempDir::removeOwn(); }) == 0;
    (void)registered;
  }
  return dir;
}

fs::path PrivateTempDir::newFileIn(const fs::path& dir) {
  static std::atomic<unsigned long> counter{0};
  for (;;) {
    const fs::path f = dir / ("file" + std::to_string(++counter) + randomTag());
    std::error_code ec;
    if (!fs::exists(f, ec)) return f;
  }
}

fs::path PrivateTempDir::newFile() {
  const fs::path dir = path();
  return dir.empty() ? fs::path{} : newFileIn(dir);
}

bool PrivateTempDir::writePrivate(const fs::path& file, const std::string& data) {
#ifdef _WIN32
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
  out.close();
  return out.good();
#else
  const int fd = ::open(file.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  if (fd < 0) return false;
  // An existing file keeps its old mode through O_CREAT; narrow it before writing.
  bool ok = ::fchmod(fd, 0600) == 0;
  size_t done = 0;
  while (ok && done < data.size()) {
    const ssize_t n = ::write(fd, data.data() + done, data.size() - done);
    if (n < 0 && errno == EINTR) continue;
    ok = n > 0;
    if (ok) done += static_cast<size_t>(n);
  }
  return ::close(fd) == 0 && ok;
#endif
}

int PrivateTempDir::sweepStale(const fs::path& base) {
  int removed = 0;
  std::error_code ec;
  for (fs::directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
    const fs::path dir = it->path();
    const long pid = ownerPid(dir.filename().string());
    if (pid < 0 || pid == currentPid() || processAlive(pid) || !ownedByMe(dir)) continue;
    std::error_code rec;
    if (fs::remove_all(dir, rec) != static_cast<std::uintmax_t>(-1) && !rec) ++removed;
  }
  return removed;
}

void PrivateTempDir::removeOwn() {
  std::lock_guard<std::mutex> lock(dirMutex());
  fs::path& dir = ownDir();
  if (!dir.empty()) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    dir.clear();
  }
}

}  // namespace FOEDAG
