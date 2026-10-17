using System.Numerics;

namespace AudioDspLab;

/// <summary>
/// The synthesizer's filter and envelopes as the firmware computes them
/// (firmware/AudioDspLab/Synth.cpp), for drawing: the ladder's small-signal
/// response and the envelopes' levels over time. The saturation the
/// firmware adds at the filter's input is left out, so emphasis at or
/// above self-oscillation (k = 4) draws an unbounded peak.
/// </summary>
public static class SynthCurves
{
    public const double SampleRate = 48_000;
    public const double EmphasisMaximum = 4.2;           // SYNTH_EMPHASIS_MAXIMUM: k at 100 %
    public const double ContourOctaves = 5;              // SYNTH_CONTOUR_OCTAVES at 100 %
    public const double CutoffLowest = 10;               // SYNTH_CUTOFF_LOWEST
    public const double CutoffHighest = 0.45 * SampleRate;
    public const double TimeConstants = 4.6;             // ln(100): within 1 % at the time set

    /// <summary>
    /// The ladder's gain in dB at a frequency: four one-pole stages in
    /// trapezoidal (bilinear) form with feedback k, times the firmware's
    /// make-up gain 1 + k / 4.
    /// </summary>
    public static double LadderGainDecibel(double hertz, double cutoffHertz, double emphasisPercent)
    {
        double k = EmphasisMaximum * Math.Clamp(emphasisPercent, 0, 100) / 100;
        double g = Math.Tan(Math.PI * Math.Clamp(cutoffHertz, CutoffLowest, CutoffHighest) / SampleRate);
        double w = Math.Tan(Math.PI * Math.Min(hertz, 0.499 * SampleRate) / SampleRate);
        Complex stage = g / new Complex(g, w);
        Complex four = stage * stage * stage * stage;
        Complex response = four / (1 + (k * four)) * (1 + (0.25 * k));
        double magnitude = response.Magnitude;
        return magnitude > 0 ? 20 * Math.Log10(magnitude) : double.NegativeInfinity;
    }

    public static IReadOnlyList<double> LadderResponse(double cutoffHertz, double emphasisPercent, IReadOnlyList<double> frequencies) =>
        [.. frequencies.Select(hertz => LadderGainDecibel(hertz, cutoffHertz, emphasisPercent))];

    /// <summary>Where the contour takes the cutoff at its peak: up to five octaves above.</summary>
    public static double ContourPeakCutoff(double cutoffHertz, double amountPercent) =>
        Math.Clamp(cutoffHertz * Math.Pow(2, ContourOctaves * Math.Clamp(amountPercent, 0, 100) / 100), CutoffLowest, CutoffHighest);

    /// <summary>
    /// An envelope's level (0..1) at a time after the key went down, the
    /// key held throughout: a linear attack to 1, then an exponential decay
    /// toward the sustain, within 1 % of it at the decay time.
    /// </summary>
    public static double HeldLevel(double milliseconds, double attack, double decay, double sustain)
    {
        if (milliseconds < attack)
        {
            return milliseconds / attack;
        }

        return sustain + ((1 - sustain) * Math.Exp(-TimeConstants * (milliseconds - attack) / decay));
    }

    /// <summary>The level a release reached after a time, from the level the key left.</summary>
    public static double ReleasedLevel(double milliseconds, double fromLevel, double release) =>
        fromLevel * Math.Exp(-TimeConstants * milliseconds / release);
}

/// <summary>The two envelopes' settings, in ms and in parts of 1 for the sustains.</summary>
public sealed record SynthEnvelopes(
    double Attack, double Decay, double Sustain, double Release,
    double ContourAttack, double ContourDecay, double ContourSustain)
{
    /// <summary>The longest time either envelope needs from the key down to its sustain.</summary>
    public double HeldSpan => Math.Max(Attack + Decay, ContourAttack + ContourDecay);

    /// <summary>The longest release; the contour releases with its decay time.</summary>
    public double ReleaseSpan => Math.Max(Release, ContourDecay);
}
