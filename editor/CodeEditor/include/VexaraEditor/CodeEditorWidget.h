#pragma once

#include <QTextDocument>
#include <QWidget>

class QPlainTextEdit;

namespace VexaraEditor {

class CodeEditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit CodeEditorWidget(QWidget* parent = nullptr);

    QPlainTextEdit* editor() const;
    QTextDocument* document() const;

    void setPlainText(const QString& text);
    QString toPlainText() const;
    bool find(const QString& text, QTextDocument::FindFlags flags = {});

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateLineNumberAreaWidth(int blockCount = 0);
    void highlightCurrentLine();

    QPlainTextEdit* editor_ = nullptr;
    QWidget* lineNumberArea_ = nullptr;
};

} // namespace VexaraEditor
