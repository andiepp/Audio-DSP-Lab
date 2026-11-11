using System.Globalization;
using System.Windows.Input;
using AudioDspLab.Protocol;
using AudioDspLab.Receiver;

namespace AudioDspLab.ViewModels;

/// <summary>
/// The receiver application (design/receiver.md, section 8), ported from
/// DSP-Box-Lab's Receiver, Filter and Designer pages: three views chosen as
/// the analysis page chooses its parts. The Receiver view holds the mode, the
/// IF dial, the meters, AGC, NR, squelch, the input and its I/Q
/// correction and the equalizer; the Filter view the
/// channel filter's edges on a chart, its measurement and the test tone; the
/// Designer view filters made on the PC and downloaded.
/// </summary>
public sealed partial class ReceiverPageViewModel : ApplicationPageViewModel, IDisposable
{
    public const string ReceiverView = "receiver";
    public const string FilterView = "filter";
    public const string DesignerView = "designer";

    private string view = ReceiverView;
    private ReceiverMeter? meter;
    private string nullText = string.Empty;
    private bool isWorking;
    private CancellationTokenSource? cancel;

    public ReceiverPageViewModel(ParameterWrite write, BoardAction run)
        : base(write, run, Applications.Receiver)
    {
        Mode = Choice("mode", "Mode", ("off", "Off"), ("am", "AM"), ("usb", "USB"), ("lsb", "LSB"), ("cw", "CW"), ("fm", "FM"));
        Offset = Integer("offset", "IF", -21000, 21000, "-- Hz --");
        Low = Integer("low", "Low edge", -5500, 5450, "-- Hz --");
        High = Integer("high", "High edge", -5450, 5500, "-- Hz --");
        Width = Integer("width", "Width", 100, 11000, "-- Hz --");
        Stopband = Integer("stopband", "Stop band", 40, 100, "-- dB --");
        Pitch = Integer("pitch", "CW pitch", 300, 1000, "-- Hz --");
        Agc = Choice("agc", "AGC", ("off", "Off"), ("fast", "Fast"), ("medium", "Medium"), ("slow", "Slow"));
        AgcGain = Integer("agc-gain", "Gain", 0, 100, "-- dB --");
        Hang = Number("hang", "Hang", 0, 5, "-- s --");
        Decay = Integer("decay", "Decay", 1, 200, "-- dB/s --");
        FmDeviation = Integer("fm-deviation", "Deviation", 100, 5000, "-- Hz --");
        Deemphasis = Integer("deemphasis", "De-emphasis", 0, 2000, "-- µs --");
        Squelch = Integer("squelch", "Squelch", -150, 0, "-- dBFS --");
        Nr = Choice("nr", "NR", ("off", "Off"), ("lms", "LMS"), ("spectral", "Spectral"));
        NrLevel = Integer("nr-level", "NR level", 0, 30, "-- dB --");
        Input = Choice("input", "Input", ("iq", "I/Q"), ("qi", "Q/I"), ("if", "IF"));
        IqGain = Number("iq-gain", "I/Q gain", -3, 3, "-- dB --");
        IqPhase = Number("iq-phase", "I/Q phase", -10, 10, "-- deg --");
        Test = Switch("test", "Test tone");
        TestOffset = Integer("test-offset", "Tone offset", -45000, 45000, "-- Hz --");
        TestLevel = Number("test-level", "Tone level", -120, 0, "-- dBFS --", 1);
        TestNoise = Integer("test-noise", "Noise", -150, 0, "-- dBFS --");
        TestKey = Integer("test-key", "Keying", 0, 5000, "-- ms --");
        Filter = Choice("filter", "Filter", ("designed", "Designed"), ("downloaded", "Downloaded"));

        Bands =
        [
            Band(EqualizerBand.Bass, "bass", 50, 400),
            Band(EqualizerBand.Middle, "middle", 300, 5000),
            Band(EqualizerBand.Treble, "treble", 500, 8000),
        ];
        Volume = Number("volume", "Volume", -60, 0, "-- dB --");
        Output = Switch("output");

        Input.ValueChanged += _ => CommandManager.InvalidateRequerySuggested();
        Mode.ValueChanged += _ =>
        {
            RaisePropertyChanged(nameof(IsCw));
            RaisePropertyChanged(nameof(IsFm));
            RaiseMeters();
        };
        Nr.ValueChanged += _ => RaisePropertyChanged(nameof(NrGainText));
        Squelch.ValueChanged += _ => RaisePropertyChanged(nameof(SquelchMark));
        foreach (ParameterViewModel edge in new[] { Low, High, Stopband, Mode, Pitch, Width, Filter })
        {
            edge.ValueChanged += _ => FilterChanged();
        }

        ShowViewCommand = new RelayCommand(shown => View = shown as string ?? ReceiverView);
        NullImageCommand = new RelayCommand(() => _ = NullImageAsync(), () => IsRunning && !isWorking && Input.Selected is not null and not "if");
        CancelCommand = new RelayCommand(() => cancel?.Cancel(), () => cancel is not null);

        InitializeFilter();
        InitializeDesigner();
    }

    // --- views -----------------------------------------------------------------------------------

    public ICommand ShowViewCommand { get; }

    /// <summary>The view on show: <see cref="ReceiverView"/>, <see cref="FilterView"/> or <see cref="DesignerView"/>.</summary>
    public string View
    {
        get => view;
        set
        {
            if (SetProperty(ref view, value))
            {
                RaisePropertyChanged(nameof(IsReceiverView));
                RaisePropertyChanged(nameof(IsFilterView));
                RaisePropertyChanged(nameof(IsDesignerView));
            }
        }
    }

    public bool IsReceiverView => view == ReceiverView;

    public bool IsFilterView => view == FilterView;

    public bool IsDesignerView => view == DesignerView;

    /// <summary>A measurement, a download or the image null is under way.</summary>
    public bool IsWorking
    {
        get => isWorking;
        private set
        {
            if (SetProperty(ref isWorking, value))
            {
                CommandManager.InvalidateRequerySuggested();
            }
        }
    }

    public ICommand CancelCommand { get; }

    // --- parameters ------------------------------------------------------------------------------

    public ParameterViewModel Mode { get; }

    /// <summary>The IF (parameter "offset"): with an IF input the IF, with I/Q the station's distance from the I/Q centre.</summary>
    public ParameterViewModel Offset { get; }

    public ParameterViewModel Low { get; }

    public ParameterViewModel High { get; }

    public ParameterViewModel Width { get; }

    public ParameterViewModel Stopband { get; }

    public ParameterViewModel Pitch { get; }

    public ParameterViewModel Agc { get; }

    public ParameterViewModel AgcGain { get; }

    public ParameterViewModel Hang { get; }

    public ParameterViewModel Decay { get; }

    public ParameterViewModel FmDeviation { get; }

    public ParameterViewModel Deemphasis { get; }

    public ParameterViewModel Squelch { get; }

    public ParameterViewModel Nr { get; }

    public ParameterViewModel NrLevel { get; }

    public ParameterViewModel Input { get; }

    public ParameterViewModel IqGain { get; }

    public ParameterViewModel IqPhase { get; }

    public ParameterViewModel Test { get; }

    public ParameterViewModel TestOffset { get; }

    public ParameterViewModel TestLevel { get; }

    public ParameterViewModel TestNoise { get; }

    public ParameterViewModel TestKey { get; }

    public ParameterViewModel Filter { get; }

    public IReadOnlyList<EqualizerBandViewModel> Bands { get; }

    public ParameterViewModel Volume { get; }

    public ParameterViewModel Output { get; }

    public bool IsCw => Mode.Selected == "cw";

    public bool IsFm => Mode.Selected == "fm";

    // --- meters ----------------------------------------------------------------------------------

    /// <summary>The S-meter, dBFS in the channel; null while unknown.</summary>
    public double? SMeter => meter?.SMeterDbfs;

    public double? SquelchMark => Squelch.Value is double squelch && squelch > -150 ? squelch : null;

    public string SMeterText => meter is { } m ? Decibel(m.SMeterDbfs, "dBFS") : "-- dBFS";

    public string AgcGainText => meter is { } m ? Decibel(m.AgcGainDb, "dB", plus: true) : "--";

    public string AudioText => meter is { } m ? Decibel(m.AudioDbfs, "dBFS") : "--";

    public string CpuText => meter is { } m ? string.Create(CultureInfo.CurrentCulture, $"{m.CpuPercent:0} %") : "--";

    public string SquelchText => meter is { } m ? m.SquelchOpen ? "open" : "closed" : "--";

    public bool IsSquelchClosed => meter is { SquelchOpen: false };

    /// <summary>The FM discriminator's tuning offset, shown in FM.</summary>
    public string FmOffsetText =>
        meter is { } m && IsFm ? string.Create(CultureInfo.CurrentCulture, $"FM offset {m.FmOffsetHz:+0;-0;0} Hz") : string.Empty;

    public string NrGainText =>
        meter is { } m && Nr.Selected == "spectral"
            ? string.Create(CultureInfo.CurrentCulture, $"spectral NR gain {m.NrGainDb:0.0} dB")
            : string.Empty;

    /// <summary>The meters as the board reported them (five times a second while the receiver runs).</summary>
    public void ApplyMeter(ReceiverMeter? reported)
    {
        meter = reported;
        RaiseMeters();
    }

    public ICommand NullImageCommand { get; }

    /// <summary>The image null's progress or result.</summary>
    public string NullText
    {
        get => nullText;
        private set => SetProperty(ref nullText, value);
    }

    /// <summary>Searches the I/Q correction for the deepest image of a steady carrier in the channel.</summary>
    public async Task NullImageAsync()
    {
        if (isWorking)
        {
            return;
        }

        Begin();
        NullText = "Searching …";
        CancellationToken token = cancel!.Token;
        try
        {
            await Run(
                async board =>
                {
                    try
                    {
                        ImageNullResult result = await ImageNull.RunAsync(board, new Progress<string>(text => NullText = text), token)
                            .ConfigureAwait(true);
                        NullText = string.Format(CultureInfo.CurrentCulture,
                            "Image rejection {0:0.0} dB → {1:0.0} dB (gain {2:0.00} dB, phase {3:0.00} deg).",
                            result.RejectionBeforeDb, result.RejectionAfterDb, result.GainDb, result.PhaseDeg);
                    }
                    catch (OperationCanceledException)
                    {
                        NullText = "Cancelled; mode, filter and AGC restored.";
                        throw new OperationCanceledException("Image null cancelled; mode, filter and AGC restored.");
                    }
                    catch (InvalidOperationException exception)
                    {
                        NullText = exception.Message;
                        throw;
                    }
                    finally
                    {
                        await RereadAsync(board, "mode", "pitch", "low", "high", "width", "offset", "agc", "hang", "decay", "iq-gain", "iq-phase")
                            .ConfigureAwait(true);
                    }
                },
                "Image null done: the I/Q corrections found are in force.").ConfigureAwait(true);
        }
        finally
        {
            End();
            if (NullText == "Searching …")
            {
                NullText = string.Empty;          // nothing ran: not connected, or the window was busy
            }
        }
    }

    // --- application life ------------------------------------------------------------------------

    /// <summary>The receiver started: its meters and its filter's state.</summary>
    internal async Task LoadAsync(BoardClient board)
    {
        ApplyMeter(await board.GetMeterAsync().ConfigureAwait(true));
        await RefreshFilterStatusAsync(board).ConfigureAwait(true);
    }

    internal override void Forget()
    {
        base.Forget();
        cancel?.Cancel();
        ApplyMeter(null);
        ApplyFilterStatus(null);
    }

    protected override void OnRunningChanged()
    {
        CommandManager.InvalidateRequerySuggested();
    }

    public void Dispose()
    {
        cancel?.Cancel();
    }

    // --- inside ----------------------------------------------------------------------------------

    private EqualizerBandViewModel Band(EqualizerBand band, string name, double minimumHertz, double maximumHertz) =>
        new(band,
            Number($"{name}-gain", $"{band} gain", -12, 12, "-- dB --"),
            Number($"{name}-frequency", band == EqualizerBand.Middle ? "Centre" : "Corner", minimumHertz, maximumHertz, "-- Hz --", 0));

    /// <summary>The values a board action changed by its own requests (the board reports only what it changed itself).</summary>
    private async Task RereadAsync(BoardClient board, params string[] names)
    {
        foreach (string name in names)
        {
            if (TryGetParameter(name, out ParameterViewModel parameter) && parameter.IsAvailable)
            {
                parameter.Apply(await board.GetAsync(name).ConfigureAwait(true), fromBoard: false);
            }
        }
    }

    private void Begin()
    {
        cancel = new CancellationTokenSource();
        IsWorking = true;
    }

    private void End()
    {
        cancel?.Dispose();
        cancel = null;
        IsWorking = false;
    }

    private void RaiseMeters()
    {
        RaisePropertyChanged(nameof(SMeter));
        RaisePropertyChanged(nameof(SMeterText));
        RaisePropertyChanged(nameof(AgcGainText));
        RaisePropertyChanged(nameof(AudioText));
        RaisePropertyChanged(nameof(CpuText));
        RaisePropertyChanged(nameof(SquelchText));
        RaisePropertyChanged(nameof(IsSquelchClosed));
        RaisePropertyChanged(nameof(FmOffsetText));
        RaisePropertyChanged(nameof(NrGainText));
    }

    private static string Decibel(double value, string unit, bool plus = false) =>
        value.ToString(plus ? "+0.0;-0.0;0.0" : "0.0", CultureInfo.CurrentCulture) + " " + unit;
}
