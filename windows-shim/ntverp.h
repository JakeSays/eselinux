// Linux shim for the Windows SDK <ntverp.h>. Real ntverp.h ships with
// the SDK and stamps build version metadata; eseutil only consumes
// VER_PRODUCTMAJORVERSION and VER_PRODUCTMINORVERSION (used to format
// the eseutil banner). Provide a stand-in matching the Windows 10
// SDK numbering so the banner reads sanely.
#pragma once

#define VER_PRODUCTMAJORVERSION 10
#define VER_PRODUCTMINORVERSION 0
#define VER_PRODUCTBUILD        20348
#define VER_PRODUCTBUILD_QFE    1
