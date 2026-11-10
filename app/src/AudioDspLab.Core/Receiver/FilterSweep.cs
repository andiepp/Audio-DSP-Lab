namespace AudioDspLab.Receiver;

/// <summary>One measured point: baseband offset (Hz re the carrier) and gain (dB re the test tone).</summary>
public readonly record struct FilterPoint(double Hz, double GainDb);

/// <summary>
/// Measures the channel filter on the board (ported from DSP-Box-Lab): the
/// internal test tone steps across the band and the S-meter reads the level
/// after the filter.
/// </summary>
/// <remarks>
/// The S-meter is the AGC's peak detector: it rises at once but falls only
/// after the hang time at the decay rate, whether the AGC is on or off. The
/// sweep therefore sets the AGC off, hang 0 and decay 200 dB/s, and reads each
/// point until two readings agree. In CW the down-converter is tuned to
/// offset - pitch, so a tone at f lands at f + pitch. Every setting it touches
/// is restored afterwards, also on cancel or error.
/// </remarks>
public static class FilterSweep
{
    public const double ToneLevel = -10;

    private static readonly string[] Touched = ["test", "test-offset", "test-level", "test-noise", "test-key", "agc", "hang", "decay"];

    public static IReadOnlyList<double> Frequencies(double from = -5900, double to = 5900, double step = 100)
    {
        var list = new List<double>();
        for (double f = from; f <= to + 1e-9; f += step)
        {
            list.Add(Math.Round(f, 3));
        }

        return list;
    }

    public static async Task<IReadOnlyList<FilterPoint>> MeasureAsync(
        BoardClient board,
        IReadOnlyList<double> frequencies,
        IProgress<FilterPoint>? progress = null,
        CancellationToken cancellation = default,
        double toneLevel = ToneLevel)
    {
        var before = new Dictionary<string, string>();
        foreach (string p in Touched)
        {
            before[p] = await board.GetAsync(p, cancellation).ConfigureAwait(false);
        }

        string mode = await board.GetAsync("mode", cancellation).ConfigureAwait(false);
        double shift = mode == "cw" ? await board.GetNumberAsync("pitch", cancellation).ConfigureAwait(false) : 0;

        var points = new List<FilterPoint>();
        try
        {
            await board.SetAsync("agc", "off", cancellation).ConfigureAwait(false);
            await board.SetAsync("hang", 0, cancellation).ConfigureAwait(false);
            await board.SetAsync("decay", 200, cancellation).ConfigureAwait(false);
            await board.SetAsync("test-level", toneLevel, cancellation).ConfigureAwait(false);
            await board.SetAsync("test-noise", -150, cancellation).ConfigureAwait(false);   // a steady, clean tone
            await board.SetAsync("test-key", 0, cancellation).ConfigureAwait(false);
            await board.SetAsync("test", true, cancellation).ConfigureAwait(false);

            foreach (double f in frequencies)
            {
                cancellation.ThrowIfCancellationRequested();
                await board.SetAsync("test-offset", Math.Round(f - shift), cancellation).ConfigureAwait(false);
                double level = await SettledSMeterAsync(board, cancellation).ConfigureAwait(false);
                var point = new FilterPoint(f, level - toneLevel);
                points.Add(point);
                progress?.Report(point);
            }
        }
        finally
        {
            // restored without the caller's token: a cancelled sweep must not leave the tone on
            foreach (string p in Touched)
            {
                await board.SetAsync(p, before[p]).ConfigureAwait(false);
            }
        }

        return points;
    }

    /// <summary>
    /// Reads the S-meter until two readings in a row agree within 0.3 dB (at
    /// least three readings, so the filter's delay of up to 43 ms has passed);
    /// a falling level takes longer, at 200 dB/s.
    /// </summary>
    public static async Task<double> SettledSMeterAsync(BoardClient board, CancellationToken cancellation)
    {
        double last = double.NaN;
        for (int i = 0; i < 60; i++)
        {
            double now = (await board.GetMeterAsync(cancellation).ConfigureAwait(false)).SMeterDbfs;
            double tolerance = now < -90 ? 1.5 : 0.3;   // deep in the stop band the floor wanders
            if (i >= 2 && Math.Abs(now - last) < tolerance)
            {
                return now;
            }

            last = now;
            await Task.Delay(20, cancellation).ConfigureAwait(false);
        }

        return last;
    }
}
