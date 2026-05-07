namespace Mc;

internal static class McParser
{
    public static McFile Parse(string source, string sourcePath)
    {
        var file = new McFile();
        var lines = SplitLines(source);
        int i = 0;
        uint lastId = 0;
        bool inHeader = true;

        while (i < lines.Length)
        {
            string raw = lines[i];
            string line = raw.TrimEnd('\r');
            string trimmed = line.TrimStart();

            if (trimmed.Length == 0 || trimmed.StartsWith(';'))
            {
                i++;
                continue;
            }

            if (TryMatchKey(line, "MessageIdTypedef", out var typedef))
            {
                if (!inHeader) throw Err(sourcePath, i, "MessageIdTypedef must appear before any MessageId block");
                file.MessageIdTypedef = typedef.Trim();
                i++;
                continue;
            }

            if (TryMatchKey(line, "LanguageNames", out var langSpec))
            {
                if (!inHeader) throw Err(sourcePath, i, "LanguageNames must appear before any MessageId block");
                file.Languages.Add(ParseLanguage(langSpec, sourcePath, i));
                i++;
                continue;
            }

            if (TryMatchKey(line, "OutputBase", out _) ||
                TryMatchKey(line, "SeverityNames", out _) ||
                TryMatchKey(line, "FacilityNames", out _))
            {
                if (!inHeader) throw Err(sourcePath, i, "Header directive must appear before any MessageId block");
                i++;
                continue;
            }

            if (TryMatchKey(line, "MessageId", out var idText))
            {
                inHeader = false;
                uint id = ParseMessageId(idText, lastId, sourcePath, i);
                lastId = id;
                var msg = ParseMessageBody(id, lines, ref i, sourcePath);
                file.Messages.Add(msg);
                continue;
            }

            throw Err(sourcePath, i, $"unexpected line: {Truncate(line, 80)}");
        }

        return file;
    }

    private static McMessage ParseMessageBody(uint id, string[] lines, ref int i, string sourcePath)
    {
        int startLine = i;
        i++;
        string? symbolic = null;
        string? severity = null;
        string? facility = null;
        var bodies = new Dictionary<string, List<string>>();

        while (i < lines.Length)
        {
            string line = lines[i].TrimEnd('\r');
            string trimmed = line.TrimStart();

            if (trimmed.Length == 0 || trimmed.StartsWith(';'))
            {
                i++;
                continue;
            }

            if (TryMatchKey(line, "SymbolicName", out var sym))
            {
                symbolic = sym.Trim();
                i++;
                continue;
            }
            if (TryMatchKey(line, "Severity", out var sev))
            {
                severity = sev.Trim();
                i++;
                continue;
            }
            if (TryMatchKey(line, "Facility", out var fac))
            {
                facility = fac.Trim();
                i++;
                continue;
            }
            if (TryMatchKey(line, "Language", out var langName))
            {
                string lang = langName.Trim();
                i++;
                var body = new List<string>();
                while (i < lines.Length)
                {
                    string bodyLine = lines[i].TrimEnd('\r');
                    if (bodyLine == ".")
                    {
                        i++;
                        break;
                    }
                    body.Add(bodyLine);
                    i++;
                }
                bodies[lang] = body;
                continue;
            }

            break;
        }

        if (symbolic is null) throw Err(sourcePath, startLine, $"MessageId={id} block has no SymbolicName");

        var msg = new McMessage { Id = id, SymbolicName = symbolic, SourceLine = startLine + 1 };
        if (severity is not null) msg.Severity = severity;
        if (facility is not null) msg.Facility = facility;
        foreach (var (k, v) in bodies) msg.Bodies[k] = v;
        return msg;
    }

    private static McLanguage ParseLanguage(string spec, string sourcePath, int lineIdx)
    {
        string s = spec.Trim();
        if (s.StartsWith('(') && s.EndsWith(')')) s = s.Substring(1, s.Length - 2);

        int eq = s.IndexOf('=');
        if (eq < 0) throw Err(sourcePath, lineIdx, $"malformed LanguageNames: {spec}");
        string name = s[..eq].Trim();
        string rest = s[(eq + 1)..].Trim();
        int colon = rest.IndexOf(':');
        string idPart = colon < 0 ? rest : rest[..colon];
        string fileBase = colon < 0 ? name : rest[(colon + 1)..].Trim();

        if (!TryParseUInt(idPart.Trim(), out uint langId))
            throw Err(sourcePath, lineIdx, $"malformed language id: {idPart}");

        return new McLanguage(name, langId, fileBase);
    }

    private static uint ParseMessageId(string text, uint lastId, string sourcePath, int lineIdx)
    {
        string t = text.Trim();
        if (t.Length == 0) return lastId + 1;
        if (TryParseUInt(t, out uint v)) return v;
        throw Err(sourcePath, lineIdx, $"malformed MessageId: {text}");
    }

    private static bool TryParseUInt(string s, out uint value)
    {
        if (s.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
            return uint.TryParse(s.AsSpan(2), System.Globalization.NumberStyles.HexNumber, System.Globalization.CultureInfo.InvariantCulture, out value);
        return uint.TryParse(s, System.Globalization.NumberStyles.Integer, System.Globalization.CultureInfo.InvariantCulture, out value);
    }

    private static bool TryMatchKey(string line, string key, out string value)
    {
        if (line.Length < key.Length + 1 || !line.StartsWith(key, StringComparison.Ordinal))
        {
            value = string.Empty;
            return false;
        }
        int idx = key.Length;
        while (idx < line.Length && (line[idx] == ' ' || line[idx] == '\t')) idx++;
        if (idx >= line.Length || line[idx] != '=')
        {
            value = string.Empty;
            return false;
        }
        value = line[(idx + 1)..];
        return true;
    }

    private static string[] SplitLines(string source)
        => source.Split('\n');

    private static McException Err(string path, int lineIdx, string message)
        => new($"{path}:{lineIdx + 1}: {message}");

    private static string Truncate(string s, int max)
        => s.Length <= max ? s : s[..max] + "…";
}
