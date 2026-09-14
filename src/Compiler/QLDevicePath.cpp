#include "QLDevicePath.h"

#include <cstdlib>  // std::getenv

namespace FOEDAG {

std::filesystem::path expandLeadingTilde(const std::filesystem::path& input_path) {

  const std::string input_string = input_path.string();
  if(input_string.empty() || input_string[0] != '~') {
    return input_path;
  }
  // '~user/...' -- not ours to resolve; leave it untouched rather than guess.
  if(input_string.size() > 1 && input_string[1] != '/' && input_string[1] != '\\') {
    return input_path;
  }

  // HOME first, USERPROFILE only as a fallback. MSYS2 sets HOME to a POSIX-style
  // '/home/<user>', which is what AURORA2_DEVICE_DATA_PATH's ':'-separated parsing
  // assumes; taking USERPROFILE first would hand back 'C:\Users\<user>' and split on
  // the drive colon.
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

  std::error_code ec;

  // resolve as much of the candidate as exists: the target dir may not have been created yet.
  std::filesystem::path candidate_existing = candidate;
  while(!candidate_existing.empty() && !std::filesystem::exists(candidate_existing, ec)) {
    std::filesystem::path parent = candidate_existing.parent_path();
    if(parent == candidate_existing) {
      break;
    }
    candidate_existing = parent;
  }

  std::filesystem::path candidate_c = std::filesystem::weakly_canonical(candidate_existing, ec);
  if(ec) { candidate_c = candidate_existing; ec.clear(); }
  std::filesystem::path ancestor_c = std::filesystem::weakly_canonical(ancestor, ec);
  if(ec) { ancestor_c = ancestor; ec.clear(); }

  auto candidate_it = candidate_c.begin();
  auto ancestor_it = ancestor_c.begin();
  for(; ancestor_it != ancestor_c.end(); ++ancestor_it, ++candidate_it) {
    if(candidate_it == candidate_c.end() || *candidate_it != *ancestor_it) {
      return false;
    }
  }
  return true;
}


std::uintmax_t directoryContentSize(const std::filesystem::path& dir_path) {

  std::error_code ec;
  std::uintmax_t total = 0;

  std::filesystem::recursive_directory_iterator it(dir_path, ec);
  if(ec) {
    return 0;
  }
  const std::filesystem::recursive_directory_iterator end;

  for(; it != end; it.increment(ec)) {
    if(ec) {
      break;
    }
    // is_regular_file, not is_directory: directory entries carry their own block size,
    // and symlinks are sized by their target when the copy dereferences them.
    if(it->is_regular_file(ec) && !ec) {
      const std::uintmax_t size = it->file_size(ec);
      if(!ec) {
        total += size;
      }
    }
    ec.clear();
  }

  return total;
}


namespace {

// Free space on the filesystem holding 'dir_path'. 0 when it cannot be determined, which
// callers treat as "unknown, do not block on it".
std::uintmax_t availableSpace(const std::filesystem::path& dir_path) {

  std::error_code ec;
  const std::filesystem::space_info info = std::filesystem::space(dir_path, ec);
  if(ec) {
    return 0;
  }
  return info.available;
}


// One place to try. 'root_dir_path' empty means "the source device's own root", which the
// caller must not register.
struct Candidate {
  std::filesystem::path root_dir_path;
  std::filesystem::path parent_dir_path;
  std::string description;
};


// Try to claim '<parent>/<devicename>', creating the parent if needed.
// Returns the created directory, or empty with 'why' set.
std::filesystem::path claimIn(const std::filesystem::path& parent_dir_path,
                              const std::string& devicename,
                              const std::filesystem::path& source_device_dir_path,
                              std::uintmax_t required_bytes,
                              bool& replaced_existing,
                              std::string& why) {

  std::error_code ec;

  const std::filesystem::path device_dir_path = parent_dir_path / devicename;

  // Never let the target land inside the package being copied: std::filesystem::copy
  // would then walk 'from' while writing into its own descendant and recurse until the
  // filesystem or the path length gives out, inside the installed device package.
  if(pathIsInside(device_dir_path, source_device_dir_path)) {
    why = "it is inside the source device package";
    return {};
  }

  std::filesystem::create_directories(parent_dir_path, ec);
  if(ec) {
    why = ec.message();
    return {};
  }

  if(required_bytes > 0) {
    const std::uintmax_t available = availableSpace(parent_dir_path);
    // 0 means "could not tell" - do not block on it. Note this is filesystem free space,
    // not a per-user quota, so it narrows the window rather than closing it.
    if(available > 0 && available < required_bytes) {
      why = "not enough space: needs " + std::to_string(required_bytes / (1024 * 1024)) +
            " MB, " + std::to_string(available / (1024 * 1024)) + " MB free";
      return {};
    }
  }

  // A directory already here is this project's own previous generation - the devicename
  // carries a per-project token - so replace it, which is what re-running has always done.
  if(std::filesystem::exists(device_dir_path, ec)) {
    std::filesystem::remove_all(device_dir_path, ec);
    if(ec) {
      why = "could not replace the existing directory: " + ec.message();
      return {};
    }
    ec.clear();
    replaced_existing = true;
  }

  // Creating the real target IS the writability test. No portable predicate exists:
  // POSIX access(W_OK) misreports NFS root-squash and ACLs, Windows _waccess ignores
  // ACLs altogether. Creating the directory we actually need avoids a probe that would
  // litter the device root and be counted by anything enumerating it.
  if(!std::filesystem::create_directory(device_dir_path, ec) || ec) {
    why = ec ? ec.message() : std::string("could not create the directory");
    return {};
  }

  return device_dir_path;
}

}  // namespace


GeneratedDeviceDestination claimGeneratedDeviceDir(
    const std::filesystem::path& source_device_dir_path,
    const std::filesystem::path& working_dir_path,
    const std::string& family,
    const std::string& foundry,
    const std::string& node,
    const std::string& devicename,
    std::uintmax_t required_bytes) {

  GeneratedDeviceDestination destination;

  auto parent_in_root = [&](const std::filesystem::path& root_dir_path) {
    return root_dir_path / family / foundry / node;
  };

  std::vector<Candidate> candidate_list;

  // [1] explicit override. Tilde-expanded and made absolute: by the time packing runs the
  //     process working directory is the project directory, so a relative value would
  //     resolve somewhere the user never typed.
  const char* const env_dir_str = std::getenv("AURORA2_GENERATED_DEVICE_DIR");
  if(env_dir_str != nullptr && *env_dir_str != '\0') {

    std::error_code ec;
    std::filesystem::path env_root_dir_path =
        std::filesystem::weakly_canonical(expandLeadingTilde(std::filesystem::path(env_dir_str)), ec);
    if(ec) {
      env_root_dir_path = expandLeadingTilde(std::filesystem::path(env_dir_str));
    }

    candidate_list.push_back({env_root_dir_path, parent_in_root(env_root_dir_path),
                              "AURORA2_GENERATED_DEVICE_DIR"});
  }

  // [2] the source device's own root. Empty root_dir_path: already a known device root,
  //     so the caller must not register it.
  candidate_list.push_back({std::filesystem::path(), source_device_dir_path.parent_path(),
                            "the device's own root"});

  // [3] ~/aurora_devices
  const std::filesystem::path home_devices_dir_path =
      expandLeadingTilde(std::filesystem::path("~/aurora_devices"));
  if(home_devices_dir_path != std::filesystem::path("~/aurora_devices")) {   // i.e. a home was found
    candidate_list.push_back({home_devices_dir_path, parent_in_root(home_devices_dir_path),
                              "~/aurora_devices"});
  }

  // [4] the project's working directory. Last because it scatters a device package per
  //     project, but it is writable by construction and is never swept.
  if(!working_dir_path.empty()) {
    const std::filesystem::path working_root_dir_path = working_dir_path / "aurora_devices";
    candidate_list.push_back({working_root_dir_path, parent_in_root(working_root_dir_path),
                              "the project's working directory"});
  }

  for(const Candidate& candidate : candidate_list) {

    std::string why;
    bool replaced_existing = false;
    const std::filesystem::path device_dir_path =
        claimIn(candidate.parent_dir_path, devicename, source_device_dir_path, required_bytes,
                replaced_existing, why);

    if(!device_dir_path.empty()) {
      destination.device_dir_path = device_dir_path;
      destination.root_dir_path = candidate.root_dir_path;
      destination.replaced_existing = replaced_existing;
      return destination;
    }

    // Only worth a warning when the user asked for that location, or when the default
    // one was passed over - the later fallbacks being unusable is not news on its own.
    if(candidate.description == "AURORA2_GENERATED_DEVICE_DIR" ||
       candidate.description == "the device's own root") {
      destination.warnings.push_back("Cannot write the generated device to " +
                                     candidate.description + " (" +
                                     candidate.parent_dir_path.string() + "): " + why);
    }
  }

  destination.error = "No writable location for the generated device '" + devicename +
                      "'. Set AURORA2_GENERATED_DEVICE_DIR to a writable directory.";
  return destination;
}

}  // namespace FOEDAG
