#include <windows.h>
#include <setupapi.h>
#include <initguid.h>

#include <cerrno>
#include <cstdlib>
#include <cwchar>
#include <iostream>
#include <string>

#include "../../src/shared/VhidProtocol.h"

#pragma comment(lib, "Setupapi.lib")

static bool FindDevicePath(std::wstring* devicePath)
{
    HDEVINFO deviceInfo;
    SP_DEVICE_INTERFACE_DATA interfaceData;
    DWORD requiredSize;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W detailData;
    bool found;

    if (devicePath == nullptr) {
        return false;
    }

    deviceInfo = SetupDiGetClassDevsW(
        &GUID_DEVINTERFACE_WINDOWS_VHID_STACK,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfo == INVALID_HANDLE_VALUE) {
        return false;
    }

    interfaceData.cbSize = sizeof(interfaceData);
    found = false;

    if (SetupDiEnumDeviceInterfaces(
            deviceInfo,
            nullptr,
            &GUID_DEVINTERFACE_WINDOWS_VHID_STACK,
            0,
            &interfaceData)) {
        requiredSize = 0;
        if (SetupDiGetDeviceInterfaceDetailW(
                deviceInfo,
                &interfaceData,
                nullptr,
                0,
                &requiredSize,
                nullptr) ||
            GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
            requiredSize < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            SetupDiDestroyDeviceInfoList(deviceInfo);
            return false;
        }

        detailData = static_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(
            HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, requiredSize));
        if (detailData != nullptr) {
            detailData->cbSize = sizeof(*detailData);
            if (SetupDiGetDeviceInterfaceDetailW(
                    deviceInfo,
                    &interfaceData,
                    detailData,
                    requiredSize,
                    nullptr,
                    nullptr)) {
                *devicePath = detailData->DevicePath;
                found = true;
            }
            HeapFree(GetProcessHeap(), 0, detailData);
        }
    }

    SetupDiDestroyDeviceInfoList(deviceInfo);
    return found;
}

static const wchar_t* SequenceStateName(LONG state)
{
    switch (state) {
    case 0:
        return L"Disabled";
    case 1:
        return L"KeyboardPreClearPending";
    case 2:
        return L"KeyboardPreClearSubmitting";
    case 3:
        return L"MousePreClearPending";
    case 4:
        return L"MousePreClearSubmitting";
    case 5:
        return L"KeyboardAPressPending";
    case 6:
        return L"KeyboardAPressSubmitting";
    case 7:
        return L"KeyboardReleasePending";
    case 8:
        return L"KeyboardReleaseSubmitting";
    case 9:
        return L"MouseMoveRightPending";
    case 10:
        return L"MouseMoveRightSubmitting";
    case 11:
        return L"MousePostClearPending";
    case 12:
        return L"MousePostClearSubmitting";
    case 13:
        return L"KeyboardFinalClearPending";
    case 14:
        return L"KeyboardFinalClearSubmitting";
    case 15:
        return L"Complete";
    case 16:
        return L"Failed";
    case 17:
        return L"MoveAbsoluteSubmitting";
    case 18:
        return L"ClickAbsoluteMoveSubmitting";
    case 19:
        return L"ClickAbsoluteDownSubmitting";
    case 20:
        return L"ClickAbsoluteUpSubmitting";
    case 21:
        return L"KeyTapDownSubmitting";
    case 22:
        return L"KeyTapUpSubmitting";
    default:
        return L"Unknown";
    }
}

static const wchar_t* CommandTypeName(ULONG commandType)
{
    switch (commandType) {
    case VHID_COMMAND_NONE:
        return L"None";
    case VHID_COMMAND_SMOKE_SEQUENCE:
        return L"SmokeSequence";
    case VHID_COMMAND_MOVE_ABSOLUTE:
        return L"MoveAbsolute";
    case VHID_COMMAND_CLICK_ABSOLUTE:
        return L"ClickAbsolute";
    case VHID_COMMAND_KEY_TAP:
        return L"KeyTap";
    default:
        return L"Unknown";
    }
}

static void PrintFlag(const wchar_t* name, ULONG value)
{
    std::wcout << name << L"=" << (value != 0 ? L"true" : L"false") << L"\n";
}

static void PrintStatusValue(const wchar_t* name, LONG status)
{
    std::wcout << name << L"=0x" << std::hex << std::uppercase
               << static_cast<unsigned long>(status)
               << std::nouppercase << std::dec << L"\n";
}

static bool ParseCoordinate(const wchar_t* text, ULONG* value)
{
    wchar_t* end;
    unsigned long parsed;

    if (text == nullptr || value == nullptr || *text == L'\0' || *text == L'-') {
        return false;
    }

    errno = 0;
    end = nullptr;
    parsed = std::wcstoul(text, &end, 10);
    if (errno != 0 || end == text || *end != L'\0') {
        return false;
    }

    if (parsed > VHID_MOVE_ABSOLUTE_COORDINATE_MAX) {
        return false;
    }

    *value = static_cast<ULONG>(parsed);
    return true;
}
static std::wstring LowerAscii(std::wstring value)
{
    for (size_t index = 0; index < value.size(); index++) {
        if (value[index] >= L'A' && value[index] <= L'Z') {
            value[index] = static_cast<wchar_t>(value[index] - L'A' + L'a');
        }
    }

    return value;
}

static bool ParseKeyToken(const wchar_t* text, ULONG* keyCode)
{
    std::wstring token;
    std::wstring lower;
    wchar_t ch;

    if (text == nullptr || keyCode == nullptr || *text == L'\0') {
        return false;
    }

    token = text;
    if (token.size() == 1) {
        ch = token[0];
        if (ch >= 0x20 && ch <= 0x7E) {
            *keyCode = static_cast<ULONG>(ch);
            return true;
        }
    }

    lower = LowerAscii(token);
    if (lower == L"space") {
        *keyCode = static_cast<ULONG>(L' ');
        return true;
    }
    if (lower == L"enter" || lower == L"return") {
        *keyCode = VHID_KEY_TAP_KEY_ENTER;
        return true;
    }
    if (lower == L"esc" || lower == L"escape") {
        *keyCode = VHID_KEY_TAP_KEY_ESCAPE;
        return true;
    }
    if (lower == L"tab") {
        *keyCode = VHID_KEY_TAP_KEY_TAB;
        return true;
    }
    if (lower == L"backspace" || lower == L"bksp") {
        *keyCode = VHID_KEY_TAP_KEY_BACKSPACE;
        return true;
    }

    return false;
}

static int RunTrigger(HANDLE device)
{
    BOOL ok;
    DWORD bytesReturned;

    bytesReturned = 0;
    std::wcout << L"sending no-payload trigger\n";
    ok = DeviceIoControl(
        device,
        VHID_IOCTL_TRIGGER_SMOKE_SEQUENCE,
        nullptr,
        0,
        nullptr,
        0,
        &bytesReturned,
        nullptr);

    if (!ok) {
        DWORD error = GetLastError();
        std::wcerr << L"trigger failed: DeviceIoControl failed, error=" << error << L"\n";
        return 5;
    }

    std::wcout << L"trigger accepted: DeviceIoControl succeeded\n";
    return 0;
}

static int RunMoveAbsolute(HANDLE device, ULONG x, ULONG y)
{
    VHID_MOVE_ABSOLUTE_REQUEST request;
    BOOL ok;
    DWORD bytesReturned;

    ZeroMemory(&request, sizeof(request));
    request.Size = sizeof(request);
    request.ProtocolVersionMajor = VHID_PROTOCOL_VERSION_MAJOR;
    request.ProtocolVersionMinor = VHID_PROTOCOL_VERSION_MINOR;
    request.CommandType = VHID_COMMAND_MOVE_ABSOLUTE;
    request.SequenceId = 1;
    request.X = x;
    request.Y = y;

    bytesReturned = 0;
    std::wcout << L"sending move-abs x=" << x << L" y=" << y << L"\n";
    ok = DeviceIoControl(
        device,
        VHID_IOCTL_MOVE_ABSOLUTE,
        &request,
        sizeof(request),
        nullptr,
        0,
        &bytesReturned,
        nullptr);

    if (!ok) {
        DWORD error = GetLastError();
        std::wcerr << L"move-abs failed: DeviceIoControl failed, error=" << error << L"\n";
        return 8;
    }

    std::wcout << L"move-abs accepted: DeviceIoControl succeeded\n";
    return 0;
}


static int RunClickAbsolute(HANDLE device, ULONG x, ULONG y)
{
    VHID_CLICK_ABSOLUTE_REQUEST request;
    BOOL ok;
    DWORD bytesReturned;

    ZeroMemory(&request, sizeof(request));
    request.Size = sizeof(request);
    request.ProtocolVersionMajor = VHID_PROTOCOL_VERSION_MAJOR;
    request.ProtocolVersionMinor = VHID_PROTOCOL_VERSION_MINOR;
    request.CommandType = VHID_COMMAND_CLICK_ABSOLUTE;
    request.SequenceId = 2;
    request.X = x;
    request.Y = y;

    bytesReturned = 0;
    std::wcout << L"sending click-abs x=" << x << L" y=" << y << L"\n";
    ok = DeviceIoControl(
        device,
        VHID_IOCTL_CLICK_ABSOLUTE,
        &request,
        sizeof(request),
        nullptr,
        0,
        &bytesReturned,
        nullptr);

    if (!ok) {
        DWORD error = GetLastError();
        std::wcerr << L"click-abs failed: DeviceIoControl failed, error=" << error << L"\n";
        return 9;
    }

    std::wcout << L"click-abs accepted: DeviceIoControl succeeded\n";
    return 0;
}

static int RunKeyTap(HANDLE device, ULONG keyCode)
{
    VHID_KEY_TAP_REQUEST request;
    BOOL ok;
    DWORD bytesReturned;

    ZeroMemory(&request, sizeof(request));
    request.Size = sizeof(request);
    request.ProtocolVersionMajor = VHID_PROTOCOL_VERSION_MAJOR;
    request.ProtocolVersionMinor = VHID_PROTOCOL_VERSION_MINOR;
    request.CommandType = VHID_COMMAND_KEY_TAP;
    request.SequenceId = 3;
    request.KeyCode = keyCode;

    bytesReturned = 0;
    std::wcout << L"sending keytap keyCode=0x" << std::hex << std::uppercase
               << keyCode << std::nouppercase << std::dec << L"\n";
    ok = DeviceIoControl(
        device,
        VHID_IOCTL_KEY_TAP,
        &request,
        sizeof(request),
        nullptr,
        0,
        &bytesReturned,
        nullptr);

    if (!ok) {
        DWORD error = GetLastError();
        std::wcerr << L"keytap failed: DeviceIoControl failed, error=" << error << L"\n";
        return 10;
    }

    std::wcout << L"keytap accepted: DeviceIoControl succeeded\n";
    return 0;
}

static int RunStatus(HANDLE device)
{
    VHID_STATUS_REPORT report;
    BOOL ok;
    DWORD bytesReturned;

    ZeroMemory(&report, sizeof(report));
    bytesReturned = 0;

    std::wcout << L"sending read-only status query\n";
    ok = DeviceIoControl(
        device,
        VHID_IOCTL_QUERY_STATUS,
        nullptr,
        0,
        &report,
        sizeof(report),
        &bytesReturned,
        nullptr);

    if (!ok) {
        DWORD error = GetLastError();
        std::wcerr << L"status failed: DeviceIoControl failed, error=" << error << L"\n";
        return 6;
    }

    if (bytesReturned < static_cast<DWORD>(sizeof(report)) ||
        report.Size != static_cast<ULONG>(sizeof(report))) {
        std::wcerr << L"status failed: unexpected response size, bytes="
                   << bytesReturned << L" structSize=" << report.Size << L"\n";
        return 7;
    }

    std::wcout << L"status accepted: DeviceIoControl succeeded\n";
    std::wcout << L"protocol=" << report.ProtocolVersionMajor << L"."
               << report.ProtocolVersionMinor << L"\n";
    PrintFlag(L"vhfHandlePresent", report.VhfHandlePresent);
    PrintFlag(L"initialized", report.Initialized);
    PrintFlag(L"vhfCreated", report.VhfCreated);
    PrintFlag(L"vhfStarted", report.VhfStarted);
    PrintFlag(L"deleting", report.Deleting);
    PrintFlag(L"reportSubmissionEnabled", report.ReportSubmissionEnabled);
    PrintFlag(L"readyForNextReport", report.ReadyForNextReport);
    std::wcout << L"reportSequenceState=" << report.ReportSequenceState
               << L" (" << SequenceStateName(report.ReportSequenceState) << L")\n";
    PrintStatusValue(L"lastReportSubmitStatus", report.LastReportSubmitStatus);
    PrintStatusValue(L"lastTriggerStatus", report.LastTriggerStatus);
    PrintFlag(L"triggerWouldBeAccepted", report.TriggerWouldBeAccepted);
    PrintStatusValue(L"triggerRejectStatus", report.TriggerRejectStatus);
    PrintFlag(L"smokeSequenceCompleted", report.SmokeSequenceCompleted);
    std::wcout << L"currentCommandType=" << report.CurrentCommandType
               << L" (" << CommandTypeName(report.CurrentCommandType) << L")\n";
    std::wcout << L"currentCommandSequenceId=" << report.CurrentCommandSequenceId << L"\n";
    std::wcout << L"lastCommandType=" << report.LastCommandType
               << L" (" << CommandTypeName(report.LastCommandType) << L")\n";
    std::wcout << L"lastCommandSequenceId=" << report.LastCommandSequenceId << L"\n";
    PrintStatusValue(L"lastCommandStatus", report.LastCommandStatus);
    std::wcout << L"supportedCommandMask=0x" << std::hex << std::uppercase
               << report.SupportedCommandMask << std::nouppercase << std::dec << L"\n";
    std::wcout << L"currentReceiptId=" << report.CurrentReceiptId << L"\n";
    std::wcout << L"lastReceiptId=" << report.LastReceiptId << L"\n";
    PrintStatusValue(L"lastCommandAcceptStatus", report.LastCommandAcceptStatus);
    PrintStatusValue(L"lastCommandReleaseStatus", report.LastCommandReleaseStatus);
    PrintStatusValue(L"lastCommandReleaseRetryStatus", report.LastCommandReleaseRetryStatus);
    PrintFlag(L"lastCommandReleaseRetryAttempted", report.LastCommandReleaseRetryAttempted);
    PrintFlag(L"lastCommandReleaseRetrySucceeded", report.LastCommandReleaseRetrySucceeded);
    PrintFlag(L"lastCommandFinalNeutralKnown", report.LastCommandFinalNeutralKnown);
    std::wcout << L"lastRejectedCommandType=" << report.LastRejectedCommandType
               << L" (" << CommandTypeName(report.LastRejectedCommandType) << L")\n";
    std::wcout << L"lastRejectedCommandSequenceId=" << report.LastRejectedCommandSequenceId << L"\n";
    PrintStatusValue(L"lastRejectedCommandStatus", report.LastRejectedCommandStatus);
    return 0;
}

static void PrintUsage()
{
    std::wcerr << L"usage: proof-client.exe trigger|status|move-abs <x> <y>|click-abs <x> <y>|keytap <key>\n";
}

int wmain(int argc, wchar_t** argv)
{
    std::wstring command;
    std::wstring devicePath;
    HANDLE device;
    DWORD desiredAccess;
    ULONG x;
    ULONG y;
    ULONG keyCode;
    int result;

    if (argc < 2) {
        PrintUsage();
        return 2;
    }

    command = argv[1];
    x = 0;
    y = 0;
    keyCode = 0;

    if (command == L"trigger" || command == L"status") {
        if (argc != 2) {
            PrintUsage();
            return 2;
        }
    } else if (command == L"move-abs" || command == L"click-abs") {
        if (argc != 4 || !ParseCoordinate(argv[2], &x) || !ParseCoordinate(argv[3], &y)) {
            PrintUsage();
            std::wcerr << command << L" coordinates must be decimal values from 0 to "
                       << VHID_MOVE_ABSOLUTE_COORDINATE_MAX << L"\n";
            return 2;
        }
    } else if (command == L"keytap") {
        if (argc != 3 || !ParseKeyToken(argv[2], &keyCode)) {
            PrintUsage();
            std::wcerr << L"keytap supports one printable US ASCII key or one of: space, enter, esc, escape, tab, backspace, bksp\n";
            return 2;
        }
    } else {
        PrintUsage();
        return 2;
    }

    if (!FindDevicePath(&devicePath)) {
        std::wcerr << L"windows-vhid-stack device interface not found\n";
        return 3;
    }

    std::wcout << L"found device interface: " << devicePath << L"\n";

    desiredAccess = (command == L"status") ? GENERIC_READ : GENERIC_WRITE;
    device = CreateFileW(
        devicePath.c_str(),
        desiredAccess,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (device == INVALID_HANDLE_VALUE) {
        std::wcerr << L"CreateFile failed, error=" << GetLastError() << L"\n";
        return 4;
    }

    if (command == L"status") {
        result = RunStatus(device);
    } else if (command == L"move-abs") {
        result = RunMoveAbsolute(device, x, y);
    } else if (command == L"click-abs") {
        result = RunClickAbsolute(device, x, y);
    } else if (command == L"keytap") {
        result = RunKeyTap(device, keyCode);
    } else {
        result = RunTrigger(device);
    }

    CloseHandle(device);
    return result;
}
