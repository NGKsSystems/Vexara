#include "VexaraEditor/PtyReaderThread.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace VexaraEditor {

PtyReaderThread::PtyReaderThread(QObject* parent)
    : QThread(parent)
{
}

PtyReaderThread::~PtyReaderThread()
{
    stopReading();
    wait(3000);
}

void PtyReaderThread::setOutputHandle(void* readHandle)
{
    readHandle_ = readHandle;
}

void PtyReaderThread::stopReading()
{
    stopRequested_ = true;
#ifdef _WIN32
    if (readHandle_ != nullptr) {
        CancelIoEx(static_cast<HANDLE>(readHandle_), nullptr);
    }
#endif
}

void PtyReaderThread::run()
{
#ifdef _WIN32
    const HANDLE handle = static_cast<HANDLE>(readHandle_);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        emit readThreadFinished();
        return;
    }

    while (!stopRequested_) {
        DWORD available = 0;
        if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr)) {
            break;
        }
        if (available == 0) {
            QThread::msleep(5);
            continue;
        }

        QByteArray buffer(static_cast<int>(available), Qt::Uninitialized);
        DWORD read = 0;
        if (!ReadFile(handle, buffer.data(), available, &read, nullptr) || read == 0) {
            break;
        }
        buffer.resize(static_cast<int>(read));
        emit bytesReceived(buffer);
    }
#endif
    emit readThreadFinished();
}

} // namespace VexaraEditor
