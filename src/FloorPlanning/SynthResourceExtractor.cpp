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

    QDomNodeList blocks = doc.elementsByTagName("block");

    for (int i=0; i<blocks.count(); ++i) {
        const QDomElement element = blocks.at(i).toElement();
        BlockType type = determineBlockType(element);
        if (type != BlockType::UNKNOWN) {
            QDomNodeList subBlocks = element.elementsByTagName("block");
            for (int j=0; j<subBlocks.count(); ++j) {
                const QDomElement subElement = subBlocks.at(j).toElement();
                if (determineBlockType(subElement) == BlockType::FLE) {
                    const std::string name = subElement.attribute("name").toStdString();
                    if (!name.empty()) {
                        m_resources.add(type, name);
                    }
                }
            }
        }
    }
}

BlockType SynthResourceExtractor::determineBlockType(const QDomElement& element) const
{
    const auto instance = element.attribute("instance");
    if (instance.contains("fle")) {
        return BlockType::FLE;
    } else if (instance.contains("clb")) {
        return BlockType::CLB;
    } else if (instance.contains("bram")) {
        return BlockType::BRAM;
    } else if (instance.contains("dsp")) {
        return BlockType::DSP;
    }
    return BlockType::UNKNOWN;
}

}  // namespace FOEDAG
