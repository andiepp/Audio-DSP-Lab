using System.Globalization;
using System.Text;
using AudioDspLab.Protocol;

namespace AudioDspLab.Receiver;

/// <summary>
/// What "! decoder" and the answers of "rtty" and "fax" report (design/rtty-fax.md):
/// rtty quality afc normal|reverse good bad; fax lines afc start stop phasing|lines phase;
/// off; no-memory.
/// </summary>
public sealed record DecoderStatus(string Kind, double Quality, double AfcHz, bool Reverse, long Good, long Bad,
    long Lines, double StartTone, double StopTone, bool Phasing, int Phase)
{
    public static readonly DecoderStatus Off = new("off", 0, 0, false, 0, 0, 0, 0, 0, false, -1);

    public bool IsRtty => Kind == "rtty";

    public bool IsFax => Kind == "fax";

    public static DecoderStatus Parse(IReadOnlyList<string> v)
    {
        double D(int i) => i < v.Count ? double.Parse(v[i], NumberStyles.Float, CultureInfo.InvariantCulture) : 0;
        long L(int i) => i < v.Count ? long.Parse(v[i], NumberStyles.Integer, CultureInfo.InvariantCulture) : 0;
        if (v.Count == 0)
        {
            return Off;
        }

        return v[0] switch
        {
            "rtty" when v.Count >= 6 => new DecoderStatus("rtty", D(1), D(2), v[3] == "reverse", L(4), L(5), 0, 0, 0, false, -1),
            "fax" when v.Count >= 7 => new DecoderStatus("fax", 0, D(2), false, 0, 0, L(1), D(3), D(4), v[5] == "phasing", (int)L(6)),
            _ => Off with { Kind = v[0] },
        };
    }
}

/// <summary>A fax line from the board: its number and pixels (0 black .. 255 white).</summary>
public sealed record FaxLine(long Number, byte[] Pixels);

/// <summary>
/// Puts the parts of "! fax line n part parts width data" together and
/// decodes them: a hex digit is a pixel's upper four bits, a letter g..z
/// repeats the pixel before it 1..20 times more.
/// </summary>
public sealed class FaxLineAssembler
{
    private long number = -1;
    private int width;
    private readonly SortedDictionary<int, string> parts = [];
    private int expected;

    /// <summary>The values of a "fax line" event (after "line"); a complete line, or null.</summary>
    public FaxLine? Add(IReadOnlyList<string> v)
    {
        long n = long.Parse(v[0], CultureInfo.InvariantCulture);
        int part = int.Parse(v[1], CultureInfo.InvariantCulture);
        int count = int.Parse(v[2], CultureInfo.InvariantCulture);
        int w = int.Parse(v[3], CultureInfo.InvariantCulture);
        string data = v.Count > 4 ? v[4] : string.Empty;
        if (n != number)
        {
            number = n;
            width = w;
            expected = count;
            parts.Clear();
        }

        parts[part] = data;
        if (parts.Count < expected)
        {
            return null;
        }

        var text = new StringBuilder();
        foreach (string piece in parts.Values)
        {
            text.Append(piece);
        }

        parts.Clear();
        number = -1;
        return new FaxLine(n, Decode(text.ToString(), width));
    }

    public static byte[] Decode(string text, int width)
    {
        var pixels = new byte[width];
        int x = 0;
        byte last = 128;
        foreach (char c in text)
        {
            if (x >= width)
            {
                break;
            }

            if (c is >= '0' and <= '9' or >= 'a' and <= 'f')
            {
                int level = c <= '9' ? c - '0' : c - 'a' + 10;
                last = (byte)(level * 17);
                pixels[x++] = last;
            }
            else if (c is >= 'g' and <= 'z')
            {
                for (int k = 0; k < c - 'f' && x < width; k++)
                {
                    pixels[x++] = last;
                }
            }
        }

        for (; x < width; x++)
        {
            pixels[x] = last;
        }

        return pixels;
    }

    /// <summary>The board's encoding, for the simulator and the tests.</summary>
    public static string Encode(IReadOnlyList<byte> pixels)
    {
        const string hex = "0123456789abcdef";
        var text = new StringBuilder();
        int last = -1, run = 0;
        foreach (byte p in pixels)
        {
            int level = p >> 4;
            if (level == last)
            {
                if (++run == 20)
                {
                    text.Append('z');      // a full run: the next letter repeats the same pixel again
                    run = 0;
                }

                continue;
            }

            if (run > 0)
            {
                text.Append((char)('f' + run));
            }

            run = 0;
            text.Append(hex[level]);
            last = level;
        }

        if (run > 0)
        {
            text.Append((char)('f' + run));
        }

        return text.ToString();
    }
}

/// <summary>
/// A weather chart as it arrives: lines of pixels, at most <see cref="MaxLines"/>;
/// a click on a column makes it the left edge (for this and every later line).
/// </summary>
public sealed class FaxChart
{
    public const int MaxLines = 2000;
    private readonly List<byte[]> lines = [];
    private int rotation;

    public FaxChart(DateTime started)
    {
        Started = started;
    }

    public DateTime Started { get; }

    public int Width { get; private set; }

    public int Height => lines.Count;

    public bool IsFull => lines.Count >= MaxLines;

    public void Add(byte[] pixels)
    {
        if (IsFull)
        {
            return;
        }

        if (Width == 0)
        {
            Width = pixels.Length;
        }

        lines.Add(Rotate(pixels));
    }

    /// <summary>The column that becomes the left edge, for the lines so far and those to come.</summary>
    public void SetLeftEdge(int column)
    {
        if (Width == 0)
        {
            return;
        }

        int shift = ((column % Width) + Width) % Width;
        for (int i = 0; i < lines.Count; i++)
        {
            byte[] line = lines[i];
            var turned = new byte[line.Length];
            for (int x = 0; x < line.Length; x++)
            {
                turned[x] = line[(x + shift) % line.Length];
            }

            lines[i] = turned;
        }

        rotation = (rotation + shift) % Width;
    }

    /// <summary>The chart as 8-bit grey, line after line (Width x Height bytes).</summary>
    public byte[] ToGray8()
    {
        var pixels = new byte[Width * lines.Count];
        for (int y = 0; y < lines.Count; y++)
        {
            Array.Copy(lines[y], 0, pixels, y * Width, Math.Min(Width, lines[y].Length));
        }

        return pixels;
    }

    private byte[] Rotate(byte[] pixels)
    {
        if (rotation == 0 || pixels.Length != Width)
        {
            return pixels;
        }

        var turned = new byte[pixels.Length];
        for (int x = 0; x < pixels.Length; x++)
        {
            turned[x] = pixels[(x + rotation) % pixels.Length];
        }

        return turned;
    }
}

/// <summary>
/// Cuts telex messages out of a stream of characters: a message begins with
/// "ZCZC" and ends with "NNNN" (the framing DWD and other weather services
/// use), or after <see cref="MaxLength"/> characters (the SDR-IQ Receiver's).
/// </summary>
public sealed class TelexMessages
{
    private readonly StringBuilder line = new();
    private StringBuilder? message;

    public int MaxLength { get; set; } = 20_000;

    public bool InMessage => message is not null;

    /// <summary>A complete message, from "ZCZC" to "NNNN" inclusive.</summary>
    public event Action<string>? Message;

    public void Add(string text)
    {
        foreach (char c in text)
        {
            Add(c);
        }
    }

    public void Add(char c)
    {
        if (c == '\n')
        {
            line.Clear();
        }
        else
        {
            line.Append(c);
        }

        if (message is null)
        {
            if (EndsWith(line, "ZCZC"))
            {
                message = new StringBuilder("ZCZC");
            }

            return;
        }

        message.Append(c);
        if (EndsWith(line, "NNNN") || message.Length > MaxLength)
        {
            string done = message.ToString();
            message = null;
            Message?.Invoke(done);
        }
    }

    /// <summary>The first line after "ZCZC" with some content: the message's title.</summary>
    public static string Title(string message)
    {
        string[] lines = message.Split('\n', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries);
        string title = lines.Skip(1).FirstOrDefault(l => l.Length > 3 && l != "NNNN") ?? lines.FirstOrDefault() ?? "message";
        return title.Length > 60 ? title[..60] : title;
    }

    private static bool EndsWith(StringBuilder text, string tail)
    {
        if (text.Length < tail.Length)
        {
            return false;
        }

        for (int i = 0; i < tail.Length; i++)
        {
            if (text[text.Length - tail.Length + i] != tail[i])
            {
                return false;
            }
        }

        return true;
    }
}

/// <summary>The decoders' requests on a <see cref="BoardClient"/>.</summary>
public static class DecoderRequests
{
    /// <summary>"rtty": the text kept on the board (lines, the last one perhaps unfinished) and the status.</summary>
    public static async Task<(string Text, DecoderStatus Status)> GetRttyAsync(this BoardClient board, CancellationToken cancellation = default)
    {
        BoardResponse response = await board.SendAsync("rtty", cancellation).ConfigureAwait(false);
        var text = new StringBuilder();
        foreach (IReadOnlyList<string> line in response.Data)
        {
            if (line.Count >= 1)
            {
                text.Append(line[0]);
                if (line.Count >= 2 && line[1] == "1")
                {
                    text.Append('\n');
                }
            }
        }

        return (text.ToString(), DecoderStatus.Parse([.. response.Values.Skip(1)]));
    }

    /// <summary>"fax events": fax lines to this connection, at a width (0 none).</summary>
    public static Task SetFaxEventsAsync(this BoardClient board, int width, CancellationToken cancellation = default) =>
        board.SendAsync(width <= 0 ? "fax events off" : string.Create(CultureInfo.InvariantCulture, $"fax events {width}"), cancellation);
}
