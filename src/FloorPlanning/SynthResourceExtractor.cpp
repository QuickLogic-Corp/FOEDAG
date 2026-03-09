#include "SynthResourceExtractor.h"

#include <QList>
#include <QDomDocument>
#include <QDomElement>
#include <QDomNodeList>
#include <QString>

#include <functional>

#include "Utils/FileUtils.h"
#include "Utils/StringUtils.h"

namespace fp {

bool SynthResourceExtractor::loadAtomNamesFromNetFile(const std::filesystem::path& filePath)
{
  if (!std::filesystem::exists(filePath)) {
    return false;
  }
  if (filePath.extension() != ".net") {
    return false;
  }
  return parseAtomNamesFromNetFileContent(FOEDAG::FileUtils::GetFileContent(filePath));
}

bool SynthResourceExtractor::loadAtomNamesFromBlifFile(const std::filesystem::path& filePath)
{
  if (!std::filesystem::exists(filePath)) {
    return false;
  }
  if (filePath.extension() != ".blif") {
    return false;
  }
  return parseAtomNamesFromBlifFileContent(FOEDAG::FileUtils::GetFileContent(filePath));
}

bool SynthResourceExtractor::loadAtomNamesFromTxtFile(const std::filesystem::path& filePath)
{
  if (!std::filesystem::exists(filePath)) {
    return false;
  }
  if (filePath.extension() != ".txt") {
    return false;
  }
  return parseAtomNamesFromTxtFileContent(FOEDAG::FileUtils::GetFileContent(filePath));
}

bool SynthResourceExtractor::parseAtomNamesFromNetFileContent(const std::string& fileContent)
{
    m_error.clear();

    QDomDocument doc;

    QString errMsg; 
    int errLine = 0;
    int errCol = 0;

    if (!doc.setContent(QByteArray::fromStdString(fileContent), &errMsg, &errLine, &errCol)) {
        m_error = "XML parse error at" + std::to_string(errLine) + ":" + std::to_string(errCol) + "-" + errMsg.toStdString();
        return false;
    }

    static std::function<void(const QDomElement&, std::set<std::string>&)>
        collectLeafBlocksRecursive = [&](const QDomElement& element, std::set<std::string>& atoms)
    {
        if (element.isNull()) {
            return;
        }

        const bool isBlock = (element.tagName() == "block");

        if (isBlock) {
            // iterate only direct child <block> elements
            QDomElement childBlock = element.firstChildElement("block");
            if (!childBlock.isNull()) {
                for (; !childBlock.isNull(); childBlock = childBlock.nextSiblingElement("block")) {
                    collectLeafBlocksRecursive(childBlock, atoms);
                }
            } else {
                // leaf <block>
                const QString name = element.attribute("name");
                if (!name.isEmpty() && (name != "open")) {
                    atoms.insert(name.toStdString());
                }
            }
        } else {
            // not a <block>, traverse all element children
            for (QDomElement childElement = element.firstChildElement(); !childElement.isNull(); childElement = childElement.nextSiblingElement()) {
                collectLeafBlocksRecursive(childElement, atoms);
            }
        }
    };

    m_elements.clear();
    collectLeafBlocksRecursive(doc.documentElement(), m_elements);

    return true;
}

bool SynthResourceExtractor::parseAtomNamesFromBlifFileContent(const std::string& fileContent)
{
  m_elements.clear();

  std::string content{fileContent};

  // Normalize line endings first
  FOEDAG::StringUtils::replaceAllInPlace(content, "\r\n", "\n");
  FOEDAG::StringUtils::replaceAllInPlace(content, "\r", "\n");

  // Remove line continuation backslash-newline
  FOEDAG::StringUtils::replaceAllInPlace(content, "\\\n", "");

  auto lines = FOEDAG::StringUtils::tokenize(content, "\n");

  constexpr const char* keySubckt = ".subckt";
  constexpr const char* keyNames = ".names";
  for (const auto& line : lines) {
    if (FOEDAG::StringUtils::startsWith(line, keySubckt)) {
      // extract token right after token ".subckt" if line starts with ".subckt"
      const size_t base = strlen(keySubckt);
      const size_t start = line.find_first_not_of(" \t", base);
      if (start != std::string::npos) {
        const size_t end = line.find_first_of(" \t", start);
        const size_t len = (end == std::string::npos) ? (line.size() - start) : (end - start);
        m_elements.emplace(line.data() + start, len);
      }
    } else if (FOEDAG::StringUtils::startsWith(line, keyNames)) {
      // extract last token in line which starts with ".names"
      const size_t end = line.find_last_not_of(" \t");
      if (end != std::string::npos) {
        const size_t start = line.find_last_of(" \t", end);
        const size_t tokenStart = (start == std::string::npos) ? 0 : (start + 1);
        m_elements.emplace(line.data() + tokenStart, end - tokenStart + 1);
      }
    }
  }

  return true;
}

bool SynthResourceExtractor::parseAtomNamesFromTxtFileContent(const std::string& fileContent)
{
  m_elements.clear();

  auto lines = FOEDAG::StringUtils::tokenize(fileContent, "\n");

  for (const auto& line : lines) {
    m_elements.insert(line);
  }

  return true;
}

}  // namespace fp
