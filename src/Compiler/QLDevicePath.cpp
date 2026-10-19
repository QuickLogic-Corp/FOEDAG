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

}  // namespace FOEDAG
