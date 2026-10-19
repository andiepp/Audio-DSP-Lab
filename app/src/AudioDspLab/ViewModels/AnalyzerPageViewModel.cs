using System.Globalization;
using System.IO;
using System.Windows.Input;
using AudioDspLab.Protocol;

namespace AudioDspLab.ViewModels;

/// <summary>
/// The response analyzer: a sine on K3/K4 stepped through a sweep, the
/// levels on K2 measured at each point, against a reference if one is kept;
/// or, in its distortion mode, the tone's THD and THD+N on K2 with the
/// harmonics and the spectrum, measured continuously by the board.
/// </summary>
public sealed class AnalyzerPageViewModel : ApplicationPageViewModel
{
    public const int MaximumPoints = 128;

    // the firmware's timing (Analyzer.cpp): a point waits 40 periods, at
    // least 20 ms, and reads 8 times; a sweep's first point waits 150 ms more
    private const double SettlePeriods = 40.0;
    private const double SettleMinimumSeconds = 0.020;
    private const double FirstSettleSeconds = 0.150;
    private const int Readings = 8;

    private SweepStatus? sweep;
    private IReadOnlyList<SweepPoint> points = [];
    private IReadOnlyList<SweepPoint>? reference;
    private double? inputLeft;
    private double? inputRight;
    private bool showRelative;
    private bool isReferenceConfirmed;
    private bool isBusy;
    private bool isDistortionMode;
    private DistortionStatus? distortion;
    private IReadOnlyList<SpectrumPoint> spectrum = [];
    private IReadOnlyList<Harmonic> harmonics = [];
    private int shownResults;
    private bool isFetching;

    public AnalyzerPageViewModel(ParameterWrite write, BoardAction run)
        : base(write, run, Applications.Analyzer)
    {
        Level = Number("level", "Output level", -60, 0, "-- dBFS --");
        Output = Switch("output");
        Start = Number("start", "Start", 20, 20000, "-- Hz --");
        Stop = Number("stop", "Stop", 20, 20000, "-- Hz --");
        PointsPerDecade = Integer("points-per-decade", "Points / decade", 1, 40, string.Empty);
        Tone = Number("tone", "Tone", 20, 20000, "-- Hz --");
        foreach (ParameterViewModel setting in new[] { Start, Stop, PointsPerDecade })
        {
            setting.ValueChanged += _ => RaisePropertyChanged(nameof(PlanText));
        }

        StartSweepCommand = new RelayCommand(() => _ = StartSweepAsync(), () => CanStart);
        StopSweepCommand = new RelayCommand(() => _ = StopSweepAsync(), () => CanStop);
        KeepReferenceCommand = new RelayCommand(() => _ = KeepReferenceAsync(), () => CanKeepReference);
        ShowRelativeCommand = new RelayCommand(mode => ShowRelative = mode as string == "relative");
        ShowModeCommand = new RelayCommand(mode => IsDistortionMode = mode as string == "distortion");
        StartDistortionCommand = new RelayCommand(() => _ = StartDistortionAsync(), () => CanStartDistortion);
        StopDistortionCommand = new RelayCommand(() => _ = StopDistortionAsync(), () => CanStopDistortion);
    }

    public ParameterViewModel Level { get; }

    public ParameterViewModel Output { get; }

    public ParameterViewModel Start { get; }

    public ParameterViewModel Stop { get; }

    public ParameterViewModel PointsPerDecade { get; }

    public ParameterViewModel Tone { get; }

    public ICommand StartSweepCommand { get; }

    public ICommand StopSweepCommand { get; }

    public ICommand KeepReferenceCommand { get; }

    public ICommand ShowRelativeCommand { get; }

    public ICommand ShowModeCommand { get; }

    public ICommand StartDistortionCommand { get; }

    public ICommand StopDistortionCommand { get; }

    /// <summary>The page shows the distortion measurement instead of the sweep.</summary>
    public bool IsDistortionMode
    {
        get => isDistortionMode;
        set
        {
            if (SetProperty(ref isDistortionMode, value))
            {
                RaisePropertyChanged(nameof(IsResponseMode));
            }
        }
    }

    public bool IsResponseMode => !isDistortionMode;

    public bool IsMeasuringDistortion => distortion?.IsRunning == true;

    public bool CanStartDistortion => IsRunning && !isBusy && !IsMeasuringDistortion;

    public bool CanStopDistortion => IsRunning && !isBusy && IsMeasuringDistortion;

    public IReadOnlyList<SpectrumPoint> Spectrum
    {
        get => spectrum;
        private set => SetProperty(ref spectrum, value);
    }

    public IReadOnlyList<Harmonic> Harmonics
    {
        get => harmonics;
        private set
        {
            if (SetProperty(ref harmonics, value))
            {
                RaisePropertyChanged(nameof(HarmonicsLeftText));
                RaisePropertyChanged(nameof(HarmonicsRightText));
            }
        }
    }

    public double? FundamentalHertz => distortion?.FrequencyHertz;

    public string DistortionStateText =>
        distortion switch
        {
            null => string.Empty,
            { State: DistortionStatus.Failed } => "failed: no clock on the DSP's serial output",
            { Results: 0, IsRunning: true } => "measuring …",
            { Results: 0 } => "not measured yet",
            _ => string.Format(CultureInfo.CurrentCulture, "{0:#,0.00} Hz at {1:0.00} / {2:0.00} dBFS · result {3}{4}",
                distortion.FrequencyHertz, distortion.Left?.LevelDecibel, distortion.Right?.LevelDecibel,
                distortion.Results, distortion.IsRunning ? string.Empty : " · stopped")
        };

    public string ThdLeftText => PercentText(distortion?.Left?.ThdDecibel);

    public string ThdLeftDecibelText => DecibelText(distortion?.Left?.ThdDecibel, "dB");

    public string ThdnLeftText => PercentText(distortion?.Left?.ThdnDecibel);

    public string ThdnLeftDecibelText => DecibelText(distortion?.Left?.ThdnDecibel, "dB");

    public string ThdRightText => PercentText(distortion?.Right?.ThdDecibel);

    public string ThdRightDecibelText => DecibelText(distortion?.Right?.ThdDecibel, "dB");

    public string ThdnRightText => PercentText(distortion?.Right?.ThdnDecibel);

    public string ThdnRightDecibelText => DecibelText(distortion?.Right?.ThdnDecibel, "dB");

    public string FundamentalLeftText => DecibelText(distortion?.Left?.LevelDecibel, "dBFS");

    public string FundamentalRightText => DecibelText(distortion?.Right?.LevelDecibel, "dBFS");

    public string HarmonicsLeftText => HarmonicsText(harmonic => harmonic.LeftDecibel);

    public string HarmonicsRightText => HarmonicsText(harmonic => harmonic.RightDecibel);

    internal bool IsBusy
    {
        get => isBusy;
        set => isBusy = value;
    }

    public bool IsSweeping => sweep?.State == SweepStatus.Running;

    public int? PlannedPoints =>
        Start.Value is double start && Stop.Value is double stop && PointsPerDecade.Value is double perDecade && stop > start
            ? Math.Min(MaximumPoints, Math.Max(2, (int)Math.Round(Math.Log10(stop / start) * perDecade, MidpointRounding.AwayFromZero) + 1))
            : null;

    public double? EstimatedSeconds
    {
        get
        {
            if (PlannedPoints is not int count || Start.Value is not double start || Stop.Value is not double stop)
            {
                return null;
            }

            double seconds = FirstSettleSeconds;
            for (int index = 0; index < count; index++)
            {
                double hertz = start * Math.Pow(stop / start, index / (double)(count - 1));
                seconds += Math.Max(SettleMinimumSeconds, SettlePeriods / hertz)
                           + (Readings * Math.Max(0.0002, 1.0 / (2.0 * Readings * hertz)));
            }

            return seconds;
        }
    }

    public string PlanText =>
        PlannedPoints is int count && EstimatedSeconds is double seconds
            ? string.Format(CultureInfo.CurrentCulture, "{0} points · about {1:0} s", count, Math.Max(1, seconds))
            : Start.Value is double start && Stop.Value is double stop && stop <= start
                ? "stop must lie above start"
                : string.Empty;

    public double Progress => sweep is { Total: > 0 } status ? status.Measured / (double)status.Total : 0;

    public string ProgressText =>
        sweep is null
            ? string.Empty
            : sweep.Total > 0
                ? string.Format(CultureInfo.CurrentCulture, "{0} of {1} · {2}", sweep.Measured, sweep.Total, sweep.State)
                : sweep.State;

    public IReadOnlyList<SweepPoint> Points
    {
        get => points;
        private set => SetProperty(ref points, value);
    }

    public IReadOnlyList<SweepPoint>? Reference
    {
        get => reference;
        private set
        {
            if (SetProperty(ref reference, value))
            {
                RaisePropertyChanged(nameof(ReferenceText));
            }
        }
    }

    public string ReferenceText =>
        reference switch
        {
            null => string.Empty,
            { Count: 0 } => "no reference kept: sweep a cable from K3 to K2 and keep it",
            _ => string.Format(CultureInfo.CurrentCulture, "reference: {0} points, {1:#,0.##} Hz to {2:#,0.##} Hz",
                reference.Count, reference[0].FrequencyHertz, reference[^1].FrequencyHertz)
        };

    public bool ShowRelative
    {
        get => showRelative;
        set
        {
            if (SetProperty(ref showRelative, value))
            {
                RaisePropertyChanged(nameof(ShowAbsolute));
            }
        }
    }

    public bool ShowAbsolute => !showRelative;

    /// <summary>The operator agrees that keeping replaces the reference.</summary>
    public bool IsReferenceConfirmed
    {
        get => isReferenceConfirmed;
        set => SetProperty(ref isReferenceConfirmed, value);
    }

    public string InputLeftText => LevelText(inputLeft);

    public string InputRightText => LevelText(inputRight);

    public bool CanStart => IsRunning && !isBusy && !IsSweeping && PlannedPoints is not null;

    public Task StartDistortionAsync() =>
        CanStartDistortion
            ? Run(async client =>
            {
                IsDistortionMode = true;
                await ApplyDistortionAsync(client, await client.StartDistortionAsync().ConfigureAwait(true)).ConfigureAwait(true);
                Output.Apply(await client.GetAsync("output").ConfigureAwait(true), fromBoard: false);
                ApplySweep(await client.GetSweepAsync().ConfigureAwait(true));
            }, "Distortion measurement started.")
            : Task.CompletedTask;

    public Task StopDistortionAsync() =>
        CanStopDistortion
            ? Run(async client => await ApplyDistortionAsync(client, await client.StopDistortionAsync().ConfigureAwait(true))
                .ConfigureAwait(true), "Distortion measurement stopped.")
            : Task.CompletedTask;

    /// <summary>
    /// The measurement's state; a result not yet shown brings its spectrum
    /// and harmonics along (read once at a time; a newer one waits for the
    /// next result).
    /// </summary>
    internal async Task ApplyDistortionAsync(BoardClient client, DistortionStatus status)
    {
        distortion = status;
        foreach (string name in new[]
                 {
                     nameof(IsMeasuringDistortion), nameof(FundamentalHertz), nameof(DistortionStateText),
                     nameof(ThdLeftText), nameof(ThdLeftDecibelText), nameof(ThdnLeftText), nameof(ThdnLeftDecibelText),
                     nameof(ThdRightText), nameof(ThdRightDecibelText), nameof(ThdnRightText), nameof(ThdnRightDecibelText),
                     nameof(FundamentalLeftText), nameof(FundamentalRightText),
                 })
        {
            RaisePropertyChanged(name);
        }

        CommandManager.InvalidateRequerySuggested();
        if (status.Results == 0)
        {
            shownResults = 0;
            Spectrum = [];
            Harmonics = [];
            return;
        }

        if (status.Results == shownResults || isFetching)
        {
            return;
        }

        isFetching = true;
        try
        {
            IReadOnlyList<SpectrumPoint> read = await client.GetSpectrumAsync().ConfigureAwait(true);
            Harmonics = await client.GetHarmonicsAsync().ConfigureAwait(true);
            Spectrum = read;
            shownResults = status.Results;
        }
        catch (Exception exception) when (exception is BoardErrorException or IOException or TimeoutException)
        {
            // the next result tries again
        }
        finally
        {
            isFetching = false;
        }
    }

    public bool CanStop => IsRunning && !isBusy && IsSweeping;

    public bool CanKeepReference => IsRunning && !isBusy && sweep?.State == SweepStatus.Done && isReferenceConfirmed;

    public Task StartSweepAsync() =>
        CanStart
            ? Run(async client =>
            {
                Points = [];
                ApplySweep(await client.StartSweepAsync().ConfigureAwait(true));
                Output.Apply(await client.GetAsync("output").ConfigureAwait(true), fromBoard: false);
            }, "Sweep started.")
            : Task.CompletedTask;

    public Task StopSweepAsync() =>
        CanStop ? Run(async client => ApplySweep(await client.StopSweepAsync().ConfigureAwait(true)), "Sweep stopped.") : Task.CompletedTask;

    public async Task KeepReferenceAsync()
    {
        if (!CanKeepReference)
        {
            return;
        }

        try
        {
            await Run(async client =>
            {
                await client.KeepReferenceAsync().ConfigureAwait(true);
                await LoadAsync(client).ConfigureAwait(true);
            }, "The sweep is the reference now.").ConfigureAwait(true);
        }
        finally
        {
            IsReferenceConfirmed = false;
        }
    }

    /// <summary>The sweep's state, its points and the reference, read at once.</summary>
    internal async Task LoadAsync(BoardClient client)
    {
        ApplySweep(await client.GetSweepAsync().ConfigureAwait(true));
        IReadOnlyList<SweepPoint> read = await client.GetPointsAsync().ConfigureAwait(true);
        Points = read;
        Reference = await client.GetReferenceAsync().ConfigureAwait(true);
        shownResults = 0;
        DistortionStatus status = await client.GetDistortionAsync().ConfigureAwait(true);
        if (status.IsRunning)
        {
            IsDistortionMode = true;
        }

        await ApplyDistortionAsync(client, status).ConfigureAwait(true);
    }

    internal void ApplySweep(SweepStatus status)
    {
        sweep = status;
        RaisePropertyChanged(nameof(IsSweeping));
        RaisePropertyChanged(nameof(Progress));
        RaisePropertyChanged(nameof(ProgressText));
        CommandManager.InvalidateRequerySuggested();
    }

    /// <summary>A point measured now: a new sweep's first one starts the curve afresh.</summary>
    internal void AddPoint(SweepPoint point)
    {
        List<SweepPoint> next = point.Index == 0 ? [] : [.. points.Where(existing => existing.Index < point.Index)];
        next.Add(point);
        Points = next;
    }

    internal void ApplyInput(double? left, double? right)
    {
        inputLeft = left;
        inputRight = right;
        RaisePropertyChanged(nameof(InputLeftText));
        RaisePropertyChanged(nameof(InputRightText));
    }

    internal override void Forget()
    {
        base.Forget();
        sweep = null;
        Points = [];
        Reference = null;
        distortion = null;
        shownResults = 0;
        Spectrum = [];
        Harmonics = [];
        RaisePropertyChanged(nameof(DistortionStateText));
        ApplyInput(null, null);
        RaisePropertyChanged(nameof(ProgressText));
        RaisePropertyChanged(nameof(Progress));
        RaisePropertyChanged(nameof(PlanText));
    }

    private static string LevelText(double? decibel) =>
        decibel is double value ? value.ToString("0.00", CultureInfo.CurrentCulture) : "-----";

    /// <summary>A ratio in dB as a percentage with three significant digits: 0.0123, 0.123, 1.23.</summary>
    internal static string PercentText(double? decibel)
    {
        if (decibel is not double value)
        {
            return "-----";
        }

        double percent = DistortionChannel.Percent(value);
        int decimals = Math.Clamp(2 - (int)Math.Floor(Math.Log10(Math.Max(percent, 1e-6))), 0, 6);
        return percent.ToString("F" + decimals.ToString(CultureInfo.InvariantCulture), CultureInfo.CurrentCulture);
    }

    private static string DecibelText(double? decibel, string unit) =>
        decibel is double value ? value.ToString("0.00", CultureInfo.CurrentCulture) + " " + unit : "-- " + unit + " --";

    private string HarmonicsText(Func<Harmonic, double?> level) =>
        harmonics.Count == 0
            ? "harmonics: --"
            : string.Join("  ", harmonics.Select(harmonic =>
                string.Format(CultureInfo.CurrentCulture, "H{0} {1}", harmonic.Order,
                    level(harmonic) is double value ? value.ToString("0", CultureInfo.CurrentCulture) : "--")))
              + " dBc";
}
