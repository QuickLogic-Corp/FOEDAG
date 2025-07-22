#include "DialogUtils.h"

#include "Compiler/CompilerDefines.h"
#include "Compiler/WildcardFileFinder.h"
#include "Widgets/SelectionDialog.h"
#include "NewProject/ProjectManager/project.h"

#include <set>

namespace FOEDAG {

QString DialogUtils::execUserSelectionOfActiveProfile(const std::set<std::string>& profiles)
{
    QString profile{""}; // default profile, when no multiple report is enabled
    if (!profiles.empty()) {
        SelectionDialog dialog("Select profile", profiles, nullptr);
        if (dialog.exec() == QDialog::Accepted) {
            profile = dialog.selectedText();
        }
    }
    return profile;
}

} // namespace FOEDAG