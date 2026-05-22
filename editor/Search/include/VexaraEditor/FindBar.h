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
    QString replacement() const;
    void focusQuery();
    void focusReplace();

signals:
    void findNextRequested();
    void findPreviousRequested();
    void replaceRequested();
    void replaceAllRequested();

private:
    QLineEdit* findField_ = nullptr;
    QLineEdit* replaceField_ = nullptr;
    QPushButton* nextButton_ = nullptr;
    QPushButton* previousButton_ = nullptr;
    QPushButton* replaceButton_ = nullptr;
    QPushButton* replaceAllButton_ = nullptr;
};

} // namespace VexaraEditor
