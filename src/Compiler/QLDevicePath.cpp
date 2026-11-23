#include "QLDevicePath.h"

#include <cstdlib>  // std::getenv
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace FOEDAG {

namespace {

// libstdc++ (MinGW) does not see Windows symlinks, so weakly_canonical returns a link
// unresolved; ask the OS for the final path instead.
std::filesystem::path resolveExisting(const std::filesystem::path& path_in) {
  std::error_code ec;
#ifdef _WIN32
  HANDLE handle = CreateFileW(path_in.wstring().c_str(), 0,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if(handle != INVALID_HANDLE_VALUE) {
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = GetFinalPathNameByHandleW(handle, buffer.data(), (DWORD)buffer.size(),
                                             FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if(length >= buffer.size()) {
      buffer.resize(length + 1);
      length = GetFinalPathNameByHandleW(handle, buffer.data(), (DWORD)buffer.size(),
                                         FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    }
    CloseHandle(handle);
    if(length > 0 && length < buffer.size()) {
      buffer.resize(length);
      if(buffer.rfind(L"\\\\?\\UNC\\", 0) == 0) {
        buffer = L"\\\\" + buffer.substr(8);
      } else if(buffer.rfind(L"\\\\?\\", 0) == 0) {
        buffer = buffer.substr(4);
      }
      return std::filesystem::path(buffer);
    }
  }
#endif
  std::filesystem::path resolved = std::filesystem::weakly_canonical(path_in, ec);
  return ec ? path_in : resolved;
}

}  // namespace

std::filesystem::path expandLeadingTilde(const std::filesystem::path& input_path) {

  const std::string input_string = input_path.string();
  if(input_string.empty() || input_string[0] != '~') {
    return input_path;
  }
  // '~user/...' -- not ours to resolve; leave it untouched rather than guess.
  if(input_string.size() > 1 && input_string[1] != '/' && input_string[1] != '\\') {
    return input_path;
  }

  // HOME first: this expands a tilde a USER typed, and their shell would have used HOME.
  const char* home_dir = std::getenv("HOME");
#ifdef _WIN32
  if(home_dir == nullptr) { home_dir = std::getenv("USERPROFILE"); }
#endif
  if(home_dir == nullptr || *home_dir == '\0') {
    return input_path;                       // no home to expand to: leave it as the user typed it
  }

  return std::filesystem::path(home_dir) / input_string.substr(input_string.size() > 1 ? 2 : 1);
}


bool pathIsInside(const std::filesystem::path& candidate,
                  const std::filesystem::path& ancestor) {

  // an empty ancestor would otherwise match everything: its component range is empty, so
  // the loop below never runs and every candidate "contains" it.
  if(ancestor.empty()) {
    return false;
  }

  std::error_code ec;

  // absolute: a relative target is relative to the cwd, which may be inside the installation.
  std::filesystem::path candidate_abs = std::filesystem::absolute(candidate, ec);
  if(ec) { candidate_abs = candidate; ec.clear(); }
#ifdef _WIN32
  // Win32 collapses '..' as text before following links; match it.
  candidate_abs = candidate_abs.lexically_normal();
#endif

  // Walk the target the way create_directories will: resolve each part that exists, so a
  // link anywhere is followed, and keep parts not created yet as text. A '..' cancels a
  // missing part, or else steps up from the resolved path (physical on POSIX).
  std::filesystem::path candidate_c = candidate_abs.root_path();
  std::vector<std::filesystem::path> missing;
  for(const std::filesystem::path& part : candidate_abs.relative_path()) {
    if(part.empty() || part == ".") {
      continue;
    }
    if(part == "..") {
      if(!missing.empty()) {
        missing.pop_back();
      } else {
        candidate_c = resolveExisting(candidate_c / part);
      }
    } else if(missing.empty() && std::filesystem::exists(candidate_c / part, ec)) {
      candidate_c = resolveExisting(candidate_c / part);
    } else {
      missing.push_back(part);
    }
  }
  for(const std::filesystem::path& part : missing) {
    candidate_c /= part;
  }
  // same resolver on both sides, so case and separators match whenever both resolve
  const std::filesystem::path ancestor_c = resolveExisting(ancestor);

  auto candidate_it = candidate_c.begin();
  auto ancestor_it = ancestor_c.begin();
  for(; ancestor_it != ancestor_c.end(); ++ancestor_it, ++candidate_it) {
    if(candidate_it == candidate_c.end() || *candidate_it != *ancestor_it) {
      return false;
    }
  }
  return true;
}

}  // namespace FOEDAG
