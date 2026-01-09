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

#include "SynthResourceExtractor.h"

#include <QDomDocument>
#include <QDomElement>
#include <QDomNodeList>

#include <Utils/FileUtils.h>

namespace FOEDAG {

void SynthResourceExtractor::parseNetFileContent(const std::string& content)
{
    QDomDocument doc;

    QString errMsg; 
    int errLine = 0;
    int errCol = 0;

    if (!doc.setContent(QByteArray::fromStdString(content), &errMsg, &errLine, &errCol)) {
        qWarning() << "XML parse error at" << errLine << ":" << errCol << "-" << errMsg;
        return;
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

    // debug
    QList<QString> lines;
    for (const std::string& element: m_resources.atoms) {
        lines.append(QString::fromStdString(element));
    }
    FileUtils::WriteToFile("aurora_floorplanning_netlist.txt", lines.join("\n").toStdString());
    // debug
}

}  // namespace FOEDAG
