namespace Mc;

internal static class Program
{
    private static int Main(string[] args)
    {
        try
        {
            var opts = CliOptions.Parse(args);
            if (opts is null) return 1;

            if (opts.ManifestMode)
            {
                Console.Error.WriteLine($"mc: -um (ETW manifest) mode is not implemented on Linux; emitting empty stubs for {opts.Input}");
                StubEmitter.EmitEmpty(opts);
                return 0;
            }

            var source = File.ReadAllText(opts.Input);
            var file = McParser.Parse(source, opts.Input);
            HeaderEmitter.Emit(file, opts);
            RcEmitter.EmitStub(file, opts);

            if (opts.Verbose)
            {
                Console.Out.WriteLine($"mc: parsed {file.Messages.Count} messages from {opts.Input}");
            }

            return 0;
        }
        catch (McException ex)
        {
            Console.Error.WriteLine($"mc: {ex.Message}");
            return 2;
        }
        catch (IOException ex)
        {
            Console.Error.WriteLine($"mc: I/O error: {ex.Message}");
            return 3;
        }
    }
}
