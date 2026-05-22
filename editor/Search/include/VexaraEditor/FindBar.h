#pragma once

#include <QWidget>

class QLineEdit;
class QPushButton;

namespace VexaraEditor {

class FindBar : public QWidget {
    Q_OBJECT

public:
    explicit FindBar(QWidget* parent = nullptr);

    QString query() const;
    void focusQuery();

signals:
    void findNextRequested();
    void findPreviousRequested();

private:
    QLineEdit* queryField_ = nullptr;
    QPushButton* nextButton_ = nullptr;
    QPushButton* previousButton_ = nullptr;
};

} // namespace VexaraEditor
