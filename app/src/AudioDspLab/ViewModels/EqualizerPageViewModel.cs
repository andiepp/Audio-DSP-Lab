using System.Globalization;
using System.Numerics;

namespace AudioDspLab.ViewModels;

public enum EqualizerBand
{
    Bass,
    Middle,
    Treble,
}

/// <summary>One band of the equalizer: its gain (on a pot) and its frequency.</summary>
public sealed class EqualizerBandViewModel
{
    internal EqualizerBandViewModel(EqualizerBand band, ParameterViewModel gain, ParameterViewModel frequency)
    {
        Band = band;
        Gain = gain;
        Frequency = frequency;
    }

    public EqualizerBand Band { get; }

    public string Name => Band.ToString();

    public string GainTitle => $"{Name} gain";

    public string FrequencyTitle => Band == EqualizerBand.Middle ? "Centre" : "Corner";

    public ParameterViewModel Gain { get; }

    public ParameterViewModel Frequency { get; }

    public string RangeText =>
        string.Format(CultureInfo.CurrentCulture, "{0:#,0} to {1:#,0} Hz", Frequency.Minimum, Frequency.Maximum);
}

/// <summary>
/// The equalizer page, shown for the equalizer and for the radio (which
/// plays through the same three bands and volume).
/// </summary>
public sealed class EqualizerPageViewModel : ApplicationPageViewModel
{
    public const int ResponsePointCount = 241;

    private static readonly IReadOnlyList<double> ResponseFrequencies = EqualizerResponse.LogarithmicFrequencies(ResponsePointCount);

    private bool isRadioSource;

    public EqualizerPageViewModel(ParameterWrite write, BoardAction run)
        : base(write, run, Applications.Equalizer, Applications.Radio)
    {
        Bands =
        [
            Band(EqualizerBand.Bass, "bass", 50, 400),
            Band(EqualizerBand.Middle, "middle", 300, 5000),
            Band(EqualizerBand.Treble, "treble", 500, 8000),
        ];
        Volume = Number("volume", "Volume", -60, 0, "-- dB --");
        Output = Switch("output");
    }

    public IReadOnlyList<EqualizerBandViewModel> Bands { get; }

    public ParameterViewModel Volume { get; }

    public ParameterViewModel Output { get; }

    /// <summary>The radio, not the line input, feeds the bands.</summary>
    public bool IsRadioSource
    {
        get => isRadioSource;
        internal set
        {
            if (SetProperty(ref isRadioSource, value))
            {
                RaisePropertyChanged(nameof(OutputNote));
            }
        }
    }

    public string OutputNote =>
        isRadioSource
            ? "Radio in, K3 and K4 out · off after every start"
            : "K2 in, K3 and K4 out · off after every start";

    /// <summary>The response of the three bands, computed, in dB at <see cref="ResponsePointCount"/> frequencies.</summary>
    public IReadOnlyList<double> Response =>
        EqualizerResponse.Combined([.. Bands.Select(band => (band.Band, band.Gain.Value, band.Frequency.Value))], ResponseFrequencies);

    public IReadOnlyList<double> MarkerFrequencies =>
        [.. Bands.Select(band => band.Frequency.Value).OfType<double>()];

    private EqualizerBandViewModel Band(EqualizerBand band, string name, double minimumHertz, double maximumHertz)
    {
        ParameterViewModel gain = Number($"{name}-gain", $"{band} gain", -12, 12, "-- dB --");
        ParameterViewModel frequency = Number($"{name}-frequency", band == EqualizerBand.Middle ? "Centre" : "Corner",
            minimumHertz, maximumHertz, "-- Hz --");
        gain.ValueChanged += _ => RaiseResponse();
        frequency.ValueChanged += _ => RaiseResponse();
        return new EqualizerBandViewModel(band, gain, frequency);
    }

    private void RaiseResponse()
    {
        RaisePropertyChanged(nameof(Response));
        RaisePropertyChanged(nameof(MarkerFrequencies));
    }
}

/// <summary>
/// The equalizer's response computed from its bands the way the firmware
/// designs them (RBJ cookbook: shelves with slope 1, a peak with Q 0.7) at
/// the DSP core's 192 kHz.
/// </summary>
public static class EqualizerResponse
{
    public const double CoreRateHertz = 192_000.0;
    public const double MiddleQuality = 0.7;

    public readonly record struct Biquad(double B0, double B1, double B2, double A1, double A2)
    {
        public double MagnitudeDecibel(double hertz)
        {
            Complex z = Complex.FromPolarCoordinates(1.0, -2.0 * Math.PI * hertz / CoreRateHertz);
            Complex numerator = B0 + (B1 * z) + (B2 * z * z);
            Complex denominator = 1.0 + (A1 * z) + (A2 * z * z);
            return 20.0 * Math.Log10(numerator.Magnitude / denominator.Magnitude);
        }
    }

    public static Biquad Design(EqualizerBand band, double gainDecibel, double hertz)
    {
        double a = Math.Pow(10.0, gainDecibel / 40.0);
        double w0 = 2.0 * Math.PI * hertz / CoreRateHertz;
        double cw = Math.Cos(w0);
        double sw = Math.Sin(w0);
        double b0, b1, b2, a0, a1, a2;
        if (band == EqualizerBand.Middle)
        {
            double alpha = sw / (2.0 * MiddleQuality);
            b0 = 1.0 + (alpha * a);
            b1 = -2.0 * cw;
            b2 = 1.0 - (alpha * a);
            a0 = 1.0 + (alpha / a);
            a1 = -2.0 * cw;
            a2 = 1.0 - (alpha / a);
        }
        else
        {
            double k = 2.0 * Math.Sqrt(a) * (sw / 2.0 * Math.Sqrt(2.0));
            double s = band == EqualizerBand.Bass ? 1.0 : -1.0;
            b0 = a * ((a + 1) - (s * (a - 1) * cw) + k);
            b1 = 2.0 * s * a * ((a - 1) - (s * (a + 1) * cw));
            b2 = a * ((a + 1) - (s * (a - 1) * cw) - k);
            a0 = (a + 1) + (s * (a - 1) * cw) + k;
            a1 = -2.0 * s * ((a - 1) + (s * (a + 1) * cw));
            a2 = (a + 1) + (s * (a - 1) * cw) - k;
        }

        return new Biquad(b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0);
    }

    /// <summary>The sum of the bands in dB; empty while any value is unknown.</summary>
    public static IReadOnlyList<double> Combined(
        IReadOnlyList<(EqualizerBand Band, double? GainDecibel, double? Hertz)> bands,
        IReadOnlyList<double> frequencies)
    {
        if (bands.Any(band => band.GainDecibel is null || band.Hertz is null))
        {
            return [];
        }

        Biquad[] filters = [.. bands.Select(band => Design(band.Band, band.GainDecibel!.Value, band.Hertz!.Value))];
        return [.. frequencies.Select(hertz => filters.Sum(filter => filter.MagnitudeDecibel(hertz)))];
    }

    /// <summary>Frequencies from 20 Hz to 20 kHz, evenly spaced on a logarithmic axis.</summary>
    public static IReadOnlyList<double> LogarithmicFrequencies(int count) =>
        [.. Enumerable.Range(0, count).Select(index => 20.0 * Math.Pow(1000.0, index / (double)(count - 1)))];
}
