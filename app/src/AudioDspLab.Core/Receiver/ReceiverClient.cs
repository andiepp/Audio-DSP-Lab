using System.Buffers.Binary;
using System.Globalization;
using System.Numerics;
using AudioDspLab.Protocol;

namespace AudioDspLab.Receiver;

/// <summary>What "meter" and "! meter" report (design/receiver.md, section 7).</summary>
public sealed record ReceiverMeter(double SMeterDbfs, double AgcGainDb, double FmOffsetHz, bool SquelchOpen, double AudioDbfs,
    double CpuPercent, double NrGainDb)
{
    public static ReceiverMeter Parse(IReadOnlyList<string> v)
    {
        double D(int i) => i < v.Count ? double.Parse(v[i], NumberStyles.Float, CultureInfo.InvariantCulture) : 0;
        return new ReceiverMeter(D(0), D(1), D(2), D(3) != 0, D(4), D(5), D(6));
    }
}

/// <summary>What "rxfilter" reports: the filter in force.</summary>
public sealed record ReceiverFilterStatus(bool Downloaded, int Taps, double TransitionHz, bool Designing, int Count)
{
    public static ReceiverFilterStatus Parse(IReadOnlyList<string> v) =>
        new(v[0] == "downloaded", ProtocolText.ParseInteger(v[1]),
            double.Parse(v[2], NumberStyles.Float, CultureInfo.InvariantCulture), v[3] == "designing",
            ProtocolText.ParseInteger(v[4]));
}

/// <summary>A download the receiver refused.</summary>
public sealed class FilterDownloadException(string message, Exception? inner = null) : Exception(message, inner);

/// <summary>The receiver application's own requests, on a <see cref="BoardClient"/>.</summary>
public static class ReceiverRequests
{
    public const string Application = "receiver";

    /// <summary>Values per data line (the firmware takes up to 16).</summary>
    public const int ValuesPerLine = 16;

    public static async Task<ReceiverMeter> GetMeterAsync(this BoardClient board, CancellationToken cancellation = default) =>
        ReceiverMeter.Parse((await board.SendAsync("meter", cancellation).ConfigureAwait(false)).Values);

    public static async Task<ReceiverFilterStatus> GetFilterAsync(this BoardClient board, CancellationToken cancellation = default) =>
        ReceiverFilterStatus.Parse((await board.SendAsync("rxfilter", cancellation).ConfigureAwait(false)).Values);

    public static async Task<double> GetNumberAsync(this BoardClient board, string parameter, CancellationToken cancellation = default) =>
        double.Parse(await board.GetAsync(parameter, cancellation).ConfigureAwait(false), NumberStyles.Float, CultureInfo.InvariantCulture);

    /// <summary>
    /// The taps as the firmware will hold them (float32), complex unless every
    /// imaginary part is zero, and the CRC-32 (IEEE, as zlib's) of their
    /// little-endian bytes in the order sent: re (real) or re, im per tap.
    /// </summary>
    public static (float[] Values, bool IsComplex, uint Crc) Prepare(IReadOnlyList<Complex> h)
    {
        if (h.Count is < 1 or > ChannelFilter.MaxTaps)
        {
            throw new ArgumentException($"A channel filter has 1 .. {ChannelFilter.MaxTaps} taps.", nameof(h));
        }

        bool complex = h.Any(c => c.Imaginary != 0);
        float[] values = complex
            ? [.. h.SelectMany(c => new[] { (float)c.Real, (float)c.Imaginary })]
            : [.. h.Select(c => (float)c.Real)];
        var bytes = new byte[values.Length * 4];
        for (int i = 0; i < values.Length; i++)
        {
            BinaryPrimitives.WriteSingleLittleEndian(bytes.AsSpan(4 * i), values[i]);
        }

        return (values, complex, Crc32(bytes));
    }

    /// <summary>
    /// Downloads a filter (natural order) and switches it in: rxfilter load,
    /// data lines of 16 values ("G9" round-trips a float32 exactly), end with
    /// the CRC. Progress reports the taps sent. A failed or cancelled download
    /// leaves the filter in force.
    /// </summary>
    public static async Task DownloadFilterAsync(this BoardClient board, IReadOnlyList<Complex> h, IProgress<int>? progress = null,
        CancellationToken cancellation = default)
    {
        (float[] values, bool complex, uint crc) = Prepare(h);
        int perTap = complex ? 2 : 1;
        try
        {
            await board.SendAsync(FormattableString.Invariant($"rxfilter load {h.Count} {(complex ? "complex" : "real")}"), cancellation)
                .ConfigureAwait(false);
            for (int i = 0; i < values.Length; i += ValuesPerLine)
            {
                cancellation.ThrowIfCancellationRequested();
                string line = "rxfilter data " + string.Join(' ',
                    values.Skip(i).Take(ValuesPerLine).Select(v => v.ToString("G9", CultureInfo.InvariantCulture)));
                await board.SendAsync(line, cancellation).ConfigureAwait(false);
                progress?.Report(Math.Min(h.Count, (i + ValuesPerLine) / perTap));
            }

            await board.SendAsync(FormattableString.Invariant($"rxfilter end {crc}"), cancellation).ConfigureAwait(false);
        }
        catch (BoardErrorException e)
        {
            throw new FilterDownloadException(e.Code switch
            {
                "count-wrong" => "The receiver counted a different number of taps; the old filter stays.",
                "crc-wrong" => "The CRC does not match; the old filter stays.",
                "busy" => "The receiver is still taking in the last download; try again.",
                "not-loading" => "The download was interrupted (a design replaced it); the old filter stays.",
                _ => $"The receiver refused the download ({e.Code}).",
            }, e);
        }
    }

    /// <summary>Back to the filter the firmware designs from its edges.</summary>
    public static Task<string> DesignAgainAsync(this BoardClient board, CancellationToken cancellation = default) =>
        board.SetAsync("filter", "designed", cancellation);

    internal static uint Crc32(ReadOnlySpan<byte> data)
    {
        uint crc = 0xFFFFFFFF;
        foreach (byte b in data)
        {
            crc ^= b;
            for (int k = 0; k < 8; k++)
            {
                crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xEDB88320 : crc >> 1;
            }
        }

        return ~crc;
    }
}
