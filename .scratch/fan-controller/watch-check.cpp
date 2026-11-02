// Integration check: real WMI reads, process memory/CPU and console cancellation.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <cstdio>
#include <string>

int wmain(int argc, wchar_t** argv) {
    if (argc != 4 && argc != 5) return 2;
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE output = CreateFileW(argv[2], GENERIC_WRITE, FILE_SHARE_READ, &security,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) return 3;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = output;
    startup.hStdError = output;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    const std::wstring arguments = argc == 5 ? argv[4] : L"watch";
    std::wstring command = L"\"" + std::wstring(argv[1]) + L"\" " + arguments;
    const BOOL created = CreateProcessW(argv[1], command.data(), nullptr, nullptr,
        TRUE, CREATE_NEW_CONSOLE, nullptr, nullptr, &startup, &process);
    CloseHandle(output);
    if (!created) return 4;
    Sleep(6500);
    FILETIME createdAt{}, exitedAt{}, kernel{}, user{};
    const BOOL timesOk = GetProcessTimes(process.hProcess, &createdAt, &exitedAt, &kernel, &user);
    PROCESS_MEMORY_COUNTERS memory{};
    memory.cb = sizeof(memory);
    const BOOL memoryOk = GetProcessMemoryInfo(process.hProcess, &memory, sizeof(memory));
    FreeConsole();
    const BOOL attached = AttachConsole(process.dwProcessId);
    const BOOL ignored = attached && SetConsoleCtrlHandler(nullptr, TRUE);
    const BOOL signaled = ignored && GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
    const DWORD wait = WaitForSingleObject(process.hProcess, 45000);
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 99); // Only this harness's own child.
        WaitForSingleObject(process.hProcess, 5000);
    }
    DWORD exitCode = 100;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    ULARGE_INTEGER kernelTicks{}, userTicks{};
    kernelTicks.LowPart=kernel.dwLowDateTime; kernelTicks.HighPart=kernel.dwHighDateTime;
    userTicks.LowPart=user.dwLowDateTime; userTicks.HighPart=user.dwHighDateTime;
    FILE* report=nullptr;
    if (_wfopen_s(&report, argv[3], L"w") != 0) return 5;
    std::fprintf(report,
        "{\"signal\":\"Ctrl+C\",\"signalSent\":%s,\"gracefulExit\":%s,\"exitCode\":%lu,"
        "\"durationBeforeSignalMs\":6500,\"cpuTimeMs\":%.3f,\"workingSetBytes\":%zu,"
        "\"metricsValid\":%s}\n",
        signaled ? "true" : "false", wait == WAIT_OBJECT_0 ? "true" : "false", exitCode,
        static_cast<double>(kernelTicks.QuadPart + userTicks.QuadPart)/10000.0,
        memory.WorkingSetSize, timesOk && memoryOk ? "true" : "false");
    std::fclose(report);
    return signaled && wait == WAIT_OBJECT_0 && exitCode == 0 ? 0 : 1;
}
