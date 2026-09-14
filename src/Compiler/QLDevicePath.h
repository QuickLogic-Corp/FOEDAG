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
// For paths a USER typed. A tilde is the shell's, not the filesystem's: a value set in
// CI yaml, a systemd unit or a .conf file reaches us literally and would otherwise
// create a directory actually named '~'.
std::filesystem::path expandLeadingTilde(const std::filesystem::path& input_path);

// The user's home directory, resolved the way the device root registry resolves it -
// USERPROFILE on Windows, HOME elsewhere. Empty when there is none usable.
//
// Deliberately NOT expandLeadingTilde()'s order. That one is HOME-first so a '~' a user
// typed means what their shell would have meant; this one has to agree with
// deviceRootRegistryFilePath(), because a device written under one home and recorded in
// a registry under the other is a device nothing can find. On MSYS2 the difference is
// real: HOME is '/home/<user>', which a native Windows build resolves drive-relative.
std::filesystem::path userHomeDirPath();

// true when 'candidate' is inside 'ancestor' (or is it), after resolving symlinks and '..'.
// Resolves as much of 'candidate' as exists, so it answers for a directory not yet created.
// An empty 'ancestor' contains nothing.
bool pathIsInside(const std::filesystem::path& candidate,
                  const std::filesystem::path& ancestor);

// Where a generated device package was placed, and what the caller has to say about it.
struct GeneratedDeviceDestination {

  // The claimed, freshly created device directory. Empty when nothing could be claimed,
  // in which case 'error' says why. Canonical, so it compares against the canonical
  // paths QLDeviceManager hands out.
  std::filesystem::path device_dir_path;

  // The device-data ROOT holding it, for registerDeviceRoot(). Canonical. Empty when the
  // package landed in the source device's own root, which is already a known root.
  std::filesystem::path root_dir_path;

  // A package of the same name was removed to make room. The name carries a per-project
  // token, so this is normally this project's own earlier run.
  bool replaced_existing{false};

  // Surfaced by the caller through Message()/ErrorMessage(): this runs below the
  // Compiler, and a diagnostic written to std::cout never reaches the GUI console.
  std::vector<std::string> warnings;
  std::string error;
};

// Claim a directory for the generated device package, creating it.
//
// Takes the first candidate it can create the directory in:
//   1. $AURORA2_GENERATED_DEVICE_DIR - explicit override
//   2. the source device's own root  - what a writable installation has always done
//   3. <home>/aurora_devices         - the location install_device is documented with
//   4. <working_directory>           - the project's own area
//
// Candidates are tried by ATTEMPTING the directory creation rather than by testing
// permissions first. There is no portable writability predicate: POSIX access(W_OK)
// misreports NFS root-squash and ACLs, and Windows _waccess only looks at the read-only
// attribute and ignores ACLs entirely. The real operation is the only honest test, and
// it is cheap next to the package copy that follows.
//
// Candidate 4 is last because it is the user's own area rather than a device location,
// and it is normally writable - the flow has been writing to the project directory for
// the whole run. It is not a guarantee: an example run from inside the source package
// puts it inside that package, where it is correctly refused. That is what keeps a last
// resort off the system temp directory, which Linux, macOS and Windows all sweep.
//
// 'required_bytes' is the size of the source package; a candidate without room for it is
// passed over rather than failed on, so a full $HOME falls through instead of aborting a
// run whose pack already succeeded. 0 disables the check.
GeneratedDeviceDestination claimGeneratedDeviceDir(
    const std::filesystem::path& source_device_dir_path,
    const std::filesystem::path& working_dir_path,
    const std::string& family,
    const std::string& foundry,
    const std::string& node,
    const std::string& devicename,
    std::uintmax_t required_bytes);

// Marker this claim drops inside the directory it created, removed by the caller once the
// package is complete. It is what lets a failed copy tell its own partial tree from a
// directory another run has since taken over - the device name separates projects, not
// concurrent runs of one project, so the two can collide.
std::filesystem::path generatedDeviceClaimMarkerPath(
    const std::filesystem::path& device_dir_path);

// Total size of the regular files under 'dir_path'. Unreadable subtrees are skipped, so
// this can under-report; it is a lower bound, used only to pass over a candidate that
// plainly cannot hold the package. 0 when nothing could be walked.
std::uintmax_t directoryContentSize(const std::filesystem::path& dir_path);

}  // namespace FOEDAG

#endif  // QLDEVICEPATH_H
