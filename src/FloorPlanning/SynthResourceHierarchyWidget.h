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

#pragma once

#include <QWidget>
#include <QTreeView>
#include <QStandardItemModel>

#include <set>
#include <string>
#include <filesystem>

namespace FOEDAG {

class SynthResourceHierarchyWidget : public QWidget {
    Q_OBJECT

    enum COLUMN {
      NETLIST = 0,
      REGION
    };
public:
    explicit SynthResourceHierarchyWidget(QWidget* parent = nullptr);

    void loadPostSynthNetFile(const std::filesystem::path& path);

    std::set<std::string> collectSelectedElements() const;
    void setSelectedElements(int id, const std::set<std::string>& elements);
    void clearSelectedElements();

signals:
    void selectedElementsChanged(std::set<std::string>);

private:
    QLineEdit* m_leSearch{nullptr};
    QTreeView* m_view{nullptr};
    QStandardItemModel* m_model{nullptr};

    QStandardItem* findRegionItem(QStandardItem* parent, const QString& text);

    void focusItem(const QString&);

    void addPath(const std::string&);
    void onItemChanged(QStandardItem*);

    void showOnlyCheckedItems();
    void showAllItems();
};

}  // namespace FOEDAG
