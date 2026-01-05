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
public:
    explicit SynthResourceHierarchyWidget(QWidget* parent = nullptr);

    void loadPostSynthNetFile(const std::filesystem::path& path);

    std::set<std::string> selectedItems(bool leavesOnly = false) const;
    void setSelectedItems(const std::set<std::string>& items);

signals:
    void selectionChanged(std::set<std::string>);

private:
    QStandardItem* buildCategory(const QString& title,
                                 const std::set<std::string>& items);

    QTreeView* m_view{nullptr};
    QStandardItemModel* m_model{nullptr};
};

}  // namespace FOEDAG
