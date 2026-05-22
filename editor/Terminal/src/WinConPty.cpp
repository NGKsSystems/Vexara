#include "VexaraEditor/WinConPty.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif
#endif

#include <QByteArray>
#include <vector>

namespace VexaraEditor {

WinConPty::WinConPty() = default;

WinConPty::~WinConPty()
{
    terminate();
}

bool WinConPty::isAvailable()
{
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

bool WinConPty::start(const QString& program,
                      const QStringList& args,
                      const QString& workingDirectory,
                      int columns,
                      int rows)
{
#ifdef _WIN32
    terminate();

    columns_ = columns > 0 ? columns : 120;
    rows_ = rows > 0 ? rows : 30;

    HANDLE inputRead = nullptr;
    HANDLE inputWriteHost = nullptr;
    HANDLE outputWrite = nullptr;
    HANDLE outputReadHost = nullptr;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&inputRead, &inputWriteHost, &sa, 0)) {
        return false;
    }
    if (!SetHandleInformation(inputWriteHost, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(inputRead);
        CloseHandle(inputWriteHost);
        return false;
    }

    if (!CreatePipe(&outputReadHost, &outputWrite, &sa, 0)) {
        CloseHandle(inputRead);
        CloseHandle(inputWriteHost);
        return false;
    }
    if (!SetHandleInformation(outputReadHost, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(inputRead);
        CloseHandle(inputWriteHost);
        CloseHandle(outputWrite);
        CloseHandle(outputReadHost);
        return false;
    }

    HPCON hpc = nullptr;
    const COORD size{static_cast<SHORT>(columns_), static_cast<SHORT>(rows_)};
    const HRESULT createResult = CreatePseudoConsole(size, inputRead, outputWrite, 0, &hpc);
    CloseHandle(inputRead);
    CloseHandle(outputWrite);
    if (FAILED(createResult) || hpc == nullptr) {
        CloseHandle(inputWriteHost);
        CloseHandle(outputReadHost);
        return false;
    }

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    SIZE_T attributeListSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeListSize);
    si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(new BYTE[attributeListSize]);
    if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attributeListSize)) {
        ClosePseudoConsole(hpc);
        CloseHandle(inputWriteHost);
        CloseHandle(outputReadHost);
        delete[] reinterpret_cast<BYTE*>(si.lpAttributeList);
        return false;
    }
    UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hpc,
                              sizeof(HPCON), nullptr, nullptr);

    PROCESS_INFORMATION pi{};
    std::wstring cmdLine = program.toStdWString();
    for (const QString& arg : args) {
        cmdLine += L' ';
        cmdLine += arg.toStdWString();
    }
    std::vector<wchar_t> cmdBuffer(cmdLine.begin(), cmdLine.end());
    cmdBuffer.push_back(L'\0');

    const std::wstring cwd = workingDirectory.toStdWString();
    const BOOL created = CreateProcessW(
        nullptr,
        cmdBuffer.data(),
        nullptr,
        nullptr,
        FALSE,
        EXTENDED_STARTUPINFO_PRESENT,
        nullptr,
        cwd.empty() ? nullptr : cwd.c_str(),
        reinterpret_cast<STARTUPINFOW*>(&si),
        &pi);

    DeleteProcThreadAttributeList(si.lpAttributeList);
    delete[] reinterpret_cast<BYTE*>(si.lpAttributeList);

    if (!created) {
        ClosePseudoConsole(hpc);
        CloseHandle(inputWriteHost);
        CloseHandle(outputReadHost);
        return false;
    }

    CloseHandle(pi.hThread);

    pseudoConsole_ = hpc;
    inputWrite_ = inputWriteHost;
    outputRead_ = outputReadHost;
    processHandle_ = pi.hProcess;
    return true;
#else
    Q_UNUSED(program);
    Q_UNUSED(args);
    Q_UNUSED(workingDirectory);
    Q_UNUSED(columns);
    Q_UNUSED(rows);
    return false;
#endif
}

bool WinConPty::isRunning() const
{
#ifdef _WIN32
    if (processHandle_ == nullptr) {
        return false;
    }
    DWORD exitCode = STILL_ACTIVE;
    if (GetExitCodeProcess(static_cast<HANDLE>(processHandle_), &exitCode)) {
        return exitCode == STILL_ACTIVE;
    }
    return false;
#else
    return false;
#endif
}

void WinConPty::writeInput(const QByteArray& data)
{
#ifdef _WIN32
    if (!inputWrite_ || data.isEmpty()) {
        return;
    }
    DWORD written = 0;
    WriteFile(static_cast<HANDLE>(inputWrite_), data.constData(),
              static_cast<DWORD>(data.size()), &written, nullptr);
#else
    Q_UNUSED(data);
#endif
}

QByteArray WinConPty::readOutput()
{
#ifdef _WIN32
    if (!outputRead_) {
        return {};
    }
    DWORD available = 0;
    const HANDLE handle = static_cast<HANDLE>(outputRead_);
    if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr) || available == 0) {
        return {};
    }
    QByteArray buffer(static_cast<int>(available), Qt::Uninitialized);
    DWORD read = 0;
    if (!ReadFile(handle, buffer.data(), available, &read, nullptr) || read == 0) {
        return {};
    }
    buffer.resize(static_cast<int>(read));
    return buffer;
#else
    return {};
#endif
}

void WinConPty::resize(int columns, int rows)
{
#ifdef _WIN32
    columns_ = columns > 0 ? columns : columns_;
    rows_ = rows > 0 ? rows : rows_;
    if (pseudoConsole_ != nullptr) {
        const COORD size{static_cast<SHORT>(columns_), static_cast<SHORT>(rows_)};
        ResizePseudoConsole(static_cast<HPCON>(pseudoConsole_), size);
    }
#else
    Q_UNUSED(columns);
    Q_UNUSED(rows);
#endif
}

void* WinConPty::outputReadHandle() const
{
    return outputRead_;
}

int WinConPty::columns() const
{
    return columns_;
}

int WinConPty::rows() const
{
    return rows_;
}

void WinConPty::terminate()
{
#ifdef _WIN32
    if (processHandle_ != nullptr) {
        TerminateProcess(static_cast<HANDLE>(processHandle_), 1);
    }
    closeHandles();
#else
    closeHandles();
#endif
}

void WinConPty::closeHandles()
{
#ifdef _WIN32
    if (pseudoConsole_ != nullptr) {
        ClosePseudoConsole(static_cast<HPCON>(pseudoConsole_));
        pseudoConsole_ = nullptr;
    }
    if (inputWrite_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(inputWrite_));
        inputWrite_ = nullptr;
    }
    if (outputRead_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(outputRead_));
        outputRead_ = nullptr;
    }
    if (processHandle_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(processHandle_));
        processHandle_ = nullptr;
    }
#endif
}

} // namespace VexaraEditor
