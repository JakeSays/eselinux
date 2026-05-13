namespace EtwLttng;

internal enum WinType
{
    UInt8,
    UInt16,
    UInt32,
    UInt64,
    Int32,
    Int64,
    Double,
    Pointer,
    Guid,
    UnicodeString,
    AnsiString,
    Binary,
}

internal sealed record TraceField(
    string Name,
    WinType InType,
    int BinaryLength);

internal sealed record TraceEvent(
    string Name,
    int EventId,
    string Level,
    string Keywords,
    bool Deprecated,
    IReadOnlyList<TraceField> Fields)
{
    //  lttng-ust caps tracepoints at 10 type/name pairs (LTTNG_UST__TP_NARGS
    //  enumerates 20,19,...,0).  Events wider than that get split:
    //   - First 9 fields stay typed as native ctf_* (filterable in babeltrace
    //     via `--names=...` or live filters).
    //   - Everything beyond the 9th is packed into a 10th `overflow` field
    //     of type ctf_string in `key=value key=value` form so the data is
    //     captured and human-readable, but not filterable directly.
    public const int MaxTypedFields = 9;

    public bool HasOverflow => Fields.Count > MaxTypedFields + 1;

    public IReadOnlyList<TraceField> TypedFields
        => HasOverflow ? Fields.Take(MaxTypedFields).ToArray() : Fields;

    public IReadOnlyList<TraceField> OverflowFields
        => HasOverflow ? Fields.Skip(MaxTypedFields).ToArray() : Array.Empty<TraceField>();
}
