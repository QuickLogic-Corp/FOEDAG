#pragma once

#include <filesystem>
#include <string>

namespace FOEDAG {

/// A per-process, user-only directory for decrypted device files (REQ-130 of
/// aurora2's docs/specs/aurora-flow-engine): created 0700 on POSIX (under the
/// per-user %TEMP% on Windows), files in it written owner-only, removed when the
/// compiler is destroyed, and swept at the next start when a process died first.
class PrivateTempDir {
 public:
  /// This process's directory under temp_directory_path(), created on first use.
  static std::filesystem::path path();
  /// A fresh file name inside path(); the file is not created.
  static std::filesystem::path newFile();
  /// Removes this process's directory and everything in it.
  static void removeOwn();

  /// Writes `data` to `file`, readable and writable by the owner only.
  static bool writePrivate(const std::filesystem::path& file, const std::string& data);
  /// Removes directories under `base` that this naming scheme gave to processes of
  /// this user that no longer run. Returns how many were removed.
  static int sweepStale(const std::filesystem::path& base);

  // Building blocks, public for the unit tests.
  static std::filesystem::path createIn(const std::filesystem::path& base);
  static std::filesystem::path newFileIn(const std::filesystem::path& dir);
  static std::string dirName(long pid, const std::string& tag);
  /// The pid encoded in a directory name of this scheme, or -1.
  static long ownerPid(const std::string& name);
  static long currentPid();
};

}  // namespace FOEDAG
