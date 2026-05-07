namespace Mc;

internal sealed class McFile
{
    public string MessageIdTypedef { get; set; } = "ULONG";
    public List<McLanguage> Languages { get; } = new();
    public List<McMessage> Messages { get; } = new();
}

internal sealed record McLanguage(string Name, uint LangId, string FileBase);

internal sealed class McMessage
{
    public required uint Id { get; init; }
    public required string SymbolicName { get; init; }
    public string? Severity { get; set; }
    public string? Facility { get; set; }
    public Dictionary<string, List<string>> Bodies { get; } = new();
    public int SourceLine { get; init; }
}
