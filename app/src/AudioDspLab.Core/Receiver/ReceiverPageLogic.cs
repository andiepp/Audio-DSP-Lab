using System.Numerics;

namespace AudioDspLab.Receiver;

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
