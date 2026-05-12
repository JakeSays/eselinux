// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// GetStdHandle / GetConsoleScreenBufferInfo. Engine uses these only to
// size diagnostic output to terminal width; we route through TIOCGWINSZ
// and synthesize a CONSOLE_SCREEN_BUFFER_INFO with dwMaximumWindowSize.X
// set to the column count. Other fields are zeroed.
//
// HANDLE values returned for STD_OUTPUT_HANDLE / STD_ERROR_HANDLE are
// the small-integer fd values cast to HANDLE — they are NOT KObjects,
// so callers must not pass them to ReadFile / CloseHandle. Engine only
// passes them back to GetConsoleScreenBufferInfo, which we recognize.

#include "osstd.hxx"

#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace
{
inline HANDLE FdToHandle(int fd)
{
    // Tag the fd with high bits so it can't collide with KObject pointers
    // (which are heap-allocated, so always > 0xFFFF).
    return reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd) | 0x10000);
}

inline int HandleToFd(HANDLE h)
{
    const auto v = reinterpret_cast<intptr_t>(h);
    if ((v & ~0xFF) == 0x10000)
        return static_cast<int>(v & 0xFF);
    return -1;
}
}

extern "C"
{
HANDLE GetStdHandle(DWORD nStdHandle)
{
    switch (nStdHandle)
    {
        case STD_INPUT_HANDLE:
            return FdToHandle(STDIN_FILENO);
        case STD_OUTPUT_HANDLE:
            return FdToHandle(STDOUT_FILENO);
        case STD_ERROR_HANDLE:
            return FdToHandle(STDERR_FILENO);
        default:
            return INVALID_HANDLE_VALUE;
    }
}

BOOL GetConsoleScreenBufferInfo(HANDLE hConsoleOutput, PCONSOLE_SCREEN_BUFFER_INFO lpInfo)
{
    if (!lpInfo)
        return FALSE;
    const int fd = HandleToFd(hConsoleOutput);
    if (fd < 0)
        return FALSE;
    struct winsize ws;
    if (ioctl(fd, TIOCGWINSZ, &ws) < 0 || ws.ws_col == 0)
    {
        return FALSE;
    }
    lpInfo->dwSize.X = static_cast<SHORT>(ws.ws_col);
    lpInfo->dwSize.Y = static_cast<SHORT>(ws.ws_row);
    lpInfo->dwCursorPosition.X = 0;
    lpInfo->dwCursorPosition.Y = 0;
    lpInfo->wAttributes = 0;
    lpInfo->srWindow.Left = 0;
    lpInfo->srWindow.Top = 0;
    lpInfo->srWindow.Right = static_cast<SHORT>(ws.ws_col - 1);
    lpInfo->srWindow.Bottom = static_cast<SHORT>(ws.ws_row - 1);
    lpInfo->dwMaximumWindowSize.X = static_cast<SHORT>(ws.ws_col);
    lpInfo->dwMaximumWindowSize.Y = static_cast<SHORT>(ws.ws_row);
    return TRUE;
}
} // extern "C"
