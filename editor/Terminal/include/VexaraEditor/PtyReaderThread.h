#pragma once

#include <QByteArray>
#include <QThread>

namespace VexaraEditor {

class PtyReaderThread : public QThread {
    Q_OBJECT

public:
    explicit PtyReaderThread(QObject* parent = nullptr);
    ~PtyReaderThread() override;

    void setOutputHandle(void* readHandle);
    void stopReading();

signals:
    void bytesReceived(const QByteArray& chunk);
    void readThreadFinished();

protected:
    void run() override;

private:
    void* readHandle_ = nullptr;
    bool stopRequested_ = false;
};

} // namespace VexaraEditor
