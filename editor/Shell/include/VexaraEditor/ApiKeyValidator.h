#pragma once

#include "VexaraCore/ModelProfile.h"

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace VexaraEditor {

class ApiKeyValidator : public QObject {
    Q_OBJECT

public:
    explicit ApiKeyValidator(QObject* parent = nullptr);
    ~ApiKeyValidator() override;

    bool isBusy() const;
    void validateProfile(const VexaraCore::ModelProfile& profile, const QString& apiKeyOverride = QString());
    void cancelAll();

signals:
    void validationStarted(const QString& profileId);
    void validationFinished(const QString& profileId, bool success, const QString& message);

private:
    void finishReply(QNetworkReply* reply, const QString& profileId);

    QNetworkAccessManager* network_ = nullptr;
    QNetworkReply* activeReply_ = nullptr;
};

bool canValidateProvider(VexaraCore::ModelProvider provider);
QString resolveKeyForValidation(const VexaraCore::ModelProfile& profile, const QString& apiKeyOverride);

} // namespace VexaraEditor
