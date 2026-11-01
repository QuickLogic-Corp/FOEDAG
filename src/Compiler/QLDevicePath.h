#ifndef QLDEVICEPATH_H
#define QLDEVICEPATH_H

#include <filesystem>
#include <string>

namespace FOEDAG {

// '~/...' -> '<home>/...'. Only a LEADING tilde is expanded; '~user' is left alone,
// as is a path with no tilde. Returns the input unchanged when there is no usable home.
//
// For paths a USER typed. A tilde is the shell's, not the filesystem's: a value set in
// CI yaml, a systemd unit or a .conf file reaches us literally and would otherwise
// create a directory actually named '~'.
std::filesystem::path expandLeadingTilde(const std::filesystem::path& input_path);

// true when 'candidate' is inside 'ancestor' (or is it), after resolving symlinks and '..'.
// Resolves as much of 'candidate' as exists, so it answers for a directory not yet created.
// An empty 'ancestor' contains nothing.
bool pathIsInside(const std::filesystem::path& candidate,
                  const std::filesystem::path& ancestor);

}  // namespace FOEDAG

#endif  // QLDEVICEPATH_H
