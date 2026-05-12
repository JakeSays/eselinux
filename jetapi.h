// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#if !defined(_JET_INCLUDED)
#define _JET_INCLUDED

#include <stdint.h>
#ifndef __cplusplus
#include <uchar.h>          /* char16_t in C; built-in in C++11+ */
#endif

#ifdef  __cplusplus
extern "C" {
#endif

#define eseVersion 0x5600
#define JET_VERSION 0x0A01

#define JET_cbPage  4096

#pragma pack(push, 8)

#define JET_API
#define JET_NODSAPI

typedef uint64_t        JET_API_PTR;

typedef int32_t         JET_ERR;
typedef uint32_t        JET_ENGINEFORMATVERSION;    /* efv - engine format version specification */

typedef JET_API_PTR JET_HANDLE; /* backup file handle */
typedef JET_API_PTR JET_INSTANCE;   /* Instance Identifier */
typedef JET_API_PTR JET_SESID;      /* Session Identifier */
typedef JET_API_PTR JET_TABLEID;    /* Table Identifier */
#if ( JET_VERSION >= 0x0501 )
typedef JET_API_PTR JET_LS;     /* Local Storage */
#endif // JET_VERSION >= 0x0501
#if ( JET_VERSION >= 0x0601 )
typedef JET_API_PTR JET_HISTO;
#endif // JET_VERSION >= 0x0601
typedef uint32_t JET_COLUMNID; /* Column Identifier */

typedef struct tagJET_INDEXID
{
    uint32_t   cbStruct;
    unsigned char   rgbIndexId[sizeof(JET_API_PTR)+sizeof(uint32_t)+sizeof(uint32_t)];
} JET_INDEXID;

typedef uint32_t JET_DBID;     /* Database Identifier */
typedef uint32_t JET_OBJTYP;   /* Object Type */
typedef uint32_t JET_COLTYP;   /* Column Type */
typedef uint32_t JET_GRBIT;    /* Group of Bits */

typedef uint32_t JET_SNP;      /* Status Notification Process */
typedef uint32_t JET_SNT;      /* Status Notification Type */
typedef uint32_t JET_SNC;      /* Status Notification Code */
typedef double JET_DATESERIAL;      /* JET_coltypDateTime format */
typedef uint32_t JET_DLLID;      /* ID of DLL for hook functions */
#if ( JET_VERSION >= 0x0501 )
typedef uint32_t JET_CBTYP;    /* Callback Types */
#endif // JET_VERSION >= 0x0501

typedef JET_ERR (JET_API *JET_PFNSTATUS)(
    JET_SESID  sesid,
    JET_SNP    snp,
    JET_SNT    snt,
    void * pv );
#if ( JET_VERSION >= 0x0A01 )

// This callback is used by JetInit4. Compared to JET_PFNSTATUS
// it has a user-provided context and eliminates the unused sesid
// parameter.
typedef JET_ERR (JET_API * JET_PFNINITCALLBACK)(
    JET_SNP    snp,
    JET_SNT    snt,
    void * pv,             // depends on the snp, snt
    void * pvContext );    // provided in JetInit4

#endif // JET_VERSION >= 0x0A01

typedef char *           JET_PSTR;       /* ASCII string, null-terminated */
typedef const char *     JET_PCSTR;      /* const ASCII string, null-terminated */
typedef char16_t *       JET_PWSTR;      /* UTF-16 string, null-terminated */
typedef const char16_t * JET_PCWSTR;     /* const UTF-16 string, null-terminated */

typedef struct
{
    char            *szDatabaseName;
    char            *szNewDatabaseName;
} JET_RSTMAP_A;         /* restore map */

typedef struct
{
    char16_t           *szDatabaseName;
    char16_t           *szNewDatabaseName;
} JET_RSTMAP_W;         /* restore map */

#ifdef JET_UNICODE
typedef JET_RSTMAP_W JET_RSTMAP;
#else
typedef JET_RSTMAP_A JET_RSTMAP;
#endif
#if ( JET_VERSION >= 0x0A01 )

typedef struct tagJET_SETDBPARAM
{
    uint32_t                           dbparamid;  //  One of the JET_dbparams.

    void *    pvParam;    //  Address of the value of the parameter. Note that even for integral types, a valid
                                                        //  memory location must be passed, as opposed to the numerical value cast to a void*.

    uint32_t                           cbParam;    //  The size of the data, in bytes, pointed to by pvParam.
} JET_SETDBPARAM;

typedef struct
{
    uint32_t                                   cbStruct;           //  size of this structure (for future expansion)
    char                                            *szDatabaseName;    //  (optional) original database path
    char                                            *szNewDatabaseName; //  new database path
    JET_SETDBPARAM  *rgsetdbparam;      //  (optional) array of database parameters
    uint32_t                                   csetdbparam;        //  number of elements in rgsetdbparam
    JET_GRBIT                                       grbit;              //  recovery options
} JET_RSTMAP2_A;

typedef struct
{
    uint32_t                                   cbStruct;           //  size of this structure (for future expansion)
    char16_t                                           *szDatabaseName;    //  (optional) original database path
    char16_t                                           *szNewDatabaseName; //  new database path
    JET_SETDBPARAM  *rgsetdbparam;      //  (optional) array of database parameters
    uint32_t                                   csetdbparam;        //  number of elements in rgsetdbparam
    JET_GRBIT                                       grbit;              //  recovery options
} JET_RSTMAP2_W;

#ifdef JET_UNICODE
typedef JET_RSTMAP2_W JET_RSTMAP2;
#else
typedef JET_RSTMAP2_A JET_RSTMAP2;
#endif

#endif // JET_VERSION >= 0x0A01
//  For edbutil convert and JetConvert() only.

typedef struct tagCONVERT_A
{
    char                    *szOldDll;
    union
    {
        uint32_t       fFlags;
        struct
        {
            uint32_t   fSchemaChangesOnly:1;
        };
    };
} JET_CONVERT_A;

typedef struct tagCONVERT_W
{
    char16_t                   *szOldDll;
    union
    {
        uint32_t       fFlags;
        struct
        {
            uint32_t   fSchemaChangesOnly:1;
        };
    };
} JET_CONVERT_W;

#ifdef JET_UNICODE
typedef JET_CONVERT_W JET_CONVERT;
#else
typedef JET_CONVERT_A JET_CONVERT;
#endif
typedef enum
{

    //  Database operations

    opDBUTILConsistency,
    opDBUTILDumpData,
    opDBUTILDumpMetaData,
    opDBUTILDumpPage,
    opDBUTILDumpNode,
    opDBUTILDumpTag,
    opDBUTILDumpSpace,
    opDBUTILSetHeaderState,
    opDBUTILDumpHeader,
    opDBUTILDumpLogfile,
    opDBUTILDumpLogfileTrackNode,
    opDBUTILDumpCheckpoint,
    opDBUTILEDBDump,
    opDBUTILEDBRepair,
    opDBUTILMunge,
    opDBUTILEDBScrub,
    opDBUTILSLVMove_ObsoleteAndUnused, // No longer used. Left in to preserve the subsequent enum values.
    opDBUTILDBConvertRecords_ObsoleteAndUnused, // No longer used. Left in to preserve the subsequent enum values.
    opDBUTILDBDefragment,
    opDBUTILDumpExchangeSLVInfo_ObsoleteAndUnused, // No longer used. Left in to preserve the subsequent enum values.
    opDBUTILDumpUnicodeFixupTable_ObsoleteAndUnused, // No longer used. Left in to preserve the subsequent enum values.
    opDBUTILDumpPageUsage,
    opDBUTILUpdateDBHeaderFromTrailer,
    opDBUTILChecksumLogFromMemory,
    opDBUTILDumpFTLHeader,
    opDBUTILDBTrim,
    opDBUTILDumpFlushMapFile,
    opDBUTILDumpSpaceCategory,
    opDBUTILDumpCachedFileHeader,
    opDBUTILDumpCacheFile,
    opDBUTILDumpRBSHeader,
    opDBUTILDumpRBSPages,
    opDBUTILEstimateRootSpaceLeak,
} DBUTIL_OP;

typedef enum
{
    opEDBDumpTables,
    opEDBDumpIndexes,
    opEDBDumpColumns,
    opEDBDumpCallbacks,
    opEDBDumpPage,
} EDBDUMP_OP;

typedef struct tagDBUTIL_A
{
    uint32_t   cbStruct;

    JET_SESID       sesid;
    JET_DBID        dbid;
    JET_TABLEID     tableid;

    DBUTIL_OP       op;
    EDBDUMP_OP      edbdump;
    JET_GRBIT       grbitOptions;

    // When adding to this union; you must use
    // fewer bytes than legacy to maintain forward/backward
    // compatibility for all clients that use it.

    union
    {
        // legacy elements
        struct
        {
            char           *szDatabase;
            char           *szSLV_ObsoleteAndUnused; // No longer used. Left in to preserve the subsequent values;
            char           *szBackup;
            const char     *szTable;
            const char     *szIndex;
            char           *szIntegPrefix;

            int32_t            pgno;
            int32_t            iline;

            int32_t            lGeneration;
            int32_t            isec;
            int32_t            ib;

            int32_t            cRetry;

            void *          pfnCallback;
            void *          pvCallback;
        };

        // ChecksumLogFromMemory
        struct
        {
            char            *szLog;     // Name of the Log file
            char            *szBase;    // Base name used e.g. "edb" or "E01"
            void            *pvBuffer;  // Pointer to buffer containing the log
            int32_t             cbBuffer;  // Length of buffer
        } checksumlogfrommemory;

        // opDBUTILDumpSpaceCategory
        struct
        {
            char               *szDatabase;            // Database from which to dump the space category of pages.
            uint32_t       pgnoFirst;             // First page to dump the category for. The first page in the database is 1.
            uint32_t       pgnoLast;              // Last page to dump the category for. The last page in the database can be passed in as (ulong)-1.
            void               *pfnSpaceCatCallback;   // Callback to receive each page's category (JET_SPCATCALLBACK).
            void               *pvContext;             // General purpose context which is passed back to the client callback (pfnSpaceCatCallback).
        } spcatOptions;

        // opDBUTILDumpRBS
        struct
        {
            char               *szDatabase;            // Database from which to dump the space category of pages.
            uint32_t       pgnoFirst;             // First page to dump the category for. The first page in the database is 1.
            uint32_t       pgnoLast;              // Last page to dump the category for. The last page in the database can be passed in as (ulong)-1.
        } rbsOptions;

    };

} JET_DBUTIL_A;

typedef struct tagDBUTIL_W
{
    uint32_t   cbStruct;

    JET_SESID       sesid;
    JET_DBID        dbid;
    JET_TABLEID     tableid;

    DBUTIL_OP       op;
    EDBDUMP_OP      edbdump;
    JET_GRBIT       grbitOptions;

    // When adding to this union; you must use
    // fewer bytes than legacy to maintain forward/backward
    // compatibility for all clients that use it.

    union
    {
        // legacy elements
        struct
        {
            char16_t          *szDatabase;
            char16_t          *szSLV_ObsoleteAndUnused; // No longer used. Left in to preserve the subsequent values;
            char16_t          *szBackup;
            const char16_t    *szTable;
            const char16_t    *szIndex;
            char16_t          *szIntegPrefix;

            int32_t            pgno;
            int32_t            iline;

            int32_t            lGeneration;
            int32_t            isec;
            int32_t            ib;

            int32_t            cRetry;

            void           *pfnCallback;
            void           *pvCallback;
        };

        // ChecksumLogFromMemory
        struct
        {
            char16_t           *szLog;     // Name of the Log file
            char16_t           *szBase;    // Base name used e.g. "edb" or "E01"
            void            *pvBuffer;  // Pointer to buffer containing the log
            int32_t             cbBuffer;  // Length of buffer
        } checksumlogfrommemory;

        // opDBUTILDumpSpaceCategory
        struct
        {
            char16_t              *szDatabase;            // Database from which to dump the space category of pages.
            uint32_t       pgnoFirst;             // First page to dump the category for. The first page in the database is 1.
            uint32_t       pgnoLast;              // Last page to dump the category for. The last page in the database can be passed in as (ulong)-1.
            void               *pfnSpaceCatCallback;   // Callback to receive each page's category (JET_SPCATCALLBACK).
            void               *pvContext;             // General purpose context.
        } spcatOptions;

        // opDBUTILDumpRBS
        struct
        {
            char16_t               *szDatabase;           // Database from which to dump the space category of pages.
            uint32_t       pgnoFirst;             // First page to dump the category for. The first page in the database is 1.
            uint32_t       pgnoLast;              // Last page to dump the category for. The last page in the database can be passed in as (ulong)-1.
        } rbsOptions;

    };

} JET_DBUTIL_W;

#ifdef JET_UNICODE
typedef JET_DBUTIL_W JET_DBUTIL;
#else
typedef JET_DBUTIL_A JET_DBUTIL;
#endif

//
//  Command (DBUTIL_OP op) specific DBUtil options
//

#if ( JET_VERSION >= 0x0A01 )
// Space category flags. Returned by opDBUTILDumpSpaceCategory.
typedef enum
{
    spcatfNone               = 0x00000000,   // Not a real flag, just a constant to signal no flags.

    // Expected/consistent categories.
    spcatfStrictlyLeaf       = 0x00000001,   // Page is strictly a leaf (i.e., not a root).
    spcatfStrictlyInternal   = 0x00000002,   // Page is strictly an internal page (i.e., not a root).
    spcatfRoot               = 0x00000004,   // Page is a root.
    spcatfSplitBuffer        = 0x00000008,   // Page is part of a split buffer.
    spcatfSmallSpace         = 0x00000010,   // Page is part of a small space tree.
    spcatfSpaceOE            = 0x00000020,   // Page belongs to an OE tree.
    spcatfSpaceAE            = 0x00000040,   // Page belongs to an AE tree (to the space tree itself, not a free/available page).
    spcatfAvailable          = 0x00000080,   // Page is free/available at the root or object level.
    // spcatf... add in increasing bit significance order (make sure it doesn't collide with the exceptional states below).

    // Exceptional categories.
    spcatfIndeterminate      = 0x04000000,   // Page type could not be determined because full category search is not enabled.
    spcatfInconsistent       = 0x08000000,   // Page was found to be inconsistent during lookup.
    spcatfLeaked             = 0x10000000,   // Page is leaked at the root or object level.
    spcatfNotOwned           = 0x20000000,   // Page is not owned by the DB root and is before or at the last known-owned page (this means root OE corruption).
    spcatfNotOwnedEof        = 0x40000000,   // Page is not owned by the DB root and is beyond the last known-owned page.
    spcatfUnknown            = 0x80000000,   // Unknown page type.
    // spcatf... add in decreasing bit significance order (i.e., top of the list above).
} SpaceCategoryFlags;

// Callback used by opDBUTILDumpSpaceCategory to return page space categories.
typedef void (JET_API *JET_SPCATCALLBACK)( const uint32_t pgno, const uint32_t objid, const SpaceCategoryFlags spcatf, void* const pvContext );
#endif // JET_VERSION >= 0x0A01

//  DBUTIL_OP op = opDBUTILDumpSpace
//
#define JET_bitDBUtilSpaceInfoBasicCatalog          0x00000001
#define JET_bitDBUtilSpaceInfoSpaceTrees                0x00000002
#define JET_bitDBUtilSpaceInfoParentOfLeaf          0x00000004
#define JET_bitDBUtilSpaceInfoFullWalk              0x00000008
//  This command also utilizes this option:
//      JET_bitDBUtilOptionDumpVerbose          0x10000000

#if ( JET_VERSION >= 0x0A01 )
//  DBUTIL_OP op = opDBUTILDumpSpaceCategory
//
#define JET_bitDBUtilFullCategorization         0x00000001
#endif // JET_VERSION >= 0x0A01

//  DBUTIL_OP op = <all others>
//
#define JET_bitDBUtilOptionAllNodes             0x00000001
#define JET_bitDBUtilOptionKeyStats             0x00000002
#define JET_bitDBUtilOptionPageDump             0x00000004
#define JET_bitDBUtilOptionStats                0x00000008
#define JET_bitDBUtilOptionSuppressConsoleOutput 0x00000010
#define JET_bitDBUtilOptionIgnoreErrors         0x00000020
#define JET_bitDBUtilOptionVerify               0x00000040
#define JET_bitDBUtilOptionReportErrors         0x00000080
#define JET_bitDBUtilOptionDontRepair           0x00000100
#define JET_bitDBUtilOptionRepairAll            0x00000200
#define JET_bitDBUtilOptionRepairIndexes        0x00000400
#define JET_bitDBUtilOptionDontBuildIndexes     0x00000800
//#define JET_bitDBUtilOptionRepairSLVChecksum  0x00001000  //  DEPRECATED
//#define JET_bitDBUtilOptionRepairMissingStream    0x00002000  //  DEPRECATED
//#define JET_bitDBUtilOptionIgnoreDbSLVMismatch    0x00004000  //  DEPRECATED
#define JET_bitDBUtilOptionSuppressLogo         0x00008000
#define JET_bitDBUtilOptionRepairCheckOnly      0x00010000
#define JET_bitDBUtilOptionDumpLVPageUsage      0x00020000
#if ( JET_VERSION >= 0x0600 )
#define JET_bitDBUtilOptionDumpLogInfoCSV           0x00040000
#define JET_bitDBUtilOptionDumpLogPermitPatching    0x00080000  //  permit the log dumper to patch edb.jtx/log if necessary
#endif // JET_VERSION >= 0x0600
#define JET_bitDBUtilOptionSkipMinLogChecksUpdateHeader 0x00100000
#define JET_bitDBUtilOptionDumpVerbose          0x10000000
#define JET_bitDBUtilOptionDumpVerboseLevel1    JET_bitDBUtilOptionDumpVerbose
#define JET_bitDBUtilOptionDumpVerboseLevel2    0x20000000
//#define JET_bitDBUtilOptionCheckBTree           0x20000000  //  DEPRECATED
#define JET_bitDBUtilOptionDumpLogSummary       0x40000000
//  Configuration Store
//
//  ESE has the ability to use an external config store for ESE database engine and instance
//  settings.
//
//  Using the registry this might look something like setting the JET param to this:
//
//      JET_paramConfigStoreSpec    "reg:HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\PopServer(Inst1)"
//
//          where "PopServer(Inst1)" is just an exaple name, you should pick a different name or
//          even a different part of the registry if appropriate.  You are limited however to
//          beginning under: HKEY_LOCAL_MACHINE or HKEY_CURRENT_USER.
//
//      And configuring the registry thusly:
//
//Windows Registry Editor Version 5.00
//
//[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\PopServer(Inst1)]
//
//[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\PopServer(Inst1)\SysParamDefault]
//"JET_paramDatabasePageSize"="8192"
//"JET_paramEnableFileCache"="1"
//
//  All values should be set as a string version of a decimal of hex values (such as "0x2000").
//

//
//  The security settings / [D]ACL of the registry key provided to JET_paramConfigStoreSpec
//  should be locked down to the same security context as the client application that uses ESE
//  and opens the database, otherwise there is a possible Escalation of Privilege attack.
//

//  The JET_wszConfigStoreReadControl in the registry are registry values under the root
//  registry key passed to JET_paramConfigStoreSpec.

#define JET_wszConfigStoreReadControl                           L"CsReadControl"
#define JET_bitConfigStoreReadControlInhibitRead                 0x1        //  Will stop reading from the registry config store, and pause reading until flag is removed (this will stall some JET initialization APIs).
#define JET_bitConfigStoreReadControlDisableAll                  0x2        //  Simply disables the registry config store from being read or used.
#define JET_bitConfigStoreReadControlDisableSetParamRead         0x4        //  If ESE should not read the config data at the time we set the JET_paramConfigStoreSpec via JetSetSystemParameter().
#define JET_bitConfigStoreReadControlDisableGlobalInitRead       0x8        //  If ESE should not read the config data at the time of global initialization / when ESE creates the very first instance.
#define JET_bitConfigStoreReadControlDisableInstInitRead        0x10        //  If ESE should not read the config data at the time we initialize the instance.
#define JET_bitConfigStoreReadControlDisableLiveRead            0x20        //  [Reserved for future usage at some point] If ESE should not read / update the values live.
#define JET_bitConfigStoreReadControlDefault                     0x0        //  Use default ESE behavior.
//  Useful for debugging and rapid development, this will fill out the ESE Config
//  Store registry key with all the values that the engine reads during runtime.

#define JET_wszConfigStorePopulateControl                       L"CsPopulateControl"
#define JET_bitConfigStorePopulateControlOff                    0x00
#define JET_bitConfigStorePopulateControlOn                     0x01
//  The JET_wszConfigStoreRelPathSysParamDefault and JET_wszConfigStoreRelPathSysParamOverride in
//  the registry are registry sub-keys under the root registry key passed to JET_paramConfigStoreSpec.
#define JET_wszConfigStoreRelPathSysParamDefault        L"SysParamDefault"
#define JET_wszConfigStoreRelPathSysParamOverride       L"SysParamOverride"
#if ( JET_VERSION >= 0x0A01 )

// These are _not_ mis-tabbed, intentionally tabbed in twice for released format versions to be able to easily
// distinguish them from independent format features, while keeping them in order. 4 space tab width.

#define JET_efvExchange55Rtm                     500                //  Final format version for Exchange 5.5 at RTM.  This is basically ESE97.
#define JET_efvWindows2000Rtm                    600                //  Final format version for Windows 2000 at RTM.  This is basically ESE97.1.
#define JET_efvExchange2000Rtm                   1000               //  Final format version for Exchange 2000 at RTM.  This is basically ESE98.
#define JET_efvWindowsXpRtm                      1200               //  Final format version for Windows XP at RTM.  This is basically ESE98.1.
#define JET_efvExchange2010Rtm                   5520               //  Final format version for Exchange 2010 (E14) at RTM.
#define JET_efvExchange2010Sp1                   5920               //  Final format version for Exchange 2010 (E14) at SP1.
#define JET_efvExchange2010Sp2                   6080               //  Final format version for Exchange 2010 (E14) at SP2.
//      JET_efvWindows8                          ~6520              //  Final format version for Windows 8 RTM.
#define JET_efvScrubLastNodeOnEmptyPage                     6960    //  Scrub the remaining prefix key on an empty page.
#define JET_efvExchange2013Rtm                   7020               //  Final format version for Exchange 2013 (E15) at RTM.
#define JET_efvExtHdrRootFieldAutoIncStorageStagedToDebug   7060    //  Extend node/page's extended header to have multiple root fields values therein; And store the max Auto-Increment Column value used in a new extended header root field.
//      JET_efvExchange2013Cu6                   7900               //  Final format version for Exchange 2013 (E15) at CU6.
#define JET_efvLogExtendDbLrAfterCreateDbLr                 7940    //  Allow the recently (as of 6 months prior) variable initial DB size to be logged with an ExtendDB LR so that recovery can always recover the DB to the exact same size as intended by the ESE consumer.
#define JET_efvRollbackInsertSpaceStagedToTest              7960    //  Implemented rolling back of insert operations triggers delete/merge to cleanup the previously used space.
#define JET_efvWindows10Rtm                     (8020)              //  Final format version for Windows Threshold / 10.
#define JET_efvQfeMsysLocalesGuidRefCountFixup              8023    //  See below JET_efvMsysLocalesGuidRefCountFixup.
#define JET_efvPersistedLostFlushMapStagedToDebug           8220    //  Added signDbHdrFlush, signFlushMapHdrFlush, le_lGenMinConsistent and the new persisted flush map.
#define JET_efvTriStatePageFlushTypeStagedToProd            8280    //  Using a 2-bit state in the database page hdr to indicate flush state.
#define JET_efvLogSplitMergeCompressionReleased             8400    //  Graduate Split/Merge log record compression to retail.
#define JET_efvTriStatePageFlushTypeReleased                8420    //  Using a 2-bit state in the database page hdr to indicate flush state in retail.
#define JET_efvIrsDbidBasedMacroInfo2                       8440    //  Adds lrtypMacroInfo2 for IRS of multiple DBs attached to a single log stream.
#define JET_efvLostFlushMapCrashResiliency                  8460    //  Add crash resiliency to the lost flush map.
#define JET_efvExchange2016Rtm                   8520               //  Final format version for Exchange 2016 RTM.  Actually released summer of 2015.
#define JET_efvMsysLocalesGuidRefCountFixup                 8620    //  Updated MsysLocales init code to fixup the ref counts of GUIDs based upon right version correctly.
#define JET_efvWindows10v2Rtm                    8620               //  Final format version for Windows 10 Version 1511 (Threshold 2).
#define JET_efvEncryptedColumns                             8720    //  Encrypted columns.
#define JET_efvLoggingDeleteOfTableFcbs                     8820    //  Signal deleted of FCBs to keep recovery cache correct.
#define JET_efvEmptyIdxUpgradesUpdateMsysLocales            8860    //  Ensure we update the MSysLocales table when we upgraded for empty indices.
#define JET_efvSupportNlsInvariantLocale                    8880    //  Supports creating a table or index with the invariant locale sorting purposes.
#define JET_efvExchange2016Cu1Rtm                8920               //  More of a .1 release, as its not just QFEs. CU1 forked 2015/01/05 - 15.1.396.
#define JET_efvWindows19H1Rtm                    8920               //  Last pre-efv version, shipped in windows 10 until 19H1
#define JET_efvSetDbVersion                                 8940    //  Added lrtypSetDbVersion to log DB version updates, and a UpdateMinor and a efv"LastAttached" version to the DB file header.
                                                                    //      Notes: Guarantees via recovery that a DB version representative of what the transaction log
                                                                    //      has redone to the DB file (layering violation info: and most importantly to fix replicas
                                                                    //      in Exchange).
#define JET_efvExtHdrRootFieldAutoIncStorageReleased        8960    //  Moves to retail the extend node/page's extended header to have multiple root fields values therein; And store the max Auto-Increment Column value used in a new extended header root field.
#define JET_efvXpress9Compression                           8980    //  Adds support for compressing/decompressing data using Xpress9.
#define JET_efvUppercaseTextNormalization                   9000    //  Allow LCMAP_UPPERCASE for OS text normalization flags in index definitions.
#define JET_efvDbscanHeaderHighestPageSeen                  9020    //  Adds support for tracking (in the header) the highest page seen by dbscan follower
#define JET_efvEscrow64                                     9040    //  Adds support for 64-bit escrow columns.
#define JET_efvSynchronousLVCleanup                         9060    //  Adds support for cleaning up LVs synchronously
#define JET_efvLid64                                        9080    //  New LID format: LIDs become 64-bit numbers.
#define JET_efvShrinkEof                                    9100    //  Added lrtypShrinkDB2, which changes the the meaning of cpgShrunk in the existing lrtypShrinkDB and how it is replayed.
#define JET_efvLogNewPage                                   9120    //  Added lrtypNewPage, which logs new page operations to better handle rolling page incomplete operations that require new pages.
#define JET_efvRootPageMove                                 9140    //  Added support for moving tree roots and their respective space tree roots (including new lrtypRootPageMove and lrtypSignalAttachDb).
#define JET_efvScanCheck2                                   9160    //  Added new log record lrtypScanCheck2 to allow for reporting of the initiator of the ScanCheck log record.
#define JET_efvLgposLastResize                              9180    //  Stamps the log position of the last database resize operation to the database header.
#define JET_efvShelvedPages                                 9200    //  Added the concept of shelved pages (available or leaked pages beyond EOF).
#define JET_efvShelvedPagesRevert                           9220    //  Reverts JET_efvShelvedPages, with additional code to make it a safe revert.
#define JET_efvShelvedPages2                                9240    //  Redoes JET_efvShelvedPages.
#define JET_efvLogtimeGenMaxRequired                        9260    //  Adds logtimeGenMaxRequired to database header
#define JET_efvVariableDbHdrSignatureSnapshot               9280    //  Adds ability to append fields to the DB header without doing a major update by storing a variable-sized header signature.
#define JET_efvLowerMinReqLogGenOnRedo                      9300    //  Brings the minimum required log generation down if we start replaying below the current min. required.
#define JET_efvDbTimeShrink                                 9320    //  Added lrtypShrinkDB3, which logs the database's running dbtime at the time of the shrink operation, to allow for replaying the shrink operation more granularly.
#define JET_efvXpress10Compression                          9340    //  Adds support for compressing/decompressing data using Xpress10.
#define JET_efvRevertSnapshot                               9360    //  Added revert snapshot flush signature to database header and added lrtypExtentFreed, which logs details about the extent freed to allow for revert snapshot to capture the pages of the freed extent.
#define JET_efvApplyRevertSnapshot                          9380    //  Added le_lgposCommitBeforeRevert to the database header which captures the last commit lgpos before revert was done and is used to ignore JET_errDbTimeTooOld errors on pasive copies.
#define JET_efvExtentPageCountCache                         9400    //  Adds support for the ExtentPageCountCache table.
#define JET_efvLz4Compression                               9420    //  Adds support for compressing/decompressing data using Lz4.
// 9440 being skipped due to revert of a bad deployed build
#define JET_efvRBSNonRevertableTableDeletes                 9460    //  Adds support for non-revertable table deletes. The active will stop logging extent freed LR for all freed extent but if available lag doesn't support it yet, shouldn't be allowed.
#define JET_efvScanCheck2Flags                              9480    //  The byte le_bSource in ScanCheck2 LR is split into 3 components and changed to le_bFlagsAndScs.
                                                                    //      The highest bit is used for objidInvalid flag and the bit next to highest is used for emptypage
                                                                    //      flag. The next 4 bits are left unused (for now) and the lower 2 bits are used for ScanCheckSource.
#define JET_efvExtentFreed2                                 9500    //  Adds support for ExtentFreed2 LR which adds dbtime of the database to the existing ExtentFreed LR.
#define JET_efvKVPStoreV2                                   9520    //  Allows upgrade of KVP stores to version 1.0.2
#define JET_efvIndexDeferredPopulate                        9540    //  Adds support for deferred population of indices.
#define JET_efvReservedTags                                 9560    //  Allows adding additional reserved tags to cpage.
#define JET_efvRBSTooSoonDeletes                            9580    //  Allows to decide if we can now perform non-revertable delete even if root page of table was moved recently by shrink or created recently.
#define JET_efvOptionallyUniqueIndices                      9600    //  Allows creation of optionally unique indices.

// Special format specifiers here
#define JET_efvUseEngineDefault             (0x40000001)    //  Instructs the engine to use the maximal default supported Engine Format Version. (default)
#define JET_efvUsePersistedFormat           (0x40000002)    //  Instructs the engine to use the minimal Engine Format Version of all loaded log and DB files.
#define JET_efvAllowHigherPersistedFormat   (0x41000000)    //  Can be combined with a specific EngineFormatVersion but will not fail if persisted files are ahead of the specified EngineFormatVersion.  Will still fail if the persisted version is ahead of what the engine actually can read/understand.

#endif // JET_VERSION >= 0x0A01

#if ( JET_VERSION >= 0x0601 )
//  JetDatabaseScan options
#define JET_bitDatabaseScanBatchStart           0x00000010  //  Starts a single-pass Database Maintenance thread.
#define JET_bitDatabaseScanBatchStop            0x00000020  //  Stops the Database Maintenance thread and frees any resources associated with it.
#define JET_bitDatabaseScanZeroPages            0x00000040  //  DEPRECATED
#define JET_bitDatabaseScanBatchStartContinuous 0x00000080  //  Starts a continuously-running Database Maintenance thread. pcSecondsMax, cmsecSleep and pfnCallback API parameters are not honored (system parameters should be used to fine-tune DBM).
#endif // JET_VERSION >= 0x0601
//  Online defragmentation (JetDefragment/JetDefragment2) options
#define JET_bitDefragmentBatchStart             0x00000001
#define JET_bitDefragmentBatchStop              0x00000002
// Obsolete:
// #define JET_bitDefragmentTest                    0x00000004  /* run internal tests (non-RTM builds only) */
// #define JET_bitDefragmentSLVBatchStart           0x00000008
// #define JET_bitDefragmentSLVBatchStop            0x00000010
// #define JET_bitDefragmentScrubSLV                0x00000020  /* synchronously zero free pages in the streaming file */
#if ( JET_VERSION >= 0x0501 )
#define JET_bitDefragmentAvailSpaceTreesOnly    0x00000040  /* only defrag AvailExt trees */
#endif // JET_VERSION >= 0x0501
#if ( JET_VERSION >= 0x0601 )
#define JET_bitDefragmentNoPartialMerges        0x00000080  /* don't do partial merges during OLD */

#define JET_bitDefragmentBTree                  0x00000100  /* defrag one B-Tree */
#if ( JET_VERSION >= 0x0A01 )
#define JET_bitDefragmentBTreeBatch             0x00000200  /* specifies options pertain to OLD2 / B-Tree defrag, such as JET_bitDefragmentBatchStart */
#endif // JET_VERSION >= 0x0A01
#endif // JET_VERSION >= 0x0601

#if ( JET_VERSION >= 0x0501 )
    /* Callback-function types */

#define JET_cbtypNull                           0x00000000
#define JET_cbtypFinalize                       0x00000001  /* DEPRECATED: a finalizable column has gone to zero */
#define JET_cbtypBeforeInsert                   0x00000002  /* about to insert a record */
#define JET_cbtypAfterInsert                    0x00000004  /* finished inserting a record */
#define JET_cbtypBeforeReplace                  0x00000008  /* about to modify a record */
#define JET_cbtypAfterReplace                   0x00000010  /* finished modifying a record */
#define JET_cbtypBeforeDelete                   0x00000020  /* about to delete a record */
#define JET_cbtypAfterDelete                    0x00000040  /* finished deleting the record */
#define JET_cbtypUserDefinedDefaultValue        0x00000080  /* calculating a user-defined default */
#define JET_cbtypOnlineDefragCompleted          0x00000100  /* a call to JetDefragment2 has completed */
#define JET_cbtypFreeCursorLS                   0x00000200  /* the Local Storage associated with a cursor must be freed */
#define JET_cbtypFreeTableLS                    0x00000400  /* the Local Storage associated with a table must be freed */
#if ( JET_VERSION >= 0x0601 )
//  actions for the defragmentation callback
#define JET_bitOld2Start                        0x00000001  /* defrag action start (testing only) */
#define JET_bitOld2Suspend                      0x00000002  /* defrag action suspend (testing only) */
#define JET_bitOld2Resume                       0x00000004  /* defrag action resume (testing only) */
#define JET_bitOld2End                          0x00000008  /* defrag action end (testing only) */

#define JET_cbtypScanProgress                   0x00004000  /* JetDatabaseScan progress */
#define JET_cbtypScanCompleted                  0x00008000  /* JetDatabaseScan completed a pass */
#endif // JET_VERSION >= 0x0601
#define JET_cbtypDTCQueryPreparedTransaction    0x00001000  /* recovery is attempting to resolve a PreparedToCommit transaction */
#define JET_cbtypOnlineDefragProgress           0x00002000  /* online defragmentation has made progress */
//  callback for JetDefragment2 actions (testing only)
//  pvArg1 is ptr to table name, pvArg2 is ptr to action code JET_bitOld2Start, etc...
#define JET_cbtypOld2Action                     0x00004000
#define JET_cbtypIndexDeferredPopulateThrottle  0x00008000
    /* Callback-function prototype */

typedef JET_ERR (JET_API *JET_CALLBACK)(
    JET_SESID      sesid,
    JET_DBID       dbid,
    JET_TABLEID    tableid,
    JET_CBTYP      cbtyp,
    void *  pvArg1,
    void *  pvArg2,
    void *     pvContext,
    JET_API_PTR    ulUnused );
#endif // JET_VERSION >= 0x0501
//  changes to JET_cbCallbackUserDataMost should affect JET_cbCallbackDataAllMost as well
//  (see below)
//
#define JET_cbCallbackUserDataMost              1024

//  callback data most is the following:
//
//  sizeof(JET_USERDEFINEDDEFAULT)
//  + JET_cbNameMost + 1 /* for callback name */
//  + JET_cbCallbackUserDataMost
//  + ( ( JET_ccolKeyMost * ( JET_cbNameMost + 1 ) ) + 1 ) /* for list of dependent columns */
//  = ~ 3234 - 3246 bytes
//
#define JET_cbCallbackDataAllMost               4096

//  return JET_errFileIOAbort, JET_errFileIORetry, or JET_errFileIOFail
//
//  currently UNSUPPORTED
//
typedef JET_ERR (JET_API *JET_ABORTRETRYFAILCALLBACK_A)(
    char *         szFile,
    uint32_t  Offset,
    uint32_t  OffsetHigh,
    uint32_t  Length,
    JET_ERR        err );

typedef JET_ERR (JET_API *JET_ABORTRETRYFAILCALLBACK_W)(
    char16_t *        szFile,
    uint32_t  Offset,
    uint32_t  OffsetHigh,
    uint32_t  Length,
    JET_ERR        err );

#ifdef JET_UNICODE
typedef JET_ABORTRETRYFAILCALLBACK_W JET_ABORTRETRYFAILCALLBACK;
#else
typedef JET_ABORTRETRYFAILCALLBACK_A JET_ABORTRETRYFAILCALLBACK;
#endif

#if ( JET_VERSION >= 0x0600 )
//  trace-tag support
//
typedef enum
{
    JET_tracetagNull,
    JET_tracetagInformation,            // [Informational] Information trace tag.
    JET_tracetagErrors,                 // [Informational] Errors trace tag.
    JET_tracetagAsserts,                // [Error] Asserts trace tag.
    JET_tracetagAPI,
    JET_tracetagInitTerm,               // High level status traces for beginning and finishing of instance-level init/term.
    JET_tracetagBufferManager,
    JET_tracetagBufferManagerHashedLatches,
    JET_tracetagIO,
    JET_tracetagMemory,
    JET_tracetagVersionStore,
    JET_tracetagVersionStoreOOM,
    JET_tracetagVersionCleanup,
    JET_tracetagCatalog,
    JET_tracetagDDLRead,
    JET_tracetagDDLWrite,
    JET_tracetagDMLRead,
    JET_tracetagDMLWrite,
    JET_tracetagDMLConflicts,
    JET_tracetagInstances,
    JET_tracetagDatabases,
    JET_tracetagSessions,
    JET_tracetagCursors,
    JET_tracetagCursorNavigation,
    JET_tracetagCursorPageRefs,
    JET_tracetagBtree,
    JET_tracetagSpace,
    JET_tracetagFCBs,
    JET_tracetagTransactions,
    JET_tracetagLogging,
    JET_tracetagRecovery,
    JET_tracetagBackup,
    JET_tracetagRestore,
    JET_tracetagOLD,
    JET_tracetagEventlog,
#if ( JET_VERSION >= 0x0601 )
    JET_tracetagBufferManagerMaintTasks,    // BufferManagerMaintTasks trace tag. Introduced in Windows 7.
    JET_tracetagSpaceManagement,
    JET_tracetagSpaceInternal,
    JET_tracetagIOQueue,
    JET_tracetagDiskVolumeManagement,
#if ( JET_VERSION >= 0x0602 )
    JET_tracetagCallbacks,              // Callbacks trace tag. Introduced in Windows 8.
    JET_tracetagIOProblems,             // [Error] I/O Problems.
    JET_tracetagUpgrade,
    JET_tracetagRecoveryValidation,
    JET_tracetagBufferManagerBufferCacheState,
    JET_tracetagBufferManagerBufferDirtyState,
    JET_tracetagTimerQueue,
    JET_tracetagSortPerf,
#if ( JET_VERSION >= 0x0A00 )
    JET_tracetagOLDRegistration,        // Registration events for online defragmentation. Introduced in Windows 10.
    JET_tracetagOLDWork,                // Work events for online defragmentation progress. Introduced in Windows 10.
    JET_tracetagSysInitTerm,            // High level status traces for beginning and finishing of one-time system init/term operations during DLL load/unload. Contrast to JET_tracetagInitTerm. Introduced in Windows 10.
#if ( JET_VERSION >= 0x0A01 )
    JET_tracetagVersionAndStagingChecks,
    JET_tracetagFile,
    JET_tracetagFlushFileBuffers,       // Traces for flush file buffers (including suppressed FFB calls).
    JET_tracetagCheckpointUpdate,
    JET_tracetagDiagnostics,
    JET_tracetagBlockCache,
    JET_tracetagRBS,
    JET_tracetagRBSCleaner,
    JET_tracetagBlockCacheOperations,

    //  Add all new tracetags here, must be in order ...
#endif // JET_VERSION >= 0x0A01
#endif // JET_VERSION >= 0x0A00
#endif // JET_VERSION >= 0x0602
#endif // JET_VERSION >= 0x0601
    JET_tracetagMax,                    //  Maximum trace tag value (invalid).
} JET_TRACETAG;

// UNICODE_UNDONE_DEFERRED: change the APIs below to use UNICODE
//
//  tracing callbacks
//
typedef void (JET_API *JET_PFNTRACEEMIT)(
    const JET_TRACETAG tag,
    JET_PCSTR          szPrefix,
    JET_PCSTR          szTrace,
    const JET_API_PTR  ul );
typedef void (JET_API *JET_PFNTRACEREGISTER)(
    const JET_TRACETAG tag,
    JET_PCSTR          szDesc,
    JET_API_PTR *     pul );

//  JetTracing operations
//
typedef enum
{
    JET_traceopNull,
    JET_traceopSetGlobal,               //  enable/disable tracing ("ul" param cast to long)
    JET_traceopSetTag,                  //  enable/disable tracing for specified tag ("ul" param cast to long)
    JET_traceopSetAllTags,              //  enable/disable tracing for all tags ("ul" param cast to long)
    JET_traceopSetMessagePrefix,        //  text which should prefix all emitted messages ("ul" param cast to char*)
    JET_traceopRegisterTag,             //  callback to register a trace tag ("ul" param cast to JET_PFNTRACEREGISTER)
    JET_traceopRegisterAllTags,         //  callback to register all trace tags ("ul" param cast to JET_PFNTRACEREGISTER)
    JET_traceopSetEmitCallback,         //  override default trace emit function with specified function, or pass NULL to revert to default ("ul" param ast to JET_PFNTRACEEMIT)
    JET_traceopSetThreadidFilter,       //  threadid to use to filter traces (0==all threads, -1==no threads)
    JET_traceopSetDbidFilter,           //  JET_DBID to use to filter traces (0x7fffffff==all db's, JET_dbidNil==no db's)
    JET_traceopMax
} JET_TRACEOP;
#endif // JET_VERSION >= 0x0600

    /*  Session information bits */

#define JET_bitCIMCommitted                     0x00000001
#define JET_bitCIMDirty                         0x00000002
#define JET_bitAggregateTransaction             0x00000008

#if ( JET_VERSION >= 0x0600 )
typedef struct JET_SESSIONINFO
{
    uint32_t   ulTrxBegin0;
    uint32_t   ulTrxLevel;
    uint32_t   ulProcid;
    uint32_t   ulFlags;
    JET_API_PTR     ulTrxContext;
} JET_SESSIONINFO;
#endif // JET_VERSION >= 0x0600
    /* Status Notification Structures */

typedef struct              /* Status Notification Progress */
{
    uint32_t   cbStruct;   /* Size of this structure */
    uint32_t   cunitDone;  /* Number of units of work completed */
    uint32_t   cunitTotal; /* Total number of units of work */
} JET_SNPROG;

typedef struct
{
    uint32_t           cbStruct;

    uint32_t           cbFilesizeLow;          //  file's current size (low ulong)
    uint32_t           cbFilesizeHigh;         //  file's current size (high ulong)

    uint32_t           cbFreeSpaceRequiredLow; //  estimate of free disk space required for in-place upgrade (low ulong)
    uint32_t           cbFreeSpaceRequiredHigh;//  estimate of free disk space required for in-place upgrade (high ulong)

    uint32_t           csecToUpgrade;          //  estimate of time required, in seconds, for upgrade

    union
    {
        uint32_t       ulFlags;
        struct
        {
            uint32_t   fUpgradable:1;
            uint32_t   fAlreadyUpgraded:1;
        };
    };
} JET_DBINFOUPGRADE;

typedef struct
{
    uint32_t       cbStruct;
    JET_OBJTYP          objtyp;
    JET_DATESERIAL      dtCreate;   //  Deprecated.
    JET_DATESERIAL      dtUpdate;   //  Deprecated.
    JET_GRBIT           grbit;
    uint32_t       flags;
    uint32_t       cRecord;
    uint32_t       cPage;
} JET_OBJECTINFO;

    /* The following flags appear in the grbit field above */

#define JET_bitTableInfoUpdatable   0x00000001
#define JET_bitTableInfoBookmark    0x00000002
#define JET_bitTableInfoRollback    0x00000004

    /* The following flags occur in the flags field above */

#define JET_bitObjectSystem         0x80000000  // Internal use only
#define JET_bitObjectTableFixedDDL  0x40000000  // Table's DDL is fixed
#define JET_bitObjectTableTemplate  0x20000000  // Table's DDL is inheritable (implies FixedDDL)
#define JET_bitObjectTableDerived   0x10000000  // Table's DDL is inherited from a template table
#define JET_bitObjectSystemDynamic  (JET_bitObjectSystem|0x08000000)    //  Internal use only (dynamic system objects)
#if ( JET_VERSION >= 0x0501 )
#define JET_bitObjectTableNoFixedVarColumnsInDerivedTables  0x04000000  //  used in conjunction with JET_bitObjectTableTemplate
                                                                        //    to disallow fixed/var columns in derived tables (so that
                                                                        //    fixed/var columns may be added to the template in the future)
#endif // JET_VERSION >= 0x0501

typedef struct
{
    uint32_t   cbStruct;
    JET_TABLEID     tableid;
    uint32_t   cRecord;
    JET_COLUMNID    columnidcontainername;
    JET_COLUMNID    columnidobjectname;
    JET_COLUMNID    columnidobjtyp;
    JET_COLUMNID    columniddtCreate;   //  XXX -- to be deleted
    JET_COLUMNID    columniddtUpdate;   //  XXX -- to be deleted
    JET_COLUMNID    columnidgrbit;
    JET_COLUMNID    columnidflags;
    JET_COLUMNID    columnidcRecord;    /* Level 2 info */
    JET_COLUMNID    columnidcPage;      /* Level 2 info */
} JET_OBJECTLIST;

#define cObjectInfoCols 9

typedef struct
{
    uint32_t   cbStruct;
    JET_TABLEID     tableid;
    uint32_t   cRecord;
    JET_COLUMNID    columnidPresentationOrder;
    JET_COLUMNID    columnidcolumnname;
    JET_COLUMNID    columnidcolumnid;
    JET_COLUMNID    columnidcoltyp;
    JET_COLUMNID    columnidCountry;        // specifies the columnid for the country/region field
    JET_COLUMNID    columnidLangid;
    JET_COLUMNID    columnidCp;
    JET_COLUMNID    columnidCollate;
    JET_COLUMNID    columnidcbMax;
    JET_COLUMNID    columnidgrbit;
    JET_COLUMNID    columnidDefault;
    JET_COLUMNID    columnidBaseTableName;
    JET_COLUMNID    columnidBaseColumnName;
    JET_COLUMNID    columnidDefinitionName;
} JET_COLUMNLIST;

#define cColumnInfoCols 14

typedef struct
{
    uint32_t   cbStruct;
    JET_COLUMNID    columnid;
    JET_COLTYP      coltyp;
    unsigned short  wCountry;           // sepcifies the country/region for the column definition
    unsigned short  langid;
    unsigned short  cp;
    unsigned short  wCollate;       /* Must be 0 */
    uint32_t   cbMax;
    JET_GRBIT       grbit;
} JET_COLUMNDEF;

typedef struct
{
    uint32_t   cbStruct;
    JET_COLUMNID    columnid;
    JET_COLTYP      coltyp;
    unsigned short  wCountry;           // specifies the columnid for the country/region field
    unsigned short  langid;
    unsigned short  cp;
    unsigned short  wFiller;       /* Must be 0 */
    uint32_t   cbMax;
    JET_GRBIT       grbit;
    char            szBaseTableName[256];
    char            szBaseColumnName[256];
} JET_COLUMNBASE_A;

typedef struct
{
    uint32_t   cbStruct;
    JET_COLUMNID    columnid;
    JET_COLTYP      coltyp;
    unsigned short  wCountry;           // specifies the columnid for the country/region field
    unsigned short  langid;
    unsigned short  cp;
    unsigned short  wFiller;       /* Must be 0 */
    uint32_t   cbMax;
    JET_GRBIT       grbit;
    char16_t           szBaseTableName[256];
    char16_t           szBaseColumnName[256];
} JET_COLUMNBASE_W;

#ifdef JET_UNICODE
typedef JET_COLUMNBASE_W JET_COLUMNBASE;
#else
typedef JET_COLUMNBASE_A JET_COLUMNBASE;
#endif

typedef struct
{
    uint32_t   cbStruct;
    JET_TABLEID     tableid;
    uint32_t   cRecord;
    JET_COLUMNID    columnidindexname;
    JET_COLUMNID    columnidgrbitIndex;
    JET_COLUMNID    columnidcKey;
    JET_COLUMNID    columnidcEntry;
    JET_COLUMNID    columnidcPage;
    JET_COLUMNID    columnidcColumn;
    JET_COLUMNID    columnidiColumn;
    JET_COLUMNID    columnidcolumnid;
    JET_COLUMNID    columnidcoltyp;
    JET_COLUMNID    columnidCountry;        // specifies the columnid for the country/region field
    JET_COLUMNID    columnidLangid;
    JET_COLUMNID    columnidCp;
    JET_COLUMNID    columnidCollate;
    JET_COLUMNID    columnidgrbitColumn;
    JET_COLUMNID    columnidcolumnname;
    JET_COLUMNID    columnidLCMapFlags;
} JET_INDEXLIST;

#define cIndexInfoCols 15

typedef struct tag_JET_COLUMNCREATE_A
{
    uint32_t   cbStruct;               // size of this structure (for future expansion)
    char            *szColumnName;          // column name
    JET_COLTYP      coltyp;                 // column type
    uint32_t   cbMax;                  // the maximum length of this column (only relevant for binary and text columns)
    JET_GRBIT       grbit;                  // column options
    void            *pvDefault;             // default value (NULL if none)
    uint32_t   cbDefault;              // length of default value
    uint32_t   cp;                     // code page (for text columns only)
    JET_COLUMNID    columnid;               // returned column id
    JET_ERR         err;                    // returned error code
} JET_COLUMNCREATE_A;

typedef struct tag_JET_COLUMNCREATE_W
{
    uint32_t   cbStruct;               // size of this structure (for future expansion)
    char16_t           *szColumnName;          // column name
    JET_COLTYP      coltyp;                 // column type
    uint32_t   cbMax;                  // the maximum length of this column (only relevant for binary and text columns)
    JET_GRBIT       grbit;                  // column options
    void            *pvDefault;             // default value (NULL if none)
    uint32_t   cbDefault;              // length of default value
    uint32_t   cp;                     // code page (for text columns only)
    JET_COLUMNID    columnid;               // returned column id
    JET_ERR         err;                    // returned error code
} JET_COLUMNCREATE_W;

#ifdef JET_UNICODE
typedef JET_COLUMNCREATE_W JET_COLUMNCREATE;
#else
typedef JET_COLUMNCREATE_A JET_COLUMNCREATE;
#endif

#if ( JET_VERSION >= 0x0501 )
//  This is the information needed to create a column with a user-defined default. It should be passed in using
//  the pvDefault and cbDefault in a JET_COLUMNCREATE structure

typedef struct tag_JET_USERDEFINEDDEFAULT_A
{
    char * szCallback;
    unsigned char * pbUserData;
    uint32_t cbUserData;
    char * szDependantColumns;
} JET_USERDEFINEDDEFAULT_A;

typedef struct tag_JET_USERDEFINEDDEFAULT_W
{
    char16_t * szCallback;
    unsigned char * pbUserData;
    uint32_t cbUserData;
    char16_t * szDependantColumns;
} JET_USERDEFINEDDEFAULT_W;

#ifdef JET_UNICODE
typedef JET_USERDEFINEDDEFAULT_W JET_USERDEFINEDDEFAULT;
#else
typedef JET_USERDEFINEDDEFAULT_A JET_USERDEFINEDDEFAULT;
#endif

#endif // JET_VERSION >= 0x0501

typedef struct tagJET_CONDITIONALCOLUMN_A
{
    uint32_t   cbStruct;               // size of this structure (for future expansion)
    char            *szColumnName;          // column that we are conditionally indexed on
    JET_GRBIT       grbit;                  // conditional column options
} JET_CONDITIONALCOLUMN_A;

typedef struct tagJET_CONDITIONALCOLUMN_W
{
    uint32_t   cbStruct;               // size of this structure (for future expansion)
    char16_t           *szColumnName;          // column that we are conditionally indexed on
    JET_GRBIT       grbit;                  // conditional column options
} JET_CONDITIONALCOLUMN_W;

#ifdef JET_UNICODE
typedef JET_CONDITIONALCOLUMN_W JET_CONDITIONALCOLUMN;
#else
typedef JET_CONDITIONALCOLUMN_A JET_CONDITIONALCOLUMN;
#endif

typedef struct tagJET_UNICODEINDEX
{
    uint32_t   lcid;
    uint32_t   dwMapFlags;
} JET_UNICODEINDEX;

#if ( JET_VERSION >= 0x0602 )
typedef struct tagJET_UNICODEINDEX2
{
    char16_t         *szLocaleName;
    uint32_t   dwMapFlags;
} JET_UNICODEINDEX2;
#endif //JET_VERSION >= 0x0602

#if ( JET_VERSION >= 0x0502 )
typedef struct tagJET_TUPLELIMITS
{
    uint32_t   chLengthMin;
    uint32_t   chLengthMax;
    uint32_t   chToIndexMax;
#if ( JET_VERSION >= 0x0600 )
    uint32_t   cchIncrement;
    uint32_t   ichStart;
#endif // JET_VERSION >= 0x0600
} JET_TUPLELIMITS;
#endif // JET_VERSION >= 0x0502

#if ( JET_VERSION >= 0x0601 )
//  This structure describes some of the hints we can give to a given B-tree, be it a
//  table, index, or the internal long values tree.
typedef struct tagJET_SPACEHINTS
{
    uint32_t       cbStruct;           //  size of this structure
    uint32_t       ulInitialDensity;   //  density at (append) layout.
    uint32_t       cbInitial;          //  initial size (in bytes).

    JET_GRBIT           grbit;              //  Combination of one or more flags from
                                            //      JET_bitSpaceHints* flags
                                            //      JET_bitCreateHints* flags
                                            //      JET_bitRetrieveHints* flags
                                            //      JET_bitUpdateHints* flags
                                            //      JET_bitDeleteHints* flags
    uint32_t       ulMaintDensity;     //  density to maintain at.
    uint32_t       ulGrowth;           //  percent growth from:
                                            //    last growth or initial size (possibly rounded to nearest native JET allocation size).
    uint32_t       cbMinExtent;        //  This overrides ulGrowth if too small.
    uint32_t       cbMaxExtent;        //  This caps ulGrowth.
} JET_SPACEHINTS;
#endif // JET_VERSION >= 0x0601
// Needed to detect if the older JET_INDEXCREATE structure
// was used (backward compatibility).
typedef struct tagJET_INDEXCREATEOLD_A
{
    uint32_t           cbStruct;               // size of this structure (for future expansion)
    char                    *szIndexName;           // index name
    char                    *szKey;                 // index key definition
    uint32_t           cbKey;                  // size of key definition in szKey
    JET_GRBIT               grbit;                  // index options
    uint32_t           ulDensity;              // index density

    union
    {
        uint32_t       lcid;                   // lcid for the index (if JET_bitIndexUnicode NOT specified)
        JET_UNICODEINDEX    *pidxunicode;           // pointer to JET_UNICODEINDEX struct (if JET_bitIndexUnicode specified)
    };

    union
    {
        uint32_t       cbVarSegMac;            // maximum length of variable length columns in index key (if JET_bitIndexTupleLimits not specified)
#if ( JET_VERSION >= 0x0502 )
        JET_TUPLELIMITS     *ptuplelimits;          // pointer to JET_TUPLELIMITS struct (if JET_bitIndexTupleLimits specified)
#endif // ! JET_VERSION >= 0x0502
    };

    JET_CONDITIONALCOLUMN_A *rgconditionalcolumn;   // pointer to conditional column structure
    uint32_t           cConditionalColumn;     // number of conditional columns
    JET_ERR                 err;                    // returned error code
} JET_INDEXCREATEOLD_A;

typedef struct tagJET_INDEXCREATEOLD_W
{
    uint32_t           cbStruct;               // size of this structure (for future expansion)
    char16_t                   *szIndexName;           // index name
    char16_t                   *szKey;                 // index key definition
    uint32_t           cbKey;                  // size of key definition in szKey
    JET_GRBIT               grbit;                  // index options
    uint32_t           ulDensity;              // index density

    union
    {
        uint32_t       lcid;                   // lcid for the index (if JET_bitIndexUnicode NOT specified)
        JET_UNICODEINDEX    *pidxunicode;           // pointer to JET_UNICODEINDEX struct (if JET_bitIndexUnicode specified)
    };

    union
    {
        uint32_t       cbVarSegMac;            // maximum length of variable length columns in index key (if JET_bitIndexTupleLimits not specified)
#if ( JET_VERSION >= 0x0502 )
        JET_TUPLELIMITS     *ptuplelimits;          // pointer to JET_TUPLELIMITS struct (if JET_bitIndexTupleLimits specified)
#endif // ! JET_VERSION >= 0x0502
    };

    JET_CONDITIONALCOLUMN_W *rgconditionalcolumn;   // pointer to conditional column structure
    uint32_t           cConditionalColumn;     // number of conditional columns
    JET_ERR                 err;                    // returned error code
} JET_INDEXCREATEOLD_W;

#ifdef JET_UNICODE
typedef JET_INDEXCREATEOLD_W JET_INDEXCREATEOLD;
#else
typedef JET_INDEXCREATEOLD_A JET_INDEXCREATEOLD;
#endif
typedef struct tagJET_INDEXCREATE_A
{
    uint32_t           cbStruct;               // size of this structure (for future expansion)
    char                    *szIndexName;           // index name
    char                    *szKey;                 // index key definition
    uint32_t           cbKey;                  // size of key definition in szKey
    JET_GRBIT               grbit;                  // index options
    uint32_t           ulDensity;              // index density

    union
    {
        uint32_t       lcid;                   // lcid for the index (if JET_bitIndexUnicode NOT specified)
        JET_UNICODEINDEX    *pidxunicode;           // pointer to JET_UNICODEINDEX struct (if JET_bitIndexUnicode specified)
    };

    union
    {
        uint32_t       cbVarSegMac;            // maximum length of variable length columns in index key (if JET_bitIndexTupleLimits not specified)
#if ( JET_VERSION >= 0x0502 )
        JET_TUPLELIMITS     *ptuplelimits;          // pointer to JET_TUPLELIMITS struct (if JET_bitIndexTupleLimits specified)
#endif // ! JET_VERSION >= 0x0502
    };

    JET_CONDITIONALCOLUMN_A *rgconditionalcolumn;   // pointer to conditional column structure
    uint32_t           cConditionalColumn;     // number of conditional columns
    JET_ERR                 err;                    // returned error code
#if ( JET_VERSION >= 0x0600 )
    uint32_t           cbKeyMost;              // size of key preserved in index, e.g. without truncation (if JET_bitIndexKeyMost specified)
#endif // JET_VERSION >= 0x0600
} JET_INDEXCREATE_A;

typedef struct tagJET_INDEXCREATE_W
{
    uint32_t           cbStruct;               // size of this structure (for future expansion)
    char16_t                   *szIndexName;           // index name
    char16_t                   *szKey;                 // index key definition
    uint32_t           cbKey;                  // size of key definition in szKey
    JET_GRBIT               grbit;                  // index options
    uint32_t           ulDensity;              // index density

    union
    {
        uint32_t       lcid;                   // lcid for the index (if JET_bitIndexUnicode NOT specified)
        JET_UNICODEINDEX    *pidxunicode;           // pointer to JET_UNICODEINDEX struct (if JET_bitIndexUnicode specified)
    };

    union
    {
        uint32_t       cbVarSegMac;            // maximum length of variable length columns in index key (if JET_bitIndexTupleLimits not specified)
#if ( JET_VERSION >= 0x0502 )
        JET_TUPLELIMITS     *ptuplelimits;          // pointer to JET_TUPLELIMITS struct (if JET_bitIndexTupleLimits specified)
#endif // ! JET_VERSION >= 0x0502
    };

    JET_CONDITIONALCOLUMN_W *rgconditionalcolumn;   // pointer to conditional column structure
    uint32_t           cConditionalColumn;     // number of conditional columns
    JET_ERR                 err;                    // returned error code
#if ( JET_VERSION >= 0x0600 )
    uint32_t           cbKeyMost;              // size of key preserved in index, e.g. without truncation (if JET_bitIndexKeyMost specified)
#endif // JET_VERSION >= 0x0600
} JET_INDEXCREATE_W;

#ifdef JET_UNICODE
typedef JET_INDEXCREATE_W JET_INDEXCREATE;
#else
typedef JET_INDEXCREATE_A JET_INDEXCREATE;
#endif

#if ( JET_VERSION >= 0x0601 )

typedef struct tagJET_INDEXCREATE2_A
{
    uint32_t           cbStruct;               // size of this structure (for future expansion)
    char                    *szIndexName;           // index name
    char                    *szKey;                 // index key definition
    uint32_t           cbKey;                  // size of key definition in szKey
    JET_GRBIT               grbit;                  // index options
    uint32_t           ulDensity;              // index density

    union
    {
        uint32_t       lcid;                   // lcid for the index (if JET_bitIndexUnicode NOT specified)
        JET_UNICODEINDEX    *pidxunicode;           // pointer to JET_UNICODEINDEX struct (if JET_bitIndexUnicode specified)
    };

    union
    {
        uint32_t       cbVarSegMac;            // maximum length of variable length columns in index key (if JET_bitIndexTupleLimits not specified)
        JET_TUPLELIMITS     *ptuplelimits;          // pointer to JET_TUPLELIMITS struct (if JET_bitIndexTupleLimits specified)
    };

    JET_CONDITIONALCOLUMN_A *rgconditionalcolumn;   // pointer to conditional column structure
    uint32_t           cConditionalColumn;     // number of conditional columns
    JET_ERR                 err;                    // returned error code
    uint32_t           cbKeyMost;              // size of key preserved in index, e.g. without truncation (if JET_bitIndexKeyMost specified)
    JET_SPACEHINTS *        pSpacehints;            // space allocation, maintenance, and usage hints
} JET_INDEXCREATE2_A;

typedef struct tagJET_INDEXCREATE2_W
{
    uint32_t           cbStruct;               // size of this structure (for future expansion)
    char16_t                   *szIndexName;           // index name
    char16_t                   *szKey;                 // index key definition
    uint32_t           cbKey;                  // size of key definition in szKey
    JET_GRBIT               grbit;                  // index options
    uint32_t           ulDensity;              // index density

    union
    {
        uint32_t       lcid;                   // lcid for the index (if JET_bitIndexUnicode NOT specified)
        JET_UNICODEINDEX    *pidxunicode;           // pointer to JET_UNICODEINDEX struct (if JET_bitIndexUnicode specified)
    };

    union
    {
        uint32_t       cbVarSegMac;            // maximum length of variable length columns in index key (if JET_bitIndexTupleLimits not specified)
        JET_TUPLELIMITS     *ptuplelimits;          // pointer to JET_TUPLELIMITS struct (if JET_bitIndexTupleLimits specified)
    };

    JET_CONDITIONALCOLUMN_W *rgconditionalcolumn;   // pointer to conditional column structure
    uint32_t           cConditionalColumn;     // number of conditional columns
    JET_ERR                 err;                    // returned error code
    uint32_t           cbKeyMost;              // size of key preserved in index, e.g. without truncation (if JET_bitIndexKeyMost specified)
    JET_SPACEHINTS *        pSpacehints;            // space allocation, maintenance, and usage hints
} JET_INDEXCREATE2_W;

#ifdef JET_UNICODE
typedef JET_INDEXCREATE2_W JET_INDEXCREATE2;
#else
typedef JET_INDEXCREATE2_A JET_INDEXCREATE2;
#endif
#endif // JET_VERSION >= 0x0601

#if ( JET_VERSION >= 0x0602 )

typedef struct tagJET_INDEXCREATE3_A
{
    uint32_t           cbStruct;               // size of this structure (for future expansion)
    char                    *szIndexName;           // index name
    char                    *szKey;                 // index key definition
    uint32_t           cbKey;                  // size of key definition in szKey
    JET_GRBIT               grbit;                  // index options
    uint32_t           ulDensity;              // index density
    JET_UNICODEINDEX2       *pidxunicode;           // pointer to JET_UNICODEINDEX2 struct (if JET_bitIndexUnicode specified)

    union
    {
        uint32_t       cbVarSegMac;            // maximum length of variable length columns in index key (if JET_bitIndexTupleLimits not specified)
        JET_TUPLELIMITS     *ptuplelimits;          // pointer to JET_TUPLELIMITS struct (if JET_bitIndexTupleLimits specified)
    };

    JET_CONDITIONALCOLUMN_A *rgconditionalcolumn;   // pointer to conditional column structure
    uint32_t           cConditionalColumn;     // number of conditional columns
    JET_ERR                 err;                    // returned error code
    uint32_t           cbKeyMost;              // size of key preserved in index, e.g. without truncation (if JET_bitIndexKeyMost specified)
    JET_SPACEHINTS *        pSpacehints;            // space allocation, maintenance, and usage hints
} JET_INDEXCREATE3_A;

typedef struct tagJET_INDEXCREATE3_W
{
    uint32_t           cbStruct;               // size of this structure (for future expansion)
    char16_t                   *szIndexName;           // index name
    char16_t                   *szKey;                 // index key definition
    uint32_t           cbKey;                  // size of key definition in szKey
    JET_GRBIT               grbit;                  // index options
    uint32_t           ulDensity;              // index density
    JET_UNICODEINDEX2       *pidxunicode;           // pointer to JET_UNICODEINDEX2 struct (if JET_bitIndexUnicode specified)

    union
    {
        uint32_t       cbVarSegMac;            // maximum length of variable length columns in index key (if JET_bitIndexTupleLimits not specified)
        JET_TUPLELIMITS     *ptuplelimits;          // pointer to JET_TUPLELIMITS struct (if JET_bitIndexTupleLimits specified)
    };

    JET_CONDITIONALCOLUMN_W *rgconditionalcolumn;   // pointer to conditional column structure
    uint32_t           cConditionalColumn;     // number of conditional columns
    JET_ERR                 err;                    // returned error code
    uint32_t           cbKeyMost;              // size of key preserved in index, e.g. without truncation (if JET_bitIndexKeyMost specified)
    JET_SPACEHINTS *        pSpacehints;            // space allocation, maintenance, and usage hints
} JET_INDEXCREATE3_W;

#ifdef JET_UNICODE
typedef JET_INDEXCREATE3_W JET_INDEXCREATE3;
#else
typedef JET_INDEXCREATE3_A JET_INDEXCREATE3;
#endif
#endif // JET_VERSION >= 0x0602

//
//      Table Creation Structures
//

typedef struct tagJET_TABLECREATE_A
{
    uint32_t       cbStruct;               // size of this structure (for future expansion)
    char                *szTableName;           // name of table to create.
    char                *szTemplateTableName;   // name of table from which to inherit base DDL
    uint32_t       ulPages;                // initial pages to allocate for table.
    uint32_t       ulDensity;              // table density.
    JET_COLUMNCREATE_A  *rgcolumncreate;        // array of column creation info
    uint32_t       cColumns;               // number of columns to create
    JET_INDEXCREATE_A       *rgindexcreate;         // array of index creation info
    uint32_t       cIndexes;               // number of indexes to create
    JET_GRBIT           grbit;
    JET_TABLEID         tableid;                // returned tableid.
    uint32_t       cCreated;               // count of objects created (columns+table+indexes).
} JET_TABLECREATE_A;

typedef struct tagJET_TABLECREATE_W
{
    uint32_t       cbStruct;               // size of this structure (for future expansion)
    char16_t               *szTableName;           // name of table to create.
    char16_t               *szTemplateTableName;   // name of table from which to inherit base DDL
    uint32_t       ulPages;                // initial pages to allocate for table.
    uint32_t       ulDensity;              // table density.
    JET_COLUMNCREATE_W  *rgcolumncreate;        // array of column creation info
    uint32_t       cColumns;               // number of columns to create
    JET_INDEXCREATE_W       *rgindexcreate;         // array of index creation info
    uint32_t       cIndexes;               // number of indexes to create
    JET_GRBIT           grbit;
    JET_TABLEID         tableid;                // returned tableid.
    uint32_t       cCreated;               // count of objects created (columns+table+indexes).
} JET_TABLECREATE_W;

#ifdef JET_UNICODE
typedef JET_TABLECREATE_W JET_TABLECREATE;
#else
typedef JET_TABLECREATE_A JET_TABLECREATE;
#endif

#if ( JET_VERSION >= 0x0501 )
typedef struct tagJET_TABLECREATE2_A
{
    uint32_t       cbStruct;               // size of this structure (for future expansion)
    char                *szTableName;           // name of table to create.
    char                *szTemplateTableName;   // name of table from which to inherit base DDL
    uint32_t       ulPages;                // initial pages to allocate for table.
    uint32_t       ulDensity;              // table density.
    JET_COLUMNCREATE_A  *rgcolumncreate;        // array of column creation info
    uint32_t       cColumns;               // number of columns to create
    JET_INDEXCREATE_A   *rgindexcreate;         // array of index creation info
    uint32_t       cIndexes;               // number of indexes to create
    char                *szCallback;            // callback to use for this table
    JET_CBTYP           cbtyp;                  // when the callback should be called
    JET_GRBIT           grbit;
    JET_TABLEID         tableid;                // returned tableid.
    uint32_t       cCreated;               // count of objects created (columns+table+indexes+callbacks).
} JET_TABLECREATE2_A;

typedef struct tagJET_TABLECREATE2_W
{
    uint32_t       cbStruct;               // size of this structure (for future expansion)
    char16_t               *szTableName;           // name of table to create.
    char16_t               *szTemplateTableName;   // name of table from which to inherit base DDL
    uint32_t       ulPages;                // initial pages to allocate for table.
    uint32_t       ulDensity;              // table density.
    JET_COLUMNCREATE_W  *rgcolumncreate;        // array of column creation info
    uint32_t       cColumns;               // number of columns to create
    JET_INDEXCREATE_W   *rgindexcreate;         // array of index creation info
    uint32_t       cIndexes;               // number of indexes to create
    char16_t               *szCallback;            // callback to use for this table
    JET_CBTYP           cbtyp;                  // when the callback should be called
    JET_GRBIT           grbit;
    JET_TABLEID         tableid;                // returned tableid.
    uint32_t       cCreated;               // count of objects created (columns+table+indexes+callbacks).
} JET_TABLECREATE2_W;

#ifdef JET_UNICODE
typedef JET_TABLECREATE2_W JET_TABLECREATE2;
#else
typedef JET_TABLECREATE2_A JET_TABLECREATE2;
#endif

#endif // JET_VERSION >= 0x0501

#if ( JET_VERSION >= 0x0601 )
typedef struct tagJET_TABLECREATE3_A
{
    uint32_t       cbStruct;               // size of this structure (for future expansion)
    char                *szTableName;           // name of table to create.
    char                *szTemplateTableName;   // name of table from which to inherit base DDL
    uint32_t       ulPages;                // initial pages to allocate for table.
    uint32_t       ulDensity;              // table density.
    JET_COLUMNCREATE_A  *rgcolumncreate;        // array of column creation info
    uint32_t       cColumns;               // number of columns to create
    JET_INDEXCREATE2_A  *rgindexcreate;         // array of index creation info
    uint32_t       cIndexes;               // number of indexes to create
    char                *szCallback;            // callback to use for this table
    JET_CBTYP           cbtyp;                  // when the callback should be called
    JET_GRBIT           grbit;
    JET_SPACEHINTS *    pSeqSpacehints;         // space allocation, maintenance, and usage hints for default sequential index
    JET_SPACEHINTS *    pLVSpacehints;          // space allocation, maintenance, and usage hints for Separated LV tree.
    uint32_t       cbSeparateLV;           // heuristic size to separate a intrinsic LV from the primary record

    JET_TABLEID         tableid;                // returned tableid.
    uint32_t       cCreated;               // count of objects created (columns+table+indexes+callbacks).
} JET_TABLECREATE3_A;

typedef struct tagJET_TABLECREATE3_W
{
    uint32_t       cbStruct;               // size of this structure (for future expansion)
    char16_t               *szTableName;           // name of table to create.
    char16_t               *szTemplateTableName;   // name of table from which to inherit base DDL
    uint32_t       ulPages;                // initial pages to allocate for table.
    uint32_t       ulDensity;              // table density.
    JET_COLUMNCREATE_W  *rgcolumncreate;        // array of column creation info
    uint32_t       cColumns;               // number of columns to create
    JET_INDEXCREATE2_W  *rgindexcreate;         // array of index creation info
    uint32_t       cIndexes;               // number of indexes to create
    char16_t               *szCallback;            // callback to use for this table
    JET_CBTYP           cbtyp;                  // when the callback should be called
    JET_GRBIT           grbit;
    JET_SPACEHINTS *    pSeqSpacehints;         // space allocation, maintenance, and usage hints for default sequential index
    JET_SPACEHINTS *    pLVSpacehints;          // space allocation, maintenance, and usage hints for Separated LV tree.
    uint32_t       cbSeparateLV;           // heuristic size to separate a intrinsic LV from the primary record
    JET_TABLEID         tableid;                // returned tableid.
    uint32_t       cCreated;               // count of objects created (columns+table+indexes+callbacks).
} JET_TABLECREATE3_W;

#ifdef JET_UNICODE
typedef JET_TABLECREATE3_W JET_TABLECREATE3;
#else
typedef JET_TABLECREATE3_A JET_TABLECREATE3;
#endif

#endif // JET_VERSION >= 0x0601

#if ( JET_VERSION >= 0x0602 )
typedef struct tagJET_TABLECREATE4_A
{
    uint32_t       cbStruct;               // size of this structure (for future expansion)
    char                *szTableName;           // name of table to create.
    char                *szTemplateTableName;   // name of table from which to inherit base DDL
    uint32_t       ulPages;                // initial pages to allocate for table.
    uint32_t       ulDensity;              // table density.
    JET_COLUMNCREATE_A  *rgcolumncreate;        // array of column creation info
    uint32_t       cColumns;               // number of columns to create
    JET_INDEXCREATE3_A  *rgindexcreate;         // array of index creation info
    uint32_t       cIndexes;               // number of indexes to create
    char                *szCallback;            // callback to use for this table
    JET_CBTYP           cbtyp;                  // when the callback should be called
    JET_GRBIT           grbit;
    JET_SPACEHINTS *    pSeqSpacehints;         // space allocation, maintenance, and usage hints for default sequential index
    JET_SPACEHINTS *    pLVSpacehints;          // space allocation, maintenance, and usage hints for Separated LV tree.
    uint32_t       cbSeparateLV;           // heuristic size to separate a intrinsic LV from the primary record

    JET_TABLEID         tableid;                // returned tableid.
    uint32_t       cCreated;               // count of objects created (columns+table+indexes+callbacks).
} JET_TABLECREATE4_A;

typedef struct tagJET_TABLECREATE4_W
{
    uint32_t       cbStruct;               // size of this structure (for future expansion)
    char16_t               *szTableName;           // name of table to create.
    char16_t               *szTemplateTableName;   // name of table from which to inherit base DDL
    uint32_t       ulPages;                // initial pages to allocate for table.
    uint32_t       ulDensity;              // table density.
    JET_COLUMNCREATE_W  *rgcolumncreate;        // array of column creation info
    uint32_t       cColumns;               // number of columns to create
    JET_INDEXCREATE3_W  *rgindexcreate;         // array of index creation info
    uint32_t       cIndexes;               // number of indexes to create
    char16_t               *szCallback;            // callback to use for this table
    JET_CBTYP           cbtyp;                  // when the callback should be called
    JET_GRBIT           grbit;
    JET_SPACEHINTS *    pSeqSpacehints;         // space allocation, maintenance, and usage hints for default sequential index
    JET_SPACEHINTS *    pLVSpacehints;          // space allocation, maintenance, and usage hints for Separated LV tree.
    uint32_t       cbSeparateLV;           // heuristic size to separate a intrinsic LV from the primary record

    JET_TABLEID         tableid;                // returned tableid.
    uint32_t       cCreated;               // count of objects created (columns+table+indexes+callbacks).
} JET_TABLECREATE4_W;

#ifdef JET_UNICODE
typedef JET_TABLECREATE4_W JET_TABLECREATE4;
#else
typedef JET_TABLECREATE4_A JET_TABLECREATE4;
#endif

#endif // JET_VERSION >= 0x0602
#if ( JET_VERSION >= 0x0A01 )
typedef struct tagJET_TABLECREATE5_A
{
    uint32_t       cbStruct;               // size of this structure (for future expansion)
    char                *szTableName;           // name of table to create.
    char                *szTemplateTableName;   // name of table from which to inherit base DDL
    uint32_t       ulPages;                // initial pages to allocate for table.
    uint32_t       ulDensity;              // table density.
    JET_COLUMNCREATE_A  *rgcolumncreate;        // array of column creation info
    uint32_t       cColumns;               // number of columns to create
    JET_INDEXCREATE3_A  *rgindexcreate;         // array of index creation info
    uint32_t       cIndexes;               // number of indexes to create
    char                *szCallback;            // callback to use for this table
    JET_CBTYP           cbtyp;                  // when the callback should be called
    JET_GRBIT           grbit;
    JET_SPACEHINTS *    pSeqSpacehints;         // space allocation, maintenance, and usage hints for default sequential index
    JET_SPACEHINTS *    pLVSpacehints;          // space allocation, maintenance, and usage hints for Separated LV tree.
    uint32_t       cbSeparateLV;           // heuristic size to separate a intrinsic LV from the primary record
    uint32_t       cbLVChunkMax;           // Maximum chunk size to use for Separated LVs

    JET_TABLEID         tableid;                // returned tableid.
    uint32_t       cCreated;               // count of objects created (columns+table+indexes+callbacks).
} JET_TABLECREATE5_A;

typedef struct tagJET_TABLECREATE5_W
{
    uint32_t       cbStruct;               // size of this structure (for future expansion)
    char16_t               *szTableName;           // name of table to create.
    char16_t               *szTemplateTableName;   // name of table from which to inherit base DDL
    uint32_t       ulPages;                // initial pages to allocate for table.
    uint32_t       ulDensity;              // table density.
    JET_COLUMNCREATE_W  *rgcolumncreate;        // array of column creation info
    uint32_t       cColumns;               // number of columns to create
    JET_INDEXCREATE3_W  *rgindexcreate;         // array of index creation info
    uint32_t       cIndexes;               // number of indexes to create
    char16_t               *szCallback;            // callback to use for this table
    JET_CBTYP           cbtyp;                  // when the callback should be called
    JET_GRBIT           grbit;
    JET_SPACEHINTS *    pSeqSpacehints;         // space allocation, maintenance, and usage hints for default sequential index
    JET_SPACEHINTS *    pLVSpacehints;          // space allocation, maintenance, and usage hints for Separated LV tree.
    uint32_t       cbSeparateLV;           // heuristic size to separate a intrinsic LV from the primary record
    uint32_t       cbLVChunkMax;           // Maximum chunk size to use for Separated LVs

    JET_TABLEID         tableid;                // returned tableid.
    uint32_t       cCreated;               // count of objects created (columns+table+indexes+callbacks).
} JET_TABLECREATE5_W;

#ifdef JET_UNICODE
typedef JET_TABLECREATE5_W JET_TABLECREATE5;
#else
typedef JET_TABLECREATE5_A JET_TABLECREATE5;
#endif

#endif // JET_VERSION >= 0x0A01
#if ( JET_VERSION >= 0x0600 )
typedef struct tagJET_OPENTEMPORARYTABLE
{
    uint32_t       cbStruct;               // size of this structure (for future expansion)
    const JET_COLUMNDEF *prgcolumndef;
    uint32_t       ccolumn;
    JET_UNICODEINDEX    *pidxunicode;
    JET_GRBIT           grbit;
    JET_COLUMNID        *prgcolumnid;
    uint32_t       cbKeyMost;
    uint32_t       cbVarSegMac;
    JET_TABLEID         tableid;
} JET_OPENTEMPORARYTABLE;
#endif // JET_VERSION >= 0x0600

#if ( JET_VERSION >= 0x0602 )
typedef struct tagJET_OPENTEMPORARYTABLE2
{
    uint32_t       cbStruct;               // size of this structure (for future expansion)
    const JET_COLUMNDEF *prgcolumndef;
    uint32_t       ccolumn;
    JET_UNICODEINDEX2   *pidxunicode;
    JET_GRBIT           grbit;
    JET_COLUMNID        *prgcolumnid;
    uint32_t       cbKeyMost;
    uint32_t       cbVarSegMac;
    JET_TABLEID         tableid;
} JET_OPENTEMPORARYTABLE2;
#endif // JET_VERSION >= 0x0602

typedef struct
{
    uint32_t   cbStruct;
    uint32_t   ibLongValue;
    uint32_t   itagSequence;
    JET_COLUMNID    columnidNextTagged;
} JET_RETINFO;

typedef struct
{
    uint32_t   cbStruct;
    uint32_t   ibLongValue;
    uint32_t   itagSequence;
} JET_SETINFO;

typedef struct
{
    uint32_t   cbStruct;
    uint32_t   centriesLT;
    uint32_t   centriesInRange;
    uint32_t   centriesTotal;
} JET_RECPOS;

// On input to JetGotoPosition, centriesLTDeprecated and centriesTotalDeprecated must be 0.
// On output from JetGetRecordPositon, centriesLTDeprecated and centriesTotalDeprecated
// hold potentially truncated versions of centriesLT and centriesTotal.
typedef struct
{
    uint32_t        cbStruct;
    uint32_t        centriesLTDeprecated;
    uint32_t        centriesInRangeDeprecated;
    uint32_t        centriesTotalDeprecated;
    uint64_t   centriesLT;
    uint64_t   centriesTotal;
} JET_RECPOS2;

typedef struct
{
    uint32_t   cbStruct;
    JET_TABLEID     tableid;
    uint32_t   cRecord;
    JET_COLUMNID    columnidBookmark;
} JET_RECORDLIST;

typedef struct
{
    uint32_t   cbStruct;
    JET_TABLEID     tableid;
    JET_GRBIT       grbit;
} JET_INDEXRANGE;

#if ( JET_VERSION >= 0x0602 )
typedef enum
{
    // can be used for index (JetPrereadIndexRanges) or residual predicate (JetSetCursorFilter)
    JET_relopEquals = 0,
    JET_relopPrefixEquals,

    // can only be used for residual predicate (JetSetCursorFilter)
    JET_relopNotEquals,
    JET_relopLessThanOrEqual,
    JET_relopLessThan,
    JET_relopGreaterThanOrEqual,
    JET_relopGreaterThan,
    JET_relopBitmaskEqualsZero,
    JET_relopBitmaskNotEqualsZero,
} JET_RELOP;

typedef struct
{
    JET_COLUMNID    columnid;   //  columnid of the column
    JET_RELOP       relop;      //  relational operator
    void *          pv;         //  pointer to the value to use
    uint32_t   cb;         //  size of the value to use
    JET_GRBIT       grbit;      //  optional grbits
} JET_INDEX_COLUMN;

typedef struct
{
    JET_INDEX_COLUMN *  rgStartColumns;
    uint32_t       cStartColumns;
    JET_INDEX_COLUMN *  rgEndColumns;
    uint32_t       cEndColumns;
} JET_INDEX_RANGE;
#endif  //  JET_VERSION >= 0x0602
//  for database DDL conversion

typedef enum
{
    opDDLConvNull = 0,
    opDDLConvAddCallback = 1,
    opDDLConvChangeColumn = 2,
    opDDLConvAddConditionalColumnsToAllIndexes = 3,
    opDDLConvAddColumnCallback = 4,
    opDDLConvIncreaseMaxColumnSize = 5,
    opDDLConvChangeIndexDensity = 6,
    opDDLConvChangeCallbackDLL = 7,
    opDDLConvMax = 8
} JET_OPDDLCONV;

#if ( JET_VERSION >= 0x0501 )
typedef struct tagDDLADDCALLBACK_A
{
    char        *szTable;
    char        *szCallback;
    JET_CBTYP   cbtyp;
} JET_DDLADDCALLBACK_A;

typedef struct tagDDLADDCALLBACK_W
{
    char16_t       *szTable;
    char16_t       *szCallback;
    JET_CBTYP   cbtyp;
} JET_DDLADDCALLBACK_W;

#ifdef JET_UNICODE
typedef JET_DDLADDCALLBACK_W JET_DDLADDCALLBACK;
#else
typedef JET_DDLADDCALLBACK_A JET_DDLADDCALLBACK;
#endif
#endif // JET_VERSION >= 0x0501

typedef struct tagDDLCHANGECOLUMN_A
{
    char        *szTable;
    char        *szColumn;
    JET_COLTYP  coltypNew;
    JET_GRBIT   grbitNew;
} JET_DDLCHANGECOLUMN_A;

typedef struct tagDDLCHANGECOLUMN_W
{
    char16_t       *szTable;
    char16_t       *szColumn;
    JET_COLTYP  coltypNew;
    JET_GRBIT   grbitNew;
} JET_DDLCHANGECOLUMN_W;

#ifdef JET_UNICODE
typedef JET_DDLCHANGECOLUMN_W JET_DDLCHANGECOLUMN;
#else
typedef JET_DDLCHANGECOLUMN_A JET_DDLCHANGECOLUMN;
#endif

typedef struct tagDDLMAXCOLUMNSIZE_A
{
    char            *szTable;
    char            *szColumn;
    uint32_t   cbMax;
} JET_DDLMAXCOLUMNSIZE_A;

typedef struct tagDDLMAXCOLUMNSIZE_W
{
    char16_t           *szTable;
    char16_t           *szColumn;
    uint32_t   cbMax;
} JET_DDLMAXCOLUMNSIZE_W;

#ifdef JET_UNICODE
typedef JET_DDLMAXCOLUMNSIZE_W JET_DDLMAXCOLUMNSIZE;
#else
typedef JET_DDLMAXCOLUMNSIZE_A JET_DDLMAXCOLUMNSIZE;
#endif

typedef struct tagDDLADDCONDITIONALCOLUMNSTOALLINDEXES_A
{
    char                    * szTable;                  // name of table to convert
    JET_CONDITIONALCOLUMN_A * rgconditionalcolumn;      // pointer to conditional column structure
    uint32_t           cConditionalColumn;         // number of conditional columns
} JET_DDLADDCONDITIONALCOLUMNSTOALLINDEXES_A;

typedef struct tagDDLADDCONDITIONALCOLUMNSTOALLINDEXES_W
{
    char16_t                   * szTable;                  // name of table to convert
    JET_CONDITIONALCOLUMN_W * rgconditionalcolumn;      // pointer to conditional column structure
    uint32_t           cConditionalColumn;         // number of conditional columns
} JET_DDLADDCONDITIONALCOLUMNSTOALLINDEXES_W;

#ifdef JET_UNICODE
typedef JET_DDLADDCONDITIONALCOLUMNSTOALLINDEXES_W JET_DDLADDCONDITIONALCOLUMNSTOALLINDEXES;
#else
typedef JET_DDLADDCONDITIONALCOLUMNSTOALLINDEXES_A JET_DDLADDCONDITIONALCOLUMNSTOALLINDEXES;
#endif

typedef struct tagDDLADDCOLUMCALLBACK_A
{
    char            *szTable;
    char            *szColumn;
    char            *szCallback;
    void            *pvCallbackData;
    uint32_t   cbCallbackData;
} JET_DDLADDCOLUMNCALLBACK_A;

typedef struct tagDDLADDCOLUMCALLBACK_W
{
    char16_t           *szTable;
    char16_t           *szColumn;
    char16_t           *szCallback;
    void            *pvCallbackData;
    uint32_t   cbCallbackData;
} JET_DDLADDCOLUMNCALLBACK_W;

#ifdef JET_UNICODE
typedef JET_DDLADDCOLUMNCALLBACK_W JET_DDLADDCOLUMNCALLBACK;
#else
typedef JET_DDLADDCOLUMNCALLBACK_A JET_DDLADDCOLUMNCALLBACK;
#endif

typedef struct tagDDLINDEXDENSITY_A
{
    char            *szTable;
    char            *szIndex;       //  pass NULL to change density of primary index
    uint32_t   ulDensity;
} JET_DDLINDEXDENSITY_A;

typedef struct tagDDLINDEXDENSITY_W
{
    char16_t           *szTable;
    char16_t           *szIndex;       //  pass NULL to change density of primary index
    uint32_t   ulDensity;
} JET_DDLINDEXDENSITY_W;

#ifdef JET_UNICODE
typedef JET_DDLINDEXDENSITY_W JET_DDLINDEXDENSITY;
#else
typedef JET_DDLINDEXDENSITY_A JET_DDLINDEXDENSITY;
#endif

typedef struct tagDDLCALLBACKDLL_A
{
    char            *szOldDLL;
    char            *szNewDLL;
} JET_DDLCALLBACKDLL_A;

typedef struct tagDDLCALLBACKDLL_W
{
    char16_t           *szOldDLL;
    char16_t           *szNewDLL;
} JET_DDLCALLBACKDLL_W;

#ifdef JET_UNICODE
typedef JET_DDLCALLBACKDLL_W JET_DDLCALLBACKDLL;
#else
typedef JET_DDLCALLBACKDLL_A JET_DDLCALLBACKDLL;
#endif

//  The caller need to setup JET_OLP with a signal wait for the signal to be set.

typedef struct
{
    void    *pvReserved1;       // internally use
    void    *pvReserved2;
    uint32_t cbActual;     // the actual number of bytes read through this IO
    JET_HANDLE  hSig;           // a manual reset signal to wait for the IO to complete.
    JET_ERR     err;                // Err code for this assync IO.
} JET_OLP;
#pragma pack(push, 1)
#define JET_MAX_COMPUTERNAME_LENGTH 15

typedef struct
{
    char        bSeconds;               //  0 - 59
    char        bMinutes;               //  0 - 59
    char        bHours;                 //  0 - 23
    char        bDay;                   //  1 - 31
    char        bMonth;                 //  1 - 12
    char        bYear;                  //  current year - 1900
    union
    {
        char        bFiller1;
        struct
        {
            unsigned char fTimeIsUTC:1;
            unsigned char bMillisecondsLow:7;
        };
    };
    union
    {
        char        bFiller2;
        struct
        {
            unsigned char fReserved:1;
            unsigned char bMillisecondsHigh:3;
            unsigned char fUnused:4;
        };
    };
} JET_LOGTIME;

#if ( JET_VERSION >= 0x0600 )
// the JET_BKLOGTIME is an extention of JET_LOGTIME to be used
// in the JET_BKINFO structure. They should have the same size for
// compatibility reasons
typedef struct
{
    char        bSeconds;               //  0 - 59
    char        bMinutes;               //  0 - 59
    char        bHours;                 //  0 - 23
    char        bDay;                   //  1 - 31
    char        bMonth;                 //  1 - 12
    char        bYear;                  //  current year - 1900
    union
    {
        char        bFiller1;
        struct
        {
            unsigned char fTimeIsUTC:1;
            unsigned char bMillisecondsLow:7;
        };
    };
    union
    {
        char        bFiller2;
        struct
        {
            unsigned char fOSSnapshot:1;
            unsigned char bMillisecondsHigh:3;
            unsigned char fReserved:4;
        };
    };
} JET_BKLOGTIME;
#endif // JET_VERSION >= 0x0600

typedef struct
{
    unsigned short  ib;             // must be the last so that lgpos can
    unsigned short  isec;           // index of disksec starting logsec
    int32_t            lGeneration;    // generation of logsec
} JET_LGPOS;                    // be casted to TIME.

typedef struct
{
    uint32_t   ulRandom;           //  a random number
    JET_LOGTIME     logtimeCreate;      //  time db created, in logtime format
    char            szComputerName[ JET_MAX_COMPUTERNAME_LENGTH + 1 ];  // where db is created
} JET_SIGNATURE;
#if ( JET_VERSION >= 0x0600 )
typedef struct
{
    uint32_t   genMin;
    uint32_t   genMax;
    JET_LOGTIME     logtimeGenMaxCreate;
} JET_CHECKPOINTINFO;
#endif // JET_VERSION >= 0x0600
typedef struct
{
    JET_LGPOS       lgposMark;          //  id for this backup
    union
    {
        JET_LOGTIME     logtimeMark;
#if ( JET_VERSION >= 0x0600 )
        JET_BKLOGTIME   bklogtimeMark;
#endif // JET_VERSION >= 0x0600
    };
    uint32_t   genLow;
    uint32_t   genHigh;
} JET_BKINFO;

#pragma pack(pop)

typedef struct
{
    uint32_t   ulVersion;      //  the major (incompatible) version of DAE from the last engine attach/create.
    uint32_t   ulUpdate;       //  used to track incremental database format "update (major)" version from the
                                    //  last attach/create that is a backward-compatible major update.
    JET_SIGNATURE   signDb;         //  (28 bytes) signature of the db (incl. creation time).
    uint32_t   dbstate;        //  consistent/inconsistent state

    JET_LGPOS       lgposConsistent;    //  null if in inconsistent state
    JET_LOGTIME     logtimeConsistent;  // null if in inconsistent state

    JET_LOGTIME     logtimeAttach;  //  Last attach time.
    JET_LGPOS       lgposAttach;

    JET_LOGTIME     logtimeDetach;  //  Last detach time.
    JET_LGPOS       lgposDetach;

    JET_SIGNATURE   signLog;        //  (28 bytes) log signature for this attachments

    JET_BKINFO      bkinfoFullPrev; //  Last successful full backup.

    JET_BKINFO      bkinfoIncPrev;  //  Last successful Incremental backup.
                                    //  Reset when bkinfoFullPrev is set
    JET_BKINFO      bkinfoFullCur;  //  current backup. Succeed if a
                                    //  corresponding pat file generated.
    uint32_t   fShadowingDisabled;
    uint32_t   fUpgradeDb;

    //  NT version information. This is needed to decide if an index need
    //  be recreated due to sort table changes.

    uint32_t   dwMajorVersion;     /*  OS version info                             */
    uint32_t   dwMinorVersion;
    uint32_t   dwBuildNumber;
    int32_t            lSPNumber;

    uint32_t   cbPageSize;         //  database page size (0 = 4k pages)

} JET_DBINFOMISC;

#if ( JET_VERSION >= 0x0600 )
typedef struct
{
    uint32_t   ulVersion;      //  the major (incompatible) version of DAE from the last engine attach/create.
    uint32_t   ulUpdate;       //  used to track incremental database format "update (major)" version from the
                                    //  last attach/create that is a backward-compatible major update.
    JET_SIGNATURE   signDb;         //  (28 bytes) signature of the db (incl. creation time).
    uint32_t   dbstate;        //  consistent/inconsistent state

    JET_LGPOS       lgposConsistent;    //  null if in inconsistent state
    JET_LOGTIME     logtimeConsistent;  // null if in inconsistent state

    JET_LOGTIME     logtimeAttach;  //  Last attach time.
    JET_LGPOS       lgposAttach;

    JET_LOGTIME     logtimeDetach;  //  Last detach time.
    JET_LGPOS       lgposDetach;

    JET_SIGNATURE   signLog;        //  (28 bytes) log signature for this attachments

    JET_BKINFO      bkinfoFullPrev; //  Last successful full backup.

    JET_BKINFO      bkinfoIncPrev;  //  Last successful Incremental backup.
                                    //  Reset when bkinfoFullPrev is set
    JET_BKINFO      bkinfoFullCur;  //  current backup. Succeed if a
                                    //  corresponding pat file generated.
    uint32_t   fShadowingDisabled;
    uint32_t   fUpgradeDb;

    //  NT version information. This is needed to decide if an index need
    //  be recreated due to sort table changes.

    uint32_t   dwMajorVersion;     /*  OS version info                             */
    uint32_t   dwMinorVersion;
    uint32_t   dwBuildNumber;
    int32_t            lSPNumber;

    uint32_t   cbPageSize;         //  database page size (0 = 4k pages)

    // new fields added on top of the above JET_DBINFOMISC
    uint32_t   genMinRequired;         //  the minimum log generation required for replaying the logs. Typically the checkpoint generation
    uint32_t   genMaxRequired;         //  the maximum log generation required for replaying the logs.
    JET_LOGTIME     logtimeGenMaxCreate;    //  creation time of the genMax log file

    uint32_t   ulRepairCount;          //  number of times repair has been called on this database
    JET_LOGTIME     logtimeRepair;          //  the date of the last time that repair was run
    uint32_t   ulRepairCountOld;       //  number of times ErrREPAIRAttachForRepair has been called on this database before the last defrag

    uint32_t   ulECCFixSuccess;        //  number of times a one bit error was fixed and resulted in a good page
    JET_LOGTIME     logtimeECCFixSuccess;   //  the date of the last time that a one bit error was fixed and resulted in a good page
    uint32_t   ulECCFixSuccessOld;     //  number of times a one bit error was fixed and resulted in a good page before last repair

    uint32_t   ulECCFixFail;           //  number of times a one bit error was fixed and resulted in a bad page
    JET_LOGTIME     logtimeECCFixFail;      //  the date of the last time that a one bit error was fixed and resulted in a bad page
    uint32_t   ulECCFixFailOld;        //  number of times a one bit error was fixed and resulted in a bad page before last repair

    uint32_t   ulBadChecksum;          //  number of times a non-correctable ECC/checksum error was found
    JET_LOGTIME     logtimeBadChecksum;     //  the date of the last time that a non-correctable ECC/checksum error was found
    uint32_t   ulBadChecksumOld;       //  number of times a non-correctable ECC/checksum error was found before last repair

} JET_DBINFOMISC2;
#endif // JET_VERSION >= 0x0600

#if ( JET_VERSION >= 0x0601 )
typedef struct
{
    uint32_t   ulVersion;      //  the major (incompatible) version of DAE from the last engine attach/create.
    uint32_t   ulUpdate;       //  used to track incremental database format "update (major)" version from the
                                    //  last attach/create that is a backward-compatible major update.
    JET_SIGNATURE   signDb;         //  (28 bytes) signature of the db (incl. creation time).
    uint32_t   dbstate;        //  consistent/inconsistent state

    JET_LGPOS       lgposConsistent;    //  null if in inconsistent state
    JET_LOGTIME     logtimeConsistent;  // null if in inconsistent state

    JET_LOGTIME     logtimeAttach;  //  Last attach time.
    JET_LGPOS       lgposAttach;

    JET_LOGTIME     logtimeDetach;  //  Last detach time.
    JET_LGPOS       lgposDetach;

    JET_SIGNATURE   signLog;        //  (28 bytes) log signature for this attachments

    JET_BKINFO      bkinfoFullPrev; //  Last successful full backup.

    JET_BKINFO      bkinfoIncPrev;  //  Last successful Incremental backup.
                                    //  Reset when bkinfoFullPrev is set
    JET_BKINFO      bkinfoFullCur;  //  current backup. Succeed if a
                                    //  corresponding pat file generated.
    uint32_t   fShadowingDisabled;
    uint32_t   fUpgradeDb;

    //  NT version information. This is needed to decide if an index need
    //  be recreated due to sort table changes.

    uint32_t   dwMajorVersion;     /*  OS version info                             */
    uint32_t   dwMinorVersion;
    uint32_t   dwBuildNumber;
    int32_t            lSPNumber;

    uint32_t   cbPageSize;         //  database page size (0 = 4k pages)

    // new fields added on top of the above JET_DBINFOMISC
    uint32_t   genMinRequired;         //  the minimum log generation required for replaying the logs. Typically the checkpoint generation
    uint32_t   genMaxRequired;         //  the maximum log generation required for replaying the logs.
    JET_LOGTIME     logtimeGenMaxCreate;    //  creation time of the genMax log file

    uint32_t   ulRepairCount;          //  number of times repair has been called on this database
    JET_LOGTIME     logtimeRepair;          //  the date of the last time that repair was run
    uint32_t   ulRepairCountOld;       //  number of times ErrREPAIRAttachForRepair has been called on this database before the last defrag

    uint32_t   ulECCFixSuccess;        //  number of times a one bit error was fixed and resulted in a good page
    JET_LOGTIME     logtimeECCFixSuccess;   //  the date of the last time that a one bit error was fixed and resulted in a good page
    uint32_t   ulECCFixSuccessOld;     //  number of times a one bit error was fixed and resulted in a good page before last repair

    uint32_t   ulECCFixFail;           //  number of times a one bit error was fixed and resulted in a bad page
    JET_LOGTIME     logtimeECCFixFail;      //  the date of the last time that a one bit error was fixed and resulted in a bad page
    uint32_t   ulECCFixFailOld;        //  number of times a one bit error was fixed and resulted in a bad page before last repair

    uint32_t   ulBadChecksum;          //  number of times a non-correctable ECC/checksum error was found
    JET_LOGTIME     logtimeBadChecksum;     //  the date of the last time that a non-correctable ECC/checksum error was found
    uint32_t   ulBadChecksumOld;       //  number of times a non-correctable ECC/checksum error was found before last repair

    // new fields added on top of the above JET_DBINFOMISC2
    uint32_t   genCommitted;           //  the maximum log generation committed to the database. Typically the current log generation

} JET_DBINFOMISC3;

typedef struct
{
    uint32_t   ulVersion;      //  the major (incompatible) version of DAE from the last engine attach/create.
    uint32_t   ulUpdate;       //  used to track incremental database format "update (major)" version from the
                                    //  last attach/create that is a backward-compatible major update.
    JET_SIGNATURE   signDb;         //  (28 bytes) signature of the db (incl. creation time).
    uint32_t   dbstate;        //  consistent/inconsistent state

    JET_LGPOS       lgposConsistent;    //  null if in inconsistent state
    JET_LOGTIME     logtimeConsistent;  // null if in inconsistent state

    JET_LOGTIME     logtimeAttach;  //  Last attach time.
    JET_LGPOS       lgposAttach;

    JET_LOGTIME     logtimeDetach;  //  Last detach time.
    JET_LGPOS       lgposDetach;

    JET_SIGNATURE   signLog;        //  (28 bytes) log signature for this attachments

    JET_BKINFO      bkinfoFullPrev; //  Last successful full backup.

    JET_BKINFO      bkinfoIncPrev;  //  Last successful Incremental backup.
                                    //  Reset when bkinfoFullPrev is set
    JET_BKINFO      bkinfoFullCur;  //  current backup. Succeed if a
                                    //  corresponding pat file generated.
    uint32_t   fShadowingDisabled;
    uint32_t   fUpgradeDb;

    //  NT version information. This is needed to decide if an index need
    //  be recreated due to sort table changes.

    uint32_t   dwMajorVersion;     /*  OS version info                             */
    uint32_t   dwMinorVersion;
    uint32_t   dwBuildNumber;
    int32_t            lSPNumber;

    uint32_t   cbPageSize;         //  database page size (0 = 4k pages)

    // new fields added on top of the above JET_DBINFOMISC
    uint32_t   genMinRequired;         //  the minimum log generation required for replaying the logs. Typically the checkpoint generation
    uint32_t   genMaxRequired;         //  the maximum log generation required for replaying the logs.
    JET_LOGTIME     logtimeGenMaxCreate;    //  creation time of the genMax log file

    uint32_t   ulRepairCount;          //  number of times repair has been called on this database
    JET_LOGTIME     logtimeRepair;          //  the date of the last time that repair was run
    uint32_t   ulRepairCountOld;       //  number of times ErrREPAIRAttachForRepair has been called on this database before the last defrag

    uint32_t   ulECCFixSuccess;        //  number of times a one bit error was fixed and resulted in a good page
    JET_LOGTIME     logtimeECCFixSuccess;   //  the date of the last time that a one bit error was fixed and resulted in a good page
    uint32_t   ulECCFixSuccessOld;     //  number of times a one bit error was fixed and resulted in a good page before last repair

    uint32_t   ulECCFixFail;           //  number of times a one bit error was fixed and resulted in a bad page
    JET_LOGTIME     logtimeECCFixFail;      //  the date of the last time that a one bit error was fixed and resulted in a bad page
    uint32_t   ulECCFixFailOld;        //  number of times a one bit error was fixed and resulted in a bad page before last repair

    uint32_t   ulBadChecksum;          //  number of times a non-correctable ECC/checksum error was found
    JET_LOGTIME     logtimeBadChecksum;     //  the date of the last time that a non-correctable ECC/checksum error was found
    uint32_t   ulBadChecksumOld;       //  number of times a non-correctable ECC/checksum error was found before last repair

    // new fields added on top of the above JET_DBINFOMISC2
    uint32_t   genCommitted;           //  the maximum log generation committed to the database. Typically the current log generation

    // new fields added on top of the above JET_DBINFOMISC3
    JET_BKINFO  bkinfoCopyPrev;         //  Last successful Copy backup
    JET_BKINFO  bkinfoDiffPrev;         //  Last successful Differential backup, reset when bkinfoFullPrev is set
} JET_DBINFOMISC4;
#endif // JET_VERSION >= 0x0601
#if ( JET_VERSION >= 0x0601 )
typedef struct
{
    uint32_t   ulVersion;      //  the major (incompatible) version of DAE from the last engine attach/create.
    uint32_t   ulUpdate;       //  used to track incremental database format "update (major)" version from the
                                    //  last attach/create that is a backward-compatible major update.
    JET_SIGNATURE   signDb;         //  (28 bytes) signature of the db (incl. creation time).
    uint32_t   dbstate;        //  consistent/inconsistent state

    JET_LGPOS       lgposConsistent;    //  null if in inconsistent state
    JET_LOGTIME     logtimeConsistent;  // null if in inconsistent state

    JET_LOGTIME     logtimeAttach;  //  Last attach time.
    JET_LGPOS       lgposAttach;

    JET_LOGTIME     logtimeDetach;  //  Last detach time.
    JET_LGPOS       lgposDetach;

    JET_SIGNATURE   signLog;        //  (28 bytes) log signature for this attachments

    JET_BKINFO      bkinfoFullPrev; //  Last successful full backup.

    JET_BKINFO      bkinfoIncPrev;  //  Last successful Incremental backup.
                                    //  Reset when bkinfoFullPrev is set
    JET_BKINFO      bkinfoFullCur;  //  current backup. Succeed if a
                                    //  corresponding pat file generated.
    uint32_t   fShadowingDisabled;
    uint32_t   fUpgradeDb;

    //  NT version information. This is needed to decide if an index need
    //  be recreated due to sort table changes.

    uint32_t   dwMajorVersion;     /*  OS version info                             */
    uint32_t   dwMinorVersion;
    uint32_t   dwBuildNumber;
    int32_t            lSPNumber;

    uint32_t   cbPageSize;         //  database page size (0 = 4k pages)

    // new fields added on top of the above JET_DBINFOMISC
    uint32_t   genMinRequired;         //  the minimum log generation required for replaying the logs. Typically the checkpoint generation
    uint32_t   genMaxRequired;         //  the maximum log generation required for replaying the logs.
    JET_LOGTIME     logtimeGenMaxCreate;    //  creation time of the genMax log file

    uint32_t   ulRepairCount;          //  number of times repair has been called on this database
    JET_LOGTIME     logtimeRepair;          //  the date of the last time that repair was run
    uint32_t   ulRepairCountOld;       //  number of times ErrREPAIRAttachForRepair has been called on this database before the last defrag

    uint32_t   ulECCFixSuccess;        //  number of times a one bit error was fixed and resulted in a good page
    JET_LOGTIME     logtimeECCFixSuccess;   //  the date of the last time that a one bit error was fixed and resulted in a good page
    uint32_t   ulECCFixSuccessOld;     //  number of times a one bit error was fixed and resulted in a good page before last repair

    uint32_t   ulECCFixFail;           //  number of times a one bit error was fixed and resulted in a bad page
    JET_LOGTIME     logtimeECCFixFail;      //  the date of the last time that a one bit error was fixed and resulted in a bad page
    uint32_t   ulECCFixFailOld;        //  number of times a one bit error was fixed and resulted in a bad page before last repair

    uint32_t   ulBadChecksum;          //  number of times a non-correctable ECC/checksum error was found
    JET_LOGTIME     logtimeBadChecksum;     //  the date of the last time that a non-correctable ECC/checksum error was found
    uint32_t   ulBadChecksumOld;       //  number of times a non-correctable ECC/checksum error was found before last repair

    // new fields added on top of the above JET_DBINFOMISC2
    uint32_t   genCommitted;           //  the maximum log generation committed to the database. Typically the current log generation

    // new fields added on top of the above JET_DBINFOMISC3
    JET_BKINFO  bkinfoCopyPrev;         //  Last successful Copy backup
    JET_BKINFO  bkinfoDiffPrev;         //  Last successful Differential backup, reset when bkinfoFullPrev is set

    // new fields added on top of the above JET_DBINFOMISC4
    uint32_t   ulIncrementalReseedCount;       //  number of times incremental reseed has been initiated on this database
    JET_LOGTIME     logtimeIncrementalReseed;       //  the date of the last time that incremental reseed was initiated on this database
    uint32_t   ulIncrementalReseedCountOld;    //  number of times incremental reseed was initiated on this database before the last defrag

    uint32_t   ulPagePatchCount;               //  number of pages patched in the database as a part of incremental reseed
    JET_LOGTIME     logtimePagePatch;               //  the date of the last time that a page was patched as a part of incremental reseed
    uint32_t   ulPagePatchCountOld;            //  number of pages patched in the database as a part of incremental reseed before the last defrag
} JET_DBINFOMISC5;

typedef struct
{
    uint32_t   ulVersion;      //  the major (incompatible) version of DAE from the last engine attach/create.
    uint32_t   ulUpdate;       //  used to track incremental database format "update (major)" version from the
                                    //  last attach/create that is a backward-compatible major update.
    JET_SIGNATURE   signDb;         //  (28 bytes) signature of the db (incl. creation time).
    uint32_t   dbstate;        //  consistent/inconsistent state

    JET_LGPOS       lgposConsistent;    //  null if in inconsistent state
    JET_LOGTIME     logtimeConsistent;  // null if in inconsistent state

    JET_LOGTIME     logtimeAttach;  //  Last attach time.
    JET_LGPOS       lgposAttach;

    JET_LOGTIME     logtimeDetach;  //  Last detach time.
    JET_LGPOS       lgposDetach;

    JET_SIGNATURE   signLog;        //  (28 bytes) log signature for this attachments

    JET_BKINFO      bkinfoFullPrev; //  Last successful full backup.

    JET_BKINFO      bkinfoIncPrev;  //  Last successful Incremental backup.
                                    //  Reset when bkinfoFullPrev is set
    JET_BKINFO      bkinfoFullCur;  //  current backup. Succeed if a
                                    //  corresponding pat file generated.
    uint32_t   fShadowingDisabled;
    uint32_t   fUpgradeDb;

    //  NT version information. This is needed to decide if an index need
    //  be recreated due to sort table changes.

    uint32_t   dwMajorVersion;     /*  OS version info                             */
    uint32_t   dwMinorVersion;
    uint32_t   dwBuildNumber;
    int32_t            lSPNumber;

    uint32_t   cbPageSize;         //  database page size (0 = 4k pages)

    // new fields added on top of the above JET_DBINFOMISC
    uint32_t   genMinRequired;         //  the minimum log generation required for replaying the logs. Typically the checkpoint generation
    uint32_t   genMaxRequired;         //  the maximum log generation required for replaying the logs.
    JET_LOGTIME     logtimeGenMaxCreate;    //  creation time of the genMax log file

    uint32_t   ulRepairCount;          //  number of times repair has been called on this database
    JET_LOGTIME     logtimeRepair;          //  the date of the last time that repair was run
    uint32_t   ulRepairCountOld;       //  number of times ErrREPAIRAttachForRepair has been called on this database before the last defrag

    uint32_t   ulECCFixSuccess;        //  number of times a one bit error was fixed and resulted in a good page
    JET_LOGTIME     logtimeECCFixSuccess;   //  the date of the last time that a one bit error was fixed and resulted in a good page
    uint32_t   ulECCFixSuccessOld;     //  number of times a one bit error was fixed and resulted in a good page before last repair

    uint32_t   ulECCFixFail;           //  number of times a one bit error was fixed and resulted in a bad page
    JET_LOGTIME     logtimeECCFixFail;      //  the date of the last time that a one bit error was fixed and resulted in a bad page
    uint32_t   ulECCFixFailOld;        //  number of times a one bit error was fixed and resulted in a bad page before last repair

    uint32_t   ulBadChecksum;          //  number of times a non-correctable ECC/checksum error was found
    JET_LOGTIME     logtimeBadChecksum;     //  the date of the last time that a non-correctable ECC/checksum error was found
    uint32_t   ulBadChecksumOld;       //  number of times a non-correctable ECC/checksum error was found before last repair

    // new fields added on top of the above JET_DBINFOMISC2
    uint32_t   genCommitted;           //  the maximum log generation committed to the database. Typically the current log generation

    // new fields added on top of the above JET_DBINFOMISC3
    JET_BKINFO  bkinfoCopyPrev;         //  Last successful Copy backup
    JET_BKINFO  bkinfoDiffPrev;         //  Last successful Differential backup, reset when bkinfoFullPrev is set

    // new fields added on top of the above JET_DBINFOMISC4
    uint32_t   ulIncrementalReseedCount;       //  number of times incremental reseed has been initiated on this database
    JET_LOGTIME     logtimeIncrementalReseed;       //  the date of the last time that incremental reseed was initiated on this database
    uint32_t   ulIncrementalReseedCountOld;    //  number of times incremental reseed was initiated on this database before the last defrag

    uint32_t   ulPagePatchCount;               //  number of pages patched in the database as a part of incremental reseed
    JET_LOGTIME     logtimePagePatch;               //  the date of the last time that a page was patched as a part of incremental reseed
    uint32_t   ulPagePatchCountOld;            //  number of pages patched in the database as a part of incremental reseed before the last defrag

    // new fields added on top of the above JET_DBINFOMISC5
    JET_LOGTIME logtimeChecksumPrev;    // last checksum pass finish time (UTC - 1900y)
    JET_LOGTIME logtimeChecksumStart;   // current checksum pass start time (UTC - 1900y)
    uint32_t cpgDatabaseChecked;   // # of page checked for current pass
} JET_DBINFOMISC6;
#endif // JET_VERSION >= 0x0601

#if ( JET_VERSION >= 0x0A00 )
typedef struct
{
    uint32_t   ulVersion;      //  the major (incompatible) version of DAE from the last engine attach/create.
    uint32_t   ulUpdate;       //  used to track incremental database format "update (major)" version from the
                                    //  last attach/create that is a backward-compatible major update.
    JET_SIGNATURE   signDb;         //  (28 bytes) signature of the db (incl. creation time).
    uint32_t   dbstate;        //  consistent/inconsistent state

    JET_LGPOS       lgposConsistent;    //  null if in inconsistent state
    JET_LOGTIME     logtimeConsistent;  // null if in inconsistent state

    JET_LOGTIME     logtimeAttach;  //  Last attach time.
    JET_LGPOS       lgposAttach;

    JET_LOGTIME     logtimeDetach;  //  Last detach time.
    JET_LGPOS       lgposDetach;

    JET_SIGNATURE   signLog;        //  (28 bytes) log signature for this attachments

    JET_BKINFO      bkinfoFullPrev; //  Last successful full backup.

    JET_BKINFO      bkinfoIncPrev;  //  Last successful Incremental backup.
                                    //  Reset when bkinfoFullPrev is set
    JET_BKINFO      bkinfoFullCur;  //  current backup. Succeed if a
                                    //  corresponding pat file generated.
    uint32_t   fShadowingDisabled;
    uint32_t   fUpgradeDb;

    //  NT version information. This is needed to decide if an index need
    //  be recreated due to sort table changes.

    uint32_t   dwMajorVersion;     /*  OS version info                             */
    uint32_t   dwMinorVersion;
    uint32_t   dwBuildNumber;
    int32_t            lSPNumber;

    uint32_t   cbPageSize;         //  database page size (0 = 4k pages)

    // new fields added on top of the above JET_DBINFOMISC
    uint32_t   genMinRequired;         //  the minimum log generation required for replaying the logs. Typically the checkpoint generation
    uint32_t   genMaxRequired;         //  the maximum log generation required for replaying the logs.
    JET_LOGTIME     logtimeGenMaxCreate;    //  creation time of the genMax log file

    uint32_t   ulRepairCount;          //  number of times repair has been called on this database
    JET_LOGTIME     logtimeRepair;          //  the date of the last time that repair was run
    uint32_t   ulRepairCountOld;       //  number of times ErrREPAIRAttachForRepair has been called on this database before the last defrag

    uint32_t   ulECCFixSuccess;        //  number of times a one bit error was fixed and resulted in a good page
    JET_LOGTIME     logtimeECCFixSuccess;   //  the date of the last time that a one bit error was fixed and resulted in a good page
    uint32_t   ulECCFixSuccessOld;     //  number of times a one bit error was fixed and resulted in a good page before last repair

    uint32_t   ulECCFixFail;           //  number of times a one bit error was fixed and resulted in a bad page
    JET_LOGTIME     logtimeECCFixFail;      //  the date of the last time that a one bit error was fixed and resulted in a bad page
    uint32_t   ulECCFixFailOld;        //  number of times a one bit error was fixed and resulted in a bad page before last repair

    uint32_t   ulBadChecksum;          //  number of times a non-correctable ECC/checksum error was found
    JET_LOGTIME     logtimeBadChecksum;     //  the date of the last time that a non-correctable ECC/checksum error was found
    uint32_t   ulBadChecksumOld;       //  number of times a non-correctable ECC/checksum error was found before last repair

    // new fields added on top of the above JET_DBINFOMISC2
    uint32_t   genCommitted;           //  the maximum log generation committed to the database. Typically the current log generation

    // new fields added on top of the above JET_DBINFOMISC3
    JET_BKINFO  bkinfoCopyPrev;         //  Last successful Copy backup
    JET_BKINFO  bkinfoDiffPrev;         //  Last successful Differential backup, reset when bkinfoFullPrev is set

    // new fields added on top of the above JET_DBINFOMISC4
    uint32_t   ulIncrementalReseedCount;       //  number of times incremental reseed has been initiated on this database
    JET_LOGTIME     logtimeIncrementalReseed;       //  the date of the last time that incremental reseed was initiated on this database
    uint32_t   ulIncrementalReseedCountOld;    //  number of times incremental reseed was initiated on this database before the last defrag

    uint32_t   ulPagePatchCount;               //  number of pages patched in the database as a part of incremental reseed
    JET_LOGTIME     logtimePagePatch;               //  the date of the last time that a page was patched as a part of incremental reseed
    uint32_t   ulPagePatchCountOld;            //  number of pages patched in the database as a part of incremental reseed before the last defrag

    // new fields added on top of the above JET_DBINFOMISC5
    JET_LOGTIME logtimeChecksumPrev;    // last checksum pass finish time (UTC - 1900y)
    JET_LOGTIME logtimeChecksumStart;   // current checksum pass start time (UTC - 1900y)
    uint32_t cpgDatabaseChecked;   // # of page checked for current pass

    // new fields added on top of the above JET_DBINFOMISC6
    JET_LOGTIME     logtimeLastReAttach;    //  Last attach time.
    JET_LGPOS       lgposLastReAttach;
} JET_DBINFOMISC7;
#endif // JET_VERSION >= 0x0A00

typedef struct
{
    uint32_t   ulGeneration;
    JET_SIGNATURE   signLog;

    JET_LOGTIME     logtimeCreate;
    JET_LOGTIME     logtimePreviousGeneration;

    uint32_t   ulFlags;

    uint32_t   ulVersionMajor;
    uint32_t   ulVersionMinor;
    uint32_t   ulVersionUpdate;

    uint32_t   cbSectorSize;
    uint32_t   cbHeader;
    uint32_t   cbFile;
    uint32_t   cbDatabasePageSize;
} JET_LOGINFOMISC;

#if ( JET_VERSION >= 0x0601 )
typedef struct
{
    uint32_t   ulGeneration;
    JET_SIGNATURE   signLog;

    JET_LOGTIME     logtimeCreate;
    JET_LOGTIME     logtimePreviousGeneration;

    uint32_t   ulFlags;

    uint32_t   ulVersionMajor;
    uint32_t   ulVersionMinor;
    uint32_t   ulVersionUpdate;

    uint32_t   cbSectorSize;
    uint32_t   cbHeader;
    uint32_t   cbFile;
    uint32_t   cbDatabasePageSize;

    JET_LGPOS       lgposCheckpoint;
} JET_LOGINFOMISC2;

#endif

#if ( JET_VERSION >= 0x0A01 )
typedef struct
{
    uint32_t   ulGeneration;
    JET_SIGNATURE   signLog;

    JET_LOGTIME     logtimeCreate;
    JET_LOGTIME     logtimePreviousGeneration;

    uint32_t   ulFlags;

    uint32_t   ulVersionMajor;
    uint32_t   ulVersionUpdateMajor;
    uint32_t   ulVersionUpdateMinor;

    uint32_t   cbSectorSize;
    uint32_t   cbHeader;
    uint32_t   cbFile;
    uint32_t   cbDatabasePageSize;

    JET_LGPOS       lgposCheckpoint;

    uint32_t   ulVersionMinorDeprecated;       //  deprecated

    uint64_t    checksumPrevLogAllSegments;

} JET_LOGINFOMISC3;

#endif

// We do not attempt to maintain version compatibility with private data structures.
// People using the private version of the header shouldn't need to target
// down-level anyay.
#if ( JET_VERSION >= 0x0A00 )

//
//  This is the list of callback sequences you could get when using JetInit4()
//  with JET_bitExternalRecoveryControl
//  Note: For all such sequences JET_snpRecoveryControl will be the JET_SNP value.
//
//  In an empty log directory:
//
//      JET_sntOpenLog, JET_OpenLogForRecoveryCheckingAndPatching, fCurrent         // pre-check
//      JET_sntMissingLog, JET_MissingLogContinueToRedo
//      JET_sntMissingLog, JET_MissingLogCreateNewLogStream                         JET_bitLogStreamMustExist -> JET_errMissingLogFile
//
//  No edb.log, no edbtmp.log, X archive logs ... the "external log provider" case.
//
//      JET_sntOpenLog, JET_OpenLogForRecoveryCheckingAndPatching, fCurrent         // pre-check
//      JET_sntOpenLog, JET_OpenLogForRecoveryCheckingAndPatching, lGenHigh         // we will close this fairly soon
//      JET_sntMissingLog, JET_MissingLogContinueToRedo
//      JET_sntOpenLog, JET_OpenLogForRedo, lGenLow/Checkpoint
//      JET_sntOpenLog, JET_OpenLogForRedo, lGenLow+1
//      JET_sntOpenLog, JET_OpenLogForRedo, lGenLow+2
//      JET_sntOpenLog, JET_OpenLogForRedo, lGenLow+3
//      JET_sntOpenLog, JET_OpenLogForRedo, lGenLow+4/lGenHighest-2
//      JET_sntOpenLog, JET_OpenLogForRedo, lGenHighest-1
//      JET_sntOpenLog, JET_OpenLogForRedo, lGenHighest
//      JET_sntOpenLog, JET_OpenLogForRedo, lGenHighest+1
//      JET_sntMissingLog, JET_MissingLogContinueTryCurrentLog (possibly x2)
//      JET_sntOpenLog, JET_OpenLogForRedo, fCurrent
//      JET_sntMissingLog, JET_MissingLogContinueToUndo
//      JET_sntBeginUndo
//      JET_sntOpenLog, JET_OpenLogForUndo
//
//  Once a decision has been made to either fail out or succeed (and continue) from
//  the JET_sntBeginUndo it is irrevocable.  The logs may be changed from that point
//  on.  If you've decided to fail JET_sntBeginUndo with JET_errRecoveredWithoutUndo
//  then you should also fail JET_sntOpenLog+JET_OpenLogForUndo with the other
//  JET_errRecoveredWithoutUndoDatabasesConsistent error for recovery without undo.
//  Or another way ... once you've decided to fail out "without undo", then you
//  should both return to JET_sntBeginUndo with JET_errRecoveredWithoutUndo and you
//  should also fail JET_sntOpenLog+JET_OpenLogForUndo with the other
//  JET_errRecoveredWithoutUndoDatabasesConsistent error for consistent results.
//

#define JET_OpenLogForRecoveryCheckingAndPatching       1   /* Indicates the log file is being opened for the purposes of checking if recovery is necessary and/or patching the shadow sector. */
#define JET_OpenLogForRedo                  2   /* Indicates the log file is being opened for purposes of performing recovery redo / log replay. */
#define JET_OpenLogForUndo                  3   /* Indicates the log file is being opened for purposes of performing recovery undo. */
#define JET_OpenLogForDo                    4   /* Indicates the log file is being opened for purposes of performing do operations. */

#define JET_MissingLogMustFail              1   /* Indicates that no continuation is possible for this error. */
#define JET_MissingLogContinueToRedo        2   /* Indicates that continuing with success will tell recovery to proceed to recovery redo. */
#define JET_MissingLogContinueTryCurrentLog 3   /* Indicates that continuing with success will tell recovery to proceed to try the current / edb.jtx|log log file. */
#define JET_MissingLogContinueToUndo        4   /* Indicates that continuing with success will tell recovery to proceed to recovery undo. */
#define JET_MissingLogCreateNewLogStream    5   /* Indicates that continuing with success will tell recovery to proceed to create a new log stream. */

typedef struct
{
    uint32_t       cbStruct;       /* size of this structure */
    JET_ERR             errDefault;     /* given no desired special treatment, the client should return this */
    JET_INSTANCE        instance;       /* the instance for which recovery is run */

    JET_SNT             sntUnion;       /* indicates the type for the union */

    union {

        //  JET_sntOpenLog
        struct
        {
            uint32_t       cbStruct;       /* size of this structure */
            uint32_t       lGenNext;       /* next log to be replayed */
            unsigned char       fCurrentLog:1;  /* 0 if log with full / archive name */
            unsigned char       eReason;        /* the open disposition or reason - JET_OpenLog* */
            unsigned char       rgbReserved[6]; /* will be 0 */
            char16_t *             wszLogFile;     /* full path of the log file we will open */
            uint32_t       cdbinfomisc;    /* number of database headers */
            JET_DBINFOMISC7 *   rgdbinfomisc;   /* array of database headers for attached databases */
        } OpenLog;

        //  JET_sntOpenCheckpoint
        struct
        {
            uint32_t       cbStruct;       /* size of this structure */
            char16_t *             wszCheckpoint;  /* full path of the checkpoint file we will open */
        } OpenCheckpoint;

        //  JET_sntOpenDatabase not yet implemented.

        //  JET_sntMissingLog
        struct
        {
            uint32_t       cbStruct;       /* size of this structure */
            uint32_t       lGenMissing;    /* next log to be replayed */
            unsigned char       fCurrentLog:1;  /* 0 if log with full / archive name */
            unsigned char       eNextAction;    /* if success is returned, what action will we take */
            unsigned char       rgbReserved[6]; /* will be 0 */
            char16_t *             wszLogFile;     /* full path of the log file we will open */
            uint32_t       cdbinfomisc;    /* number of database headers */
            JET_DBINFOMISC7 *   rgdbinfomisc;   /* array of database headers for attached databases */
        } MissingLog;

        //  JET_sntBeginUndo
        struct
        {
            uint32_t       cbStruct;       /* size of this structure */
            uint32_t       cdbinfomisc;    /* number of database headers */
            JET_DBINFOMISC7 *   rgdbinfomisc;   /* array of database headers for attached databases */
        } BeginUndo;

        //  JET_sntNotificationEvent
        struct
        {
            uint32_t       cbStruct;       /* size of this structure */
            uint32_t       EventID;        /* ID of the event we would publish */
        } NotificationEvent;

        //  JET_sntSignalErrorCondition
        struct
        {
            uint32_t       cbStruct;       /* size of this structure */
            //  no extra info beyond errDefault above
        } SignalErrorCondition;

        //  JET_sntAttachedDb
        struct
        {
            uint32_t       cbStruct;       /* size of this structure */
            const char16_t *       wszDbPath;      /* full path of the database file */
        } AttachedDb;

        //  JET_sntDetachingDb
        struct
        {
            uint32_t       cbStruct;       /* size of this structure */
            const char16_t *       wszDbPath;      /* full path of the database file */
        } DetachingDb;

        //  JET_sntCommitCtx
        struct
        {
            uint32_t       cbStruct;       /* size of this structure */
            const void *        pbCommitCtx;    /* commit context */
            uint32_t       cbCommitCtx;    /* size of commit context */
            uint32_t       fCallbackType;  /* type of callback */
        } CommitCtx;
    };
} JET_RECOVERYCONTROL;

#define fCommitCtxLegacyCommitCallback  1
#define fCommitCtxPreCommitCallback     2
#define fCommitCtxPostCommitCallback    3
#endif // JET_VERSION >= 0x0A00

#if ( JET_VERSION >= 0x0600 )
typedef struct              /* Status Notification Message */
{
    uint32_t   cbStruct;   /* Size of this structure */
    JET_SNC         snc;        /* Status Notification Code */
    uint32_t   ul;         /* Numeric identifier */
    char            sz[256];    /* Identifier */
} JET_SNMSG;
#endif // JET_VERSION >= 0x0600

// Introduced in Win7, but format changed post Win10.
#if ( JET_VERSION >= 0x0A01 )

typedef struct              // Status Notification Page Patch Request
{
    uint32_t   cbStruct;       // Size of this structure
    uint32_t   pageNumber;     // Page being patched
    const char16_t *   szLogFile;      // Full path of the current logfile
    JET_INSTANCE    instance;       // Instance that is running recovery
    JET_DBINFOMISC7 dbinfomisc;     // Database header for the database being patched
    const void *    pvToken;        // Patch token
    uint32_t   cbToken;        // Size of the patch token
    const void *    pvData;         // Patch data (the database page)
    uint32_t   cbData;         // Size of the patch data
    JET_DBID        dbid;           // JET_DBID of database being patched
} JET_SNPATCHREQUEST;

typedef struct              // Status Notification Corrupted Page
{
    uint32_t   cbStruct;       // Size of this structure
    const char16_t *   wszDatabase;    // File name of the database corrupted
    JET_DBID        dbid;           // JET_DBID of database corrupted
    JET_DBINFOMISC7 dbinfomisc;     // Database header for corrupted database
    uint32_t   pageNumber;     // That is corrupted
} JET_SNCORRUPTEDPAGE;
#endif // JET_VERSION >= 0x0A01

typedef struct
{
    uint32_t   cpageOwned;     //  number of owned pages in the streaming file
    uint32_t   cpageAvail;     //  number of available pages in the streaming file (subset of cpageOwned)
} JET_STREAMINGFILESPACEINFO;
#if ( JET_VERSION >= 0x0600 )
//  JET performance counters accumulated by thread
//
struct JET_THREADSTATS
{
    uint32_t   cbStruct;           //  size of this struct
    uint32_t   cPageReferenced;    //  pages referenced
    uint32_t   cPageRead;          //  pages read from disk
    uint32_t   cPagePreread;       //  pages preread from disk
    uint32_t   cPageDirtied;       //  clean pages modified
    uint32_t   cPageRedirtied;     //  dirty pages modified
    uint32_t   cLogRecord;         //  log records generated
    uint32_t   cbLogRecord;        //  log record bytes generated
};
#endif // JET_VERSION >= 0x0600

#if ( JET_VERSION >= 0x0A00 )
//  JET performance counters accumulated by thread
//
struct JET_THREADSTATS2
{
    uint32_t       cbStruct;               //  size of this struct
    uint32_t       cPageReferenced;        //  pages referenced
    uint32_t       cPageRead;              //  pages read from disk
    uint32_t       cPagePreread;           //  pages preread from disk
    uint32_t       cPageDirtied;           //  clean pages modified
    uint32_t       cPageRedirtied;         //  dirty pages modified
    uint32_t       cLogRecord;             //  log records generated
    uint32_t       cbLogRecord;            //  log record bytes generated
    uint64_t    cusecPageCacheMiss;     //  page cache miss latency in microseconds
    uint32_t       cPageCacheMiss;         //  page cache misses
};
#endif // JET_VERSION >= 0x0A00

#if ( JET_VERSION >= 0x0A01 )
//  JET performance counters accumulated by thread
//
struct JET_THREADSTATS3
{
    uint32_t       cbStruct;                       //  size of this struct
    uint32_t       cPageReferenced;                //  pages referenced
    uint32_t       cPageRead;                      //  pages read from disk
    uint32_t       cPagePreread;                   //  pages preread from disk
    uint32_t       cPageDirtied;                   //  clean pages modified
    uint32_t       cPageRedirtied;                 //  dirty pages modified
    uint32_t       cLogRecord;                     //  log records generated
    uint32_t       cbLogRecord;                    //  log record bytes generated
    uint64_t    cusecPageCacheMiss;             //  page cache miss latency in microseconds
    uint32_t       cPageCacheMiss;                 //  page cache misses
    uint32_t       cSeparatedLongValueRead;        //  separated LV reads
    uint64_t    cusecLongValuePageCacheMiss;    //  page cache miss latency in microseconds while reading separated LV data
    uint32_t       cLongValuePageCacheMiss;        //  page cache misses while reading separated LV data
};
#endif // JET_VERSION >= 0x0A01
#if ( JET_VERSION >= 0x0A01 )
//  JET performance counters accumulated by thread
//
struct JET_THREADSTATS4
{
    uint32_t       cbStruct;                           //  size of this struct
    uint32_t       cPageReferenced;                    //  pages referenced
    uint32_t       cPageRead;                          //  pages read from disk
    uint32_t       cPagePreread;                       //  pages preread from disk
    uint32_t       cPageDirtied;                       //  clean pages modified
    uint32_t       cPageRedirtied;                     //  dirty pages modified
    uint32_t       cLogRecord;                         //  log records generated
    uint32_t       cbLogRecord;                        //  log record bytes generated
    uint64_t    cusecPageCacheMiss;                 //  page cache miss latency in microseconds
    uint32_t       cPageCacheMiss;                     //  page cache misses
    uint32_t       cSeparatedLongValueRead;            //  separated LV reads
    uint64_t    cusecLongValuePageCacheMiss;        //  page cache miss latency in microseconds while reading separated LV data
    uint32_t       cLongValuePageCacheMiss;            //  page cache misses while reading separated LV data
    uint32_t       cSeparatedLongValueCreated;         //  separated LV creations
    uint32_t       cPageUniqueCacheHits;               //  number of unique pages for which requests could be fulfilled by the buffer cache
    uint32_t       cPageUniqueCacheRequests;           //  number of unique pages for which requests were made to the buffer cache
    uint32_t       cDatabaseReads;                     //  number of database reads from disk
    uint32_t       cSumDatabaseReadQueueDepthImpact;   //  sum of the impact on disk queue depth made by each database read from disk
    uint32_t       cSumDatabaseReadQueueDepth;         //  sum of the actual disk queue depths experienced by each database read from disk
    uint64_t    cusecWait;                          //  elapsed thread wait time in microseconds
    uint32_t       cWait;                              //  number of thread waits
    uint32_t       cNodesFlagDeleted;                  //  number of nodes marked for delete
    uint32_t       cbNodesFlagDeleted;                 //  size of nodes marked for delete
    uint32_t       cPageTableAllocated;                //  number of pages allocated by a table from the database
    uint32_t       cPageTableReleased;                 //  number of pages released by a table to the database
    uint32_t       cPageUpdateAllocated;               //  number of pages allocated as a side effect of an update
    uint32_t       cPageUpdateReleased;                //  number of pages released as a side effect of an update
    uint32_t       cPageUniqueModified;                //  number of unique pages modified
};
#endif // JET_VERSION >= 0x0A01

#if ( JET_VERSION >= 0x0603 )
//  Resources supported by the JET
//  NOTE: Instanceless resources are not bound to a specific instance
//

typedef enum
{
    JET_residNull,                  //  invalid (null) resource id
    JET_residFCB,                   //  tables
    JET_residFUCB,                  //  cursors
    JET_residTDB,                   //  table descriptors
    JET_residIDB,                   //  index descriptors
    JET_residPIB,                   //  sessions
    JET_residSCB,                   //  sorted tables
    JET_residVERBUCKET,             //  buckets used by Vesrion store
    JET_residPAGE,                  //  [deprecated] general purposes page usage (instanceless)
    JET_residSPLIT,                 //  split struct used by BTree page spilt (instanceless)
    JET_residSPLITPATH,             //  another split struct used by BTree page split (instanceless)
    JET_residMERGE,                 //  merge sruct used by BTree pages merge (instanceless)
    JET_residMERGEPATH,             //  another merge struct used by BTree page merge (instanceless)
    JET_residINST,                  //  instances (instanceless)
    JET_residLOG,                   //  log instances (instanceless)
    // JET_residVER,                //  WAS version store win8 and prior. Removed in Win8.1. Therefore JET_residKEY has a different numerical value for Win8 and Win8.1!
                                    //  Since we removed JET_residVER at some point in the past, if at some future time makes this public, it should be locked down to
                                    //  JET_VERSION 0xXxx that is as or more recent than the JET_residVER removal.
    JET_residKEY,                   //  key buffers (instanceless)
    JET_residBOOKMARK,              //  bookmark buffers (instanceless)
    JET_residLRUKHIST,              //  LRUK history records (instanceless)
    JET_residRBSBuf,                //  Revert snapshot buffers
    JET_residTest,                  //  internal use only: for testing the resource manager
    JET_residMax,
    JET_residAll = 0x0fffffff       //  special value to be used when a setting is to be applied to all known resources
} JET_RESID;

#endif // JET_VERSION >= 0x0603

#if ( JET_VERSION >= 0x0600 )

//  Operations related with JET resources management
//  NOTE: on get the instance param is ignored, unless other is said

typedef enum
{
    JET_resoperNull,
    JET_resoperTag,         //  Each object class has (unique) tag, get only
    JET_resoperSize,        //  The size of the object, get only
    JET_resoperAlign,       //  Alignmet of the object, Set before jetinit
    JET_resoperMinUse,      //  Min number of preallocated objects, Set before jetinit
    JET_resoperMaxUse,      //  Max number of allocated object, After jetinit is set per instance
                            //  Befre jetinit is set for the whole process
                            //      and is rounded up to the full chunk
                            //  the count is object based
    JET_resoperObjectsPerChunk, //  Get only
    JET_resoperCurrentUse,  //  Get only, with nonnull instance param the count is object based
                            //  with null instance param the count is chunks based
    JET_resoperChunkSize,   //  Set before jetinit only
    JET_resoperLookasideEntries,    // Set before jetinit only
    JET_resoperRFOLOffset,  //  offset where to store the pointer to next free object
                            //  set before jet init only
                            //  NOTE: Debug and JET dev only
    JET_resoperGuard,       //  Turn the guard page at the end of the section on and off
                            //  Set before jetinit only
    JET_resoperAllocTopDown,    //  chunks are allocated using MEM_TOP_DOWN
    JET_resoperPreserveFreed,   //  freed chunks are not decommitted/released back to the OS
    JET_resoperAllocFromHeap,   //  objects are allocated directly from the heap, bypassing resource management
    JET_resoperEnableMaxUse,    //  Enables the enforcement of the max number of allocated objects
    JET_resoperRCIList,         //  Expose m_pRCIList for debug extension
    JET_resoperSectionHeader,   //  Expose cbSectionHeader for debug extension
    JET_resoperMax
} JET_RESOPER;

#endif // JET_VERSION >= 0x0600
#if ( JET_VERSION >= 0x0600 )

typedef struct
{
    uint32_t           cbStruct;

    JET_RSTMAP_A *          rgrstmap;
    int32_t                    crstmap;

    JET_LGPOS               lgposStop;
    JET_LOGTIME             logtimeStop;

    JET_PFNSTATUS           pfnStatus;
} JET_RSTINFO_A;

typedef struct
{
    uint32_t           cbStruct;

    JET_RSTMAP_W *          rgrstmap;
    int32_t                    crstmap;

    JET_LGPOS               lgposStop;
    JET_LOGTIME             logtimeStop;

    JET_PFNSTATUS           pfnStatus;
} JET_RSTINFO_W;

#ifdef JET_UNICODE
typedef JET_RSTINFO_W JET_RSTINFO;
#else
typedef JET_RSTINFO_A JET_RSTINFO;
#endif

#endif // JET_VERSION >= 0x0600
#if ( JET_VERSION >= 0x0A01 )

typedef struct
{
    uint32_t           cbStruct;

    JET_RSTMAP2_A *         rgrstmap;
    int32_t                    crstmap;

    JET_LGPOS               lgposStop;
    JET_LOGTIME             logtimeStop;

    JET_PFNINITCALLBACK     pfnCallback;
    void *                  pvCallbackContext;
} JET_RSTINFO2_A;

typedef struct
{
    uint32_t           cbStruct;

    JET_RSTMAP2_W *         rgrstmap;
    int32_t                    crstmap;

    JET_LGPOS               lgposStop;
    JET_LOGTIME             logtimeStop;

    JET_PFNINITCALLBACK     pfnCallback;
    void *                  pvCallbackContext;
} JET_RSTINFO2_W;

#ifdef JET_UNICODE
typedef JET_RSTINFO2_W JET_RSTINFO2;
#else
typedef JET_RSTINFO2_A JET_RSTINFO2;
#endif

#endif // JET_VERSION >= 0x0A01

#if ( JET_VERSION >= 0x0601 )
typedef enum
{
    eBTreeTypeInvalid = 0,
    eBTreeTypeInternalDbRootSpace,
    eBTreeTypeInternalSpaceOE,
    eBTreeTypeInternalSpaceAE,
    eBTreeTypeUserClusteredIndex,
    eBTreeTypeInternalLongValue,
    eBTreeTypeUserSecondaryIndex,
    eBTreeTypeMax
} JET_BTREETYPE;

//
//  This explains the heirarchy of callbacks a database will generate:
//
//  o eBTreeTypeInternalDbRootSpace (not really a B-Tree)
//      o eBTreeTypeInternalSpaceOE (of DB root)
//      o eBTreeTypeInternalSpaceAE (of DB root)
//      o eBTreeTypeUserClusteredIndex x N times (i.e. user defined tables)
//          o eBTreeTypeInternalSpaceOE (optional)
//          o eBTreeTypeInternalSpaceAE (optional)
//          o eBTreeTypeInternalLongValue (optional)
//              o eBTreeTypeInternalSpaceOE (optional)
//              o eBTreeTypeInternalSpaceAE (optional)
//          o eBTreeTypeUserSecondaryIndex x M times (i.e. user defined indices)
//              o eBTreeTypeInternalSpaceOE (optional)
//              o eBTreeTypeInternalSpaceAE (optional)
//      o ...more eBTreeTypeUserClusteredIndex
//          o eBTreeTypeInternalSpaceOE (optional)
//          o eBTreeTypeInternalSpaceAE (optional)
//          o eBTreeTypeInternalLongValue (optional)
//              o eBTreeTypeInternalSpaceOE (optional)
//              o eBTreeTypeInternalSpaceAE (optional)
//          o ...more eBTreeTypeUserSecondaryIndex
//              o eBTreeTypeInternalSpaceOE (optional)
//              o eBTreeTypeInternalSpaceAE (optional)
//
// Note: When we say user defined tables and indices, its just where they would get
// called, but the callbacks for the BTree types will happen for system tables as
// well such as the catalog, defrag table, etc.
//

//  Retrieved with JET_bitSpaceInfoBasicCatalog
//
typedef struct _BTREE_STATS_BASIC_CATALOG
{
    uint32_t                   cbStruct;
    JET_BTREETYPE                   eType;
    char16_t                           rgName[64];
    uint32_t                   objidFDP;
    uint32_t                   pgnoFDP;
    JET_SPACEHINTS *            pSpaceHints;
} BTREE_STATS_BASIC_CATALOG;

typedef struct _BTREE_SPACE_EXTENT_INFO
{
    uint32_t                   iPool;
    uint32_t                   pgnoLast;
    uint32_t                   cpgExtent;
    uint32_t                   pgnoSpaceNode;
} BTREE_SPACE_EXTENT_INFO;

//  Retrieved with JET_bitSpaceInfoSpaceTrees
//
typedef struct _BTREE_STATS_SPACE_TREES
{
    uint32_t                   cbStruct;
    uint32_t                   cpgPrimary;
    uint32_t                   cpgLastAlloc;
    uint32_t                   fMultiExtent;
    uint32_t                   pgnoOE;
    uint32_t                   pgnoAE;
    uint32_t                   cpgOwned;
    uint32_t                   cpgOwnedCache;
    uint32_t                   cpgAvailable;
    uint32_t                   cpgAvailableCache;
    uint32_t                   cpgSpaceTreeAvailable;
    uint32_t                   cpgReserved;
    uint32_t                   cpgShelved;
    int                             fAutoIncPresents;
    uint64_t                qwAutoInc;
    uint32_t                   cOwnedExtents;
    BTREE_SPACE_EXTENT_INFO *       prgOwnedExtents;
    uint32_t                   cAvailExtents;
    BTREE_SPACE_EXTENT_INFO *       prgAvailExtents;
} BTREE_STATS_SPACE_TREES;

//  Retrieved with JET_bitSpaceInfoFullWalk for data page.
//
typedef struct
{
    uint32_t                   cbStruct;
    JET_HISTO *                     phistoFreeBytes;            // per page
    JET_HISTO *                     phistoNodeCounts;           // per page (not including TAG 0)
    JET_HISTO *                     phistoKeySizes;             // per node
    JET_HISTO *                     phistoDataSizes;            // per node
    JET_HISTO *                     phistoKeyCompression;       // per compressed node
    JET_HISTO *                     phistoResvTagSizes;         // per reserved tag
    JET_HISTO *                     phistoUnreclaimedBytes;     // per deleted node
#if ( JET_VERSION >= 0x0602 )
    int64_t                         cVersionedNodes;            // node accumulation
#endif
} BTREE_STATS_PAGE_SPACE;

//  Retrieved with JET_bitSpaceInfoParentOfLeaf
//

//  Some idea of some edge cases for this structure ...
//
//  Assumes 8 KB page size, here is some of the simple boundary cases...
//
//                  fEmpty  cDepth  cpgIntr cpgData Owned   Avail
//  DbRoot (4MB)    fFalse  0       0       0       512     34
//  -- tbl | idx --
//  no data         fTrue   1       0       1       1       0
//  1 small row     fFalse  1       0       1       1       0
//  5- 7.8k rows    fFalse  2       1       5       8       3
//      (w/8 pg PriExt)
//  -- oe | ae
//  OE:1-pg,2-node  fFalse? 1       0       1       1       0
//  AE:1-pg,0-node  fTrue?  1       0       1       1       0
//  AE:1-pg,1-node  fFalse? 1       0       1       1       0       0 avail, inspite of fact it represents avail pages.
//  OE:3-pg,many    fFalse? 2       1       2       3       0
typedef struct _BTREE_STATS_PARENT_OF_LEAF
{
    uint32_t                   cbStruct;
    uint32_t                   fEmpty;
    uint32_t                   cpgInternal;
    uint32_t                   cpgData;
    uint32_t                   cDepth;
    JET_HISTO *                     phistoIOContiguousRuns;
    uint32_t                   cForwardScans;
    BTREE_STATS_PAGE_SPACE *        pInternalPageStats;
} BTREE_STATS_PARENT_OF_LEAF;

#if ( JET_VERSION >= 0x0602 )

typedef struct _BTREE_STATS_LV
{
    uint32_t                   cbStruct;
    int64_t                         cLVRefs;
    int64_t                         cCorruptLVs;
    int64_t                         cSeparatedRootChunks;
    int64_t                         cPartiallyDeletedLVs;
    uint64_t                lidMax;
    int                             cbLVChunkMax;
    JET_HISTO *                     phistoLVSize;
    JET_HISTO *                     phistoLVComp;
    JET_HISTO *                     phistoLVRatio;
    JET_HISTO *                     phistoLVSeeks;
    JET_HISTO *                     phistoLVBytes;
    JET_HISTO *                     phistoLVExtraSeeks;
    JET_HISTO *                     phistoLVExtraBytes;
} BTREE_STATS_LV;

#endif

typedef struct _BTREE_STATS
{
    //
    //  Version and specified data.
    //
    uint32_t                   cbStruct;
    uint32_t                   grbitData;
    //
    //  ESE's B+ Trees / space are heirarchical.
    //
    struct _BTREE_STATS *           pParent;
    //
    //  Broken out data, by amount of effort.
    //
    BTREE_STATS_BASIC_CATALOG *     pBasicCatalog;
    BTREE_STATS_SPACE_TREES *       pSpaceTrees;
    BTREE_STATS_PARENT_OF_LEAF *    pParentOfLeaf;
    BTREE_STATS_PAGE_SPACE *        pFullWalk;
#if ( JET_VERSION >= 0x0602 )
    BTREE_STATS_LV *                pLvData;
#endif
    uint32_t                   fPgnoFDPRootDelete;
} BTREE_STATS;

typedef JET_ERR (JET_API *JET_PFNSPACEDATA)(
    BTREE_STATS *      pBTreeStats,
    JET_API_PTR        pvContext );
#endif // JET_VERSION >= 0x0601

//typedef struct
//  {
//  ulong   cDiscont;
//  ulong   cUnfixedMessyPage;
//  ulong   centriesLT;
//  ulong   centriesTotal;
//  ulong   cpgCompactFreed;
//  } JET_OLCSTAT;
#if ( JET_VERSION >= 0x0602 )

// JET_errcatError
//    |
//    |-- JET_errcatOperation
//    |     |-- JET_errcatFatal
//    |     |-- JET_errcatIO                //  bad IO issues, may or may not be transient.
//    |     |-- JET_errcatResource
//    |           |-- JET_errcatMemory      //  out of memory (all variants)
//    |           |-- JET_errcatQuota
//    |           |-- JET_errcatDisk        //  out of disk space (all variants)
//    |-- JET_errcatData
//    |     |-- JET_errcatCorruption
//    |     |-- JET_errcatInconsistent      //  typically caused by user Mishandling
//    |     |-- JET_errcatFragmentation
//    |-- JET_errcatApi
//          |-- JET_errcatUsage
//          |-- JET_errcatState
// The above hierarchy is represented in errorhierarchy.cxx as rgerrorHierarchies.
// Both places need to be updated together.
// A brief description of each error type
//
//  Operation(al) - Errors that can usually happen any time due to uncontrollable
//                  conditions.  Frequently temporary, but not always.
//
//                  Recovery: Probably retry, or eventually inform the operator.
//
//      Fatal -     This sort error happens only when ESE encounters an error condition
//                  so grave, that we can not continue on in a safe (often transactional)
//                  way, and rather than corrupt data we throw errors of this category.
//
//                  Recovery: Restart the instance or process.  If the problem persists
//                  inform the operator.
//
//      IO -        IO errors come from the OS, and are out of ESE's control, this sort
//                  of error is possibly temporary, possibly not.
//
//                  Recovery: Retry.  If not resolved, ask operator about disk issue.
//
//      Resource -  This is a category that indicates one of many potential out-of-resource
//                  conditions.
//
//          Memory  Classic out of memory condition.
//
//                  Recovery: Wait a while and retry, free up memory, or quit.
//
//          Quota   Certain "specialty" resources are in pools of a certain size, making
//                  it easier to detect leaks of these resources.
//
//                  Recovery: Bug fix, generally the application should Assert() on these
//                  conditions so as to detect these issues during development.  However,
//                  in retail code, the best to hope for is to treat like Memory.
//
//          Disk    Out of disk conditions.
//
//                  Recovery: Can retry later in the hope more space is available, or
//                  ask the operator to free some disk space.
//  Data
//
//      Corruption  My hard drive ate my homework.  Classic corruption issues, frequently
//                  permanent without corrective action.
//
//                  Recovery: Restore from backup, perhaps the ese utilities repair
//                  operation (which only salvages what data is left / lossy).  Also
//                  in the case of recovery(JetInit) perhaps recovery can be performed
//                  by allowing data loss.
//
//      Inconsistent This is similar to Corruption in that the database and/or log files
//                  are in a state that is inconsistent and unreconcilable with each
//                  other. Often this is caused by application/administrator mishandling.
//
//                  Recovery: Restore from backup, perhaps the ese utilities repair
//                  operation (which only salvages what data is left / lossy).  Also
//                  in the case of recovery(JetInit) perhaps recovery can be performed
//                  by allowing data loss.
//
//      Fragmentation   This is a class of errors where some persisted internal resource ran
//                  out.
//
//                  Recovery: For database errors, offline defragmentation will rectify
//                  the problem, for the log files _first_ recover all attached databases
//                  to a clean shutdown, and then delete all the log files and checkpoint.
//
//  Api
//
//      Usage       Classic usage error, this means the client code did not pass correct
//                  arguments to the JET API.  This error will likely not go away with
//                  retry.
//
//                  Recovery: Generally speaking client code should Assert() this class
//                  of errors is not returned, so issues can be caught during development.
//                  In retail, the app will probably have little option but to return
//                  the issue up to the operator.
//
//      State       This is the classification for different signals the API could return
//                  describe the state of the database, a classic case is JET_errRecordNotFound
//                  which can be returned by JetSeek() when the record you asked for
//                  was not found.
//
//                  Recovery: Not really relevant, depends greatly on the API.
//

typedef enum
{
    JET_errcatUnknown = 0,  //      unknown, error retrieving err category
    JET_errcatError,        //      top level (no errors should be of this class)
    JET_errcatOperation,
    JET_errcatFatal,
    JET_errcatIO,           //      bad IO issues, may or may not be transient.
    JET_errcatResource,
    JET_errcatMemory,       //      out of memory (all variants)
    JET_errcatQuota,
    JET_errcatDisk,         //      out of disk space (all variants)
    JET_errcatData,
    JET_errcatCorruption,
    JET_errcatInconsistent,
    JET_errcatFragmentation,
    JET_errcatApi,
    JET_errcatUsage,
    JET_errcatState,
    JET_errcatObsolete,
    JET_errcatMax,
} JET_ERRCAT;

// Output structure for JetGetErrorInfoW(). Not all fields may
// be populated by all error levels.
typedef struct
{
    uint32_t       cbStruct;
    JET_ERR             errValue;                   //  The error value for the requested info level.
    JET_ERRCAT          errcatMostSpecific;         //  The most specific category of the error.
    unsigned char       rgCategoricalHierarchy[8];  //  Hierarchy of error categories. Position 0 is the highest level in the hierarchy, and the rest are JET_errcatUnknown.
    uint32_t       lSourceLine;                //  The source file line for the requested info level.
    char16_t               rgszSourceFile[64];         //  The source file name for the requested info level.
} JET_ERRINFOBASIC_W;

// grbits for JET_PFNDURABLECOMMITCALLBACK
#if ( JET_VERSION >= 0x0A00 )
#define JET_bitDurableCommitCallbackLogUnavailable      0x00000001  // Passed back to durable commit callback to let it know that log is down (and all pending commits will not be flushed to disk)
#endif

// commit-id from JetCommitTransaction2
typedef struct
{
    JET_SIGNATURE   signLog;
    int             reserved; // for packing so int64 below is 8-byte aligned on 32-bits despite the pshpack4 above
    int64_t         commitId;
} JET_COMMIT_ID;

// assert that commit-id is 8-byte aligned so managed interop works correctly
// C_ASSERT( offsetof( JET_COMMIT_ID, commitId ) % 8 == 0 );

// callback for JET_paramDurableCommitCallback
typedef JET_ERR (JET_API *JET_PFNDURABLECOMMITCALLBACK)(
    JET_INSTANCE   instance,
    JET_COMMIT_ID *pCommitIdSeen,
    JET_GRBIT      grbit );

#endif // JET_VERSION >= 0x0602
typedef struct
{
    int32_t                    lRBSGeneration;             //  Revert snapshot generation.

    JET_LOGTIME             logtimeCreate;              //  date time file creation
    JET_LOGTIME             logtimeCreatePrevRBS;       //  date time prev file creation

    uint32_t           ulMajor;                    //  major version number
    uint32_t           ulMinor;                    //  minor version number

    uint64_t      cbLogicalFileSize;          //  Logical file size
} JET_RBSINFOMISC;

typedef struct
{
    int32_t                    lGenMinRevertStart;         // Min log generation across databases at start of revert.
    int32_t                    lGenMaxRevertStart;         // Max log generation across databases at start of revert.

    int32_t                    lGenMinRevertEnd;           // Min log generation across databases at end of revert.
    int32_t                    lGenMaxRevertEnd;           // Max log generation across databases at end of revert.

    JET_LOGTIME             logtimeRevertFrom;          // The time we started reverting from. We will skip adding reverting to time as the caller already gets that info as part of prepare call.

    uint64_t      cSecRevert;                 // Total secs spent in revert process.
    uint64_t      cPagesReverted;             // Total pages reverted across all the database files as part of the revert.

    int32_t                    lGenRBSMaxApplied;          // Max revert snapshot generation applied during revert.
    int32_t                    lGenRBSMinApplied;          // Min revert snapshot generation applied during revert.
} JET_RBSREVERTINFOMISC;
/************************************************************************/
/*************************     JET CONSTANTS     ************************/
/************************************************************************/

#if ( JET_VERSION >= 0x0501 )
#define JET_instanceNil             (~(JET_INSTANCE)0)
#endif // JET_VERSION >= 0x0501
#define JET_sesidNil                (~(JET_SESID)0)
#define JET_tableidNil              (~(JET_TABLEID)0)
#define JET_columnidNil             (~(JET_COLUMNID)0)
#define JET_bitNil                  ((JET_GRBIT)0)

    /* Max size of a bookmark */

#define JET_cbBookmarkMost          256
#if ( JET_VERSION >= 0x0601 )
#define JET_cbBookmarkMostMost      JET_cbKeyMostMost
#endif // JET_VERSION >= 0x0601

    /* Max length of a object/column/index/property name */

#ifndef JET_UNICODE
#define JET_cbNameMost              64
#else
#define JET_cbNameMost              128
#endif

    /* Max length of a "name.name.name..." construct */

#ifndef JET_UNICODE
#define JET_cbFullNameMost          255
#else
#define JET_cbFullNameMost          510
#endif

    /* Max size of long-value (LongBinary or LongText) column chunk */

//  #define JET_cbColumnLVChunkMost     ( JET_cbPage - 82 ) to the following:
//  Get cbPage from GetSystemParameter.
//  changed JET_cbColumnLVChunkMost reference to cbPage - JET_cbColumnLVPageOverhead

#define JET_cbColumnLVPageOverhead      82      // ONLY for small (<=8kiB) page, otherwise, query JET_paramLVChunkSizeMost
#define JET_cbColumnLVChunkMost     ( 4096 - 82 ) // This def will be removed after other components change not to use this def
#define JET_cbColumnLVChunkMost_OLD 4035
    /* Max size of long-value (LongBinary or LongText) column default value */

#define JET_cbLVDefaultValueMost    255

    /* Max size of non-long-value column data */

#define JET_cbColumnMost            255

    /* Max size of long-value column data. */

#define JET_cbLVColumnMost          0x7FFFFFFF

    /* Max size of a sort/index key */

#if ( JET_VERSION >= 0x0601 )
#define JET_cbKeyMostMost               JET_cbKeyMost32KBytePage
#define JET_cbKeyMost32KBytePage        JET_cbKeyMost8KBytePage
#define JET_cbKeyMost16KBytePage        JET_cbKeyMost8KBytePage
#endif // JET_VERSION >= 0x0601
#if ( JET_VERSION >= 0x0600 )
#define JET_cbKeyMost8KBytePage     2000
#define JET_cbKeyMost4KBytePage     1000
#define JET_cbKeyMost2KBytePage     500
#define JET_cbKeyMostMin            255
#endif // JET_VERSION >= 0x0600

#define JET_cbKeyMost               255     //  defunct constant retained for backward compatibility
#define JET_cbLimitKeyMost          256     //  defunct constant retained for backward compatibility
#define JET_cbPrimaryKeyMost        255     //  defunct constant retained for backward compatibility
#define JET_cbSecondaryKeyMost      255     //  defunct constant retained for backward compatibility
#define JET_cbKeyMost_OLD           255
    /* Max number of components in a sort/index key */

#if ( JET_VERSION >= 0x0600 )
#define JET_ccolKeyMost             16
#else // !JET_VERSION >= 0x0600
#define JET_ccolKeyMost             12
#endif // !JET_VERSION >= 0x0600

//  maximum number of columns
#if ( JET_VERSION >= 0x0501 )
#define JET_ccolMost                0x0000fee0
#else // !JET_VERSION >= 0x0501
#define JET_ccolMost                0x00007ffe
#endif // !JET_VERSION >= 0x0501
#define JET_ccolFixedMost           0x0000007f
#define JET_ccolVarMost             0x00000080
#define JET_ccolTaggedMost          ( JET_ccolMost - 0x000000ff )

#if ( JET_VERSION >= 0x0501 )
#define JET_EventLoggingDisable     0
#if ( JET_VERSION >= 0x0601 )
#define JET_EventLoggingLevelMin    1
#define JET_EventLoggingLevelLow    25
#define JET_EventLoggingLevelMedium 50
#define JET_EventLoggingLevelHigh   75
#endif // JET_VERSION >= 0x0601
#define JET_EventLoggingLevelMax    100
#endif // JET_VERSION >= 0x0501

#if ( JET_VERSION >= 0x0603 )
// Values for JET_paramEnableIndexChecking.
typedef enum
{
    JET_IndexCheckingOff = 0,
    JET_IndexCheckingOn = 1,
    JET_IndexCheckingDeferToOpenTable = 2,
    JET_IndexCheckingMax = 3,
} JET_INDEXCHECKING;
#endif
// The following values are bit-fields that JET_paramIOPriority can be set to
#if ( JET_VERSION >= 0x0600 )
// Values for JET_paramIOPriority
#define JET_IOPriorityNormal                    0x0       // default
#define JET_IOPriorityLow                       0x1
#endif // JET_VERSION >= 0x0600
#if ( JET_VERSION >= 0x0603 )
#define JET_IOPriorityLowForCheckpoint          0x2
#define JET_IOPriorityLowForScavenge            0x4

// Diagnostic IO Priority Flags - No behavioral effects.
#define JET_IOPriorityUserClassIdMask           0x0F000000  //  User Level Priority Class ID.  Only used for tracing, produces no ESE behavioral changes.
#define JET_IOPriorityMarkAsMaintenance         0x40000000  //  Identifies the IO activity produced by this session as Maintenance (vs. Transactional) for perfmon.  Only used for perfmon, produces no ESE behavioral changes.
#endif // JET_VERSION >= 0x0603
#if ( JET_VERSION >= 0x0602 )
//  Values for usage with JET_paramConfiguration
//
//  Can set the optimization configs one at a time.
//
#define JET_configDefault                       0x0001  //  Resets ALL parameters to their default value
#define JET_configRemoveQuotas                  0x0002  //  Unrestricts the quota enforcement (by setting to as high as possible) for any ESE handle types where memory is not pre-allocated or used as a cache.
#define JET_configLowDiskFootprint              0x0004  //  Set appropriate parameters to optimize the engine to use a small amount of disk space.  Uses circular logging.
#define JET_configMediumDiskFootprint           0x0008  //  Set appropriate parameters to optimize the engine to use a medium amount of disk space.  Uses circular logging.
#define JET_configLowMemory                     0x0010  //  Set appropriate parameters to optimize the engine to use a small amount of memory/working set at the cost of CPU efficiency and some disk efficiency.
#define JET_configDynamicMediumMemory           0x0020  //  Set appropriate parameters to optimize the engine to use a modest amount of memory/working set at the cost of CPU efficiency, dynamically adjusting for bursts in activity.
#define JET_configLowPower                      0x0040  //  Set appropriate parameters to optimize the engine to attempt to conserve power over keeping everything the most up to date, or memory usage.
#define JET_configSSDProfileIO                  0x0080  //  Set appropriate parameters to optimize the engine to be using the SSD profile IO parameters.
#define JET_configRunSilent                     0x0100  //  Turns off all externally visible signs of the library running (event logs, perfmon, tracing, etc).  NOTE: This makes debugging issues difficult, best if app policy has way to configure this off or on.
#if ( JET_VERSION >= 0x0A00 )
#define JET_configUnthrottledMemory             0x0200  //  Allows ESE to grow to most of memory because this is likely a single purpose server for this machine, or wants to allow our variable memory caches to grow to use most of memory if in use.
#define JET_configHighConcurrencyScaling        0x0400  //  Ensures ESE uses all its high concurrency scaling methods to achieve high levels of performance on multi-CPU systems (SMP, Multi-Core, Hyper-Threading, etc) for server scale applications, at a higher fixed memory overhead.
#endif // JET_VERSION >= 0x0A00
#define JET_configMask                          (JET_configDefault|JET_configRemoveQuotas|JET_configLowDiskFootprint|JET_configMediumDiskFootprint|JET_configLowMemory|JET_configDynamicMediumMemory|JET_configLowPower|JET_configSSDProfileIO|JET_configRunSilent|JET_configUnthrottledMemory|JET_configHighConcurrencyScaling)
#endif // JET_VERSION >= 0x0602

//  system parameters
//
//  NOTE:  the default values of these parameters used to be documented here.
//  this can no longer be done because we now support multiple sets of default
//  values as set by JET_paramConfiguration
//
//  location parameters
//
#define JET_paramSystemPath                     0   //  path to check point file
#define JET_paramTempPath                       1   //  path to the temporary database
#define JET_paramLogFilePath                    2   //  path to the log file directory
#define JET_paramBaseName                       3   //  base name for all DBMS object names
#define JET_paramEventSource                    4   //  language independent process descriptor string

//  performance parameters
//
#define JET_paramMaxSessions                    5   //  maximum number of sessions
#define JET_paramMaxOpenTables                  6   //  maximum number of open directories
                                                    //      need 1 for each open table index,
                                                    //      plus 1 for each open table with no indexes,
                                                    //      plus 1 for each table with long column data,
                                                    //      plus a few more.
                                                    //      for 4.1, 1/3 for regular table, 2/3 for index
#define JET_paramPreferredMaxOpenTables         7   //  preferred maximum number of open directories
#if ( JET_VERSION >= 0x0600 )
#define JET_paramCachedClosedTables             125 //  number of closed tables to cache the meta-data for
#endif // JET_VERSION >= 0x0600
#define JET_paramMaxCursors                     8   //  maximum number of open cursors
#define JET_paramMaxVerPages                    9   //  maximum version store size in version pages
#define JET_paramPreferredVerPages              63  //  preferred version store size in version pages
#if ( JET_VERSION >= 0x0501 )
#define JET_paramGlobalMinVerPages              81  //  minimum version store size for all instances in version pages
#define JET_paramVersionStoreTaskQueueMax       105 //  maximum number of tasks in the task queue before start dropping the tasks
#endif // JET_VERSION >= 0x0501
#define JET_paramMaxTemporaryTables             10  //  maximum concurrent open temporary table/index creation
#define JET_paramLogFileSize                    11  //  log file size in kBytes
#define JET_paramLogBuffers                     12  //  log buffers in 512 byte units.
#define JET_paramWaitLogFlush                   13  //  log flush wait time in milliseconds
#define JET_paramLogCheckpointPeriod            14  //  checkpoint period in sectors
#define JET_paramLogWaitingUserMax              15  //  maximum sessions waiting log flush
#define JET_paramCommitDefault                  16  //  default grbit for JetCommitTransaction
#define JET_paramCircularLog                    17  //  boolean flag for circular logging
#define JET_paramDbExtensionSize                18  //  database extension size in pages
#define JET_paramPageTempDBMin                  19  //  minimum size temporary database in pages
#define JET_paramPageFragment                   20  //  maximum disk extent considered fragment in pages
#define JET_paramPageReadAheadMax               21  //  maximum read-ahead in pages
#if ( JET_VERSION >= 0x0600 )
#define JET_paramEnableFileCache                126 //  enable the use of the OS file cache for all managed files
#define JET_paramVerPageSize                    128 //  the version store page size
#define JET_paramConfiguration                  129 //  RESETs all parameters to their default for a given configuration
#define JET_paramEnableAdvanced                 130 //  enables the modification of advanced settings
#define JET_paramMaxColtyp                      131 //  maximum coltyp supported by this version of ESE
#endif // JET_VERSION >= 0x0600

//  cache performance parameters
//
#define JET_paramBatchIOBufferMax               22  //  maximum batch I/O buffers in pages
#define JET_paramCacheSize                      41  //  current cache size in pages
#define JET_paramCacheSizeMin                   60  //  minimum cache size in pages
#define JET_paramCacheSizeMax                   23  //  maximum cache size in pages
#define JET_paramCheckpointDepthMax             24  //  maximum checkpoint depth in bytes
#define JET_paramLRUKCorrInterval               25  //  time (usec) under which page accesses are correlated
#define JET_paramLRUKHistoryMax                 26  //  maximum LRUK history records
#define JET_paramLRUKPolicy                     27  //  K-ness of LRUK page eviction algorithm (1...2)
#define JET_paramLRUKTimeout                    28  //  time (sec) after which cached pages are always evictable
#define JET_paramLRUKTrxCorrInterval            29  //  Not Used: time (usec) under which page accesses by the same transaction are correlated
#define JET_paramOutstandingIOMax               30  //  maximum outstanding I/Os
#define JET_paramStartFlushThreshold            31  //  evictable pages at which to start a flush (proportional to CacheSizeMax)
#define JET_paramStopFlushThreshold             32  //  evictable pages at which to stop a flush (proportional to CacheSizeMax)
#define JET_paramTableClassName                 33  //  table stats class name (class #, string)
#define JET_paramIdleFlushTime                  106 //  time interval (msec) over which all dirty pages should be written to disk after idle conditions are detected.
#define JET_paramVAReserve                      109 //  amount of address space (bytes) to reserve from use by the cache
#define JET_paramDBAPageAvailMin                120 //  limit of VM pages available at which NT starts to page out processes
#define JET_paramDBAPageAvailThreshold          121 //  constant used in internal calculation of page eviction rate *THIS IS A DOUBLE, PASS A POINTER*
#define JET_paramDBAK1                          122 //  constant used in internal DBA calculation *THIS IS A DOUBLE, PASS A POINTER*
#define JET_paramDBAK2                          123 //  constant used in internal DBA calculation *THIS IS A DOUBLE, PASS A POINTER*
#define JET_paramMaxRandomIOSize                124 //  maximum size of scatter/gather I/O in bytes
#if ( JET_VERSION >= 0x0600 )
#define JET_paramEnableViewCache                127 //  enable the use of memory mapped file I/O for database files
#define JET_paramCheckpointIOMax                135 //  maxiumum number of pending flush writes
#endif // JET_VERSION >= 0x0600

#if ( JET_VERSION >= 0x0600 )
// TableClass names
#define JET_paramTableClass1Name                137     // name of tableclass1
#define JET_paramTableClass2Name                138     // name of tableclass2
#define JET_paramTableClass3Name                139     // name of tableclass3
#define JET_paramTableClass4Name                140     // name of tableclass4
#define JET_paramTableClass5Name                141     // name of tableclass5
#define JET_paramTableClass6Name                142     // name of tableclass6
#define JET_paramTableClass7Name                143     // name of tableclass7
#define JET_paramTableClass8Name                144     // name of tableclass8
#define JET_paramTableClass9Name                145     // name of tableclass9
#define JET_paramTableClass10Name               146     // name of tableclass10
#define JET_paramTableClass11Name               147     // name of tableclass11
#define JET_paramTableClass12Name               148     // name of tableclass12
#define JET_paramTableClass13Name               149     // name of tableclass13
#define JET_paramTableClass14Name               150     // name of tableclass14
#define JET_paramTableClass15Name               151     // name of tableclass15
#endif // JET_VERSION >= 0x0600
#if ( JET_VERSION >= 0x0A01 )
// TableClass names (continued)
#define JET_paramTableClass16Name               196     // name of tableclass16
#define JET_paramTableClass17Name               197     // name of tableclass17
#define JET_paramTableClass18Name               198     // name of tableclass18
#define JET_paramTableClass19Name               199     // name of tableclass19
#define JET_paramTableClass20Name               200     // name of tableclass20
#define JET_paramTableClass21Name               201     // name of tableclass21
#define JET_paramTableClass22Name               202     // name of tableclass22
#define JET_paramTableClass23Name               203     // name of tableclass23
#define JET_paramTableClass24Name               204     // name of tableclass24
#define JET_paramTableClass25Name               205     // name of tableclass25
#define JET_paramTableClass26Name               206     // name of tableclass26
#define JET_paramTableClass27Name               207     // name of tableclass27
#define JET_paramTableClass28Name               208     // name of tableclass28
#define JET_paramTableClass29Name               209     // name of tableclass29
#define JET_paramTableClass30Name               210     // name of tableclass30
#define JET_paramTableClass31Name               211     // name of tableclass31
#endif // JET_VERSION >= 0x0A01
#define JET_paramIOPriority                     152     //  adjust IO priority per instance, anytime. Mainly for background recovery
                                                        //  Doesn't affect pending IOs, just subsequent ones

#define JET_paramRecovery                       34  //  enable recovery via setting the string "On" or "Off"
#define JET_paramOnLineCompact                  35  //  enable online defrag
#define JET_paramEnableOnlineDefrag             35  //  enable online defrag
//  debug only parameters
//
#define JET_paramAssertAction                   36  //  action on assert
#define JET_paramPrintFunction                  37  //  synched print function
#define JET_paramTransactionLevel               38  //  transaction level of session [deprecated - use JetGetSessionParameter( sesid, JET_sesparamTransactionLevel ... )]
#define JET_paramRFS2IOsPermitted               39  //  #IOs permitted to succeed [-1 = all]
#define JET_paramRFS2AllocsPermitted            40  //  #allocs permitted to success [-1 = all]
//                                              41  //  JET_paramCacheSize defined above
#define JET_paramCacheRequests                  42  //  #cache requests (Read Only)
#define JET_paramCacheHits                      43  //  #cache hits (Read Only)
//  Application specific parameter
//
#define JET_paramCheckFormatWhenOpenFail        44  //  JetInit may return JET_errDatabaseXXXformat instead of database corrupt when it is set
#define JET_paramEnableTempTableVersioning      46  //  Enable versioning of temp tables
#define JET_paramIgnoreLogVersion               47  //  Do not check the log version
#define JET_paramDeleteOldLogs                  48  //  Delete the log files if the version is old, after deleting may make database non-recoverable
#define JET_paramEventSourceKey                 49  //  Event source registration key value
#define JET_paramNoInformationEvent             50  //  Disable logging information event
#if ( JET_VERSION >= 0x0501 )
#define JET_paramEventLoggingLevel              51  //  Set the type of information that goes to event log
#define JET_paramDeleteOutOfRangeLogs           52  //  Delete the log files that are not matching (generation wise) during soft recovery
#define JET_paramAccessDeniedRetryPeriod        53  //  Number of milliseconds to retry when about to fail with AccessDenied
#endif // JET_VERSION >= 0x0501

//  Index-checking parameters
//
//  After Windows 7, it was discovered that JET_paramEnableIndexCleanup had some implementation limitations, reducing its effectiveness.
//  Rather than update it to work with locale names, the functionality is removed altogether.
//
//  Unfortunately JET_paramEnableIndexCleanup can not be ignored altogether. JET_paramEnableIndexChecking defaults to false, so if
//  JET_paramEnableIndexCleanup were to be removed entirely, then by default there were would be no checks for NLS changes!
//
//  The current behavious (when enabled) is to track the language sort versions for the indices, and when the sort version for that
//  particular locale changes, the engine knows which indices are now invalid. For example, if the sort version for only "de-de" changes,
//  then the "de-de" indices are invalid, but the "en-us" indices will be fine.
//
//  Post-Windows 8:
//  JET_paramEnableIndexChecking accepts JET_INDEXCHECKING (which is an enum). The values of '0' and '1' have the same meaning as before,
//  but '2' is JET_IndexCheckingDeferToOpenTable, which means that the NLS up-to-date-ness is NOT checked when the database is attached.
//  It is deferred to JetOpenTable(), which may now fail with JET_errPrimaryIndexCorrupted or JET_errSecondaryIndexCorrupted (which
//  are NOT actual corruptions, but instead reflect an NLS sort change).
//
//  IN SUMMARY:
//  New code should explicitly set both IndexChecking and IndexCleanup to the same value.
//
//
//  OLDER NOTES (up to and including Windows 7)
//
//  Different versions of windows normalize unicode text in different ways. That means indexes built under one version of Windows may
//  not work on other versions. Windows Server 2003 Beta 3 introduced GetNLSVersion() which can be used to determine the version of unicode normalization
//  that the OS currently provides. Indexes built in server 2003 are flagged with the version of unicode normalization that they were
//  built with (older indexes have no version information). Most unicode normalization changes consist of adding new characters -- codepoints
//  which were previously undefined are defined and normalize differently. Thus, if binary data is stored in a unicode column it will normalize
//  differently as new codepoints are defined.
//
//  As of Windows Server 2003 RC1 ESENT tracks unicode index entries that contain undefined codepoints. These can be used to fixup an index when the
//  set of defined unicode characters changes.
//
//  These parameters control what happens when ESENT attaches to a database that was last used under a different build of the OS (the OS version
//  is stamped in the database header).
//
//  If JET_paramEnableIndexChecking is TRUE JetAttachDatabase() will delete indexes if JET_bitDbDeleteCorruptIndexes or return an error if
//  the grbit was not specified and there are indexes which need deletion. If it is set to FALSE then JetAttachDatabase() will succeed, even
//  if there are potentially corrupt indexes.
//
//  If JET_paramEnableIndexCleanup is set, the internal fixup table will be used to fixup index entries. This may not fixup all index corruptions
//  but will be transparent to the application.
//

#define JET_paramEnableIndexChecking            45  //  Enable checking OS version for indexes (false by default).
#if ( JET_VERSION >= 0x0502 )
#define JET_paramEnableIndexCleanup             54  //  Enable cleanup of out-of-date index entries (Windows 2003 through Windows 7); Does NLS version checking (Windows 2003 and later).
#endif // JET_VERSION >= 0x0502
#if ( JET_VERSION >= 0x0A01 )
#define JET_paramFlight_SmoothIoTestPermillage  55  //  The per mille of total (or one thousandths, or tenths of a percent) of IO should be made smooth.  Ex(s): 995(/1000) = 99.5% smooth, 10(/1000) = 1%, etc.  0 = disabled.
#define JET_paramElasticWaypointLatency         56  //  Amount of extra elastic waypoint latency
#define JET_paramFlight_SynchronousLVCleanup    57  //  Perform synchronous cleanup (actual delete) of LVs instead of flag delete with cleanup happening later
#define JET_paramFlight_RBSRevertIOUrgentLevel  58  // IO urgent level for reverting the databases using RBS. Used to decide how many outstanding I/Os will be allowed.
#define JET_paramFlight_EnableXpress10Compression 59 //  Enable Xpress10 compression using corsica hardware
#endif // JET_VERSION >= 0x0A01
//                                              60  //  JET_paramCacheSizeMin defined above
#define JET_paramLogFileFailoverPath            61  //  path to use if the log file disk should fail
#define JET_paramEnableImprovedSeekShortcut     62  //  check to see if we are seeking for the record we are currently on
//                                              63  //  JET_paramPreferredVerPages defined above
#define JET_paramDatabasePageSize               64  //  set database page size
#if ( JET_VERSION >= 0x0501 )
#define JET_paramDisableCallbacks               65  //  turn off callback resolution (for defrag/repair)
#endif // JET_VERSION >= 0x0501
#if ( JET_VERSION >= 0x0501 )
#define JET_paramAbortRetryFailCallback         108 //  I/O error callback (JET_ABORTRETRYFAILCALLBACK)

//  Backup performance parameters
//
#define JET_paramBackupChunkSize                66  //  backup read size in pages
#define JET_paramBackupOutstandingReads         67  //  backup maximum reads outstanding

#define JET_paramFlight_RBSMaxTableDeletePages  68  //  Maximum table delete size (in pages) to allow if we are activated on the RBS copy
#define JET_paramLogFileCreateAsynch            69  //  prepares next log file while logging to the current one to smooth response time
#endif // JET_VERSION >= 0x0501
#define JET_paramErrorToString                  70  //  turns a JET_err into a string (taken from the comment in jet.h)
#if ( JET_VERSION >= 0x0501 )
#define JET_paramZeroDatabaseDuringBackup       71  //  Overwrite deleted records/LVs during backup
#endif // JET_VERSION >= 0x0501
#define JET_paramUnicodeIndexDefault            72  //  default LCMapString() lcid and flags to use for CreateIndex() and unique multi-values check
                                                    //      (pass pointer to JET_UNICODEINDEX structure for plParam and sizeof(JET_UNICODE_INDEX) for cbMax)
#if ( JET_VERSION >= 0x0501 )
#define JET_paramRuntimeCallback                73  //  pointer to runtime-only callback function
// #define JET_paramSLVDefragFreeThreshold      74  //  chunks whose free % is > this will be allocated from
// #define JET_paramSLVDefragMoveThreshold      75  //  chunks whose free % is > this will be relocated
#define JET_paramEnableSortedRetrieveColumns    76  //  internally sort (in a dynamically allocated parallel array) JET_RETRIEVECOLUMN structures passed to JetRetrieveColumns()
#endif // JET_VERSION >= 0x0501
#define JET_paramCleanupMismatchedLogFiles      77  //  instead of erroring out after a successful recovery with JET_errLogFileSizeMismatchDatabasesConsistent, ESE will silently delete the old log files and checkpoint file and continue operations
#if ( JET_VERSION >= 0x0501 )
#define JET_paramRecordUpgradeDirtyLevel        78  //  how aggresively should pages with their record format converted be flushed (0-3)
#define JET_paramRecoveryCurrentLogfile         79  //  which generation is currently being replayed (read only)
//                                              81  //  JET_paramGlobalMinVerPages defined above
#define JET_paramOSSnapshotTimeout              82  //  timeout for the freeze period in msec
#if ( JET_VERSION >= 0x0A01 )
#define JET_paramFlight_RBSDbScanRaiseCorruptionRevertedFDP     74  // Dbscan normally should redelete reverted FDPs which have the delete flag set. But, we don't expect that unless we delete logs and mount that copy and for automated testing, we don't delete logs. So for the automated testing cases, we will raise a corruption instead, which should avoid any real corruption due to bugs.
#define JET_paramFlight_RBSAllowTooSoonNonRevertableDelete      75  //  If set, we will do a non-revertable table even if PgnoFDPLastSetTime is null or within the last 7days. Note: Both JET_bitRevertableTableDeleteIfTooSoon and JET_paramFlight_RBSRevertableDeleteIfTooSoonTimeNull will be ignored if this variant is set.
#define JET_paramFlight_RBSForceRollIntervalSec                 80  // Time after which we should force roll into new revert snapshot by raising failure item and letting HA remount. This is temporary till we have live roll.
#define JET_paramFlight_EnableScanCheckFDPDeleteFlags           83  //  Whether we want to enable logging FDPDelete flags in ScanCheck2 log record.
#define JET_paramFlight_NewQueueOptions                         84  //  Controls options for new Meted IO Queue
#define JET_paramFlight_ConcurrentMetedOps                      85  //  Controls how many IOs we leave out at once for the new Meted IO Queue.
#define JET_paramFlight_LowMetedOpsThreshold                    86  //  Controls the transition from 1 meted op to JET_paramFlight_ConcurrentMetedOps (which is the max).
#define JET_paramFlight_MetedOpStarvedThreshold                 87  //  Milliseconds until a meted IO op is considered starved and dispatched no matter what.

#define JET_paramFlight_MaxRBSBuffers                           88  //  Max number of buffers to allocate for revert snapshot.

#define JET_paramFlight_EnableShrinkArchiving                   89  //  Turns on archiving truncated data when shrinking a database (subject to efv).

#define JET_paramFlight_EnableBackupDuringRecovery              90  //  Turns on backup during recovery (i.e. seed from passive copy).

#define JET_paramFlight_RBSRollIntervalSec                      91 // Time after which we should roll into new revert snapshot.
#define JET_paramFlight_RBSMaxRequiredRange                     92 // Max required range allowed for revert snapshot. If combined required range of the dbs is greater than this we will skip creating the revert snapshot
#define JET_paramFlight_RBSCleanupEnabled                       93 // Turns on clean up for revert snapshot.
#define JET_paramFlight_RBSLowDiskSpaceThresholdGb              94 // Low disk space in gigabytes at which we will start cleaning up RBS aggressively.
#define JET_paramFlight_RBSMaxSpaceWhenLowDiskSpaceGb           95 // Max alloted space in gigabytes for revert snapshots when the disk space is low.
#define JET_paramFlight_RBSMaxTimeSpanSec                       96 // Max timespan of a revert snapshot
#define JET_paramFlight_RBSCleanupIntervalMinSec                97 // Min time between cleanup attempts of revert snapshots.

#endif // JET_VERSION >= 0x0A01
#endif // JET_VERSION >= 0x0501

#define JET_paramExceptionAction                98  //  what to do with exceptions generated within JET
#define JET_paramEventLogCache                  99  //  number of bytes of eventlog records to cache if service is not available

#if ( JET_VERSION >= 0x0501 )
#define JET_paramCreatePathIfNotExist           100 //  create system/temp/log/log-failover paths if they do not exist
#define JET_paramPageHintCacheSize              101 //  maximum size of the fast page latch hint cache in bytes
#define JET_paramOneDatabasePerSession          102 //  allow just one open user database per session
#define JET_paramMaxDatabasesPerInstance        103 //  maximum number of databases per instance
#define JET_paramMaxInstances                   104 //  maximum number of instances per process
//                                              105 //  JET_paramVersionStoreTaskQueueMax
//                                              106 //  JET_paramIdleFlushTime
#define JET_paramDisablePerfmon                 107 //  disable perfmon support for this process
//                                              108 //  JET_paramAbortRetryFailCallback
//                                              109 //  JET_paramVAReserve
#define JET_paramIndexTuplesLengthMin           110 //  for tuple indexes, minimum length of a tuple
#define JET_paramIndexTuplesLengthMax           111 //  for tuple indexes, maximum length of a tuple
#define JET_paramIndexTuplesToIndexMax          112 //  for tuple indexes, maximum number of characters in a given string to index
#endif // JET_VERSION >= 0x0501

// Parameters added in Windows 2003/XP64.
#if ( JET_VERSION >= 0x0502 )
#define JET_paramAlternateDatabaseRecoveryPath  113 //  recovery-only - search for dirty-shutdown databases in specified location only
#endif // JET_VERSION >= 0x0502
#define JET_paramFlight_ExtentPageCountCacheVerifyOnly          114 //  Verify values read from the Extent Page Count Cache rather than just returning them.
#define JET_paramFlight_EnablePgnoFDPLastSetTime                115 //  whether we want to enable setting PgnoPFDSetTime in the system table for a table entry.
#define JET_paramFlight_EnableScanCheck2Flags                   116 //  whether we want to enable logging flags in ScanCheck2 log record.
#define JET_paramFlight_EnableExtentFreed2                      117 //  whether we want to enable logging ExtentFreed2 LR after the efv upgrade.
#define JET_paramFlight_RBSLargeRevertableDeletePages           118 //  Large revertable delete size for a table (in pages) beyond which we will track the deletes.
#define JET_paramFlight_RBSRevertableDeleteIfTooSoonTimeNull    119 //  If set, we will do a revertable table delete even if NonRevertableTableDelete flag is passed provided NonRevertable delete is failing due to JET_errRBSDeleteTableTooSoon due to time not being set. Note: If JET_bitRevertableTableDeleteIfTooSoon is set, this variant is ignored.

//                                              120 //  JET_paramDBAPageAvailMin
//                                              121 //  JET_paramDBAPageAvailThreshold
//                                              122 //  JET_paramDBAK1
//                                              123 //  JET_paramDBAK2
//                                              124 //  JET_paramMaxRandomIOSize
//                                              125 //  JET_paramCachedClosedTables
// Parameters added in Windows Vista.
#if ( JET_VERSION >= 0x0600 )
#define JET_paramIndexTupleIncrement            132 //  for tuple indexes, offset increment for each succesive tuple
#define JET_paramIndexTupleStart                133 //  for tuple indexes, offset to start tuple indexing
#define JET_paramKeyMost                        134 //  read only maximum settable key length before key trunctation occurs
#define JET_paramLegacyFileNames                136  // Legacy  file name characteristics to preserve ( JET_bitESE98FileNames | JET_bitEightDotThreeSoftCompat )
#define JET_paramEnablePersistedCallbacks       156  //  allow the database engine to resolve and use callbacks persisted in a database
#endif // JET_VERSION >= 0x0600

// Parameters added in Windows 7.
#if ( JET_VERSION >= 0x0601 )
#define JET_paramWaypointLatency                153  // The latency (in logs) behind the tip / highest committed log to defer database page flushes.
#define JET_paramCheckpointTooDeep              154  // The maximum allowed depth (in logs) of the checkpoint.  Once this limit is reached, updates will fail with JET_errCheckpointDepthTooDeep.
#define JET_paramDisableBlockVerification       155  // TEST ONLY:  use to disable block checksum verification for file fuzz testing
#define JET_paramAggressiveLogRollover          157  // force log rollover after certain operations
#define JET_paramPeriodicLogRolloverLLR         158  // force log rollover after a certain length of inactivity (currently requires setting a waypoint to take affect)
#define JET_paramUsePageDependencies            159  // use page dependencies when logging splits/merges (reduces data logged)
#define JET_paramDefragmentSequentialBTrees     160 //  Turn on/off automatic sequential B-tree defragmentation tasks (On by default, but also requires JET_SPACEHINTS flags / JET_bitRetrieveHintTableScan* to trigger on any given tables).
#define JET_paramDefragmentSequentialBTreesDensityCheckFrequency    161 //  Determine how frequently B-tree density is checked
#define JET_paramIOThrottlingTimeQuanta         162 //  Max time (in MS) that the I/O throttling mechanism gives a task to run for it to be considered 'completed'.
#define JET_paramLVChunkSizeMost                163 //  Max LV chunk size supported wrt the chosen page size (R/O)
#define JET_paramMaxCoalesceReadSize            164 //  Max number of bytes that can be grouped for a coalesced read operation.
#define JET_paramMaxCoalesceWriteSize           165 //  Max number of bytes that can be grouped for a coalesced write operation.
#define JET_paramMaxCoalesceReadGapSize         166 //  Max number of bytes that can be gapped for a coalesced read IO operation.
#define JET_paramMaxCoalesceWriteGapSize        167 //  Max number of bytes that can be gapped for a coalesced write IO operation.
#define JET_paramEnableHaPublish                168 //  Event through HA publishing mechanism.
#define JET_paramEnableDBScanInRecovery         169 //  Do checksumming of the database during recovery.
#define JET_paramDbScanThrottle                 170 //  throttle (mSec).
#define JET_paramDbScanIntervalMinSec           171 //  Min internal to repeat checksumming (Sec).
#define JET_paramDbScanIntervalMaxSec           172 //  Max internal checksumming must finish (Sec).
#define JET_paramEmitLogDataCallback            173 //  Set the callback for emitting log data to an external 3rd party.
#define JET_paramEmitLogDataCallbackCtx         174 //  Context for the the callback for emitting log data to an external 3rd party.
#endif // JET_VERSION >= 0x0601

#if ( JET_VERSION >= 0x0602 )
#define JET_paramEnableExternalAutoHealing      175 //  Enable logging of page patch request and callback on page patch request processing (and corrupt page notification) during recovery.
#define JET_paramPatchRequestTimeout            176 //  Time before an outstanding patch request is considered stale (Seconds).
#define JET_paramCachePriority                  177 //  Per-instance property for relative cache priorities (default = 100).
                                                    //
                                                    //  There are three scopes for which cache priority may be assigned:
                                                    //
                                                    //    - Instance: by calling JetSetSystemParameter and setting JET_paramCachePriority for a specific
                                                    //                ESE instance. The cache priority for this scope is always defined (default is 100),
                                                    //                even if the client does not set the priority explicitly using the system parameter.
                                                    //    - Session: by calling JetSetSessionParameter and setting JET_sesparamCachePriority for a specific
                                                    //               ESE session. The cache priority for this scope is undefined by default.
                                                    //    - Database: by calling JetCreateDatabase3 (or above) or JetAttachDatabase3 (or above) and setting
                                                    //                JET_dbparamCachePriority for a specific new or attached database. The cache priority
                                                    //                for this scope is undefined by default.
                                                    //
                                                    //  The way cache priority for those three scopes interact is as follows:
                                                    //    - If only the priority for the instance scope is defined, it is used for all operations related
                                                    //      to that instance.
                                                    //    - If only the priorities for the instance and session scopes are defined, the session scope priority
                                                    //      is used for all operations related to that session.
                                                    //    - If only the priorities for the instance and database scopes are defined, the database scope priority
                                                    //      is used for all operations related to that database.
                                                    //    - If the priorities for all three scopes are defined, the lowest of session and database scope
                                                    //      priorities is used for all operations related to that session/database combination. For everything
                                                    //      else, the rules above apply on a per-database-page basis.

#define JET_paramMaxTransactionSize             178 //  Percentage of version store that can be used by oldest transaction before JET_errVersionStoreOutOfMemory (default = 100).
#define JET_paramPrereadIOMax                   179 //  Maximum number of I/O operations dispatched for a given purpose.
#define JET_paramEnableDBScanSerialization      180 //  Database Maintenance serialization is enabled for databases sharing the same disk.
#define JET_paramHungIOThreshold                181 //  The threshold for what is considered a hung IO that should be acted upon.
#define JET_paramHungIOActions                  182 //  A set of actions to be taken on IOs that appear hung.
#define JET_paramMinDataForXpress               183 //  Smallest amount of data that should be compressed with xpress compression.
#endif // JET_VERSION >= 0x0602

#if ( JET_VERSION >= 0x0603 )
#define JET_paramEnableShrinkDatabase           184 //  Release space back to the OS when deleting data. This may require an OS feature of Sparse Files, and is subject to change.
// DEPRECATED: this was once used in the first implementation of DB shrink.
// #define JET_paramAutomaticShrinkDatabaseFreeSpaceThreshold   185 //  DEPRECATED: Minimum threshold (percentage of the database size) that determines if the periodic shrink and/or shrink at JetTerm will take place or not.
#endif // JET_VERSION >= 0x0603

// Parameters added in Windows 8.
#if ( JET_VERSION >= 0x0602 )
// System parameter 185 is reserved.
#define JET_paramProcessFriendlyName            186 //  Friendly name for this instance of the process (e.g. performance counter global instance name, event logs).
#define JET_paramDurableCommitCallback          187 //  callback for when log is flushed
#endif // JET_VERSION >= 0x0602

// Parameters added in Windows 8.1.
#if ( JET_VERSION >= 0x0603 )
#define JET_paramEnableSqm                      188 //  Deprecated / ignored param.
#endif // JET_VERSION >= 0x0603

// Parameters added in Windows 10.
#if ( JET_VERSION >= 0x0A00 )

#define JET_paramConfigStoreSpec                189 //  Custom path that allows the consumer to specify a path (currently from in the registry) from which to pull custom ESE configuration.
#define JET_paramStageFlighting                 190 //  It's like stage fright-ing but different - use JET_bitStage* bits.
#define JET_paramZeroDatabaseUnusedSpace        191 //  Controls scrubbing of unused database space.
#define JET_paramDisableVerifications           192 //  Verification modes disabled
#endif // JET_VERSION >= 0x0A00
#if ( JET_VERSION >= 0x0A01 )
#define JET_paramPersistedLostFlushDetection    193 //  Configures persisted lost flush detection for databases while attached to an instance.
#define JET_paramEngineFormatVersion            194 //  Engine format version - specifies the maximum format version the engine should allow, ensuring no format features are used beyond this (allowing the DB / logs to be forward compatible).
#define JET_paramReplayThrottlingLevel          195 //  Should replay be throttled so as not to generate too much disk IO

#define JET_paramBlockCacheConfiguration        212 //  Configuration for the ESE Block Cache via an IBlockCacheConfiguration* (optional).

#define JET_paramRecordSizeMost                 213 //  Read only param that returns the maximum record size supported by the current pagesize.
                                                    //  This includes storage overhead. Use in combination with JetGetRecordSize().
#endif // JET_VERSION >= 0x0A01
#if ( JET_VERSION >= 0x0A01 )
#define JET_paramUseFlushForWriteDurability     214 //  This controls whether ESE uses Flush or FUA to make sure a write to disk is durable.

#define JET_paramEnableRBS                      215 //  Turns on revert snapshot. Not an ESE flight as we will let the variant be controlled outside ESE (like HA can enable this when lag is disabled)
#define JET_paramRBSFilePath                    216 //  path to the revert snapshot directory

#define JET_paramPerfmonRefreshInterval         217 //  Interval, in units of msec, used by the Permormance Monitor to refresh values for collection.

#define JET_paramEnableBlockCache               218 //  Indicates that the ESE Block Cache is enabled.  This is sufficient to access files previously attached to the ESE Block Cache but not to attach new files.

#endif // JET_VERSION >= 0x0A01
#define JET_paramDeferredIndexPopulateRowsPerTransaction 219 // Number of primary index rows to process in a single transaction when processing
                                                             // a delayed-populate index
#define JET_paramEnableBlockCacheDetach         220 //  Indicates that ESE Block Cache detach is enabled.  This will allow a file cached by the ESE Block Cache to be detached on open.

#define JET_paramMaxValueInvalid                221 //  This is not a valid parameter. It can change from release to release!
#if ( JET_VERSION >= 0x0A01 )

    /* Flags for JET_sesparamIOSessTraceFlags */

#define JET_bitIOSessTraceReads             0x01
#define JET_bitIOSessTraceWrites            0x02
#define JET_bitIOSessTraceHDD               0x04
#define JET_bitIOSessTraceSSD               0x08

#endif // JET_VERSION >= 0x0A01
//  Session parameters
//
//      JET_sesparamBase                    4096    //  All JET_sesparams designed to be distinct from system / JET_params and JET_dbparams for code defense.

#define JET_sesparamCommitDefault           4097    //  Default grbit for JetCommitTransaction
#define JET_sesparamCommitGenericContext    4098    //  A generic context to be logged with the Commit0 LR
#if ( JET_VERSION >= 0x0A00 )
#define JET_sesparamTransactionLevel        4099    //  Retrieves (read-only, no set) the current number of nested levels of transactions begun.  0 = not in a transaction.
#define JET_sesparamOperationContext        4100    //  a client context that the engine uses to track and trace operations (such as IOs)
#define JET_sesparamCorrelationID           4101    //  an ID that is logged in traces and can be used by clients to correlate ESE actions with their activity
#if ( JET_VERSION >= 0x0A01 )
#define JET_sesparamCachePriority           4102    //  Cache priority to be assigned to database pages accessed by the session.
                                                    //  See comment next to JET_paramCachePriority for how JET_sesparamCachePriority,
                                                    //  JET_dbparamCachePriority and JET_paramCachePriority interact.

// Store specific trace context parameters
#define JET_sesparamClientComponentDesc     4103
#define JET_sesparamClientActionDesc        4104
#define JET_sesparamClientActionContextDesc 4105
#define JET_sesparamClientActivityId        4106
#define JET_sesparamIOSessTraceFlags        4107
#define JET_sesparamIOPriority              4108    //  Specifies IO Priority flags to use (see JET_IOPriority* flags)

#define JET_sesparamCommitContextContainsCustomerData   4109    //  Boolean value specifying whether the value specified with JET_sesparamCommitGenericContext contains customer data.
#define JET_sesparamCommitContextNeedPreCommitCallback  4110    //  Boolean value specifying whether the application wants pre/post commit callbacks with the generic context.
#endif // JET_VERSION >= 0x0A01
#define JET_sesparamMaxValueInvalid         4111    //  This is not a valid session parameter. It can change from release to release!

typedef struct
{
    uint32_t        ulUserID;
    uint8_t         nOperationID;
    uint8_t         nOperationType;
    uint8_t         nClientType;
    uint8_t         fFlags;
} JET_OPERATIONCONTEXT;
#endif // JET_VERSION >= 0x0A00

#if ( JET_VERSION >= 0x0600 )

    /* Flags for JET_paramLegacyFileNames */

#define JET_bitESE98FileNames           0x00000001  //  Preserve the .log and .chk extension for compatibility reasons (i.e. Exchange)
#define JET_bitEightDotThreeSoftCompat  0x00000002  //  Preserve the 8.3 naming syntax for as long as possible. (this should not be changed, w/o ensuring there are no log files)
#endif // JET_VERSION >= 0x0600

    /* Flags for JET_paramHungIOActions */

#define JET_bitHungIOEvent                  0x00000001  // Log event when an IO appears to be hung for over the IO threshold.
#define JET_bitHungIOCancel                 0x00000002  // Cancel an IO when an IO appears to be hung for over 2 x the IO threshhold.
#define JET_bitHungIODebug                  0x00000004  // Crash the process when an IO appears to be hung for over 3 x the IO threshhold.
#define JET_bitHungIOEnforce                0x00000008  // Crash the process when an IO appears to be hung for over 3 x the IO threshhold.
#define JET_bitHungIOTimeout                0x00000010  // Failure item when an IO appears to be hung for over 4 x the IO threshhold (considered timed out).

    /* Flags for JET_paramPersistedLostFlushDetection */

#define JET_bitPersistedLostFlushDetectionEnabled           0x00000001  // Enables persisted lost flush detection.
#define JET_bitPersistedLostFlushDetectionCreateNew         0x00000002  // If set, the persisted flush map is re-created on every database attachment.
#define JET_bitPersistedLostFlushDetectionFailOnRuntimeOnly 0x00000004  // If set, lost flush errors are only returned if the flush state was set at runtime.

    /* values for JET_paramReplayThrottlingLevel */

#define JET_ReplayThrottlingNone            0   //  No throttling
#define JET_ReplayThrottlingSleep           1   //  Sleep between replaying log segments which were generated slowly by active
#if ( JET_VERSION >= 0x0603 )
// Values for JET_paramEnableShrinkDatabase.
#define JET_bitShrinkDatabaseOff            0x0
#define JET_bitShrinkDatabaseOn             0x1     // Uses the file system's Sparse Files feature to release space in the middle of a file.
#define JET_bitShrinkDatabaseRealtime       0x2     // Attempts to reclaim space back to the file system after freeing significant amounts of data (when space is marked as Available to the Root space tree).
// DEPRECATED: this was once used by the instance-wide JET_paramEnableShrinkDatabase parameter.
//             A new value is now defined as a database-wide parameter.
// #define JET_bitShrinkDatabaseEofOnAttach    0x4000  // Resizes the database file during its attachment. All fully available extents at the end of the database are truncated out. It currently does not rearrange data.

#define JET_bitShrinkDatabasePeriodically   0x8000  // Periodically try to trim the database.
// DEPRECATED:
#define JET_bitShrinkDatabaseTrim           0x1     // DEPRECATED: Deprecated value for JET_bitShrinkDatabaseOn; Will be removed!

#endif // JET_VERSION >= 0x0603

    /* Flags for JetInit2, JetInit3 */
#define JET_bitReplayReplicatedLogFiles     0x00000001  //  OBSOLETE and UNSUPPORTED, current log shipping implementations use JET_bitRecoveryWithoutUndo.
// #define JET_bitCreateSFSVolumeIfNotExist 0x00000002  //  OBSOLETE and UNSUPPORTED, but needed to prevent Exchange Store compilation errors
#if ( JET_VERSION >= 0x0501 )
#define JET_bitReplayIgnoreMissingDB        0x00000004  //  Ignore missing databases during recovery. This is a very dangerous option and may irrevocably produce an inconsistent database if improperly used. Normal ESE usage does not typically require this dangerous option.
#endif // JET_VERSION >= 0x0501
#if ( JET_VERSION >= 0x0600 )
#define JET_bitRecoveryWithoutUndo          0x00000008  //  perform recovery, but halt at the Undo phase
#define JET_bitTruncateLogsAfterRecovery    0x00000010  //  on successful soft recovery, truncate log files
#define JET_bitReplayMissingMapEntryDB      0x00000020  //  missing database map entry default to same location
#define JET_bitLogStreamMustExist           0x00000040  //  transaction logs must exist in the logfile directory (ie. cannot auto-start a new log stream)
#endif // JET_VERSION >= 0x0600
#if ( JET_VERSION >= 0x0601 )
#define JET_bitReplayIgnoreLostLogs         0x00000080  //  ignore logs lost from the end of the log stream
#endif // JET_VERSION >= 0x0601
#if ( JET_VERSION >= 0x0602 )
#define JET_bitAllowMissingCurrentLog       0x00000100  //  this allows JetInitX() to ignore the fact that we are missing edb.jtx|log and edbtmp.jtx|log
#define JET_bitAllowSoftRecoveryOnBackup    0x00000200  //  this allows JetInitX() to perform soft recovery on a backed up database, essentially implementing hard recovery via JetInitX()
#define JET_bitSkipLostLogsEvent            0x00000400  //  this supresses the event for lost committed logs for HA incremental reseed V1
#define JET_bitExternalRecoveryControl      0x00000800  //  this for absolute control of the recovery process via invoking a callback to be made for all significant state decisions during recovery
#define JET_bitKeepDbAttachedAtEndOfRecovery 0x00001000 //  this allows db to remain attached at the end of recovery (for faster transition to running state)
#endif // JET_VERSION >= 0x0602
#if ( JET_VERSION >= 0x0A01 )

#define JET_bitReplayIgnoreLogRecordsBeforeMinRequiredLog 0x00002000    //  Ignore log records before the min required log for an attached database.
#define JET_bitReplayInferCheckpointFromRstmapDbs 0x00004000 //   When no checkpoint file is present use databases in restore-map to infer checkpoint instead of starting from oldest log.

/* Flags for JetInit4 JET_RSTMAP2 */

#define JET_bitRestoreMapIgnoreWhenMissing  0x00000001  //  Ignore missing database when replaying an attach or create database during recovery
#define JET_bitRestoreMapFailWhenMissing    0x00000002  //  Fail early on a missing database when replaying an attach or create database during recovery
#define JET_bitRestoreMapOverwriteOnCreate  0x00000004  //  Overwrite existing database when replaying a create database during recovery

#endif // JET_VERSION >= 0x0A01
    /* Flags for JetTerm2 */

#define JET_bitTermComplete             0x00000001
#define JET_bitTermAbrupt               0x00000002
#define JET_bitTermStopBackup           0x00000004
#if ( JET_VERSION >= 0x0601 )
#define JET_bitTermDirty                0x00000008
#endif // JET_VERSION >= 0x0601
#if ( JET_VERSION >= 0x0602 )
// DEPRECATED: this was once used in the first implementation of DB shrink.
// #define JET_bitTermShrink                0x00000010
#endif // JET_VERSION >= 0x0602
    /* Flags for JetIdle */

#define JET_bitIdleFlushBuffers         0x00000001
#define JET_bitIdleCompact              0x00000002
#define JET_bitIdleStatus               0x00000004
#define JET_bitIdleVersionStoreTest     0x00000008 /* INTERNAL USE ONLY. call version store consistency check */
#if ( JET_VERSION >= 0x0603 )
// following can only be used in combination with JET_bitIdleCompact
#define JET_bitIdleCompactAsync         0x00000010
#if ( JET_VERSION >= 0x0A01 )
#define JET_bitIdleAvailBuffersStatus   0x00000020  //  Returns JET_wrnIdleFull when database cache available buffers is less than the JET_paramStartFlushThreshold setting.
#define JET_bitIdleWaitForAsyncActivity 0x00000040  //  Waits for all async activity to quiesce. Returns JET_wrnRemainingVersions if there are still pending version store buckets.
#endif // JET_VERSION >= 0x0A01
#endif // JET_VERSION >= 0x0603
    /* Flags for JetEndSession */
#define JET_bitForceSessionClosed       0x00000001
    /* Flags for JetAttachDatabase/JetOpenDatabase */

#define JET_bitDbReadOnly               0x00000001
#define JET_bitDbExclusive              0x00000002 /* multiple opens allowed */
#define JET_bitDbSingleExclusive        0x00000002 /* NOT CURRENTLY IMPLEMENTED - currently maps to JET_bitDbExclusive */
// RESERVED                             0x00000008 /* JET_bitDbRecoveryOff defined under JetCreateDatabase() */
#define JET_bitDbDeleteCorruptIndexes   0x00000010 /* delete indexes possibly corrupted by NT version upgrade */
#define JET_bitDbRebuildCorruptIndexes  0x00000020 /* NOT CURRENTLY IMPLEMENTED - recreate indexes possibly corrupted by NT version upgrade */
// RESERVED                             0x00000040 /* JET_bitDbVersioningOff defined under JetCreateDatabase() */
#if ( JET_VERSION >= 0x0502 )
#define JET_bitDbDeleteUnicodeIndexes   0x00000400 /* delete all indexes with unicode columns */
#endif // JET_VERSION >= 0x0502
#if ( JET_VERSION >= 0x0501 )
#define JET_bitDbUpgrade                0x00000200 /* */
#endif // JET_VERSION >= 0x0501
#if ( JET_VERSION >= 0x0601 )
#define JET_bitDbEnableBackgroundMaintenance    0x00000800  /* the database engine will initiate automatic background database maintenance */
#endif
#if ( JET_VERSION >= 0x0602 )
#define JET_bitDbPurgeCacheOnAttach     0x00001000 /* used to ensure any kept alive cache is purged for this DB before attach */
#endif
#define bitDbOpenForRecovery            0x00002000 /* internal flag used by recovery */
    /* Flags for JetDetachDatabase2 */

#if ( JET_VERSION >= 0x0501 )
#define JET_bitForceDetach                  0x00000001
#define JET_bitForceCloseAndDetach          (0x00000002 | JET_bitForceDetach)
#endif // JET_VERSION >= 0x0501
// DEPRECATED: this was once used in the first implementation of DB shrink.
// #define JET_bitDetachShrink                  0x00000004
    /* Flags for JetCreateDatabase */

#define JET_bitDbRecoveryOff            0x00000008 /* disable logging/recovery for this database */
#define JET_bitDbVersioningOff          0x00000040 /* INTERNAL USE ONLY */
#define JET_bitDbShadowingOff           0x00000080 /* disable catalog shadowing */
#define JET_bitDbCreateStreamingFile    0x00000100 /* create streaming file with same name as db */
#if ( JET_VERSION >= 0x0501 )
#define JET_bitDbOverwriteExisting      0x00000200 /* overwrite existing database with same name */
#endif // JET_VERSION >= 0x0501
#define bitCreateDbImplicitly           0x00000400 /* internal use only: create database implicitly (unlogged) */
// RESERVED                             0x00000800 /* JET_bitDbEnableBackgroundMaintenance defined under JetAttachDatabase() */
    /* Flags for JetBackup, JetBeginExternalBackup, JetBeginExternalBackupInstance, JetBeginSurrogateBackup */

#define JET_bitBackupIncremental        0x00000001
#define JET_bitKeepOldLogs              0x00000002
#define JET_bitBackupAtomic             0x00000004
#define JET_bitBackupFullWithAllLogs    0x00000008
#if ( JET_VERSION >= 0x0501 )
#define JET_bitBackupSnapshot           0x00000010
#endif // JET_VERSION >= 0x0501
#if ( JET_VERSION >= 0x0601 )
#define JET_bitBackupSurrogate          0x00000020
#endif // JET_VERSION >= 0x0601
#define JET_bitInternalCopy         0x00000040
    /* Flags for JetEndExternalBackupInstance2, JetEndSurrogateBackup */

#if ( JET_VERSION >= 0x0501 )
#define JET_bitBackupEndNormal              0x0001
#define JET_bitBackupEndAbort               0x0002
#endif // JET_VERSION >= 0x0501
#if ( JET_VERSION >= 0x0600 )
#define JET_bitBackupTruncateDone           0x0100
#endif // JET_VERSION >= 0x0600
#if ( JET_VERSION >= 0x0601 )
#define JET_bitBackupNoDbHeaderUpdate       0x0200
#endif // JET_VERSION >= 0x0601
    /* Database types */

#define JET_dbidNil         ((JET_DBID) 0xFFFFFFFF)
#define JET_dbidNoValid     ((JET_DBID) 0xFFFFFFFE) /* used as a flag to indicate that there is no valid dbid */
    /* Flags for JetCreateTableColumnIndex */
#define JET_bitTableCreateFixedDDL          0x00000001  /* DDL is fixed */
#define JET_bitTableCreateTemplateTable     0x00000002  /* DDL is inheritable (implies FixedDDL) */
#if ( JET_VERSION >= 0x0501 )
#define JET_bitTableCreateNoFixedVarColumnsInDerivedTables  0x00000004
                                                        //  used in conjunction with JET_bitTableCreateTemplateTable
                                                        //  to disallow fixed/var columns in derived tables (so that
                                                        //  fixed/var columns may be added to the template in the future)
#endif // JET_VERSION >= 0x0501
#if JET_VERSION >= 0x0A00
#define JET_bitTableCreateImmutableStructure    0x00000008  // Do not write to the input structures. Additionally, do not return any auto-opened tableid.
#endif // JET_VERSION >= 0x0A00
#define JET_bitTableCreateSystemTable       0x80000000  /*  INTERNAL USE ONLY */
    /* Flags for JetAddColumn, JetGetColumnInfo, JetOpenTempTable */

#define JET_bitColumnFixed              0x00000001
#define JET_bitColumnTagged             0x00000002
#define JET_bitColumnNotNULL            0x00000004
#define JET_bitColumnVersion                0x00000008
#define JET_bitColumnAutoincrement      0x00000010
#define JET_bitColumnUpdatable          0x00000020 /* JetGetColumnInfo only */
#define JET_bitColumnTTKey              0x00000040 /* JetOpenTempTable only */
#define JET_bitColumnTTDescending       0x00000080 /* JetOpenTempTable only */
#define JET_bitColumnMultiValued            0x00000400
#define JET_bitColumnEscrowUpdate       0x00000800 /* escrow updated, supported coltyps are long and longlong */
#define JET_bitColumnUnversioned        0x00001000 /* for add column only - add column unversioned */
#if ( JET_VERSION >= 0x0501 )
#define JET_bitColumnMaybeNull          0x00002000 /* for retrieve column info of outer join where no match from the inner table */
#define JET_bitColumnFinalize           0x00004000 /* DEPRECATED / Not Fully Implemented: use JET_bitColumnDeleteOnZero instead. */
#define JET_bitColumnUserDefinedDefault 0x00008000 /* default value from a user-provided callback */
#define JET_bitColumnRenameConvertToPrimaryIndexPlaceholder 0x00010000  //  FOR JetRenameColumn() ONLY: rename and convert to primary index placeholder (ie. no longer part of primary index ecxept as a placeholder)
#endif // JET_VERSION >= 0x0501
#if ( JET_VERSION >= 0x0502 )
#define JET_bitColumnDeleteOnZero       0x00020000 /* When the escrow-update column reaches a value of zero (after all versions are resolve), the record will be deleted. A common use for a column that can be finalized is to use it as a reference count field, and when the field reaches zero the record gets deleted. A Delete-on-zero column must be an escrow update / JET_bitColumnEscrowUpdate column. JET_bitColumnDeleteOnZero cannot be used with JET_bitColumnFinalize. JET_bitColumnDeleteOnZero cannot be used with user defined default columns. */
#endif // JET_VERSION >= 0x0502
#if ( JET_VERSION >= 0x0600 )
// Note: this bit only used in the C# interop provider
#define JET_bitColumnVariable           0x00040000 /* make column a variable length column */
#endif // JET_VERSION >= 0x0600
#if ( JET_VERSION >= 0x0601 )
#define JET_bitColumnCompressed         0x00080000 /* data in the column can be compressed */
#endif
#if ( JET_VERSION >= 0x0A01 )
#define JET_bitColumnEncrypted          0x00100000 /* data in the column is encrypted */
#endif
#if ( JET_VERSION >= 0x0501 )
//  flags for JetDeleteColumn
#define JET_bitDeleteColumnIgnoreTemplateColumns    0x00000001  //  for derived tables, don't bother looking in template columns
#endif // JET_VERSION >= 0x0501

    /* Flags for JetSetCurrentIndex */

#define JET_bitMoveFirst                0x00000000
#define JET_bitMoveBeforeFirst          0x00000001  // unsupported -- DO NOT USE
#define JET_bitNoMove                   0x00000002

    /* Flags for JetMakeKey */

#define JET_bitNewKey                   0x00000001
#define JET_bitStrLimit                 0x00000002
#define JET_bitSubStrLimit              0x00000004
#define JET_bitNormalizedKey            0x00000008
#define JET_bitKeyDataZeroLength        0x00000010
#if ( JET_VERSION >= 0x0501 )
#define JET_bitKeyOverridePrimaryIndexPlaceholder   0x00000020
#endif // JET_VERSION >= 0x0501

#define JET_maskLimitOptions            0x00000f00
#if ( JET_VERSION >= 0x0501 )
#define JET_bitFullColumnStartLimit     0x00000100
#define JET_bitFullColumnEndLimit       0x00000200
#define JET_bitPartialColumnStartLimit  0x00000400
#define JET_bitPartialColumnEndLimit    0x00000800
#endif // JET_VERSION >= 0x0501

    /* Flags for JetSetIndexRange */

#define JET_bitRangeInclusive           0x00000001
#define JET_bitRangeUpperLimit          0x00000002
#define JET_bitRangeInstantDuration     0x00000004
#define JET_bitRangeRemove              0x00000008

    /* Flags for JetGetLock */

#define JET_bitReadLock                 0x00000001
#define JET_bitWriteLock                0x00000002
#if ( JET_VERSION >= 0x0A01 )
#define JET_bitKeyLock                  0x00000004
#endif // JET_VERSION >= 0x0A01
    /* Constants for JetMove */

#define JET_MoveFirst                   (0x80000000)
#define JET_MovePrevious                (-1)
#define JET_MoveNext                    (+1)
#define JET_MoveLast                    (0x7fffffff)

    /* Flags for JetMove */

#define JET_bitMoveKeyNE                0x00000001

    /* Flags for JetSeek */

#define JET_bitSeekEQ                   0x00000001
#define JET_bitSeekLT                   0x00000002
#define JET_bitSeekLE                   0x00000004
#define JET_bitSeekGE                   0x00000008
#define JET_bitSeekGT                   0x00000010
#define JET_bitSetIndexRange            0x00000020
#if ( JET_VERSION >= 0x0502 )
#define JET_bitCheckUniqueness          0x00000040  //  to be used with JET_bitSeekEQ only, returns JET_wrnUniqueKey if seek lands on a key which has no dupes
#endif // JET_VERSION >= 0x0502

#if ( JET_VERSION >= 0x0501 )
    //  Flags for JetGotoSecondaryIndexBookmark
#define JET_bitBookmarkPermitVirtualCurrency    0x00000001  //  place cursor on relative position in index if specified bookmark no longer exists
#endif // JET_VERSION >= 0x0501

    /* Flags for JET_CONDITIONALCOLUMN */
#define JET_bitIndexColumnMustBeNull    0x00000001
#define JET_bitIndexColumnMustBeNonNull 0x00000002

    /* Flags for JET_INDEXRANGE */
#define JET_bitRecordInIndex            0x00000001
#define JET_bitRecordNotInIndex         0x00000002

    /* Flags for JetCreateIndex */

#define JET_bitIndexUnique              0x00000001
#define JET_bitIndexPrimary             0x00000002
#define JET_bitIndexDisallowNull        0x00000004
#define JET_bitIndexIgnoreNull          0x00000008
#define JET_bitIndexClustered40         0x00000010  /*  for backward compatibility */
#define JET_bitIndexIgnoreAnyNull       0x00000020
#define JET_bitIndexIgnoreFirstNull     0x00000040
#define JET_bitIndexLazyFlush           0x00000080
#define JET_bitIndexEmpty               0x00000100  // don't attempt to build index, because all entries would evaluate to NULL (MUST also specify JET_bitIgnoreAnyNull)
#define JET_bitIndexUnversioned         0x00000200
#define JET_bitIndexSortNullsHigh       0x00000400  // NULL sorts after data for all columns in the index
#define JET_bitIndexUnicode             0x00000800  // LCID field of JET_INDEXCREATE actually points to a JET_UNICODEINDEX struct to allow user-defined LCMapString() flags
#if ( JET_VERSION >= 0x0501 )
#define JET_bitIndexTuples              0x00001000  // index on substring tuples (text columns only)
#endif // JET_VERSION >= 0x0501
#if ( JET_VERSION >= 0x0502 )
#define JET_bitIndexTupleLimits         0x00002000  // cbVarSegMac field of JET_INDEXCREATE actually points to a JET_TUPLELIMITS struct to allow custom tuple index limits (implies JET_bitIndexTuples)
#endif // JET_VERSION >= 0x0502
#if ( JET_VERSION >= 0x0600 )
#define JET_bitIndexCrossProduct        0x00004000  // index over multiple multi-valued columns has full cross product
#define JET_bitIndexKeyMost             0x00008000  // custom index key size set instead of default of 255 bytes
#define JET_bitIndexDisallowTruncation  0x00010000  // fail update rather than truncate index keys
#define JET_bitIndexNestedTable         0x00020000  // index over multiple multi-valued columns but only with values of same itagSequence
#endif // JET_VERSION >= 0x0600
#if ( JET_VERSION >= 0x0602 )
#define JET_bitIndexDotNetGuid          0x00040000  // index over GUID column according to .Net GUID sort order
#endif // JET_VERSION >= 0x602
#if ( JET_VERSION >= 0x0A00 )
#define JET_bitIndexImmutableStructure  0x00080000  // Do not write to the input structures during a JetCreateIndexN call.
#endif // JET_VERSION >= 0x0A00
#if ( JET_VERSION >= 0x0A00 )
#define JET_bitIndexDeferredPopulateCreate  0x00100000  // Only create the index, don't actually populate it.
#define JET_bitIndexDeferredPopulateProcess 0x00200000  // Populate an index that was previously created with JET_bitIndexDeferredPopulateCreate
#endif // JET_VERSION >= 0x0A00
#if ( JET_VERSION >= 0x0A01 )
#define JET_bitIndexOptionallyUnique    0x00400000  // Index uniqueness is only enforced on updates using JET_bitUpdateEnforceOptionallyUniqueIndices
#endif // JET_VERSION >= 0x0A01

// These are not persisted anywhere. These are bits used by the 'Isam layer', a simpler C#-based
// interface to access ESE databases.
//
// #define JET_bitIndexAllowTruncation          0x01000000  // Isam-layer only. Specifies that index keys may be truncated (default in ESE is to allow truncation).
    /* Flags for index key definition */

#define JET_bitKeyAscending             0x00000000
#define JET_bitKeyDescending            0x00000001

    /* Flags for JetOpenTable */

#define JET_bitTableDenyWrite           0x00000001
#define JET_bitTableDenyRead            0x00000002
#define JET_bitTableReadOnly            0x00000004
#define JET_bitTableUpdatable           0x00000008
#define JET_bitTablePermitDDL           0x00000010  /*  override table flagged as FixedDDL (must be used with DenyRead) */
#define JET_bitTableNoCache             0x00000020  /*  don't cache the pages for this table */
#define JET_bitTablePreread             0x00000040  /*  assume the table is probably not in the buffer cache */
#define JET_bitTableOpportuneRead       0x00000080  /*  attempt to opportunely read physically adjacent leaf pages using larger physical IOs */
#if ( JET_VERSION >= 0x0A01 )
#define JET_bitTableAllowOutOfDate      0x00000100  /*  allow opening with indexes using out-of-date (but valid) sort versions */
#define JET_bitAllowPgnoFDPLastSetTime  0x00000200  /*  allow changing pgnofdp last set time in catalog while opening the table */
#endif
#define JET_bitTableSequential          0x00008000  /*  assume the table will be scanned sequentially */
#define JET_bitTableTryPurgeOnClose     0x01000000  /*  INTERNAL USE ONLY: attempt to cleanup resources when table is closed */
#define JET_bitTableAllowSensitiveOperation 0x08000000 /*  INTERNAL USE ONLY */
#define JET_bitTableDelete              0x10000000  /*  INTERNAL USE ONLY */
#define JET_bitTableCreate              0x20000000  /*  INTERNAL USE ONLY */
#define bitTableUpdatableDuringRecovery 0x40000000  /*  INTERNAL USE ONLY */
#define JET_bitTableClassMask       0x001F0000  /*  table stats class mask  */
#define JET_bitTableClassNone       0x00000000  /*  table belongs to no stats class (default)  */
#define JET_bitTableClass1          0x00010000  /*  table belongs to stats class 1  */
#define JET_bitTableClass2          0x00020000  /*  table belongs to stats class 2  */
#define JET_bitTableClass3          0x00030000  /*  table belongs to stats class 3  */
#define JET_bitTableClass4          0x00040000  /*  table belongs to stats class 4  */
#define JET_bitTableClass5          0x00050000  /*  table belongs to stats class 5  */
#define JET_bitTableClass6          0x00060000  /*  table belongs to stats class 6  */
#define JET_bitTableClass7          0x00070000  /*  table belongs to stats class 7  */
#define JET_bitTableClass8          0x00080000  /*  table belongs to stats class 8  */
#define JET_bitTableClass9          0x00090000  /*  table belongs to stats class 9  */
#define JET_bitTableClass10         0x000A0000  /*  table belongs to stats class 10  */
#define JET_bitTableClass11         0x000B0000  /*  table belongs to stats class 11  */
#define JET_bitTableClass12         0x000C0000  /*  table belongs to stats class 12  */
#define JET_bitTableClass13         0x000D0000  /*  table belongs to stats class 13  */
#define JET_bitTableClass14         0x000E0000  /*  table belongs to stats class 14  */
#define JET_bitTableClass15         0x000F0000  /*  table belongs to stats class 15  */
#if ( JET_VERSION >= 0x0A01 )
#define JET_bitTableClass16         0x00100000  /*  table belongs to stats class 16  */
#define JET_bitTableClass17         0x00110000  /*  table belongs to stats class 17  */
#define JET_bitTableClass18         0x00120000  /*  table belongs to stats class 18  */
#define JET_bitTableClass19         0x00130000  /*  table belongs to stats class 19  */
#define JET_bitTableClass20         0x00140000  /*  table belongs to stats class 20  */
#define JET_bitTableClass21         0x00150000  /*  table belongs to stats class 21  */
#define JET_bitTableClass22         0x00160000  /*  table belongs to stats class 22  */
#define JET_bitTableClass23         0x00170000  /*  table belongs to stats class 23  */
#define JET_bitTableClass24         0x00180000  /*  table belongs to stats class 24  */
#define JET_bitTableClass25         0x00190000  /*  table belongs to stats class 25  */
#define JET_bitTableClass26         0x001A0000  /*  table belongs to stats class 26  */
#define JET_bitTableClass27         0x001B0000  /*  table belongs to stats class 27  */
#define JET_bitTableClass28         0x001C0000  /*  table belongs to stats class 28  */
#define JET_bitTableClass29         0x001D0000  /*  table belongs to stats class 29  */
#define JET_bitTableClass30         0x001E0000  /*  table belongs to stats class 30  */
#define JET_bitTableClass31         0x001F0000  /*  table belongs to stats class 31  */
#endif // JET_VERSION >= 0x0A01
#if ( JET_VERSION >= 0x0501 )
#define JET_bitLSReset              0x00000001  /*  reset LS value */
#define JET_bitLSCursor             0x00000002  /*  set/retrieve LS of table cursor */
#define JET_bitLSTable              0x00000004  /*  set/retrieve LS of table */

#define JET_LSNil                   (~(JET_LS)0)
#endif // JET_VERSION >= 0x0501

#if ( JET_VERSION >= 0x0601 )
    /* Flags for JetSetTableSequential and JetPrereadIndexRanges */

#define JET_bitPrereadForward       0x00000001  /*  Hint that the sequential traversal will be in the forward direction */
#define JET_bitPrereadBackward      0x00000002  /*  Hint that the sequential traversal will be in the backward direction */
#if ( JET_VERSION >= 0x0602 )
#define JET_bitPrereadFirstPage     0x00000004  /*  Only first page of long values should be preread */
#define JET_bitPrereadNormalizedKey 0x00000008  /*  Normalized key/bookmark provided instead of column value */
#define bitPrereadSingletonRanges   0x00000010  /*  Internal: All ranges are singleton ranges */
#define bitPrereadDoNotDoOLD2       0x00000020  /*  Internal: Do not perform OLD2 if fragmentation detected, used by delete cleanup task */
#if ( JET_VERSION >= 0x0A01 )
#define bitPrereadSkip              0x00000040  /*  Internal: Just figure out the pages which needs to be preread but skip the actual preread */
#define bitIncludeNonLeafRead       0x00000080  /*  Internal: Include the non-leaf nodes in the list of pgnos read as well */
#endif // JET_VERSION >= 0x0A01
#endif // JET_VERSION >= 0x0602
#endif // JET_VERSION >= 0x0601

    /* Flags for JetOpenTempTable */

#define JET_bitTTIndexed            0x00000001  /* Allow seek */
#define JET_bitTTUnique             0x00000002  /* Remove duplicates */
#define JET_bitTTUpdatable          0x00000004  /* Allow updates */
#define JET_bitTTScrollable         0x00000008  /* Allow backwards scrolling */
#define JET_bitTTSortNullsHigh      0x00000010  /* NULL sorts after data for all columns in the index */
#define JET_bitTTForceMaterialization       0x00000020                      /* Forces temp. table to be materialized into a btree (allows for duplicate detection) */
#if ( JET_VERSION >= 0x0501 )
#define JET_bitTTErrorOnDuplicateInsertion  JET_bitTTForceMaterialization   /* Error always returned when duplicate is inserted (instead of dupe being silently removed) */
#endif // JET_VERSION >= 0x0501
#if ( JET_VERSION >= 0x0502 )
#define JET_bitTTForwardOnly        0x00000040  /* Prevents temp. table from being materialized into a btree (and enables duplicate keys) */
#endif // JET_VERSION >= 0x0502
#if ( JET_VERSION >= 0x0601 )
#define JET_bitTTIntrinsicLVsOnly   0x00000080  //  permit only intrinsic LV's (so materialisation is not required simply because a TT has an LV column)
#endif // JET_VERSION >= 0x0601
#if ( JET_VERSION >= 0x0602 )
#define JET_bitTTDotNetGuid         0x00000100  //  sort all JET_coltypGUID columns according to .Net Guid sort order
#endif // JET_VERSION >= 0x0601

    /* Flags for JetSetColumn */

#define JET_bitSetAppendLV                  0x00000001
#define JET_bitSetOverwriteLV               0x00000004 /* overwrite JET_coltypLong* byte range */
#define JET_bitSetSizeLV                    0x00000008 /* set JET_coltypLong* size */
#define JET_bitSetZeroLength                0x00000020
#define JET_bitSetSeparateLV                0x00000040 /* force LV separation */
#define JET_bitSetUniqueMultiValues         0x00000080 /* prevent duplicate multi-values */
#define JET_bitSetUniqueNormalizedMultiValues   0x00000100 /* prevent duplicate multi-values, normalizing all data before performing comparisons */
#if ( JET_VERSION >= 0x0501 )
#define JET_bitSetRevertToDefaultValue      0x00000200 /* if setting last tagged instance to NULL, revert to default value instead if one exists */
#define JET_bitSetIntrinsicLV               0x00000400 /* store whole LV in record without bursting or return an error */
#endif // JET_VERSION >= 0x0501
#if ( JET_VERSION >= 0x0601 )
#define JET_bitSetUncompressed              0x00010000 /* don't attempt compression when storing the data */
#define JET_bitSetCompressed                0x00020000 /* attempt compression when storing the data */
#if ( JET_VERSION >= 0x0A01 )
#define JET_bitSetContiguousLV              0x00040000 /* Allocates the long-value across contiguous pages (at potentialy space saving costs) for better IO behavior. Valid only with JET_bitSetSeparateLV. Invalid (or not implemented) with certain long-value operations such as replace, and certain column options such as compression. Use across many varying LVs sizes may cause space fragmentation / allocation issues. */
#endif // JET_VERSION >= 0x0A01
#endif // JET_VERSION >= 0x0601

#if ( JET_VERSION >= 0x0601 )
    /*  Space Hint Flags / JET_SPACEHINTS   */

//  Generic
#define JET_bitSpaceHintsUtilizeParentSpace         0x00000001  //  This changes the internal allocation policy to get space hierarchically from a B-Tree's immediate parent.
//  Create
#define JET_bitCreateHintAppendSequential           0x00000002  //  This bit will enable Append split behavior to grow according to the growth dynamics of the table (set by cbMinExtent, ulGrowth, cbMaxExtent).
#define JET_bitCreateHintHotpointSequential         0x00000004  //  This bit will enable Hotpoint split behavior to grow according to the growth dynamics of the table (set by cbMinExtent, ulGrowth, cbMaxExtent).
//  Retrieve
#define JET_bitRetrieveHintReserve1                 0x00000008  //  Reserved and ignored
#define JET_bitRetrieveHintTableScanForward         0x00000010  //  By setting this the client indicates that forward sequential scan is the predominant usage pattern of this table (causing B+ Tree defrag to be auto-triggered to clean it up if fragmented).
#define JET_bitRetrieveHintTableScanBackward        0x00000020  //  By setting this the client indicates that backwards sequential scan is the predominant usage pattern of this table (causing B+ Tree defrag to be auto-triggered to clean it up if fragmented).
#define JET_bitRetrieveHintReserve2                 0x00000040  //  Reserved and ignored
#define JET_bitRetrieveHintReserve3                 0x00000080  //  Reserved and ignored
//  Update
//#define JET_bitUpdateReserved                     0x00000000  //  TBD.
//  Delete
#define JET_bitDeleteHintTableSequential            0x00000100  //  This means that the application expects this table to be cleaned up in-order sequentially (from lowest key to highest key)
#endif // JET_VERSION >= 0x0601

#if ( JET_VERSION >= 0x0A01 )
#define JET_bitSpaceHintsUtilizeExactExtents        0x00000200  //  This changes the internal allocation policy to always allocate extents of the size requested by the space hints.
#endif // JET_VERSION >= 0x0A01

    /*  Set column parameter structure for JetSetColumns */

typedef struct
{
    JET_COLUMNID            columnid;
    const void              *pvData;
    uint32_t           cbData;
    JET_GRBIT               grbit;
    uint32_t           ibLongValue;
    uint32_t           itagSequence;
    JET_ERR                 err;
} JET_SETCOLUMN;

#if ( JET_VERSION >= 0x0501 )
typedef struct
{
    uint32_t   paramid;
    JET_API_PTR     lParam;
    const char      *sz;
    JET_ERR         err;
} JET_SETSYSPARAM_A;

typedef struct
{
    uint32_t   paramid;
    JET_API_PTR     lParam;
    const char16_t     *sz;
    JET_ERR         err;
} JET_SETSYSPARAM_W;

#ifdef JET_UNICODE
typedef JET_SETSYSPARAM_W JET_SETSYSPARAM;
#else
typedef JET_SETSYSPARAM_A JET_SETSYSPARAM;
#endif

#endif // JET_VERSION >= 0x0501

    /* Options for JetPrepareUpdate */

#define JET_prepInsert                      0
#define JET_prepReplace                     2
#define JET_prepCancel                      3
#define JET_prepReplaceNoLock               4
#define JET_prepInsertCopy                  5
// #define JET_prepInsertCopyWithoutSLVColumns  6   //  same as JET_prepInsertCopy, except that SLV columns are nullified instead of copied in the new record */
#if ( JET_VERSION >= 0x0501 )
#define JET_prepInsertCopyDeleteOriginal    7   //  used for updating a record in the primary key; avoids the delete/insert process and updates autoinc */
#endif // JET_VERSION >= 0x0501
#define JET_prepReadOnlyCopy                8   //  copy record into copy buffer for read-only purposes
#if ( JET_VERSION >= 0x0603 )
#define JET_prepInsertCopyReplaceOriginal   9   //  used for updating a record in the primary key; avoids the delete/insert process and keeps autoinc */
#endif // JET_VERSION >= 0x0603
#if ( JET_VERSION >= 0x0A01 )
#define JET_prepInsertMustSetAutoIncrement  10  //  this option has the same behavior as JET_prepInsert, but the caller must set the auto-increment column explicitly */
#endif // JET_VERSION >= 0x0A01
#if ( JET_VERSION >= 0x0603 )
// Values for JET_paramEnableSqm
#define JET_sqmDisable                      0   //  Explicitly disable SQM
#define JET_sqmEnable                       1   //  Explicitly enable SQM
#define JET_sqmFromCEIP                     2   //  Enables SQM based on Customer Experience Improvement Program opt-in
#endif // JET_VERSION >= 0x0603

    //  Flags for JetUpdate
#if ( JET_VERSION >= 0x0502 )
#define JET_bitUpdateCheckESE97Compatibility    0x00000001  //  check whether record fits if represented in ESE97 database format
#endif // JET_VERSION >= 0x0502
#define JET_bitUpdateNoVersion                  0x00000002  //  do not create rollback or versioning information for update
#if ( JET_VERSION >= 0x0A01 )
#define JET_bitUpdateEnforceOptionallyUniqueIndices 0x00000004 // Enforce optionally unique indices.
#endif // JET_VERSION >= 0x0A01
    /* Flags for JetEscrowUpdate */
#define JET_bitEscrowNoRollback             0x0001

    /* Flags for JetRetrieveColumn */

#define JET_bitRetrieveCopy                 0x00000001
#define JET_bitRetrieveFromIndex            0x00000002
#define JET_bitRetrieveFromPrimaryBookmark  0x00000004
#define JET_bitRetrieveTag                  0x00000008
#define JET_bitRetrieveNull                 0x00000010  /*  for columnid 0 only */
#define JET_bitRetrieveIgnoreDefault        0x00000020  /*  for columnid 0 only */
#define JET_bitRetrieveLongId               0x00000040
#define JET_bitRetrieveLongValueRefCount    0x00000080  /*  for testing use only */
// #define JET_bitRetrieveSLVAsSLVInfo          0x00000100  /*  internal use only  */

    /* Flags for JetRetrieveColumn when the SLV Provider is enabled  */

// #define JET_bitRetrieveSLVAsSLVFile          0x00000200 /* retrieve SLV as an SLV File handle */
// #define JET_bitRetrieveSLVAsSLVEA            0x00000400 /* retrieve SLV as an SLV EA list */
#if ( JET_VERSION >= 0x0600 )
#define JET_bitRetrieveTuple                0x00000800 /* retrieve tuple fragment from index */
#endif // JET_VERSION >= 0x0600
#if ( JET_VERSION >= 0x0601 )
#define JET_bitRetrievePageNumber           0x00001000 /* page number list for column */
#endif // JET_VERSION >= 0x0600

#define JET_bitRetrieveCopyIntrinsic        0x00002000  /*  retrieves size of data that can be added to a record before offloading long columns.  Fixed sized columns return 0 or column size. */

//  Has no effect on non-separate long value retrievals.  On separated long values,
//  initiate read of separate long value data without waiting for read to complete.
//  cbActual will be 0 becuase no data is read for separate LVs.
//  Currently only reads one chunk at ibOffset given and will not read all data based on cbMax.
//  If cbMax greater than single chunk given then JET_wrnNyi returned.
//
#define JET_bitRetrievePrereadOnly          0x00004000
//  Causes more pages to be preread than might be needed in an effort to improve performance.
//
#define JET_bitRetrievePrereadMany          0x00008000
#if ( JET_VERSION >= 0x0603 )
#define JET_bitRetrievePhysicalSize         0x00010000  /* retrieves compressed size only in cbActual; no data is retrieved */
#endif // JET_VERSION >= 0x0603
#if ( JET_VERSION >= 0x0A01 )
#define JET_bitRetrieveAsRefIfNotInRecord   0x00020000 /* Retrieves the column value as a reference if it is not in the record. */
#endif // JET_VERSION >= 0x0A01
#if ( JET_VERSION >= 0x0602 )
    /* Flags for JET_INDEX_COLUMN */
#define JET_bitZeroLength                   0x00000001
#endif

    /* Retrieve column parameter structure for JetRetrieveColumns */

typedef struct
{
    JET_COLUMNID        columnid;
    void                *pvData;
    uint32_t       cbData;
    uint32_t       cbActual;
    JET_GRBIT           grbit;
    uint32_t       ibLongValue;
    uint32_t       itagSequence;
    JET_COLUMNID        columnidNextTagged;
    JET_ERR             err;
} JET_RETRIEVECOLUMN;
typedef struct
{
    JET_COLUMNID            columnid;
    unsigned short          cMultiValues;

    union
    {
        unsigned short      usFlags;
        struct
        {
            unsigned short  fLongValue:1;           //  is column LongText/Binary?
            unsigned short  fDefaultValue:1;        //  was a default value retrieved?
            unsigned short  fNullOverride:1;        //  was there an explicit null to override a default value?
            unsigned short  fDerived:1;             //  was column derived from template table?
        };
    };
} JET_RETRIEVEMULTIVALUECOUNT;
#if ( JET_VERSION >= 0x0501 )
    /* Flags for JetEnumerateColumns */

#define JET_bitEnumerateCopy                        JET_bitRetrieveCopy
#define JET_bitEnumerateIgnoreDefault               JET_bitRetrieveIgnoreDefault
#define JET_bitEnumerateLocal                       0x00010000
#define JET_bitEnumeratePresenceOnly                0x00020000
#define JET_bitEnumerateTaggedOnly                  0x00040000
#define JET_bitEnumerateCompressOutput              0x00080000
#if ( JET_VERSION >= 0x0502 )
// Available on Server 2003 SP1
#define JET_bitEnumerateIgnoreUserDefinedDefault    0x00100000
#endif // JET_VERSION >= 0x0502
#if ( JET_VERSION >= 0x0601 )
#define JET_bitEnumerateInRecordOnly                0x00200000
#endif // JET_VERSION >= 0x0601
#if ( JET_VERSION >= 0x0A01 )
#define JET_bitEnumerateAsRefIfNotInRecord          0x00400000 /* Retrieves the column value as a reference if it is not in the record. */
#endif // JET_VERSION >= 0x0A01
    /* Parameter structures for JetEnumerateColumns */

typedef struct
{
    JET_COLUMNID            columnid;
    uint32_t           ctagSequence;
    uint32_t*          rgtagSequence;
} JET_ENUMCOLUMNID;

typedef struct
{
    uint32_t           itagSequence;
    JET_ERR                 err;
    uint32_t           cbData;
    void*                   pvData;
} JET_ENUMCOLUMNVALUE;

typedef struct
{
    JET_COLUMNID            columnid;
    JET_ERR                 err;
    union
    {
        struct /* err != JET_wrnColumnSingleValue */
        {
            uint32_t           cEnumColumnValue;
            JET_ENUMCOLUMNVALUE*    rgEnumColumnValue;
        };
        struct /* err == JET_wrnColumnSingleValue */
        {
            uint32_t           cbData;
            void*                   pvData;
        };
    };
} JET_ENUMCOLUMN;

    /* Realloc callback for JetEnumerateColumns */

typedef void* (JET_API *JET_PFNREALLOC)(
    void *     pvContext,
    void *     pv,
    uint32_t  cb );

#endif // JET_VERSION >= 0x0501
#if ( JET_VERSION >= 0x0A01 )

/* Flags for JetStreamRecords */

#define JET_bitStreamForward            0x00000001  /*  Stream records in ascending key order. */
#define JET_bitStreamBackward           0x00000002  /*  Stream records in descending key order. */
#define JET_bitStreamColumnReferences   0x00000004  /*  Stream column values as column references when not in the record. */

#endif // JET_VERSION >= 0x0A01
#if ( JET_VERSION >= 0x0600 )
    /* Flags for JetGetRecordSize */

#define JET_bitRecordSizeInCopyBuffer           0x00000001  //  use record in copy buffer
#define JET_bitRecordSizeRunningTotal           0x00000002  //  increment totals in output buffer instead of setting them
#define JET_bitRecordSizeLocal                  0x00000004  //  ignore Long Values (and other data otherwise not in the same page as the record)
// These will remain private until we need them.
#define JET_bitRecordSizeIncludeDefaultValues   0x00000008  //  compute size of default-valued columns (CURRENTLY UNSUPPORTED)
#define JET_bitRecordSizeSecondaryIndexKeyOnly  0x00000010  //  compute size of secondary index key instead of record size (CURRENTLY UNSUPPORTED)
#define JET_bitRecordSizeIntrinsicPhysicalOnly  0x00000020  //  only get physical size for intrinsic columns (cheaper)
#define JET_bitRecordSizeExtrinsicLogicalOnly   0x00000040  //  only get logical size for extrinsic columns (cheaper)
    /* parameter structures for JetGetRecordSize */

typedef struct
{
    uint64_t    cbData;                 //  user data in record
    uint64_t    cbLongValueData;        //  user data associated with the record but stored in the long-value tree (NOTE: does NOT count intrinsic long-values)
    uint64_t    cbOverhead;             //  record overhead
    uint64_t    cbLongValueOverhead;    //  overhead of long-value data (NOTE: does not count intrinsic long-values)
    uint64_t    cNonTaggedColumns;      //  total number of fixed/variable columns
    uint64_t    cTaggedColumns;         //  total number of tagged columns
    uint64_t    cLongValues;            //  total number of values stored in the long-value tree for this record (NOTE: does NOT count intrinsic long-values)
    uint64_t    cMultiValues;           //  total number of values beyond the first for each column in the record
} JET_RECSIZE;
#endif // JET_VERSION >= 0x0600
#if ( JET_VERSION >= 0x0600 )
typedef struct tagJET_PAGEINFO
{
    uint32_t       pgno;                   //  pgno for the page. must be passed in
    uint32_t       fPageIsInitialized:1;   //  false if the page is zeroed
    uint32_t       fCorrectableError:1;    //  correctable error found on page
    uint64_t    checksumActual;         //  checksum stored on the page
    uint64_t    checksumExpected;       //  checksum expected for the page
    uint64_t    dbtime;                 //  dbtime on the page
    uint64_t    structureChecksum;      //  checksum of the page structure
    uint64_t    flags;                  //  currently unused
} JET_PAGEINFO;
#endif // JET_VERSION >= 0x0600
#if ( JET_VERSION >= 0x0601 )
typedef struct
{
    uint64_t    cbData;                 //  user data in record
    uint64_t    cbLongValueData;        //  user data associated with the record but stored in the long-value tree (NOTE: does NOT count intrinsic long-values)
    uint64_t    cbOverhead;             //  record overhead
    uint64_t    cbLongValueOverhead;    //  overhead of long-value data (NOTE: does not count intrinsic long-values)
    uint64_t    cNonTaggedColumns;      //  total number of fixed/variable columns
    uint64_t    cTaggedColumns;         //  total number of tagged columns
    uint64_t    cLongValues;            //  total number of values stored in the long-value tree for this record (NOTE: does NOT count intrinsic long-values)
    uint64_t    cMultiValues;           //  total number of values beyond the first for each column in the record
    uint64_t    cCompressedColumns;     //  total number of columns which are compressed
    uint64_t    cbDataCompressed;       //  compressed size of user data in record (same as cbData if no intrinsic long-values are compressed)
    uint64_t    cbLongValueDataCompressed;  // compressed size of user data in the long-value tree (same as cbLongValue data if no separated long values are compressed)
} JET_RECSIZE2;
#endif // JET_VERSION >= 0x0601
#if ( JET_VERSION >= 0x0A01 )
typedef struct
{
    uint64_t    cbData;                 //  user data in record
    uint64_t    cbLongValueData;        //  user data associated with the record but stored in the long-value tree (NOTE: does NOT count intrinsic long-values)
    uint64_t    cbOverhead;             //  record overhead
    uint64_t    cbLongValueOverhead;    //  overhead of long-value data (NOTE: does not count intrinsic long-values)
    uint64_t    cNonTaggedColumns;      //  total number of fixed/variable columns
    uint64_t    cTaggedColumns;         //  total number of tagged columns
    uint64_t    cLongValues;            //  total number of values stored in the long-value tree for this record (NOTE: does NOT count intrinsic long-values)
    uint64_t    cMultiValues;           //  total number of values beyond the first for each column in the record
    uint64_t    cCompressedColumns;     //  total number of columns which are compressed
    uint64_t    cbDataCompressed;       //  compressed size of user data in record (same as cbData if no intrinsic long-values are compressed)
    uint64_t    cbLongValueDataCompressed;  // compressed size of user data in the long-value tree (same as cbLongValue data if no separated long values are compressed)
    uint64_t    cbIntrinsicLongValueData;   // user data stored in intrinsic LVs (in the record).
    uint64_t    cbIntrinsicLongValueDataCompressed;   // compressed size of user data stored in intrinsic LVs (in the record).
    uint64_t    cIntrinsicLongValues;       // total number of intrinsic LVs stored in the record.
    uint64_t    cbKey;                  //  Key size in bytes. Doesn't include storage overhead. Does include key normalization overhead.
} JET_RECSIZE3;

#endif // JET_VERSION >= 0x0A01

#if ( JET_VERSION >= 0x0601 )
typedef struct tagJET_PAGEINFO2
{
    JET_PAGEINFO        pageInfo;
    uint64_t    rgChecksumActual[ 3 ];  //  more checksum stored on the page
    uint64_t    rgChecksumExpected[ 3]; //  more checksum expected for the page
} JET_PAGEINFO2;
#endif // JET_VERSION >= 0x0601

    /* Flags for JetBeginTransaction2 */

#if ( JET_VERSION >= 0x0501 )
#define JET_bitTransactionReadOnly      0x00000001  /* transaction will not modify the database */
#endif // JET_VERSION >= 0x0501
#define JET_bitDistributedTransaction   0x00000002  /* transaction will require two-phase commit */
#define bitTransactionWritableDuringRecovery 0x00000004 /* internal updatable transaction during recovery */
    /* Flags for JetCommitTransaction */

#define JET_bitCommitLazyFlush          0x00000001  /* lazy flush log buffers. */
#define JET_bitWaitLastLevel0Commit     0x00000002  /* wait for last level 0 commit record flushed */
#if ( JET_VERSION >= 0x0502 )
#define JET_bitWaitAllLevel0Commit      0x00000008  /* wait for all level 0 commits to be flushed */
#endif // JET_VERSION >= 0x0502
#if ( JET_VERSION >= 0x0601 )
#define JET_bitForceNewLog              0x00000010
#endif // JET_VERSION >= 0x0601
#if ( JET_VERSION >= 0x0A00 )
#define JET_bitCommitRedoCallback       0x00000020
#endif // JET_VERSION >= 0x0A00

#define JET_bitCommitFlush_OLD          0x00000001  /* commit and flush page buffers. */
#define JET_bitCommitLazyFlush_OLD      0x00000004  /* lazy flush log buffers. */
#define JET_bitWaitLastLevel0Commit_OLD 0x00000010  /* wait for last level 0 commit record flushed */
    /* Flags for JetRollback */

#define JET_bitRollbackAll              0x00000001
#if ( JET_VERSION >= 0x0A00 )
#define JET_bitRollbackRedoCallback     0x00000020
#endif // JET_VERSION >= 0x0A00
#if ( JET_VERSION >= 0x0600 )
    /* Flags for JetOSSnapshot APIs */

    /* Flags for JetOSSnapshotPrepare */
#define JET_bitIncrementalSnapshot      0x00000001  /* bit 0: full (0) or incremental (1) snapshot */
#define JET_bitCopySnapshot             0x00000002  /* bit 1: normal (0) or copy (1) snapshot */
#define JET_bitContinueAfterThaw        0x00000004  /* bit 2: end on thaw (0) or wait for [truncate +] end snapshot */
#if ( JET_VERSION >= 0x0601 )
#define JET_bitExplicitPrepare          0x00000008  /* bit 3: all instaces prepared by default (0) or no instance prepared by default (1)  */
#endif // JET_VERSION >= 0x0601

    /* Flags for JetOSSnapshotTruncateLog & JetOSSnapshotTruncateLogInstance */
#define JET_bitAllDatabasesSnapshot     0x00000001  /* bit 0: there are detached dbs in the instance (i.e. can't truncate logs) */

    /* Flags for JetOSSnapshotEnd */
#define JET_bitAbortSnapshot            0x00000001  /* snapshot process failed */
#endif // JET_VERSION >= 0x0600
#if ( JET_VERSION >= 0x0601 )

    /* Flags for JET_EMITDATACTX and used by JET_PFNEMITLOGDATA and JetConsumeLogData */

#define JET_bitShadowLogEmitFirstCall           0x00000001  //  the very first emit has only this bit set, log data will likely follow next
#define JET_bitShadowLogEmitLastCall            0x00000002  //  the very last emit has only this bit set, no more log data will follow
#define JET_bitShadowLogEmitCancel              0x00000004  //  future: user requested this be cancelled via resetting the JET_param to NULL
#define JET_bitShadowLogEmitDataBuffers     0x00000008  //  callback emits some portion of the log buffer and position in log
#define JET_bitShadowLogEmitLogComplete     0x00000010  //  callback emits signal that the current log file is completed

    /* Information context surrounded data emitted from JET_PFNEMITLOGDATA */

typedef struct tag_JET_EMITDATACTX
{
    uint32_t               cbStruct;
    uint32_t               dwVersion;
    uint64_t            qwSequenceNum;
    JET_GRBIT                   grbitOperationalFlags;
    JET_LOGTIME                 logtimeEmit;
    JET_LGPOS                   lgposLogData;
    uint32_t               cbLogData;
} JET_EMITDATACTX;
// 40 bytes

    /* Callback for JET_param JET_paramEmitLogDataCallback */

typedef JET_ERR (JET_API * JET_PFNEMITLOGDATA)(
    JET_INSTANCE        instance,
    JET_EMITDATACTX *   pEmitLogDataCtx,
    void *              pvLogData,
    uint32_t       cbLogData,
    void *              callbackCtx );

#endif // JET_VERSION >= 0x0601
    /* Info parameter for JetGetDatabaseInfo and JetGetDatabaseFileInfo */

#define JET_DbInfoFilename          0
#define JET_DbInfoConnect           1
#define JET_DbInfoCountry           2   //  retrieves the default country/region
#if ( JET_VERSION >= 0x0501 )
#define JET_DbInfoLCID              3
#endif // JET_VERSION >= 0x0501
#define JET_DbInfoLangid            3       // OBSOLETE: use JET_DbInfoLCID instead
#define JET_DbInfoCp                4
#define JET_DbInfoCollate           5
#define JET_DbInfoOptions           6
#define JET_DbInfoTransactions      7
#define JET_DbInfoVersion           8
#define JET_DbInfoIsam              9
#define JET_DbInfoFilesize          10
#define JET_DbInfoSpaceOwned        11
#define JET_DbInfoSpaceAvailable    12
#define JET_DbInfoUpgrade           13
#define JET_DbInfoMisc              14
#if ( JET_VERSION >= 0x0501 )
#define JET_DbInfoDBInUse           15
#define JET_DbInfoHasSLVFile_Obsolete       16
#define JET_DbInfoPageSize          17
#endif // JET_VERSION >= 0x0501
#define JET_DbInfoStreamingFileSpace_Obsolete       18  //  SLV owned and available space (may be slow because this sequentially scans the SLV space tree)
#if ( JET_VERSION >= 0x0600 )
#define JET_DbInfoFileType          19
#define JET_DbInfoStreamingFileSize_Obsolete        20      //  SLV owned space only (fast because it does NOT scan the SLV space tree)
#if ( JET_VERSION >= 0x603 )
#define JET_DbInfoFilesizeOnDisk    21
#endif
#if ( JET_VERSION >= 0x0A01 )
#define dbInfoSpaceShelved          22  /*  INTERNAL USE ONLY */
#endif
#define JET_DbInfoSplitBuffers      23
#define JET_DbInfoUseCachedResult   0x40000000   /* Obsolete, this behavior is now always on */

    /* Info parameter for JetGetLogFileInfo */

#define JET_LogInfoMisc             0
#if ( JET_VERSION >= 0x0601 )
#define JET_LogInfoMisc2            1
#if ( JET_VERSION >= 0x0A01 )
#define JET_LogInfoMisc3            2
#endif
#endif

/* Info parameter for JetGetRBSFileInfo */

#define JET_RBSFileInfoMisc              0
#endif // JET_VERSION >= 0x0600

    /* Dbstates from JetGetDatabaseFileInfo */

#define JET_dbstateJustCreated                  1
#define JET_dbstateDirtyShutdown                2
#define JET_dbstateCleanShutdown                3
#define JET_dbstateBeingConverted               4
#if ( JET_VERSION >= 0x0501 )
#define JET_dbstateForceDetach                  5
#endif // JET_VERSION >= 0x0501
#if ( JET_VERSION >= 0x0601 )
#define JET_dbstateIncrementalReseedInProgress  6
#define JET_dbstateDirtyAndPatchedShutdown      7   // Database has extensive multi-log recovery requirements due to some form of database page patching.
#endif // JET_VERSION >= 0x0601
#if ( JET_VERSION >= 0x0A01 )
#define JET_dbstateRevertInProgress             8   // Database is currently being revert to a previous state using the revert snapshots.
#endif // JET_VERSION >= 0x0A01
#if ( JET_VERSION >= 0x0600 )

    //  supported file types (returned from JetGetDatabaseFileInfo with JET_DbInfoFileType)

#define JET_filetypeUnknown                 0
#define JET_filetypeDatabase                1
#define JET_filetypeStreamingFile           2
#define JET_filetypeLog                     3
#define JET_filetypeCheckpoint              4
#define JET_filetypeTempDatabase            5
#define JET_filetypeFTL                     6
#define JET_filetypeFlushMap                7
#define JET_filetypeCachedFile              8
#define JET_filetypeCachingFile             9
#define JET_filetypeSnapshot                10
#define JET_filetypeRBSRevertCheckpoint     11
#define JET_filetypeMax                     12
#endif // JET_VERSION >= 0x0600

    /* Column data types */

#define JET_coltypNil               0
#define JET_coltypBit               1   /* True, False, or NULL */
#define JET_coltypUnsignedByte      2   /* 1-byte integer, unsigned */
#define JET_coltypShort             3   /* 2-byte integer, signed */
#define JET_coltypLong              4   /* 4-byte integer, signed */
#define JET_coltypCurrency          5   /* 8 byte integer, signed */
#define JET_coltypIEEESingle        6   /* 4-byte IEEE single precision */
#define JET_coltypIEEEDouble        7   /* 8-byte IEEE double precision */
#define JET_coltypDateTime          8   /* Integral date, fractional time */
#define JET_coltypBinary            9   /* Binary data, < 255 bytes */
#define JET_coltypText              10  /* ANSI text, case insensitive, < 255 bytes */
#define JET_coltypLongBinary        11  /* Binary data, long value */
#define JET_coltypLongText          12  /* ANSI text, long value */

// Pre XP
#if ( JET_VERSION < 0x0501 )
#define JET_coltypMax               13  /* the number of column types  */
                                        /* used for validity tests and */
                                        /* array declarations.         */
#endif // JET_VERSION < 0x0501

// Windows XP
#if ( JET_VERSION >= 0x0501 )
#define JET_coltypSLV               13  /* SLV's. Obsolete. */

#if ( JET_VERSION < 0x0600 )
#define JET_coltypMax               14  /* the number of column types  */
                                        /* used for validity tests and */
                                        /* array declarations.         */
#endif // JET_VERSION == 0x0501

#endif // JET_VERSION >= 0x0501

// Windows Vista to Windows 8.1
#if ( JET_VERSION >= 0x0600 )
#define JET_coltypUnsignedLong      14  /* 4-byte unsigned integer */
#define JET_coltypLongLong          15  /* 8-byte signed integer */
#define JET_coltypGUID              16  /* 16-byte globally unique identifier */
#define JET_coltypUnsignedShort     17  /* 2-byte unsigned integer */

#if ( JET_VERSION >= 0x0600 && JET_VERSION <= 0x0603 )
#define JET_coltypMax               18  /* the number of column types  */
                                        /* used for validity tests and */
                                        /* array declarations.         */
#endif // ( JET_VERSION >= 0x0600 && JET_VERSION <= 0x0603 )

#endif // JET_VERSION >= 0x0600

// Windows 10
#if ( JET_VERSION >= 0x0A00 )
#define JET_coltypUnsignedLongLong  18  /* 8-byte unsigned integer */
#define JET_coltypMax               19  /* the number of column types  */
                                        /* used for validity tests and */
                                        /* array declarations.         */
#endif // JET_VERSION >= 0x0A00
#if ( JET_VERSION >= 0x0A01 )

    /* RBS revert states */
#define JET_revertstateNone                     0   // Revert has not yet started/default state.
#define JET_revertstateInProgress               1   // Revert snapshots are currently being applied to the databases.
#define JET_revertstateCopingLogs               2   // The required logs to bring databases to a clean state are being copied to the log directory after revert.
#define JET_revertstateBackupSnapshot           3   // Backs up revert snapshots for investigation purposes.
#define JET_revertstateRemoveSnapshot           4   // Removes the snapshot which have been applied to the databases and backed up.
#define JET_revertstateCaptureRootPageRecords   5   // Indicates that we need to capture the root pages records' FDPDeleteFlag state into a temporary file for crash consistency.
#define JET_revertstateApplyRootPageRecords     6   // Indicates that we need to apply the root page records and update the FDPDeleteFlag state.

    /* RBS revert grbits */
#define JET_bitDeleteAllExistingLogs        0x00000001  /* Delete all the existing log files at the end of revert. */

    /* Delete table grbit */
#define JET_bitNonRevertableTableDelete         0x00000001  // If set, doesn't capture page preimages to allow for reverting the table to a state where it still existed using RBS.
#define JET_bitRevertableTableDeleteIfTooSoon   0x00000002  // If set, we will do a revertable table delete even if NonRevertableTableDelete flag is passed provided NonRevertable delete is failing due to JET_errRBSDeleteTableTooSoon.

#endif // JET_VERSION >= 0x0A01

#if ( JET_VERSION >= 0x0600 )
        /* Info levels for JetGetSessionInfo */

#define JET_SessionInfo             0U
#endif // !JET_VERSION >= 0x0600
    /* Info levels for JetGetObjectInfo */

#define JET_ObjInfo                 0U
#define JET_ObjInfoListNoStats      1U
#define JET_ObjInfoList             2U
#define JET_ObjInfoSysTabCursor     3U
#define JET_ObjInfoListACM          4U /* Blocked by JetGetObjectInfo */
#define JET_ObjInfoNoStats          5U
#define JET_ObjInfoSysTabReadOnly   6U
#define JET_ObjInfoRulesLoaded      7U
#define JET_ObjInfoMax              8U

    /* Info levels for JetGetTableInfo/JetSetTableInfo */

#define JET_TblInfo                    0U
#define JET_TblInfoName                1U
#define JET_TblInfoDbid                2U
#define JET_TblInfoMostMany            3U
#define JET_TblInfoRvt                 4U
#define JET_TblInfoOLC                 5U
#define JET_TblInfoResetOLC            6U
#define JET_TblInfoSpaceUsage          7U
#define JET_TblInfoDumpTable           8U
#define JET_TblInfoSpaceAlloc          9U
#define JET_TblInfoSpaceOwned         10U         // OwnExt for primary, 2ndary indices, and LV
#define JET_TblInfoSpaceAvailable     11U         // AvailExt for primary, 2ndary indices, and LV
#define JET_TblInfoTemplateTableName  12U
#if ( JET_VERSION >= 0x0A01 )
#define JET_TblInfoLVChunkMax         13U
#define JET_TblInfoEncryptionKey      14U
//#define JET_TblInfoUnused                       // Skipped during development, may be reused.
#define JET_TblInfoRetrieveAndReserveAutoIncrement 16U  // Retrieves the current table-wide auto-increment counter and increments its value. Only valid with JetGetTableInfo.
#endif
#if ( JET_VERSION >= 0x0A01 )
#define JET_TblInfoSpaceOwnedLV       17U         // OwnExt for LV
#define JET_TblInfoSpaceAvailableLV   18U         // AvailExt for LV
#define JET_TblInfoObjectId           19U
#endif

    /* Info levels for JetGetIndexInfo and JetGetTableIndexInfo */

#define JET_IdxInfo                 0U
#define JET_IdxInfoList             1U
#define JET_IdxInfoSysTabCursor     2U      //  OBSOLETE and unused.
#define JET_IdxInfoOLC              3U      //  OBSOLETE and unused.
#define JET_IdxInfoResetOLC         4U      //  OBSOLETE and unused.
#define JET_IdxInfoSpaceAlloc       5U
#if ( JET_VERSION >= 0x0501 )
#define JET_IdxInfoLCID             6U
#endif // JET_VERSION >= 0x0501
#define JET_IdxInfoLangid           6U      //  OBSOLETE: use JET_IdxInfoLCID instead
#define JET_IdxInfoCount            7U
#define JET_IdxInfoVarSegMac        8U
#define JET_IdxInfoIndexId          9U
#if ( JET_VERSION >= 0x0600 )
#define JET_IdxInfoKeyMost          10U
#endif // JET_VERSION >= 0x0600
#if ( JET_VERSION >= 0x0601 )
#define JET_IdxInfoCreateIndex      11U     //  return a JET_INDEXCREATE structure suitable for use by JetCreateIndex2()
#define JET_IdxInfoCreateIndex2     12U     //  return a JET_INDEXCREATE2 structure suitable for use by JetCreateIndex3()
#endif // JET_VERSION >= 0x0601
#if ( JET_VERSION >= 0x0602 )
#define JET_IdxInfoCreateIndex3     13U     //  return a JET_INDEXCREATE3 structure suitable for use by JetCreateIndex4()
#define JET_IdxInfoLocaleName       14U     //  Returns the locale name, which can be a wide string of up to LOCALE_NAME_MAX_LENGTH (including null).
#endif // JET_VERSION >= 0x0602
// These bits aren't public at the moment. Not because they are confidential, but
// because it's leaking OS implementation details. If they are useful to clients,
// then they should be exposed.
#if ( JET_VERSION >= 0x0A00 )
#define JET_IdxInfoSortVersion      15U     //  Returns a 32-bit integer representing the sort version for Unicode indices. (NLSVERSIONINFO.dwNLSVersion)
#define JET_IdxInfoDefinedSortVersion   16U     //  Returns a 32-bit integer representing the sort version for Unicode indices. (NLSVERSIONINFO.dwDefinedVersion). Only relevant on pre-Windows8.
#define JET_IdxInfoSortId           17U     //  Returns a Sort ID (GUID) used for sorting Unicode text.
#endif // JET_VERSION >= 0x0A00
#if ( JET_VERSION >= 0x0A01 )
#define JET_IdxInfoSpaceOwned       18U    // Space owned exclusively by this index (unlike tables, ignores space from 2ndary indices, even
                                           //     when inquiring about primary indices
#define JET_IdxInfoSpaceAvailable   19U    // Space available exclusively for this index
#endif

    /* Info levels for JetGetColumnInfo and JetGetTableColumnInfo */

#define JET_ColInfo                 0U
#define JET_ColInfoList             1U
#define JET_ColInfoSysTabCursor     3U
#define JET_ColInfoBase             4U
#define JET_ColInfoListCompact      5U      //  INTERNAL USE ONLY
#if ( JET_VERSION >= 0x0501 )
#define JET_ColInfoByColid          6U
#define JET_ColInfoListSortColumnid 7U      //  OBSOLETE: use grbit instead
#endif // JET_VERSION >= 0x0501
#if ( JET_VERSION >= 0x0600 )
#define JET_ColInfoBaseByColid      8U
#endif // JET_VERSION >= 0x0600

#if ( JET_VERSION >= 0x0600 )

        // Grbits for JET_GetColumnInfo and JetGetTableColumnInfo (OR together with the info level)
#define JET_ColInfoGrbitNonDerivedColumnsOnly   0x80000000  //  for lists, only return non-derived columns (if the table is derived from a template)
#define JET_ColInfoGrbitMinimalInfo             0x40000000  //  for lists, only return the column name and columnid of each column
#define JET_ColInfoGrbitSortByColumnid          0x20000000  //  for lists, sort returned column list by columnid (default is to sort list by column name)
#define JET_ColInfoGrbitCompacting              0x10000000  //  INTERNAL USE ONLY
#endif // JET_VERSION >= 0x0600

#if ( JET_VERSION >= 0x0600 )

    /* Info levels for JetGetInstanceMiscInfo, which is very different than JetGetInstanceInfo, as that retrieves a list of all instances */

#define JET_InstanceMiscInfoLogSignature    0U
#define JET_InstanceMiscInfoCheckpoint      1U
#endif // JET_VERSION >= 0x0600

#if ( JET_VERSION >= 0x0A01 )

#define JET_InstanceMiscInfoRBS             2U // Retrieve revert snapshot info for the instance.

#endif // JET_VERSION >= 0x0A01
#if ( JET_VERSION >= 0x0600 )

    /* Info parameter for JetGetPageInfo */

#define JET_PageInfo                0U
#if ( JET_VERSION >= 0x0601 )
#define JET_PageInfo2           1U
#endif // JET_VERSION >= 0x0601

    /* grbits for JetGetPageInfo */

#if ( JET_VERSION >= 0x0602 )
#define JET_bitPageInfoNoStructureChecksum  0x00000001  /* Do not compute structure checksum */
#endif // JET_VERSION >= 0x0602

#endif // JET_VERSION >= 0x0600

#if ( JET_VERSION >= 0x0601 )

    /* Flags for JetPatchDatabasePages */

#define JET_bitTestUninitShrunkPageImage    0x00000001
#define JET_bitPatchingCorruptPage          0x00000002

#endif // JET_VERSION >= 0x0601

#if ( JET_VERSION >= 0x602 )

    /* Flags for JetOnlinePatchDatabasePage */

// #define JET_bitPatchAllowCorruption      0x00000002              //  OBSOLETE and UNSUPPORTED - don't checksum the page

#endif // JET_VERSION >= 0x0602

#if ( JET_VERSION >= 0x0601 )

    /* Flags for JetEndDatabaseIncrementalReseed */

#define JET_bitEndDatabaseIncrementalReseedCancel       0x00000001      //  Stop an incremental reseed operation prematurely for any (failing) reason.  Database will be left in inconsistent JET_dbstateIncrementalReseedInProgress state.

#endif // JET_VERSION >= 0x0601

#if ( JET_VERSION >= 0x0A01 )

    /* Flags for JetBeginDatabaseIncrementalReseed */

#define JET_bitBeginDatabaseIncrementalReseedPatchRBS   0x00000001      //  Try to patch RBS file as part of increseed. If this flag is not passed, we will lose the RBS files and the ability to revert back to the past.

#endif // JET_VERSION >= 0x0A01
    /* Engine Object Types */

#define JET_objtypNil               0
#define JET_objtypTable             1
#define JET_objtypDb                2
#define JET_objtypContainer         3
#define JET_objtypLongRoot          9   /*  INTERNAL USE ONLY */
    /* Compact Options */

#define JET_bitCompactStats             0x00000020  /* Dump off-line compaction stats (only when progress meter also specified) */
#define JET_bitCompactRepair            0x00000040  /* Don't preread and ignore duplicate keys */
// #define JET_bitCompactSLVCopy            0x00000080  /* Recreate SLV file, do not reuse the existing one */
#if ( JET_VERSION >= 0x0600 )
#define JET_bitCompactPreserveOriginal  0x00000100  /* Preserve original database */
#endif // JET_VERSION >= 0x0600
    /* Status Notification Processes */

#define JET_snpRepair                   2
#define JET_snpCompact                  4
#define JET_snpRestore                  8
#define JET_snpBackup                   9
#define JET_snpUpgrade                  10
#if ( JET_VERSION >= 0x0501 )
#define JET_snpScrub                    11
#define JET_snpUpgradeRecordFormat      12
#endif // JET_VERSION >= 0x0501
// #define JET_snpSLVChecksum               17
#if ( JET_VERSION >= 0x0602 )
#define JET_snpRecoveryControl          18
#define JET_snpExternalAutoHealing      19
#endif // JET_VERSION >= 0x0602
#if ( JET_VERSION >= 0x0A01 )
#define JET_snpSpaceCategorization      20
#endif // JET_VERSION >= 0x0A01
    /* Status Notification Types */

//  Generic Status Notification Types
#define JET_sntBegin            5   /* callback for beginning of operation */
#define JET_sntRequirements     7   /* callback for returning operation requirements */
#define JET_sntProgress         0   /* callback for progress */
#define JET_sntComplete         6   /* callback for completion of operation */
#define JET_sntFail             3   /* callback for failure during progress */
#if ( JET_VERSION >= 0x0602 )

//  Operation Specific "Status Notification" Types

//  JET_snpRecoveryControl specific JET_SNTs (controlled by JET_bitExternalRecoveryControl)
//
#define JET_sntOpenLog                  1001    /* callback for opening a new log */
#define JET_sntOpenCheckpoint           1002    /* callback for opening a checkpoint */
// Reserved JET_sntOpenDatabase         1003    /* callback for opening a database during recovery */
#define JET_sntMissingLog               1004    /* callback due to an expected log is missing */
#define JET_sntBeginUndo                1005    /* callback to indicate the desire to being undo */
#define JET_sntNotificationEvent        1006    /* callback to indicate an event that we would normally submit to the event log */
#define JET_sntSignalErrorCondition     1007    /* callback to indicate a generic error condition has arrisen */
// Reserved JET_sntHitRecoveryPoint     100?    /* callback to indicate we have reached the lgposStop set in the JET_RSTINFO[2]_* structure */
#define JET_sntAttachedDb               1008    /* database attached during recovery */
#define JET_sntDetachingDb              1009    /* database about to be detached during recovery */
#define JET_sntCommitCtx                1010    /* commit context found during recovery */

//  JET_snpExternalAutoHealing specific JET_SNTs (controlled by JET_paramEnableExternalAutoHealing)
//
#define JET_sntPagePatchRequest         1101    /* a log record request a page patch was encountered */
#define JET_sntCorruptedPage            1102    /* a corrupted page was encountered */
#endif
    /* Exception action / JET_paramExceptionAction */

#define JET_ExceptionMsgBox     0x0001      /* Display message box on exception */
#define JET_ExceptionNone       0x0002      /* Do nothing on exceptions */
#define JET_ExceptionFailFast   0x0004      /* Use the Windows RaiseFailFastException API to force a crash */
    /* AssertAction / JET_paramAssertAction  */

#define JET_AssertExit              0x0000      /* Exit the application */
#define JET_AssertBreak             0x0001      /* Break to debugger */
#define JET_AssertMsgBox            0x0002      /* Display message box */
#define JET_AssertStop              0x0004      /* Alert and stop */
#if ( JET_VERSION >= 0x0601 )
#define JET_AssertSkippableMsgBox   0x0008      /* Display skippable message box */
#define JET_AssertSkipAll           0x0010      /* Skip all asserts */
#define JET_AssertCrash             0x0020      /* AV (*0=0 style) */
#define JET_AssertFailFast          0x0040      /* Use the Windows RaiseFailFastException API to force a crash */
#endif // JET_VERSION >= 0x0601
#if ( JET_VERSION >= 0x0501 )
    //  Online defragmentation options
#define JET_OnlineDefragDisable         0x0000      //  disable online defrag
#define JET_OnlineDefragAllOBSOLETE     0x0001      //  enable online defrag for everything (must be 1 for backward compatibility)
#define JET_OnlineDefragDatabases       0x0002      //  enable online defrag of databases
#define JET_OnlineDefragSpaceTrees      0x0004      //  enable online defrag of space trees
#define JET_OnlineDefragStreamingFiles  0x0008      //  enable online defrag of streaming files
#define JET_OnlineDefragAll             0xffff      //  enable online defrag for everything

#endif // JET_VERSION >= 0x0501
    /* Counter flags */         // For XJET only, not for JET97

#define ctAccessPage            1
#define ctLatchConflict         2
#define ctSplitRetry            3
#define ctNeighborPageScanned   4
#define ctSplits                5

#if ( JET_VERSION >= 0x0600 )
    /* Resource manager tag size */

#define JET_resTagSize          4
#endif // JET_VERSION >= 0x0600
#if ( JET_VERSION >= 0x0602 )

// Info levels for JetGetErrorInfo:
#define JET_ErrorInfoSpecificErr        1U          //  Info about the specific error passed in pvContext.

// grbits for JetGetErrorInfoW:
// None yet.

// grbits for JetResizeDatabase:
#define JET_bitResizeDatabaseOnlyGrow               0x00000001  // Only grow the database. If the resize call would shrink the database, do nothing.
#endif // JET_VERSION >= 0x0602

#if ( JET_VERSION >= 0x0603 )
#define JET_bitResizeDatabaseOnlyShrink             0x00000002  // Only shrink the database. If the resize call would grow the database, do nothing. The file may end up smaller than requested.
// DEPRECATED: this was once used in the first implementation of DB shrink.
// #define JET_bitResizeDatabaseShrinkCompactSize       0x00000004  // DEPRECATED: Shrink the database to the smallest possible size without keeping an empty extent at the end.
#endif // JET_VERSION >= 0x0603

#if ( JET_VERSION >= 0x0602 )
#define JET_bitStopServiceAll                       0x00000000  //  Stops all ESE services for the specified instance.
// reserved:                                        0x00000001  //  (Internal StopServiceAll indicator)
#define JET_bitStopServiceBackgroundUserTasks       0x00000002  //  Stops restartable client specificed background maintenance tasks (B+ Tree Defrag for example).
#define JET_bitStopServiceQuiesceCaches             0x00000004  //  Quiesces all dirty caches to disk. Asynchronous. Cancellable.

// Warning: This bit can only be used to resume StopServiceBackgroundUserTasks and JET_bitStopServiceQuiesceCaches, if you
// previously called with JET_bitStopServiceAll, attempting to use JET_bitStopServiceResume will fail.
#define JET_bitStopServiceResume                    0x80000000  //  Resumes previously issued StopService operations, i.e. "restarts service".  Can be combined with above grbits to Resume specific services, or with JET_bitStopServiceAll to Resume all previously stopped services.
#endif // JET_VERSION >= 0x0602
#if ( JET_VERSION >= 0x0A01 )
#define JET_bitStopServiceStopAndEmitLog            0x00000008  //  Stop any further logging and emit last chunk of log.
#endif

// These bits are used with JET_paramStageFlighting for private staging, alpha, beta, and test in production staging.  Should not be used by
// any consumers without contacting the ESE dev team directly.
#define JET_bitStageTestEnvLocalMode                    0x04
#define JET_bitStageTestEnvAlphaMode                    0x10
#define JET_bitStageTestEnvBetaMode                     0x40

#define JET_bitStageSelfhostLocalMode                 0x0400
#define JET_bitStageSelfhostAlphaMode                 0x1000
#define JET_bitStageSelfhostBetaMode                  0x4000

#define JET_bitStageProdLocalMode                   0x040000
#define JET_bitStageProdAlphaMode                   0x100000
#define JET_bitStageProdBetaMode                    0x400000

// Encryption algorithms for JetCreateEncryptionKey
#if ( JET_VERSION >= 0x0A01 )
#define JET_EncryptionAlgorithmAes256       1
#endif
/**********************************************************************/
/***********************     ERROR CODES     **************************/
/**********************************************************************/

/* The Error codes are not versioned. */

/* SUCCESS */

#define JET_errSuccess                       0    /* Successful Operation */

/* ERRORS */

#define JET_wrnNyi                          -1    /* Function Not Yet Implemented */

/*  SYSTEM errors
#define JET_errRfsFailure                   -100  /* Resource Failure Simulator failure */
#define JET_errRfsNotArmed                  -101  /* Resource Failure Simulator not initialized */
#define JET_errFileClose                    -102  /* Could not close file */
#define JET_errOutOfThreads                 -103  /* Could not start thread */
#define JET_errTooManyIO                    -105  /* System busy due to too many IOs */
#define JET_errTaskDropped                  -106  /* A requested async task could not be executed */
#define JET_errInternalError                -107  /* Fatal internal error */
#define errCodeInconsistency                -108 /* Fatal internal error */
#define errNotFound                         -109  /* Generic not found error, internal only */
#define wrnSlow                              110  /* Generic inefficiency detected, internal only */
#define wrnLossy                             111  /* Generic lossy process taken, fidelity lost, internal only */
#define JET_errDisabledFunctionality        -112  /* You are running MinESE, that does not have all features compiled in.  This functionality is only supported in a full version of ESE. */
#define JET_errUnloadableOSFunctionality    -113  /* The desired OS functionality could not be located and loaded / linked. */
#define JET_errDeviceMissing                -114  /* A required hardware device or functionality was missing. */
#define JET_errDeviceMisconfigured          -115  /* A required hardware device was misconfigured externally. */
#define JET_errDeviceTimeout                -116  /* Timeout occurred while waiting for a hardware device to respond. */
#define errDeviceBusy                       -117  /* Hardware device is busy doing other operations. */
#define JET_errDeviceFailure                -118  /* A required hardware device didn't function as expected. */
//  BUFFER MANAGER errors
//
#define wrnBFCacheMiss                       200  /*  ese97,esent only:  page latch caused a cache miss  */
#define errBFPageNotCached                  -201  /*  page is not cached  */
#define errBFLatchConflict                  -202  /*  page latch conflict  */
#define errBFPageCached                     -203  /*  page is cached  */
#define wrnBFPageFlushPending                204  /*  page is currently being written  */
#define wrnBFPageFault                       205  /*  page latch caused a page fault  */
#define wrnBFBadLatchHint                    206  /*  page latch hint was incorrect  */
#define wrnBFLatchMaintConflict              207  /*  page latch conflict with foreground maintenance  */
#define wrnBFIWriteIOComplete                208  /*  signal a successful write IO from the async IO completion function  */

#define errBFIPageEvicted                   -250  /*  ese97,esent only:  page evicted from the cache  */
#define errBFIPageCached                    -251  /*  ese97,esent only:  page already cached  */
#define errBFIOutOfOLPs                     -252  /*  ese97,esent only:  out of OLPs  */
#define errBFIOutOfBatchIOBuffers           -253  /*  out of Batch I/O (Opportune write) Buffers  */
#define errBFINoBufferAvailable             -254  /*  no buffer available for immediate use  */
#define JET_errDatabaseBufferDependenciesCorrupted  -255    /* Buffer dependencies improperly set. Recovery failure */
#define errBFIRemainingDependencies         -256  /*  dependencies remain on this buffer  */
#define errBFIPageFlushPending              -257  /*  page is currently being written  */
#define errBFIPageDirty                     -258  /*  the page could not be evicted from the cache because it or its versions were not clean enough */
#define errBFIPageFlushed                   -259  /*  page write initiated  */
#define errBFIPageFaultPending              -260  /*  page is currently being read  */
#define errBFIPageNotVerified               -261  /*  page data has not been verified  */
#define errBFIDependentPurged               -262  /*  page cannot be flushed due to purged dependencies  */
#define errBFIPageFlushDisallowedOnIOThread -263  /*  the page couldn't be written because ErrBFIFlushPage is being called from the I/O thread  */
#define errBFIPageTouchTooRecent            -264  /*  the page could not be flushed because a recent page touch would offend the waypoint */
#define errBFICheckpointWorkRemaining       -266  /*  checkpoint depth maintenance is not finished due to page flushes or dependency flushes remaining */
#define errBFIPageRemapNotReVerified        -267  /*  page is remapped after a write, which means it needs to be reverified */
#define errBFIReqSyncFlushMapWriteFailed    -268  /*  UNUSED: required synchronous write to the flush map failed  */
#define errBFIPageFlushPendingHungIO        -269  /*  page is currently being written and the write I/O is hung */
#define errBFIPageFaultPendingHungIO        -270  /*  page is currently being read and the read I/O is hung */
#define errBFIPageFlushPendingSlowIO        -271  /*  page is currently being written and the write I/O is slow */
#define errBFIPageAbandoned                 -272  /*  page is currently in an abandoned state */
// #define errBFIPrereadPageBeyondEOF          -273  /*  DEPRECATED: we tried to preread a page beyond EOF */

//  VERSION STORE errors
//
#define wrnVERRCEMoved                       275  /*  RCE was moved instead of being cleaned */
/*  DIRECTORY MANAGER errors
#define errPMOutOfPageSpace                 -300  /* Out of page space */
#define errPMItagTooBig                     -301  /* Itag too big */                    //  XXX -- to be deleted
#define errPMRecDeleted                     -302  /* Record deleted */                  //  XXX -- to be deleted
#define errPMTagsUsedUp                     -303  /* Tags used up */                    //  XXX -- to be deleted
#define wrnBMConflict                        304  /* conflict in BM Clean up */
#define errDIRNoShortCircuit                -305  /* No Short Circuit Avail */
#define errDIRCannotSplit                   -306  /* Cannot horizontally split FDP */
#define errDIRTop                           -307  /* Cannot go up */
#define errDIRFDP                            308  /* On an FDP Node */
#define errDIRNotSynchronous                -309  /* May have left critical section */
#define wrnDIREmptyPage                      310  /* Moved through empty page */
#define errSPConflict                       -311  /* Device extent being extended */
#define wrnNDFoundLess                       312  /* Found Less */
//moved: errNDNotFound                      -312  /* new value: -349 */
#define wrnNDFoundGreater                    313  /* Found Greater */
#define wrnNDNotFoundInPage                  314  /* for smart refresh */
//moved: errNDOutSonRange                   -314  /* new value: -350 */
#define errNDOutItemRange                   -315  /* Item out of range */
#define errNDGreaterThanAllItems            -316  /* Greater than all items */
#define errNDLastItemNode                   -317  /* Last node of item list */
#define errNDFirstItemNode                  -318  /* First node of item list */
#define wrnNDDuplicateItem                   319  /* Duplicated Item */
#define errNDNoItem                         -320  /* Item not there */
#define JET_wrnRemainingVersions             321  /* The version store is still active */
#define JET_errPreviousVersion              -322  /* Version already existed. Recovery failure */
#define JET_errPageBoundary                 -323  /* Reached Page Boundary */
#define JET_errKeyBoundary                  -324  /* Reached Key Boundary */
#define errDIRInPageFather                  -325  /* sridFather in page to free */
#define errBMMaxKeyInPage                   -326  /* used by OLC to avoid cleanup of parent pages */
#define JET_errBadPageLink                  -327  /* Database corrupted */
#define JET_errBadBookmark                  -328  /* Bookmark has no corresponding address in database */
#define wrnBMCleanNullOp                     329  // BMClean returns this on encountering a page
                                                  // deleted MaxKeyInPage [but there was no conflict]
#define errBTOperNone                       -330  // Split with no accompanying
                                                  // insert/replace
#define errSPOutOfAvailExtCacheSpace        -331  // unable to make update to AvailExt tree since
                                                  // in-cursor space cache is depleted
#define errSPOutOfOwnExtCacheSpace          -332  // unable to make update to OwnExt tree since
                                                  // in-cursor space cache is depleted
#define wrnBTMultipageOLC                    333  // needs multipage OLC operation
#define JET_errNTSystemCallFailed           -334  // A call to the operating system failed
#define wrnBTShallowTree                     335  // BTree is only one or two levels deep
#define errBTMergeNotSynchronous            -336  // Multiple threads attempting to perform merge/split on same page (likely OLD vs. RCEClean)
#define wrnSPReservedPages                   337  // space manager reserved pages for future space tree splits
#define JET_errBadParentPageLink            -338  // Database corrupted
#define wrnSPBuildAvailExtCache              339  // AvailExt tree is sufficiently large that it should be cached
#define JET_errSPAvailExtCacheOutOfSync     -340  // AvailExt cache doesn't match btree
#define JET_errSPAvailExtCorrupted          -341  // AvailExt space tree is corrupt
#define JET_errSPAvailExtCacheOutOfMemory   -342  // Out of memory allocating an AvailExt cache node
#define JET_errSPOwnExtCorrupted            -343  // OwnExt space tree is corrupt
#define JET_errDbTimeCorrupted              -344  // Dbtime on current page is greater than global database dbtime
#define JET_wrnUniqueKey                     345  // seek on non-unique index yielded a unique key
#define JET_errKeyTruncated                 -346  // key truncated on index that disallows key truncation
#define errSPNoSpaceForYou                  -347  // There was no space of this type or in this avail tree for the caller.
#define JET_errDatabaseLeakInSpace          -348  // Some database pages have become unreachable even from the avail tree, only an offline defragmentation can return the lost space.
#define errNDNotFound                       -349  /* Not found */
#define errNDOutSonRange                    -350  /* Son out of range */
#define JET_errBadEmptyPage                 -351  // Database corrupted. Searching an unexpectedly empty page.
#define wrnBTNotVisibleRejected              352  /* Current entry is not visible because it has been rejected by a move filter */
#define wrnBTNotVisibleAccumulated           353  /* Current entry is not visible because it is being accumulated by a move filter */
#define JET_errBadLineCount                 -354  /* Number of lines on the page is too few compared to the line being operated on */
#define errSPNoSpaceBelowShrinkTarget       -355  // There was no space available which falls below the current page number we are trying to shrink to.
#define wrnSPRequestSpBufRefill              356  // A split buffer page may have been consumed in the process of refilling split buffers and the space manager should request new split buffer refill.
#define JET_errPageTagCorrupted             -357  // A tag / line on page is logically corrupted, offset or size is bad, or tag count on page is bad.
#define JET_errNodeCorrupted                -358  // A node or prefix node is logically corrupted, the key suffix size is larger than the node or line's size.

/*  RECORD MANAGER errors
#define wrnFLDKeyTooBig                      400  /* Key too big (truncated it) */
#define errFLDTooManySegments               -401  /* Too many key segments */
#define wrnFLDNullKey                        402  /* Key is entirely NULL */
#define wrnFLDOutOfKeys                      403  /* No more keys to extract */
#define wrnFLDNullSeg                        404  /* Null segment in key */
#define wrnFLDNotPresentInIndex              405
#define JET_wrnSeparateLongValue             406  /* Column is a separated long-value */
#define wrnRECLongField                      407  /* Long value */
#define JET_wrnRecordFoundGreater           JET_wrnSeekNotEqual
#define JET_wrnRecordFoundLess              JET_wrnSeekNotEqual
#define JET_errColumnIllegalNull            JET_errNullInvalid
//moved: wrnFLDNullFirstSeg                  408  /* new value: 422 */
#define JET_errKeyTooBig                    -408  /* Key is too large */
#define wrnRECUserDefinedDefault             409  /* User-defined default value */
#define wrnRECSeparatedLV                    410  /* LV stored in LV tree */
#define wrnRECIntrinsicLV                    411  /* LV stored in the record */
#define wrnFLDIndexUpdated                   414    // index update performed
#define wrnFLDOutOfTuples                    415    // no more tuples for current string
#define JET_errCannotSeparateIntrinsicLV    -416    // illegal attempt to separate an LV which must be intrinsic
#define wrnRECCompressed                     417  /* LV stored in the record in compressed form */
#define errRECCannotCompress                -418  /* column cannot be compressed */
#define errRECCompressionNotPossible        -419  /* can't store column in compressed form */
#define wrnRECCompressionScrubDetected       420  /* Returned when the record has been scrubbed. It is invalid to try and retrieve any data from this record. This warning is used for internal signaling only. */
#define JET_errSeparatedLongValue           -421 /* Operation not supported on separated long-value */
#define wrnFLDNullFirstSeg                   422  /* Null first segment in key */
#define JET_errMustBeSeparateLongValue      -423  /* Can only preread long value columns that can be separate, e.g. not size constrained so that they are fixed or variable columns */
#define JET_errInvalidPreread               -424  /* Cannot preread long values when current index secondary */
#define wrnRECSeparatedEncryptedLV           425  /* LV stored encrypted in LV tree */
#define JET_errInvalidColumnReference       -426  /* Column reference is invalid */
#define JET_errStaleColumnReference         -427  /* Column reference is stale */
#define JET_wrnNoMoreRecords                 428  /* No more records to stream */
#define errRECColumnNotFound                -429  /* Column value not found in record */
#define errRECNoCurrentColumnValue          -430  /* No current column value in record */
#define JET_errCompressionIntegrityCheckFailed  -431  /* A compression integrity check failed. Decompressing data failed the integrity checksum indicating a data corruption in the compress/decompress pipeline. */
#define JET_wrnIndexDeferredPopulateIncomplete   432  /* Populating a deferred populate index did not complete. */
#define JET_wrnIndexDeferredPopulateHalted   433  /* Populating a deferred populate index was unexpectedly halted. */
#define JET_errIndexDeferredPopulateCurrentlyUnavailable -434 /* Populating a deferred populate index is not allowed at this time. */
/*  LOGGING/RECOVERY errors
#define JET_errInvalidLoggedOperation       -500  /* Logged operation cannot be redone */
#define JET_errLogFileCorrupt               -501  /* Log file is corrupt */
#define errLGNoMoreRecords                  -502  /* Last log record read */
#define JET_errNoBackupDirectory            -503  /* No backup directory given */
#define JET_errBackupDirectoryNotEmpty      -504  /* The backup directory is not empty */
#define JET_errBackupInProgress             -505  /* Backup is active already */
#define JET_errRestoreInProgress            -506  /* Restore in progress */
#define JET_errMissingPreviousLogFile       -509  /* Missing the log file for check point */
#define JET_errLogWriteFail                 -510  /* Failure writing to log file */
#define JET_errLogDisabledDueToRecoveryFailure  -511 /* Try to log something after recovery failed */
#define JET_errCannotLogDuringRecoveryRedo      -512    /* Try to log something during recovery redo */
#define JET_errLogGenerationMismatch        -513  /* Name of logfile does not match internal generation number */
#define JET_errBadLogVersion                -514  /* Version of log file is not compatible with Jet version */
#define JET_errInvalidLogSequence           -515  /* Timestamp in next log does not match expected */
#define JET_errLoggingDisabled              -516  /* Log is not active */
#define JET_errLogBufferTooSmall            -517  /* An operation generated a log record which was too large to fit in the log buffer or in a single log file */
#define errLGNotSynchronous                 -518  /* retry to LGLogRec */
#define JET_errLogSequenceEnd               -519  /* Maximum log file number exceeded */
#define JET_errNoBackup                     -520  /* No backup in progress */
#define JET_errInvalidBackupSequence        -521  /* Backup call out of sequence */
#define JET_errBackupNotAllowedYet          -523  /* Cannot do backup now */
#define JET_errDeleteBackupFileFail         -524  /* Could not delete backup file */
#define JET_errMakeBackupDirectoryFail      -525  /* Could not make backup temp directory */
#define JET_errInvalidBackup                -526  /* Cannot perform incremental backup when circular logging enabled */
#define JET_errRecoveredWithErrors          -527  /* Restored with errors */
#define JET_errMissingLogFile               -528  /* Current log file missing */
#define JET_errLogDiskFull                  -529  /* Log disk full */
#define JET_errBadLogSignature              -530  /* Bad signature for a log file */
#define JET_errBadDbSignature               -531  /* Bad signature for a db file */
#define JET_errBadCheckpointSignature       -532  /* Bad signature for a checkpoint file */
#define JET_errCheckpointCorrupt            -533  /* Checkpoint file not found or corrupt */
#define JET_errMissingPatchPage             -534  /* Patch file page not found during recovery */
#define JET_errBadPatchPage                 -535  /* Patch file page is not valid */
#define JET_errRedoAbruptEnded              -536  /* Redo abruptly ended due to sudden failure in reading logs from log file */
#define JET_errPatchFileMissing             -538  /* Hard restore detected that patch file is missing from backup set */
#define JET_errDatabaseLogSetMismatch       -539  /* Database does not belong with the current set of log files */
#define JET_errDatabaseStreamingFileMismatch    -540 /* Database and streaming file do not match each other */
#define JET_errLogFileSizeMismatch          -541  /* actual log file size does not match JET_paramLogFileSize */
#define JET_errCheckpointFileNotFound       -542  /* Could not locate checkpoint file */
#define JET_errRequiredLogFilesMissing      -543  /* The required log files for recovery is missing. */
#define JET_errSoftRecoveryOnBackupDatabase -544  /* Soft recovery is intended on a backup database. Restore should be used instead */
#define JET_errLogFileSizeMismatchDatabasesConsistent   -545  /* databases have been recovered, but the log file size used during recovery does not match JET_paramLogFileSize */
#define JET_errLogSectorSizeMismatch        -546  /* the log file sector size does not match the current volume's sector size */
#define JET_errLogSectorSizeMismatchDatabasesConsistent -547  /* databases have been recovered, but the log file sector size (used during recovery) does not match the current volume's sector size */
#define JET_errLogSequenceEndDatabasesConsistent        -548 /* databases have been recovered, but all possible log generations in the current sequence are used; delete all log files and the checkpoint file and backup the databases before continuing */

#define JET_errStreamingDataNotLogged       -549  /* Illegal attempt to replay a streaming file operation where the data wasn't logged. Probably caused by an attempt to roll-forward with circular logging enabled */

#define JET_errDatabaseDirtyShutdown        -550  /* Database was not shutdown cleanly. Recovery must first be run to properly complete database operations for the previous shutdown. */
#define JET_errDatabaseInconsistent         JET_errDatabaseDirtyShutdown    /* OBSOLETE */
#define JET_errConsistentTimeMismatch       -551  /* Database last consistent time unmatched */
#define JET_errDatabasePatchFileMismatch    -552  /* Patch file is not generated from this backup */
#define JET_errEndingRestoreLogTooLow       -553  /* The starting log number too low for the restore */
#define JET_errStartingRestoreLogTooHigh    -554  /* The starting log number too high for the restore */
#define JET_errGivenLogFileHasBadSignature  -555  /* Restore log file has bad signature */
#define JET_errGivenLogFileIsNotContiguous  -556  /* Restore log file is not contiguous */
#define JET_errMissingRestoreLogFiles       -557  /* Some restore log files are missing */
#define JET_wrnExistingLogFileHasBadSignature   558  /* Existing log file has bad signature */
#define JET_wrnExistingLogFileIsNotContiguous   559  /* Existing log file is not contiguous */
#define JET_errMissingFullBackup            -560  /* The database missed a previous full backup before incremental backup */
#define JET_errBadBackupDatabaseSize        -561  /* The backup database size is not in 4k */
#define JET_errDatabaseAlreadyUpgraded      -562  /* Attempted to upgrade a database that is already current */
#define JET_errDatabaseIncompleteUpgrade    -563  /* Attempted to use a database which was only partially converted to the current format -- must restore from backup */
#define JET_wrnSkipThisRecord                564  /* INTERNAL ERROR */
#define JET_errMissingCurrentLogFiles       -565  /* Some current log files are missing for continuous restore */

#define JET_errDbTimeTooOld                     -566  /* dbtime on page smaller than dbtimeBefore in record */
#define JET_errDbTimeTooNew                     -567  /* dbtime on page in advance of the dbtimeBefore and below dbtimeAfter in record */
//#define wrnCleanedUpMismatchedFiles                568  /* INTERNAL WARNING: indicates that the redo function cleaned up logs/checkpoint because of a size mismatch (see JET_paramCleanupMismatchedLogFiles) */
#define JET_errMissingFileToBackup              -569  /* Some log or patch files are missing during backup */

#define JET_errLogTornWriteDuringHardRestore    -570    /* torn-write was detected in a backup set during hard restore */
#define JET_errLogTornWriteDuringHardRecovery   -571    /* torn-write was detected during hard recovery (log was not part of a backup set) */
#define JET_errLogCorruptDuringHardRestore      -573    /* corruption was detected in a backup set during hard restore */
#define JET_errLogCorruptDuringHardRecovery     -574    /* corruption was detected during hard recovery (log was not part of a backup set) */

#define JET_errMustDisableLoggingForDbUpgrade   -575    /* Cannot have logging enabled while attempting to upgrade db */
#define errLGRecordDataInaccessible             -576    /* an incomplete log record was created because all the data to be logged was not accessible */
#define JET_errBadRestoreTargetInstance         -577    /* TargetInstance specified for restore is not found or log files don't match */
#define JET_wrnTargetInstanceRunning             578    /* TargetInstance specified for restore is running */

#define JET_errRecoveredWithoutUndo             -579    /* Soft recovery successfully replayed all operations, but the Undo phase of recovery was skipped */

#define JET_errDatabasesNotFromSameSnapshot     -580    /* Databases to be restored are not from the same shadow copy backup */
#define JET_errSoftRecoveryOnSnapshot           -581    /* Soft recovery on a database from a shadow copy backup set */
#define JET_errCommittedLogFilesMissing         -582    /* One or more logs that were committed to this database, are missing.  These log files are required to maintain durable ACID semantics, but not required to maintain consistency if the JET_bitReplayIgnoreLostLogs bit is specified during recovery. */
#define JET_errSectorSizeNotSupported           -583    /* The physical sector size reported by the disk subsystem, is unsupported by ESE for a specific file type. */
#define JET_errRecoveredWithoutUndoDatabasesConsistent  -584    /* Soft recovery successfully replayed all operations and intended to skip the Undo phase of recovery, but the Undo phase was not required */
#define JET_wrnCommittedLogFilesLost            585     /* One or more logs that were committed to this database, were not recovered.  The database is still clean/consistent, as though the lost log's transactions were committed lazily (and lost). */
#define JET_errCommittedLogFileCorrupt          -586    /* One or more logs were found to be corrupt during recovery.  These log files are required to maintain durable ACID semantics, but not required to maintain consistency if the JET_bitIgnoreLostLogs bit and JET_paramDeleteOutOfRangeLogs is specified during recovery. */
#define JET_wrnCommittedLogFilesRemoved         587     /* One or more logs that were committed to this database, were no recovered.  The database is still clean/consistent, as though the corrupted log's transactions were committed lazily (and lost). */
#define JET_wrnFinishWithUndo                   588     /* Signal used by clients to indicate JetInit() finished with undo */
#define errSkipLogRedoOperation                 -589    /* The log redo operation should be skipped */
#define JET_errLogSequenceChecksumMismatch      -590    /* The previous log's accumulated segment checksum doesn't match the next log */

#define JET_wrnDatabaseRepaired                  595    /* Database corruption has been repaired */
#define JET_errPageInitializedMismatch          -596    /* Database divergence mismatch. Page was uninitialized on remote node, but initialized on local node. */

#define JET_errUnicodeTranslationBufferTooSmall -601    /* Unicode translation buffer too small */
#define JET_errUnicodeTranslationFail           -602    /* Unicode normalization failed */
#define JET_errUnicodeNormalizationNotSupported -603    /* OS does not provide support for Unicode normalisation (and no normalisation callback was specified) */
#define JET_errUnicodeLanguageValidationFailure -604    /* Can not validate the language */

#define JET_errExistingLogFileHasBadSignature   -610    /* Existing log file has bad signature */
#define JET_errExistingLogFileIsNotContiguous   -611    /* Existing log file is not contiguous */

#define JET_errLogReadVerifyFailure         -612  /* Checksum error in log file during backup */

#define JET_errCheckpointDepthTooDeep       -614    //  too many outstanding generations between checkpoint and current generation

#define JET_errRestoreOfNonBackupDatabase   -615    //  hard recovery attempted on a database that wasn't a backup database
#define JET_errLogFileNotCopied             -616    //  log truncation attempted but not all required logs were copied
#define JET_errSurrogateBackupInProgress    -617    //  A surrogate backup is in progress.
#define JET_errTransactionTooLong           -618    //  Too many outstanding generations between JetBeginTransaction and current generation.

#define JET_errEngineFormatVersionNoLongerSupportedTooLow           -619 /* The specified JET_ENGINEFORMATVERSION value is too low to be supported by this version of ESE. */
#define JET_errEngineFormatVersionNotYetImplementedTooHigh          -620 /* The specified JET_ENGINEFORMATVERSION value is too high, higher than this version of ESE knows about. */
#define JET_errEngineFormatVersionParamTooLowForRequestedFeature    -621 /* Thrown by a format feature (not at JetSetSystemParameter) if the client requests a feature that requires a version higher than that set for the JET_paramEngineFormatVersion. */
#define JET_errEngineFormatVersionSpecifiedTooLowForLogVersion                      -622 /* The specified JET_ENGINEFORMATVERSION is set too low for this log stream, the log files have already been upgraded to a higher version.  A higher JET_ENGINEFORMATVERSION value must be set in the param. */
#define JET_errEngineFormatVersionSpecifiedTooLowForDatabaseVersion                 -623 /* The specified JET_ENGINEFORMATVERSION is set too low for this database file, the database file has already been upgraded to a higher version.  A higher JET_ENGINEFORMATVERSION value must be set in the param. */
#define errLogServiceStopped                -624  /* Logging has been stopped via JetStopServiceInstance2 JET_bitStopServiceStopAndEmitLog */
#define JET_errDbTimeBeyondMaxRequired      -625  /* dbtime on page greater than or equal to dbtimeAfter in record, but record is outside required range for the database */
#define JET_errLogOperationInconsistentWithDatabase -626 /* Log record in the log is inconsistent with the current state of the database and cannot be applied */
#define errBackupAbortByCaller              -800  /* INTERNAL ERROR: Backup was aborted by client or RPC connection with client failed */
#define JET_errBackupAbortByServer          -801  /* Backup was aborted by server by calling JetTerm with JET_bitTermStopBackup or by calling JetStopBackup */
#define errTooManyPatchRequests             -802  /* Too many patch requests in the patch request list */
#define JET_errInvalidGrbit                 -900  /* Invalid flags parameter */

#define JET_errTermInProgress               -1000 /* Termination in progress */
#define JET_errFeatureNotAvailable          -1001 /* API not supported */
#define JET_errInvalidName                  -1002 /* Invalid name */
#define JET_errInvalidParameter             -1003 /* Invalid API parameter */
#define JET_wrnColumnNull                    1004 /* Column is NULL-valued */
#define JET_wrnBufferTruncated               1006 /* Buffer too small for data */
#define JET_wrnDatabaseAttached              1007 /* Database is already attached */
#define JET_errDatabaseFileReadOnly         -1008 /* Tried to attach a read-only database file for read/write operations */
#define JET_wrnSortOverflow                  1009 /* Sort does not fit in memory */
#define JET_errInvalidDatabaseId            -1010 /* Invalid database id */
#define JET_errOutOfMemory                  -1011 /* Out of Memory */
#define JET_errOutOfDatabaseSpace           -1012 /* Maximum database size reached */
#define JET_errOutOfCursors                 -1013 /* Out of table cursors */
#define JET_errOutOfBuffers                 -1014 /* Out of database page buffers */
#define JET_errTooManyIndexes               -1015 /* Too many indexes */
#define JET_errTooManyKeys                  -1016 /* Too many columns in an index */
#define JET_errRecordDeleted                -1017 /* Record has been deleted */
#define JET_errReadVerifyFailure            -1018 /* Checksum error on a database page */
#define JET_errPageNotInitialized           -1019 /* Blank database page */
#define JET_errOutOfFileHandles             -1020 /* Out of file handles */
#define JET_errDiskReadVerificationFailure  -1021 /* The OS returned ERROR_CRC from file IO */
#define JET_errDiskIO                       -1022 /* Disk IO error */
#define JET_errInvalidPath                  -1023 /* Invalid file path */
#define JET_errInvalidSystemPath            -1024 /* Invalid system path */
#define JET_errInvalidLogDirectory          -1025 /* Invalid log directory */
#define JET_errRecordTooBig                 -1026 /* Record larger than maximum size */
#define JET_errTooManyOpenDatabases         -1027 /* Too many open databases */
#define JET_errInvalidDatabase              -1028 /* Not a database file */
#define JET_errNotInitialized               -1029 /* Database engine not initialized */
#define JET_errAlreadyInitialized           -1030 /* Database engine already initialized */
#define JET_errInitInProgress               -1031 /* Database engine is being initialized */
#define JET_errFileAccessDenied             -1032 /* Cannot access file, the file is locked or in use */
#define JET_errQueryNotSupported            -1034 /* Query support unavailable */               //  XXX -- to be deleted
#define JET_errSQLLinkNotSupported          -1035 /* SQL Link support unavailable */            //  XXX -- to be deleted
#define JET_errBufferTooSmall               -1038 /* Buffer is too small */
#define JET_wrnSeekNotEqual                  1039 /* Exact match not found during seek */
#define JET_errTooManyColumns               -1040 /* Too many columns defined */
#define JET_errContainerNotEmpty            -1043 /* Container is not empty */
#define JET_errInvalidFilename              -1044 /* Filename is invalid */
#define JET_errInvalidBookmark              -1045 /* Invalid bookmark */
#define JET_errColumnInUse                  -1046 /* Column used in an index */
#define JET_errInvalidBufferSize            -1047 /* Data buffer doesn't match column size */
#define JET_errColumnNotUpdatable           -1048 /* Cannot set column value */
#define JET_errIndexInUse                   -1051 /* Index is in use */
#define JET_errLinkNotSupported             -1052 /* Link support unavailable */
#define JET_errNullKeyDisallowed            -1053 /* Null keys are disallowed on index */
#define JET_errNotInTransaction             -1054 /* Operation must be within a transaction */
#define JET_wrnNoErrorInfo                   1055 /* No extended error information */
#define JET_errMustRollback                 -1057 /* Transaction must rollback because failure of unversioned update */
#define JET_wrnNoIdleActivity                1058 /* No idle activity occurred */
#define JET_errTooManyActiveUsers           -1059 /* Too many active database users */
#define JET_errInvalidCountry               -1061 /* Invalid or unknown country/region code */
#define JET_errInvalidLanguageId            -1062 /* Invalid or unknown language id */
#define JET_errInvalidCodePage              -1063 /* Invalid or unknown code page */
#define JET_errInvalidLCMapStringFlags      -1064 /* Invalid flags for LCMapString() */
#define JET_errVersionStoreEntryTooBig      -1065 /* Attempted to create a version store entry (RCE) larger than a version bucket */
#define JET_errVersionStoreOutOfMemoryAndCleanupTimedOut    -1066 /* Version store out of memory (and cleanup attempt failed to complete) */
#define JET_wrnNoWriteLock                   1067 /* No write lock at transaction level 0 */
#define JET_wrnColumnSetNull                 1068 /* Column set to NULL-value */
#define JET_errVersionStoreOutOfMemory      -1069 /* Version store out of memory (cleanup already attempted) */
#define JET_errCurrencyStackOutOfMemory     -1070 /* UNUSED: lCSRPerfFUCB * g_lCursorsMax exceeded (XJET only) */
#define JET_errCannotIndex                  -1071 /* Cannot index escrow column */
#define JET_errRecordNotDeleted             -1072 /* Record has not been deleted */
#define JET_errTooManyMempoolEntries        -1073 /* Too many mempool entries requested */
#define JET_errOutOfObjectIDs               -1074 /* Out of btree ObjectIDs (perform offline defrag to reclaim freed/unused ObjectIds) */
#define JET_errOutOfLongValueIDs            -1075 /* Long-value ID counter has reached maximum value. (perform offline defrag to reclaim free/unused LongValueIDs) */
#define JET_errOutOfAutoincrementValues     -1076 /* Auto-increment counter has reached maximum value (offline defrag WILL NOT be able to reclaim free/unused Auto-increment values). */
#define JET_errOutOfDbtimeValues            -1077 /* Dbtime counter has reached maximum value (perform offline defrag to reclaim free/unused Dbtime values) */
#define JET_errOutOfSequentialIndexValues   -1078 /* Sequential index counter has reached maximum value (perform offline defrag to reclaim free/unused SequentialIndex values) */

#define JET_errRunningInOneInstanceMode     -1080 /* Multi-instance call with single-instance mode enabled */
#define JET_errRunningInMultiInstanceMode   -1081 /* Single-instance call with multi-instance mode enabled */
#define JET_errSystemParamsAlreadySet       -1082 /* Global system parameters have already been set */

#define JET_errSystemPathInUse              -1083 /* System path already used by another database instance */
#define JET_errLogFilePathInUse             -1084 /* Logfile path already used by another database instance */
#define JET_errTempPathInUse                -1085 /* Temp path already used by another database instance */
#define JET_errInstanceNameInUse            -1086 /* Instance Name already in use */
#define JET_errSystemParameterConflict      -1087 /* Global system parameters have already been set, but to a conflicting or disagreeable state to the specified values. */

#define JET_errInstanceUnavailable          -1090 /* This instance cannot be used because it encountered a fatal error */
#define JET_errDatabaseUnavailable          -1091 /* This database cannot be used because it encountered a fatal error */
#define JET_errInstanceUnavailableDueToFatalLogDiskFull -1092 /* This instance cannot be used because it encountered a log-disk-full error performing an operation (likely transaction rollback) that could not tolerate failure */
#define JET_errInvalidSesparamId            -1093 /* This JET_sesparam* identifier is not known to the ESE engine. */

#define JET_errTooManyRecords               -1094 /* There are too many records to enumerate, switch to an API that handles 64-bit numbers */

#define JET_errInvalidDbparamId             -1095 /* This JET_dbparam* identifier is not known to the ESE engine. */

#define JET_errOutOfSessions                -1101 /* Out of sessions */
#define JET_errWriteConflict                -1102 /* Write lock failed due to outstanding write lock */
#define JET_errTransTooDeep                 -1103 /* Transactions nested too deeply */
#define JET_errInvalidSesid                 -1104 /* Invalid session handle */
#define JET_errWriteConflictPrimaryIndex    -1105 /* Update attempted on uncommitted primary index */
#define JET_errInTransaction                -1108 /* Operation not allowed within a transaction */
#define JET_errRollbackRequired             -1109 /* Must rollback current transaction -- cannot commit or begin a new one */
#define JET_errTransReadOnly                -1110 /* Read-only transaction tried to modify the database */
#define JET_errSessionWriteConflict         -1111 /* Attempt to replace the same record by two different cursors in the same session */

#define JET_errRecordTooBigForBackwardCompatibility             -1112 /* record would be too big if represented in a database format from a previous version of Jet */
#define JET_errCannotMaterializeForwardOnlySort                 -1113 /* The temp table could not be created due to parameters that conflict with JET_bitTTForwardOnly */

#define JET_errSesidTableIdMismatch         -1114 /* This session handle can't be used with this table id */
#define JET_errInvalidInstance              -1115 /* Invalid instance handle */
#define JET_errDirtyShutdown                -1116 /* The instance was shutdown successfully but all the attached databases were left in a dirty state by request via JET_bitTermDirty */
// unused -1117
#define JET_errReadPgnoVerifyFailure        -1118 /* The database page read from disk had the wrong page number. */
#define JET_errReadLostFlushVerifyFailure   -1119 /* The database page read from disk had a previous write not represented on the page. */
#define errCantRetrieveDebuggeeMemory           -1120 /* Can not retrieve the requested memory from the debuggee */
#define JET_errFileSystemCorruption             -1121 /* File system operation failed with an error indicating the file system is corrupt. */
#define JET_wrnShrinkNotPossible                1122 /* Database file could not be shrunk because there is not enough internal free space available or there is unmovable data present. */
#define JET_errRecoveryVerifyFailure            -1123 /* One or more database pages read from disk during recovery do not match the expected state. */

#define JET_errFilteredMoveNotSupported         -1124 /* Attempted to provide a filter to JetSetCursorFilter() in an unsupported scenario. */
#define JET_errMustCommitDistributedTransactionToLevel0         -1150 /* Attempted to PrepareToCommit a distributed transaction to non-zero level */
#define JET_errDistributedTransactionAlreadyPreparedToCommit    -1151 /* Attempted a write-operation after a distributed transaction has called PrepareToCommit */
#define JET_errNotInDistributedTransaction                      -1152 /* Attempted to PrepareToCommit a non-distributed transaction */
#define JET_errDistributedTransactionNotYetPreparedToCommit     -1153 /* Attempted to commit a distributed transaction, but PrepareToCommit has not yet been called */
#define JET_errCannotNestDistributedTransactions                -1154 /* Attempted to begin a distributed transaction when not at level 0 */
#define JET_errDTCMissingCallback                               -1160 /* Attempted to begin a distributed transaction but no callback for DTC coordination was specified on initialisation */
#define JET_errDTCMissingCallbackOnRecovery                     -1161 /* Attempted to recover a distributed transaction but no callback for DTC coordination was specified on initialisation */
#define JET_errDTCCallbackUnexpectedError                       -1162 /* Unexpected error code returned from DTC callback */
#define JET_wrnDTCCommitTransaction                              1163 /* Warning code DTC callback should return if the specified transaction is to be committed */
#define JET_wrnDTCRollbackTransaction                            1164 /* Warning code DTC callback should return if the specified transaction is to be rolled back */
#define JET_errDatabaseDuplicate            -1201 /* Database already exists */
#define JET_errDatabaseInUse                -1202 /* Database in use */
#define JET_errDatabaseNotFound             -1203 /* No such database */
#define JET_errDatabaseInvalidName          -1204 /* Invalid database name */
#define JET_errDatabaseInvalidPages         -1205 /* Invalid number of pages */
#define JET_errDatabaseCorrupted            -1206 /* Non database file or corrupted db */
#define JET_errDatabaseLocked               -1207 /* Database exclusively locked */
#define JET_errCannotDisableVersioning      -1208 /* Cannot disable versioning for this database */
#define JET_errInvalidDatabaseVersion       -1209 /* Database engine is incompatible with database */

/*  The following error code are for NT clients only. It will return such error during
 *  JetInit if JET_paramCheckFormatWhenOpenFail is set.
 */
#define JET_errDatabase200Format            -1210 /* The database is in an older (200) format */
#define JET_errDatabase400Format            -1211 /* The database is in an older (400) format */
#define JET_errDatabase500Format            -1212 /* The database is in an older (500) format */

#define JET_errPageSizeMismatch             -1213 /* The database page size does not match the engine */
#define JET_errTooManyInstances             -1214 /* Cannot start any more database instances */
#define JET_errDatabaseSharingViolation     -1215 /* A different database instance is using this database */
#define JET_errAttachedDatabaseMismatch     -1216 /* An outstanding database attachment has been detected at the start or end of recovery, but database is missing or does not match attachment info */
#define JET_errDatabaseInvalidPath          -1217 /* Specified path to database file is illegal */
#define JET_errDatabaseIdInUse              -1218 /* A database is being assigned an id already in use */
#define JET_errForceDetachNotAllowed        -1219 /* Force Detach allowed only after normal detach errored out */
#define JET_errCatalogCorrupted             -1220 /* Corruption detected in catalog */
#define JET_errPartiallyAttachedDB          -1221 /* Database is partially attached. Cannot complete attach operation */
#define JET_errDatabaseSignInUse            -1222 /* Database with same signature in use */
#define errSkippedDbHeaderUpdate            -1223 /* some db header weren't update becase there were during detach */
#define JET_errDatabaseCorruptedNoRepair    -1224 /* Corrupted db but repair not allowed */
#define JET_errInvalidCreateDbVersion       -1225 /* recovery tried to replay a database creation, but the database was originally created with an incompatible (likely older) version of the database engine */
#define JET_errDatabaseIncompleteIncrementalReseed  -1226 /* The database cannot be attached because it is currently being rebuilt as part of an incremental reseed. */
#define JET_errDatabaseInvalidIncrementalReseed     -1227 /* The database is not a valid state to perform an incremental reseed. */
#define JET_errDatabaseFailedIncrementalReseed      -1228 /* The incremental reseed being performed on the specified database cannot be completed due to a fatal error.  A full reseed is required to recover this database. */
#define JET_errNoAttachmentsFailedIncrementalReseed -1229 /* The incremental reseed being performed on the specified database cannot be completed because the min required log contains no attachment info.  A full reseed is required to recover this database. */
#define JET_errDatabaseNotReady             -1230 /* Recovery on this database has not yet completed enough to permit access. */
#define JET_errDatabaseAttachedForRecovery  -1231 /* Database is attached but only for recovery.  It must be explicitly attached before it can be opened.  */
#define JET_errTransactionsNotReadyDuringRecovery -1232  /* Recovery has not seen any Begin0/Commit0 records and so does not know what trxBegin0 to assign to this transaction */

#define JET_wrnTableEmpty                    1301 /* Opened an empty table */
#define JET_errTableLocked                  -1302 /* Table is exclusively locked */
#define JET_errTableDuplicate               -1303 /* Table already exists */
#define JET_errTableInUse                   -1304 /* Table is in use, cannot lock */
#define JET_errObjectNotFound               -1305 /* No such table or object */
#define JET_errDensityInvalid               -1307 /* Bad file/index density */
#define JET_errTableNotEmpty                -1308 /* Table is not empty */
#define JET_errInvalidTableId               -1310 /* Invalid table id */
#define JET_errTooManyOpenTables            -1311 /* Cannot open any more tables (cleanup already attempted) */
#define JET_errIllegalOperation             -1312 /* Oper. not supported on table */
#define JET_errTooManyOpenTablesAndCleanupTimedOut  -1313 /* Cannot open any more tables (cleanup attempt failed to complete) */
#define JET_errObjectDuplicate              -1314 /* Table or object name in use */
#define JET_errInvalidObject                -1316 /* Object is invalid for operation */
#define JET_errCannotDeleteTempTable        -1317 /* Use CloseTable instead of DeleteTable to delete temp table */
#define JET_errCannotDeleteSystemTable      -1318 /* Illegal attempt to delete a system table */
#define JET_errCannotDeleteTemplateTable    -1319 /* Illegal attempt to delete a template table */
#define errFCBTooManyOpen                   -1320 /* Cannot open any more FCB's (cleanup not yet attempted) */
#define errFCBAboveThreshold                -1321 /* Can only allocate FCB above preferred threshold (cleanup not yet attempted) */
#define JET_errExclusiveTableLockRequired   -1322 /* Must have exclusive lock on table. */
#define JET_errFixedDDL                     -1323 /* DDL operations prohibited on this table */
#define JET_errFixedInheritedDDL            -1324 /* On a derived table, DDL operations are prohibited on inherited portion of DDL */
#define JET_errCannotNestDDL                -1325 /* Nesting of hierarchical DDL is not currently supported. */
#define JET_errDDLNotInheritable            -1326 /* Tried to inherit DDL from a table not marked as a template table. */
#define JET_wrnTableInUseBySystem            1327 /* System cleanup has a cursor open on the table */
#define JET_errInvalidSettings              -1328 /* System parameters were set improperly */
#define JET_errClientRequestToStopJetService            -1329   /* Client has requested stop service */
#define JET_errCannotAddFixedVarColumnToDerivedTable    -1330   /* Template table was created with NoFixedVarColumnsInDerivedTables */
#define errFCBExists                        -1331 /* Tried to create an FCB that already exists */
#define errFCBUnusable                      -1332 /* Placeholder to mark an FCB that must be purged as unusable */
#define wrnCATNoMoreRecords                  1333 /* Attempted to navigate past the end of the catalog */
/*  DDL errors
// Note: Some DDL errors have snuck into other categories.
#define JET_errIndexCantBuild               -1401 /* Index build failed */
#define JET_errIndexHasPrimary              -1402 /* Primary index already defined */
#define JET_errIndexDuplicate               -1403 /* Index is already defined */
#define JET_errIndexNotFound                -1404 /* No such index */
#define JET_errIndexMustStay                -1405 /* Cannot delete clustered index */
#define JET_errIndexInvalidDef              -1406 /* Illegal index definition */
#define JET_errInvalidCreateIndex           -1409 /* Invalid create index description */
#define JET_errTooManyOpenIndexes           -1410 /* Out of index description blocks */
#define JET_errMultiValuedIndexViolation    -1411 /* Non-unique inter-record index keys generated for a multivalued index */
#define JET_errIndexBuildCorrupted          -1412 /* Failed to build a secondary index that properly reflects primary index */
#define JET_errPrimaryIndexCorrupted        -1413 /* Primary index is corrupt. The database must be defragmented or the table deleted. */
#define JET_errSecondaryIndexCorrupted      -1414 /* Secondary index is corrupt. The database must be defragmented or the affected index must be deleted. If the corrupt index is over Unicode text, a likely cause is a sort-order change. */
#define JET_wrnCorruptIndexDeleted           1415 /* Out of date index removed */
#define JET_errInvalidIndexId               -1416 /* Illegal index id */
#define JET_wrnPrimaryIndexOutOfDate         1417 /* The Primary index is created with an incompatible OS sort version. The table can not be safely modified. */
#define JET_wrnSecondaryIndexOutOfDate       1418 /* One or more Secondary index is created with an incompatible OS sort version. Any index over Unicode text should be deleted. */
#define JET_errCantUseDeferredPopulateIndex -1419 /* A deferred population index may not be used until completely populated */
#define JET_errIndexTuplesSecondaryIndexOnly        -1430   //  tuple index can only be on a secondary index
#define JET_errIndexTuplesTooManyColumns            -1431   //  tuple index may only have eleven columns in the index
#define JET_errIndexTuplesOneColumnOnly             JET_errIndexTuplesTooManyColumns    /* OBSOLETE */
#define JET_errIndexTuplesNonUniqueOnly             -1432   //  tuple index must be a non-unique index
#define JET_errIndexTuplesTextBinaryColumnsOnly     -1433   //  tuple index must be on a text/binary column
#define JET_errIndexTuplesTextColumnsOnly           JET_errIndexTuplesTextBinaryColumnsOnly     /* OBSOLETE */
#define JET_errIndexTuplesVarSegMacNotAllowed       -1434   //  tuple index does not allow setting cbVarSegMac
#define JET_errIndexTuplesInvalidLimits             -1435   //  invalid min/max tuple length or max characters to index specified
#define JET_errIndexTuplesCannotRetrieveFromIndex   -1436   //  cannot call RetrieveColumn() with RetrieveFromIndex on a tuple index
#define JET_errIndexTuplesKeyTooSmall               -1437   //  specified key does not meet minimum tuple length
#define JET_errInvalidLVChunkSize                   -1438   //  Specified LV chunk size is not supported
#define JET_errColumnCannotBeEncrypted              -1439   //  Only JET_coltypLongText and JET_coltypLongBinary columns without default values can be encrypted
#define JET_errCannotIndexOnEncryptedColumn         -1440   //  Cannot index encrypted column

/*  DML errors
// Note: Some DML errors have snuck into other categories.
// Note: Some DDL errors have inappropriately snuck in here.
#define JET_errColumnLong                   -1501 /* Column value is long */
#define JET_errColumnNoChunk                -1502 /* No such chunk in long value */
#define JET_errColumnDoesNotFit             -1503 /* Field will not fit in record */
#define JET_errNullInvalid                  -1504 /* Null not valid */
#define JET_errColumnIndexed                -1505 /* Column indexed, cannot delete */
#define JET_errColumnTooBig                 -1506 /* Field length is greater than maximum */
#define JET_errColumnNotFound               -1507 /* No such column */
#define JET_errColumnDuplicate              -1508 /* Field is already defined */
#define JET_errMultiValuedColumnMustBeTagged    -1509 /* Attempted to create a multi-valued column, but column was not Tagged */
#define JET_errColumnRedundant              -1510 /* Second autoincrement or version column */
#define JET_errInvalidColumnType            -1511 /* Invalid column data type */
#define JET_wrnColumnMaxTruncated            1512 /* Max length too big, truncated */
#define JET_errTaggedNotNULL                -1514 /* No non-NULL tagged columns */
#define JET_errNoCurrentIndex               -1515 /* Invalid w/o a current index */
#define JET_errKeyIsMade                    -1516 /* The key is completely made */
#define JET_errBadColumnId                  -1517 /* Column Id Incorrect */
#define JET_errBadItagSequence              -1518 /* Bad itagSequence for tagged column */
#define JET_errColumnInRelationship         -1519 /* Cannot delete, column participates in relationship */
#define JET_wrnCopyLongValue                 1520 /* Single instance column bursted */
#define JET_errCannotBeTagged               -1521 /* AutoIncrement and Version cannot be tagged */
#define wrnLVNoLongValues                    1522 /* Table does not have a long value tree */
#define JET_wrnTaggedColumnsRemaining        1523 /* RetrieveTaggedColumnList ran out of copy buffer before retrieving all tagged columns */
#define JET_errDefaultValueTooBig           -1524 /* Default value exceeds maximum size */
#define JET_errMultiValuedDuplicate         -1525 /* Duplicate detected on a unique multi-valued column */
#define JET_errLVCorrupted                  -1526 /* Corruption encountered in long-value tree */
#define wrnLVNoMoreData                      1527 /* Reached end of LV data */
#define JET_errMultiValuedDuplicateAfterTruncation  -1528 /* Duplicate detected on a unique multi-valued column after data was normalized, and normalizing truncated the data before comparison */
#define JET_errDerivedColumnCorruption      -1529 /* Invalid column in derived table */
#define JET_errInvalidPlaceholderColumn     -1530 /* Tried to convert column to a primary index placeholder, but column doesn't meet necessary criteria */
#define JET_wrnColumnSkipped                 1531 /* Column value(s) not returned because the corresponding column id or itagSequence requested for enumeration was null */
#define JET_wrnColumnNotLocal                1532 /* Column value(s) not returned because they could not be reconstructed from the data at hand */
#define JET_wrnColumnMoreTags                1533 /* Column values exist that were not requested for enumeration */
#define JET_wrnColumnTruncated               1534 /* Column value truncated at the requested size limit during enumeration */
#define JET_wrnColumnPresent                 1535 /* Column values exist but were not returned by request */
#define JET_wrnColumnSingleValue             1536 /* Column value returned in JET_COLUMNENUM as a result of JET_bitEnumerateCompressOutput */
#define JET_wrnColumnDefault                 1537 /* Column value(s) not returned because they were set to their default value(s) and JET_bitEnumerateIgnoreDefault was specified */
#define JET_errColumnCannotBeCompressed     -1538 /* Only JET_coltypLongText and JET_coltypLongBinary columns can be compressed */
#define JET_wrnColumnNotInRecord             1539 /* Column value(s) not returned because they could not be reconstructed from the data in the record */
#define JET_errColumnNoEncryptionKey        -1540 /* Cannot retrieve/set encrypted column without an encryption key */
#define JET_wrnColumnReference               1541 /* Column value returned as a reference because it could not be reconstructed from the data in the record */

#define JET_errRecordNotFound               -1601 /* The key was not found */
#define JET_errRecordNoCopy                 -1602 /* No working buffer */
#define JET_errNoCurrentRecord              -1603 /* Currency not on a record */
#define JET_errRecordPrimaryChanged         -1604 /* Primary key may not change */
#define JET_errKeyDuplicate                 -1605 /* Illegal duplicate key */
#define JET_errAlreadyPrepared              -1607 /* Attempted to update record when record update was already in progress */
#define JET_errKeyNotMade                   -1608 /* No call to JetMakeKey */
#define JET_errUpdateNotPrepared            -1609 /* No call to JetPrepareUpdate */
#define JET_wrnDataHasChanged                1610 /* Data has changed */
#define JET_errDataHasChanged               -1611 /* Data has changed, operation aborted */
#define JET_wrnKeyChanged                    1618 /* Moved to new key */
#define JET_errLanguageNotSupported         -1619 /* Windows installation does not support language */
#define JET_errDecompressionFailed          -1620 /* Internal error: data could not be decompressed */
#define JET_errUpdateMustVersion            -1621 /* No version updates only for uncommitted tables */
#define JET_errDecryptionFailed             -1622 /* Data could not be decrypted */
#define JET_errEncryptionBadItag            -1623 /* Cannot encrypt tagged columns with itag>1 */
#define JET_errSetAutoIncrementTooHigh      -1624  /* The auto-increment value that the user tried to set explicitly is too high . */
#define JET_errAutoIncrementNotSet          -1625  /* The user must have explicitly set the auto-increment column for this table. */

/*  Sort Table errors
#define JET_errTooManySorts                 -1701 /* Too many sort processes */
#define JET_errInvalidOnSort                -1702 /* Invalid operation on Sort */

/*  Other errors
#define JET_errTempFileOpenError            -1803 /* Temp file could not be opened */
#define JET_errTooManyAttachedDatabases     -1805 /* Too many open databases */
#define JET_errDiskFull                     -1808 /* No space left on disk */
#define JET_errPermissionDenied             -1809 /* Permission denied */
#define JET_errFileNotFound                 -1811 /* File not found */
#define JET_errFileInvalidType              -1812 /* Invalid file type */
#define JET_wrnFileOpenReadOnly              1813 /* Database file is read only */
#define JET_errFileAlreadyExists            -1814 /* File already exists */

#define JET_errAfterInitialization          -1850 /* Cannot Restore after init. */
#define JET_errLogCorrupted                 -1852 /* Logs could not be interpreted */

#define JET_errInvalidOperation             -1906 /* Invalid operation */
#define JET_errAccessDenied                 -1907 /* Access denied */
#define JET_wrnIdleFull                      1908 /* Idle registry full */
#define JET_errTooManySplits                -1909 /* Infinite split */
#define JET_errSessionSharingViolation      -1910 /* Multiple threads are using the same session */
#define JET_errEntryPointNotFound           -1911 /* An entry point in a DLL we require could not be found */
#define JET_errSessionContextAlreadySet     -1912 /* Specified session already has a session context set */
#define JET_errSessionContextNotSetByThisThread -1913 /* Tried to reset session context, but current thread did not originally set the session context */
#define JET_errSessionInUse                 -1914 /* Tried to terminate session in use */
#define JET_errRecordFormatConversionFailed -1915 /* Internal error during dynamic record format conversion */
#define JET_errOneDatabasePerSession        -1916 /* Just one open user database per session is allowed (JET_paramOneDatabasePerSession) */
#define JET_errRollbackError                -1917 /* error during rollback */
#define JET_errFlushMapVersionUnsupported   -1918 /* The version of the persisted flush map is not supported by this version of the engine. */
#define JET_errFlushMapDatabaseMismatch     -1919 /* The persisted flush map and the database do not match. */
#define JET_errFlushMapUnrecoverable        -1920 /* The persisted flush map cannot be reconstructed. */
#define JET_errRBSFileCorrupt               -1921  /* RBS file is corrupt */
#define JET_errRBSHeaderCorrupt             -1922  /* RBS header is corrupt */
#define JET_errRBSDbMismatch                -1923  /* RBS is out of sync with the database file */
#define errRBSAttachInfoNotFound            -1924  /* Couldn't find the RBS attach info we wanted */
#define JET_errBadRBSVersion                -1925  /* Version of revert snapshot file is not compatible with Jet version */
#define JET_errOutOfRBSSpace                -1926  /* Revert snapshot file has reached its maximum size */
#define JET_errRBSInvalidSign               -1927  /* RBS signature is not set in the RBS header */
#define JET_errRBSInvalidRecord             -1928  /* Invalid RBS record found in the revert snapshot */
#define JET_errRBSRCInvalidRBS              -1929  /* The database cannot be reverted to the expected time as there are some invalid revert snapshots to revert to that time. */
#define JET_errRBSRCNoRBSFound              -1930  /* The database cannot be reverted to the expected time as there no revert snapshots to revert to that time. */
#define JET_errRBSRCBadDbState              -1931  /* The database revert to the expected time failed as the database has a bad dbstate. */
#define JET_errRBSMissingReqLogs            -1932  /* The required logs for the revert snapshot are missing. */
#define JET_errRBSLogDivergenceFailed       -1933  /* The required logs for the revert snapshot are diverged with logs in the log directory. */
#define JET_errRBSRCCopyLogsRevertState     -1934  /* The database cannot be reverted to the expected time as we are in copying logs stage from previous revert request and a further revert in the past is requested which might leave logs in corrupt state. */
#define JET_errDatabaseIncompleteRevert     -1935  /* The database cannot be attached because it is currently being reverted using revert snapshot. */
#define JET_errRBSRCRevertCancelled         -1936  /* The database revert has been cancelled. */
#define JET_errRBSRCInvalidDbFormatVersion  -1937  /* The database format version for the databases to be reverted doesn't support applying the revert snapshot. */
#define JET_errRBSCannotDetermineDivergence -1938  /* The required logs for the revert snapshot are missing in log directory and hence we cannot determine if those logs are diverged with the logs in snapshot directory. */
#define errRBSRequiredRangeTooLarge         -1939  /* RBS was not created as the required range was too large and we don't want to start revert snapshot from such a state. */
#define errRBSPatching                      -1940  /* RBS was not created as RBS is being attached for patching purposes, usually due to incremental reseed or page patching on the databases attached to the RBS. */
#define JET_errRBSDeleteTableTooBig         -1941  /* The table being deleted is bigger than the configured max size to delete while activated on RBS copy, retry delete when activated on another copy */
#define JET_errRBSDeleteTableTooSoon        -1942  /* The table was created or the root page of table being deleted was moved in the last few days and hence a non-revertable delete cannot be attempted right now. */
#define JET_errRBSFDPToBeDeleted            -1943  /* The FDP is about to be deleted. The table was originally deleted using non-revertable flag and the database was then reverted to a previous state using RBS causing the table's pages to not be reverted but table root page and space tree pages were reverted to assist in catalog cleanup. */
#define JET_errRBSRevertableDeleteNotPossible -1944  /* The table being deleted with revertable delete flag is not possible as this table was previously deleted with non-revertable flag and partially reverted by RBS. */
#define errRBSDeleteTableTooSoonTimeNull     -1945  /* The time the table was created or the time since the root page of table was last moved is not set and hence a non-revertable delete cannot be attempted right now. */
#define errRBSCorruptUninitializedRBSRemoved -1946  /* The RBS being loaded is either missing or corrupt and uninitialized, so it has been removed. */
#define JET_errRBSRedeleteFDPUnexpected     -1947  /* Indicates that the reverted table marked with delete flag is unexpected. */
#define JET_errRBSRCPageFDPDeleteFileCorrupt -1948  /* The database cannot be reverted to the expected time as we are in apply root page records state but the corresponding file to init the page state is corrupt */
#define JET_wrnDefragAlreadyRunning          2000 /* Online defrag already running on specified database */
#define JET_wrnDefragNotRunning              2001 /* Online defrag not running on specified database */
#define JET_wrnDatabaseScanAlreadyRunning    2002 /* JetDatabaseScan already running on specified database */
#define JET_wrnDatabaseScanNotRunning        2003 /* JetDatabaseScan not running on specified database */
#define JET_errDatabaseAlreadyRunningMaintenance -2004  /* The operation did not complete successfully because the database is already running maintenance on specified database */
#define wrnOLD2TaskSlotFull                  2005 /* Online defrag task slots are full */
#define JET_errRootSpaceLeakEstimationAlreadyRunning -2006  /* The operation did not complete successfully because root space leak estimation is already running on the specified database */
#define JET_wrnCallbackNotRegistered         2100 /* Unregistered a non-existent callback function */
#define JET_errCallbackFailed               -2101 /* A callback failed */
#define JET_errCallbackNotResolved          -2102 /* A callback function could not be found */

#define JET_errSpaceHintsInvalid            -2103 /* An element of the JET space hints structure was not correct or actionable. */

#define JET_errOSSnapshotInvalidSequence    -2401 /* OS Shadow copy API used in an invalid sequence */
#define JET_errOSSnapshotTimeOut            -2402 /* OS Shadow copy ended with time-out */
#define JET_errOSSnapshotNotAllowed         -2403 /* OS Shadow copy not allowed (backup or recovery in progress) */
#define JET_errOSSnapshotInvalidSnapId      -2404 /* invalid JET_OSSNAPID */
#define errOSSnapshotNewLogStopped          -2405 /* new log file creation stopped in with edbtmp.[log|jtx] created */

/** TEST INJECTION ERRORS
 **/
#define JET_errTooManyTestInjections        -2501 /* Internal test injection limit hit */
#define JET_errTestInjectionNotSupported    -2502 /* Test injection not supported */
/** PRODUCE and CONSUME LOG DATA ERRORS
 **/
#define JET_errInvalidLogDataSequence       -2601 /* Some how the log data provided got out of sequence with the current state of the instance */
#define JET_wrnPreviousLogFileIncomplete    2602 /* The log data provided jumped to the next log suddenly, we have deleted the incomplete log file as a precautionary measure */
/** KVP ERRORS
 **/
#define wrnKVPEntryAlreadyNotPresent        2626 /* Attempted to delete a non-existant key */
#define JET_errLSCallbackNotSpecified       -3000 /* Attempted to use Local Storage without a callback function being specified */
#define JET_errLSAlreadySet                 -3001 /* Attempted to set Local Storage for an object which already had it set */
#define JET_errLSNotSet                     -3002 /* Attempted to retrieve Local Storage from an object which didn't have it set */

/** FILE and DISK ERRORS
 **/
//JET_errFileAccessDenied                   -1032
//JET_errFileNotFound                       -1811
//JET_errInvalidFilename                    -1044
#define JET_errFileIOSparse                 -4000 /* an I/O was issued to a location that was sparse */
#define JET_errFileIOBeyondEOF              -4001 /* a read was issued to a location beyond EOF (writes will expand the file) */
#define JET_errFileIOAbort                  -4002 /* instructs the JET_ABORTRETRYFAILCALLBACK caller to abort the specified I/O */
#define JET_errFileIORetry                  -4003 /* instructs the JET_ABORTRETRYFAILCALLBACK caller to retry the specified I/O */
#define JET_errFileIOFail                   -4004 /* instructs the JET_ABORTRETRYFAILCALLBACK caller to fail the specified I/O */
#define JET_errFileCompressed               -4005 /* read/write access is not supported on compressed files */
#define errDiskTilt                             -4006 /* too much concurrent IO outstanding */
// was: wrnDiskGameOn                       4007 /* the respective IO dispatch queue has gotten low */
// wrnIOHeapNotReserved                     4008
#define wrnIOPending                        4009 /* IO is pending in the OS */
#define wrnIOSlow                       4010 /* IO completed but took abnormally long to return from the OS */
/** CLIENT RESERVED ERROR SPACE.
    An unused errors/warnings section.  JET will never generate values in this space.  Clients may use this space
        without conflicting with ESE.  Note that the warnings are reserved as well as the errors.  That is, the
        range from -10,000 to -11,999 is reserved as well as the range from 10,000 to 11,999.
 **/
#define JET_errClientSpaceBegin             -10000 /* Begin of the error space reserved for JET client use */
#define JET_errClientSpaceEnd               -11999 /* End of the error space reserved for JET client use */

/**********************************************************************/
/***********************     PROTOTYPES      **************************/
/**********************************************************************/

#if !defined(_JET_NOPROTOTYPES)

#ifdef __cplusplus
extern "C" {
#endif

JET_ERR JET_API
JetInit(
    JET_INSTANCE *  pinstance );

#if ( JET_VERSION >= 0x0501 )
JET_ERR JET_API
JetInit2(
    JET_INSTANCE *  pinstance,
    JET_GRBIT              grbit );

#endif // JET_VERSION >= 0x0501

#if ( JET_VERSION >= 0x0600 )
#if ( JET_VERSION < 0x0600 )
#define JetInit3A JetInit3
#endif

JET_ERR JET_API
JetInit3A(
    JET_INSTANCE *  pinstance,
    JET_RSTINFO_A *    prstInfo,
    JET_GRBIT              grbit );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetInit3W(
    JET_INSTANCE *  pinstance,
    JET_RSTINFO_W *    prstInfo,
    JET_GRBIT              grbit );

#ifdef JET_UNICODE
#define JetInit3 JetInit3W
#else
#define JetInit3 JetInit3A
#endif
#endif

#endif // JET_VERSION >= 0x0600
#if ( JET_VERSION >= 0x0A01 )

JET_ERR JET_API
JetInit4A(
    JET_INSTANCE *  pinstance,
    JET_RSTINFO2_A *   prstInfo,
    JET_GRBIT              grbit );

JET_ERR JET_API
JetInit4W(
    JET_INSTANCE *  pinstance,
    JET_RSTINFO2_W *   prstInfo,
    JET_GRBIT              grbit );

#ifdef JET_UNICODE
#define JetInit4 JetInit4W
#else
#define JetInit4 JetInit4A
#endif

#endif // JET_VERSION >= 0x0A01
#if ( JET_VERSION >= 0x0501 )
#if ( JET_VERSION < 0x0600 )
#define JetCreateInstanceA JetCreateInstance
#endif

JET_ERR JET_API
JetCreateInstanceA(
    JET_INSTANCE *    pinstance,
    JET_PCSTR      szInstanceName );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetCreateInstanceW(
    JET_INSTANCE *    pinstance,
    JET_PCWSTR     szInstanceName );

#ifdef JET_UNICODE
#define JetCreateInstance JetCreateInstanceW
#else
#define JetCreateInstance JetCreateInstanceA
#endif
#endif

#if ( JET_VERSION < 0x0600 )
#define JetCreateInstance2A JetCreateInstance2
#endif

JET_ERR JET_API
JetCreateInstance2A(
    JET_INSTANCE *    pinstance,
    JET_PCSTR      szInstanceName,
    JET_PCSTR      szDisplayName,
    JET_GRBIT          grbit );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetCreateInstance2W(
    JET_INSTANCE *    pinstance,
    JET_PCWSTR     szInstanceName,
    JET_PCWSTR     szDisplayName,
    JET_GRBIT          grbit );

#ifdef JET_UNICODE
#define JetCreateInstance2 JetCreateInstance2W
#else
#define JetCreateInstance2 JetCreateInstance2A
#endif
#endif

#endif // JET_VERSION >= 0x0501

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetGetInstanceMiscInfo(
    JET_INSTANCE               instance,
    void *  pvResult,
    uint32_t              cbMax,
    uint32_t              InfoLevel );

#endif // JET_VERSION >= 0x0600

JET_ERR JET_API
JetTerm(
    JET_INSTANCE   instance );

JET_ERR JET_API
JetTerm2(
    JET_INSTANCE   instance,
    JET_GRBIT      grbit );

JET_ERR JET_API
JetStopService();

#if ( JET_VERSION >= 0x0501 )

JET_ERR JET_API
JetStopServiceInstance(
    JET_INSTANCE   instance );

#endif // JET_VERSION >= 0x0501

#if ( JET_VERSION >= 0x0602 )

JET_ERR JET_API
JetStopServiceInstance2(
    JET_INSTANCE       instance,
    const JET_GRBIT    grbit );

#endif // JET_VERSION >= 0x0602

JET_ERR JET_API
JetStopBackup();

#if ( JET_VERSION >= 0x0501 )

JET_ERR JET_API
JetStopBackupInstance(
    JET_INSTANCE   instance );

#endif // JET_VERSION >= 0x0501

#if ( JET_VERSION < 0x0600 )
#define JetSetSystemParameterA JetSetSystemParameter
#endif

JET_ERR JET_API
JetSetSystemParameterA(
    JET_INSTANCE *  pinstance,
    JET_SESID          sesid,
    uint32_t          paramid,
    JET_API_PTR        lParam,
    JET_PCSTR          szParam );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetSetSystemParameterW(
    JET_INSTANCE *  pinstance,
    JET_SESID          sesid,
    uint32_t          paramid,
    JET_API_PTR        lParam,
    JET_PCWSTR         szParam );

#ifdef JET_UNICODE
#define JetSetSystemParameter JetSetSystemParameterW
#else
#define JetSetSystemParameter JetSetSystemParameterA
#endif
#endif

#if ( JET_VERSION < 0x0600 )
#define JetGetSystemParameterA JetGetSystemParameter
#endif

JET_ERR JET_API
JetGetSystemParameterA(
    JET_INSTANCE                   instance,
    JET_SESID                  sesid,
    uint32_t                  paramid,
    JET_API_PTR *             plParam,
    JET_PSTR    szParam,
    uint32_t                  cbMax );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetGetSystemParameterW(
    JET_INSTANCE                   instance,
    JET_SESID                  sesid,
    uint32_t                  paramid,
    JET_API_PTR *             plParam,
    JET_PWSTR   szParam,
    uint32_t                  cbMax );

#ifdef JET_UNICODE
#define JetGetSystemParameter JetGetSystemParameterW
#else
#define JetGetSystemParameter JetGetSystemParameterA
#endif
#endif
#if ( JET_VERSION >= 0x0603 )

JET_ERR JET_API
JetSetResourceParam(
    JET_INSTANCE   instance,
    JET_RESOPER    resoper,
    JET_RESID      resid,
    JET_API_PTR    ulParam );

JET_ERR JET_API
JetGetResourceParam(
    JET_INSTANCE   instance,
    JET_RESOPER    resoper,
    JET_RESID      resid,
    JET_API_PTR*  pulParam );

#endif // JET_VERSION >= 0x0603
#if ( JET_VERSION >= 0x0501 )

#if ( JET_VERSION < 0x0600 )
#define JetEnableMultiInstanceA JetEnableMultiInstance
#endif

JET_ERR JET_API
JetEnableMultiInstanceA(
    JET_SETSYSPARAM_A *  psetsysparam,
    uint32_t                                  csetsysparam,
    uint32_t *                           pcsetsucceed );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetEnableMultiInstanceW(
    JET_SETSYSPARAM_W *  psetsysparam,
    uint32_t                                  csetsysparam,
    uint32_t *                           pcsetsucceed );

#ifdef JET_UNICODE
#define JetEnableMultiInstance JetEnableMultiInstanceW
#else
#define JetEnableMultiInstance JetEnableMultiInstanceA
#endif
#endif

#endif // JET_VERSION >= 0x0501
JET_ERR JET_API
JetResetCounter(
    JET_SESID  sesid,
    int32_t       CounterType );

JET_ERR JET_API
JetGetCounter(
    JET_SESID  sesid,
    int32_t       CounterType,
    int32_t *    plValue );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetGetThreadStats(
    void *  pvResult,
    uint32_t              cbMax );

#endif // JET_VERSION >= 0x0600

#if ( JET_VERSION < 0x0600 )
#define JetBeginSessionA JetBeginSession
#endif

JET_ERR JET_API JetBeginSessionA(
    JET_INSTANCE   instance,
    JET_SESID *   psesid,
    JET_PCSTR  szUserName,
    JET_PCSTR  szPassword );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API JetBeginSessionW(
    JET_INSTANCE   instance,
    JET_SESID *   psesid,
    JET_PCWSTR szUserName,
    JET_PCWSTR szPassword );

#ifdef JET_UNICODE
#define JetBeginSession JetBeginSessionW
#else
#define JetBeginSession JetBeginSessionA
#endif
#endif

JET_ERR JET_API
JetDupSession(
    JET_SESID      sesid,
    JET_SESID *   psesid );

JET_ERR JET_API
JetEndSession(
    JET_SESID  sesid,
    JET_GRBIT  grbit );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetGetSessionInfo(
    JET_SESID                  sesid,
    void *  pvResult,
     const uint32_t        cbMax,
      const uint32_t        ulInfoLevel );

#endif // JET_VERSION >= 0x0600
JET_ERR JET_API
JetGetVersion(
    JET_SESID          sesid,
    uint32_t *   pwVersion );

JET_ERR JET_API
JetIdle(
    JET_SESID  sesid,
    JET_GRBIT  grbit );

#if ( JET_VERSION >= 0x0A01 )

//  Database parameters
//
//      JET_dbparamBase                     8192     //  All JET_dbparams designed to be distinct from system / JET_params and JET_sesparams for code defense.

#define JET_dbparamDbSizeMaxPages           8193     //  This is equivalent to the cpgDatabaseSizeMax parameter passed into JetAttachDatabase2 and
                                                     //  JetCreateDatabase2, i.e., it determines the maximum size of the database in number of pages.

#define JET_dbparamCachePriority            8194     //  Cache priority to be assigned to database pages from a specific database.
                                                     //  See comment next to JET_paramCachePriority for how JET_sesparamCachePriority,
                                                     //  JET_dbparamCachePriority and JET_paramCachePriority interact.

#define JET_dbparamShrinkDatabaseOptions    8195     //  Options for database shrink. The default value is 0 (no options set).

#define JET_dbparamShrinkDatabaseTimeQuota  8196     //  Time quota (in msec) allocated to database shrink. -1 means "unlimited".
                                                     //  Valid range is 0 - 7 days, in addition to -1. The default value is -1.

#define JET_dbparamShrinkDatabaseSizeLimit  8197     //  Size below which the engine should stop attempting to shrink a database during attach, in pages.
                                                     //  The default value is 0 (i.e., shrink until it times out or there is no available space to move
                                                     //  data into).

#define JET_dbparamLeakReclaimerEnabled     8198    //  Enable reclaiming leaks during database attachment.

#define JET_dbparamLeakReclaimerTimeQuota   8199    //  Time quota (in msec) allocated to database leak reclaimer. -1 means "unlimited".
                                                    //  Valid range is 0 - 7 days, in addition to -1. The default value is -1.

#define JET_dbparamMaintainExtentPageCountCache 8200 //  If non-zero, the ExtentPageCountCache table will be created and maintained.
                                                     //  If zero, the ExtentPageCountCache table will be removed, if it exists.
                                                     //  The default value is 0 (i.e. remove unless explicitly told to keep).

#define JET_dbparamFlight_SelfAllocSpBufReservationEnabled 8201 //  Enable self-allocation of space to refill root split buffers.

#define JET_dbparamMaxValueInvalid          8202     //  This is not a valid database parameter. It can change from release to release!

// Values for JET_dbparamShrinkDatabaseOptions.
//

#define JET_bitShrinkDatabaseEofOnAttach                                0x00000001  // Resizes the database file during its attachment.

#define JET_bitShrinkDatabaseFullCategorizationOnAttach                 0x00000002  // Enables full space categorization when shrinking the database
                                                                                    // at attachment time. Enabling this may incur extra cost when
                                                                                    // shrinking the database, but avoids a potential small extra
                                                                                    // cost afterwards, when operating on the shrunk database.

#define JET_bitShrinkDatabaseDontMoveRootsOnAttach                      0x00000004  // Disable root moves when shrinking the database
                                                                                    // at attachment time. NOTE: temporary, for flighting only.

#define JET_bitShrinkDatabaseDontTruncateLeakedPagesOnAttach            0x00000008  // Disable truncating leaked pages when shrinking the database
                                                                                    // at attachment time. NOTE: temporary, for flighting only.

#define JET_bitShrinkDatabaseDontTruncateIndeterminatePagesOnAttach     0x00000010  // Disable truncating indeterminate/uncategorized pages when
                                                                                    // shrinking the database at attachment time.
                                                                                    // NOTE: temporary, for flighting only.

#endif // JET_VERSION >= 0x0A01
#if ( JET_VERSION < 0x0600 )
#define JetCreateDatabaseA JetCreateDatabase
#endif

JET_ERR JET_API
JetCreateDatabaseA(
    JET_SESID      sesid,
    JET_PCSTR      szFilename,
    JET_PCSTR  szConnect,
    JET_DBID *    pdbid,
    JET_GRBIT      grbit );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetCreateDatabaseW(
    JET_SESID      sesid,
    JET_PCWSTR     szFilename,
    JET_PCWSTR szConnect,
    JET_DBID *    pdbid,
    JET_GRBIT      grbit );

#ifdef JET_UNICODE
#define JetCreateDatabase JetCreateDatabaseW
#else
#define JetCreateDatabase JetCreateDatabaseA
#endif
#endif

#if ( JET_VERSION < 0x0600 )
#define JetCreateDatabase2A JetCreateDatabase2
#endif

JET_ERR JET_API
JetCreateDatabase2A(
    JET_SESID              sesid,
    JET_PCSTR              szFilename,
    const uint32_t    cpgDatabaseSizeMax,
    JET_DBID *            pdbid,
    JET_GRBIT              grbit );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API JetCreateDatabase2W(
    JET_SESID              sesid,
    JET_PCWSTR             szFilename,
    const uint32_t    cpgDatabaseSizeMax,
    JET_DBID *            pdbid,
    JET_GRBIT              grbit );

#ifdef JET_UNICODE
#define JetCreateDatabase2 JetCreateDatabase2W
#else
#define JetCreateDatabase2 JetCreateDatabase2A
#endif
#endif
#if ( JET_VERSION >= 0x0A01 )

JET_ERR JET_API JetCreateDatabase3A(
    JET_SESID                                  sesid,
    JET_PCSTR                                  szFilename,
    JET_DBID *                                pdbid,
    JET_SETDBPARAM *  rgsetdbparam,
    uint32_t                              csetdbparam,
    JET_GRBIT                                  grbit );

JET_ERR JET_API JetCreateDatabase3W(
    JET_SESID                                  sesid,
    JET_PCWSTR                                 szFilename,
    JET_DBID *                                pdbid,
    JET_SETDBPARAM *  rgsetdbparam,
    uint32_t                              csetdbparam,
    JET_GRBIT                                  grbit );

#ifdef JET_UNICODE
#define JetCreateDatabase3 JetCreateDatabase3W
#else
#define JetCreateDatabase3 JetCreateDatabase3A
#endif
#endif // JET_VERSION >= 0x0A01
#if ( JET_VERSION < 0x0600 )
#define JetAttachDatabaseA JetAttachDatabase
#endif

JET_ERR JET_API
JetAttachDatabaseA(
    JET_SESID  sesid,
    JET_PCSTR  szFilename,
    JET_GRBIT  grbit );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetAttachDatabaseW(
    JET_SESID  sesid,
    JET_PCWSTR szFilename,
    JET_GRBIT  grbit );

#ifdef JET_UNICODE
#define JetAttachDatabase JetAttachDatabaseW
#else
#define JetAttachDatabase JetAttachDatabaseA
#endif
#endif

#if ( JET_VERSION < 0x0600 )
#define JetAttachDatabase2A JetAttachDatabase2
#endif

JET_ERR JET_API
JetAttachDatabase2A(
    JET_SESID              sesid,
    JET_PCSTR              szFilename,
    const uint32_t    cpgDatabaseSizeMax,
    JET_GRBIT              grbit );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetAttachDatabase2W(
    JET_SESID              sesid,
    JET_PCWSTR             szFilename,
    const uint32_t    cpgDatabaseSizeMax,
    JET_GRBIT              grbit );

#ifdef JET_UNICODE
#define JetAttachDatabase2 JetAttachDatabase2W
#else
#define JetAttachDatabase2 JetAttachDatabase2A
#endif
#endif
#if ( JET_VERSION >= 0x0A01 )

JET_ERR JET_API
JetAttachDatabase3A(
    JET_SESID                                  sesid,
    JET_PCSTR                                  szFilename,
    JET_SETDBPARAM *  rgsetdbparam,
    uint32_t                              csetdbparam,
    JET_GRBIT                                  grbit );

JET_ERR JET_API
JetAttachDatabase3W(
    JET_SESID                                  sesid,
    JET_PCWSTR                                 szFilename,
    JET_SETDBPARAM *  rgsetdbparam,
    uint32_t                              csetdbparam,
    JET_GRBIT                                  grbit );

#ifdef JET_UNICODE
#define JetAttachDatabase3 JetAttachDatabase3W
#else
#define JetAttachDatabase3 JetAttachDatabase3A
#endif
#endif // JET_VERSION >= 0x0A01
#if ( JET_VERSION < 0x0600 )
#define JetDetachDatabaseA JetDetachDatabase
#endif

JET_ERR JET_API
JetDetachDatabaseA(
    JET_SESID  sesid,
    JET_PCSTR  szFilename );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetDetachDatabaseW(
    JET_SESID  sesid,
    JET_PCWSTR szFilename );

#ifdef JET_UNICODE
#define JetDetachDatabase JetDetachDatabaseW
#else
#define JetDetachDatabase JetDetachDatabaseA
#endif
#endif

#if ( JET_VERSION >= 0x0501 )
#if ( JET_VERSION < 0x0600 )
#define JetDetachDatabase2A JetDetachDatabase2
#endif

JET_ERR JET_API
JetDetachDatabase2A(
    JET_SESID  sesid,
    JET_PCSTR  szFilename,
    JET_GRBIT  grbit);

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetDetachDatabase2W(
    JET_SESID  sesid,
    JET_PCWSTR szFilename,
    JET_GRBIT  grbit);

#ifdef JET_UNICODE
#define JetDetachDatabase2 JetDetachDatabase2W
#else
#define JetDetachDatabase2 JetDetachDatabase2A
#endif
#endif

#endif // JET_VERSION >= 0x0501

#if ( JET_VERSION < 0x0600 )
#define JetGetObjectInfoA JetGetObjectInfo
#endif

JET_ERR JET_API
JetGetObjectInfoA(
    JET_SESID                  sesid,
    JET_DBID                   dbid,
    JET_OBJTYP                 objtyp,
    JET_PCSTR              szContainerName,
    JET_PCSTR              szObjectName,
    void *  pvResult,
    uint32_t              cbMax,
    uint32_t              InfoLevel );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetGetObjectInfoW(
    JET_SESID                  sesid,
    JET_DBID                   dbid,
    JET_OBJTYP                 objtyp,
    JET_PCWSTR             szContainerName,
    JET_PCWSTR             szObjectName,
    void *  pvResult,
    uint32_t              cbMax,
    uint32_t              InfoLevel );

#ifdef JET_UNICODE
#define JetGetObjectInfo JetGetObjectInfoW
#else
#define JetGetObjectInfo JetGetObjectInfoA
#endif
#endif

#if ( JET_VERSION < 0x0600 )
#define JetGetTableInfoA JetGetTableInfo
#endif

JET_ERR JET_API
JetGetTableInfoA(
    JET_SESID                  sesid,
    JET_TABLEID                tableid,
    void *  pvResult,
    uint32_t              cbMax,
    uint32_t              InfoLevel );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetGetTableInfoW(
    JET_SESID                  sesid,
    JET_TABLEID                tableid,
    void *  pvResult,
    uint32_t              cbMax,
    uint32_t              InfoLevel );

#ifdef JET_UNICODE
#define JetGetTableInfo JetGetTableInfoW
#else
#define JetGetTableInfo JetGetTableInfoA
#endif
#endif
#if ( JET_VERSION >= 0x0A01 )

JET_ERR JET_API
JetSetTableInfoW(
    JET_SESID                              sesid,
    JET_TABLEID                                tableid,
    const void *    pvParam,
    uint32_t                              cbParam,
    uint32_t                              InfoLevel );

JET_ERR JET_API
JetSetTableInfoA(
    JET_SESID                              sesid,
    JET_TABLEID                                tableid,
    const void *    pvParam,
    uint32_t                              cbParam,
    uint32_t                              InfoLevel );

#ifdef JET_UNICODE
#define JetSetTableInfo JetSetTableInfoW
#else
#define JetSetTableInfo JetSetTableInfoA
#endif

JET_ERR JET_API
JetCreateEncryptionKey(
    uint32_t                                      encryptionAlgorithm,
    void *   pvKey,
    uint32_t                                      cbKey,
    uint32_t *                               pcbActual );

#endif // JET_VERSION >= 0x0A01
#if ( JET_VERSION < 0x0600 )
#define JetCreateTableA JetCreateTable
#endif

JET_ERR JET_API
JetCreateTableA(
    JET_SESID      sesid,
    JET_DBID       dbid,
    JET_PCSTR      szTableName,
    uint32_t  lPages,
    uint32_t  lDensity,
    JET_TABLEID * ptableid );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetCreateTableW(
    JET_SESID      sesid,
    JET_DBID       dbid,
    JET_PCWSTR     szTableName,
    uint32_t  lPages,
    uint32_t  lDensity,
    JET_TABLEID * ptableid );

#ifdef JET_UNICODE
#define JetCreateTable JetCreateTableW
#else
#define JetCreateTable JetCreateTableA
#endif
#endif

#if ( JET_VERSION < 0x0600 )
#define JetCreateTableColumnIndexA JetCreateTableColumnIndex
#endif

JET_ERR JET_API
JetCreateTableColumnIndexA(
    JET_SESID              sesid,
    JET_DBID               dbid,
    JET_TABLECREATE_A * ptablecreate );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetCreateTableColumnIndexW(
    JET_SESID              sesid,
    JET_DBID               dbid,
    JET_TABLECREATE_W * ptablecreate );

#ifdef JET_UNICODE
#define JetCreateTableColumnIndex JetCreateTableColumnIndexW
#else
#define JetCreateTableColumnIndex JetCreateTableColumnIndexA
#endif
#endif

#if ( JET_VERSION >= 0x0501 )
#if ( JET_VERSION < 0x0600 )
#define JetCreateTableColumnIndex2A JetCreateTableColumnIndex2
#endif

JET_ERR JET_API
JetCreateTableColumnIndex2A(
    JET_SESID                  sesid,
    JET_DBID                   dbid,
    JET_TABLECREATE2_A *    ptablecreate );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetCreateTableColumnIndex2W(
    JET_SESID                  sesid,
    JET_DBID                   dbid,
    JET_TABLECREATE2_W *    ptablecreate );

#ifdef JET_UNICODE
#define JetCreateTableColumnIndex2 JetCreateTableColumnIndex2W
#else
#define JetCreateTableColumnIndex2 JetCreateTableColumnIndex2A
#endif
#endif // JET_VERSION >= 0x0600
#endif // JET_VERSION >= 0x0501

#if ( JET_VERSION >= 0x0601 )

JET_ERR JET_API
JetCreateTableColumnIndex3A(
    JET_SESID                  sesid,
    JET_DBID                   dbid,
    JET_TABLECREATE3_A *    ptablecreate );

JET_ERR JET_API
JetCreateTableColumnIndex3W(
    JET_SESID                  sesid,
    JET_DBID                   dbid,
    JET_TABLECREATE3_W *    ptablecreate );

#ifdef JET_UNICODE
#define JetCreateTableColumnIndex3 JetCreateTableColumnIndex3W
#else
#define JetCreateTableColumnIndex3 JetCreateTableColumnIndex3A
#endif
#endif // JET_VERSION >= 0x0601

#if ( JET_VERSION >= 0x0602 )

JET_ERR JET_API
JetCreateTableColumnIndex4A(
    JET_SESID                  sesid,
    JET_DBID                   dbid,
    JET_TABLECREATE4_A *    ptablecreate );

JET_ERR JET_API
JetCreateTableColumnIndex4W(
    JET_SESID                  sesid,
    JET_DBID                   dbid,
    JET_TABLECREATE4_W *    ptablecreate );

#ifdef JET_UNICODE
#define JetCreateTableColumnIndex4 JetCreateTableColumnIndex4W
#else
#define JetCreateTableColumnIndex4 JetCreateTableColumnIndex4A
#endif
#endif // JET_VERSION >= 0x0602
#if ( JET_VERSION >= 0x0A01 )

JET_ERR JET_API
JetCreateTableColumnIndex5A(
    JET_SESID                  sesid,
    JET_DBID                   dbid,
    JET_TABLECREATE5_A *    ptablecreate );

JET_ERR JET_API
JetCreateTableColumnIndex5W(
    JET_SESID                  sesid,
    JET_DBID                   dbid,
    JET_TABLECREATE5_W *    ptablecreate );

#ifdef JET_UNICODE
#define JetCreateTableColumnIndex5 JetCreateTableColumnIndex5W
#else
#define JetCreateTableColumnIndex5 JetCreateTableColumnIndex5A
#endif
#endif // JET_VERSION >= 0x0A01
#if ( JET_VERSION < 0x0600 )
#define JetDeleteTableA JetDeleteTable
#endif

JET_ERR JET_API
JetDeleteTableA(
    JET_SESID  sesid,
    JET_DBID   dbid,
    JET_PCSTR  szTableName );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetDeleteTableW(
    JET_SESID  sesid,
    JET_DBID   dbid,
    JET_PCWSTR szTableName );

#ifdef JET_UNICODE
#define JetDeleteTable JetDeleteTableW
#else
#define JetDeleteTable JetDeleteTableA
#endif
#endif
#if ( JET_VERSION > 0x0A01 )

JET_ERR JET_API
JetDeleteTable2A(
    JET_SESID          sesid,
    JET_DBID           dbid,
    JET_PCSTR          szTableName,
    const JET_GRBIT    grbit );

JET_ERR JET_API
JetDeleteTable2W(
    JET_SESID          sesid,
    JET_DBID           dbid,
    JET_PCWSTR         wszTableName,
    const JET_GRBIT    grbit );

#ifdef JET_UNICODE
#define JetDeleteTable2 JetDeleteTable2A
#else
#define JetDeleteTable2 JetDeleteTable2W
#endif

#endif // JET_VERSION >= 0x0A01
#if ( JET_VERSION < 0x0600 )
#define JetRenameTableA JetRenameTable
#endif

JET_ERR JET_API
JetRenameTableA(
    JET_SESID  sesid,
    JET_DBID   dbid,
    JET_PCSTR  szName,
    JET_PCSTR  szNameNew );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API JetRenameTableW(
    JET_SESID  sesid,
    JET_DBID   dbid,
    JET_PCWSTR szName,
    JET_PCWSTR szNameNew );

#ifdef JET_UNICODE
#define JetRenameTable JetRenameTableW
#else
#define JetRenameTable JetRenameTableA
#endif
#endif

#if ( JET_VERSION < 0x0600 )
#define JetGetTableColumnInfoA JetGetTableColumnInfo
#endif

JET_ERR JET_API
JetGetTableColumnInfoA(
    JET_SESID                  sesid,
    JET_TABLEID                tableid,
    JET_PCSTR              szColumnName,
    void *  pvResult,
    uint32_t              cbMax,
    uint32_t              InfoLevel );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API JetGetTableColumnInfoW(
    JET_SESID                  sesid,
    JET_TABLEID                tableid,
    JET_PCWSTR             szColumnName,
    void *  pvResult,
    uint32_t              cbMax,
    uint32_t              InfoLevel );

#ifdef JET_UNICODE
#define JetGetTableColumnInfo JetGetTableColumnInfoW
#else
#define JetGetTableColumnInfo JetGetTableColumnInfoA
#endif
#endif

#if ( JET_VERSION < 0x0600 )
#define JetGetColumnInfoA JetGetColumnInfo
#endif

JET_ERR JET_API
JetGetColumnInfoA(
    JET_SESID                  sesid,
    JET_DBID                   dbid,
    JET_PCSTR                  szTableName,
    JET_PCSTR              pColumnNameOrId,
    void *  pvResult,
    uint32_t              cbMax,
    uint32_t              InfoLevel );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API JetGetColumnInfoW(
    JET_SESID                  sesid,
    JET_DBID                   dbid,
    JET_PCWSTR                 szTableName,
    JET_PCWSTR             pwColumnNameOrId,
    void *  pvResult,
    uint32_t              cbMax,
    uint32_t              InfoLevel );

#ifdef JET_UNICODE
#define JetGetColumnInfo JetGetColumnInfoW
#else
#define JetGetColumnInfo JetGetColumnInfoA
#endif
#endif

#if ( JET_VERSION < 0x0600 )
#define JetAddColumnA JetAddColumn
#endif

JET_ERR JET_API
JetAddColumnA(
    JET_SESID                              sesid,
    JET_TABLEID                            tableid,
    JET_PCSTR                              szColumnName,
    const JET_COLUMNDEF *                  pcolumndef,
    const void *  pvDefault,
    uint32_t                          cbDefault,
    JET_COLUMNID *                    pcolumnid );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API JetAddColumnW(
    JET_SESID                              sesid,
    JET_TABLEID                            tableid,
    JET_PCWSTR                             szColumnName,
    const JET_COLUMNDEF *                  pcolumndef,
    const void *  pvDefault,
    uint32_t                          cbDefault,
    JET_COLUMNID *                    pcolumnid );

#ifdef JET_UNICODE
#define JetAddColumn JetAddColumnW
#else
#define JetAddColumn JetAddColumnA
#endif
#endif

#if ( JET_VERSION < 0x0600 )
#define JetDeleteColumnA JetDeleteColumn
#endif

JET_ERR JET_API
JetDeleteColumnA(
    JET_SESID      sesid,
    JET_TABLEID    tableid,
    JET_PCSTR      szColumnName );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetDeleteColumnW(
    JET_SESID      sesid,
    JET_TABLEID    tableid,
    JET_PCWSTR     szColumnName );

#ifdef JET_UNICODE
#define JetDeleteColumn JetDeleteColumnW
#else
#define JetDeleteColumn JetDeleteColumnA
#endif
#endif

#if ( JET_VERSION >= 0x0501 )
#if ( JET_VERSION < 0x0600 )
#define JetDeleteColumn2A JetDeleteColumn2
#endif

JET_ERR JET_API
JetDeleteColumn2A(
    JET_SESID          sesid,
    JET_TABLEID        tableid,
    JET_PCSTR          szColumnName,
    const JET_GRBIT    grbit );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetDeleteColumn2W(
    JET_SESID          sesid,
    JET_TABLEID        tableid,
    JET_PCWSTR         szColumnName,
    const JET_GRBIT    grbit );

#ifdef JET_UNICODE
#define JetDeleteColumn2 JetDeleteColumn2W
#else
#define JetDeleteColumn2 JetDeleteColumn2A
#endif
#endif

#if ( JET_VERSION < 0x0600 )
#define JetRenameColumnA JetRenameColumn
#endif

JET_ERR JET_API
JetRenameColumnA(
    JET_SESID      sesid,
    JET_TABLEID    tableid,
    JET_PCSTR      szName,
    JET_PCSTR      szNameNew,
    JET_GRBIT      grbit );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetRenameColumnW(
    JET_SESID      sesid,
    JET_TABLEID    tableid,
    JET_PCWSTR     szName,
    JET_PCWSTR     szNameNew,
    JET_GRBIT      grbit );

#ifdef JET_UNICODE
#define JetRenameColumn JetRenameColumnW
#else
#define JetRenameColumn JetRenameColumnA
#endif
#endif

#endif // JET_VERSION >= 0x0501

#if ( JET_VERSION < 0x0600 )
#define JetSetColumnDefaultValueA JetSetColumnDefaultValue
#endif

JET_ERR JET_API
JetSetColumnDefaultValueA(
    JET_SESID                      sesid,
    JET_DBID                       dbid,
    JET_PCSTR                      szTableName,
    JET_PCSTR                      szColumnName,
    const void * pvData,
    const uint32_t            cbData,
    const JET_GRBIT                grbit );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetSetColumnDefaultValueW(
    JET_SESID                      sesid,
    JET_DBID                       dbid,
    JET_PCWSTR                     szTableName,
    JET_PCWSTR                     szColumnName,
    const void * pvData,
    const uint32_t            cbData,
    const JET_GRBIT                grbit );

#ifdef JET_UNICODE
#define JetSetColumnDefaultValue JetSetColumnDefaultValueW
#else
#define JetSetColumnDefaultValue JetSetColumnDefaultValueA
#endif
#endif

#if ( JET_VERSION < 0x0600 )
#define JetGetTableIndexInfoA JetGetTableIndexInfo
#endif

JET_ERR JET_API
JetGetTableIndexInfoA(
    JET_SESID                  sesid,
    JET_TABLEID                tableid,
    JET_PCSTR              szIndexName,
    void *   pvResult,
    uint32_t              cbResult,
    uint32_t              InfoLevel );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetGetTableIndexInfoW(
    JET_SESID                  sesid,
    JET_TABLEID                tableid,
    JET_PCWSTR             szIndexName,
    void *   pvResult,
    uint32_t              cbResult,
    uint32_t              InfoLevel );

#ifdef JET_UNICODE
#define JetGetTableIndexInfo JetGetTableIndexInfoW
#else
#define JetGetTableIndexInfo JetGetTableIndexInfoA
#endif
#endif

#if ( JET_VERSION < 0x0600 )
#define JetGetIndexInfoA JetGetIndexInfo
#endif

JET_ERR JET_API
JetGetIndexInfoA(
    JET_SESID                  sesid,
    JET_DBID                   dbid,
    JET_PCSTR                  szTableName,
    JET_PCSTR              szIndexName,
    void *   pvResult,
    uint32_t              cbResult,
    uint32_t              InfoLevel );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetGetIndexInfoW(
    JET_SESID                  sesid,
    JET_DBID                   dbid,
    JET_PCWSTR                 szTableName,
    JET_PCWSTR             szIndexName,
    void *   pvResult,
    uint32_t              cbResult,
    uint32_t              InfoLevel );

#ifdef JET_UNICODE
#define JetGetIndexInfo JetGetIndexInfoW
#else
#define JetGetIndexInfo JetGetIndexInfoA
#endif
#endif

#if ( JET_VERSION < 0x0600 )
#define JetCreateIndexA JetCreateIndex
#endif

JET_ERR JET_API
JetCreateIndexA(
    JET_SESID                      sesid,
    JET_TABLEID                    tableid,
    JET_PCSTR                      szIndexName,
    JET_GRBIT                      grbit,
    const char *  szKey,
    uint32_t                  cbKey,
    uint32_t                  lDensity );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetCreateIndexW(
    JET_SESID                      sesid,
    JET_TABLEID                    tableid,
    JET_PCWSTR                     szIndexName,
    JET_GRBIT                      grbit,
    const char16_t * szKey,
    uint32_t                  cbKey,
    uint32_t                  lDensity );

#ifdef JET_UNICODE
#define JetCreateIndex JetCreateIndexW
#else
#define JetCreateIndex JetCreateIndexA
#endif
#endif

#if ( JET_VERSION < 0x0600 )
#define JetCreateIndex2A JetCreateIndex2
#endif

JET_ERR JET_API
JetCreateIndex2A(
    JET_SESID                                  sesid,
    JET_TABLEID                                tableid,
    JET_INDEXCREATE_A *  pindexcreate,
    uint32_t                              cIndexCreate );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetCreateIndex2W(
    JET_SESID                                  sesid,
    JET_TABLEID                                tableid,
    JET_INDEXCREATE_W *  pindexcreate,
    uint32_t                              cIndexCreate );

#ifdef JET_UNICODE
#define JetCreateIndex2 JetCreateIndex2W
#else
#define JetCreateIndex2 JetCreateIndex2A
#endif
#endif

#if ( JET_VERSION >= 0x0601 )

JET_ERR JET_API
JetCreateIndex3A(
    JET_SESID                                  sesid,
    JET_TABLEID                                tableid,
    JET_INDEXCREATE2_A *pindexcreate,
    uint32_t                              cIndexCreate );

JET_ERR JET_API
JetCreateIndex3W(
    JET_SESID                                  sesid,
    JET_TABLEID                                tableid,
    JET_INDEXCREATE2_W *pindexcreate,
    uint32_t                              cIndexCreate );

#ifdef JET_UNICODE
#define JetCreateIndex3 JetCreateIndex3W
#else
#define JetCreateIndex3 JetCreateIndex3A
#endif

#endif

#if ( JET_VERSION >= 0x0602 )

JET_ERR JET_API
JetCreateIndex4A(
    JET_SESID                                  sesid,
    JET_TABLEID                                tableid,
    JET_INDEXCREATE3_A *pindexcreate,
    uint32_t                              cIndexCreate );

JET_ERR JET_API
JetCreateIndex4W(
    JET_SESID                                  sesid,
    JET_TABLEID                                tableid,
    JET_INDEXCREATE3_W *pindexcreate,
    uint32_t                              cIndexCreate );

#ifdef JET_UNICODE
#define JetCreateIndex4 JetCreateIndex4W
#else
#define JetCreateIndex4 JetCreateIndex4A
#endif

#endif

#if ( JET_VERSION < 0x0600 )
#define JetDeleteIndexA JetDeleteIndex
#endif

JET_ERR JET_API
JetDeleteIndexA(
    JET_SESID      sesid,
    JET_TABLEID    tableid,
    JET_PCSTR      szIndexName );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetDeleteIndexW(
    JET_SESID      sesid,
    JET_TABLEID    tableid,
    JET_PCWSTR     szIndexName );

#ifdef JET_UNICODE
#define JetDeleteIndex JetDeleteIndexW
#else
#define JetDeleteIndex JetDeleteIndexA
#endif
#endif

JET_ERR JET_API
JetBeginTransaction(
    JET_SESID  sesid );

JET_ERR JET_API
JetBeginTransaction2(
    JET_SESID  sesid,
    JET_GRBIT  grbit );

#if ( JET_VERSION >= 0x0602 )

JET_ERR JET_API
JetBeginTransaction3(
    JET_SESID      sesid,
    int64_t        trxid,
    JET_GRBIT      grbit );

#endif // JET_VERSION >= 0x0602
JET_ERR JET_API
JetPrepareToCommitTransaction(
    JET_SESID                      sesid,
    const void * pvData,
    uint32_t                  cbData,
    JET_GRBIT                      grbit );

JET_ERR JET_API
JetCommitTransaction(
    JET_SESID  sesid,
    JET_GRBIT  grbit );

#if ( JET_VERSION >= 0x0602 )
JET_ERR JET_API
JetCommitTransaction2(
    JET_SESID              sesid,
    JET_GRBIT              grbit,
    uint32_t          cmsecDurableCommit,
    JET_COMMIT_ID *   pCommitId );
#endif // JET_VERSION >= 0x0602

JET_ERR JET_API
JetRollback(
    JET_SESID  sesid,
    JET_GRBIT  grbit );

#if ( JET_VERSION < 0x0600 )
#define JetGetDatabaseInfoA JetGetDatabaseInfo
#endif

JET_ERR JET_API JetGetDatabaseInfoA(
    JET_SESID                  sesid,
    JET_DBID                   dbid,
    void *  pvResult,
    uint32_t              cbMax,
    uint32_t              InfoLevel );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetGetDatabaseInfoW(
    JET_SESID                  sesid,
    JET_DBID                   dbid,
    void *  pvResult,
    uint32_t              cbMax,
    uint32_t              InfoLevel );

#ifdef JET_UNICODE
#define JetGetDatabaseInfo JetGetDatabaseInfoW
#else
#define JetGetDatabaseInfo JetGetDatabaseInfoA
#endif
#endif // JET_VERSION >= 0x0600

#if ( JET_VERSION < 0x0600 )
#define JetGetDatabaseFileInfoA JetGetDatabaseFileInfo
#endif

JET_ERR JET_API
JetGetDatabaseFileInfoA(
    JET_PCSTR                  szDatabaseName,
    void *  pvResult,
    uint32_t              cbMax,
    uint32_t              InfoLevel );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetGetDatabaseFileInfoW(
    JET_PCWSTR                 szDatabaseName,
    void *  pvResult,
    uint32_t              cbMax,
    uint32_t              InfoLevel );

#ifdef JET_UNICODE
#define JetGetDatabaseFileInfo JetGetDatabaseFileInfoW
#else
#define JetGetDatabaseFileInfo JetGetDatabaseFileInfoA
#endif
#endif // JET_VERSION >= 0x0600
#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetGetLogFileInfoA(
    JET_PCSTR                  szLog,
    void *  pvResult,
    const uint32_t        cbMax,
    const uint32_t        InfoLevel );

JET_ERR JET_API
JetGetLogFileInfoW(
    JET_PCWSTR                 szLog,
    void *  pvResult,
    const uint32_t        cbMax,
    const uint32_t        InfoLevel );

#ifdef JET_UNICODE
#define JetGetLogFileInfo JetGetLogFileInfoW
#else
#define JetGetLogFileInfo JetGetLogFileInfoA
#endif
#endif // JET_VERSION >= 0x0600
#if ( JET_VERSION < 0x0600 )
#define JetOpenDatabaseA JetOpenDatabase
#endif

JET_ERR JET_API
JetOpenDatabaseA(
    JET_SESID      sesid,
    JET_PCSTR      szFilename,
    JET_PCSTR  szConnect,
    JET_DBID*     pdbid,
    JET_GRBIT      grbit );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetOpenDatabaseW(
    JET_SESID      sesid,
    JET_PCWSTR     szFilename,
    JET_PCWSTR szConnect,
    JET_DBID*     pdbid,
    JET_GRBIT      grbit );

#ifdef JET_UNICODE
#define JetOpenDatabase JetOpenDatabaseW
#else
#define JetOpenDatabase JetOpenDatabaseA
#endif
#endif // JET_VERSION >= 0x0600

JET_ERR JET_API
JetCloseDatabase(
    JET_SESID  sesid,
    JET_DBID   dbid,
    JET_GRBIT  grbit );

#if ( JET_VERSION < 0x0600 )
#define JetOpenTableA JetOpenTable
#endif

JET_ERR JET_API
JetOpenTableA(
    JET_SESID                                  sesid,
    JET_DBID                                   dbid,
    JET_PCSTR                                  szTableName,
    const void *   pvParameters,
    uint32_t                              cbParameters,
    JET_GRBIT                                  grbit,
    JET_TABLEID *                             ptableid );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetOpenTableW(
    JET_SESID                                  sesid,
    JET_DBID                                   dbid,
    JET_PCWSTR                                 szTableName,
    const void *   pvParameters,
    uint32_t                              cbParameters,
    JET_GRBIT                                  grbit,
    JET_TABLEID *                             ptableid );

#ifdef JET_UNICODE
#define JetOpenTable JetOpenTableW
#else
#define JetOpenTable JetOpenTableA
#endif
#endif // JET_VERSION >= 0x0600

#if ( JET_VERSION >= 0x0501 )

JET_ERR JET_API
JetSetTableSequential(
    JET_SESID      sesid,
    JET_TABLEID    tableid,
    JET_GRBIT      grbit );

JET_ERR JET_API
JetResetTableSequential(
    JET_SESID      sesid,
    JET_TABLEID    tableid,
    JET_GRBIT      grbit );

#endif // JET_VERSION >= 0x0501

JET_ERR JET_API
JetCloseTable(
    JET_SESID      sesid,
    JET_TABLEID    tableid );

JET_ERR JET_API
JetDelete(
    JET_SESID      sesid,
    JET_TABLEID    tableid );

JET_ERR JET_API
JetUpdate(
    JET_SESID                                          sesid,
    JET_TABLEID                                        tableid,
    void *  pvBookmark,
    uint32_t                                      cbBookmark,
    uint32_t *                               pcbActual );

#if ( JET_VERSION >= 0x0502 )

JET_ERR JET_API
JetUpdate2(
    JET_SESID                                          sesid,
    JET_TABLEID                                        tableid,
    void *  pvBookmark,
    uint32_t                                      cbBookmark,
    uint32_t *                               pcbActual,
    const JET_GRBIT                                    grbit );

#endif // JET_VERSION >= 0x0502

JET_ERR JET_API
JetEscrowUpdate(
    JET_SESID                                          sesid,
    JET_TABLEID                                        tableid,
    JET_COLUMNID                                       columnid,
    void *                        pv,
    uint32_t                                      cbMax,
    void * pvOld,
    uint32_t                                      cbOldMax,
    uint32_t *                               pcbOldActual,
    JET_GRBIT                                          grbit );

JET_ERR JET_API
JetRetrieveColumn(
    JET_SESID                                      sesid,
    JET_TABLEID                                    tableid,
    JET_COLUMNID                                   columnid,
    void *   pvData,
    uint32_t                                  cbData,
    uint32_t *                           pcbActual,
    JET_GRBIT                                      grbit,
    JET_RETINFO *                           pretinfo );

JET_ERR JET_API
JetRetrieveColumns(
    JET_SESID                                              sesid,
    JET_TABLEID                                            tableid,
    JET_RETRIEVECOLUMN * pretrievecolumn,
    uint32_t                                          cretrievecolumn );

#if ( JET_VERSION >= 0x0501 )

JET_ERR JET_API
JetEnumerateColumns(
    JET_SESID                                              sesid,
    JET_TABLEID                                            tableid,
    uint32_t                                          cEnumColumnId,
    JET_ENUMCOLUMNID *          rgEnumColumnId,
    uint32_t *                                       pcEnumColumn,
    JET_ENUMCOLUMN **   prgEnumColumn,
    JET_PFNREALLOC                                         pfnRealloc,
    void *                                             pvReallocContext,
    uint32_t                                          cbDataMost,
    JET_GRBIT                                              grbit );

#endif // JET_VERSION >= 0x0501
JET_ERR JET_API
JetRetrieveTaggedColumnList(
    JET_SESID                                                                              sesid,
    JET_TABLEID                                                                            tableid,
    uint32_t *                                                                       pcColumns,
    void *  pvData,
    uint32_t                                                                          cbData,
    JET_COLUMNID                                                                           columnidStart,
    JET_GRBIT                                                                              grbit );

#if ( JET_VERSION >= 0x0600 )
JET_ERR JET_API
JetGetRecordSize(
    JET_SESID          sesid,
    JET_TABLEID        tableid,
    JET_RECSIZE *   precsize,
    const JET_GRBIT    grbit );

#endif // JET_VERSION >= 0x0600

#if ( JET_VERSION >= 0x0601 )

JET_ERR JET_API
JetGetRecordSize2(
    JET_SESID          sesid,
    JET_TABLEID        tableid,
    JET_RECSIZE2 *  precsize,
    const JET_GRBIT    grbit );

#endif // JET_VERSION >= 0x0601
#if ( JET_VERSION >= 0x0A01 )

JET_ERR JET_API
JetGetRecordSize3(
    JET_SESID          sesid,
    JET_TABLEID        tableid,
    JET_RECSIZE3 *    precsize,
    const JET_GRBIT    grbit );

#endif // JET_VERSION >= 0x0A01
JET_ERR JET_API
JetSetColumn(
    JET_SESID                          sesid,
    JET_TABLEID                        tableid,
    JET_COLUMNID                       columnid,
    const void * pvData,
    uint32_t                      cbData,
    JET_GRBIT                          grbit,
    JET_SETINFO *                  psetinfo );

JET_ERR JET_API
JetSetColumns(
    JET_SESID                                  sesid,
    JET_TABLEID                                tableid,
    JET_SETCOLUMN *    psetcolumn,
    uint32_t                              csetcolumn );

JET_ERR JET_API
JetPrepareUpdate(
    JET_SESID      sesid,
    JET_TABLEID    tableid,
    uint32_t  prep );

JET_ERR JET_API
JetGetRecordPosition(
    JET_SESID                          sesid,
    JET_TABLEID                        tableid,
    JET_RECPOS * precpos,
    uint32_t                      cbRecpos );

JET_ERR JET_API
JetGotoPosition(
    JET_SESID      sesid,
    JET_TABLEID    tableid,
    JET_RECPOS *   precpos );

JET_ERR JET_API
JetGetCursorInfo(
    JET_SESID                  sesid,
    JET_TABLEID                tableid,
    void *  pvResult,
    uint32_t              cbMax,
    uint32_t              InfoLevel );

JET_ERR JET_API
JetDupCursor(
    JET_SESID      sesid,
    JET_TABLEID    tableid,
    JET_TABLEID * ptableid,
    JET_GRBIT      grbit );

#if ( JET_VERSION < 0x0600 )
#define JetGetCurrentIndexA JetGetCurrentIndex
#endif

JET_ERR JET_API
JetGetCurrentIndexA(
    JET_SESID                          sesid,
    JET_TABLEID                        tableid,
    JET_PSTR  szIndexName,
    uint32_t                      cbIndexName );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetGetCurrentIndexW(
    JET_SESID                          sesid,
    JET_TABLEID                        tableid,
    JET_PWSTR szIndexName,
    uint32_t                      cbIndexName );

#ifdef JET_UNICODE
#define JetGetCurrentIndex JetGetCurrentIndexW
#else
#define JetGetCurrentIndex JetGetCurrentIndexA
#endif
#endif // JET_VERSION >= 0x0600

#if ( JET_VERSION < 0x0600 )
#define JetSetCurrentIndexA JetSetCurrentIndex
#endif

JET_ERR JET_API
JetSetCurrentIndexA(
    JET_SESID      sesid,
    JET_TABLEID    tableid,
    JET_PCSTR  szIndexName );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetSetCurrentIndexW(
    JET_SESID      sesid,
    JET_TABLEID    tableid,
    JET_PCWSTR szIndexName );

#ifdef JET_UNICODE
#define JetSetCurrentIndex JetSetCurrentIndexW
#else
#define JetSetCurrentIndex JetSetCurrentIndexA
#endif
#endif // JET_VERSION >= 0x0600

#if ( JET_VERSION < 0x0600 )
#define JetSetCurrentIndex2A JetSetCurrentIndex2
#endif

JET_ERR JET_API
JetSetCurrentIndex2A(
    JET_SESID      sesid,
    JET_TABLEID    tableid,
    JET_PCSTR  szIndexName,
    JET_GRBIT      grbit );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetSetCurrentIndex2W(
    JET_SESID      sesid,
    JET_TABLEID    tableid,
    JET_PCWSTR szIndexName,
    JET_GRBIT      grbit );

#ifdef JET_UNICODE
#define JetSetCurrentIndex2 JetSetCurrentIndex2W
#else
#define JetSetCurrentIndex2 JetSetCurrentIndex2A
#endif
#endif // JET_VERSION >= 0x0600

#if ( JET_VERSION < 0x0600 )
#define JetSetCurrentIndex3A JetSetCurrentIndex3
#endif

JET_ERR JET_API
JetSetCurrentIndex3A(
    JET_SESID      sesid,
    JET_TABLEID    tableid,
    JET_PCSTR  szIndexName,
    JET_GRBIT      grbit,
    uint32_t  itagSequence );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetSetCurrentIndex3W(
    JET_SESID      sesid,
    JET_TABLEID    tableid,
    JET_PCWSTR szIndexName,
    JET_GRBIT      grbit,
    uint32_t  itagSequence );

#ifdef JET_UNICODE
#define JetSetCurrentIndex3 JetSetCurrentIndex3W
#else
#define JetSetCurrentIndex3 JetSetCurrentIndex3A
#endif
#endif // JET_VERSION >= 0x0600

#if ( JET_VERSION < 0x0600 )
#define JetSetCurrentIndex4A JetSetCurrentIndex4
#endif

JET_ERR JET_API
JetSetCurrentIndex4A(
    JET_SESID          sesid,
    JET_TABLEID        tableid,
    JET_PCSTR      szIndexName,
    JET_INDEXID *  pindexid,
    JET_GRBIT          grbit,
    uint32_t      itagSequence );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetSetCurrentIndex4W(
    JET_SESID          sesid,
    JET_TABLEID        tableid,
    JET_PCWSTR     szIndexName,
    JET_INDEXID *  pindexid,
    JET_GRBIT          grbit,
    uint32_t      itagSequence );

#ifdef JET_UNICODE
#define JetSetCurrentIndex4 JetSetCurrentIndex4W
#else
#define JetSetCurrentIndex4 JetSetCurrentIndex4A
#endif
#endif // JET_VERSION >= 0x0600

JET_ERR JET_API
JetMove(
    JET_SESID      sesid,
    JET_TABLEID    tableid,
    int32_t           cRow,
    JET_GRBIT      grbit );

#if ( JET_VERSION >= 0x0602 )
JET_ERR JET_API
JetSetCursorFilter(
    JET_SESID          sesid,
    JET_TABLEID        tableid,
    JET_INDEX_COLUMN *rgColumnFilters,
    uint32_t      cColumnFilters,
    JET_GRBIT          grbit );
#endif  //  JET_VERSION >= 0x0602

JET_ERR JET_API
JetGetLock(
    JET_SESID      sesid,
    JET_TABLEID    tableid,
    JET_GRBIT      grbit );

JET_ERR JET_API
JetMakeKey(
    JET_SESID                          sesid,
    JET_TABLEID                        tableid,
    const void * pvData,
    uint32_t                      cbData,
    JET_GRBIT                          grbit );

JET_ERR JET_API
JetSeek(
    JET_SESID      sesid,
    JET_TABLEID    tableid,
    JET_GRBIT      grbit );

#if ( JET_VERSION >= 0x0601 )

JET_ERR JET_API
JetPrereadKeys(
    JET_SESID                                      sesid,
    JET_TABLEID                                    tableid,
    const void **                 rgpvKeys,
    const uint32_t *         rgcbKeys,
    int32_t                                           ckeys,
    int32_t *                                    pckeysPreread,
    JET_GRBIT                                      grbit );

#endif // JET_VERSION >= 0x0601

#if ( JET_VERSION >= 0x0602 )

JET_ERR JET_API
JetPrereadIndexRanges(
    JET_SESID                                              sesid,
    JET_TABLEID                                            tableid,
    const JET_INDEX_RANGE * const      rgIndexRanges,
    const uint32_t                                    cIndexRanges,
    uint32_t * const                             pcRangesPreread,
    const JET_COLUMNID * const     rgcolumnidPreread,
    const uint32_t                                    ccolumnidPreread,
    JET_GRBIT                                              grbit ); // JET_bitPrereadForward, JET_bitPrereadBackward

#endif // JET_VERSION >= 0x0602

JET_ERR JET_API
JetGetBookmark(
    JET_SESID                                      sesid,
    JET_TABLEID                                    tableid,
    void *   pvBookmark,
    uint32_t                                  cbMax,
    uint32_t *                           pcbActual );

#if ( JET_VERSION >= 0x0501 )

JET_ERR JET_API
JetGetSecondaryIndexBookmark(
    JET_SESID                                                                  sesid,
    JET_TABLEID                                                                tableid,
    void *       pvSecondaryKey,
    uint32_t                                                              cbSecondaryKeyMax,
    uint32_t *                                                       pcbSecondaryKeyActual,
    void * pvPrimaryBookmark,
    uint32_t                                                              cbPrimaryBookmarkMax,
    uint32_t *                                                       pcbPrimaryBookmarkActual,
    const JET_GRBIT                                                            grbit );

#endif // JET_VERSION >= 0x0501

#if ( JET_VERSION < 0x0600 )
#define JetCompactA JetCompact
#endif

JET_ERR JET_API
JetCompactA(
    JET_SESID              sesid,
    JET_PCSTR              szDatabaseSrc,
    JET_PCSTR              szDatabaseDest,
    JET_PFNSTATUS          pfnStatus,
    JET_CONVERT_A *    pconvert,
    JET_GRBIT              grbit );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetCompactW(
    JET_SESID              sesid,
    JET_PCWSTR             szDatabaseSrc,
    JET_PCWSTR             szDatabaseDest,
    JET_PFNSTATUS          pfnStatus,
    JET_CONVERT_W *    pconvert,
    JET_GRBIT              grbit );

#ifdef JET_UNICODE
#define JetCompact JetCompactW
#else
#define JetCompact JetCompactA
#endif
#endif // JET_VERSION >= 0x0600

#if ( JET_VERSION < 0x0600 )
#define JetDefragmentA JetDefragment
#endif

JET_ERR JET_API
JetDefragmentA(
    JET_SESID              sesid,
    JET_DBID               dbid,
    JET_PCSTR          szTableName,
    uint32_t * pcPasses,
    uint32_t * pcSeconds,
    JET_GRBIT              grbit );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetDefragmentW(
    JET_SESID              sesid,
    JET_DBID               dbid,
    JET_PCWSTR         szTableName,
    uint32_t * pcPasses,
    uint32_t * pcSeconds,
    JET_GRBIT              grbit );

#ifdef JET_UNICODE
#define JetDefragment JetDefragmentW
#else
#define JetDefragment JetDefragmentA
#endif
#endif

#if ( JET_VERSION >= 0x0501 )
#if ( JET_VERSION < 0x0600 )
#define JetDefragment2A JetDefragment2
#endif

JET_ERR JET_API
JetDefragment2A(
    JET_SESID              sesid,
    JET_DBID               dbid,
    JET_PCSTR          szTableName,
    uint32_t * pcPasses,
    uint32_t * pcSeconds,
    JET_CALLBACK           callback,
    JET_GRBIT              grbit );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetDefragment2W(
    JET_SESID              sesid,
    JET_DBID               dbid,
    JET_PCWSTR         szTableName,
    uint32_t * pcPasses,
    uint32_t * pcSeconds,
    JET_CALLBACK           callback,
    JET_GRBIT              grbit );

#ifdef JET_UNICODE
#define JetDefragment2 JetDefragment2W
#else
#define JetDefragment2 JetDefragment2A
#endif
#endif // JET_VERSION >= 0x0600

#if ( JET_VERSION < 0x0600 )
#define JetDefragment3A JetDefragment3
#endif

JET_ERR JET_API
JetDefragment3A(
    JET_SESID              sesid,
    JET_PCSTR              szDatabaseName,
    JET_PCSTR          szTableName,
    uint32_t * pcPasses,
    uint32_t * pcSeconds,
    JET_CALLBACK           callback,
    void *                 pvContext,
    JET_GRBIT              grbit );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetDefragment3W(
    JET_SESID              sesid,
    JET_PCWSTR             szDatabaseName,
    JET_PCWSTR         szTableName,
    uint32_t * pcPasses,
    uint32_t * pcSeconds,
    JET_CALLBACK           callback,
    void *                 pvContext,
    JET_GRBIT              grbit );

#ifdef JET_UNICODE
#define JetDefragment3 JetDefragment3W
#else
#define JetDefragment3 JetDefragment3A
#endif
#endif // JET_VERSION >= 0x0600

#endif // JET_VERSION >= 0x0501
#if ( JET_VERSION >= 0x0601 )

JET_ERR JET_API
JetDatabaseScan(
    JET_SESID      sesid,
    JET_DBID       dbid,
    uint32_t * pcSecondsMax,
    uint32_t  cmsecSleep,
    JET_CALLBACK   pfnCallback,
    JET_GRBIT      grbit );

#endif // JET_VERSION >= 0x0601

#if ( JET_VERSION < 0x0600 )
#define JetConvertDDLA JetConvertDDL
#endif

JET_ERR JET_API
JetConvertDDLA(
    JET_SESID                  sesid,
    JET_DBID                   dbid,
    JET_OPDDLCONV              convtyp,
    void * pvData,
    uint32_t              cbData );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetConvertDDLW(
    JET_SESID                  sesid,
    JET_DBID                   dbid,
    JET_OPDDLCONV              convtyp,
    void * pvData,
    uint32_t              cbData );

#ifdef JET_UNICODE
#define JetConvertDDL JetConvertDDLW
#else
#define JetConvertDDL JetConvertDDLA
#endif
#endif // JET_VERSION >= 0x0600

#if ( JET_VERSION >= 0x0501 )
#if ( JET_VERSION < 0x0600 )
#define JetUpgradeDatabaseA JetUpgradeDatabase
#endif

JET_ERR JET_API
JetUpgradeDatabaseA(
    JET_SESID          sesid,
    JET_PCSTR          szDbFileName,
    JET_PCSTR    szSLVFileName,
    const JET_GRBIT    grbit );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetUpgradeDatabaseW(
    JET_SESID          sesid,
    JET_PCWSTR         szDbFileName,
    JET_PCWSTR   szSLVFileName,
    const JET_GRBIT    grbit );

#ifdef JET_UNICODE
#define JetUpgradeDatabase JetUpgradeDatabaseW
#else
#define JetUpgradeDatabase JetUpgradeDatabaseA
#endif
#endif // JET_VERSION >= 0x0600

#endif // JET_VERSION >= 0x0501

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetSetMaxDatabaseSize(
    JET_SESID      sesid,
    JET_DBID       dbid,
    uint32_t  cpg,
    JET_GRBIT      grbit );

JET_ERR JET_API
JetGetMaxDatabaseSize(
    JET_SESID          sesid,
    JET_DBID           dbid,
    uint32_t *   pcpg,
    JET_GRBIT          grbit );

#endif // JET_VERSION >= 0x0600
#if ( JET_VERSION < 0x0600 )
#define JetSetDatabaseSizeA JetSetDatabaseSize
#endif

JET_ERR JET_API
JetSetDatabaseSizeA(
    JET_SESID          sesid,
    JET_PCSTR          szDatabaseName,
    uint32_t      cpg,
    uint32_t *   pcpgReal );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetSetDatabaseSizeW(
    JET_SESID          sesid,
    JET_PCWSTR         szDatabaseName,
    uint32_t      cpg,
    uint32_t *   pcpgReal );

#ifdef JET_UNICODE
#define JetSetDatabaseSize JetSetDatabaseSizeW
#else
#define JetSetDatabaseSize JetSetDatabaseSizeA
#endif
#endif // JET_VERSION >= 0x0600

JET_ERR JET_API
JetGrowDatabase(
    JET_SESID          sesid,
    JET_DBID           dbid,
    uint32_t      cpg,
    uint32_t *    pcpgReal );

#if ( JET_VERSION >= 0x0602 )
JET_ERR JET_API
JetResizeDatabase(
    JET_SESID         sesid,
    JET_DBID          dbid,
    uint32_t     cpgTarget,
    uint32_t *   pcpgActual,
    const JET_GRBIT   grbit );
#endif // JET_VERSION >= 0x0602

JET_ERR JET_API
JetSetSessionContext(
    JET_SESID      sesid,
    JET_API_PTR    ulContext );

JET_ERR JET_API
JetResetSessionContext(
    JET_SESID      sesid );

#if ( JET_VERSION < 0x0600 )
#define JetDBUtilitiesA JetDBUtilities
#endif
JET_ERR JET_API JetDBUtilitiesA( JET_DBUTIL_A *pdbutil );

#if ( JET_VERSION >= 0x0600 )
JET_ERR JET_API JetDBUtilitiesW( JET_DBUTIL_W *pdbutil );
#ifdef JET_UNICODE
#define JetDBUtilities JetDBUtilitiesW
#else
#define JetDBUtilities JetDBUtilitiesA
#endif
#endif // JET_VERSION >= 0x0600
JET_ERR JET_API
JetGotoBookmark(
    JET_SESID                      sesid,
    JET_TABLEID                    tableid,
    void *   pvBookmark,
    uint32_t                  cbBookmark );

#if ( JET_VERSION >= 0x0501 )

JET_ERR JET_API
JetGotoSecondaryIndexBookmark(
    JET_SESID                              sesid,
    JET_TABLEID                            tableid,
    void *       pvSecondaryKey,
    uint32_t                          cbSecondaryKey,
    void *    pvPrimaryBookmark,
    uint32_t                          cbPrimaryBookmark,
    const JET_GRBIT                        grbit );

#endif // JET_VERSION >= 0x0501

JET_ERR JET_API
JetIntersectIndexes(
    JET_SESID                              sesid,
    JET_INDEXRANGE *  rgindexrange,
    uint32_t                          cindexrange,
    JET_RECORDLIST *                    precordlist,
    JET_GRBIT                              grbit );

JET_ERR JET_API
JetComputeStats(
    JET_SESID      sesid,
    JET_TABLEID    tableid );

JET_ERR JET_API
JetOpenTempTable(
    JET_SESID                                  sesid,
    const JET_COLUMNDEF * prgcolumndef,
    uint32_t                              ccolumn,
    JET_GRBIT                                  grbit,
    JET_TABLEID *                             ptableid,
    JET_COLUMNID *          prgcolumnid );

JET_ERR JET_API
JetOpenTempTable2(
    JET_SESID                                  sesid,
    const JET_COLUMNDEF * prgcolumndef,
    uint32_t                              ccolumn,
    uint32_t                              lcid,
    JET_GRBIT                                  grbit,
    JET_TABLEID *                             ptableid,
    JET_COLUMNID *          prgcolumnid );

JET_ERR JET_API
JetOpenTempTable3(
    JET_SESID                                  sesid,
    const JET_COLUMNDEF * prgcolumndef,
    uint32_t                              ccolumn,
    JET_UNICODEINDEX *                     pidxunicode,
    JET_GRBIT                                  grbit,
    JET_TABLEID *                             ptableid,
    JET_COLUMNID *          prgcolumnid );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetOpenTemporaryTable(
    JET_SESID                  sesid,
    JET_OPENTEMPORARYTABLE *   popentemporarytable );

#endif // JET_VERSION >= 0x0600

#if ( JET_VERSION >= 0x0602 )

JET_ERR JET_API
JetOpenTemporaryTable2(
    JET_SESID                  sesid,
    JET_OPENTEMPORARYTABLE2 *  popentemporarytable );

#endif // JET_VERSION >= 0x0602

#if ( JET_VERSION < 0x0600 )
#define JetBackupA JetBackup
#endif

JET_ERR JET_API
JetBackupA(
    JET_PCSTR      szBackupPath,
    JET_GRBIT      grbit,
    JET_PFNSTATUS  pfnStatus );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetBackupW(
    JET_PCWSTR     szBackupPath,
    JET_GRBIT      grbit,
    JET_PFNSTATUS  pfnStatus );

#ifdef JET_UNICODE
#define JetBackup JetBackupW
#else
#define JetBackup JetBackupA
#endif
#endif // JET_VERSION >= 0x0600

#if ( JET_VERSION >= 0x0501 )
#if ( JET_VERSION < 0x0600 )
#define JetBackupInstanceA JetBackupInstance
#endif

JET_ERR JET_API
JetBackupInstanceA(
    JET_INSTANCE   instance,
    JET_PCSTR      szBackupPath,
    JET_GRBIT      grbit,
    JET_PFNSTATUS  pfnStatus );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetBackupInstanceW(
    JET_INSTANCE   instance,
    JET_PCWSTR     szBackupPath,
    JET_GRBIT      grbit,
    JET_PFNSTATUS  pfnStatus );

#ifdef JET_UNICODE
#define JetBackupInstance JetBackupInstanceW
#else
#define JetBackupInstance JetBackupInstanceA
#endif
#endif // JET_VERSION >= 0x0600

#endif // JET_VERSION >= 0x0501

#if ( JET_VERSION < 0x0600 )
#define JetRestoreA JetRestore
#endif

JET_ERR JET_API
JetRestoreA(
    JET_PCSTR      szSource,
    JET_PFNSTATUS  pfn );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetRestoreW(
    JET_PCWSTR     szSource,
    JET_PFNSTATUS  pfn );

#ifdef JET_UNICODE
#define JetRestore JetRestoreW
#else
#define JetRestore JetRestoreA
#endif
#endif // JET_VERSION >= 0x0600

#if ( JET_VERSION < 0x0600 )
#define JetRestore2A JetRestore2
#endif

JET_ERR JET_API
JetRestore2A(
    JET_PCSTR      sz,
    JET_PCSTR  szDest,
    JET_PFNSTATUS  pfn );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetRestore2W(
    JET_PCWSTR     sz,
    JET_PCWSTR szDest,
    JET_PFNSTATUS  pfn );

#ifdef JET_UNICODE
#define JetRestore2 JetRestore2W
#else
#define JetRestore2 JetRestore2A
#endif
#endif // JET_VERSION >= 0x0600

#if ( JET_VERSION >= 0x0501 )
#if ( JET_VERSION < 0x0600 )
#define JetRestoreInstanceA JetRestoreInstance
#endif

JET_ERR JET_API
JetRestoreInstanceA(
    JET_INSTANCE   instance,
    JET_PCSTR      sz,
    JET_PCSTR  szDest,
    JET_PFNSTATUS  pfn );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetRestoreInstanceW(
    JET_INSTANCE   instance,
    JET_PCWSTR     sz,
    JET_PCWSTR szDest,
    JET_PFNSTATUS  pfn );

#ifdef JET_UNICODE
#define JetRestoreInstance JetRestoreInstanceW
#else
#define JetRestoreInstance JetRestoreInstanceA
#endif
#endif // JET_VERSION >= 0x0600

#endif // JET_VERSION >= 0x0501

JET_ERR JET_API
JetSetIndexRange(
    JET_SESID      sesid,
    JET_TABLEID    tableidSrc,
    JET_GRBIT      grbit );

JET_ERR JET_API
JetIndexRecordCount(
    JET_SESID          sesid,
    JET_TABLEID        tableid,
    uint32_t *   pcrec,
    uint32_t      crecMax );
#if ( JET_VERSION >= 0x0A01 )

JET_ERR JET_API
JetIndexRecordCount2(
    JET_SESID              sesid,
    JET_TABLEID            tableid,
    uint64_t *    pcrec,
    uint64_t       crecMax );

#endif // JET_VERSION >= 0x0A01
JET_ERR JET_API
JetRetrieveKey(
    JET_SESID                                      sesid,
    JET_TABLEID                                    tableid,
    void *   pvKey,
    uint32_t                                  cbMax,
    uint32_t *                           pcbActual,
    JET_GRBIT                                      grbit );

JET_ERR JET_API JetBeginExternalBackup(
    JET_GRBIT grbit );

#if ( JET_VERSION >= 0x0501 )

JET_ERR JET_API JetBeginExternalBackupInstance(
    JET_INSTANCE instance,
    JET_GRBIT grbit );

#endif // JET_VERSION >= 0x0501
#if ( JET_VERSION >= 0x0601 )

JET_ERR JET_API JetBeginSurrogateBackup(
    JET_INSTANCE    instance,
    uint32_t       lgenFirst,
    uint32_t       lgenLast,
    JET_GRBIT       grbit );

#endif // JET_VERSION >= 0x0601
#if ( JET_VERSION < 0x0600 )
#define JetGetAttachInfoA JetGetAttachInfo
#endif

JET_ERR JET_API
JetGetAttachInfoA(
#if ( JET_VERSION < 0x0600 )
    void *   pv,
#else
    JET_PSTR szzDatabases,
#endif
    uint32_t                                  cbMax,
    uint32_t *                           pcbActual );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetGetAttachInfoW(
    JET_PWSTR    wszzDatabases,
    uint32_t                                      cbMax,
    uint32_t *                               pcbActual );

#ifdef JET_UNICODE
#define JetGetAttachInfo JetGetAttachInfoW
#else
#define JetGetAttachInfo JetGetAttachInfoA
#endif
#endif // JET_VERSION >= 0x0600

#if ( JET_VERSION >= 0x0501 )
#if ( JET_VERSION < 0x0600 )
#define JetGetAttachInfoInstanceA JetGetAttachInfoInstance
#endif

JET_ERR JET_API
JetGetAttachInfoInstanceA(
    JET_INSTANCE                                   instance,
#if ( JET_VERSION < 0x0600 )
    void *   pv,
#else
    JET_PSTR szzDatabases,
#endif
    uint32_t                                  cbMax,
    uint32_t *                           pcbActual );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetGetAttachInfoInstanceW(
    JET_INSTANCE                                       instance,
    JET_PWSTR    szzDatabases,
    uint32_t                                      cbMax,
    uint32_t *                               pcbActual );

#ifdef JET_UNICODE
#define JetGetAttachInfoInstance JetGetAttachInfoInstanceW
#else
#define JetGetAttachInfoInstance JetGetAttachInfoInstanceA
#endif
#endif // JET_VERSION >= 0x0600

#endif // JET_VERSION >= 0x0501

#if ( JET_VERSION < 0x0600 )
#define JetOpenFileA JetOpenFile
#endif

JET_ERR JET_API
JetOpenFileA(
    JET_PCSTR          szFileName,
    JET_HANDLE *      phfFile,
    uint32_t *   pulFileSizeLow,
    uint32_t *   pulFileSizeHigh );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetOpenFileW(
    JET_PCWSTR         szFileName,
    JET_HANDLE *      phfFile,
    uint32_t *   pulFileSizeLow,
    uint32_t *   pulFileSizeHigh );

#ifdef JET_UNICODE
#define JetOpenFile JetOpenFileW
#else
#define JetOpenFile JetOpenFileA
#endif
#endif

#if ( JET_VERSION >= 0x0501 )
#if ( JET_VERSION < 0x0600 )
#define JetOpenFileInstanceA JetOpenFileInstance
#endif

JET_ERR JET_API
JetOpenFileInstanceA(
    JET_INSTANCE       instance,
    JET_PCSTR          szFileName,
    JET_HANDLE *      phfFile,
    uint32_t *   pulFileSizeLow,
    uint32_t *   pulFileSizeHigh );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetOpenFileInstanceW(
    JET_INSTANCE       instance,
    JET_PCWSTR         szFileName,
    JET_HANDLE *      phfFile,
    uint32_t *   pulFileSizeLow,
    uint32_t *   pulFileSizeHigh );

#ifdef JET_UNICODE
#define JetOpenFileInstance JetOpenFileInstanceW
#else
#define JetOpenFileInstance JetOpenFileInstanceA
#endif
#endif // JET_VERSION >= 0x0600

#endif // JET_VERSION >= 0x0501
#if ( JET_VERSION < 0x0600 )
#define JetOpenFileSectionInstanceA JetOpenFileSectionInstance
#endif

JET_ERR JET_API
JetOpenFileSectionInstanceA(
    JET_INSTANCE       instance,
    JET_PSTR           szFile,
    JET_HANDLE *      phFile,
    int32_t               iSection,
    int32_t               cSections,
    uint64_t   ibRead,
    uint32_t *   pulSectionSizeLow,
    int32_t *            plSectionSizeHigh );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetOpenFileSectionInstanceW(
    JET_INSTANCE       instance,
    JET_PWSTR          szFile,
    JET_HANDLE *      phFile,
    int32_t               iSection,
    int32_t               cSections,
    uint64_t   ibRead,
    uint32_t *   pulSectionSizeLow,
    int32_t *            plSectionSizeHigh );

#ifdef JET_UNICODE
#define JetOpenFileSectionInstance JetOpenFileSectionInstanceW
#else
#define JetOpenFileSectionInstance JetOpenFileSectionInstanceA
#endif
#endif // JET_VERSION >= 0x0600
JET_ERR JET_API
JetReadFile(
    JET_HANDLE                             hfFile,
    void *  pv,
    uint32_t                          cb,
    uint32_t *                   pcbActual );

#if ( JET_VERSION >= 0x0501 )

JET_ERR JET_API
JetReadFileInstance(
    JET_INSTANCE                           instance,
    JET_HANDLE                             hfFile,
    void *  pv,
    uint32_t                          cb,
    uint32_t *                   pcbActual );

#endif // JET_VERSION >= 0x0501

JET_ERR JET_API
JetCloseFile(
    JET_HANDLE     hfFile );

#if ( JET_VERSION >= 0x0501 )

JET_ERR JET_API
JetCloseFileInstance(
    JET_INSTANCE   instance,
    JET_HANDLE     hfFile );

#endif // JET_VERSION >= 0x0501

#if ( JET_VERSION < 0x0600 )
#define JetGetLogInfoA JetGetLogInfo
#endif

JET_ERR JET_API
JetGetLogInfoA(
#if ( JET_VERSION < 0x0600 )
    void *   pv,
#else
    JET_PSTR szzLogs,
#endif
    uint32_t                                  cbMax,
    uint32_t *                           pcbActual );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetGetLogInfoW(
        JET_PWSTR    szzLogs,
        uint32_t                                      cbMax,
        uint32_t *                               pcbActual );

#ifdef JET_UNICODE
#define JetGetLogInfo JetGetLogInfoW
#else
#define JetGetLogInfo JetGetLogInfoA
#endif
#endif // JET_VERSION >= 0x0600

#if ( JET_VERSION >= 0x0501 )
#if ( JET_VERSION < 0x0600 )
#define JetGetLogInfoInstanceA JetGetLogInfoInstance
#endif

JET_ERR JET_API
JetGetLogInfoInstanceA(
    JET_INSTANCE                                   instance,
#if ( JET_VERSION < 0x0600 )
    void *   pv,
#else
    JET_PSTR szzLogs,
#endif
    uint32_t                                  cbMax,
    uint32_t *                           pcbActual );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetGetLogInfoInstanceW(
    JET_INSTANCE                                       instance,
    JET_PWSTR    wszzLogs,
    uint32_t                                      cbMax,
    uint32_t *                               pcbActual );

#ifdef JET_UNICODE
#define JetGetLogInfoInstance JetGetLogInfoInstanceW
#else
#define JetGetLogInfoInstance JetGetLogInfoInstanceA
#endif
#endif // JET_VERSION >= 0x0600

// JET_LOGINFO is used by JetGetLogInfoInstance2(), which is internal.
// But it's also used by JetExternalRestore2().
#define JET_BASE_NAME_LENGTH    3
typedef struct
{
    uint32_t   cbSize;
    uint32_t   ulGenLow;
    uint32_t   ulGenHigh;
    char            szBaseName[ JET_BASE_NAME_LENGTH + 1 ];
} JET_LOGINFO_A;

typedef struct
{
    uint32_t   cbSize;
    uint32_t   ulGenLow;
    uint32_t   ulGenHigh;
    char16_t           szBaseName[ JET_BASE_NAME_LENGTH + 1 ];
} JET_LOGINFO_W;

#ifdef JET_UNICODE
typedef JET_LOGINFO_W JET_LOGINFO;
#else
typedef JET_LOGINFO_A JET_LOGINFO;
#endif

#if ( JET_VERSION < 0x0600 )
#define JetGetLogInfoInstance2A JetGetLogInfoInstance2
#endif

JET_ERR JET_API
JetGetLogInfoInstance2A(
    JET_INSTANCE                                   instance,
#if ( JET_VERSION < 0x0600 )
    void *   pv,
#else
    JET_PSTR szzLogs,
#endif
    uint32_t                                  cbMax,
    uint32_t *                           pcbActual,
    JET_LOGINFO_A *                         pLogInfo );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetGetLogInfoInstance2W(
    JET_INSTANCE                                       instance,
    JET_PWSTR    wszzLogs,
    uint32_t                                      cbMax,
    uint32_t *                               pcbActual,
    JET_LOGINFO_W *                             pLogInfo );

#ifdef JET_UNICODE
#define JetGetLogInfoInstance2 JetGetLogInfoInstance2W
#else
#define JetGetLogInfoInstance2 JetGetLogInfoInstance2A
#endif
#endif // JET_VERSION >= 0x0600

#if ( JET_VERSION < 0x0600 )
#define JetGetTruncateLogInfoInstanceA JetGetTruncateLogInfoInstance
#endif

JET_ERR JET_API
JetGetTruncateLogInfoInstanceA(
    JET_INSTANCE                                   instance,
#if ( JET_VERSION < 0x0600 )
    void *   pv,
#else
    JET_PSTR szzLogs,
#endif
    uint32_t                                  cbMax,
    uint32_t *                           pcbActual );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetGetTruncateLogInfoInstanceW(
    JET_INSTANCE                                       instance,
    JET_PWSTR    wszzLogs,
    uint32_t                                      cbMax,
    uint32_t *                               pcbActual );

#ifdef JET_UNICODE
#define JetGetTruncateLogInfoInstance JetGetTruncateLogInfoInstanceW
#else
#define JetGetTruncateLogInfoInstance JetGetTruncateLogInfoInstanceA
#endif
#endif // JET_VERSION >= 0x0600

#endif // JET_VERSION >= 0x0501

JET_ERR JET_API JetTruncateLog( void );

#if ( JET_VERSION >= 0x0501 )

JET_ERR JET_API
JetTruncateLogInstance(
    JET_INSTANCE   instance );

#endif // JET_VERSION >= 0x0501

JET_ERR JET_API JetEndExternalBackup( void );

#if ( JET_VERSION >= 0x0501 )

JET_ERR JET_API
JetEndExternalBackupInstance(
    JET_INSTANCE   instance );

JET_ERR JET_API
JetEndExternalBackupInstance2(
    JET_INSTANCE   instance,
    JET_GRBIT      grbit );

#endif // JET_VERSION >= 0x0501
#if ( JET_VERSION >= 0x0601 )
JET_ERR JET_API JetEndSurrogateBackup(
    JET_INSTANCE    instance,
    JET_GRBIT       grbit );

#endif // JET_VERSION >= 0x0601

#if ( JET_VERSION < 0x0600 )
#define JetExternalRestoreA JetExternalRestore
#endif

JET_ERR JET_API
JetExternalRestoreA(
    JET_PSTR                                   szCheckpointFilePath,
    JET_PSTR                                   szLogPath,
    JET_RSTMAP_A *    rgrstmap,
    int32_t                                       crstfilemap,
    JET_PSTR                                   szBackupLogPath,
    int32_t                                       genLow,
    int32_t                                       genHigh,
    JET_PFNSTATUS                              pfn );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetExternalRestoreW(
    JET_PWSTR                                  szCheckpointFilePath,
    JET_PWSTR                                  szLogPath,
    JET_RSTMAP_W *    rgrstmap,
    int32_t                                       crstfilemap,
    JET_PWSTR                                  szBackupLogPath,
    int32_t                                       genLow,
    int32_t                                       genHigh,
    JET_PFNSTATUS                              pfn );

#ifdef JET_UNICODE
#define JetExternalRestore JetExternalRestoreW
#else
#define JetExternalRestore JetExternalRestoreA
#endif
#endif // JET_VERSION >= 0x0600

#if JET_VERSION >= 0x0501
#if ( JET_VERSION < 0x0600 )
#define JetExternalRestore2A JetExternalRestore2
#endif

JET_ERR JET_API
JetExternalRestore2A(
    JET_PSTR                                   szCheckpointFilePath,
    JET_PSTR                                   szLogPath,
    JET_RSTMAP_A *    rgrstmap,
    int32_t                                       crstfilemap,
    JET_PSTR                                   szBackupLogPath,
    JET_LOGINFO_A *                         pLogInfo,
    JET_PSTR                               szTargetInstanceName,
    JET_PSTR                               szTargetInstanceLogPath,
    JET_PSTR                               szTargetInstanceCheckpointPath,
    JET_PFNSTATUS                              pfn );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetExternalRestore2W(
    JET_PWSTR                                  szCheckpointFilePath,
    JET_PWSTR                                  szLogPath,
    JET_RSTMAP_W *    rgrstmap,
    int32_t                                       crstfilemap,
    JET_PWSTR                                  szBackupLogPath,
    JET_LOGINFO_W *                         pLogInfo,
    JET_PWSTR                              szTargetInstanceName,
    JET_PWSTR                              szTargetInstanceLogPath,
    JET_PWSTR                              szTargetInstanceCheckpointPath,
    JET_PFNSTATUS                              pfn );

#ifdef JET_UNICODE
#define JetExternalRestore2 JetExternalRestore2W
#else
#define JetExternalRestore2 JetExternalRestore2A
#endif
#endif // JET_VERSION >= 0x0600
#if ( JET_VERSION < 0x0600 )
#define JetSnapshotStartA JetSnapshotStart
#endif

JET_ERR JET_API
JetSnapshotStartA(
    JET_INSTANCE   instance,
    JET_PSTR       szDatabases,
    JET_GRBIT      grbit );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetSnapshotStartW(
    JET_INSTANCE   instance,
    JET_PWSTR      szDatabases,
    JET_GRBIT      grbit );

#ifdef JET_UNICODE
#define JetSnapshotStart JetSnapshotStartW
#else
#define JetSnapshotStart JetSnapshotStartA
#endif
#endif // JET_VERSION >= 0x0600

JET_ERR JET_API
JetSnapshotStop(
    JET_INSTANCE   instance,
    JET_GRBIT      grbit);

JET_ERR JET_API
JetRegisterCallback(
    JET_SESID      sesid,
    JET_TABLEID    tableid,
    JET_CBTYP      cbtyp,
    JET_CALLBACK   pCallback,
    void *     pvContext,
    JET_HANDLE *   phCallbackId );

JET_ERR JET_API
JetUnregisterCallback(
    JET_SESID      sesid,
    JET_TABLEID    tableid,
    JET_CBTYP      cbtyp,
    JET_HANDLE     hCallbackId );

typedef struct _JET_INSTANCE_INFO_A
{
    JET_INSTANCE        hInstanceId;
    char *              szInstanceName;

    JET_API_PTR         cDatabases;
    char **             szDatabaseFileName;
    char **             szDatabaseDisplayName;
    char **             szDatabaseSLVFileName_Obsolete;
} JET_INSTANCE_INFO_A;

typedef struct _JET_INSTANCE_INFO_W
{
    JET_INSTANCE        hInstanceId;
    char16_t *             szInstanceName;

    JET_API_PTR         cDatabases;
    char16_t **            szDatabaseFileName;
    char16_t **            szDatabaseDisplayName;
    char16_t **            szDatabaseSLVFileName_Obsolete;
} JET_INSTANCE_INFO_W;

#ifdef JET_UNICODE
typedef JET_INSTANCE_INFO_W JET_INSTANCE_INFO;
#else
typedef JET_INSTANCE_INFO_A JET_INSTANCE_INFO;
#endif

#if ( JET_VERSION < 0x0600 )
#define JetGetInstanceInfoA JetGetInstanceInfo
#endif

JET_ERR JET_API
JetGetInstanceInfoA(
    uint32_t *                                           pcInstanceInfo,
    JET_INSTANCE_INFO_A **    paInstanceInfo );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetGetInstanceInfoW(
    uint32_t *                                           pcInstanceInfo,
    JET_INSTANCE_INFO_W **    paInstanceInfo );

#ifdef JET_UNICODE
#define JetGetInstanceInfo JetGetInstanceInfoW
#else
#define JetGetInstanceInfo JetGetInstanceInfoA
#endif
#endif // JET_VERSION >= 0x0600

JET_ERR JET_API
JetFreeBuffer(
    char *    pbBuf );

JET_ERR JET_API
JetSetLS(
    JET_SESID      sesid,
    JET_TABLEID    tableid,
    JET_LS         ls,
    JET_GRBIT      grbit );

JET_ERR JET_API
JetGetLS(
    JET_SESID      sesid,
    JET_TABLEID    tableid,
    JET_LS *      pls,
    JET_GRBIT      grbit );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetTracing(
    const JET_TRACEOP  traceop,
    const JET_TRACETAG tracetag,
    const JET_API_PTR  ul );

#endif // JET_VERSION >= 0x0600
typedef JET_API_PTR JET_OSSNAPID;   /* Snapshot Session Identifier */

JET_ERR JET_API
JetOSSnapshotPrepare(
    JET_OSSNAPID *    psnapId,
    const JET_GRBIT    grbit );
#if ( JET_VERSION >= 0x0600 )
JET_ERR JET_API
JetOSSnapshotPrepareInstance(
    JET_OSSNAPID       snapId,
    JET_INSTANCE       instance,
    const JET_GRBIT    grbit );

#endif // JET_VERSION >= 0x0600

#if ( JET_VERSION < 0x0600 )
#define JetOSSnapshotFreezeA JetOSSnapshotFreeze
#endif

JET_ERR JET_API
JetOSSnapshotFreezeA(
    const JET_OSSNAPID                                         snapId,
    uint32_t *                                           pcInstanceInfo,
    JET_INSTANCE_INFO_A **    paInstanceInfo,
    const JET_GRBIT                                            grbit );

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetOSSnapshotFreezeW(
    const JET_OSSNAPID                                         snapId,
    uint32_t *                                           pcInstanceInfo,
    JET_INSTANCE_INFO_W **    paInstanceInfo,
    const JET_GRBIT                                            grbit );

#ifdef JET_UNICODE
#define JetOSSnapshotFreeze JetOSSnapshotFreezeW
#else
#define JetOSSnapshotFreeze JetOSSnapshotFreezeA
#endif
#endif // JET_VERSION >= 0x0600

JET_ERR JET_API
JetOSSnapshotThaw(
    const JET_OSSNAPID snapId,
    const JET_GRBIT    grbit );

#endif // JET_VERSION >= 0x0501

#if ( JET_VERSION >= 0x0502 )

JET_ERR JET_API
JetOSSnapshotAbort(
    const JET_OSSNAPID snapId,
    const JET_GRBIT    grbit );

#endif // JET_VERSION >= 0x0502

#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetOSSnapshotTruncateLog(
    const JET_OSSNAPID snapId,
    const JET_GRBIT    grbit );

JET_ERR JET_API
JetOSSnapshotTruncateLogInstance(
    const JET_OSSNAPID snapId,
    JET_INSTANCE       instance,
    const JET_GRBIT    grbit );

#if ( JET_VERSION < 0x0600 )
#define JetOSSnapshotGetFreezeInfoA JetOSSnapshotGetFreezeInfo
#endif

JET_ERR JET_API
JetOSSnapshotGetFreezeInfoA(
    const JET_OSSNAPID                                         snapId,
    uint32_t *                                           pcInstanceInfo,
    JET_INSTANCE_INFO_A **    paInstanceInfo,
    const JET_GRBIT                                            grbit );

JET_ERR JET_API
JetOSSnapshotGetFreezeInfoW(
    const JET_OSSNAPID                                         snapId,
    uint32_t *                                           pcInstanceInfo,
    JET_INSTANCE_INFO_W **    paInstanceInfo,
    const JET_GRBIT                                            grbit );

#ifdef JET_UNICODE
#define JetOSSnapshotGetFreezeInfo JetOSSnapshotGetFreezeInfoW
#else
#define JetOSSnapshotGetFreezeInfo JetOSSnapshotGetFreezeInfoA
#endif

JET_ERR JET_API
JetOSSnapshotEnd(
    const JET_OSSNAPID snapId,
    const JET_GRBIT    grbit );

#endif // JET_VERSION >= 0x0600
#if ( JET_VERSION >= 0x0600 )

JET_ERR JET_API
JetGetPageInfo(
    void * const         pvPages,        //  raw page data
    uint32_t                          cbData,         //  size of raw page data
    JET_PAGEINFO *  rgPageInfo,     //  array of pageinfo structures
    uint32_t                          cbPageInfo,     //  length of buffer for pageinfo array
    JET_GRBIT                              grbit,          //  options
    uint32_t                          ulInfoLevel );  //  info level

#endif // JET_VERSION >= 0x0600

#if ( JET_VERSION >= 0x0601 )

JET_ERR JET_API
JetGetPageInfo2(
    void * const         pvPages,        //  raw page data
    uint32_t                          cbData,         //  size of raw page data
    void * const    rgPageInfo,     //  array of pageinfo structures
    uint32_t                          cbPageInfo,     //  length of buffer for pageinfo array
    JET_GRBIT                              grbit,          //  options
    uint32_t                          ulInfoLevel );  //  info level

JET_ERR JET_API
JetGetDatabasePages(
    JET_SESID                              sesid,
    JET_DBID                               dbid,
    uint32_t                          pgnoStart,
    uint32_t                          cpg,
    void *  pv,
    uint32_t                          cb,
    uint32_t *                       pcbActual,
    JET_GRBIT                              grbit );

// JetPatchDatabasePages works on an offline database and updates
// the header. This API operates on an online database and doesn't
// update the header.

#if ( JET_VERSION >= 0x602 )

JET_ERR JET_API
JetOnlinePatchDatabasePage(
    JET_SESID                              sesid,
    JET_DBID                               dbid,
    uint32_t                          pgno,
    const void *          pvToken,
    uint32_t                          cbToken,
    const void *            pvData,
    uint32_t                          cbData,
    JET_GRBIT                              grbit );

#endif // JET_VERSION >= 0x602

JET_ERR JET_API
JetRemoveLogfileA(
    JET_PCSTR szDatabase,
    JET_PCSTR szLogfile,
    JET_GRBIT grbit );

JET_ERR JET_API
JetRemoveLogfileW(
    JET_PCWSTR wszDatabase,
    JET_PCWSTR wszLogfile,
    JET_GRBIT grbit );

#ifdef JET_UNICODE
#define JetRemoveLogfile JetRemoveLogfileW
#else
#define JetRemoveLogfile JetRemoveLogfileA
#endif

JET_ERR JET_API
JetBeginDatabaseIncrementalReseedA(
    JET_INSTANCE   instance,
    JET_PCSTR      szDatabase,
    uint32_t  genFirstDivergedLog,
    JET_GRBIT      grbit );

JET_ERR JET_API
JetBeginDatabaseIncrementalReseedW(
    JET_INSTANCE   instance,
    JET_PCWSTR     szDatabase,
    uint32_t  genFirstDivergedLog,
    JET_GRBIT      grbit );

#ifdef JET_UNICODE
#define JetBeginDatabaseIncrementalReseed JetBeginDatabaseIncrementalReseedW
#else
#define JetBeginDatabaseIncrementalReseed JetBeginDatabaseIncrementalReseedA
#endif

JET_ERR JET_API
JetEndDatabaseIncrementalReseedA(
    JET_INSTANCE   instance,
    JET_PCSTR      szDatabase,
    uint32_t  genMinRequired,
    uint32_t  genFirstDivergedLog,
    uint32_t  genMaxRequired,
    JET_GRBIT      grbit );

JET_ERR JET_API
JetEndDatabaseIncrementalReseedW(
    JET_INSTANCE   instance,
    JET_PCWSTR     szDatabase,
    uint32_t  genMinRequired,
    uint32_t  genFirstDivergedLog,
    uint32_t  genMaxRequired,
    JET_GRBIT      grbit );

#ifdef JET_UNICODE
#define JetEndDatabaseIncrementalReseed JetEndDatabaseIncrementalReseedW
#else
#define JetEndDatabaseIncrementalReseed JetEndDatabaseIncrementalReseedA
#endif

JET_ERR JET_API
JetPatchDatabasePagesA(
    JET_INSTANCE               instance,
    JET_PCSTR                  szDatabase,
    uint32_t              pgnoStart,
    uint32_t              cpg,
    const void * pv,
    uint32_t              cb,
    JET_GRBIT                  grbit );

JET_ERR JET_API
JetPatchDatabasePagesW(
    JET_INSTANCE               instance,
    JET_PCWSTR                 szDatabase,
    uint32_t              pgnoStart,
    uint32_t              cpg,
    const void * pv,
    uint32_t              cb,
    JET_GRBIT                  grbit );

#ifdef JET_UNICODE
#define JetPatchDatabasePages JetPatchDatabasePagesW
#else
#define JetPatchDatabasePages JetPatchDatabasePagesA
#endif

#endif // JET_VERSION >= 0x0601

#if ( JET_VERSION >= 0x0A01 )

JET_ERR JET_API
JetGetRBSFileInfoA(
    JET_PCSTR                  szRBSFileName,
    void *  pvResult,
    uint32_t              cbMax,
    uint32_t              InfoLevel );

JET_ERR JET_API
JetGetRBSFileInfoW(
    JET_PCWSTR                 szRBSFileName,
    void *  pvResult,
    uint32_t              cbMax,
    uint32_t              InfoLevel );

#ifdef JET_UNICODE
#define JetGetRBSFileInfo JetGetRBSFileInfoW
#else
#define JetGetRBSFileInfo JetGetRBSFileInfoA
#endif

JET_ERR JET_API
JetRBSPrepareRevert(
    JET_INSTANCE    instance,
    JET_LOGTIME     jltRevertExpected,
    int32_t            cpgCache,
    JET_GRBIT       grbit,
    JET_LOGTIME*    pjltRevertActual );

JET_ERR JET_API
JetRBSExecuteRevert(
    JET_INSTANCE    instance,
    JET_GRBIT       grbit,
    JET_RBSREVERTINFOMISC*  prbsrevertinfomisc );

JET_ERR JET_API
JetRBSCancelRevert(
    JET_INSTANCE    instance );

#endif // JET_VERSION >= 0x0A01
#if ( JET_VERSION >= 0x0601 )

//  Options for JetConfigureProcessForCrashDump

#define JET_bitDumpMinimum                      0x00000001
//  dump minimum includes cache minimum
#define JET_bitDumpMaximum                      0x00000002
//  dump maximum includes dump minimum
//  dump maximum includes cache maximum
#define JET_bitDumpCacheMinimum                 0x00000004
//  cache minimum includes pages that are latched
//  cache minimum includes pages that are used for memory
//  cache minimum includes pages that are flagged with errors
#define JET_bitDumpCacheMaximum                 0x00000008
//  cache maximum includes cache minimum
//  cache maximum includes the entire cache image
#define JET_bitDumpCacheIncludeDirtyPages       0x00000010
//  dump includes pages that are modified
#define JET_bitDumpCacheIncludeCachedPages      0x00000020
//  dump includes pages that contain valid data
#define JET_bitDumpCacheIncludeCorruptedPages   0x00000040
//  dump includes pages that are corrupted (expensive to compute)
#define JET_bitDumpCacheNoDecommit              0x00000080
//  do not decommit any pages not intending to include in crash dump
#define JET_bitDumpUnitTest                     0x80000000

//  Looks at all pages for internal testing purposes
JET_ERR JET_API
JetConfigureProcessForCrashDump(
    const JET_GRBIT grbit );

#endif // JET_VERSION >= 0x0601

#if ( JET_VERSION >= 0x0601 )
//  Opcodes for JetTestHook
typedef enum
{
    opTestHookUnitTests,                                //  takes a char*
    opTestHookTestInjection,                            //  takes a JET_TESTHOOKTESTINJECTION*
    opTestHookHookNtQueryInformationProcess,            //  takes a JET_TESTHOOKAPIHOOKING*
    opTestHookHookNtQuerySystemInformation,             //  takes a JET_TESTHOOKAPIHOOKING*
    opTestHookHookGlobalMemoryStatus,                   //  takes a JET_TESTHOOKAPIHOOKING*
    opTestHookSetNegativeTesting,                       //  takes a ulong*
    opTestHookResetNegativeTesting,                     //  takes a ulong*
    opTestHookThrowError,                               //  takes a JET_ERR
    opTestHookThrowAssert,                              //  takes nothing
    opTestHookUnitTests2,                               //  takes a JET_TESTHOOKUNITTEST2
    opTestHookEnforceContextFail,                       //  takes a JET_TESTHOOKAPIHOOKING*, replaces g_pfnEnforceContextFail with user-supplied callback
    opTestHookGetBFLowMemoryCallback,                   //  return pointer to function
    opTestHookTraceTestMarker,                          //  traces a test marker ETW/OSTrace event
    opTestHookGetCheckpointDepth,                       //  return value of the current checkpoint depth
    opTestHookGetOLD2Status,                            //  return whether we are processing B+ tree defrag tasks in the defrag manager
    opTestHookGetEngineTickNow,                         //  returns the TickOSTimeCurrent() from inside the engine
    opTestHookSetEngineTickTime,                        //  takes a JET_TESTHOOKTIMEINJECTION structure
    opTestHookCacheQuery,                               //  takes a JET_TESTHOOKCACHEQUERY*
    opTestHookEvictCache,                               //  takes a JET_TESTHOOKEVICTCACHE*
    opTestHookCorrupt,                                  //  takes a JET_TESTHOOKCORRUPT*
    opTestHookEnableAutoIncDeprecated,                  //  REMOVED - takes a ulong*
    opTestHookSetErrorTrap,                             //  takes a ulong*, set g_errTrap
    opTestHookGetTablePgnoFDP,                          //  takes a JET_TABLEID in, returns a pgno
    opTestHookAlterDatabaseFileHeader,                  //  takes a JET_TESTHOOKALTERDBFILEHDR.
    opTestHookGetLogTip,                                //  returns the current log tip LGPOS as a 64 bit integer
    opTestHookBlockCacheTestEnabled,                    //  takes a long* indicating if the block cache should be forced on for test purposes
} TESTHOOK_OP;

//  This is the list of "forgiveable" sins that test can commit against ESE:
//  you must merely ask for forgiveness by setting the ESE\Debug\Negative Testing
//  registry value (keep in mind decimal, not hex) -or
//  via opTestHookSetNegativeTesting/ResetNegativeTesting

//  The integer values of the appropriate sins are here:

enum
{
    fDeletingLogFiles                       = 0x00000001,
    fCorruptingLogFiles                     = 0x00000002,
    fLockingCheckpointFile                  = 0x00000004,
    fCorruptingDbHeaders                    = 0x00000008,
    fCorruptingPagePgnos                    = 0x00000010,  // but checksum is ok.
    fLeakStuff                              = 0x00000020,
    fCorruptingWithLostFlush                = 0x00000040,
    fDisableTimeoutDeadlockDetection        = 0x00000080,
    fCorruptingPages                        = 0x00000100,
    fDiskIOError                            = 0x00000200,  // Injecting ERROR_IO_DEVICE (results in Jet_errDiskIO)
    fInvalidAPIUsage                        = 0x00000400,  // invalid usage of the JET API
    fInvalidUsage                           = 0x00000800,  // invalid usage of some sub-component during unit testing
    fCorruptingPageLogically                = 0x00001000,  // but checksum (and perhaps structure / consistency) is ok.
    fOutOfMemory                            = 0x00002000,
    fLeakingUnflushedIos                    = 0x00004000,  // for internal tests that do not flush file buffers before deleting the pfapi.
    fHangingIOs                             = 0x00008000,  // Simulate hung IOs
    fCorruptingWithLostFlushWithinReqRange  = 0x00010000,  // Used to disable specific asserts if lost flushes are within required range.
    fStrictIoPerfTesting                    = 0x00020000,  // Use this to turn off any fault injection or test randomization that causes unnecessary / repairative IOs or adverse IO performance effects.
    fDisableAssertReqRangeConsistentLgpos   = 0x00040000,  // Use this to turn off asserts related to setting lgposOB0 or lgposModify below min required.
};

typedef struct tagJET_TESTHOOKUNITTEST2
{
    uint32_t       cbStruct;       //  size of this structure
    char *              szTestName;     //  test name / test wildcard
    JET_DBID            dbidTestOn;     //  database to perform the internal tests against
} JET_TESTHOOKUNITTEST2;

//  Flags to be passed for JET_TestInjectHang type test injection
#define bitHangInjectSleep          0x40000000
#define mskHangInjectOptions        ( bitHangInjectSleep | 0xFFFF0000 )

//  Type of context that is being passed to JET_TESTHOOKTESTINJECTION, when opTestHookTestInjection
//  is used.
typedef enum
{
    JET_TestInjectInvalid = 0,
    JET_TestInjectMin,
    JET_TestInjectFault = JET_TestInjectMin,
    JET_TestInjectConfigOverride,
    JET_TestInjectHang,
    JET_TestInjectMax
} JET_TESTINJECTIONTYPE;

//  Options for JetTestHook( opTestHookTestInjection / JET_TESTHOOKTESTINJECTION )

#define JET_bitInjectionProbabilityPct          0x00000001  //  default, with 100% ulProbability.
#define JET_bitInjectionProbabilityCount        0x00000002  //  one shot on nth (ulProbability) evaluation.
#define JET_bitInjectionProbabilityPermanent    0x00000004  //  same as one shot, but permanent after fire (must be OR'd with JET_bitInjectionProbabilityCount).
#define JET_bitInjectionProbabilityFailUntil    0x00000008  //  same as one shot, but fails until fire (must be OR'd with JET_bitInjectionProbabilityCount).
#define JET_bitInjectionProbabilitySuppress     0x40000000  //  removes injection temporarily
#define JET_bitInjectionProbabilityCleanup      0x80000000  //  removes injection entry for the ID.

//  pv struct for opTestHookTestInjection
typedef struct tagJET_TESTHOOKTESTINJECTION
{
    uint32_t           cbStruct;
    uint32_t           ulID;
    JET_API_PTR             pv;
    JET_TESTINJECTIONTYPE   type;
    uint32_t           ulProbability;
    JET_GRBIT               grbit;
} JET_TESTHOOKTESTINJECTION;

//  pv struct for opTestHookHookNtQueryInformationProcess, opTestHookHookNtQuerySystemInformation
//  and opTestHookHookGlobalMemoryStatus
typedef struct tagJET_TESTHOOKAPIHOOKING
{
    uint32_t   cbStruct;
    const void *    pfnOld;
    const void *    pfnNew;
} JET_TESTHOOKAPIHOOKING;

//  pv struct for opTestHookTraceTestMarker
typedef struct tagJET_TESTHOOKTRACETESTMARKER
{
    uint32_t       cbStruct;
    const char *        szAnnotation;
    uint64_t    qwMarkerID;
} JET_TESTHOOKTRACETESTMARKER;

//  pv struct for opTestHookSetEngineTickTime
typedef struct tagJET_TESTHOOKTIMEINJECTION
{
    uint32_t       cbStruct;
    uint32_t       tickNow;
    uint32_t       eTimeInjWrapMode;
    uint32_t       dtickTimeInjWrapOffset;
    uint32_t       dtickTimeInjAccelerant;
} JET_TESTHOOKTIMEINJECTION;

//  pv struct for opTestHookCacheQuery
typedef struct tagJET_TESTHOOKCACHEQUERY
{
    uint32_t       cbStruct;

    //  in args
    int32_t                cCacheQuery;
    char **             rgszCacheQuery;

    //  out arg
    void *              pvOut;
} JET_TESTHOOKCACHEQUERY;

#define JET_bitTestHookEvictDataByPgno  0x00000001      //  Specifies that we are evicting data from the database cache, specified by pgno.

//  pv struct for opTestHookEvictCache
typedef struct tagJET_TESTHOOKEVICTCACHE
{
    uint32_t           cbStruct;
    JET_API_PTR         ulTargetContext;        //  For ..EvictDataByPgno = JET_DBID
    JET_API_PTR         ulTargetData;           //  For ..EvictDataByPgno = PageNumber/pgno
    JET_GRBIT           grbit;
} JET_TESTHOOKEVICTCACHE;

//  args for opTestHookCorrupt

#define JET_bitTestHookCorruptDatabaseFile  0x80000000      //  Use .CorruptDatabaseFile
#define JET_bitTestHookCorruptDatabasePageImage 0x40000000      //  Use .CorruptDatabasePageImage
#define JET_mskTestHookCorruptFileType      ( JET_bitTestHookCorruptDatabaseFile | JET_bitTestHookCorruptDatabasePageImage )

#define JET_bitTestHookCorruptPageChksumRand    0x00000001      //  usable with ...CorruptDatabase*
#define JET_bitTestHookCorruptPageChksumSafe    0x00000002      //  usable with ...CorruptDatabase*
#define JET_bitTestHookCorruptPageSingleFld 0x00000004      //  usable with ...CorruptDatabase*
#define JET_bitTestHookCorruptPageRemoveNode    0x00000008      //  usable with ...CorruptDatabase*
#define JET_bitTestHookCorruptPageDbtimeDelta   0x00000010      //  usable with ...CorruptDatabase*
#define JET_bitTestHookCorruptNodePrefix    0x00000020      //  usable with ...CorruptDatabase*
#define JET_bitTestHookCorruptNodeSuffix    0x00000040      //  usable with ...CorruptDatabase*

#define JET_mskTestHookCorruptDataType      ( JET_bitTestHookCorruptPageChksumRand | JET_bitTestHookCorruptPageChksumSafe | JET_bitTestHookCorruptPageSingleFld | JET_bitTestHookCorruptPageRemoveNode | JET_bitTestHookCorruptPageDbtimeDelta )

//  Following usable only with JET_bitTestHookCorruptNodePrefix | JET_bitTestHookCorruptNodeSuffix
#define JET_bitTestHookCorruptSizeLargerThanNode    0x00010000  //  adds a cb that is just larger than the node / line.cb size.
#define JET_bitTestHookCorruptSizeShortWrapSmall    0x00020000  //  adds 0x8000 to the cb.
#define JET_bitTestHookCorruptSizeShortWrapLarge    0x00040000  //  adds 0xF000 to the cb.

#define JET_mskTestHookCorruptSpecific          0x00FF0000  //  reserved for bits of a specific Node and someday Page Corruption types.

#define JET_bitTestHookCorruptLeaveChecksum 0x01000000      //  usable with ...CorruptDatabase*

#define JET_pgnoTestHookCorruptRandom       0xFFFFFFFFFFFFFFFFLL    //  Specifies to select the page number randomly

typedef struct tagJET_TESTHOOKCORRUPT
{
    uint32_t           cbStruct;
    JET_GRBIT           grbit;

    union
    {
        // ESE tries to pack as densely as possible, which can result in misalignment.
#pragma pack(push, 8)
        struct // CorruptDatabaseFile
        {
            JET_PWSTR   wszDatabaseFilePath;        //  Name of the database file
            int64_t     pgnoTarget;         //  Page number target, or JET_pgnoTestHookCorruptRandom
            int64_t     iSubTarget;         //  Depends upon the JET_bitTestHookCorruptPage* type.
        } CorruptDatabaseFile;
#pragma pack(pop)

        struct // CorruptDatabasePageImage
        {
            JET_API_PTR pbPageImageTarget;      //  Pointer to the page image to corrupt
            uint32_t   cbPageImage;
            int64_t     pgnoTarget;         //  Page number target (note: this may not seem like it should be required, but it is b/c 4 KB pages xor this into the checksum)
            int64_t     iSubTarget;         //  Depends upon the JET_bitTestHookCorruptPage* type.
        } CorruptDatabasePageImage;
    };

} JET_TESTHOOKCORRUPT;

//  args for opTestHookAlterDatabaseFileHeader / JET_TESTHOOKALTERDBFILEHDR

#define JET_bitAlterDbFileHdrAddField                       0x1     //  Makes it so the pbField (but only if cbField is 4 or 8 bytes) be interpreted as an long or long long.

#define JET_ibfieldDbFileHdrMajorVersion                    0x008   //  Sets or alters the DB's Major Version value.
#define JET_ibfieldDbFileHdrUpdateMajor                     0x0e8   //  Sets or alters the DAE Update Major version value.
#define JET_ibfieldDbFileHdrUpdateMinor                     0x284   //  Sets or alters the DAE Update Minor version value.

typedef struct tagJET_TESTHOOKALTERDBFILEHDR
{
    JET_PWSTR           szDatabase;
    uint32_t       ibField;
    uint32_t       cbField;
    char *              pbField;
    JET_GRBIT           grbit;
} JET_TESTHOOKALTERDBFILEHDR;

JET_ERR JET_API JetTestHook(
    const TESTHOOK_OP   opcode,
    void * const        pv );

JET_ERR JET_API JetConsumeLogData(
    JET_INSTANCE        instance,
    JET_EMITDATACTX *   pEmitLogDataCtx,
    void *              pvLogData,
    uint32_t       cbLogData,
    JET_GRBIT           grbits );

#ifndef _WIN32
//  Linux clients (eseutil, BookStoreSample, custom apps) must call
//  JetPlatformInitialize() once per process before any other Jet API.
//  It does the work Windows hides inside libese.dll's DllMain plus the
//  Linux-specific bits: registers the user TLS size, suppresses the
//  perfmon path (not compiled into osposix), and bumps OSU's init
//  counter so the resource managers freeze with the right param
//  defaults.  Idempotent — extra calls return JET_errSuccess.
//
//  Pair with JetPlatformTerminate() at process shutdown when you want
//  deterministic teardown; otherwise the .so destructor handles it.
JET_ERR JET_API JetPlatformInitialize( void );
JET_ERR JET_API JetPlatformTerminate( void );
#endif /* !_WIN32 */

#endif // JET_VERSION >= 0x0601

#if ( JET_VERSION >= 0x0602 )

JET_ERR JET_API JetGetErrorInfoW(
    void *                 pvContext,
    void *  pvResult,
    uint32_t              cbMax,
    uint32_t              InfoLevel,
    JET_GRBIT                  grbit );

#ifdef JET_UNICODE
#define JetGetErrorInfo JetGetErrorInfoW
#else
#define JetGetErrorInfo JetGetErrorInfoA_DoesNotExist_OnlyUnicodeVersionOfThisAPI_UseExcplicit_JetGetErrorInfoW_Instead
#endif

JET_ERR JET_API
JetSetSessionParameter(
    JET_SESID                                          sesid,
    uint32_t                                          sesparamid,
    void *                      pvParam,
    uint32_t                                          cbParam );

JET_ERR JET_API
JetGetSessionParameter(
    JET_SESID                                          sesid,
    uint32_t                                          sesparamid,
    void *    pvParam,
    uint32_t                                          cbParamMax,
    uint32_t *                                   pcbParamActual );

JET_ERR JET_API JetPrereadTablesW(
    JET_SESID                          sesid,
    JET_DBID                           dbid,
    JET_PCWSTR *   rgwszTables,
    int32_t                               cwszTables,
    JET_GRBIT                          grbit );

#ifdef JET_UNICODE
#define JetPrereadTables JetPrereadTablesW
#else
#define JetPrereadTables JetPrereadTablesA_DoesNotExist_OnlyUnicodeVersionOfThisAPI_UseExcplicit_JetPrereadTablesW_Instead
#endif

#endif // JET_VERSION >= 0x0602
#if ( JET_VERSION >= 0x0A00 )

JET_ERR JET_API
JetPrereadIndexRange(
    JET_SESID                      sesid,
    JET_TABLEID                    tableid,
    const JET_INDEX_RANGE * const  pIndexRange,
    const uint32_t            cPageCacheMin,
    const uint32_t            cPageCacheMax,
    JET_GRBIT                      grbit,
    uint32_t * const     pcPageCacheActual );

#endif // JET_VERSION >= 0x0A00

#if ( JET_VERSION >= 0x0A01 )

JET_ERR JET_API JetRetrieveColumnByReference(
    const JET_SESID                                            sesid,
    const JET_TABLEID                                          tableid,
    const void * const              pvReference,
    const uint32_t                                        cbReference,
    const uint32_t                                        ibData,
    void * const pvData,
    const uint32_t                                        cbData,
    uint32_t * const                                 pcbActual,
    const JET_GRBIT                                            grbit );

JET_ERR JET_API JetPrereadColumnsByReference(
    const JET_SESID                                    sesid,
    const JET_TABLEID                                  tableid,
    const void * const * const    rgpvReferences,
    const uint32_t * const   rgcbReferences,
    const uint32_t                                cReferences,
    const uint32_t                                cPageCacheMin,
    const uint32_t                                cPageCacheMax,
    uint32_t * const                         pcReferencesPreread,
    const JET_GRBIT                                    grbit );

#endif // JET_VERSION >= 0x0A01

#if ( JET_VERSION >= 0x0A01 )

JET_ERR JET_API JetStreamRecords(
    JET_SESID                                                  sesid,
    JET_TABLEID                                                tableid,
    const uint32_t                                        ccolumnid,
    const JET_COLUMNID * const          rgcolumnid,
    void * const    pvData,
    const uint32_t                                        cbData,
    uint32_t * const                                 pcbActual,
    const JET_GRBIT                                            grbit );

JET_ERR JET_API JetRetrieveColumnFromRecordStream(
    void * const    pvData,
    const uint32_t                        cbData,
    uint32_t * const                     piRecord,
    JET_COLUMNID * const                      pcolumnid,
    uint32_t * const                     pitagSequence,
    uint32_t * const                     pibValue,
    uint32_t * const                     pcbValue );

#endif // JET_VERSION >= 0x0A01
#ifdef  __cplusplus
} // extern "C" - Note: from the beginning of the #if !defined(_JET_NOPROTOTYPES) section.
#endif

#endif  /* _JET_NOPROTOTYPES */

#pragma pack(pop)

#ifdef  __cplusplus
} // extern "C"
#endif

#endif  /* _JET_INCLUDED */
