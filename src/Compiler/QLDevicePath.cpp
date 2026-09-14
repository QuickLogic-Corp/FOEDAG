#include "QLDevicePath.h"

#include <cstdlib>  // std::getenv
#include <fstream>

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


std::filesystem::path userHomeDirPath() {

#ifdef _WIN32
  const char* const home_dir_env_str = std::getenv("USERPROFILE");
#else
  const char* const home_dir_env_str = std::getenv("HOME");
#endif

  if(home_dir_env_str == nullptr || *home_dir_env_str == '\0') {
    return {};
  }
  return std::filesystem::path(home_dir_env_str);
}


bool pathIsInside(const std::filesystem::path& candidate,
                  const std::filesystem::path& ancestor) {

  // an empty ancestor would otherwise match everything: its component range is empty, so
  // the loop below never runs and every candidate "contains" it.
  if(ancestor.empty()) {
    return false;
  }

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

  // skip_permission_denied, and 'continue' rather than 'break', so one unreadable subtree
  // costs its own bytes instead of every sibling after it. A truncated total is worse than
  // none: it silently weakens the space check instead of disabling it.
  std::filesystem::recursive_directory_iterator it(
      dir_path, std::filesystem::directory_options::skip_permission_denied, ec);
  if(ec) {
    return 0;
  }
  const std::filesystem::recursive_directory_iterator end;

  while(it != end) {

    if(it->is_regular_file(ec) && !ec) {
      const std::uintmax_t size = it->file_size(ec);
      if(!ec) {
        total += size;
      }
    }
    ec.clear();

    it.increment(ec);
    if(ec) {
      ec.clear();
      break;   // the iterator is at end() after a failed increment; nothing left to do
    }
  }

  return total;
}


std::filesystem::path generatedDeviceClaimMarkerPath(
    const std::filesystem::path& device_dir_path) {

  return device_dir_path / ".aurora_generated_device_incomplete";
}


namespace {

// std::filesystem::space reports an unobtainable field as uintmax_t(-1), not 0, and a
// genuinely full filesystem reports 0 available. Both have to be told apart: 0 is a real
// answer that must skip the candidate, -1 and an error mean "cannot tell, do not block".
constexpr std::uintmax_t kSpaceUnknown = static_cast<std::uintmax_t>(-1);

std::uintmax_t availableSpace(const std::filesystem::path& dir_path) {

  std::error_code ec;
  const std::filesystem::space_info info = std::filesystem::space(dir_path, ec);
  if(ec) {
    return kSpaceUnknown;
  }
  return info.available;
}


std::filesystem::path canonicalOrSelf(const std::filesystem::path& path) {

  std::error_code ec;
  const std::filesystem::path canonical_path = std::filesystem::weakly_canonical(path, ec);
  return ec ? path : canonical_path;
}


// One place to try. 'root_dir_path' empty means "the source device's own root", which the
// caller must not register.
struct Candidate {
  std::filesystem::path root_dir_path;
  std::filesystem::path parent_dir_path;
  std::string description;
  bool warn_on_failure{false};
};


// Remove directories this claim created and then abandoned, innermost first, stopping at
// the first one that is not empty. Without it a rejected candidate leaves an empty
// '<root>/<family>/<foundry>/<node>' skeleton in the user's home or project directory -
// a phantom device root that was never there before.
void removeCreatedEmptyDirs(std::filesystem::path dir_path,
                            const std::filesystem::path& stop_above_dir_path) {

  std::error_code ec;
  while(!dir_path.empty() && dir_path != stop_above_dir_path) {
    if(!std::filesystem::is_empty(dir_path, ec) || ec) {
      return;
    }
    if(!std::filesystem::remove(dir_path, ec) || ec) {
      return;
    }
    const std::filesystem::path parent = dir_path.parent_path();
    if(parent == dir_path) {
      return;
    }
    dir_path = parent;
  }
}


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
  // Checked first, so nothing is created or deleted inside the source package.
  if(pathIsInside(device_dir_path, source_device_dir_path)) {
    why = "it is inside the source device package";
    return {};
  }

  // remember what already existed, so an abandoned candidate can be tidied up again
  const bool parent_existed = std::filesystem::exists(parent_dir_path, ec);
  ec.clear();

  std::filesystem::create_directories(parent_dir_path, ec);
  if(ec) {
    why = ec.message();
    return {};
  }

  auto give_up = [&](const std::string& reason) -> std::filesystem::path {
    why = reason;
    if(!parent_existed) {
      removeCreatedEmptyDirs(parent_dir_path, std::filesystem::path());
    }
    return {};
  };

  // A directory already here is this project's own previous generation - the devicename
  // carries a per-project token. Remove it BEFORE the space check: it is about to be
  // replaced, so its bytes are available, and checking first would demand room for two
  // copies and push a routine re-run onto a different root.
  if(std::filesystem::exists(device_dir_path, ec)) {
    std::filesystem::remove_all(device_dir_path, ec);
    if(ec) {
      return give_up("could not replace the existing directory: " + ec.message());
    }
    ec.clear();
    replaced_existing = true;
  }

  if(required_bytes > 0) {
    const std::uintmax_t available = availableSpace(parent_dir_path);
    // note this is filesystem free space, not a per-user quota, so it narrows the window
    // rather than closing it.
    if(available != kSpaceUnknown && available < required_bytes) {
      return give_up("not enough space: needs " + std::to_string(required_bytes / (1024 * 1024)) +
                     " MB, " + std::to_string(available / (1024 * 1024)) + " MB free");
    }
  }

  // Creating the real target IS the writability test. No portable predicate exists:
  // POSIX access(W_OK) misreports NFS root-squash and ACLs, Windows _waccess ignores
  // ACLs altogether. Creating the directory we actually need avoids a probe that would
  // litter the device root and be counted by anything enumerating it.
  if(!std::filesystem::create_directory(device_dir_path, ec)) {
    // create_directory returns false with no error when the directory already exists,
    // which after the removal above means another run claimed it in between.
    return give_up(ec ? ec.message()
                      : std::string("another run claimed the same directory"));
  }

  // Drop the marker while we still hold an empty directory we know we created.
  { std::ofstream marker(generatedDeviceClaimMarkerPath(device_dir_path).string()); }

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

    // path(const char*) transcodes on Windows and throws on an invalid sequence; this
    // function reports failures rather than throwing them.
    try {
      const std::filesystem::path env_root_dir_path =
          canonicalOrSelf(expandLeadingTilde(std::filesystem::path(env_dir_str)));
      candidate_list.push_back({env_root_dir_path, parent_in_root(env_root_dir_path),
                                "AURORA2_GENERATED_DEVICE_DIR", true});
    }
    catch (const std::exception& e) {
      destination.warnings.push_back(
          std::string("Ignoring AURORA2_GENERATED_DEVICE_DIR, it is not a usable path: ") + e.what());
    }
  }

  // [2] the source device's own root. Empty root_dir_path: already a known device root,
  //     so the caller must not register it.
  candidate_list.push_back({std::filesystem::path(),
                            canonicalOrSelf(source_device_dir_path.parent_path()),
                            "the device's own root", true});

  // [3] <home>/aurora_devices
  const std::filesystem::path home_dir_path = userHomeDirPath();
  if(!home_dir_path.empty()) {
    const std::filesystem::path home_root_dir_path =
        canonicalOrSelf(home_dir_path / "aurora_devices");
    candidate_list.push_back({home_root_dir_path, parent_in_root(home_root_dir_path),
                              "'" + home_root_dir_path.string() + "'", true});
  }

  // [4] the project's working directory.
  if(!working_dir_path.empty()) {
    const std::filesystem::path working_root_dir_path =
        canonicalOrSelf(working_dir_path / "aurora_devices");
    candidate_list.push_back({working_root_dir_path, parent_in_root(working_root_dir_path),
                              "'" + working_root_dir_path.string() + "'", true});
  }

  std::vector<std::string> rejection_list;

  for(const Candidate& candidate : candidate_list) {

    std::string why;
    bool replaced_existing = false;
    const std::filesystem::path device_dir_path =
        claimIn(candidate.parent_dir_path, devicename, source_device_dir_path, required_bytes,
                replaced_existing, why);

    if(!device_dir_path.empty()) {
      destination.device_dir_path = canonicalOrSelf(device_dir_path);
      // canonicalised HERE, not when the candidate was built: the root did not exist yet
      // then, and weakly_canonical leaves a trailing slash on a path it cannot resolve.
      destination.root_dir_path = candidate.root_dir_path.empty()
                                      ? std::filesystem::path()
                                      : canonicalOrSelf(candidate.root_dir_path);
      destination.replaced_existing = replaced_existing;
      return destination;
    }

    rejection_list.push_back(candidate.description + " (" + candidate.parent_dir_path.string() +
                             "): " + why);

    // A removal that happened in a candidate we then abandoned is still a removal.
    if(replaced_existing) {
      destination.warnings.push_back("Removed the previous generated device in " +
                                     candidate.parent_dir_path.string() +
                                     ", but could not write the new one there: " + why);
    }
    else if(candidate.warn_on_failure) {
      destination.warnings.push_back("Cannot write the generated device to " +
                                     candidate.description + " (" +
                                     candidate.parent_dir_path.string() + "): " + why);
    }
  }

  destination.error = "No writable location for the generated device '" + devicename + "'. Tried: ";
  for(std::vector<std::string>::size_type i = 0; i < rejection_list.size(); ++i) {
    destination.error += (i > 0 ? "; " : "") + rejection_list[i];
  }
  destination.error += env_dir_str != nullptr && *env_dir_str != '\0'
                           ? ". AURORA2_GENERATED_DEVICE_DIR is set; point it at a writable directory."
                           : ". Set AURORA2_GENERATED_DEVICE_DIR to a writable directory.";
  return destination;
}

}  // namespace FOEDAG
