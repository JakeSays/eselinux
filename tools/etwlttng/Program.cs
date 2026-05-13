namespace EtwLttng;

internal static class Program
{
    private static int Main(string[] args)
    {
        try
        {
            if (args.Length != 2)
            {
                Console.Error.WriteLine("usage: etwlttng <EseEtwEventsPregen.txt> <output-dir>");
                return 1;
            }
            var input = args[0];
            var outDir = args[1];

            var source = File.ReadAllText(input);
            var events = Parser.Parse(source);
            Emitter.Emit(events, outDir);
            Console.Out.WriteLine($"etwlttng: emitted {events.Count} tracepoints to {outDir}");
            return 0;
        }
        catch (IOException ex)
        {
            Console.Error.WriteLine($"etwlttng: I/O error: {ex.Message}");
            return 2;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"etwlttng: {ex.Message}");
            return 3;
        }
    }
}
