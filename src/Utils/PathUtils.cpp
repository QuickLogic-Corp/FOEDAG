#include "PathUtils.h"

#if defined(_MSC_VER)
#include <direct.h>
#define PATH_MAX _MAX_PATH
#else
#include <sys/param.h>
#include <unistd.h>
#endif

#include <iostream>
#include <sstream>

namespace FOEDAG {

namespace {

bool getFullPath(const std::filesystem::path& path,
                 std::filesystem::path* result) {
  std::error_code ec;
  std::filesystem::path fullPath = std::filesystem::canonical(path, ec);
  bool found = (!ec && std::filesystem::is_regular_file(fullPath));
  if (result != nullptr) {
    *result = found ? fullPath : path;
  }
  return found;
}

// Try to find the full absolute path of the program currently running.
static std::filesystem::path GetProgramNameAbsolutePath(const char* progname) {
#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__CYGWIN__)
  const char PATH_DELIMITER = ';';
#else
  char buf[PATH_MAX];
  // If the executable is invoked with a path, we can extract it from there,
  // otherwise, we use some operating system trick to find that path:
  // In Linux, the current running binary is symbolically linked from
  // /proc/self/exe which we can resolve.
  // It won't resolve anything on other platforms, but doesnt harm either.
  for (const char* testpath : {progname, "/proc/self/exe"}) {
    const char* const program_name = realpath(testpath, buf);
    if (program_name != nullptr) return program_name;
  }
  const char PATH_DELIMITER = ':';
#endif

  // Still not found, let's go through the $PATH and see what comes up first.
  const char* const path = std::getenv("PATH");
  if (path != nullptr) {
    std::stringstream search_path(path);
    std::string path_element;
    std::filesystem::path program_path;
    while (std::getline(search_path, path_element, PATH_DELIMITER)) {
      const std::filesystem::path testpath =
          path_element / std::filesystem::path(progname);
      if (getFullPath(testpath, &program_path)) {
        return program_path;
      }
#if _WIN32
// additional search for cmd.exe, we need to be specific about the program name + extension!
// TODO: why don't we use the Qt method to get the path definitively?
      else {
        std::string progname_exe_str = std::string(progname) + std::string(".exe");
        const std::filesystem::path testpath_exe =
          path_element / std::filesystem::path(progname_exe_str);
        if (getFullPath(testpath_exe, &program_path)) {
          return program_path;
        }
      }
#endif // #if _WIN32
    }
  }

  return progname;  // Didn't find anything, return progname as-is.
}

} // namespace

void PathUtils::init(const char* progname) 
{
  if (!m_isInitilized) {
    std::filesystem::path exePath = GetProgramNameAbsolutePath(progname);
    m_exeDir = exePath.parent_path();
    m_installDir = m_exeDir.parent_path();

    m_isInitilized = true;
  }
}

const std::filesystem::path& PathUtils::exeDir() const 
{ 
  if (!m_isInitilized) {
    std::cerr << "attempt to access not initialized exeDir, requested path will be empty" << std::endl;
  }
  return m_exeDir; 
}
const std::filesystem::path& PathUtils::installDir() const 
{ 
  if (!m_isInitilized) {
    std::cerr << "attempt to access not initialized installDir, requested path will be empty" << std::endl;
  }
  return m_installDir; 
}

}  // namespace FOEDAG