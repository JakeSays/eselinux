namespace Mc;

internal sealed class CliOptions
{
    public required string Input { get; init; }
    public required string HeaderDir { get; init; }
    public required string RcDir { get; init; }
    public string? CTableDir { get; init; }
    public bool Verbose { get; init; }
    public bool ManifestMode { get; init; }

    public static CliOptions? Parse(string[] args)
    {
        if (args.Length == 0 || args is ["-h" or "--help"])
        {
            PrintUsage();
            return null;
        }

        string? input = null;
        string? headerDir = null;
        string? rcDir = null;
        string? cTableDir = null;
        bool verbose = false;
        bool manifest = false;

        for (int i = 0; i < args.Length; i++)
        {
            string a = args[i];
            switch (a)
            {
                case "-v":
                    verbose = true;
                    break;
                case "-um":
                    manifest = true;
                    if (i + 1 >= args.Length) throw new McException("-um requires a manifest path");
                    input = args[++i];
                    break;
                case "-h":
                    if (i + 1 >= args.Length) throw new McException("-h requires a directory");
                    headerDir = args[++i];
                    break;
                case "-r":
                    if (i + 1 >= args.Length) throw new McException("-r requires a directory");
                    rcDir = args[++i];
                    break;
                case "-c":
                    if (i + 1 >= args.Length) throw new McException("-c requires a directory");
                    cTableDir = args[++i];
                    break;
                default:
                    if (a.StartsWith('-'))
                    {
                        Console.Error.WriteLine($"mc: ignoring unsupported flag '{a}'");
                        break;
                    }
                    if (input is not null && !manifest) throw new McException($"unexpected positional argument '{a}'");
                    input ??= a;
                    break;
            }
        }

        if (input is null) throw new McException("missing input file");
        headerDir ??= ".";
        rcDir ??= ".";

        Directory.CreateDirectory(headerDir);
        Directory.CreateDirectory(rcDir);
        if (cTableDir is not null)
        {
            Directory.CreateDirectory(cTableDir);
        }

        return new CliOptions
        {
            Input = input,
            HeaderDir = headerDir,
            RcDir = rcDir,
            CTableDir = cTableDir,
            Verbose = verbose,
            ManifestMode = manifest,
        };
    }

    private static void PrintUsage()
    {
        Console.Out.WriteLine("""
            mc - Linux replacement for Microsoft Message Compiler

            Usage:
              mc [-v] -h <header-dir> -r <rc-dir> [-c <ctable-dir>] <input.mc>
              mc -um <input.man> -h <header-dir> -r <rc-dir>     (stub only)

            Options:
              -h <dir>   Directory for emitted .h header
              -r <dir>   Directory for emitted .rc resource stub
              -c <dir>   Directory for emitted C++ message-table source
                         (<stem>_table.cxx, used by FormatMessageW on Linux)
              -v         Verbose
              -um <man>  ETW manifest mode (NOT IMPLEMENTED — emits empty stubs)

            Reads the classic Microsoft Message Compiler text format and emits a C
            header containing #define for each SymbolicName, plus an (empty) .rc
            stub for build-system compatibility.  With -c, also emits a C++ source
            file containing a sorted const array of { id, template } pairs that
            the Linux port's FormatMessageW(FROM_HMODULE) implementation looks up
            at runtime.
            """);
    }
}

internal sealed class McException(string message) : Exception(message);
