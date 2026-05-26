#include "VexaraEditor/ApiKeyValidator.h"

#include "VexaraCore/ModelProfile.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace VexaraEditor {

namespace {

QString validationUrl(VexaraCore::ModelProvider provider)
{
    switch (provider) {
    case VexaraCore::ModelProvider::Xai:
        return QStringLiteral("https://api.x.ai/v1/models");
    case VexaraCore::ModelProvider::OpenAi:
        return QStringLiteral("https://api.openai.com/v1/models");
    case VexaraCore::ModelProvider::Anthropic:
        return QStringLiteral("https://api.anthropic.com/v1/models");
    case VexaraCore::ModelProvider::OpenRouter:
        return QStringLiteral("https://openrouter.ai/api/v1/models");
    default:
        return QString();
    }
}

void applyAuthHeaders(QNetworkRequest& httpRequest,
                      VexaraCore::ModelProvider provider,
                      const QString& apiKey)
{
    switch (provider) {
    case VexaraCore::ModelProvider::Anthropic:
        httpRequest.setRawHeader("x-api-key", apiKey.toUtf8());
        httpRequest.setRawHeader("anthropic-version", "2023-06-01");
        break;
    default:
        httpRequest.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey.toUtf8());
        break;
    }
}

QString errorMessageFromBody(const QJsonObject& root, const QByteArray& body, int httpStatus)
{
    const QJsonObject error = root.value(QStringLiteral("error")).toObject();
    QString message = error.value(QStringLiteral("message")).toString();
    if (message.isEmpty()) {
        message = root.value(QStringLiteral("message")).toString();
    }
    if (message.isEmpty() && !body.isEmpty()) {
        message = QString::fromUtf8(body).trimmed().left(240);
    }
    if (message.isEmpty()) {
        message = QStringLiteral("HTTP %1").arg(httpStatus);
    }
    return message;
}

} // namespace

bool canValidateProvider(VexaraCore::ModelProvider provider)
{
    return !validationUrl(provider).isEmpty();
}

QString resolveKeyForValidation(const VexaraCore::ModelProfile& profile, const QString& apiKeyOverride)
{
    if (!apiKeyOverride.trimmed().isEmpty()) {
        return apiKeyOverride.trimmed();
    }
    return profile.resolvedApiKey();
}

ApiKeyValidator::ApiKeyValidator(QObject* parent)
    : QObject(parent)
    , network_(new QNetworkAccessManager(this))
{
}

ApiKeyValidator::~ApiKeyValidator()
{
    cancelAll();
}

bool ApiKeyValidator::isBusy() const
{
    return activeReply_ != nullptr;
}

void ApiKeyValidator::cancelAll()
{
    if (activeReply_) {
        activeReply_->abort();
        activeReply_->deleteLater();
        activeReply_ = nullptr;
    }
}

void ApiKeyValidator::validateProfile(const VexaraCore::ModelProfile& profile,
                                      const QString& apiKeyOverride)
{
    if (isBusy()) {
        emit validationFinished(profile.id,
                              false,
                              QStringLiteral("Another key check is already running."));
        return;
    }

    if (!canValidateProvider(profile.provider)) {
        emit validationFinished(
            profile.id,
            false,
            QStringLiteral("Online validation is not available for %1 yet.")
                .arg(VexaraCore::modelProviderLabel(profile.provider)));
        return;
    }

    const QString apiKey = resolveKeyForValidation(profile, apiKeyOverride);
    if (apiKey.isEmpty()) {
        const QString hint = profile.apiKeyEnv.isEmpty()
                                 ? QStringLiteral("Enter an API key or set an environment variable.")
                                 : QStringLiteral("Enter a key above or set %1 in your environment.")
                                       .arg(profile.apiKeyEnv);
        emit validationFinished(profile.id, false, hint);
        return;
    }

    const QString endpoint = validationUrl(profile.provider);
    QNetworkRequest httpRequest{QUrl(endpoint)};
    applyAuthHeaders(httpRequest, profile.provider, apiKey);

    emit validationStarted(profile.id);
    activeReply_ = network_->get(httpRequest);
    connect(activeReply_, &QNetworkReply::finished, this, [this, profileId = profile.id]() {
        finishReply(activeReply_, profileId);
    });
}

void ApiKeyValidator::finishReply(QNetworkReply* reply, const QString& profileId)
{
    if (!reply) {
        return;
    }

    reply->deleteLater();
    if (reply != activeReply_) {
        return;
    }
    activeReply_ = nullptr;

    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const QJsonDocument document = QJsonDocument::fromJson(body);
    const QJsonObject root = document.isObject() ? document.object() : QJsonObject();

    if (reply->error() == QNetworkReply::NoError && httpStatus >= 200 && httpStatus < 300) {
        emit validationFinished(profileId, true, QStringLiteral("Valid — key works."));
        return;
    }

    if (httpStatus == 401 || httpStatus == 403) {
        emit validationFinished(profileId,
                              false,
                              QStringLiteral("Invalid — %1")
                                  .arg(errorMessageFromBody(root, body, httpStatus)));
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        emit validationFinished(
            profileId,
            false,
            QStringLiteral("Check failed — %1").arg(reply->errorString()));
        return;
    }

    emit validationFinished(profileId,
                          false,
                          QStringLiteral("Unexpected response — %1")
                              .arg(errorMessageFromBody(root, body, httpStatus)));
}

} // namespace VexaraEditor
