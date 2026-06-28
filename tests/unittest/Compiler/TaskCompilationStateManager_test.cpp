/*
Copyright 2022 The Foedag team

GPL License

Copyright (c) 2022 The Open-Source FPGA Foundation

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "Compiler/TaskCompilationStateManager.h"

#include "Compiler/CommandWrapper.h"
#include "Compiler/Compiler.h"
#include "gtest/gtest.h"

#include <filesystem>
#include <string>

using namespace FOEDAG;

namespace {

// RAII helper that materializes a file with the given content and removes it on
// destruction, so each test leaves the filesystem clean.
class ScopedFile {
public:
  ScopedFile(const std::filesystem::path& filePath, const std::string& content)
      : m_filePath(filePath), m_content(content) {
    FileUtils::WriteToFile(m_filePath, m_content);
  }

  const std::filesystem::path& filePath() const { return m_filePath; }

  void appendContent(const std::string& appendix) {
    m_content += appendix;
    FileUtils::WriteToFile(m_filePath, m_content);
  }

  ~ScopedFile() { FileUtils::removeFile(m_filePath); }

private:
  std::filesystem::path m_filePath;
  std::string m_content;
};

// Compiler whose logging is silenced so the manager's diagnostic messages do
// not pollute the test output. Message()/ErrorMessage() are virtual, so a thin
// override is enough and we avoid depending on a real Tcl/output setup.
class SilentCompiler : public Compiler {
public:
  void Message(const std::string& /*message*/) const override {}
  void ErrorMessage(const std::string& /*message*/,
                    bool /*append*/ = true) const override {}
};

class TaskCompilationStateManagerTest : public ::testing::Test {
protected:
  void SetUp() override {
    const std::string testName =
        ::testing::UnitTest::GetInstance()->current_test_info()->name();
    m_projectDir = std::filesystem::temp_directory_path() /
                   ("tcsm_test_" + testName);
    std::filesystem::remove_all(m_projectDir);
    std::filesystem::create_directories(m_projectDir);
  }

  void TearDown() override {
    std::filesystem::remove_all(m_projectDir);
    // The project path lives in static state shared across the whole test
    // binary, so reset it to avoid leaking into unrelated tests.
    CommandWrapper::setProjectPath({});
    ScriptRenderer::setProjectPath({});
  }

  std::filesystem::path inProject(const std::string& name) const {
    return m_projectDir / name;
  }

  // Builds a representative task command: a couple of plain arguments plus two
  // source files referenced by absolute path.
  CommandWrapperPtr makeCommand(const std::filesystem::path& src1,
                                const std::filesystem::path& src2,
                                const std::string& effort = "high") {
    auto cmd = std::make_shared<CommandWrapper>("synth");
    cmd->append("--top", "top_module");
    cmd->append("--effort", effort);
    cmd->appendPath("--src1", src1);
    cmd->appendPath("--src2", src2);
    return cmd;
  }

  SilentCompiler m_compiler;
  std::filesystem::path m_projectDir;
};

}  // namespace

// A task that has never been stored must always be (re)compiled.
TEST_F(TaskCompilationStateManagerTest, never_compiled_task_requires_compilation) {
  ScopedFile src1{inProject("a.v"), "module a; endmodule"};
  ScopedFile src2{inProject("b.v"), "module b; endmodule"};

  TaskCompilationStateManager mgr(&m_compiler);
  mgr.setProjectPath(m_projectDir);

  EXPECT_TRUE(mgr.isCompilationRequired(1, makeCommand(src1.filePath(), src2.filePath())));
}

// Storing a task and re-checking it with an identical command (files untouched)
// must report the task as up-to-date.
TEST_F(TaskCompilationStateManagerTest, unchanged_task_does_not_require_recompilation) {
  ScopedFile src1{inProject("a.v"), "module a; endmodule"};
  ScopedFile src2{inProject("b.v"), "module b; endmodule"};

  TaskCompilationStateManager mgr(&m_compiler);
  mgr.setProjectPath(m_projectDir);

  mgr.storeTaskCommand(1, makeCommand(src1.filePath(), src2.filePath()));

  EXPECT_FALSE(mgr.isCompilationRequired(1, makeCommand(src1.filePath(), src2.filePath())));
}

// Editing the content of an input file must mark the task dirty.
TEST_F(TaskCompilationStateManagerTest, changed_file_content_marks_task_dirty) {
  ScopedFile src1{inProject("a.v"), "module a; endmodule"};
  ScopedFile src2{inProject("b.v"), "module b; endmodule"};

  TaskCompilationStateManager mgr(&m_compiler);
  mgr.setProjectPath(m_projectDir);

  mgr.storeTaskCommand(1, makeCommand(src1.filePath(), src2.filePath()));

  src1.appendContent("\n// touched");

  EXPECT_TRUE(mgr.isCompilationRequired(1, makeCommand(src1.filePath(), src2.filePath())));
}

// Changing a command-line argument value must mark the task dirty even if every
// input file is byte-for-byte identical.
TEST_F(TaskCompilationStateManagerTest, changed_argument_marks_task_dirty) {
  ScopedFile src1{inProject("a.v"), "module a; endmodule"};
  ScopedFile src2{inProject("b.v"), "module b; endmodule"};

  TaskCompilationStateManager mgr(&m_compiler);
  mgr.setProjectPath(m_projectDir);

  mgr.storeTaskCommand(1, makeCommand(src1.filePath(), src2.filePath(), "high"));

  EXPECT_TRUE(mgr.isCompilationRequired(
      1, makeCommand(src1.filePath(), src2.filePath(), "low")));
}

// Adding a new input file to the task must mark it dirty.
TEST_F(TaskCompilationStateManagerTest, added_file_marks_task_dirty) {
  ScopedFile src1{inProject("a.v"), "module a; endmodule"};
  ScopedFile src2{inProject("b.v"), "module b; endmodule"};
  ScopedFile src3{inProject("c.v"), "module c; endmodule"};

  TaskCompilationStateManager mgr(&m_compiler);
  mgr.setProjectPath(m_projectDir);

  mgr.storeTaskCommand(1, makeCommand(src1.filePath(), src2.filePath()));

  auto extended = makeCommand(src1.filePath(), src2.filePath());
  extended->appendPath("--src3", src3.filePath());

  EXPECT_TRUE(mgr.isCompilationRequired(1, extended));
}

// Removing an input file from the task must mark it dirty.
TEST_F(TaskCompilationStateManagerTest, removed_file_marks_task_dirty) {
  ScopedFile src1{inProject("a.v"), "module a; endmodule"};
  ScopedFile src2{inProject("b.v"), "module b; endmodule"};

  TaskCompilationStateManager mgr(&m_compiler);
  mgr.setProjectPath(m_projectDir);

  mgr.storeTaskCommand(1, makeCommand(src1.filePath(), src2.filePath()));

  auto reduced = std::make_shared<CommandWrapper>("synth");
  reduced->append("--top", "top_module");
  reduced->append("--effort", "high");
  reduced->appendPath("--src1", src1.filePath());

  EXPECT_TRUE(mgr.isCompilationRequired(1, reduced));
}

// Different task ids are tracked independently: storing task 1 must not make
// task 2 look up-to-date.
TEST_F(TaskCompilationStateManagerTest, tasks_are_tracked_independently) {
  ScopedFile src1{inProject("a.v"), "module a; endmodule"};
  ScopedFile src2{inProject("b.v"), "module b; endmodule"};

  TaskCompilationStateManager mgr(&m_compiler);
  mgr.setProjectPath(m_projectDir);

  mgr.storeTaskCommand(1, makeCommand(src1.filePath(), src2.filePath()));

  EXPECT_FALSE(mgr.isCompilationRequired(1, makeCommand(src1.filePath(), src2.filePath())));
  EXPECT_TRUE(mgr.isCompilationRequired(2, makeCommand(src1.filePath(), src2.filePath())));
}

// The same task id under a different profile is a distinct entry.
TEST_F(TaskCompilationStateManagerTest, profiles_are_tracked_independently) {
  ScopedFile src1{inProject("a.v"), "module a; endmodule"};
  ScopedFile src2{inProject("b.v"), "module b; endmodule"};

  TaskCompilationStateManager mgr(&m_compiler);
  mgr.setProjectPath(m_projectDir);

  mgr.storeTaskCommand(1, "rtl", makeCommand(src1.filePath(), src2.filePath()));

  EXPECT_FALSE(mgr.isCompilationRequired(
      1, "rtl", makeCommand(src1.filePath(), src2.filePath())));
  EXPECT_TRUE(mgr.isCompilationRequired(
      1, "timing", makeCommand(src1.filePath(), src2.filePath())));
}

// State persisted to compilation_cache.json must survive being reloaded by a
// fresh manager instance.
TEST_F(TaskCompilationStateManagerTest, state_survives_persist_and_reload) {
  ScopedFile src1{inProject("a.v"), "module a; endmodule"};
  ScopedFile src2{inProject("b.v"), "module b; endmodule"};

  {
    TaskCompilationStateManager mgr(&m_compiler);
    mgr.setProjectPath(m_projectDir);
    mgr.storeTaskCommand(1, makeCommand(src1.filePath(), src2.filePath()));
  }

  TaskCompilationStateManager reloaded(&m_compiler);
  reloaded.setProjectPath(m_projectDir);
  reloaded.load();

  EXPECT_FALSE(reloaded.isCompilationRequired(
      1, makeCommand(src1.filePath(), src2.filePath())));

  src1.appendContent("\n// touched after reload");
  EXPECT_TRUE(reloaded.isCompilationRequired(
      1, makeCommand(src1.filePath(), src2.filePath())));
}

// A whole directory passed as a task input is expanded recursively; changing a
// file inside that directory must mark the task dirty (exercises the directory
// expansion path in CommandWrapper).
TEST_F(TaskCompilationStateManagerTest, directory_input_detects_inner_file_change) {
  const std::filesystem::path srcDir = inProject("rtl");
  std::filesystem::create_directories(srcDir / "sub");
  ScopedFile inner1{srcDir / "x.v", "module x; endmodule"};
  ScopedFile inner2{srcDir / "sub" / "y.v", "module y; endmodule"};

  TaskCompilationStateManager mgr(&m_compiler);
  mgr.setProjectPath(m_projectDir);

  auto makeDirCommand = [&]() {
    auto cmd = std::make_shared<CommandWrapper>("synth");
    cmd->append("--top", "top_module");
    cmd->appendPath("--srcdir", srcDir);
    return cmd;
  };

  mgr.storeTaskCommand(1, makeDirCommand());

  // Unchanged directory -> up-to-date.
  EXPECT_FALSE(mgr.isCompilationRequired(1, makeDirCommand()));

  // Edit a file nested inside the directory -> dirty.
  inner2.appendContent("\n// touched");
  EXPECT_TRUE(mgr.isCompilationRequired(1, makeDirCommand()));
}

// Adding a new file into a directory input must also mark the task dirty.
TEST_F(TaskCompilationStateManagerTest, directory_input_detects_new_file) {
  const std::filesystem::path srcDir = inProject("rtl");
  std::filesystem::create_directories(srcDir);
  ScopedFile inner1{srcDir / "x.v", "module x; endmodule"};

  TaskCompilationStateManager mgr(&m_compiler);
  mgr.setProjectPath(m_projectDir);

  auto makeDirCommand = [&]() {
    auto cmd = std::make_shared<CommandWrapper>("synth");
    cmd->appendPath("--srcdir", srcDir);
    return cmd;
  };

  mgr.storeTaskCommand(1, makeDirCommand());

  ScopedFile inner2{srcDir / "z.v", "module z; endmodule"};
  EXPECT_TRUE(mgr.isCompilationRequired(1, makeDirCommand()));
}
