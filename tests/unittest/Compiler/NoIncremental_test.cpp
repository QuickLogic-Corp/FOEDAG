/*
Copyright 2026 QuickLogic team

GPL License

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

#include <stdexcept>
#include <string>
#include <vector>

#include "Compiler/Compiler.h"
#include "Main/CommandLine.h"
#include "gtest/gtest.h"

using namespace FOEDAG;

namespace {

bool parsed(std::vector<std::string> args) {
  std::vector<char*> argv;
  for (std::string& arg : args) argv.push_back(arg.data());
  CommandLine cmd(static_cast<int>(argv.size()), argv.data());
  cmd.processArgs();
  return cmd.NoIncremental();
}

TEST(NoIncremental, the_option_is_parsed) {
  EXPECT_TRUE(parsed({"aurora", "--no-incremental", "--batch"}));
  EXPECT_TRUE(parsed({"aurora", "--batch", "--no-incremental"}));
  EXPECT_FALSE(parsed({"aurora", "--batch"}));
}

// Counts clears; BeforeCompileTask notes how many happened before it and ends
// the task there, so no task manager or project is needed.
class CountingCompiler : public Compiler {
 public:
  using Compiler::RunCompileTask;
  void clearCompilationCache() override { ++clears; }
  void BeforeCompileTask(Action) override {
    clearsSeenByTask.push_back(clears);
    throw std::runtime_error("stop before the task body");
  }
  void Message(const std::string&) const override {}
  int clears = 0;
  std::vector<int> clearsSeenByTask;
};

TEST(NoIncremental, clears_once_before_the_first_task) {
  CountingCompiler compiler;
  compiler.ForceFullRun(true);
  EXPECT_THROW(compiler.RunCompileTask(Compiler::Action::Synthesis), std::runtime_error);
  EXPECT_THROW(compiler.RunCompileTask(Compiler::Action::Pack), std::runtime_error);
  EXPECT_THROW(compiler.RunCompileTask(Compiler::Action::Routing), std::runtime_error);
  EXPECT_EQ(compiler.clears, 1);
  EXPECT_EQ(compiler.clearsSeenByTask, (std::vector<int>{1, 1, 1}));
  EXPECT_FALSE(compiler.ForceFullRunPending());
}

TEST(NoIncremental, without_the_option_nothing_is_cleared) {
  CountingCompiler compiler;
  EXPECT_THROW(compiler.RunCompileTask(Compiler::Action::Synthesis), std::runtime_error);
  EXPECT_THROW(compiler.RunCompileTask(Compiler::Action::Pack), std::runtime_error);
  EXPECT_EQ(compiler.clears, 0);
}

}  // namespace
