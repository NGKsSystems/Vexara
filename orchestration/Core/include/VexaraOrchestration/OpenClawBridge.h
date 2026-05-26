#pragma once



#include "VexaraCore/OpenClawSettings.h"



#include <QObject>

#include <QString>



class QProcess;



namespace VexaraOrchestration {



class OpenClawBridge : public QObject {

    Q_OBJECT



public:

    explicit OpenClawBridge(QObject* parent = nullptr);



    void configure(const VexaraCore::OpenClawSettings& settings);

    bool isConfigured() const;

    bool isRunning() const;



    void runTask(const QString& prompt, const QString& workingDirectory);
    void cancelActiveTask();



signals:

    void taskStarted(const QString& prompt);

    void outputChunk(const QString& text);

    void taskFinished(bool success, const QString& summary);



private:

    void finishProcess(bool success, const QString& summary);



    VexaraCore::OpenClawSettings settings_;
    QProcess* process_ = nullptr;
    QString capturedOutput_;
    bool localOllamaConfigured_ = false;
    bool taskFinishedEmitted_ = false;
};



} // namespace VexaraOrchestration


