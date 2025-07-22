#pragma once

#include <QComboBox>
#include <QList>
#include <QString>

namespace FOEDAG {

class MultiComboBox : public QWidget {
    Q_OBJECT

public:
    explicit MultiComboBox(QWidget* parent=nullptr);

    void addItem(const QString& text);

    QString currentText() const;
    void setCurrentText(const QString&);

 signals:
    void currentTextChanged(QString);

private slots:
    void updateDisplayText();

private:
    QComboBox* m_combo{nullptr};

    QList<QString> selectedItems() const;
    void setSelectedItems(const QList<QString>&);
};

}  // namespace FOEDAG
