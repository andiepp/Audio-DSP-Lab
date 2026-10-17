using System.Globalization;
using System.Windows.Input;

namespace AudioDspLab.ViewModels;

/// <summary>The signal generator: waveform, its width, frequency, amplitude, output, P2's range.</summary>
public sealed class GeneratorPageViewModel : ApplicationPageViewModel
{
    /// <summary>The analog output at 0 dBFS that Elektor gives for the board; the output stage sets the real level.</summary>
    public const double FullScaleVoltsRms = 0.9;

    private string frequencyText = string.Empty;
    private bool isFrequencyTextDirty;
    private string amplitudeText = string.Empty;
    private bool isAmplitudeTextDirty;

    public GeneratorPageViewModel(ParameterWrite write, BoardAction run)
        : base(write, run, Applications.Generator)
    {
        Waveform = Choice("waveform", "Waveform",
            ("sine", "Sine"), ("rectangle", "Rectangle"), ("triangle", "Triangle"),
            ("saw", "Saw"), ("inverse-saw", "Inverse saw"), ("noise", "Noise"));
        Width = Integer("width", "Width", 5, 95, string.Empty);
        Frequency = Number("frequency", "Frequency", 20, 20000, "---- Hz ----");
        Amplitude = Number("amplitude", "A", -60, 0, "-- dBFS --");
        Output = Switch("output");
        P2Range = Choice("p2-range", "P2 range", ("20-200", "20-200 Hz"), ("200-2k", "200 Hz-2 kHz"), ("2k-20k", "2-20 kHz"));

        Waveform.ValueChanged += _ =>
        {
            RaisePropertyChanged(nameof(HasWidth));
            RaisePropertyChanged(nameof(WidthHint));
        };

        Frequency.ValueChanged += _ => SeedFrequencyText(force: false);
        Amplitude.ValueChanged += _ =>
        {
            SeedAmplitudeText(force: false);
            RaisePropertyChanged(nameof(ApproximateRmsText));
        };

        ApplyFrequencyTextCommand = new RelayCommand(() => _ = ApplyFrequencyTextAsync(), () => IsRunning);
        RevertFrequencyTextCommand = new RelayCommand(() => SeedFrequencyText(force: true));
        SetFrequencyCommand = new RelayCommand(
            text => _ = Frequency.SetAsync(double.Parse((string)text!, CultureInfo.InvariantCulture)),
            _ => IsRunning);
        ApplyAmplitudeTextCommand = new RelayCommand(() => _ = ApplyAmplitudeTextAsync(), () => IsRunning);
        RevertAmplitudeTextCommand = new RelayCommand(() => SeedAmplitudeText(force: true));
        SetAmplitudeCommand = new RelayCommand(
            text => _ = Amplitude.SetAsync(double.Parse((string)text!, CultureInfo.InvariantCulture)),
            _ => IsRunning);
    }

    public ParameterViewModel Waveform { get; }

    public ParameterViewModel Frequency { get; }

    public ParameterViewModel Amplitude { get; }

    public ParameterViewModel Output { get; }

    /// <summary>The rectangle's high part or the triangle's rise, in percent of the cycle.</summary>
    public ParameterViewModel Width { get; }

    /// <summary>Whether the waveform has a width: rectangle and triangle.</summary>
    public bool HasWidth => Waveform.Selected is "rectangle" or "triangle";

    public string WidthHint => Waveform.Selected switch
    {
        "rectangle" => "% of the cycle high · 50 % is a square",
        "triangle" => "% of the cycle rising · 50 % is symmetric",
        _ => "for the rectangle and the triangle",
    };

    public ParameterViewModel P2Range { get; }

    public ICommand ApplyFrequencyTextCommand { get; }

    public ICommand RevertFrequencyTextCommand { get; }

    /// <summary>Takes a frequency in Hertz, written invariantly, for the quick keys.</summary>
    public ICommand SetFrequencyCommand { get; }

    public ICommand ApplyAmplitudeTextCommand { get; }

    public ICommand RevertAmplitudeTextCommand { get; }

    public ICommand SetAmplitudeCommand { get; }

    /// <summary>The frequency entry: follows the board until typed in; Enter applies, Escape gives up.</summary>
    public string FrequencyText
    {
        get => frequencyText;
        set
        {
            if (SetProperty(ref frequencyText, value))
            {
                isFrequencyTextDirty = true;
            }
        }
    }

    public string AmplitudeText
    {
        get => amplitudeText;
        set
        {
            if (SetProperty(ref amplitudeText, value))
            {
                isAmplitudeTextDirty = true;
            }
        }
    }

    /// <summary>The level the amplitude stands for, from Elektor's full-scale figure.</summary>
    public string ApproximateRmsText =>
        Amplitude.Value is double decibel ? FormatApproximateRms(decibel) : string.Empty;

    public static string FormatApproximateRms(double decibel)
    {
        double volts = FullScaleVoltsRms * Math.Pow(10, decibel / 20);
        return volts >= 0.1
            ? string.Format(CultureInfo.CurrentCulture, "≈ {0:0.000} Vrms", volts)
            : string.Format(CultureInfo.CurrentCulture, "≈ {0:0.0} mVrms", volts * 1000);
    }

    /// <summary>Hertz, with an optional "Hz" and a trailing k for kilohertz.</summary>
    public static bool TryParseFrequency(string? text, out double hertz)
    {
        hertz = 0;
        string value = (text ?? string.Empty).Trim();
        if (value.EndsWith("hz", StringComparison.OrdinalIgnoreCase))
        {
            value = value[..^2].TrimEnd();
        }

        double factor = 1;
        if (value.EndsWith('k') || value.EndsWith('K'))
        {
            factor = 1000;
            value = value[..^1].TrimEnd();
        }

        if (!TryParseNumber(value, out double number))
        {
            return false;
        }

        hertz = number * factor;
        return true;
    }

    public static bool TryParseNumber(string? text, out double number) =>
        double.TryParse(text, NumberStyles.Float, CultureInfo.CurrentCulture, out number)
        || double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out number);

    internal override void Forget()
    {
        base.Forget();
        SeedFrequencyText(force: true);
        SeedAmplitudeText(force: true);
    }

    private Task ApplyFrequencyTextAsync()
    {
        if (!TryParseFrequency(frequencyText, out double hertz))
        {
            SeedFrequencyText(force: true);
            return Task.CompletedTask;
        }

        isFrequencyTextDirty = false;
        return Frequency.SetAsync(hertz);
    }

    private Task ApplyAmplitudeTextAsync()
    {
        if (!TryParseNumber(amplitudeText, out double decibel))
        {
            SeedAmplitudeText(force: true);
            return Task.CompletedTask;
        }

        isAmplitudeTextDirty = false;
        return Amplitude.SetAsync(decibel);
    }

    private void SeedFrequencyText(bool force)
    {
        if (force || !isFrequencyTextDirty)
        {
            frequencyText = Frequency.Value is double hertz ? hertz.ToString("0.##", CultureInfo.CurrentCulture) : string.Empty;
            isFrequencyTextDirty = false;
            RaisePropertyChanged(nameof(FrequencyText));
        }
    }

    private void SeedAmplitudeText(bool force)
    {
        if (force || !isAmplitudeTextDirty)
        {
            amplitudeText = Amplitude.Value is double decibel ? decibel.ToString("0.##", CultureInfo.CurrentCulture) : string.Empty;
            isAmplitudeTextDirty = false;
            RaisePropertyChanged(nameof(AmplitudeText));
        }
    }
}
