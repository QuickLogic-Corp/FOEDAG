#pragma once

#include <QPushButton>

namespace fp {

class CheckableButton final : public QPushButton {
  Q_OBJECT
 public:
  CheckableButton(QIcon, QWidget* parent = nullptr);

 protected:
  void paintEvent(QPaintEvent* event) override final;
};

}  // namespace fp
