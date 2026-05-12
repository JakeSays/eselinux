// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
// EseTable — RAII over JetCreateTable / JetOpenTable → JetCloseTable.

#pragma once

#include <jet.h>

#include <string>
#include <string_view>

namespace ese::tests
{

class EseDatabase;

enum class EseTableMode
{
    Create,
    Open,
};

class EseTable
{
public:
    EseTable(EseDatabase& database,
             std::string_view tableName,
             EseTableMode mode = EseTableMode::Create,
             unsigned long initialPages = 16,
             unsigned long initialDensity = 80);
    ~EseTable();

    EseTable(const EseTable&) = delete;
    EseTable& operator=(const EseTable&) = delete;

    JET_TABLEID Id() const
    {
        return _id;
    }

    EseDatabase& Database() const
    {
        return _database;
    }

    const std::string& Name() const
    {
        return _name;
    }

    // Convenience over JetAddColumnA — most scenarios add columns one
    // at a time without a default value. Returns the assigned
    // JET_COLUMNID and throws ScenarioFailure on error.
    JET_COLUMNID AddColumn(std::string_view columnName,
                           JET_COLTYP columnType,
                           JET_GRBIT flags = 0,
                           uint32_t maximumBytes = 0,
                           uint16_t codePage = 0);

    // AddColumn variant that supplies a default value. The default
    // buffer must outlive this call.
    JET_COLUMNID AddColumnWithDefault(std::string_view columnName,
                                      JET_COLTYP columnType,
                                      const void* defaultValue,
                                      uint32_t defaultValueBytes,
                                      JET_GRBIT flags = 0,
                                      uint32_t maximumBytes = 0,
                                      uint16_t codePage = 0);

    // Create an index over the table. keyDescriptor follows the
    // JET key-spec convention: "+ColumnA\0-ColumnB\0\0" — a
    // null-separated list of \[+-\]ColumnName entries terminated by
    // a double null. The descriptor's length is computed by
    // scanning to that terminator.
    void CreateIndex(std::string_view indexName,
                     std::string_view keyDescriptor,
                     JET_GRBIT flags = 0,
                     unsigned long density = 80);

private:
    EseDatabase& _database;
    std::string _name;
    JET_TABLEID _id = JET_tableidNil;
};

} // namespace ese::tests
