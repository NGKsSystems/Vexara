#include "VexaraEditor/CodeEditorWidget.h"

#include "VexaraEditor/TextContextMenu.h"

#include <QColor>
#include <QPainter>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextDocument>
#include <QVBoxLayout>

namespace VexaraEditor {

namespace {

class EditorPane : public QPlainTextEdit {
public:
    using QPlainTextEdit::QPlainTextEdit;

    void setGutterMargin(int left)
    {
        setViewportMargins(left, 0, 0, 0);
    }

    friend class LineNumberArea;
};

class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(EditorPane* editor, QWidget* parent = nullptr)
        : QWidget(parent)
        , editor_(editor)
    {
    }

    QSize sizeHint() const override
    {
        int digits = 1;
        int max = qMax(1, editor_->blockCount());
        while (max >= 10) {
            max /= 10;
            ++digits;
        }
        const int space = 12 + editor_->fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
        return QSize(space, 0);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QPainter painter(this);
        painter.fillRect(event->rect(), QColor(245, 245, 245));

        QTextBlock block = editor_->firstVisibleBlock();
        int blockNumber = block.blockNumber();
        int top = qRound(editor_->blockBoundingGeometry(block).translated(editor_->contentOffset()).top());
        int bottom = top + qRound(editor_->blockBoundingRect(block).height());

        painter.setPen(QColor(120, 120, 120));
        while (block.isValid() && top <= event->rect().bottom()) {
            if (block.isVisible() && bottom >= event->rect().top()) {
                const QString number = QString::number(blockNumber + 1);
                painter.drawText(0, top, width() - 4, editor_->fontMetrics().height(),
                                 Qt::AlignRight, number);
            }

            block = block.next();
            top = qRound(editor_->blockBoundingGeometry(block).translated(editor_->contentOffset()).top());
            bottom = top + qRound(editor_->blockBoundingRect(block).height());
            ++blockNumber;
        }
    }

private:
    EditorPane* editor_ = nullptr;
};

} // namespace

CodeEditorWidget::CodeEditorWidget(QWidget* parent)
    : QWidget(parent)
{
    editor_ = new EditorPane(this);
    lineNumberArea_ = new LineNumberArea(static_cast<EditorPane*>(editor_), this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(editor_);

    connect(editor_, &QPlainTextEdit::blockCountChanged, this, [this](int) {
        updateLineNumberAreaWidth();
    });
    connect(editor_, &QPlainTextEdit::updateRequest, this, [this](const QRect& rect, int dy) {
        if (dy) {
            lineNumberArea_->scroll(0, dy);
        } else {
            lineNumberArea_->update(0, rect.y(), lineNumberArea_->width(), rect.height());
        }
        if (rect.contains(editor_->viewport()->rect())) {
            updateLineNumberAreaWidth();
        }
    });
    connect(editor_, &QPlainTextEdit::cursorPositionChanged, this, [this]() {
        highlightCurrentLine();
    });

    installPlainTextContextMenu(editor_);

    updateLineNumberAreaWidth();
    highlightCurrentLine();
}

QPlainTextEdit* CodeEditorWidget::editor() const
{
    return editor_;
}

QTextDocument* CodeEditorWidget::document() const
{
    return editor_->document();
}

void CodeEditorWidget::setPlainText(const QString& text)
{
    editor_->setPlainText(text);
}

QString CodeEditorWidget::toPlainText() const
{
    return editor_->toPlainText();
}

bool CodeEditorWidget::find(const QString& text, QTextDocument::FindFlags flags)
{
    return editor_->find(text, flags);
}

void CodeEditorWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    const QRect cr = editor_->contentsRect();
    const int width = lineNumberArea_->sizeHint().width();
    lineNumberArea_->setGeometry(QRect(cr.left(), cr.top(), width, cr.height()));
}

void CodeEditorWidget::updateLineNumberAreaWidth(int)
{
    const int width = lineNumberArea_->sizeHint().width();
    static_cast<EditorPane*>(editor_)->setGutterMargin(width);
}

void CodeEditorWidget::highlightCurrentLine()
{
    QList<QTextEdit::ExtraSelection> extra;
    QTextEdit::ExtraSelection selection;
    selection.format.setBackground(QColor(232, 242, 255));
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    selection.cursor = editor_->textCursor();
    selection.cursor.clearSelection();
    extra.append(selection);
    editor_->setExtraSelections(extra);
}

} // namespace VexaraEditor
