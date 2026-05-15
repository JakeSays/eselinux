// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
// Coverage for the W (UTF-16) entry points of the JET API.  Every
// other scenario file routes through the A (UTF-8 / ANSI) variants
// via the Framework helpers; this file talks directly to the W
// surface so a bug in the windows-shim MultiByteToWideChar /
// WideCharToMultiByte plumbing on Linux can't hide behind the A path.

#include "Framework/Check.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/TemporaryDirectory.hxx"

#include <jetapi.h>

#include <cstring>
#include <filesystem>
#include <string>

using namespace ese::tests;

namespace
{

//  std::filesystem::path is char-typed on Linux; transcode to UTF-16
//  for the W APIs.  All scenario paths in this file are 7-bit ASCII
//  except where a test deliberately exercises non-ASCII, so a simple
//  widening loop is sufficient — no UTF-8 decoding needed.
std::u16string ToWide( const std::string& narrow )
{
    std::u16string wide;
    wide.reserve( narrow.size() );
    for ( char c : narrow )
    {
        wide.push_back( static_cast<char16_t>( static_cast<unsigned char>( c ) ) );
    }
    return wide;
}

std::u16string PathWithSeparator( const std::filesystem::path& directory )
{
    auto narrow = directory.string();
    if ( !narrow.empty() && narrow.back() != '/' )
    {
        narrow.push_back( '/' );
    }
    return ToWide( narrow );
}

//  Build an absolute path under `directory` for a UTF-16 leaf
//  filename.  Used by every scenario that calls JetCreateDatabaseW
//  so the .mdb lands under the scenario's TemporaryDirectory and
//  not in whatever cwd the test runner happened to be in.
std::u16string DatabasePathUnder( const std::filesystem::path& directory,
                                  const char16_t* leafName )
{
    auto path = PathWithSeparator( directory );
    for ( const char16_t* p = leafName; *p != u'\0'; ++p )
    {
        path.push_back( *p );
    }
    return path;
}

//  Boot an instance with the path/log/temp params pointed at the
//  scenario's temp directory.  Mirrors EseInstance's A-based bootstrap
//  but every system-parameter call goes through JetSetSystemParameterW.
JET_INSTANCE InitInstanceWide( const TemporaryDirectory& directory,
                               const std::u16string& instanceName )
{
    const auto wideDirectory = PathWithSeparator( directory.Path() );
    JET_INSTANCE handle = JET_instanceNil;

    CheckJet( JetCreateInstance2W( &handle,
                                   instanceName.c_str(),
                                   instanceName.c_str(),
                                   0 ) );

    auto setString = [&]( unsigned long paramId,
                          const std::u16string& value ) {
        CheckJet( JetSetSystemParameterW( &handle,
                                          JET_sesidNil,
                                          paramId,
                                          0,
                                          value.c_str() ) );
    };
    auto setInteger = [&]( unsigned long paramId,
                           JET_API_PTR value ) {
        CheckJet( JetSetSystemParameterW( &handle,
                                          JET_sesidNil,
                                          paramId,
                                          value,
                                          nullptr ) );
    };

    setString( JET_paramSystemPath, wideDirectory );
    setString( JET_paramTempPath, wideDirectory );
    setString( JET_paramLogFilePath, wideDirectory );
    setString( JET_paramBaseName, u"edb" );
    setString( JET_paramEventSource, instanceName );
    setInteger( JET_paramCircularLog, 1 );

    CheckJet( JetInit( &handle ) );
    return handle;
}

void TerminateInstance( JET_INSTANCE handle )
{
    if ( handle != JET_instanceNil )
    {
        (void)JetTerm2( handle, JET_bitTermComplete );
    }
}

} // namespace

//  JetCreateInstanceW — single-name v1 form.  Verify the instance
//  boots, runs JetInit, and tears down cleanly via JetTerm2.
EseIntegrationScenario( WideApi, CreateInstanceWBootsCleanly )
{
    TemporaryDirectory directory( "WideApi.CreateInstanceWBootsCleanly" );
    JET_INSTANCE handle = JET_instanceNil;
    static const char16_t InstanceName[] = u"WideApi-CreateInstanceW";
    CheckJet( JetCreateInstanceW( &handle, InstanceName ) );
    Require( handle != JET_instanceNil );

    const auto wideDirectory = PathWithSeparator( directory.Path() );
    CheckJet( JetSetSystemParameterW( &handle, JET_sesidNil,
                                      JET_paramSystemPath, 0,
                                      wideDirectory.c_str() ) );
    CheckJet( JetSetSystemParameterW( &handle, JET_sesidNil,
                                      JET_paramTempPath, 0,
                                      wideDirectory.c_str() ) );
    CheckJet( JetSetSystemParameterW( &handle, JET_sesidNil,
                                      JET_paramLogFilePath, 0,
                                      wideDirectory.c_str() ) );
    CheckJet( JetSetSystemParameterW( &handle, JET_sesidNil,
                                      JET_paramBaseName, 0, u"edb" ) );
    CheckJet( JetSetSystemParameterW( &handle, JET_sesidNil,
                                      JET_paramCircularLog, 1, nullptr ) );

    CheckJet( JetInit( &handle ) );
    Require( handle != JET_instanceNil );
    CheckJet( JetTerm2( handle, JET_bitTermComplete ) );
}

//  JetCreateInstance2W — two-name (instance + display) form with
//  grbit.  Once the instance is up, run a session begin/end to prove
//  that the wide bootstrap left a healthy instance behind.
EseIntegrationScenario( WideApi, CreateInstance2WStampsDisplayName )
{
    TemporaryDirectory directory( "WideApi.CreateInstance2WStampsDisplayName" );
    JET_INSTANCE handle = InitInstanceWide( directory,
                                            u"WideApi-CreateInstance2W" );

    JET_SESID session = JET_sesidNil;
    CheckJet( JetBeginSessionW( handle, &session, nullptr, nullptr ) );
    Require( session != JET_sesidNil );
    CheckJet( JetEndSession( session, 0 ) );

    TerminateInstance( handle );
}

//  JetBeginSessionW with non-null user/password JET_PCWSTR inputs.
//  The engine ignores credentials (no auth model), but we want the
//  W path to accept arbitrary UTF-16 strings without trying to
//  narrow them to ANSI.
EseIntegrationScenario( WideApi, BeginSessionWAcceptsWideCredentials )
{
    TemporaryDirectory directory( "WideApi.BeginSessionWAcceptsWideCredentials" );
    JET_INSTANCE handle = InitInstanceWide( directory, u"WideApi-BeginSessionW" );

    JET_SESID session = JET_sesidNil;
    static const char16_t UserName[] = u"jake";
    static const char16_t Password[] = u"placeholder";
    CheckJet( JetBeginSessionW( handle, &session, UserName, Password ) );
    Require( session != JET_sesidNil );
    CheckJet( JetEndSession( session, 0 ) );

    TerminateInstance( handle );
}

//  JetCreateDatabaseW + JetAttachDatabaseW round-trip.  Create the
//  database, close + detach, reattach and reopen — proves the W
//  path persists the path correctly across detach/attach.
EseIntegrationScenario( WideApi, CreateDatabaseWThenAttachWReopens )
{
    TemporaryDirectory directory(
        "WideApi.CreateDatabaseWThenAttachWReopens" );
    JET_INSTANCE handle = InitInstanceWide( directory, u"WideApi-CreateDbW" );
    JET_SESID session = JET_sesidNil;
    CheckJet( JetBeginSessionW( handle, &session, nullptr, nullptr ) );

    const auto databasePath = DatabasePathUnder( directory.Path(),
                                                 u"createdbw.mdb" );
    JET_DBID dbid = JET_dbidNil;
    CheckJet( JetCreateDatabaseW( session,
                                  databasePath.c_str(),
                                  nullptr,
                                  &dbid,
                                  0 ) );
    Require( dbid != JET_dbidNil );
    CheckJet( JetCloseDatabase( session, dbid, 0 ) );
    CheckJet( JetDetachDatabaseW( session, databasePath.c_str() ) );

    CheckJet( JetAttachDatabaseW( session, databasePath.c_str(), 0 ) );
    JET_DBID reopened = JET_dbidNil;
    CheckJet( JetOpenDatabaseW( session,
                                databasePath.c_str(),
                                nullptr,
                                &reopened,
                                0 ) );
    Require( reopened != JET_dbidNil );
    CheckJet( JetCloseDatabase( session, reopened, 0 ) );

    CheckJet( JetEndSession( session, 0 ) );
    TerminateInstance( handle );
}

//  JetCreateDatabase2W — adds cpgDatabaseSizeMax to the v1 surface.
//  Confirm the cap is round-tripped via JetGetMaxDatabaseSize.
EseIntegrationScenario( WideApi, CreateDatabase2WCapsMaxSize )
{
    TemporaryDirectory directory( "WideApi.CreateDatabase2WCapsMaxSize" );
    JET_INSTANCE handle = InitInstanceWide( directory, u"WideApi-CreateDb2W" );
    JET_SESID session = JET_sesidNil;
    CheckJet( JetBeginSessionW( handle, &session, nullptr, nullptr ) );

    const auto databasePath = DatabasePathUnder( directory.Path(),
                                                 u"createdb2w.mdb" );
    JET_DBID dbid = JET_dbidNil;
    constexpr uint32_t MaxPages = 2048;
    CheckJet( JetCreateDatabase2W( session,
                                   databasePath.c_str(),
                                   MaxPages,
                                   &dbid,
                                   0 ) );
    Require( dbid != JET_dbidNil );

    uint32_t observedMaxPages = 0;
    CheckJet( JetGetMaxDatabaseSize( session, dbid,
                                     &observedMaxPages, 0 ) );
    Require( observedMaxPages == MaxPages );

    CheckJet( JetCloseDatabase( session, dbid, 0 ) );
    CheckJet( JetEndSession( session, 0 ) );
    TerminateInstance( handle );
}

//  JetAttachDatabase2W — adds cpgDatabaseSizeMax + grbit to the v1
//  attach.  Detach the original attach, re-attach via v2W with a
//  cap, confirm the cap stuck.
EseIntegrationScenario( WideApi, AttachDatabase2WAppliesMaxSize )
{
    TemporaryDirectory directory( "WideApi.AttachDatabase2WAppliesMaxSize" );
    JET_INSTANCE handle = InitInstanceWide( directory, u"WideApi-AttachDb2W" );
    JET_SESID session = JET_sesidNil;
    CheckJet( JetBeginSessionW( handle, &session, nullptr, nullptr ) );

    const auto databasePath = DatabasePathUnder( directory.Path(),
                                                 u"attachdb2w.mdb" );
    JET_DBID dbid = JET_dbidNil;
    CheckJet( JetCreateDatabaseW( session, databasePath.c_str(), nullptr,
                                  &dbid, 0 ) );
    CheckJet( JetCloseDatabase( session, dbid, 0 ) );
    CheckJet( JetDetachDatabaseW( session, databasePath.c_str() ) );

    constexpr uint32_t MaxPages = 4096;
    CheckJet( JetAttachDatabase2W( session,
                                   databasePath.c_str(),
                                   MaxPages,
                                   0 ) );
    JET_DBID reopened = JET_dbidNil;
    CheckJet( JetOpenDatabaseW( session, databasePath.c_str(), nullptr,
                                &reopened, 0 ) );
    uint32_t observedMaxPages = 0;
    CheckJet( JetGetMaxDatabaseSize( session, reopened,
                                     &observedMaxPages, 0 ) );
    Require( observedMaxPages == MaxPages );

    CheckJet( JetCloseDatabase( session, reopened, 0 ) );
    CheckJet( JetEndSession( session, 0 ) );
    TerminateInstance( handle );
}

//  JetCreateTableW + JetAddColumnW + JetCreateIndexW.  End-to-end
//  schema build through the W path, then insert + retrieve a row
//  to confirm the table is functional.  Three APIs in one scenario.
EseIntegrationScenario( WideApi, CreateTableWAddColumnWCreateIndexWFullDDL )
{
    TemporaryDirectory directory(
        "WideApi.CreateTableWAddColumnWCreateIndexWFullDDL" );
    JET_INSTANCE handle = InitInstanceWide( directory, u"WideApi-DDLChainW" );
    JET_SESID session = JET_sesidNil;
    CheckJet( JetBeginSessionW( handle, &session, nullptr, nullptr ) );

    const auto databasePath = DatabasePathUnder( directory.Path(),
                                                 u"ddlchainw.mdb" );
    JET_DBID dbid = JET_dbidNil;
    CheckJet( JetCreateDatabaseW( session, databasePath.c_str(), nullptr,
                                  &dbid, 0 ) );

    static const char16_t TableName[] = u"Items";
    JET_TABLEID tableId = JET_tableidNil;
    CheckJet( JetCreateTableW( session, dbid, TableName, 16, 100,
                               &tableId ) );
    Require( tableId != JET_tableidNil );

    JET_COLUMNDEF identityColumn = {};
    identityColumn.cbStruct = sizeof( identityColumn );
    identityColumn.coltyp = JET_coltypLong;
    identityColumn.grbit = JET_bitColumnAutoincrement;
    JET_COLUMNID identityColumnId = 0;
    CheckJet( JetAddColumnW( session, tableId, u"Identity",
                             &identityColumn, nullptr, 0,
                             &identityColumnId ) );

    JET_COLUMNDEF valueColumn = {};
    valueColumn.cbStruct = sizeof( valueColumn );
    valueColumn.coltyp = JET_coltypLong;
    valueColumn.grbit = JET_bitColumnNotNULL;
    JET_COLUMNID valueColumnId = 0;
    CheckJet( JetAddColumnW( session, tableId, u"Value",
                             &valueColumn, nullptr, 0,
                             &valueColumnId ) );

    //  cbKey is byte count (not code-unit count) — wide keyspec
    //  doubles the byte cost vs the A path.  The trailing extra
    //  u'\0' inside the literal plus the implicit string terminator
    //  give the required double-null termination.
    static const char16_t IndexKey[] = u"+Identity\0";
    CheckJet( JetCreateIndexW( session, tableId, u"PrimaryByIdentity",
                               JET_bitIndexPrimary | JET_bitIndexUnique,
                               IndexKey,
                               sizeof( IndexKey ),
                               80 ) );

    CheckJet( JetBeginTransaction( session ) );
    const int32_t value = 42;
    CheckJet( JetPrepareUpdate( session, tableId, JET_prepInsert ) );
    CheckJet( JetSetColumn( session, tableId, valueColumnId,
                            &value, sizeof( value ), 0, nullptr ) );
    CheckJet( JetUpdate( session, tableId, nullptr, 0, nullptr ) );
    CheckJet( JetCommitTransaction( session, 0 ) );

    CheckJet( JetMove( session, tableId, JET_MoveFirst, 0 ) );
    int32_t retrieved = 0;
    uint32_t cb = 0;
    CheckJet( JetRetrieveColumn( session, tableId, valueColumnId,
                                 &retrieved, sizeof( retrieved ),
                                 &cb, 0, nullptr ) );
    Require( retrieved == 42 );

    CheckJet( JetCloseTable( session, tableId ) );
    CheckJet( JetCloseDatabase( session, dbid, 0 ) );
    CheckJet( JetEndSession( session, 0 ) );
    TerminateInstance( handle );
}

//  JetCreateTableColumnIndexW — single-call DDL via JET_TABLECREATE_W
//  struct.  Same surface as CreateTableW + AddColumnW + CreateIndexW
//  but bundled, so all the W strings sit in one struct.
EseIntegrationScenario( WideApi, CreateTableColumnIndexWBuildsAtomically )
{
    TemporaryDirectory directory(
        "WideApi.CreateTableColumnIndexWBuildsAtomically" );
    JET_INSTANCE handle = InitInstanceWide( directory, u"WideApi-CTCIW" );
    JET_SESID session = JET_sesidNil;
    CheckJet( JetBeginSessionW( handle, &session, nullptr, nullptr ) );

    const auto databasePath = DatabasePathUnder( directory.Path(),
                                                 u"ctciw.mdb" );
    JET_DBID dbid = JET_dbidNil;
    CheckJet( JetCreateDatabaseW( session, databasePath.c_str(), nullptr,
                                  &dbid, 0 ) );

    JET_COLUMNCREATE_W columns[2] = { {}, {} };
    static char16_t IdentityName[] = u"Identity";
    columns[0].cbStruct = sizeof( columns[0] );
    columns[0].szColumnName = IdentityName;
    columns[0].coltyp = JET_coltypLong;
    columns[0].grbit = JET_bitColumnAutoincrement;
    static char16_t ValueName[] = u"Value";
    columns[1].cbStruct = sizeof( columns[1] );
    columns[1].szColumnName = ValueName;
    columns[1].coltyp = JET_coltypLong;
    columns[1].grbit = JET_bitColumnNotNULL;

    JET_INDEXCREATE_W indexes[1] = { {} };
    static char16_t IndexName[] = u"PrimaryByIdentity";
    static char16_t IndexKey[] = u"+Identity\0";
    indexes[0].cbStruct = sizeof( indexes[0] );
    indexes[0].szIndexName = IndexName;
    indexes[0].szKey = IndexKey;
    indexes[0].cbKey = sizeof( IndexKey );
    indexes[0].grbit = JET_bitIndexPrimary | JET_bitIndexUnique;
    indexes[0].ulDensity = 80;

    JET_TABLECREATE_W tableCreate = {};
    static char16_t TableName[] = u"Items";
    tableCreate.cbStruct = sizeof( tableCreate );
    tableCreate.szTableName = TableName;
    tableCreate.ulPages = 16;
    tableCreate.ulDensity = 100;
    tableCreate.rgcolumncreate = columns;
    tableCreate.cColumns = 2;
    tableCreate.rgindexcreate = indexes;
    tableCreate.cIndexes = 1;

    CheckJet( JetCreateTableColumnIndexW( session, dbid, &tableCreate ) );
    Require( tableCreate.tableid != JET_tableidNil );
    Require( columns[0].columnid != 0 );
    Require( columns[1].columnid != 0 );

    CheckJet( JetCloseTable( session, tableCreate.tableid ) );
    CheckJet( JetCloseDatabase( session, dbid, 0 ) );
    CheckJet( JetEndSession( session, 0 ) );
    TerminateInstance( handle );
}

//  JetCreateIndex2W — INDEXCREATE struct path (vs. v1's positional
//  args).  The W variant stresses that every szIndexName + szKey
//  pointer in the struct array is decoded through the W path.
EseIntegrationScenario( WideApi, CreateIndex2WStructPath )
{
    TemporaryDirectory directory( "WideApi.CreateIndex2WStructPath" );
    JET_INSTANCE handle = InitInstanceWide( directory, u"WideApi-CI2W" );
    JET_SESID session = JET_sesidNil;
    CheckJet( JetBeginSessionW( handle, &session, nullptr, nullptr ) );

    const auto databasePath = DatabasePathUnder( directory.Path(),
                                                 u"ci2w.mdb" );
    JET_DBID dbid = JET_dbidNil;
    CheckJet( JetCreateDatabaseW( session, databasePath.c_str(), nullptr,
                                  &dbid, 0 ) );

    JET_TABLEID tableId = JET_tableidNil;
    CheckJet( JetCreateTableW( session, dbid, u"Rows", 16, 100,
                               &tableId ) );
    JET_COLUMNDEF column = {};
    column.cbStruct = sizeof( column );
    column.coltyp = JET_coltypLong;
    column.grbit = JET_bitColumnNotNULL;
    JET_COLUMNID columnId = 0;
    CheckJet( JetAddColumnW( session, tableId, u"Rank",
                             &column, nullptr, 0, &columnId ) );

    JET_INDEXCREATE_W indexes[1] = { {} };
    static char16_t IndexName[] = u"ByRank";
    static char16_t IndexKey[] = u"+Rank\0";
    indexes[0].cbStruct = sizeof( indexes[0] );
    indexes[0].szIndexName = IndexName;
    indexes[0].szKey = IndexKey;
    indexes[0].cbKey = sizeof( IndexKey );
    indexes[0].grbit = JET_bitIndexUnique;
    indexes[0].ulDensity = 80;

    CheckJet( JetCreateIndex2W( session, tableId, indexes, 1 ) );

    CheckJet( JetCloseTable( session, tableId ) );
    CheckJet( JetCloseDatabase( session, dbid, 0 ) );
    CheckJet( JetEndSession( session, 0 ) );
    TerminateInstance( handle );
}

//  JetSetSystemParameterW + JetGetSystemParameterW round-trip on a
//  string-valued parameter.  String params on ESE are init-time only
//  (post-init writes return JET_errAlreadyInitialized), so the W
//  marker has to land between JetCreateInstance2W and JetInit.  Read
//  it back via JetGetSystemParameterW after init to prove the W
//  marshalling preserved the bytes through the param store.
EseIntegrationScenario( WideApi, SystemParameterRoundTripsViaWString )
{
    TemporaryDirectory directory(
        "WideApi.SystemParameterRoundTripsViaWString" );
    JET_INSTANCE handle = JET_instanceNil;
    static const char16_t InstanceName[] = u"WideApi-SysParamW";
    CheckJet( JetCreateInstance2W( &handle, InstanceName, InstanceName, 0 ) );

    const auto wideDirectory = PathWithSeparator( directory.Path() );
    auto setString = [&]( unsigned long paramId, const char16_t* value ) {
        CheckJet( JetSetSystemParameterW( &handle, JET_sesidNil,
                                          paramId, 0, value ) );
    };
    setString( JET_paramSystemPath, wideDirectory.c_str() );
    setString( JET_paramTempPath, wideDirectory.c_str() );
    setString( JET_paramLogFilePath, wideDirectory.c_str() );
    setString( JET_paramBaseName, u"edb" );
    CheckJet( JetSetSystemParameterW( &handle, JET_sesidNil,
                                      JET_paramCircularLog, 1, nullptr ) );

    static const char16_t MarkerValue[] = u"WideApi-marker-value-1234";
    setString( JET_paramEventSource, MarkerValue );

    CheckJet( JetInit( &handle ) );

    char16_t buffer[64] = {};
    JET_API_PTR throwaway = 0;
    CheckJet( JetGetSystemParameterW( handle, JET_sesidNil,
                                      JET_paramEventSource,
                                      &throwaway,
                                      buffer,
                                      sizeof( buffer ) ) );
    bool matches = true;
    for ( size_t i = 0; MarkerValue[i] != u'\0'; ++i )
    {
        if ( buffer[i] != MarkerValue[i] )
        {
            matches = false;
            break;
        }
    }
    Require( matches );

    TerminateInstance( handle );
}

//  Non-ASCII edge case: database filename contains characters
//  outside the ANSI codepage (Cyrillic + accented Latin).  The W
//  path must marshal these to UTF-8 (or whatever the engine's
//  filesystem layer uses) without falling back to ANSI narrowing.
//  Verify by detaching, reattaching by the same wide path, and
//  reopening — proves the engine stored the path losslessly.
EseIntegrationScenario( WideApi, NonAsciiDatabasePathPersists )
{
    TemporaryDirectory directory( "WideApi.NonAsciiDatabasePathPersists" );
    JET_INSTANCE handle = InitInstanceWide( directory, u"WideApi-NonAsciiPath" );
    JET_SESID session = JET_sesidNil;
    CheckJet( JetBeginSessionW( handle, &session, nullptr, nullptr ) );

    //  "test-тест-café.mdb" — mix of ASCII, Cyrillic and Latin-1
    //  Supplement, all representable in BMP so each char16_t is a
    //  single code unit.
    const auto databasePath = DatabasePathUnder( directory.Path(),
                                                 u"test-тест-café.mdb" );
    JET_DBID dbid = JET_dbidNil;
    CheckJet( JetCreateDatabaseW( session, databasePath.c_str(), nullptr,
                                  &dbid, 0 ) );
    CheckJet( JetCloseDatabase( session, dbid, 0 ) );
    CheckJet( JetDetachDatabaseW( session, databasePath.c_str() ) );

    CheckJet( JetAttachDatabaseW( session, databasePath.c_str(), 0 ) );
    JET_DBID reopened = JET_dbidNil;
    CheckJet( JetOpenDatabaseW( session, databasePath.c_str(), nullptr,
                                &reopened, 0 ) );
    Require( reopened != JET_dbidNil );
    CheckJet( JetCloseDatabase( session, reopened, 0 ) );

    CheckJet( JetEndSession( session, 0 ) );
    TerminateInstance( handle );
}

//  Non-ASCII table + column names round-trip.  Insert and retrieve
//  a row via the W-named columns to prove the catalog stored the
//  W identifiers losslessly and the cursor APIs find them again.
EseIntegrationScenario( WideApi, NonAsciiTableAndColumnNamesRoundTrip )
{
    TemporaryDirectory directory(
        "WideApi.NonAsciiTableAndColumnNamesRoundTrip" );
    JET_INSTANCE handle = InitInstanceWide( directory,
                                            u"WideApi-NonAsciiSchema" );
    JET_SESID session = JET_sesidNil;
    CheckJet( JetBeginSessionW( handle, &session, nullptr, nullptr ) );

    const auto databasePath = DatabasePathUnder( directory.Path(),
                                                 u"nonascii-schema.mdb" );
    JET_DBID dbid = JET_dbidNil;
    CheckJet( JetCreateDatabaseW( session, databasePath.c_str(), nullptr,
                                  &dbid, 0 ) );

    //  Identifier names round-trip through the engine's narrow
    //  catalog, which on Linux is CP_ACP = Windows-1252.  These
    //  names use 1252-representable characters (Latin-1 Supplement +
    //  ligatures from 1252's 0x80-0x9F block).  See companion
    //  scenario `NonCp1252IdentifierIsRejected` for the explicit
    //  rejection of codepoints outside the codepage.
    static const char16_t TableName[]  = u"Café";   // U+00E9 in 1252
    static const char16_t ColumnName[] = u"étoile"; // U+00E9 in 1252
    JET_TABLEID tableId = JET_tableidNil;
    CheckJet( JetCreateTableW( session, dbid, TableName, 16, 100,
                               &tableId ) );

    JET_COLUMNDEF column = {};
    column.cbStruct = sizeof( column );
    column.coltyp = JET_coltypLong;
    column.grbit = JET_bitColumnNotNULL;
    JET_COLUMNID columnId = 0;
    CheckJet( JetAddColumnW( session, tableId, ColumnName,
                             &column, nullptr, 0, &columnId ) );

    CheckJet( JetBeginTransaction( session ) );
    const int32_t value = 7;
    CheckJet( JetPrepareUpdate( session, tableId, JET_prepInsert ) );
    CheckJet( JetSetColumn( session, tableId, columnId,
                            &value, sizeof( value ), 0, nullptr ) );
    CheckJet( JetUpdate( session, tableId, nullptr, 0, nullptr ) );
    CheckJet( JetCommitTransaction( session, 0 ) );

    //  Close + reopen the table by its W name to confirm the catalog
    //  stored the non-ASCII identifier losslessly.
    CheckJet( JetCloseTable( session, tableId ) );
    JET_TABLEID reopened = JET_tableidNil;
    CheckJet( JetOpenTableW( session, dbid, TableName, nullptr, 0, 0,
                             &reopened ) );
    Require( reopened != JET_tableidNil );

    CheckJet( JetMove( session, reopened, JET_MoveFirst, 0 ) );
    int32_t retrieved = 0;
    uint32_t cb = 0;
    CheckJet( JetRetrieveColumn( session, reopened, columnId,
                                 &retrieved, sizeof( retrieved ),
                                 &cb, 0, nullptr ) );
    Require( retrieved == 7 );

    CheckJet( JetCloseTable( session, reopened ) );
    CheckJet( JetCloseDatabase( session, dbid, 0 ) );
    CheckJet( JetEndSession( session, 0 ) );
    TerminateInstance( handle );
}

//  Catalog identifiers (table / column / index names) are stored
//  in the engine's narrow form, which on Linux is CP_ACP = 1252.
//  Codepoints outside 1252 (e.g. Cyrillic) can't round-trip; the
//  shim's WideCharToMultiByte substitutes the default '?' for each
//  unrepresentable codepoint, and the engine then rejects the
//  resulting identifier as invalid (JET_errInvalidName).  This is
//  Windows-equivalent behavior for an en-US Windows install.
EseIntegrationScenario( WideApi, NonCp1252IdentifierIsRejected )
{
    TemporaryDirectory directory( "WideApi.NonCp1252IdentifierIsRejected" );
    JET_INSTANCE handle = InitInstanceWide( directory, u"WideApi-NonCp1252" );
    JET_SESID session = JET_sesidNil;
    CheckJet( JetBeginSessionW( handle, &session, nullptr, nullptr ) );

    const auto databasePath = DatabasePathUnder( directory.Path(),
                                                 u"noncp1252.mdb" );
    JET_DBID dbid = JET_dbidNil;
    CheckJet( JetCreateDatabaseW( session, databasePath.c_str(), nullptr,
                                  &dbid, 0 ) );

    //  "товары" (Russian for "goods") — none of these codepoints
    //  are in CP_1252.  Wide → 1252 substitutes '?' for each,
    //  yielding "??????", and the engine rejects the all-'?' name.
    static const char16_t CyrillicTableName[] = u"товары";
    JET_TABLEID tableId = JET_tableidNil;
    RequireJetError( JetCreateTableW( session, dbid,
                                      CyrillicTableName, 16, 100,
                                      &tableId ),
                     JET_errInvalidName );

    CheckJet( JetCloseDatabase( session, dbid, 0 ) );
    CheckJet( JetEndSession( session, 0 ) );
    TerminateInstance( handle );
}
