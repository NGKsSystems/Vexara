#pragma once

#include <QString>
#include <QStringList>

namespace VexaraEditor {

class WinConPty {
public:
    WinConPty();
    ~WinConPty();

    static bool isAvailable();

    bool start(const QString& program,
               const QStringList& args,
               const QString& workingDirectory,
               int columns,
               int rows);
    bool isRunning() const;
    void writeInput(const QByteArray& data);
    QByteArray readOutput();
    void resize(int columns, int rows);
    void terminate();

    void* outputReadHandle() const;
    int columns() const;
    int rows() const;

private:
    void closeHandles();

    void* pseudoConsole_ = nullptr;
    void* inputWrite_ = nullptr;
    void* outputRead_ = nullptr;
    void* processHandle_ = nullptr;
    int columns_ = 120;
    int rows_ = 30;
};

} // namespace VexaraEditor
