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

#ifdef DEBUG_NETLIST

    /* base/vpr_api.cpp
    void vpr_print_arch_resources(const t_vpr_setup& vpr_setup, const t_arch& Arch) {
    ...
    vpr_create_device(vpr_setup, arch);
    // Dump atom netlist (DEBUG FLOORPLANNING)
    {
        std::ofstream atom_file("atom_netlist.txt");
        const auto& atom_netlist = g_vpr_ctx.atom().netlist();
        for (auto blk_id: atom_netlist.blocks()) {
            atom_file << atom_netlist.block_name(blk_id)) << "\n";
        }
    }
    // Dump atom netlist (DEBUG FLOORPLANNING)
     */
    QList<QString> lines;
    for (const std::string& element: m_elements) {
        lines.append(QString::fromStdString(element));
    }
    FOEDAG::FileUtils::WriteToFile("aurora_floorplanning_netlist.txt", lines.join("\n").toStdString());
#endif

    return true;
}

bool SynthResourceExtractor::parseAtomNamesFromBlifFileContent(const std::string& fileContent) {
  m_elements.clear();

  std::string content{fileContent};
  FOEDAG::StringUtils::replaceAllInPlace(content, "\\n", "");
  auto lines = FOEDAG::StringUtils::tokenize(content, "\n");

  for (const auto& line : lines) {
    if (!FOEDAG::StringUtils::startsWith(line, ".subckt")) {
      continue;
    }

    // extract token after ".subckt" assuming it's atom element
    size_t pos = strlen(".subckt");
    while (pos < line.size() && std::isspace((unsigned char)line[pos])) {
      ++pos;
    }

    size_t start = pos;
    while (pos < line.size() && !std::isspace((unsigned char)line[pos])) {
      ++pos;
    }

    if (start == pos) {
      continue;
    }

    std::string inst = line.substr(start, pos - start);
    m_elements.insert(inst);
  }

  return true;
}

}  // namespace fp
