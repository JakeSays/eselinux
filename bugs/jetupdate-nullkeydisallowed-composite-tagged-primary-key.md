# `JetUpdate` returns `JET_errNullKeyDisallowed` for any index with `JET_bitIndexDisallowNull` whose key includes a tagged (LongBinary/LongText) column

## Summary

Calling `JetUpdate` after a `JET_prepInsert` prepare returns `JET_errNullKeyDisallowed (-1053)`
whenever the table being inserted into has any index that combines `JET_bitIndexDisallowNull`
with a key column of a tagged type (`JET_coltypLongBinary` / `JET_coltypLongText`) — even
though every key column was explicitly set to a non-null, non-empty value via `JetSetColumn`
before the `JetUpdate` call.

The flag is safe only on indexes where **every key column is a fixed-width type**.

## Reproducing schema

The bug was found in an indexer that uses ESE as a persistent symbol store.  The initial
reproducing case was the `TUSymbols` table:

```cpp
// TUSymbols — AFFECTED (original schema, no fixed-width columns)
builder.AddColumn("File",      JET_coltypLongBinary);             // tagged
builder.AddColumn("SymbolKey", JET_coltypLongBinary);             // tagged
builder.AddIndex("PK", "+File\0+SymbolKey\0\0", cbKey=18,
                 JET_bitIndexPrimary | JET_bitIndexUnique | JET_bitIndexDisallowNull);
```

The insert that fails:

```cpp
JetPrepareUpdate(sesid, tableid, JET_prepInsert);
JetSetColumn(sesid, tableid, colFile,      file.data(),      file.size(),      0, nullptr);
JetSetColumn(sesid, tableid, colSymbolKey, symbolKey.data(), symbolKey.size(), 0, nullptr);
JetUpdate(sesid, tableid, nullptr, 0, nullptr);   // returns JET_errNullKeyDisallowed (-1053)
```

Both `file` and `symbolKey` are non-empty strings validated by the caller.  The
`JetSetColumn` calls succeed (return `JET_errSuccess`).

## Root cause

The `JET_bitIndexDisallowNull` validation path inside `JetUpdate` checks column nullness
by consulting the null bitmap for each key column, regardless of column type.  Tagged columns
(`JET_coltypLongBinary`, `JET_coltypLongText`) are not represented in the null bitmap — the
null bitmap covers only fixed-width and variable-length columns — so the check reads from an
absent or incorrect bit and concludes the column is null.

### Why adding fixed-width columns to the table does not fix it

Adding fixed-width columns to a table that previously had none establishes the null bitmap in
the on-disk record format.  This fixes the case where the **entire table** had no null bitmap
at all, but it does not fix the per-column check for tagged key columns: the null bitmap still
has no bit for a `LongBinary` column, so the buggy code reads whatever is at that position in
the bitmap (zero-initialized or adjacent column data) and still may conclude the tagged column
is null.

Observed behavior after adding fixed-width columns:

| Index | Key columns | All fixed? | `JET_bitIndexDisallowNull` safe? |
|---|---|---|---|
| PK on fixed `uint64` | Fixed only | Yes | Yes |
| Secondary on fixed `uint64` | Fixed only | Yes | Yes |
| Secondary unique on `LongBinary` | Tagged | **No** | **No** |
| Composite PK `(fixed uint64, LongBinary)` | Mixed | **No** | **No** |
| Secondary on `LongBinary` | Tagged | **No** | **No** |

The single-column LongBinary primary key case (e.g., a PK on a `File` column) appeared to
work when the table had other fixed-width columns; behavior in that specific case may be
incidentally correct due to how the null bitmap boundary falls, but it cannot be relied upon.

## Resolution

The schema was normalized to introduce autoincrement `uint64` surrogate keys on
`TranslationUnits` and `TUSymbols`, replacing LongBinary primary keys with fixed-width integer
keys.  `JET_bitIndexDisallowNull` is used only on indexes whose every key column is a
fixed-width type; indexes with any tagged key column omit the flag.

```cpp
// TranslationUnits — Id (fixed) is PK; ByFile secondary for path lookup (no DisallowNull: File is tagged)
builder.AddColumn("Id",           JET_coltypUnsignedLongLong,
                  JET_bitColumnFixed | JET_bitColumnNotNULL | JET_bitColumnAutoincrement);
builder.AddColumn("File",         JET_coltypLongBinary);
builder.AddColumn("LastModified", JET_coltypUnsignedLongLong,
                  JET_bitColumnFixed | JET_bitColumnNotNULL);
builder.AddIndex("PK",     "+Id\0\0",   cbKey=5,  JET_bitIndexPrimary | JET_bitIndexUnique | JET_bitIndexDisallowNull);
builder.AddIndex("ByFile", "+File\0\0", cbKey=7,  JET_bitIndexUnique);

// TUSymbols — FileId (fixed) + SymbolKey (tagged) composite PK; Id autoincrement FK for Entries
//   DisallowNull omitted from PK and BySymbolKey because SymbolKey is tagged
builder.AddColumn("FileId",    JET_coltypUnsignedLongLong,
                  JET_bitColumnFixed | JET_bitColumnNotNULL);
builder.AddColumn("SymbolKey", JET_coltypLongBinary);
builder.AddColumn("Id",        JET_coltypUnsignedLongLong,
                  JET_bitColumnFixed | JET_bitColumnNotNULL | JET_bitColumnAutoincrement);
builder.AddIndex("PK",          "+FileId\0+SymbolKey\0\0", cbKey=20, JET_bitIndexPrimary | JET_bitIndexUnique);
builder.AddIndex("ByFileId",    "+FileId\0\0",             cbKey=9,  JET_bitIndexDisallowNull);
builder.AddIndex("BySymbolKey", "+SymbolKey\0\0",          cbKey=12, 0);
builder.AddIndex("ById",        "+Id\0\0",                 cbKey=5,  JET_bitIndexUnique | JET_bitIndexDisallowNull);

// Entries — SymbolId (fixed uint64) replaces the LongBinary SymbolKey PK
builder.AddColumn("SymbolId",   JET_coltypUnsignedLongLong,
                  JET_bitColumnFixed | JET_bitColumnNotNULL);
builder.AddColumn("Name",       JET_coltypLongBinary);
builder.AddColumn("Kind",       JET_coltypUnsignedByte,
                  JET_bitColumnFixed | JET_bitColumnNotNULL);
builder.AddColumn("DeclFile",   JET_coltypLongBinary);
builder.AddColumn("DeclLine",   JET_coltypUnsignedLong,
                  JET_bitColumnFixed | JET_bitColumnNotNULL);
builder.AddColumn("DeclColumn", JET_coltypUnsignedLong,
                  JET_bitColumnFixed | JET_bitColumnNotNULL);
builder.AddIndex("PK", "+SymbolId\0\0", cbKey=11, JET_bitIndexPrimary | JET_bitIndexUnique | JET_bitIndexDisallowNull);
```

Uniqueness on `ByFile` and the `TUSymbols` composite PK is still enforced by
`JET_bitIndexUnique`.  Application logic enforces non-null inputs for all tagged key columns,
so omitting `JET_bitIndexDisallowNull` on those indexes is safe.

**Rule of thumb:** never combine `JET_bitIndexDisallowNull` with a tagged key column in this
ESE Linux port.  Reserve the flag for indexes whose entire key is composed of fixed-width
columns.

## Additional notes

- The autoincrement column value is allocated when `JetPrepareUpdate(JET_prepInsert)` is
  called and can be retrieved from the copy buffer at any time until the update is complete
  via `JetRetrieveColumn` with `JET_bitRetrieveCopy`.  Without `JET_bitRetrieveCopy` the
  call returns `JET_errNoCurrentRecord (-1603)` because there is no existing cursor record
  during an insert.
- `JetSetCurrentIndexA` cancels any pending prepared update on the tableid.  Wrapping cursors
  in RAII that resets the index to primary on destruction must therefore not fire while a
  prepared update is in flight on the same tableid.

## Affected version

Open-source ESE Linux port (`libese.so`), confirmed against the version vendored in this
repository.  Not reproduced against Windows ESENT.
