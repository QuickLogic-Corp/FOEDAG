#ifndef QLDEVICEPATH_H
#define QLDEVICEPATH_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace FOEDAG {

// '~/...' -> '<home>/...'. Only a LEADING tilde is expanded; '~user' is left alone,
// as is a path with no tilde. Returns the input unchanged when there is no usable home.
//
// Needed because a tilde is the shell's, not the filesystem's: a value set in CI yaml,
// a systemd unit or a .conf file reaches us literally, and would otherwise create a
// directory actually named '~'.
std::filesystem::path expandLeadingTilde(const std::filesystem::path& input_path);

// true when 'candidate' is inside 'ancestor' (or is it), after resolving symlinks and '..'.
// Resolves as much of 'candidate' as exists, so it answers for a directory not yet created.
bool pathIsInside(const std::filesystem::path& candidate,
                  const std::filesystem::path& ancestor);

// Where a generated device package was placed, and what the caller has to say about it.
struct GeneratedDeviceDestination {

  // The claimed, freshly created device directory. Empty when nothing could be claimed,
  // in which case 'error' says why.
  std::filesystem::path device_dir_path;

  // The device-data ROOT holding it, for registerDeviceRoot(). Empty when the package
  // landed in the source device's own root, which is already a known root.
  std::filesystem::path root_dir_path;

  // A previous package of the same name was removed to make room. The name carries a
  // per-project token, so this is normally this project's own earlier run.
  bool replaced_existing{false};

  // Surfaced by the caller through Message()/ErrorMessage(): this runs below the
  // Compiler, and a diagnostic written to std::cout never reaches the GUI console.
  std::vector<std::string> warnings;
  std::string error;
};

// Claim a directory for the generated device package, creating it.
//
// Takes the first candidate it can actually create the directory in:
//   1. $AURORA2_GENERATED_DEVICE_DIR - explicit override
//   2. the source device's own root  - what a writable installation has always done
//   3. ~/aurora_devices              - the location install_device is documented with
//   4. <working_directory>           - writable by construction, see below
//
// Candidates are tried by ATTEMPTING the directory creation rather than by testing
// permissions first. There is no portable writability predicate: POSIX access(W_OK)
// misreports NFS root-squash and ACLs, and Windows _waccess only looks at the read-only
// attribute and ignores ACLs entirely. The real operation is the only honest test, and
// it is cheap next to the package copy that follows.
//
// Candidate 4 cannot realistically fail: the flow has been writing to the project
// directory for the whole run. That is what keeps a last resort off the system temp
// directory - the generated package is a deliverable, and Linux /tmp, macOS $TMPDIR and
// Windows %TEMP% are all swept.
//
// 'required_bytes' is the size of the source package; a candidate without room for it is
// passed over rather than failed on, so a full $HOME falls through instead of aborting a
// run whose pack already succeeded.
GeneratedDeviceDestination claimGeneratedDeviceDir(
    const std::filesystem::path& source_device_dir_path,
    const std::filesystem::path& working_dir_path,
    const std::string& family,
    const std::string& foundry,
    const std::string& node,
    const std::string& devicename,
    std::uintmax_t required_bytes);

// Total size of everything under 'dir_path', 0 if it cannot be walked.
std::uintmax_t directoryContentSize(const std::filesystem::path& dir_path);

}  // namespace FOEDAG

#endif  // QLDEVICEPATH_H
