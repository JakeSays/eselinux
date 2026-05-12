// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/EseTable.hxx"

#include "Framework/Check.hxx"
#include "Framework/EseDatabase.hxx"
#include "Framework/EseSession.hxx"

namespace ese::tests
{

EseTable::EseTable(EseDatabase&     database,
                   std::string_view tableName,
                   EseTableMode     mode,
                   unsigned long    initialPages,
                   unsigned long    initialDensity)
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

}  //  namespace ese::tests
