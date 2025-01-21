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
#include "LazyComboBox.h"

namespace FOEDAG {

LazyComboBox::LazyComboBox(QWidget *parent) : ComboBox(parent) {
}

void LazyComboBox::showPopup() {
  if (m_delayedModel) {
    bindDelayedModel();
  }
  QComboBox::showPopup();
}

void LazyComboBox::showEvent(QShowEvent* event) {
  if (m_delayedModel) {
    bindDelayedModel();
  }
  QComboBox::showEvent(event);
}

void LazyComboBox::bindDelayedModel()
{
  blockSignals(true);
  QComboBox::setModel(m_delayedModel);
  blockSignals(false);
  m_delayedModel = nullptr;
}

void LazyComboBox::setCurrentText(const QString& text)
{
  if (m_delayedModel) {
    bindDelayedModel();
  }
  QComboBox::setCurrentText(text);
}

void LazyComboBox::setCurrentIndex(int index)
{
  if (m_delayedModel) {
    bindDelayedModel();
  }
  QComboBox::setCurrentIndex(index);
}

}  // namespace FOEDAG
