using System.Globalization;
using System.Text;

namespace AudioDspLab.Protocol;

/// <summary>
/// The text rules of the protocol (docs/Protocol.md): words separated by
/// spaces, texts in double quotes with <c>\"</c> and <c>\\</c> inside,
/// numbers with a decimal point.
/// </summary>
public static class ProtocolText
{
    /// <summary>The word the board sends for "no value".</summary>
    public const string None = "-";

    /// <summary>
    /// Splits a line into its words; a quoted text is one word without its
    /// quotes. Returns null for an unterminated quote.
    /// </summary>
    public static IReadOnlyList<string>? Tokenize(string line)
    {
        ArgumentNullException.ThrowIfNull(line);
        var tokens = new List<string>();
        int index = 0;
        while (true)
        {
            while (index < line.Length && (line[index] == ' ' || line[index] == '\t'))
            {
                index++;
            }

            if (index >= line.Length)
            {
                return tokens;
            }

            var token = new StringBuilder();
            if (line[index] == '"')
            {
                index++;
                while (true)
                {
                    if (index >= line.Length)
                    {
                        return null;
                    }

                    char c = line[index];
                    if (c == '"')
                    {
                        index++;
                        break;
                    }

                    if (c == '\\' && index + 1 < line.Length && (line[index + 1] == '"' || line[index + 1] == '\\'))
                    {
                        index++;
                        c = line[index];
                    }

                    token.Append(c);
                    index++;
                }
            }
            else
            {
                while (index < line.Length && line[index] != ' ' && line[index] != '\t')
                {
                    token.Append(line[index++]);
                }
            }

            tokens.Add(token.ToString());
        }
    }

    /// <summary>A text in double quotes, quote and backslash escaped, control characters as spaces.</summary>
    public static string Quote(string text)
    {
        ArgumentNullException.ThrowIfNull(text);
        var quoted = new StringBuilder(text.Length + 2);
        quoted.Append('"');
        foreach (char c in text)
        {
            if (c is '"' or '\\')
            {
                quoted.Append('\\').Append(c);
            }
            else if (char.IsControl(c))
            {
                quoted.Append(' ');
            }
            else
            {
                quoted.Append(c);
            }
        }

        return quoted.Append('"').ToString();
    }

    /// <summary>A number as the board writes it: invariant, at most two decimals, no trailing zeros.</summary>
    public static string FormatNumber(double value) =>
        Math.Round(value, 2, MidpointRounding.AwayFromZero).ToString("0.##", CultureInfo.InvariantCulture);

    public static bool TryParseNumber(string? text, out double value) =>
        double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out value) && double.IsFinite(value);

    /// <summary>A number, or null for <c>-</c> and anything that is not one.</summary>
    public static double? ParseNumberOrNone(string? text) =>
        text != None && TryParseNumber(text, out double value) ? value : null;

    public static int ParseInteger(string text) => int.Parse(text, NumberStyles.Integer, CultureInfo.InvariantCulture);

    /// <summary>on/off, 1/0, true/false.</summary>
    public static bool ParseSwitch(string text) =>
        text.ToLowerInvariant() switch
        {
            "on" or "1" or "true" or "yes" => true,
            "off" or "0" or "false" or "no" => false,
            _ => throw new FormatException($"'{text}' is not on or off.")
        };

    public static string FormatSwitch(bool on) => on ? "on" : "off";
}
