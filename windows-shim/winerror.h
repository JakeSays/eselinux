// Linux shim for <winerror.h>. Win32 error codes (winerror.h on Windows).
// The engine consumes these via SetLastError/GetLastError; the posix
// layer translates errno → ERROR_* at API boundaries (see
// os/posix/error_posix.cxx).
//
// Values come straight from the Windows SDK to preserve the engine's
// switch( error ) → JET_err* mapping byte-for-byte.
#pragma once

#ifndef ERROR_SUCCESS
#define ERROR_SUCCESS                    0L
#define NO_ERROR                         0L
#define ERROR_INVALID_FUNCTION           1L
#define ERROR_FILE_NOT_FOUND             2L
#define ERROR_PATH_NOT_FOUND             3L
#define ERROR_TOO_MANY_OPEN_FILES        4L
#define ERROR_ACCESS_DENIED              5L
#define ERROR_INVALID_HANDLE             6L
#define ERROR_NOT_ENOUGH_MEMORY          8L
#define ERROR_INVALID_BLOCK              9L
#define ERROR_BAD_ENVIRONMENT            10L
#define ERROR_BAD_FORMAT                 11L
#define ERROR_INVALID_ACCESS             12L
#define ERROR_INVALID_DATA               13L
#define ERROR_OUTOFMEMORY                14L
#define ERROR_INVALID_DRIVE              15L
#define ERROR_NO_MORE_FILES              18L
#define ERROR_WRITE_PROTECT              19L
#define ERROR_BAD_UNIT                   20L
#define ERROR_NOT_READY                  21L
#define ERROR_BAD_COMMAND                22L
#define ERROR_CRC                        23L
#define ERROR_BAD_LENGTH                 24L
#define ERROR_SEEK                       25L
#define ERROR_NOT_DOS_DISK               26L
#define ERROR_SECTOR_NOT_FOUND           27L
#define ERROR_WRITE_FAULT                29L
#define ERROR_READ_FAULT                 30L
#define ERROR_GEN_FAILURE                31L
#define ERROR_SHARING_VIOLATION          32L
#define ERROR_LOCK_VIOLATION             33L
#define ERROR_WRONG_DISK                 34L
#define ERROR_SHARING_BUFFER_EXCEEDED    36L
#define ERROR_HANDLE_EOF                 38L
#define ERROR_HANDLE_DISK_FULL           39L
#define ERROR_NOT_SUPPORTED              50L
#define ERROR_BAD_NETPATH                53L
#define ERROR_BAD_NET_NAME               67L
#define ERROR_FILE_EXISTS                80L
#define ERROR_CANNOT_MAKE                82L
#define ERROR_INVALID_PARAMETER          87L
#define ERROR_BROKEN_PIPE                109L
#define ERROR_OPEN_FAILED                110L
#define ERROR_BUFFER_OVERFLOW            111L
#define ERROR_DISK_FULL                  112L
#define ERROR_CALL_NOT_IMPLEMENTED       120L
#define ERROR_INSUFFICIENT_BUFFER        122L
#define ERROR_INVALID_NAME               123L
#define ERROR_BAD_PATHNAME               161L
#define ERROR_LOCK_FAILED                167L
#define ERROR_ALREADY_EXISTS             183L
#define ERROR_NO_DATA                    232L
#define ERROR_MORE_DATA                  234L
#define ERROR_NO_MORE_ITEMS              259L
#define ERROR_DIRECTORY                  267L
#define ERROR_NOT_OWNER                  288L
#define ERROR_TOO_MANY_POSTS             298L
#define ERROR_BUSY                       170L
#define ERROR_INVALID_ADDRESS            487L
#define ERROR_INTERNAL_ERROR             1359L
#define ERROR_USER_MAPPED_FILE           1224L
#define ERROR_NO_SYSTEM_RESOURCES        1450L
#define ERROR_NONPAGED_SYSTEM_RESOURCES  1451L
#define ERROR_PAGED_SYSTEM_RESOURCES     1452L
#define ERROR_WORKING_SET_QUOTA          1453L
#define ERROR_PAGEFILE_QUOTA             1454L
#define ERROR_COMMITMENT_LIMIT           1455L
#define ERROR_TIMEOUT                    1460L
#define ERROR_INVALID_FLAGS              1004L
#define ERROR_BADDB                      1009L
#define ERROR_INVALID_USER_BUFFER        1784L
#define ERROR_PROC_NOT_FOUND             127L
#define ERROR_MOD_NOT_FOUND              126L
#define ERROR_FILE_INVALID               1006L
#define ERROR_DEVICE_NOT_CONNECTED       1167L
#define ERROR_VC_DISCONNECTED            240L
#define ERROR_FILE_CORRUPT               1392L
#define ERROR_DISK_CORRUPT               1393L
#define ERROR_NO_UNICODE_TRANSLATION     1113L
#define ERROR_NOT_SAME_DEVICE            17L
#define ERROR_BAD_PIPE                   230L
#define ERROR_PIPE_BUSY                  231L
#define ERROR_PIPE_NOT_CONNECTED         233L
#define ERROR_IO_PENDING                 997L
#define ERROR_OPERATION_ABORTED          995L
#define ERROR_IO_INCOMPLETE              996L
#define ERROR_IO_DEVICE                  1117L
#define ERROR_SERIAL_NO_DEVICE           1118L
#define INVALID_FILE_SIZE                ((DWORD)0xFFFFFFFFL)
#define INVALID_SET_FILE_POINTER         ((DWORD)-1)

// HRESULT helpers (winerror.h on Windows). HRESULT is signed; high bit
// flags failure.
#ifndef FAILED
#define FAILED(hr)    (((HRESULT)(hr)) < 0)
#endif
#ifndef SUCCEEDED
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#endif

#endif
