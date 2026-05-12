// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
//  EseTable — RAII over JetCreateTable / JetOpenTable → JetCloseTable.

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
    EseTable(EseDatabase&     database,
             std::string_view tableName,
             EseTableMode     mode = EseTableMode::Create,
             unsigned long    initialPages    = 16,
             unsigned long    initialDensity  = 80);
    ~EseTable();

    EseTable(const EseTable&)            = delete;
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

private:
    EseDatabase& _database;
    std::string  _name;
    JET_TABLEID  _id = JET_tableidNil;
};

}  //  namespace ese::tests
