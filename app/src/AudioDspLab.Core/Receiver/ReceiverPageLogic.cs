using System.Numerics;
using AudioDspLab.FrontEnd;

namespace AudioDspLab.Receiver;

/// <summary>
/// The Receiver page's frequency dial (design/receiver.md, section 8). With
/// the front end connected (and the I/Q input) it is the RF frequency: the
/// front end's frequency plus the board's <c>offset</c>, tuned through
/// <see cref="ReceiverTuner"/>. Without it, it is the <c>offset</c> itself.
/// </summary>
public static class ReceiverDial
{
    public static double Minimum(bool rf) => rf ? FrontEndClient.MinHz + SdrTuner.Centre : -SdrTuner.OffsetLimit;

    public static double Maximum(bool rf) => rf ? FrontEndClient.MaxHz + SdrTuner.Centre : SdrTuner.OffsetLimit;

    /// <summary>What the dial shows for the board's offset: the RF frequency when the front end's is known, else the offset.</summary>
    public static double? Show(double? offsetHz, long? frontEndHz) =>
        offsetHz is not double offset ? null : frontEndHz is long fe ? fe + offset : offset;

    /// <summary>The offset a dial value asks for without a front end.</summary>
    public static int OffsetFor(double dial) => (int)Math.Clamp(Math.Round(dial), -SdrTuner.OffsetLimit, SdrTuner.OffsetLimit);

    /// <summary>The RF frequency a dial value asks for with a front end.</summary>
    public static long RfFor(double dial) => (long)Math.Clamp(Math.Round(dial), Minimum(true), Maximum(true));
}

/// <summary>
/// Runs the newest of quickly following requests, one at a time: while one
/// runs, later ones replace each other and only the last runs afterwards (a
/// spinning dial tunes once per answer, not once per step).
/// </summary>
public sealed class LatestRequest<T>
    where T : struct
{
    private T? pending;
    private bool running;

    public bool IsRunning => running;

    public async Task SubmitAsync(T value, Func<T, Task> run)
    {
        ArgumentNullException.ThrowIfNull(run);
        pending = value;
        if (running)
        {
            return;
        }

        running = true;
        try
        {
            while (pending is T next)
            {
                pending = null;
                await run(next).ConfigureAwait(true);
            }
        }
        finally
        {
            pending = null;
            running = false;
        }
    }
}

/// <summary>The channel filter's edges, Hz re the carrier, as the Filter view's chart edits them.</summary>
public readonly record struct FilterEdges(double Low, double High);

/// <summary>How a measured channel filter compares with the prediction.</summary>
/// <param name="WorstPassbandDb">The largest difference where the prediction is above -3 dB.</param>
/// <param name="MinimumStopDb">The least attenuation beyond one transition from the edges; null without such points.</param>
public sealed record FilterComparison(double WorstPassbandDb, double? MinimumStopDb);

/// <summary>The curves of the Filter and Designer views, computed as the firmware filters.</summary>
public static class ReceiverResponse
{
    public const double From = -6000, To = 6000, Step = 25;

    /// <summary>What the S-meter shows for a test tone across the band: the filter plus the down-converter.</summary>
    public static IReadOnlyList<FilterPoint> AtSMeter(IReadOnlyList<Complex> h, double from = From, double to = To, double step = Step)
    {
        var points = new List<FilterPoint>((int)((to - from) / step) + 1);
        for (double f = from; f <= to + 1e-9; f += step)
        {
            points.Add(new FilterPoint(f, ChannelFilter.AtSMeterDb(h, f)));
        }

        return points;
    }

    /// <summary>The chart's floor for a stop band: at least -100 dB, 25 dB below the stop band, in tens.</summary>
    public static double BottomDb(double stopDb) => -Math.Max(100, Math.Ceiling((stopDb + 25) / 10) * 10);

    /// <summary>Measured against predicted (ported from DSP-Box-Lab's Filter page).</summary>
    public static FilterComparison Compare(ChannelFilterDesign design, IReadOnlyList<FilterPoint> measured)
    {
        double worstPass = 0, minStop = double.MaxValue;
        foreach (FilterPoint p in measured)
        {
            double expect = ChannelFilter.AtSMeterDb(design.Coefficients, p.Hz);
            if (expect > -3)
            {
                worstPass = Math.Max(worstPass, Math.Abs(p.GainDb - expect));
            }

            if (p.Hz < design.Low - design.TransitionHz || p.Hz > design.High + design.TransitionHz)
            {
                minStop = Math.Min(minStop, -p.GainDb);
            }
        }

        return new FilterComparison(worstPass, minStop < double.MaxValue ? minStop : null);
    }

    /// <summary>
    /// Whether to set the low edge before the high one when both move: the
    /// board pushes the other edge along when they come closer than 50 Hz, so
    /// an edge moving down goes first, one moving up second.
    /// </summary>
    public static bool LowEdgeFirst(double currentLow, double newLow) => newLow <= currentLow;
}

/// <summary>The Designer's starting masks (from DSP-Box-Lab).</summary>
public static class MaskTemplates
{
    public const string NotchInUsb = "Notch in USB";
    public const string TwoPassbands = "Two passbands";
    public const string Cw200 = "CW 200 Hz";
    public const string SlopedSpeech = "Sloped speech";
    public const string LikeTheFilter = "Like the filter in force";

    public static IReadOnlyList<string> Names { get; } = [NotchInUsb, TwoPassbands, Cw200, SlopedSpeech, LikeTheFilter];

    /// <summary>The template's bands, stop band and transition; "like the filter" takes the edges in force.</summary>
    public static MaskSpec For(string name, double low, double high, double stopDb) =>
        name switch
        {
            TwoPassbands => new(name, [new(-2700, -1500, 0), new(1500, 2700, 0)], 70, 150),
            Cw200 => new(name, [new(500, 700, 0)], 70, 60),
            SlopedSpeech => new(name, [new(300, 800, -6), new(800, 1500, -3), new(1500, 2700, 0)], 70, 150),
            LikeTheFilter => new(name, [new(low, high, 0)], stopDb, ChannelFilter.Design(low, high, stopDb).TransitionHz),
            _ => new(NotchInUsb, [new(300, 1400, 0), new(1600, 2700, 0)], 70, 100),
        };
}
