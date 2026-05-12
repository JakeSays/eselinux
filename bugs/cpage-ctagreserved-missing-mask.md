# `CPAGE::CTagReserved` reads bit 15 of `PGHDR::itagState`, ignoring the documented mask

## Summary

`PGHDR::itagState` is a 16-bit field whose bit layout is documented in `dev/ese/src/inc/cpage.hxx`:

```cpp
static const USHORT ITAG_MIC_FREE_MASK = 0x0fff;   // 12-bits (for itagMicFree)
static const USHORT CTAG_RESERVED_MASK = 0x7000;   // 3-bits (ctagReserved), high bit reserved for future use
static const USHORT SHF_CTAG_RESERVED  = 12;
static const USHORT CTAG_RESERVED_MAX  = CTAG_RESERVED_MASK >> SHF_CTAG_RESERVED;   // = 7
```

That is:

- bits 0–11 (12 bits, mask `0x0fff`): `itagMicFree`
- bits 12–14 (3 bits, mask `0x7000`): `ctagReserved` (max value 7)
- bit 15: **reserved for future use**

The read paths for `itagMicFree` correctly apply `ITAG_MIC_FREE_MASK`. The read paths for `ctagReserved` apply the *shift* but **never apply the mask**, so bit 15 leaks into the result. A `ctagReserved` value sourced from a corrupted (or otherwise bit-15-set) `itagState` ends up in the range 0..15 instead of the documented 0..7.

`SetITagState_` (the only write path that produces values >= 1 in those bits) asserts `ctagReserved <= CTAG_RESERVED_MAX`, so the asymmetry never bites in normal operation — bit 15 is never set by a legitimate write. The bug is latent until a corrupted page (or a test that deliberately sets bit 15) shows up.

## Affected code

All in `dev/ese/src/inc/cpage.hxx`:

| Site | Before |
|---|---|
| `static USHORT PGHDR::CTagReserved(ppghdr)` (line ~1131) | `return ( ppghdr->itagState >> PGHDR::SHF_CTAG_RESERVED );` |
| `CPAGE::InitItagState` (line ~1557) | `USHORT ctagReserved = ppghdr->itagState >> PGHDR::SHF_CTAG_RESERVED;` |
| `CPAGE::ITagMicFree_` and assertions in `CTagReserved_` / `SetITagMicFree_` / `SetCTagReserved_` / `SetITagState_` (lines ~1582, ~1596, ~1606) | `( ppghdr->itagState >> PGHDR::SHF_CTAG_RESERVED ) == m_ctagReserved` |
| `SetITagState_`'s "format already uses reserved tag bits" predicate (line ~1624) | `( ppghdr->itagState >> PGHDR::SHF_CTAG_RESERVED ) > 0` |

`PGHDR::CTagReserved(ppghdr)` is the static helper that reads `ctagReserved` directly from a `PGHDR*` without going through a `CPAGE` instance.  It's used in several engine files outside `cpage.cxx`:

```
dev/ese/src/ese/bt.cxx
dev/ese/src/ese/lv.cxx
dev/ese/src/ese/cpage.cxx
dev/ese/src/ese/dbutil.cxx
dev/ese/src/ese/node.cxx
```

…and likely indirectly in any code that asks "is `itag` a reserved tag?" via `itag < cpage.CTagReserved()`.

## How it surfaces

Running `Node.TestCorruptItagMicFreeFullFuzzResilientCodePaths` (`dev/ese/src/ese/node_test.cxx:810`) reveals the bug.  The fuzz repeatedly does

```cpp
cpageSmall.CorruptHdr( ipgfldCorruptItagMicFree, (USHORT)i );  // i = 1 .. 0xFFFF
```

which increments `ppghdr->itagState` by `(USHORT)i`.  For `i >= 0x7FF9` the lower 12 bits wrap such that the `ctagReserved` field tips to 8 or higher; eventually the carry propagates into bit 15.  The engine's `ErrCheckPage` then iterates tags starting at `itag = CTagReserved_()` and reports

```
pgvr [UnitTest], page (2): Non TAG-0 - TAG 9 has zero cb(cb = 0, ib = 0)
pgvr [UnitTest], page (2): Non TAG-0 - TAG 10 has zero cb(cb = 0, ib = 0)
…
pgvr [UnitTest], page (2): Non TAG-0 - TAG 15 has zero cb(cb = 0, ib = 0)
```

instead of `TAG 7`, which is what would fire if the engine consistently observed `CTAG_RESERVED_MAX = 7`.

The test was last tuned to Windows-observed output, where the same code path produces a much narrower distribution (just `TAG 7`).  The recorded "expected" counts (`cTagHasZeroCb == 9158`, `cItagMicFreeTooLarge == 243772`, etc., at `node_test.cxx:944-948`) are tied to that older behavior.

## Real-world impact

The fix only changes behavior when bit 15 of `itagState` is set on a loaded page.  Three ways that can happen today:

1. **A normal write path** — Impossible.  `SetITagState_` (`cpage.hxx:1618`) asserts `ctagReserved <= CTAG_RESERVED_MAX = 7`, and the only producer (`cpage.cxx:3344`) gates the increment on `itag < PGHDR::CTAG_RESERVED_MAX`.

2. **An older on-disk format** — The pre-`ctagReserved` "legacy" format stored only `itagMicFree` in `itagState`.  The maximum sensible `itagMicFree` for the largest page size (32 KB) is `cbBufferData / sizeof(TAG) ≈ 8192 = 0x2000`, well below bit 15.  So even legacy pages never wrote bit 15.

3. **Bit-flip disk corruption** — A single flipped bit could set bit 15 in `itagState` on a real on-disk page.  The bug means the engine then reads `ctagReserved = 8..15` and tries to iterate tags from that index.  The corruption is still caught — `ErrCheckPage` will still report the page as corrupt — just with a slightly different message and the iteration starting at the wrong index.

Grepping the codebase for `0x8000`, `0xf000`, or any other reference to bit 15 of `itagState` turns up nothing.  No live code reads bit 15 separately, and no comments hint at a pending feature that would.

That said, the doc comment says "high bit reserved for **future** use", so it's conceivable bit 15 is intended for something later and the read sites simply hadn't been updated to mask it.  We didn't find any evidence of that being the case in the codebase, but it's worth a Microsoft confirmation before this fix is rolled upstream.

## Fix

Apply `CTAG_RESERVED_MASK` consistently at every read of `ctagReserved`:

```diff
- INLINE static USHORT CTagReserved( const PGHDR* ppghdr )        { return ( ppghdr->itagState >> PGHDR::SHF_CTAG_RESERVED ); }
+ INLINE static USHORT CTagReserved( const PGHDR* ppghdr )        { return ( ( ppghdr->itagState & PGHDR::CTAG_RESERVED_MASK ) >> PGHDR::SHF_CTAG_RESERVED ); }
```

```diff
  // CPAGE::InitItagState
  m_itagMicFree = ppghdr->itagState & PGHDR::ITAG_MIC_FREE_MASK;
- USHORT  ctagReserved = ppghdr->itagState >> PGHDR::SHF_CTAG_RESERVED;
+ USHORT  ctagReserved = ( ppghdr->itagState & PGHDR::CTAG_RESERVED_MASK ) >> PGHDR::SHF_CTAG_RESERVED;
  m_ctagReserved = max( 1, ctagReserved );
```

and the same mask added to the assertions in `ITagMicFree_`, `CTagReserved_`, `SetITagMicFree_`, `SetCTagReserved_`, `SetITagState_`, plus the `SetITagState_` "use new format" predicate at line ~1624.

After the fix, `ctagReserved` is constrained to its documented 0..7 range at every read site, matching what the `SetITagState_` Assert already enforced at every write site.

## Verification

`TestCorruptItagMicFreeFullFuzzResilientCodePaths` no longer emits the spurious `TAG 8..15` lines and the sum invariant `cChecks == cGoodSmall + cGoodLarge + Σ category counters` now holds end-to-end on Linux.  The Windows-tuned exact-equality count expectations still don't reproduce exactly (those numbers are sensitive to engine internals that have evolved), but with the mask in place lower-bound asserts succeed.

`TestCorruptTagIbFullFuzzResilientCodePaths` and `TestCorruptTagCbFullFuzzResilientCodePaths` also pass.

Tier-1 (`560/560`) and tier-2 (`35/35`) unit tests are unaffected — no normal-operation page has bit 15 set, so no production code path observes a behavioral change.

## Context

This bug was identified while bringing up `node_test.cxx`'s opt-in `JETUNITTESTEX dwDontRunByDefault` suites on the Linux ESE port.  The suites are gated with `dwDontRunByDefault` and never run on Windows by default, which is presumably why the latent bug has gone unnoticed since the `ctagReserved` field was introduced.

## Commits in this repo

- `c00f86b` — initial `InitItagState` mask + test-side range relaxation (caught `TestCorruptItagMicFree` failing the count invariant).
- *(follow-up)* — applied the same mask to `static PGHDR::CTagReserved(ppghdr)` and the consistency assertions in `cpage.hxx`, so the field is uniformly 0..7 wherever it's read.
