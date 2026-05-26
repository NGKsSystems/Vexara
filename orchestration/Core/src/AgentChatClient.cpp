#include "VexaraOrchestration/AgentChatClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace VexaraOrchestration {

namespace {

QString chatEndpoint(const VexaraCore::ModelProfile& profile)
{
    if (!profile.apiBaseUrl.trimmed().isEmpty()) {
        QString base = profile.apiBaseUrl.trimmed();
        if (base.endsWith(QLatin1Char('/'))) {
            base.chop(1);
        }
        if (base.endsWith(QStringLiteral("/v1"))) {
            return base + QStringLiteral("/chat/completions");
        }
        if (base.contains(QStringLiteral("/chat/completions"))) {
            return base;
        }
        return base + QStringLiteral("/v1/chat/completions");
    }

    switch (profile.provider) {
    case VexaraCore::ModelProvider::Xai:
        return QStringLiteral("https://api.x.ai/v1/chat/completions");
    case VexaraCore::ModelProvider::OpenAi:
        return QStringLiteral("https://api.openai.com/v1/chat/completions");
    case VexaraCore::ModelProvider::OpenRouter:
        return QStringLiteral("https://openrouter.ai/api/v1/chat/completions");
    default:
        return QString();
    }
}

QJsonArray buildMessages(const QVector<ChatMessage>& history,
                         const QString& userText,
                         const QString& systemContext)
{
    QJsonArray messages;
    if (!systemContext.trimmed().isEmpty()) {
        QJsonObject system;
        system.insert(QStringLiteral("role"), QStringLiteral("system"));
        system.insert(QStringLiteral("content"), systemContext);
        messages.append(system);
    }

    for (const ChatMessage& entry : history) {
        if (entry.role == QStringLiteral("system")) {
            continue;
        }
        QJsonObject message;
        message.insert(QStringLiteral("role"), entry.role);
        message.insert(QStringLiteral("content"), entry.content);
        messages.append(message);
    }

    QJsonObject userMessage;
    userMessage.insert(QStringLiteral("role"), QStringLiteral("user"));
    userMessage.insert(QStringLiteral("content"), userText);
    messages.append(userMessage);
    return messages;
}

QString extractAssistantText(const QJsonObject& root)
{
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        return QString();
    }
    const QJsonObject first = choices.first().toObject();
    const QJsonObject message = first.value(QStringLiteral("message")).toObject();
    return message.value(QStringLiteral("content")).toString();
}

QString extractErrorText(const QJsonObject& root, const QByteArray& body)
{
    const QJsonObject error = root.value(QStringLiteral("error")).toObject();
    const QString message = error.value(QStringLiteral("message")).toString();
    if (!message.isEmpty()) {
        return message;
    }
    const QString trimmed = QString::fromUtf8(body).trimmed();
    if (!trimmed.isEmpty()) {
        return trimmed.left(500);
    }
    return QStringLiteral("Chat request failed.");
}

} // namespace

AgentChatClient::AgentChatClient(QObject* parent)
    : QObject(parent)
    , network_(new QNetworkAccessManager(this))
{
}

AgentChatClient::~AgentChatClient()
{
    if (activeReply_) {
        activeReply_->abort();
        activeReply_->deleteLater();
        activeReply_ = nullptr;
    }
}

bool AgentChatClient::isBusy() const
{
    return activeReply_ != nullptr;
}

void AgentChatClient::sendMessage(const VexaraCore::ModelProfile& profile,
                                  const QString& agentDisplayName,
                                  const QVector<ChatMessage>& history,
                                  const QString& userText,
                                  const QString& systemContext)
{
    if (isBusy()) {
        emit requestFinished(false, QString(), QStringLiteral("A chat request is already in progress."));
        return;
    }

    const QString trimmed = userText.trimmed();
    if (trimmed.isEmpty()) {
        emit requestFinished(false, QString(), QStringLiteral("Enter a message first."));
        return;
    }

    if (!profile.isUsableForChat()) {
        const QString hint = profile.apiKeyEnv.isEmpty()
                                 ? QStringLiteral("Add api_key or api_key_env for this model in vexara.json.")
                                 : QStringLiteral("Set the %1 environment variable or api_key in vexara.json.")
                                       .arg(profile.apiKeyEnv);
        emit requestFinished(
            false,
            QString(),
            QStringLiteral("%1 is not ready for chat (OpenAI/xAI/OpenRouter only). %2")
                .arg(profile.displayName.isEmpty() ? profile.id : profile.displayName, hint));
        return;
    }

    const QString endpoint = chatEndpoint(profile);
    if (endpoint.isEmpty()) {
        emit requestFinished(false,
                             QString(),
                             QStringLiteral("Provider is not supported for in-app chat yet."));
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("model"), profile.modelName);
    body.insert(QStringLiteral("messages"), buildMessages(history, trimmed, systemContext));

    QNetworkRequest httpRequest{QUrl(endpoint)};
    httpRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    httpRequest.setRawHeader("Authorization",
                             QByteArray("Bearer ") + profile.resolvedApiKey().toUtf8());

    emit requestStarted(agentDisplayName);
    activeReply_ =
        network_->post(httpRequest, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(activeReply_, &QNetworkReply::finished, this, [this]() {
        onReplyFinished(activeReply_);
    });
}

void AgentChatClient::onReplyFinished(QNetworkReply* reply)
{
    if (!reply) {
        return;
    }

    reply->deleteLater();
    if (reply != activeReply_) {
        return;
    }
    activeReply_ = nullptr;

    const QByteArray body = reply->readAll();
    const QJsonDocument document = QJsonDocument::fromJson(body);
    const QJsonObject root = document.isObject() ? document.object() : QJsonObject();

    if (reply->error() != QNetworkReply::NoError) {
        finishRequest(false, QString(), extractErrorText(root, body));
        return;
    }

    const QString assistantText = extractAssistantText(root);
    if (assistantText.isEmpty()) {
        finishRequest(false, QString(), extractErrorText(root, body));
        return;
    }

    finishRequest(true, assistantText, QString());
}

void AgentChatClient::finishRequest(bool success, const QString& reply, const QString& errorSummary)
{
    emit requestFinished(success, reply, errorSummary);
}

} // namespace VexaraOrchestration
