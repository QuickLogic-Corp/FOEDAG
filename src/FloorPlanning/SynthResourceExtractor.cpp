#include "SynthResourceExtractor.h"

#include <QList>
#include <QDomDocument>
#include <QDomElement>
#include <QDomNodeList>
#include <QString>

#include <functional>

#ifdef DEBUG_NETLIST
#include <Utils/FileUtils.h>
#endif

namespace fp {

bool SynthResourceExtractor::parseNetFileContent(const std::string& content)
{
    m_error.clear();

    QDomDocument doc;

    QString errMsg; 
    int errLine = 0;
    int errCol = 0;

    if (!doc.setContent(QByteArray::fromStdString(content), &errMsg, &errLine, &errCol)) {
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

}  // namespace fp
