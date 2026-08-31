#include "Compiler/FloorplanningConfigProvider.h"

#include <string>

#include "Compiler/Compiler.h"
#include "Compiler/QLDeviceManager.h"
#include "MainWindow/Session.h"
#include "Utils/FileUtils.h"

extern FOEDAG::Session* GlobalSession;

namespace FOEDAG {

std::filesystem::path FloorplanningConfigProvider::getConfig() {
  Compiler* compiler =
      (GlobalSession != nullptr) ? GlobalSession->GetCompiler() : nullptr;

  const std::filesystem::path configFile =
      QLDeviceManager::getInstance()->deviceConfigJSONPath();

  if (!FileUtils::FileExists(configFile)) {
    if (compiler) {
      compiler->ErrorMessage("Floorplanning: device config.json not found: " +
                             configFile.string());
    }
    return {};
  }

  return configFile;
}

}  // namespace FOEDAG
