#pragma once

#include <QString>
#include <set>

namespace FOEDAG {

class DialogUtils {
public:
    static QString execUserSelectionOfActiveProfile(const std::set<std::string>& profiles);
};

}  // namespace FOEDAG
