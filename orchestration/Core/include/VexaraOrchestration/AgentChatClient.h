#pragma once

#include "VexaraCore/ModelProfile.h"
#include "VexaraOrchestration/ChatMessage.h"

#include <QObject>
#include <QString>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

namespace VexaraOrchestration {

class AgentChatClient : public QObject {
    Q_OBJECT

public:
    explicit AgentChatClient(QObject* parent = nullptr);
    ~AgentChatClient() override;

    bool isBusy() const;

    void sendMessage(const VexaraCore::ModelProfile& profile,
                     const QString& agentDisplayName,
                     const QVector<ChatMessage>& history,
                     const QString& userText,
                     const QString& systemContext);

signals:
    void requestStarted(const QString& agentDisplayName);
    void requestFinished(bool success, const QString& reply, const QString& errorSummary);

private:
    void finishRequest(bool success, const QString& reply, const QString& errorSummary);
    void onReplyFinished(QNetworkReply* reply);

    QNetworkAccessManager* network_ = nullptr;
    QNetworkReply* activeReply_ = nullptr;
};

} // namespace VexaraOrchestration
