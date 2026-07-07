param(
    [string]$Filter = "hid_device_system_vhf",
    [switch]$AllHid
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$source = @"
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;

public static class HidCapsInspector
{
    private const uint DIGCF_PRESENT = 0x00000002;
    private const uint DIGCF_DEVICEINTERFACE = 0x00000010;
    private const uint FILE_SHARE_READ = 0x00000001;
    private const uint FILE_SHARE_WRITE = 0x00000002;
    private const uint OPEN_EXISTING = 3;
    private static readonly IntPtr INVALID_HANDLE_VALUE = new IntPtr(-1);

    [StructLayout(LayoutKind.Sequential)]
    private struct SP_DEVICE_INTERFACE_DATA
    {
        public int cbSize;
        public Guid InterfaceClassGuid;
        public uint Flags;
        public IntPtr Reserved;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Auto)]
    private struct SP_DEVICE_INTERFACE_DETAIL_DATA
    {
        public int cbSize;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 1024)]
        public string DevicePath;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct HIDD_ATTRIBUTES
    {
        public int Size;
        public ushort VendorID;
        public ushort ProductID;
        public ushort VersionNumber;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct HIDP_CAPS
    {
        public ushort Usage;
        public ushort UsagePage;
        public ushort InputReportByteLength;
        public ushort OutputReportByteLength;
        public ushort FeatureReportByteLength;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 17)]
        public ushort[] Reserved;
        public ushort NumberLinkCollectionNodes;
        public ushort NumberInputButtonCaps;
        public ushort NumberInputValueCaps;
        public ushort NumberInputDataIndices;
        public ushort NumberOutputButtonCaps;
        public ushort NumberOutputValueCaps;
        public ushort NumberOutputDataIndices;
        public ushort NumberFeatureButtonCaps;
        public ushort NumberFeatureValueCaps;
        public ushort NumberFeatureDataIndices;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct HIDP_BUTTON_CAPS
    {
        public ushort UsagePage;
        public byte ReportID;
        public byte IsAlias;
        public ushort BitField;
        public ushort LinkCollection;
        public ushort LinkUsage;
        public ushort LinkUsagePage;
        public byte IsRange;
        public byte IsStringRange;
        public byte IsDesignatorRange;
        public byte IsAbsolute;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 10)]
        public uint[] Reserved;
        public ushort UsageMin;
        public ushort UsageMax;
        public ushort StringMin;
        public ushort StringMax;
        public ushort DesignatorMin;
        public ushort DesignatorMax;
        public ushort DataIndexMin;
        public ushort DataIndexMax;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct HIDP_VALUE_CAPS
    {
        public ushort UsagePage;
        public byte ReportID;
        public byte IsAlias;
        public ushort BitField;
        public ushort LinkCollection;
        public ushort LinkUsage;
        public ushort LinkUsagePage;
        public byte IsRange;
        public byte IsStringRange;
        public byte IsDesignatorRange;
        public byte IsAbsolute;
        public byte HasNull;
        public byte ReservedByte;
        public ushort BitSize;
        public ushort ReportCount;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 5)]
        public ushort[] Reserved;
        public uint UnitsExp;
        public uint Units;
        public int LogicalMin;
        public int LogicalMax;
        public int PhysicalMin;
        public int PhysicalMax;
        public ushort UsageMin;
        public ushort UsageMax;
        public ushort StringMin;
        public ushort StringMax;
        public ushort DesignatorMin;
        public ushort DesignatorMax;
        public ushort DataIndexMin;
        public ushort DataIndexMax;
    }

    public sealed class DeviceCaps
    {
        public string Path;
        public ushort VendorId;
        public ushort ProductId;
        public ushort VersionNumber;
        public ushort UsagePage;
        public ushort Usage;
        public ushort InputReportByteLength;
        public ushort OutputReportByteLength;
        public ushort FeatureReportByteLength;
        public ushort InputButtonCaps;
        public ushort InputValueCaps;
        public string[] ButtonCaps;
        public string[] ValueCaps;
    }

    [DllImport("hid.dll")]
    private static extern void HidD_GetHidGuid(out Guid hidGuid);

    [DllImport("hid.dll", SetLastError = true)]
    private static extern bool HidD_GetAttributes(IntPtr hidDeviceObject, ref HIDD_ATTRIBUTES attributes);

    [DllImport("hid.dll", SetLastError = true)]
    private static extern bool HidD_GetPreparsedData(IntPtr hidDeviceObject, out IntPtr preparsedData);

    [DllImport("hid.dll", SetLastError = true)]
    private static extern bool HidD_FreePreparsedData(IntPtr preparsedData);

    [DllImport("hid.dll")]
    private static extern int HidP_GetCaps(IntPtr preparsedData, ref HIDP_CAPS capabilities);

    [DllImport("hid.dll")]
    private static extern int HidP_GetButtonCaps(ushort reportType, [Out] HIDP_BUTTON_CAPS[] buttonCaps, ref ushort buttonCapsLength, IntPtr preparsedData);

    [DllImport("hid.dll")]
    private static extern int HidP_GetValueCaps(ushort reportType, [Out] HIDP_VALUE_CAPS[] valueCaps, ref ushort valueCapsLength, IntPtr preparsedData);

    [DllImport("setupapi.dll", SetLastError = true)]
    private static extern IntPtr SetupDiGetClassDevs(ref Guid classGuid, IntPtr enumerator, IntPtr hwndParent, uint flags);

    [DllImport("setupapi.dll", SetLastError = true)]
    private static extern bool SetupDiEnumDeviceInterfaces(IntPtr deviceInfoSet, IntPtr deviceInfoData, ref Guid interfaceClassGuid, uint memberIndex, ref SP_DEVICE_INTERFACE_DATA deviceInterfaceData);

    [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Auto)]
    private static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr deviceInfoSet, ref SP_DEVICE_INTERFACE_DATA deviceInterfaceData, IntPtr deviceInterfaceDetailData, uint deviceInterfaceDetailDataSize, out uint requiredSize, IntPtr deviceInfoData);

    [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Auto)]
    private static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr deviceInfoSet, ref SP_DEVICE_INTERFACE_DATA deviceInterfaceData, ref SP_DEVICE_INTERFACE_DETAIL_DATA deviceInterfaceDetailData, uint deviceInterfaceDetailDataSize, out uint requiredSize, IntPtr deviceInfoData);

    [DllImport("setupapi.dll", SetLastError = true)]
    private static extern bool SetupDiDestroyDeviceInfoList(IntPtr deviceInfoSet);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Auto)]
    private static extern IntPtr CreateFile(string fileName, uint desiredAccess, uint shareMode, IntPtr securityAttributes, uint creationDisposition, uint flagsAndAttributes, IntPtr templateFile);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr handle);

    public static DeviceCaps[] Inspect(string filter, bool allHid)
    {
        Guid hidGuid;
        HidD_GetHidGuid(out hidGuid);

        IntPtr set = SetupDiGetClassDevs(ref hidGuid, IntPtr.Zero, IntPtr.Zero, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (set == INVALID_HANDLE_VALUE)
        {
            throw new Win32Exception(Marshal.GetLastWin32Error(), "SetupDiGetClassDevs failed");
        }

        var results = new List<DeviceCaps>();

        try
        {
            for (uint index = 0; ; index++)
            {
                var interfaceData = new SP_DEVICE_INTERFACE_DATA();
                interfaceData.cbSize = Marshal.SizeOf(typeof(SP_DEVICE_INTERFACE_DATA));

                if (!SetupDiEnumDeviceInterfaces(set, IntPtr.Zero, ref hidGuid, index, ref interfaceData))
                {
                    int error = Marshal.GetLastWin32Error();
                    if (error == 259)
                    {
                        break;
                    }

                    throw new Win32Exception(error, "SetupDiEnumDeviceInterfaces failed");
                }

                uint requiredSize;
                SetupDiGetDeviceInterfaceDetail(set, ref interfaceData, IntPtr.Zero, 0, out requiredSize, IntPtr.Zero);

                var detailData = new SP_DEVICE_INTERFACE_DETAIL_DATA();
                detailData.cbSize = IntPtr.Size == 8 ? 8 : 6;

                if (!SetupDiGetDeviceInterfaceDetail(set, ref interfaceData, ref detailData, requiredSize, out requiredSize, IntPtr.Zero))
                {
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "SetupDiGetDeviceInterfaceDetail failed");
                }

                string path = detailData.DevicePath;
                if (!allHid && !path.ToLowerInvariant().Contains(filter.ToLowerInvariant()))
                {
                    continue;
                }

                results.Add(ReadCaps(path));
            }
        }
        finally
        {
            SetupDiDestroyDeviceInfoList(set);
        }

        return results.ToArray();
    }

    private static DeviceCaps ReadCaps(string path)
    {
        IntPtr handle = CreateFile(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, IntPtr.Zero, OPEN_EXISTING, 0, IntPtr.Zero);
        if (handle == INVALID_HANDLE_VALUE)
        {
            throw new Win32Exception(Marshal.GetLastWin32Error(), "CreateFile read-only open failed for " + path);
        }

        IntPtr preparsedData = IntPtr.Zero;
        try
        {
            var attributes = new HIDD_ATTRIBUTES();
            attributes.Size = Marshal.SizeOf(typeof(HIDD_ATTRIBUTES));
            if (!HidD_GetAttributes(handle, ref attributes))
            {
                throw new Win32Exception(Marshal.GetLastWin32Error(), "HidD_GetAttributes failed");
            }

            if (!HidD_GetPreparsedData(handle, out preparsedData))
            {
                throw new Win32Exception(Marshal.GetLastWin32Error(), "HidD_GetPreparsedData failed");
            }

            var caps = new HIDP_CAPS();
            int status = HidP_GetCaps(preparsedData, ref caps);
            if (status != 0x00110000)
            {
                throw new InvalidOperationException("HidP_GetCaps failed with NTSTATUS 0x" + status.ToString("X8"));
            }

            ushort buttonCount = caps.NumberInputButtonCaps;
            var buttonCaps = new HIDP_BUTTON_CAPS[buttonCount];
            if (buttonCount > 0)
            {
                status = HidP_GetButtonCaps(0, buttonCaps, ref buttonCount, preparsedData);
                if (status != 0x00110000)
                {
                    throw new InvalidOperationException("HidP_GetButtonCaps failed with NTSTATUS 0x" + status.ToString("X8"));
                }
            }

            ushort valueCount = caps.NumberInputValueCaps;
            var valueCaps = new HIDP_VALUE_CAPS[valueCount];
            if (valueCount > 0)
            {
                status = HidP_GetValueCaps(0, valueCaps, ref valueCount, preparsedData);
                if (status != 0x00110000)
                {
                    throw new InvalidOperationException("HidP_GetValueCaps failed with NTSTATUS 0x" + status.ToString("X8"));
                }
            }

            return new DeviceCaps
            {
                Path = path,
                VendorId = attributes.VendorID,
                ProductId = attributes.ProductID,
                VersionNumber = attributes.VersionNumber,
                UsagePage = caps.UsagePage,
                Usage = caps.Usage,
                InputReportByteLength = caps.InputReportByteLength,
                OutputReportByteLength = caps.OutputReportByteLength,
                FeatureReportByteLength = caps.FeatureReportByteLength,
                InputButtonCaps = caps.NumberInputButtonCaps,
                InputValueCaps = caps.NumberInputValueCaps,
                ButtonCaps = FormatButtonCaps(buttonCaps, buttonCount),
                ValueCaps = FormatValueCaps(valueCaps, valueCount)
            };
        }
        finally
        {
            if (preparsedData != IntPtr.Zero)
            {
                HidD_FreePreparsedData(preparsedData);
            }

            CloseHandle(handle);
        }
    }

    private static string[] FormatButtonCaps(HIDP_BUTTON_CAPS[] caps, ushort count)
    {
        var values = new List<string>();
        for (int i = 0; i < count; i++)
        {
            values.Add(String.Format("ReportID={0} UsagePage=0x{1:X4} UsageMin=0x{2:X4} UsageMax=0x{3:X4} LinkUsagePage=0x{4:X4} LinkUsage=0x{5:X4}",
                caps[i].ReportID, caps[i].UsagePage, caps[i].UsageMin, caps[i].UsageMax, caps[i].LinkUsagePage, caps[i].LinkUsage));
        }
        return values.ToArray();
    }

    private static string[] FormatValueCaps(HIDP_VALUE_CAPS[] caps, ushort count)
    {
        var values = new List<string>();
        for (int i = 0; i < count; i++)
        {
            values.Add(String.Format("ReportID={0} UsagePage=0x{1:X4} UsageMin=0x{2:X4} UsageMax=0x{3:X4} LogicalMin={4} LogicalMax={5} BitSize={6} ReportCount={7} IsAbsolute={8} LinkUsagePage=0x{9:X4} LinkUsage=0x{10:X4}",
                caps[i].ReportID, caps[i].UsagePage, caps[i].UsageMin, caps[i].UsageMax, caps[i].LogicalMin, caps[i].LogicalMax, caps[i].BitSize, caps[i].ReportCount, caps[i].IsAbsolute, caps[i].LinkUsagePage, caps[i].LinkUsage));
        }
        return values.ToArray();
    }
}
"@

Add-Type -TypeDefinition $source

$devices = [HidCapsInspector]::Inspect($Filter, [bool]$AllHid)

if ($devices.Count -eq 0) {
    Write-Host "No matching HID device interfaces found."
    Write-Host "Filter: $Filter"
    Write-Host "Use -AllHid to list every present HID interface."
    exit 1
}

foreach ($device in $devices) {
    Write-Host ""
    Write-Host "DevicePath: $($device.Path)"
    Write-Host ("VID/PID/Version: 0x{0:X4} / 0x{1:X4} / 0x{2:X4}" -f $device.VendorId, $device.ProductId, $device.VersionNumber)
    Write-Host ("Top-level Usage: Page=0x{0:X4} Usage=0x{1:X4}" -f $device.UsagePage, $device.Usage)
    Write-Host "InputReportByteLength: $($device.InputReportByteLength)"
    Write-Host "OutputReportByteLength: $($device.OutputReportByteLength)"
    Write-Host "FeatureReportByteLength: $($device.FeatureReportByteLength)"
    Write-Host "InputButtonCaps: $($device.InputButtonCaps)"
    foreach ($cap in $device.ButtonCaps) {
        Write-Host "  ButtonCap: $cap"
    }
    Write-Host "InputValueCaps: $($device.InputValueCaps)"
    foreach ($cap in $device.ValueCaps) {
        Write-Host "  ValueCap: $cap"
    }
}
