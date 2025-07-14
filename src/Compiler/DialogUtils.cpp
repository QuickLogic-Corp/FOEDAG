#include "DialogUtils.h"

#include "Compiler/CompilerDefines.h"
#include "Compiler/WildcardFileFinder.h"
#include "Widgets/SelectionDialog.h"
#include "NewProject/ProjectManager/project.h"

#include <set>

namespace FOEDAG {

QString DialogUtils::execUserSelectionOfActiveStaProfile()
{
    QString profile{""}; // default profile, when no multiple report is enabled
    std::set<std::string> profiles = WildcardFileFinder::findProfilesBasedOnExistedFiles(Project::Instance()->projectPath().toStdString(), TIMING_ANALYSIS_LOG_PATTERN);
    if (!profiles.empty()) {
        SelectionDialog dialog("Select profile", profiles, nullptr);
        if (dialog.exec() == QDialog::Accepted) {
            profile = dialog.selectedText();
        }
    }
    return profile;
}

} // namespace FOEDAG