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

#include "Compiler/CommandWrapper.h"

#include "gtest/gtest.h"
#include <thread>

using namespace FOEDAG;

namespace {
class ScopedFile {
public:
    ScopedFile(const std::filesystem::path& filePath, const std::string& content):
    m_filePath(filePath) {
        FileUtils::WriteToFile(filePath, content);
    }
    
    const std::filesystem::path& filePath() const { return m_filePath; }

    void writeContent(const std::string& content, int delay = 0) {
        if (delay > 0) {
            std::this_thread::sleep_for(std::chrono::seconds(delay));
        }
        FileUtils::WriteToFile(m_filePath, content);
    }

    ~ScopedFile() {
        FileUtils::removeFile(m_filePath);
    }

private:
    std::filesystem::path m_filePath;
};
} // namespace

TEST(CommandWrapper, to_string)
{
    ScopedFile filePath1{std::filesystem::path{"filepath1"}, "content for filepath1..."};
    ScopedFile filePath2{std::filesystem::path{"filepath2"}, "content for filepath2..."};

    CommandWrapper command{"cmd"};
    command.append("--p1", "v1");
    command.append("--p2");
    command.append("--p3", "v3");
    command.appendFile("--file1", filePath1.filePath());
    command.appendFile(filePath2.filePath());

    EXPECT_EQ("cmd --p1 v1 --p2 --p3 v3 --file1 filepath1 filepath2", command.string());
}

TEST(CommandWrapper, compare_matches)
{
    ScopedFile filePath1{std::filesystem::path{"filepath1"}, "content for filepath1..."};
    ScopedFile filePath2{std::filesystem::path{"filepath2"}, "content for filepath2..."};

    CommandWrapper command1{"cmd"};
    command1.append("--p1", "v1");
    command1.append("--p2");
    command1.append("--p3", "v3");
    command1.appendFile("--file1", filePath1.filePath());
    command1.appendFile(filePath2.filePath());

    CommandWrapper command2{"cmd"};
    command2.append("--p1", "v1");
    command2.append("--p2");
    command2.append("--p3", "v3");
    command2.appendFile("--file1", filePath1.filePath());
    command2.appendFile(filePath2.filePath());

    DiffCommandPtr diff = command2.compare(command1);
    EXPECT_TRUE(diff->empty());
}

TEST(CommandWrapper, remove_argument)
{
    ScopedFile filePath1{std::filesystem::path{"filepath1"}, "content for filepath1..."};
    ScopedFile filePath2{std::filesystem::path{"filepath2"}, "content for filepath2..."};

    CommandWrapper command1{"cmd"};
    command1.append("--p1", "v1");
    command1.append("--p2");
    command1.append("--p3", "v3");
    command1.appendFile("--file1", filePath1.filePath());
    command1.appendFile(filePath2.filePath());

    CommandWrapper command2{"cmd"};
    command2.append("--p2");
    command2.append("--p3", "v3");
    command2.appendFile("--file1", filePath1.filePath());
    command2.appendFile(filePath2.filePath());

    DiffCommandPtr diff = command2.compare(command1);
    EXPECT_TRUE(!diff->empty());

    EXPECT_EQ(diff->removed().size(), 1);

    EXPECT_EQ(diff->removed()[0].parameter, "--p1");
    EXPECT_EQ(diff->removed()[0].value, "v1");

    EXPECT_TRUE(diff->changed().empty());
    EXPECT_TRUE(diff->added().empty());
    EXPECT_TRUE(diff->files().empty());
}

TEST(CommandWrapper, remove_argument_file)
{
    ScopedFile filePath1{std::filesystem::path{"filepath1"}, "content for filepath1..."};
    ScopedFile filePath2{std::filesystem::path{"filepath2"}, "content for filepath2..."};

    CommandWrapper command1{"cmd"};
    command1.append("--p1", "v1");
    command1.append("--p2");
    command1.append("--p3", "v3");
    command1.appendFile("--file1", filePath1.filePath());
    command1.appendFile(filePath2.filePath());

    CommandWrapper command2{"cmd"};
    command2.append("--p1", "v1");
    command2.append("--p2");
    command2.append("--p3", "v3");
    command2.appendFile(filePath2.filePath());

    DiffCommandPtr diff = command2.compare(command1);
    EXPECT_TRUE(!diff->empty());

    EXPECT_EQ(diff->removed().size(), 1);
    EXPECT_EQ(diff->removed()[0].parameter, "--file1");
    EXPECT_EQ(diff->removed()[0].value, "filepath1");

    EXPECT_TRUE(diff->changed().empty());
    EXPECT_TRUE(diff->added().empty());
    EXPECT_TRUE(diff->files().empty());
}

TEST(CommandWrapper, add_argument)
{
    ScopedFile filePath1{std::filesystem::path{"filepath1"}, "content for filepath1..."};
    ScopedFile filePath2{std::filesystem::path{"filepath2"}, "content for filepath2..."};

    CommandWrapper command1{"cmd"};
    command1.append("--p1", "v1");
    command1.append("--p2");
    command1.append("--p3", "v3");
    command1.appendFile("--file1", filePath1.filePath());
    command1.appendFile(filePath2.filePath());

    CommandWrapper command2{"cmd"};
    command2.append("--p1", "v1");
    command2.append("--p2");
    command2.append("--p3", "v3");
    command2.append("--p4", "v4");
    command2.appendFile("--file1", filePath1.filePath());
    command2.appendFile(filePath2.filePath());

    DiffCommandPtr diff = command2.compare(command1);
    EXPECT_TRUE(!diff->empty());

    EXPECT_EQ(diff->added().size(), 1);
    EXPECT_EQ(diff->added()[0].parameter, "--p4");
    EXPECT_EQ(diff->added()[0].value, "v4");

    EXPECT_TRUE(diff->changed().empty());
    EXPECT_TRUE(diff->removed().empty());
    EXPECT_TRUE(diff->files().empty());
}

TEST(CommandWrapper, add_argument_file)
{
    ScopedFile filePath1{std::filesystem::path{"filepath1"}, "content for filepath1..."};
    ScopedFile filePath2{std::filesystem::path{"filepath2"}, "content for filepath2..."};

    CommandWrapper command1{"cmd"};
    command1.append("--p1", "v1");
    command1.append("--p2");
    command1.append("--p3", "v3");
    command1.appendFile("--file1", filePath1.filePath());

    CommandWrapper command2{"cmd"};
    command2.append("--p1", "v1");
    command2.append("--p2");
    command2.append("--p3", "v3");
    command2.appendFile("--file1", filePath1.filePath());
    command2.appendFile(filePath2.filePath());

    DiffCommandPtr diff = command2.compare(command1);
    EXPECT_TRUE(!diff->empty());

    EXPECT_EQ(diff->added().size(), 1);
    EXPECT_EQ(diff->added()[0].parameter, "filepath2");
    EXPECT_EQ(diff->added()[0].value, "");

    EXPECT_TRUE(diff->changed().empty());
    EXPECT_TRUE(diff->removed().empty());
    EXPECT_TRUE(diff->files().empty());
}

TEST(CommandWrapper, change_argument)
{
    ScopedFile filePath1{std::filesystem::path{"filepath1"}, "content for filepath1..."};
    ScopedFile filePath2{std::filesystem::path{"filepath2"}, "content for filepath2..."};

    CommandWrapper command1{"cmd"};
    command1.append("--p1", "v1");
    command1.append("--p2");
    command1.append("--p3", "v3");
    command1.appendFile("--file1", filePath1.filePath());
    command1.appendFile(filePath2.filePath());

    CommandWrapper command2{"cmd"};
    command2.append("--p1", "v1");
    command2.append("--p2");
    command2.append("--p3", "v3_NEW");
    command2.appendFile("--file1", filePath1.filePath());
    command2.appendFile(filePath2.filePath());

    DiffCommandPtr diff = command2.compare(command1);
    EXPECT_TRUE(!diff->empty());

    EXPECT_EQ(diff->changed().size(), 1);
    EXPECT_EQ(diff->changed()[0].parameter, "--p3");
    EXPECT_EQ(diff->changed()[0].prevValue, "v3");
    EXPECT_EQ(diff->changed()[0].value, "v3_NEW");

    EXPECT_TRUE(diff->added().empty());
    EXPECT_TRUE(diff->removed().empty());
    EXPECT_TRUE(diff->files().empty());
}

TEST(CommandWrapper, change_argument_file)
{
    ScopedFile filePath1{std::filesystem::path{"filepath1"}, "content for filepath1..."};
    ScopedFile filePath2{std::filesystem::path{"filepath2"}, "content for filepath2..."};

    CommandWrapper command1{"cmd"};
    command1.append("--p1", "v1");
    command1.append("--p2");
    command1.append("--p3", "v3");
    command1.appendFile("--file1", filePath1.filePath());
    command1.appendFile(filePath2.filePath());

    CommandWrapper command2{"cmd"};
    command2.append("--p1", "v1");
    command2.append("--p2");
    command2.append("--p3", "v3");
    command2.appendFile("--file1", std::filesystem::path{"filepath1_NEW"});
    command2.appendFile(filePath2.filePath());

    DiffCommandPtr diff = command2.compare(command1);
    EXPECT_TRUE(!diff->empty());

    EXPECT_EQ(diff->changed().size(), 1);
    EXPECT_EQ(diff->changed()[0].parameter, "--file1");
    EXPECT_EQ(diff->changed()[0].prevValue, "filepath1");
    EXPECT_EQ(diff->changed()[0].value, "filepath1_NEW");

    EXPECT_TRUE(diff->added().empty());
    EXPECT_TRUE(diff->removed().empty());
    EXPECT_TRUE(diff->files().empty());
}

TEST(CommandWrapper, file_content_changed)
{
    ScopedFile filePath1{std::filesystem::path{"filepath1"}, "content for filepath1..."};
    ScopedFile filePath2{std::filesystem::path{"filepath2"}, "content for filepath2..."};

    const std::string filePath1CreationTimeStr = std::to_string(FileUtils::Mtime(filePath1.filePath()));

    CommandWrapper command1{"cmd"};
    command1.append("--p1", "v1");
    command1.append("--p2");
    command1.append("--p3", "v3");
    command1.appendFile("--file1", filePath1.filePath());
    command1.appendFile(filePath2.filePath());

    filePath1.writeContent("changed content for filepath1...", 1);
    const std::string filePath1ModificationTimeStr = std::to_string(FileUtils::Mtime(filePath1.filePath()));

    CommandWrapper command2{"cmd"};
    command2.append("--p1", "v1");
    command2.append("--p2");
    command2.append("--p3", "v3");
    command2.appendFile("--file1", filePath1.filePath());
    command2.appendFile(filePath2.filePath());

    DiffCommandPtr diff = command2.compare(command1);
    EXPECT_TRUE(!diff->empty());

    EXPECT_EQ(diff->files().size(), 1);

    EXPECT_EQ(diff->files().begin()->file, filePath1.filePath().string());
    EXPECT_EQ(diff->files().begin()->criteria, "datetime");
    EXPECT_EQ(diff->files().begin()->prevValue, filePath1CreationTimeStr);
    EXPECT_EQ(diff->files().begin()->value, filePath1ModificationTimeStr);

    EXPECT_TRUE(diff->changed().empty());
    EXPECT_TRUE(diff->added().empty());
    EXPECT_TRUE(diff->removed().empty());

}
