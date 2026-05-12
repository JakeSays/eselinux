// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/EseTable.hxx"

#include "Framework/Check.hxx"
#include "Framework/EseDatabase.hxx"
#include "Framework/EseSession.hxx"

namespace ese::tests
{

EseTable::EseTable(EseDatabase& database,
                   std::string_view tableName,
                   EseTableMode mode,
                   unsigned long initialPages,
                   unsigned long initialDensity)
    : _database(database),
      _name(tableName)
{
    switch (mode)
    {
        case EseTableMode::Create:
        {
            CheckJet(JetCreateTableA(_database.Session().Handle(),
                                     _database.Id(),
                                     _name.c_str(),
                                     initialPages,
                                     initialDensity,
                                     &_id));
            break;
        }
        case EseTableMode::Open:
        {
            CheckJet(JetOpenTableA(_database.Session().Handle(),
                                   _database.Id(),
                                   _name.c_str(),
                                   nullptr,
                                   0,
                                   0,
                                   &_id));
            break;
        }
    }
}

EseTable::~EseTable()
{
    if (_id != JET_tableidNil)
    {
        (void)JetCloseTable(_database.Session().Handle(), _id);
        _id = JET_tableidNil;
    }
}

JET_COLUMNID EseTable::AddColumn(std::string_view columnName,
                                 JET_COLTYP columnType,
                                 JET_GRBIT flags,
                                 uint32_t maximumBytes,
                                 uint16_t codePage)
{
    return AddColumnWithDefault(columnName,
                                columnType,
                                nullptr,
                                0,
                                flags,
                                maximumBytes,
                                codePage);
}

JET_COLUMNID EseTable::AddColumnWithDefault(std::string_view columnName,
                                            JET_COLTYP columnType,
                                            const void* defaultValue,
                                            uint32_t defaultValueBytes,
                                            JET_GRBIT flags,
                                            uint32_t maximumBytes,
                                            uint16_t codePage)
{
    const std::string columnNameOwned(columnName);

    JET_COLUMNDEF columnDefinition = {};
    columnDefinition.cbStruct = sizeof(columnDefinition);
    columnDefinition.coltyp = columnType;
    columnDefinition.grbit = flags;
    columnDefinition.cbMax = maximumBytes;
    columnDefinition.cp = codePage;

    JET_COLUMNID columnId = 0;
    CheckJet(JetAddColumnA(_database.Session().Handle(),
                           _id,
                           columnNameOwned.c_str(),
                           &columnDefinition,
                           defaultValue,
                           defaultValueBytes,
                           &columnId));
    return columnId;
}

void EseTable::CreateIndex(std::string_view indexName,
                           std::string_view keyDescriptor,
                           JET_GRBIT flags,
                           unsigned long density)
{
    const std::string indexNameOwned(indexName);

    // The JET key descriptor convention requires a double-NUL
    // terminator. Treat the input view as bytes so callers can pass
    // it as a string literal containing embedded \0 separators.
    const auto cbKey = static_cast<uint32_t>(keyDescriptor.size());
    CheckJet(JetCreateIndexA(_database.Session().Handle(),
                             _id,
                             indexNameOwned.c_str(),
                             flags,
                             keyDescriptor.data(),
                             cbKey,
                             density));
}

} // namespace ese::tests
