#include <QByteArray>

#include "Compiler/Compiler.h"
#include "gtest/gtest.h"

using namespace FOEDAG;

namespace {
class PlatformEnv {
 public:
  explicit PlatformEnv(const char* value)
      : m_had(qEnvironmentVariableIsSet("QT_QPA_PLATFORM")),
        m_old(qgetenv("QT_QPA_PLATFORM")) {
    if (value)
      qputenv("QT_QPA_PLATFORM", value);
    else
      qunsetenv("QT_QPA_PLATFORM");
  }
  ~PlatformEnv() {
    if (m_had)
      qputenv("QT_QPA_PLATFORM", m_old);
    else
      qunsetenv("QT_QPA_PLATFORM");
  }

 private:
  bool m_had;
  QByteArray m_old;
};
}  // namespace

TEST(QtPlatformPlugin, NoDisplayIsOffscreen) {
  PlatformEnv env("xcb");
  EXPECT_EQ(Compiler::getQtPlatformPluginFor(false), "offscreen");
}

TEST(QtPlatformPlugin, ViewerFollowsOffscreenAurora) {
  PlatformEnv env("offscreen");
  EXPECT_EQ(Compiler::getQtPlatformPluginFor(true), "offscreen");
}

TEST(QtPlatformPlugin, ViewerWithDisplay) {
  PlatformEnv env(nullptr);
#ifdef __linux__
  EXPECT_EQ(Compiler::getQtPlatformPluginFor(true), "xcb");
#else
  EXPECT_EQ(Compiler::getQtPlatformPluginFor(true), "");
#endif
}
