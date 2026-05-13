namespace EtwLttng;

internal static class TypeMap
{
    //  C scalar type used in the table struct signature and in the
    //  trampoline parameter list inside libese_tracepoints.so.
    //  Strings are UTF-8 char* by the time they reach the trampoline
    //  (the osposix dispatch converts the engine's 16-bit wchar_t
    //  buffer before invoking the function pointer).
    public static string ScalarCType(WinType t) => t switch
    {
        WinType.UInt8 => "uint8_t",
        WinType.UInt16 => "uint16_t",
        WinType.UInt32 => "uint32_t",
        WinType.UInt64 => "uint64_t",
        WinType.Int32 => "int32_t",
        WinType.Int64 => "int64_t",
        WinType.Double => "double",
        WinType.Pointer => "uintptr_t",
        _ => throw new InvalidOperationException($"not a scalar: {t}"),
    };

    public static bool IsScalar(WinType t) => t switch
    {
        WinType.UInt8 or WinType.UInt16 or WinType.UInt32 or WinType.UInt64 => true,
        WinType.Int32 or WinType.Int64 or WinType.Double or WinType.Pointer => true,
        _ => false,
    };

    //  Field declaration in the EseTracepointTable function-pointer
    //  parameter list / trampoline signature.
    public static string TrampolineParamType(TraceField f) => f.InType switch
    {
        WinType.UnicodeString or WinType.AnsiString => "const char*",
        WinType.Binary or WinType.Guid => "const uint8_t*",
        _ => ScalarCType(f.InType),
    };

    //  Source expression in the osposix OSEventTrace_ dispatch when
    //  unpacking from va_list `ap`.
    //
    //  Scalars: engine passes a pointer to the value;
    //   dereference once.
    //  Strings: engine passes the pointer directly (NUL-terminated).
    //  Binary / GUID: engine passes the pointer directly (fixed
    //   length).
    public static string VaArgUnpack(TraceField f)
    {
        if (IsScalar(f.InType))
        {
            return $"static_cast<{ScalarCType(f.InType)}>( *va_arg( ap, const {ScalarCType(f.InType)}* ) )";
        }
        switch (f.InType)
        {
            case WinType.UnicodeString:
                //  16-bit wchar_t from the engine; convert to UTF-8
                //  before calling the trampoline.
                return "va_arg( ap, const uint16_t* )";
            case WinType.AnsiString:
                return "va_arg( ap, const char* )";
            case WinType.Binary:
            case WinType.Guid:
                return "va_arg( ap, const uint8_t* )";
        }
        throw new InvalidOperationException($"unhandled type for va_arg: {f.InType}");
    }

    //  ctf_* macro line for this field inside a TRACEPOINT_EVENT.
    public static string CtfMacro(TraceField f) => f.InType switch
    {
        WinType.UInt8 => $"ctf_integer( uint8_t, {f.Name}, {f.Name} )",
        WinType.UInt16 => $"ctf_integer( uint16_t, {f.Name}, {f.Name} )",
        WinType.UInt32 => $"ctf_integer( uint32_t, {f.Name}, {f.Name} )",
        WinType.UInt64 => $"ctf_integer( uint64_t, {f.Name}, {f.Name} )",
        WinType.Int32 => $"ctf_integer( int32_t, {f.Name}, {f.Name} )",
        WinType.Int64 => $"ctf_integer( int64_t, {f.Name}, {f.Name} )",
        WinType.Double => $"ctf_float( double, {f.Name}, {f.Name} )",
        WinType.Pointer => $"ctf_integer_hex( uintptr_t, {f.Name}, {f.Name} )",
        WinType.UnicodeString or WinType.AnsiString => $"ctf_string( {f.Name}, {f.Name} != nullptr ? {f.Name} : \"\" )",
        WinType.Guid => $"ctf_array_hex( uint8_t, {f.Name}, {f.Name}, 16 )",
        WinType.Binary => $"ctf_array_hex( uint8_t, {f.Name}, {f.Name}, {f.BinaryLength} )",
        _ => throw new InvalidOperationException($"unhandled CTF type: {f.InType}"),
    };

    //  Format specifier used when packing an overflow field into the
    //  text blob.  Each entry becomes "name=<formatted-value>".
    public static string OverflowFormatSpec(TraceField f) => f.InType switch
    {
        WinType.UInt8 or WinType.UInt16 or WinType.UInt32 => "%u",
        WinType.UInt64 => "%llu",
        WinType.Int32 => "%d",
        WinType.Int64 => "%lld",
        WinType.Double => "%g",
        WinType.Pointer => "0x%lx",
        WinType.UnicodeString or WinType.AnsiString => "%s",
        WinType.Guid or WinType.Binary => "(blob)",
        _ => throw new InvalidOperationException($"unhandled overflow type: {f.InType}"),
    };

    //  C expression to feed snprintf for an overflow field.  The
    //  caller has already unpacked the va_arg into `arg_<name>` and
    //  (for UnicodeString) UTF-8-converted into the same local.
    public static string OverflowFormatArg(TraceField f) => f.InType switch
    {
        WinType.UInt64 => $"(unsigned long long)arg_{f.Name}",
        WinType.Int64 => $"(long long)arg_{f.Name}",
        WinType.Pointer => $"(unsigned long)arg_{f.Name}",
        WinType.Guid or WinType.Binary => "",   // emitted as the literal "(blob)" — no arg.
        _ => $"arg_{f.Name}",
    };
}
