# Custom placement-`operator new` returns NULL on resource exhaustion but isn't declared `noexcept`

## Summary

A large set of ESE engine classes (`BUCKET`, `FCB`, `FUCB`, `PIB`, `SCB`, `IDB`, `TDB`, `LOG`, `INST`, `BTSPLIT` / `BTSPLITPATH` / `BTMERGE` / `BTMERGEPATH`, `CSnapshotBuffer`) each define a custom placement-`operator new` that allocates from a resource pool. The common pattern is:

```cpp
void* operator new( size_t cbAlloc, INST* pinst )
{
    return pinst->m_cresFCB.PvRESAlloc_( SzNewFile(), UlNewLine() );
}
```

`PvRESAlloc_` can — and does — return NULL on resource exhaustion. Other call sites in the codebase that consume `PvRESAlloc()` wrap the call in the `Alloc()` macro, which checks for null and propagates `JET_errOutOfMemory`. These placement-`new`s are the odd ones out — they just return whatever `PvRESAlloc_` gave them.

The standard C++ rule for new-expressions is:

- If `operator new` is **`noexcept` (or `nothrow`)** and returns null, the new-expression is null and the constructor is *not* called.
- If `operator new` is **not `noexcept`** (the default), the compiler emits the constructor call without a null check, assuming the allocation function would have thrown on failure.

None of these custom placement-`new`s are declared `noexcept`. The compiler emits the constructor call unconditionally. When `PvRESAlloc_` returns NULL, the language proceeds to call `<Class>::<Class>(this=0x0, ...)`, the constructor dereferences `this`, and the engine segfaults.

Every call site is even *aware* that null is possible. The pattern at the call site looks like:

```cpp
pfcbCandidate = new( pinst ) FCB( ifmp, pgnoFDP );

if ( pfcbNil == pfcbCandidate )
{
    //  We have allocated all of allowed FCBs and need to recycle one of them
    ...
    return JET_errOutOfMemory;  // or similar
}
```

These `if (pX == pXNil)` checks are dead code as written: without `noexcept`, the language never lets the pointer actually equal null because the constructor crashes first.

## Affected code

All in `dev/ese/src/inc/`:

| File | Class | Pool | Line |
|------|-------|------|------|
| `ver.hxx` | `BUCKET` | `m_cresBucket` | 1849 |
| `fcb.hxx` | `FCB` | `m_cresFCB` | 535 |
| `fucb.hxx` | `FUCB` | `m_cresFUCB` | 101 |
| `pib.hxx` | `PIB` | `m_cresPIB` | 227 |
| `scb.hxx` | `SCB` | `m_cresSCB` | 287 |
| `idb.hxx` | `IDB` | `m_cresIDB` | 167 |
| `tdb.hxx` | `TDB` | `m_cresTDB` | 373 |
| `log.hxx` | `LOG` | `RESLOG` | 985 |
| `daedef.hxx` | `INST` | `RESINST` | 5027 |
| `revertsnapshot.h` | `CSnapshotBuffer` | parameter | 215 |
| `btsplit.hxx` | `BTSPLIT` | `RESSPLIT` | 139 |
| `btsplit.hxx` | `BTSPLITPATH` | `RESSPLITPATH` | 227 |
| `btsplit.hxx` | `BTMERGE` | `RESMERGE` | 323 |
| `btsplit.hxx` | `BTMERGEPATH` | `RESMERGEPATH` | 395 |

Each has the same shape:

```cpp
void* operator new( size_t cbAlloc[, <some context arg>] )
{
    return <pool>.PvRESAlloc_( SzNewFile(), UlNewLine() );
}
```

## How it surfaces

Any workload that exhausts one of the resource pools. The two we hit while bringing up the refactorizer indexer:

1. **Version-store exhaustion (`BUCKET`).** A long-running transaction with many writes overflowed `JET_paramMaxVerPages` (default ~32 pages). The crash backtrace:

   ```
   #0  BUCKET::BUCKET (this=0x0, pver=...)               ver.hxx:1838
   #1  VER::ErrVERIBUAllocBucket                          ver.cxx:538
   #2  VER::ErrVERIAllocateRCE                            ver.cxx:1684
   ...
   #14 JetUpdate                                          jetapi.cxx:11162
   ```

2. **FCB-pool exhaustion (`FCB`).** Creating a few thousand derived tables in a tight loop overflowed the FCB pool. The crash backtrace:

   ```
   #0  __memset_avx2_unaligned_erms
   #1  CZeroInit::CZeroInit (this=0x0, cbDerivedClass=384)  types.hxx:1221
   #2  FCB::FCB (this=0x0, ifmp=1, pgnoFDP=40600)           fcb.hxx:1280
   #3  FCB::ErrAlloc_                                       fcb.cxx:1272
   #4  FCB::ErrCreate                                       fcb.cxx:979
   #5  ErrBTICreateFCB                                      bt.cxx:7655
   ...
   #19 JetCreateTableColumnIndex2A                          jetapi.cxx:15733
   ```

`this=0x0` on `<Class>::<Class>` is the smoking gun in both cases.

## Real-world impact

- Any workload that overflows a resource pool crashes the engine instead of returning the corresponding `JET_err...` code.
- Hidden in normal production use because most pools are generously sized and pool-cleanup runs frequently. Surfaces under bulk-write workloads (indexers, schema migrations, schema-creation scripts that create many tables).
- The crash is non-obvious — `this=0x0` in a constructor reads like memory corruption rather than an unchecked-allocation bug, so it takes a careful look at the allocation contract to track down.

## Fix

Add `noexcept` to every custom placement-`operator new` that returns from a pool which can return NULL.

```diff
-        void* operator new( size_t cbAlloc, INST* pinst )
+        void* operator new( size_t cbAlloc, INST* pinst ) noexcept
         {
             return pinst->m_cresFCB.PvRESAlloc_( SzNewFile(), UlNewLine() );
         }
```

With `noexcept`, the C++ standard requires the compiler to:

1. Call the allocator.
2. If it returns null, skip the constructor and evaluate the new-expression to null.

The existing `if (pX == pXNil)` checks at the call sites then catch the null and return the documented error code through the normal CheckJet path. The retry-after-cleanup logic in those call sites also starts working as intended.

## Files changed

- `dev/ese/src/inc/ver.hxx` (BUCKET)
- `dev/ese/src/inc/fcb.hxx` (FCB)
- `dev/ese/src/inc/fucb.hxx` (FUCB)
- `dev/ese/src/inc/pib.hxx` (PIB)
- `dev/ese/src/inc/scb.hxx` (SCB)
- `dev/ese/src/inc/idb.hxx` (IDB)
- `dev/ese/src/inc/tdb.hxx` (TDB)
- `dev/ese/src/inc/log.hxx` (LOG)
- `dev/ese/src/inc/daedef.hxx` (INST)
- `dev/ese/src/inc/revertsnapshot.h` (CSnapshotBuffer)
- `dev/ese/src/inc/btsplit.hxx` (BTSPLIT, BTSPLITPATH, BTMERGE, BTMERGEPATH)

## Verification

Reproduced both crash modes from a Clang-based indexer:

- **BUCKET path:** without the patch, segfault at `BUCKET::BUCKET(this=0x0)` once the transaction's uncommitted RCEs overflow the default version store. With the patch and default `JET_paramMaxVerPages`: clean `JET_errVersionStoreOutOfMemory` (-1069) thrown as a `JetError` exception. With the patch plus a bumped `JET_paramMaxVerPages = 65536`, the workload completes normally without exhausting the pool.
- **FCB path:** without the patch, segfault at `FCB::FCB(this=0x0)` when creating thousands of derived tables. With the patch: ~2718 derived tables (453 per-file table sets × 6 tables each) created successfully in one phase with no parameter tuning — the FCB pool recycles cleared FCBs faster than the workload allocates, so the pool never depletes in practice once the null-handling is correct.

No further JET parameter tuning was needed beyond the existing `JET_paramMaxVerPages = 65536` for the BUCKET case.

## Context

Found while bringing up the refactorizer's v2 indexer on the Linux ESE port. The indexer's table-creation phase exposed the FCB-pool variant after the BUCKET variant was already fixed; gdb backtraces on both crashes pointed straight at `<Class>::<Class>(this=0x0, ...)`, which led to the placement-`new` declarations. Grepping `dev/ese/src/inc/` for `PvRESAlloc_` then surfaced the full set.

The bug is latent under Windows / MSVC if (a) the workload doesn't exhaust any of these pools, or (b) MSVC's handling of non-`noexcept` placement `operator new` differs from clang's. Worth a confirmation from Microsoft as part of upstreaming the fix.
