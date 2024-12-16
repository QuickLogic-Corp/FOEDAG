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

#include "ComboBox.h"

class QAbstractItemModel;
class QShowEvent;

namespace FOEDAG {

/*!
 * \brief The LazyComboBox class
 *
 * This implementation of a combo box delays model setup until the model is required, such as during:
 * 1) Show event
 * 2) Popup event
 * 3) Setting the current text
 * 4) Setting the current index
 *
 * This approach allows the use of a large number of combo boxes with faster initialization.
 */
class LazyComboBox : public ComboBox {
  Q_OBJECT
 public:
  explicit LazyComboBox(QWidget *parent = nullptr);

  void setCurrentText(const QString&);
  void setCurrentIndex(int);

  void setDelayedModel(QAbstractItemModel* model) { m_delayedModel = model; }

 protected:
  void showPopup() override final;
  void showEvent(QShowEvent*) override final;

 private:
  QAbstractItemModel* m_delayedModel{nullptr};
  void bindDelayedModel();
};

}  // namespace FOEDAG
