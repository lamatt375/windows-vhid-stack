#include <windows.h>
#include <setupapi.h>
#include <initguid.h>

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
    return 0;
}

int wmain(int argc, wchar_t** argv)
{
    std::wstring command;
    std::wstring devicePath;
    HANDLE device;
    DWORD desiredAccess;
    int result;

    if (argc != 2) {
        std::wcerr << L"usage: proof-client.exe trigger|status\n";
        return 2;
    }

    command = argv[1];
    if (command != L"trigger" && command != L"status") {
        std::wcerr << L"usage: proof-client.exe trigger|status\n";
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
    } else {
        result = RunTrigger(device);
    }

    CloseHandle(device);
    return result;
}
