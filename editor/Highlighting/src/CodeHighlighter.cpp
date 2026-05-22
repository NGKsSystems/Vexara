#include "VexaraEditor/CodeHighlighter.h"

#include <QColor>
#include <QFont>
#include <QRegularExpression>
#include <QTextDocument>

namespace VexaraEditor {

namespace {

struct Rule {
    QRegularExpression pattern;
    QTextCharFormat format;
};

QTextCharFormat makeFormat(const QColor& color, bool bold = false)
{
    QTextCharFormat format;
    format.setForeground(color);
    if (bold) {
        format.setFontWeight(QFont::Bold);
    }
    return format;
}

QVector<Rule> rulesForLanguage(const QString& language)
{
    QVector<Rule> rules;

    const QTextCharFormat keyword = makeFormat(QColor(0, 0, 200), true);
    const QTextCharFormat stringFmt = makeFormat(QColor(163, 21, 21));
    const QTextCharFormat comment = makeFormat(QColor(0, 128, 0));
    const QTextCharFormat number = makeFormat(QColor(9, 134, 88));

    if (language == QStringLiteral("cpp") || language == QStringLiteral("python")) {
        rules.append(Rule{QRegularExpression(QStringLiteral("\\b(class|const|return|if|else|for|while|struct|namespace|public|private|protected|void|int|bool|auto|include|define|import|from|def)\\b")), keyword});
        rules.append(Rule{QRegularExpression(QStringLiteral("//[^\n]*")), comment});
        rules.append(Rule{QRegularExpression(QStringLiteral("\".*\"")), stringFmt});
        rules.append(Rule{QRegularExpression(QStringLiteral("'.*'")), stringFmt});
        rules.append(Rule{QRegularExpression(QStringLiteral("\\b[0-9]+\\b")), number});
        return rules;
    }

    if (language == QStringLiteral("json") || language == QStringLiteral("toml")) {
        rules.append(Rule{QRegularExpression(QStringLiteral("\".*\"")), stringFmt});
        rules.append(Rule{QRegularExpression(QStringLiteral("\\b(true|false|null)\\b")), keyword});
        rules.append(Rule{QRegularExpression(QStringLiteral("\\b[0-9]+(\\.[0-9]+)?\\b")), number});
        return rules;
    }

    if (language == QStringLiteral("markdown")) {
        rules.append(Rule{QRegularExpression(QStringLiteral("^#{1,6} .*")), makeFormat(QColor(0, 0, 128), true)});
        rules.append(Rule{QRegularExpression(QStringLiteral("\\*\\*[^*]+\\*\\*")), makeFormat(QColor(0, 0, 0), true)});
        return rules;
    }

    if (language == QStringLiteral("markup")) {
        rules.append(Rule{QRegularExpression(QStringLiteral("<[^>]+>")), makeFormat(QColor(128, 0, 128))});
        return rules;
    }

    return rules;
}

} // namespace

CodeHighlighter::CodeHighlighter(QTextDocument* parent)
    : QSyntaxHighlighter(parent)
{
    setLanguage(QStringLiteral("plain"));
}

void CodeHighlighter::setLanguage(const QString& language)
{
    QVector<HighlightRule> converted;
    for (const Rule& rule : rulesForLanguage(language)) {
        HighlightRule entry;
        entry.pattern = rule.pattern;
        entry.format = rule.format;
        converted.append(entry);
    }
    applyRules(converted);
}

void CodeHighlighter::highlightBlock(const QString& text)
{
    for (const HighlightRule& rule : rules_) {
        QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }
}

void CodeHighlighter::applyRules(const QVector<HighlightRule>& rules)
{
    rules_ = rules;
    rehighlight();
}

} // namespace VexaraEditor
