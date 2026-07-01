#pragma once

#include <ntddk.h>

#define VHID_LOG_INFO(_fmt_, ...) \
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "windows-vhid-stack: " _fmt_ "\n", __VA_ARGS__)

#define VHID_LOG_ERROR(_fmt_, ...) \
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "windows-vhid-stack: " _fmt_ "\n", __VA_ARGS__)
