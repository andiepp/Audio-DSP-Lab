using System.Globalization;
using System.IO;
using System.Numerics;
using System.Windows.Input;
using AudioDspLab.Protocol;
using AudioDspLab.Receiver;

namespace AudioDspLab.ViewModels;

/// <summary>
/// The Filter view (from DSP-Box-Lab's Filter page): the channel filter in
/// force on a chart with draggable edges, its predicted response (computed
/// with the firmware's own design rule), a measurement with the internal
/// test tone and the S-meter, the test tone's settings, and the filter's
/// state as the receiver reports it.
/// </summary>
public sealed partial class ReceiverPageViewModel
{
    private ChannelFilterDesign? design;
    private IReadOnlyList<FilterPoint> predicted = [];
    private IReadOnlyList<FilterPoint>? measured;
    private ReceiverFilterStatus? filterStatus;
    private Complex[]? downloaded;
    private bool filterStatusStale;
    private bool refreshingFilterStatus;
    private string measureText = "Not measured.";
    private double measureProgress;
    private double sweepStep = 100;

    public ICommand MeasureCommand { get; private set; } = null!;

    /// <summary>Takes <see cref="FilterEdges"/> from the chart.</summary>
    public ICommand SetEdgesCommand { get; private set; } = null!;

    /// <summary>Takes the stop band (a double, dB) from the chart.</summary>
    public ICommand SetStopCommand { get; private set; } = null!;

    public IReadOnlyList<FilterPoint> Predicted
    {
        get => predicted;
        private set => SetProperty(ref predicted, value);
    }

    public IReadOnlyList<FilterPoint>? Measured
    {
        get => measured;
        private set => SetProperty(ref measured, value);
    }

    /// <summary>The edges for the chart (a sensible band while unknown).</summary>
    public double ChartLow => Low.Value ?? 300;

    public double ChartHigh => High.Value ?? 2700;

    public double ChartStop => Stopband.Value ?? 80;

    public double BottomDb => ReceiverResponse.BottomDb(ChartStop);

    public string PredictedLabel =>
        Filter.Selected != "downloaded" ? "predicted (firmware design + down-converter)"
        : downloaded is not null ? "predicted (the filter downloaded from here + down-converter)"
        : "the receiver's own design: it runs a downloaded filter";

    public string EdgesText =>
        Low.Value is double low && High.Value is double high
            ? string.Format(CultureInfo.CurrentCulture, "{0:#,0} to {1:#,0} Hz · {2:#,0} Hz wide · centre {3}",
                low, high, high - low, ((low + high) / 2).ToString("+#,0;-#,0;0", CultureInfo.CurrentCulture) + " Hz")
            : "--";

    /// <summary>The design the firmware makes from the edges, as computed here.</summary>
    public string DesignText =>
        design is { } d
            ? string.Format(CultureInfo.CurrentCulture, "Firmware design: {0} taps · transition {1:0} Hz · delay {2:0.0} ms{3}",
                d.Taps, d.TransitionHz, d.GroupDelayMs,
                d.Widened ? string.Format(CultureInfo.CurrentCulture, " · at most {0} taps: transition widened", ChannelFilter.MaxTaps) : string.Empty)
            : string.Empty;

    /// <summary>The filter in force as "rxfilter" reports it.</summary>
    public string FilterStatusText =>
        filterStatus switch
        {
            null => "--",
            { Downloaded: true } s => string.Format(CultureInfo.CurrentCulture,
                "Downloaded from the PC, {0} taps · a new edge, stop band or mode designs again", s.Taps),
            { } s => string.Format(CultureInfo.CurrentCulture, "Designed by the receiver, {0} taps, transition {1:0} Hz{2}",
                s.Taps, s.TransitionHz, s.Designing ? " · designing …" : string.Empty)
        };

    public bool IsFilterDownloaded => filterStatus?.Downloaded == true;

    public string MeasureText
    {
        get => measureText;
        private set => SetProperty(ref measureText, value);
    }

    /// <summary>0 .. 1.</summary>
    public double MeasureProgress
    {
        get => measureProgress;
        private set => SetProperty(ref measureProgress, value);
    }

    public IReadOnlyList<double> SweepSteps { get; } = [50, 100, 200];

    public double SweepStep
    {
        get => sweepStep;
        set => SetProperty(ref sweepStep, value);
    }

    /// <summary>The filter's state is to be read again (MainViewModel's tick does it).</summary>
    internal bool IsFilterStatusStale => filterStatusStale && IsRunning && !refreshingFilterStatus;

    internal async Task RefreshFilterStatusAsync(BoardClient board)
    {
        filterStatusStale = false;
        refreshingFilterStatus = true;
        try
        {
            ApplyFilterStatus(await board.GetFilterAsync().ConfigureAwait(true));
        }
        catch (Exception exception) when (exception is BoardErrorException or IOException or TimeoutException or FormatException
                                              or ArgumentOutOfRangeException)
        {
            ApplyFilterStatus(null);
        }
        finally
        {
            refreshingFilterStatus = false;
        }
    }

    /// <summary>Moves both edges, the one that would cross the other last.</summary>
    public Task SetEdgesAsync(FilterEdges edges)
    {
        double low = Math.Round(edges.Low), high = Math.Round(edges.High);
        bool lowFirst = ReceiverResponse.LowEdgeFirst(Low.Value ?? low, low);
        return Run(
            async board =>
            {
                foreach ((ParameterViewModel edge, double value) in lowFirst ? new[] { (Low, low), (High, high) } : new[] { (High, high), (Low, low) })
                {
                    edge.Apply(await board.SetAsync(edge.Name, value).ConfigureAwait(true), fromBoard: false);
                }

                await RereadAsync(board, "low", "high", "width").ConfigureAwait(true);
            },
            string.Format(CultureInfo.CurrentCulture, "Filter edges {0:#,0} to {1:#,0} Hz.", low, high));
    }

    /// <summary>Sweeps the test tone across the band and reads the S-meter at each step; the settings it touches are restored.</summary>
    public async Task MeasureAsync()
    {
        if (isWorking)
        {
            return;
        }

        IReadOnlyList<double> frequencies = FilterSweep.Frequencies(-5900, 5900, sweepStep);
        var live = new List<FilterPoint>();
        Measured = [];
        MeasureProgress = 0;
        MeasureText = "Measuring …";
        Begin();
        CancellationToken token = cancel!.Token;
        try
        {
            await Run(
                async board =>
                {
                    try
                    {
                        var progress = new Progress<FilterPoint>(point =>
                        {
                            live.Add(point);
                            Measured = [.. live];
                            MeasureProgress = live.Count / (double)frequencies.Count;
                        });
                        IReadOnlyList<FilterPoint> points = await FilterSweep.MeasureAsync(board, frequencies, progress, token).ConfigureAwait(true);
                        Measured = points;
                        MeasureProgress = 1;
                        MeasureText = Compare(points);
                    }
                    catch (OperationCanceledException)
                    {
                        MeasureText = "Cancelled.";
                        throw new OperationCanceledException("Measurement cancelled; the test tone and the AGC restored.");
                    }
                    finally
                    {
                        await RereadAsync(board, "test", "test-offset", "test-level", "test-noise", "test-key", "agc", "hang", "decay")
                            .ConfigureAwait(true);
                    }
                },
                string.Format(CultureInfo.CurrentCulture, "Filter measured: {0} points.", frequencies.Count)).ConfigureAwait(true);
        }
        finally
        {
            End();
            if (MeasureText == "Measuring …")
            {
                MeasureText = "Not measured.";
            }
        }
    }

    private void InitializeFilter()
    {
        MeasureCommand = new RelayCommand(() => _ = MeasureAsync(), () => IsRunning && !isWorking);
        SetEdgesCommand = new RelayCommand(
            edges =>
            {
                if (edges is FilterEdges chosen)
                {
                    _ = SetEdgesAsync(chosen);
                }
            },
            edges => IsRunning && !isWorking && edges is FilterEdges);
        SetStopCommand = new RelayCommand(
            stop =>
            {
                if (stop is double chosen)
                {
                    _ = Stopband.SetAsync(chosen);
                }
            },
            stop => IsRunning && !isWorking && stop is double);
    }

    private string Compare(IReadOnlyList<FilterPoint> points)
    {
        if (Filter.Selected == "downloaded")
        {
            return downloaded is not null
                ? Within(points, predicted)
                : "Measured. The receiver runs a filter downloaded before: the curve shows its own design, not that one.";
        }

        if (design is null)
        {
            return "Measured.";
        }

        FilterComparison comparison = ReceiverResponse.Compare(design, points);
        return string.Format(CultureInfo.CurrentCulture, "Passband within {0:0.00} dB of the prediction.{1}",
            comparison.WorstPassbandDb,
            comparison.MinimumStopDb is double stop
                ? string.Format(CultureInfo.CurrentCulture, "  Beyond one transition: at least {0:0} dB down.", stop)
                : string.Empty);
    }

    private static string Within(IReadOnlyList<FilterPoint> points, IReadOnlyList<FilterPoint> curve)
    {
        double worst = 0;
        foreach (FilterPoint point in points)
        {
            double expect = curve.Count > 0 ? curve.MinBy(q => Math.Abs(q.Hz - point.Hz)).GainDb : double.NegativeInfinity;
            if (expect > -40)
            {
                worst = Math.Max(worst, Math.Abs(point.GainDb - expect));
            }
        }

        return string.Format(CultureInfo.CurrentCulture, "Within {0:0.00} dB of the downloaded design where it is above -40 dB.", worst);
    }

    private void ApplyFilterStatus(ReceiverFilterStatus? status)
    {
        filterStatus = status;
        RaisePropertyChanged(nameof(FilterStatusText));
        RaisePropertyChanged(nameof(IsFilterDownloaded));
        RaisePropertyChanged(nameof(FilterSummaryText));
        RaisePropertyChanged(nameof(DesignerBoardText));
    }

    /// <summary>One of the filter's values changed: the curve again, and the state to be read.</summary>
    private void FilterChanged()
    {
        filterStatusStale = true;
        if (Low.Value is double low && High.Value is double high && Stopband.Value is double stop && high > low)
        {
            design = ChannelFilter.Design(low, high, stop);
            Predicted = ReceiverResponse.AtSMeter(Filter.Selected == "downloaded" && downloaded is not null ? downloaded : design.Coefficients);
        }
        else
        {
            design = null;
            Predicted = [];
        }

        RaisePropertyChanged(nameof(ChartLow));
        RaisePropertyChanged(nameof(ChartHigh));
        RaisePropertyChanged(nameof(ChartStop));
        RaisePropertyChanged(nameof(BottomDb));
        RaisePropertyChanged(nameof(PredictedLabel));
        RaisePropertyChanged(nameof(EdgesText));
        RaisePropertyChanged(nameof(DesignText));
        RaisePropertyChanged(nameof(FilterSummaryText));
    }

    /// <summary>The Receiver view's line about the filter.</summary>
    public string FilterSummaryText => filterStatus is null ? EdgesText : $"{EdgesText} · {(filterStatus.Downloaded ? "downloaded" : "designed")}, {filterStatus.Taps} taps";
}
