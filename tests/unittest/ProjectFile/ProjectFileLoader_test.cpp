#include "Main/ProjectFile/ProjectFileLoader.h"

#include <array>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "NewProject/ProjectManager/project.h"
#include "gtest/gtest.h"

using namespace FOEDAG;

namespace {

class SpyComponent : public ProjectFileComponent {
 public:
  void Save(QXmlStreamWriter*) override { ++saved; }
  int saved{0};
};

class ProjectFileLoaderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    Project::Instance()->InitProject();
    // The loader owns and deletes its components.
    for (size_t i = 0; i < spies.size(); ++i) {
      spies[i] = new SpyComponent;
      loader.registerComponent(spies[i], static_cast<ComponentId>(i));
    }
  }
  void TearDown() override { Project::Instance()->InitProject(); }
  int savedTotal() const {
    int n = 0;
    for (const auto* s : spies) n += s->saved;
    return n;
  }
  std::array<SpyComponent*, static_cast<size_t>(ComponentId::Count)> spies{};
  ProjectFileLoader loader{Project::Instance()};
};

}  // namespace

// `aurora --batch --script` saves on exit even when the script opened no project.
// The file name was then "/" + "" + ".ospr": the drive root on Windows, where an
// administrator could write it before every component dereferenced a project
// configuration that does not exist (access violation at exit).
TEST_F(ProjectFileLoaderTest, NoOpenProjectSavesNothing) {
  loader.Save();
  EXPECT_EQ(savedTotal(), 0);
}

TEST_F(ProjectFileLoaderTest, ProjectWithoutANameSavesNothing) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  Project::Instance()->setProjectPath(dir.path());
  loader.Save();
  EXPECT_EQ(savedTotal(), 0);
  EXPECT_FALSE(QFile::exists(dir.filePath(".ospr")));
}

TEST_F(ProjectFileLoaderTest, OpenProjectIsSaved) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  Project::Instance()->setProjectName("counter_16bit");
  Project::Instance()->setProjectPath(dir.path());
  loader.Save();
  EXPECT_EQ(savedTotal(), static_cast<int>(spies.size()));
  EXPECT_TRUE(QFile::exists(dir.filePath("counter_16bit.ospr")));
}
