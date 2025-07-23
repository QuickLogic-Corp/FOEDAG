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

using namespace FOEDAG;

TEST(CommandWrapper, to_string)
{
    CommandWrapper command{"cmd"};
    command.append("--p1", "v1");
    command.append("--p2");
    command.append("--p3", "v3");
    command.appendFile("--file1", std::filesystem::path{"filepath1"});
    command.appendFile(std::filesystem::path{"filepath2"});

    EXPECT_EQ("cmd --p1 v1 --p2 --p3 v3 --file1 filepath1 filepath2", command.string());
}

TEST(CommandWrapper, compare_matches)
{
    CommandWrapper command1{"cmd"};
    command1.append("--p1", "v1");
    command1.append("--p2");
    command1.append("--p3", "v3");
    command1.appendFile("--file1", std::filesystem::path{"filepath1"});
    command1.appendFile(std::filesystem::path{"filepath2"});

    CommandWrapper command2{"cmd"};
    command2.append("--p1", "v1");
    command2.append("--p2");
    command2.append("--p3", "v3");
    command2.appendFile("--file1", std::filesystem::path{"filepath1"});
    command2.appendFile(std::filesystem::path{"filepath2"});

    DiffCommandPtr diff = command2.compare(command1);
    EXPECT_TRUE(diff->empty());
}

TEST(CommandWrapper, remove_argument1)
{
    CommandWrapper command1{"cmd"};
    command1.append("--p1", "v1");
    command1.append("--p2");
    command1.append("--p3", "v3");
    command1.appendFile("--file1", std::filesystem::path{"filepath1"});
    command1.appendFile(std::filesystem::path{"filepath2"});

    CommandWrapper command2{"cmd"};
    command2.append("--p2");
    command2.append("--p3", "v3");
    command2.appendFile("--file1", std::filesystem::path{"filepath1"});
    command2.appendFile(std::filesystem::path{"filepath2"});

    DiffCommandPtr diff = command2.compare(command1);
    EXPECT_TRUE(!diff->empty());

    EXPECT_EQ(diff->removed().size(), 1);

    EXPECT_EQ(diff->removed()[0].parameter, "--p1");
    EXPECT_EQ(diff->removed()[0].value, "v1");

    EXPECT_TRUE(diff->changed().empty());
    EXPECT_TRUE(diff->added().empty());
    EXPECT_TRUE(diff->files().empty());
}

TEST(CommandWrapper, remove_argument2)
{
    CommandWrapper command1{"cmd"};
    command1.append("--p1", "v1");
    command1.append("--p2");
    command1.append("--p3", "v3");
    command1.appendFile("--file1", std::filesystem::path{"filepath1"});
    command1.appendFile(std::filesystem::path{"filepath2"});

    CommandWrapper command2{"cmd"};
    command2.append("--p1", "v1");
    command2.append("--p2");
    command2.append("--p3", "v3");
    command2.appendFile(std::filesystem::path{"filepath2"});

    DiffCommandPtr diff = command2.compare(command1);
    EXPECT_TRUE(!diff->empty());

    EXPECT_EQ(diff->removed().size(), 1);
    EXPECT_EQ(diff->removed()[0].parameter, "--file1");
    EXPECT_EQ(diff->removed()[0].value, "filepath1");

    EXPECT_TRUE(diff->changed().empty());
    EXPECT_TRUE(diff->added().empty());
    EXPECT_TRUE(diff->files().empty());
}

TEST(CommandWrapper, add_argument1)
{
    CommandWrapper command1{"cmd"};
    command1.append("--p1", "v1");
    command1.append("--p2");
    command1.append("--p3", "v3");
    command1.appendFile("--file1", std::filesystem::path{"filepath1"});
    command1.appendFile(std::filesystem::path{"filepath2"});

    CommandWrapper command2{"cmd"};
    command2.append("--p1", "v1");
    command2.append("--p2");
    command2.append("--p3", "v3");
    command2.append("--p4", "v4");
    command2.appendFile("--file1", std::filesystem::path{"filepath1"});
    command2.appendFile(std::filesystem::path{"filepath2"});

    DiffCommandPtr diff = command2.compare(command1);
    EXPECT_TRUE(!diff->empty());

    EXPECT_EQ(diff->added().size(), 1);
    EXPECT_EQ(diff->added()[0].parameter, "--p4");
    EXPECT_EQ(diff->added()[0].value, "v4");

    EXPECT_TRUE(diff->changed().empty());
    EXPECT_TRUE(diff->removed().empty());
    EXPECT_TRUE(diff->files().empty());
}

TEST(CommandWrapper, add_argument2)
{
    CommandWrapper command1{"cmd"};
    command1.append("--p1", "v1");
    command1.append("--p2");
    command1.append("--p3", "v3");
    command1.appendFile("--file1", std::filesystem::path{"filepath1"});

    CommandWrapper command2{"cmd"};
    command2.append("--p1", "v1");
    command2.append("--p2");
    command2.append("--p3", "v3");
    command2.appendFile("--file1", std::filesystem::path{"filepath1"});
    command2.appendFile(std::filesystem::path{"filepath2"});

    DiffCommandPtr diff = command2.compare(command1);
    EXPECT_TRUE(!diff->empty());

    EXPECT_EQ(diff->added().size(), 1);
    EXPECT_EQ(diff->added()[0].parameter, "filepath2");
    EXPECT_EQ(diff->added()[0].value, "");

    EXPECT_TRUE(diff->changed().empty());
    EXPECT_TRUE(diff->removed().empty());
    EXPECT_TRUE(diff->files().empty());
}

TEST(CommandWrapper, change_argument1)
{
    CommandWrapper command1{"cmd"};
    command1.append("--p1", "v1");
    command1.append("--p2");
    command1.append("--p3", "v3");
    command1.appendFile("--file1", std::filesystem::path{"filepath1"});
    command1.appendFile(std::filesystem::path{"filepath2"});

    CommandWrapper command2{"cmd"};
    command2.append("--p1", "v1");
    command2.append("--p2");
    command2.append("--p3", "v3_NEW");
    command2.appendFile("--file1", std::filesystem::path{"filepath1"});
    command2.appendFile(std::filesystem::path{"filepath2"});

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

TEST(CommandWrapper, change_argument2)
{
    CommandWrapper command1{"cmd"};
    command1.append("--p1", "v1");
    command1.append("--p2");
    command1.append("--p3", "v3");
    command1.appendFile("--file1", std::filesystem::path{"filepath1"});
    command1.appendFile(std::filesystem::path{"filepath2"});

    CommandWrapper command2{"cmd"};
    command2.append("--p1", "v1");
    command2.append("--p2");
    command2.append("--p3", "v3");
    command2.appendFile("--file1", std::filesystem::path{"filepath1_NEW"});
    command2.appendFile(std::filesystem::path{"filepath2"});

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