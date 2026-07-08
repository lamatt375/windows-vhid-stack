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

int wmain(int argc, wchar_t** argv)
{
    std::wstring devicePath;
    HANDLE device;
    BOOL ok;
    DWORD bytesReturned;

    if (argc != 2 || std::wstring(argv[1]) != L"trigger") {
        std::wcerr << L"usage: proof-client.exe trigger\n";
        return 2;
    }

    if (!FindDevicePath(&devicePath)) {
        std::wcerr << L"windows-vhid-stack device interface not found\n";
        return 3;
    }

    std::wcout << L"found device interface: " << devicePath << L"\n";

    device = CreateFileW(
        devicePath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (device == INVALID_HANDLE_VALUE) {
        std::wcerr << L"CreateFile failed, error=" << GetLastError() << L"\n";
        return 4;
    }

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
        CloseHandle(device);
        std::wcerr << L"trigger failed: DeviceIoControl failed, error=" << error << L"\n";
        return 5;
    }

    CloseHandle(device);
    std::wcout << L"trigger accepted: DeviceIoControl succeeded\n";
    return 0;
}
