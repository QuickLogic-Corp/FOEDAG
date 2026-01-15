#include "SynthResourceExtractor.h"

#include <QDomDocument>
#include <QDomElement>
#include <QDomNodeList>

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
        m_error = "XML parse error at" + QString::number(errLine) + ":" + QString::number(errCol) + "-" + errMsg;
        return false;
    }

    static std::function<void(const QDomElement&, std::vector<QDomElement>&)>
        collectLeafAtomsRecursive = [](const QDomElement& block, std::vector<QDomElement>& out)
    {
        if (block.isNull() || (block.tagName() != "block")) {
            return;
        }

        // Check if this block has any child <block>
        QDomElement child = block.firstChildElement("block");
        if (child.isNull()) {
            const QString name = block.attribute("name");
            if (!name.isEmpty()) {
                out.push_back(block);
            }
            return;
        }

        // Recurse into direct child blocks only
        for (; !child.isNull(); child = child.nextSiblingElement("block")) {
            collectLeafAtomsRecursive(child, out);
        }
    };

    std::vector<QDomElement> atoms;
    collectLeafAtomsRecursive(doc.documentElement(), atoms);

    for (const QDomElement& a: atoms) {
        const QString name = a.attribute("name");
        m_resources.add(name.toStdString());
    }

#ifdef DEBUG_NETLIST
    QList<QString> lines;
    for (const std::string& element: m_resources.atoms) {
        lines.append(QString::fromStdString(element));
    }
    FOEDAG::FileUtils::WriteToFile("aurora_floorplanning_netlist.txt", lines.join("\n").toStdString());
#endif

    return true;
}

}  // namespace fp
