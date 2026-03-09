#pragma once

#include <filesystem>

namespace FOEDAG {

class PathUtils {
public:
  static PathUtils& instance() {
    static PathUtils instance;
    return instance;
  }

  void init(const char* progname);

  const std::filesystem::path& exeDir() const;
  const std::filesystem::path& installDir() const;

private:
  PathUtils()=default;

  bool m_isInitilized = false;
  std::filesystem::path m_exeDir;
  std::filesystem::path m_installDir;
};

}  // namespace FOEDAG

