using System.Globalization;
using System.Text.RegularExpressions;
using AudioDspLab.Simulation;

namespace AudioDspLab.Tests;

/// <summary>
/// Reads the firmware's own tables (Applications.cpp, ParamInfo.cpp) and
/// checks that the simulated board serves the same parameters: names,
/// kinds, ranges, defaults, units, choices and pots.
/// </summary>
public partial class FirmwareTableTests
{
    private static readonly string FirmwareDirectory = Path.Combine(RepositoryRoot(), "firmware", "AudioDspLab");

    public static TheoryData<string, string, string, string> Applications => new()
    {
        { AudioDspLab.Applications.Generator, "generatorInfo", "generatorParams", "generatorPots" },
        { AudioDspLab.Applications.Equalizer, "equalizerInfo", "equalizerParams", "equalizerPots" },
        { AudioDspLab.Applications.Radio, "equalizerInfo", "radioParams", "equalizerPots" },
        { AudioDspLab.Applications.Analyzer, "analyzerInfo", "analysisParams", "analysisPots" },
        { AudioDspLab.Applications.Synthesizer, "synthInfo", "synthParams", "synthPots" },
        { AudioDspLab.Applications.Receiver, "receiverInfo", "receiverParams", "receiverPots" },
    };

    [Theory]
    [MemberData(nameof(Applications))]
    public void TheSimulatorServesTheFirmwaresParameters(string application, string infoTable, string paramTable, string potTable)
    {
        string applications = File.ReadAllText(Path.Combine(FirmwareDirectory, "Applications.cpp"));
        string paramInfo = File.ReadAllText(Path.Combine(FirmwareDirectory, "ParamInfo.cpp"));
        Dictionary<string, string> symbols = Defines(applications);
        string header = File.ReadAllText(Path.Combine(FirmwareDirectory, "Applications.h"));
        string chain = File.ReadAllText(Path.Combine(FirmwareDirectory, "RxChain.h"));
        foreach ((string key, string value) in Defines(paramInfo).Concat(Defines(header)).Concat(Defines(chain)))
        {
            symbols[key] = value;
        }

        // enum values the tables use (Generator.h, Synth.h, Radio.h)
        symbols["GENERATOR_SINE"] = "0";
        symbols["GENERATOR_WAVEFORM_LAST"] = "5";
        symbols["SYNTH_SAWTOOTH"] = "1";
        symbols["SYNTH_WAVEFORM_LAST"] = "3";
        symbols["RADIO_STATION_LAST"] = "7";
        // RxChain.h's enums
        symbols["RX_USB"] = "2";
        symbols["RX_FM"] = "5";
        symbols["RX_INPUT_IQ"] = "0";
        symbols["RX_NR_LMS"] = "1";
        symbols["RX_NR_SPECTRAL"] = "2";

        var info = Rows(paramInfo, infoTable).ToDictionary(row => int.Parse(row[0], CultureInfo.InvariantCulture));
        var pots = Rows(applications, potTable).ToDictionary(row => Resolve(row[1], symbols), row => $"P{int.Parse(row[0], CultureInfo.InvariantCulture) + 1}");
        IReadOnlyList<SimulatedParameter> simulated = SimulatedParameters.For(application);
        List<string[]> firmware = [.. Rows(applications, paramTable)];

        Assert.Equal(firmware.Count, simulated.Count);
        for (int index = 0; index < firmware.Count; index++)
        {
            string[] row = firmware[index];
            int id = Resolve(row[0], symbols);
            string[] names = info[id];
            string kind = names[2];
            double scale = kind == "KIND_NUMBER" ? Math.Pow(10, int.Parse(names[3], CultureInfo.InvariantCulture)) : 1;
            SimulatedParameter parameter = simulated[index];

            Assert.Equal(Unquote(names[1]), parameter.Name);
            Assert.Equal(kind, "KIND_" + parameter.Kind.ToString().ToUpperInvariant());
            Assert.Equal(Resolve(row[1], symbols) / scale, parameter.Minimum, 6);
            Assert.Equal(Resolve(row[2], symbols) / scale, parameter.Maximum, 6);
            Assert.Equal(Resolve(row[3], symbols) / scale, parameter.Default, 6);
            Assert.Equal(Unquote(names[4]), parameter.Unit);
            string choices = names[5] == "nullptr" ? string.Empty : Unquote(symbols.GetValueOrDefault(names[5], names[5]));
            Assert.Equal(choices, parameter.Choices is null ? string.Empty : string.Join(',', parameter.Choices));
            Assert.Equal(pots.GetValueOrDefault(id), parameter.Pot);
        }
    }

    private static string RepositoryRoot()
    {
        string? directory = AppContext.BaseDirectory;
        while (directory is not null && !Directory.Exists(Path.Combine(directory, "firmware", "AudioDspLab")))
        {
            directory = Path.GetDirectoryName(directory);
        }

        return directory ?? throw new DirectoryNotFoundException("The repository's firmware folder was not found.");
    }

    private static Dictionary<string, string> Defines(string source) =>
        DefinePattern().Matches(source).ToDictionary(match => match.Groups[1].Value, match => match.Groups[2].Value.Trim());

    // the rows of a static table: "static const T name[] = { { a, b, ... }, ... };"
    private static IEnumerable<string[]> Rows(string source, string table)
    {
        int start = source.IndexOf($" {table}[] = {{", StringComparison.Ordinal);
        Assert.True(start >= 0, $"table {table} not found");
        int end = source.IndexOf("};", start, StringComparison.Ordinal);
        foreach (Match row in RowPattern().Matches(source[start..end]))
        {
            yield return [.. SplitRow(row.Groups[1].Value)];
        }
    }

    private static IEnumerable<string> SplitRow(string row)
    {
        int depth = 0;
        int begin = 0;
        bool quoted = false;
        for (int i = 0; i < row.Length; i++)
        {
            char c = row[i];
            if (c == '"')
            {
                quoted = !quoted;
            }
            else if (!quoted && c == '(')
            {
                depth++;
            }
            else if (!quoted && c == ')')
            {
                depth--;
            }
            else if (!quoted && depth == 0 && c == ',')
            {
                yield return row[begin..i].Trim();
                begin = i + 1;
            }
        }

        yield return row[begin..].Trim();
    }

    // a token, or a simple expression of them: unary minus, a + b, a - b, a * b
    private static int Resolve(string token, IReadOnlyDictionary<string, string> symbols)
    {
        string text = token.Trim();
        foreach (string op in new[] { " + ", " - " })
        {
            int at = text.LastIndexOf(op, StringComparison.Ordinal);
            if (at > 0)
            {
                int left = Resolve(text[..at], symbols), right = Resolve(text[(at + 3)..], symbols);
                return op == " + " ? left + right : left - right;
            }
        }

        int times = text.IndexOf(" * ", StringComparison.Ordinal);
        if (times > 0)
        {
            return Resolve(text[..times], symbols) * Resolve(text[(times + 3)..], symbols);
        }

        if (text.StartsWith('-') && !int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out _))
        {
            return -Resolve(text[1..], symbols);
        }

        return ResolveSymbol(text, symbols);
    }

    private static int ResolveSymbol(string token, IReadOnlyDictionary<string, string> symbols)
    {
        string value = token;
        for (int i = 0; i < 4 && !int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out _); i++)
        {
            value = symbols.TryGetValue(value, out string? next)
                ? CommentPattern().Replace(next, string.Empty).Trim()
                : throw new KeyNotFoundException($"'{token}' is not defined");
        }

        return int.Parse(value, CultureInfo.InvariantCulture);
    }

    private static string Unquote(string text) => text.Trim().Trim('"');

    [GeneratedRegex(@"^#define\s+(\w+)\s+([^\r\n]*?)\s*(?://.*)?$", RegexOptions.Multiline)]
    private static partial Regex DefinePattern();

    [GeneratedRegex(@"\{\s*([^{}]+?)\s*\}")]
    private static partial Regex RowPattern();

    [GeneratedRegex(@"//.*$")]
    private static partial Regex CommentPattern();
}
