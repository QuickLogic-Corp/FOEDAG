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

    std::string hash() const {
        return FileUtils::calcHashFileContent(m_filePath);
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
    ScopedFile filePath3{std::filesystem::path{"filepath3"}, "content for filepath3..."};
    ScopedFile filePath4{std::filesystem::path{"filepath4"}, "content for filepath4..."};

    CommandWrapper command{"cmd"};
    command.append("--p1", "v1");
    command.append("--p2");
    command.append("--p3", "v3");
    command.appendFile("--file1", filePath1.filePath());
    command.appendFile(filePath2.filePath());
    command.appendFile(filePath3.filePath(), "arch");
    command.appendFile("--file4", filePath4.filePath(), "arch");

    EXPECT_EQ("cmd --p1 v1 --p2 --p3 v3 --file1 filepath1 filepath2 filepath3 --file4 filepath4", command.string());
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

    EXPECT_EQ(diff->removedParameters().size(), 1);

    EXPECT_EQ(diff->removedParameters()[0].name, "--p1");
    EXPECT_EQ(diff->removedParameters()[0].value, "v1");

    EXPECT_TRUE(diff->changedParameters().empty());
    EXPECT_TRUE(diff->addedParameters().empty());
    EXPECT_TRUE(diff->diffFiles().empty());
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

    EXPECT_EQ(diff->removedParameters().size(), 1);
    EXPECT_EQ(diff->removedParameters()[0].name, "--file1");
    EXPECT_EQ(diff->removedParameters()[0].value, "filepath1");

    EXPECT_TRUE(diff->changedParameters().empty());
    EXPECT_TRUE(diff->addedParameters().empty());
    EXPECT_TRUE(diff->diffFiles().empty());
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

    EXPECT_EQ(diff->addedParameters().size(), 1);
    EXPECT_EQ(diff->addedParameters()[0].name, "--p4");
    EXPECT_EQ(diff->addedParameters()[0].value, "v4");

    EXPECT_TRUE(diff->changedParameters().empty());
    EXPECT_TRUE(diff->removedParameters().empty());
    EXPECT_TRUE(diff->diffFiles().empty());
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

    EXPECT_EQ(diff->addedParameters().size(), 1);
    EXPECT_EQ(diff->addedParameters()[0].name, "filepath2");
    EXPECT_EQ(diff->addedParameters()[0].value, "");

    EXPECT_TRUE(diff->changedParameters().empty());
    EXPECT_TRUE(diff->removedParameters().empty());
    EXPECT_TRUE(diff->diffFiles().empty());
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

    EXPECT_EQ(diff->changedParameters().size(), 1);
    EXPECT_EQ(diff->changedParameters()[0].name, "--p3");
    EXPECT_EQ(diff->changedParameters()[0].prevValue, "v3");
    EXPECT_EQ(diff->changedParameters()[0].value, "v3_NEW");

    EXPECT_TRUE(diff->addedParameters().empty());
    EXPECT_TRUE(diff->removedParameters().empty());
    EXPECT_TRUE(diff->diffFiles().empty());
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

    EXPECT_EQ(diff->changedParameters().size(), 1);
    EXPECT_EQ(diff->changedParameters()[0].name, "--file1");
    EXPECT_EQ(diff->changedParameters()[0].prevValue, "filepath1");
    EXPECT_EQ(diff->changedParameters()[0].value, "filepath1_NEW");

    EXPECT_TRUE(diff->addedParameters().empty());
    EXPECT_TRUE(diff->removedParameters().empty());
    EXPECT_TRUE(diff->diffFiles().empty());
}

TEST(CommandWrapper, file_content_changed)
{
    ScopedFile filePath1{std::filesystem::path{"filepath1"}, "content for filepath1..."};
    ScopedFile filePath2{std::filesystem::path{"filepath2"}, "content for filepath2..."};

    const std::string filePath1CreationTimeStr = FileUtils::ModifiedTimeStr(filePath1.filePath());

    CommandWrapper command1{"cmd"};
    command1.append("--p1", "v1");
    command1.append("--p2");
    command1.append("--p3", "v3");
    command1.appendFile("--file1", filePath1.filePath());
    command1.appendFile(filePath2.filePath());

    filePath1.writeContent("changed content for filepath1...", 2);
    const std::string filePath1ModificationTimeStr = FileUtils::ModifiedTimeStr(filePath1.filePath());

    CommandWrapper command2{"cmd"};
    command2.append("--p1", "v1");
    command2.append("--p2");
    command2.append("--p3", "v3");
    command2.appendFile("--file1", filePath1.filePath());
    command2.appendFile(filePath2.filePath());

    DiffCommandPtr diff = command2.compare(command1);
    EXPECT_TRUE(!diff->empty());

    EXPECT_EQ(diff->diffFiles().size(), 1);

    EXPECT_EQ(diff->diffFiles().begin()->file, filePath1.filePath().string());
    EXPECT_EQ(diff->diffFiles().begin()->criteria, "datetime");
    EXPECT_EQ(diff->diffFiles().begin()->prevValue, filePath1CreationTimeStr);
    EXPECT_EQ(diff->diffFiles().begin()->value, filePath1ModificationTimeStr);

    EXPECT_TRUE(diff->changedParameters().empty());
    EXPECT_TRUE(diff->addedParameters().empty());
    EXPECT_TRUE(diff->removedParameters().empty());
}

TEST(CommandWrapper, masked_files_matches) 
{
    // in this particular case we rely that file has specific role, filepath could be different, but content expected to be the same
    ScopedFile filePath1{std::filesystem::path{"filepath1"}, "shared content..."};
    ScopedFile filePath2{std::filesystem::path{"filepath2"}, "shared content..."};

    CommandWrapper command1{"cmd"};
    command1.append("--p1", "v1");
    command1.append("--p2");
    command1.append("--p3", "v3");
    command1.appendFile(filePath1.filePath(), "arch");

    CommandWrapper command2{"cmd"};
    command2.append("--p1", "v1");
    command2.append("--p2");
    command2.append("--p3", "v3");
    command2.appendFile(filePath2.filePath(), "arch");

    DiffCommandPtr diff = command2.compare(command1);
    EXPECT_TRUE(diff->empty());

    EXPECT_TRUE(diff->diffFiles().empty());
    EXPECT_TRUE(diff->changedParameters().empty());
    EXPECT_TRUE(diff->addedParameters().empty());
    EXPECT_TRUE(diff->removedParameters().empty());
}

TEST(CommandWrapper, masked_files_differ) 
{
    // in this particular case we rely that file has specific role, filepath could be different, but content expected to be the same
    ScopedFile filePath1{std::filesystem::path{"filepath1"}, "content 1..."};
    ScopedFile filePath2{std::filesystem::path{"filepath2"}, "content 2..."};

    CommandWrapper command1{"cmd"};
    command1.append("--p1", "v1");
    command1.append("--p2");
    command1.append("--p3", "v3");
    command1.appendFile(filePath1.filePath(), "arch");

    CommandWrapper command2{"cmd"};
    command2.append("--p1", "v1");
    command2.append("--p2");
    command2.append("--p3", "v3");
    command2.appendFile(filePath2.filePath(), "arch");

    DiffCommandPtr diff = command2.compare(command1);
    EXPECT_TRUE(!diff->empty());

    EXPECT_EQ(diff->diffFiles().size(), 1);
    EXPECT_EQ(diff->diffFiles().begin()->file, "arch");
    EXPECT_EQ(diff->diffFiles().begin()->criteria, "hash");
    EXPECT_EQ(diff->diffFiles().begin()->prevValue, filePath1.hash());
    EXPECT_EQ(diff->diffFiles().begin()->value, filePath2.hash());

    EXPECT_TRUE(diff->changedParameters().empty());
    EXPECT_TRUE(diff->addedParameters().empty());
    EXPECT_TRUE(diff->removedParameters().empty());
}

TEST(CommandWrapper, serialization) 
{
    ScopedFile filePath1{std::filesystem::path{"filepath1"}, "shared content 1..."};
    ScopedFile filePath2{std::filesystem::path{"filepath2"}, "shared content 2..."};

    CommandWrapper commandOrig{"cmd"};
    commandOrig.append("--p1", "v1");
    commandOrig.append("--p2");
    commandOrig.append("--p3", "v3");
    commandOrig.appendFile(filePath1.filePath(), "arch");
    commandOrig.appendFile("--file2", filePath2.filePath());

    // Serialize
    nlohmann::json json = commandOrig;

    // Deserialize
    CommandWrapper commandRestored = json.get<CommandWrapper>();
    for (const auto& [key, fileIdentity]: commandRestored.files()) {
        if (fileIdentity.mask().empty()) {
            std::cout << "~~~ fileIdentity.modifiedDateTime()" << fileIdentity.modifiedDateTime() << std::endl;
            EXPECT_TRUE(!fileIdentity.modifiedDateTime().empty());
            EXPECT_TRUE(fileIdentity.contentHash().empty());
        } else {
            std::cout << "~~~ fileIdentity.contentHash()" << fileIdentity.contentHash() << std::endl;
            EXPECT_TRUE(!fileIdentity.contentHash().empty());
            EXPECT_TRUE(fileIdentity.modifiedDateTime().empty());
        }
    }

    DiffCommandPtr diff = commandRestored.compare(commandOrig);
    EXPECT_TRUE(diff->empty());

    EXPECT_TRUE(diff->diffFiles().empty());
    EXPECT_TRUE(diff->changedParameters().empty());
    EXPECT_TRUE(diff->addedParameters().empty());
    EXPECT_TRUE(diff->removedParameters().empty());
}

TEST(CommandWrapperBuilder, restore_single_cmd_from_string)
{
    ScopedFile filePath1{std::filesystem::path{"filepath1"}, "shared content 1..."};
    ScopedFile filePath2{std::filesystem::path{"filepath2"}, "shared content 2..."};
    ScopedFile filePath3{std::filesystem::path{"filepath3"}, "shared content 3..."};

    CommandWrapper commandOrig{"cmd"};
    commandOrig.append("--p1", "v1");
    commandOrig.append("--p2");
    commandOrig.append("--p3", "v3");
    commandOrig.appendFile(filePath1.filePath(), "mask1");
    commandOrig.appendFile("--file2", filePath2.filePath());
    commandOrig.appendFile("--file3", filePath3.filePath(), "mask2");

    const std::map<std::filesystem::path, std::string> maskedFiles = {{filePath1.filePath(), "mask1"}, {filePath3.filePath(), "mask2"}};
    CommandWrapperPtr commandRestoredPtr = CommandWrapperBuilder::fromString(commandOrig.string(), maskedFiles);
    for (const auto& [key, fileIdentity]: commandRestoredPtr->files()) {
        if (fileIdentity.filePath() == filePath1.filePath()) {
            EXPECT_EQ("mask1", fileIdentity.mask());
        }
        if (fileIdentity.filePath() == filePath3.filePath()) {
            EXPECT_EQ("mask2", fileIdentity.mask());
        }
    }
    EXPECT_EQ("cmd --p1 v1 --p2 --p3 v3 filepath1 --file2 filepath2 --file3 filepath3", commandRestoredPtr->string());
}

TEST(CommandWrapperBuilder, restore_single_cmd_from_string_automatic_mask_detection)
{
    ScopedFile archFilePath{std::filesystem::path{"arch/filepath"}, "shared content 1..."};
    ScopedFile filePath2{std::filesystem::path{"filepath2"}, "shared content 2..."};

    CommandWrapper commandOrig{"/some/path/to/vpr"};
    commandOrig.appendFile(archFilePath.filePath(), VPR_ARCH_FILE_MASK); // for automatic VPR_ARCH_FILE_MASK detect it must be first argument after the vpr command
    commandOrig.append("--p1", "v1");
    commandOrig.append("--p2");
    commandOrig.append("--p3", "v3");
    commandOrig.appendFile("--file2", filePath2.filePath());

    CommandWrapperPtr commandRestoredPtr = CommandWrapperBuilder::fromString(commandOrig.string());
    for (const auto& [key, fileIdentity]: commandRestoredPtr->files()) {
        if (fileIdentity.filePath() == archFilePath.filePath()) {
            EXPECT_EQ(VPR_ARCH_FILE_MASK, fileIdentity.mask());
        }
    }
    EXPECT_EQ("/some/path/to/vpr arch/filepath --p1 v1 --p2 --p3 v3 --file2 filepath2", commandRestoredPtr->string());
}

TEST(CommandWrapperBuilder, restore_multiple_cmds_from_string)
{
    // TODO: https://github.com/QL-Proprietary/aurora2/issues/1306
}