#pragma once

#include <QPushButton>
#include <QPen>

namespace fp {

class CheckableButton final : public QPushButton {
  Q_OBJECT
 public:
  CheckableButton(const QIcon&, QWidget* parent = nullptr);
  CheckableButton(const QIcon&, const QIcon&, QWidget* parent = nullptr);

 protected:
  void paintEvent(QPaintEvent* event) override final;

 private:
  bool m_hightLightWhenChecked = false;
  QPen m_hightLightPen;
  double m_lineWidth = 2.0;
  QColor m_hightLightColor = QColor(50, 160, 50, 100);

  QIcon m_checkedIcon;
  QIcon m_uncheckedIcon;
};

}  // namespace fp
