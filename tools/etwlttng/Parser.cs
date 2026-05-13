using System.Text.RegularExpressions;

namespace EtwLttng;

internal static class Parser
{
    private static readonly Regex ListLine = new(
        @"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*,\s*(\d+)\s*,\s*([A-Za-z_:]+)\s*,\s*""([^""]*)""(.*)$",
        RegexOptions.Compiled);

    private static readonly Regex DefineLine = new(
        @"^\s*Define:\s*([A-Za-z_][A-Za-z0-9_]*)\s*$",
        RegexOptions.Compiled);

    private static readonly Regex DataLine = new(
        @"^\s*<data\b([^/]*)/>\s*$",
        RegexOptions.Compiled);

    private static readonly Regex AttrPattern = new(
        @"(\w+)\s*=\s*""([^""]*)""",
        RegexOptions.Compiled);

    public static IReadOnlyList<TraceEvent> Parse(string source)
    {
        var lines = source.Split('\n');

        var entries = new List<(string Name, int Id, string Level, string Keywords, bool Deprecated)>();
        var fieldsByName = new Dictionary<string, List<TraceField>>(StringComparer.Ordinal);

        var state = ParseState.Header;
        string? currentDefine = null;

        foreach (var raw in lines)
        {
            var line = raw.TrimEnd('\r');
            var trimmed = line.TrimStart();
            if (trimmed.StartsWith("//", StringComparison.Ordinal))
            {
                continue;
            }
            if (line.StartsWith("START_TRACE_LIST:", StringComparison.Ordinal))
            {
                state = ParseState.List;
                continue;
            }
            if (line.StartsWith("START_TRACE_DEFNS:", StringComparison.Ordinal))
            {
                state = ParseState.Defns;
                continue;
            }
            if (string.IsNullOrWhiteSpace(line))
            {
                continue;
            }

            if (state == ParseState.List)
            {
                var match = ListLine.Match(line);
                if (!match.Success)
                {
                    continue;
                }
                var name = match.Groups[1].Value;
                var id = int.Parse(match.Groups[2].Value, System.Globalization.CultureInfo.InvariantCulture);
                var level = match.Groups[3].Value;
                var keywords = match.Groups[4].Value;
                var rest = match.Groups[5].Value;
                var deprecated = rest.Contains("fDeprecated", StringComparison.Ordinal);
                entries.Add((name, id, level, keywords, deprecated));
            }
            else if (state == ParseState.Defns)
            {
                var defMatch = DefineLine.Match(line);
                if (defMatch.Success)
                {
                    currentDefine = defMatch.Groups[1].Value;
                    if (!fieldsByName.ContainsKey(currentDefine))
                    {
                        fieldsByName[currentDefine] = new List<TraceField>();
                    }
                    continue;
                }
                if (currentDefine is null)
                {
                    continue;
                }
                var dataMatch = DataLine.Match(line);
                if (!dataMatch.Success)
                {
                    continue;
                }
                var attrs = new Dictionary<string, string>(StringComparer.Ordinal);
                foreach (Match m in AttrPattern.Matches(dataMatch.Groups[1].Value))
                {
                    attrs[m.Groups[1].Value] = m.Groups[2].Value;
                }
                if (!attrs.TryGetValue("inType", out var inTypeStr)
                    || !attrs.TryGetValue("name", out var fieldName))
                {
                    continue;
                }
                var inType = ParseInType(inTypeStr);
                var binaryLength = 0;
                if (inType == WinType.Binary && attrs.TryGetValue("length", out var lenStr))
                {
                    binaryLength = int.Parse(lenStr, System.Globalization.CultureInfo.InvariantCulture);
                }
                fieldsByName[currentDefine].Add(new TraceField(fieldName, inType, binaryLength));
            }
        }

        var events = new List<TraceEvent>(entries.Count);
        foreach (var entry in entries)
        {
            var fields = fieldsByName.TryGetValue(entry.Name, out var list)
                ? (IReadOnlyList<TraceField>)list
                : Array.Empty<TraceField>();
            events.Add(new TraceEvent(entry.Name, entry.Id, entry.Level, entry.Keywords, entry.Deprecated, fields));
        }
        return events;
    }

    private static WinType ParseInType(string raw) => raw switch
    {
        "win:UInt8" => WinType.UInt8,
        "win:UInt16" => WinType.UInt16,
        "win:UInt32" => WinType.UInt32,
        "win:UInt64" => WinType.UInt64,
        "win:Int32" => WinType.Int32,
        "win:Int64" => WinType.Int64,
        "win:Double" => WinType.Double,
        "win:Pointer" => WinType.Pointer,
        "win:GUID" => WinType.Guid,
        "win:UnicodeString" => WinType.UnicodeString,
        "win:AnsiString" => WinType.AnsiString,
        "win:Binary" => WinType.Binary,
        _ => throw new InvalidOperationException($"unknown inType: {raw}"),
    };

    private enum ParseState
    {
        Header,
        List,
        Defns,
    }
}
