using System.Collections.ObjectModel;
using System.Globalization;
using System.IO;
using System.Windows.Input;
using AudioDspLab.FrontEnd;
using AudioDspLab.Protocol;
using AudioDspLab.Receiver;

namespace AudioDspLab.ViewModels;

/// <summary>
/// The receiver application (design/receiver.md, section 8), ported from
/// DSP-Box-Lab's Receiver, Filter and Designer pages: three views chosen as
/// the analysis page chooses its parts. The Receiver view holds the mode, the
/// frequency dial, the meters, AGC, NR, squelch, the input and its I/Q
/// correction, the SDR front end and the equalizer; the Filter view the
/// channel filter's edges on a chart, its measurement and the test tone; the
/// Designer view filters made on the PC and downloaded.
/// </summary>
/// <remarks>
/// The front end (the Uno with the Si5351A) has its own serial port and is
/// not a board action: its outcomes show on its card. The dial is the RF
/// frequency while the front end is connected and the input is I/Q
/// (<see cref="ReceiverTuner"/> moves the front end only when needed and sets
/// the board's offset), the offset itself otherwise.
/// </remarks>
public sealed partial class ReceiverPageViewModel : ApplicationPageViewModel, IDisposable
{
    public const string ReceiverView = "receiver";
    public const string FilterView = "filter";
    public const string DesignerView = "designer";

    /// <summary>The front end's usual port (design/receiver.md, R4), until another was used.</summary>
    public const string DefaultFrontEndPort = "COM24";

    private static readonly int[] DriveSteps = [0, 2, 4, 6, 8];

    private readonly AppPreferences? preferences;
    private readonly LatestRequest<long> dialRequests = new();
    private string view = ReceiverView;
    private ReceiverMeter? meter;
    private FrontEndClient? frontEnd;
    private ReceiverTuner? tuner;
    private BoardClient? tunerBoard;
    private FrontEndStatus? frontEndStatus;
    private string? selectedFrontEndPort;
    private string frontEndText = "Not connected.";
    private string correctionText = string.Empty;
    private string nullText = string.Empty;
    private bool isConnectingFrontEnd;
    private bool isSimulatedBoard;
    private bool isWorking;
    private CancellationTokenSource? cancel;

    public ReceiverPageViewModel(ParameterWrite write, BoardAction run, AppPreferences? preferences = null)
        : base(write, run, Applications.Receiver)
    {
        this.preferences = preferences;

        Mode = Choice("mode", "Mode", ("off", "Off"), ("am", "AM"), ("usb", "USB"), ("lsb", "LSB"), ("cw", "CW"), ("fm", "FM"));
        Offset = Integer("offset", "Offset", -21000, 21000, "-- Hz --");
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

        Offset.ValueChanged += _ => RaiseDial();
        Input.ValueChanged += _ => RaiseDial();
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
        RefreshFrontEndPortsCommand = new RelayCommand(RefreshFrontEndPorts, () => frontEnd is null && !isConnectingFrontEnd);
        ConnectFrontEndCommand = new RelayCommand(() => _ = ConnectFrontEndAsync(),
            () => frontEnd is null && !isConnectingFrontEnd && selectedFrontEndPort is not null);
        DisconnectFrontEndCommand = new RelayCommand(() => _ = DisconnectFrontEndAsync("Disconnected."), () => frontEnd is not null);
        SelectDriveCommand = new RelayCommand(
            drive => _ = SetDriveAsync(int.Parse((string)drive!, CultureInfo.InvariantCulture)),
            drive => frontEnd is not null && drive is string);
        DriveKeys = [.. DriveSteps.Select(ma => new ChoiceKey(ma.ToString(CultureInfo.InvariantCulture), ma == 0 ? "Off" : $"{ma} mA", SelectDriveCommand))];
        SetCorrectionCommand = new RelayCommand(() => _ = SetCorrectionAsync(), () => frontEnd is not null);
        NullImageCommand = new RelayCommand(() => _ = NullImageAsync(), () => IsRunning && !isWorking && Input.Selected is not null and not "if");
        CancelCommand = new RelayCommand(() => cancel?.Cancel(), () => cancel is not null);

        InitializeFilter();
        InitializeDesigner();
        selectedFrontEndPort = string.IsNullOrWhiteSpace(preferences?.FrontEndPort) ? DefaultFrontEndPort : preferences.FrontEndPort;
        RefreshFrontEndPorts();
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

    // --- dial ------------------------------------------------------------------------------------

    /// <summary>The dial is the RF frequency: the front end is connected and the input is I/Q.</summary>
    public bool IsRfDial => frontEnd is not null && frontEndStatus is not null && Input.Selected is not null and not "if";

    public bool IsOffsetDial => !IsRfDial;

    public double RfMinimum => ReceiverDial.Minimum(true);

    public double RfMaximum => ReceiverDial.Maximum(true);

    /// <summary>The RF frequency the receiver listens to: front end plus offset; null without the front end.</summary>
    public double? RfDialValue
    {
        get => IsRfDial ? ReceiverDial.Show(Offset.Value, frontEndStatus?.FrequencyHz) : null;
        set
        {
            if (value is double dial && IsRfDial && IsRunning && dial != RfDialValue)
            {
                _ = TuneAsync(dial);
            }
        }
    }

    public string DialTitle => IsRfDial ? "Frequency" : "Offset";

    public string DialUnitLine => IsRfDial ? "-- Hz, through the front end --" : "-- Hz, the receiver's offset --";

    public string TuningText =>
        IsRfDial && frontEndStatus is { } s
            ? string.Format(CultureInfo.CurrentCulture, "Front end {0:#,0} Hz (LO {1:0.000000} MHz) · offset {2}",
                s.FrequencyHz, s.LoHz / 1e6, Offset.Value is double o ? o.ToString("+#,0;-#,0;0", CultureInfo.CurrentCulture) + " Hz" : "--")
            : frontEnd is not null
                ? "The IF input: the dial sets the offset, the front end stays."
                : "No front end: the dial sets the offset from the I/Q input's centre.";

    public string BandText =>
        RfDialValue is double rf && !SdrTuner.InBand((long)rf)
            ? string.Format(CultureInfo.CurrentCulture, "Outside the 20 m band-pass filter ({0:0.0} to {1:0.0} MHz): strongly attenuated.",
                SdrTuner.BandLowHz / 1e6, SdrTuner.BandHighHz / 1e6)
            : string.Empty;

    /// <summary>Tunes the dial (newest request wins while one is under way).</summary>
    public Task TuneAsync(double dial) =>
        dialRequests.SubmitAsync(ReceiverDial.RfFor(dial), hz => Run(
            async board =>
            {
                if (frontEnd is null)
                {
                    return;
                }

                ReceiverTuner current = await TunerAsync(board).ConfigureAwait(true);
                SdrTuning tuning = await current.TuneAsync(hz).ConfigureAwait(true);
                Offset.Apply(tuning.OffsetHz.ToString(CultureInfo.InvariantCulture), fromBoard: false);
                if (frontEndStatus is { } s && s.FrequencyHz != current.FrontEndHz)
                {
                    frontEndStatus = s with { FrequencyHz = current.FrontEndHz };
                    RaiseFrontEnd();
                }
            },
            string.Format(CultureInfo.CurrentCulture, "Tuned to {0:#,0} Hz.", hz)));

    // --- front end -------------------------------------------------------------------------------

    public ObservableCollection<string> FrontEndPorts { get; } = [];

    public string? SelectedFrontEndPort
    {
        get => selectedFrontEndPort;
        set => SetProperty(ref selectedFrontEndPort, value);
    }

    public ICommand RefreshFrontEndPortsCommand { get; }

    public ICommand ConnectFrontEndCommand { get; }

    public ICommand DisconnectFrontEndCommand { get; }

    public bool IsFrontEndConnected => frontEnd is not null;

    public bool IsFrontEndDisconnected => frontEnd is null;

    /// <summary>The front end's identity, or why it is not connected.</summary>
    public string FrontEndText
    {
        get => frontEndText;
        private set => SetProperty(ref frontEndText, value);
    }

    public string FrontEndStatusText =>
        frontEndStatus is { } s
            ? string.Format(CultureInfo.CurrentCulture, "{0:#,0} Hz · drive {1} · {2} ppb",
                s.FrequencyHz, s.DriveMa == 0 ? "off" : $"{s.DriveMa} mA", s.CorrectionPpb)
            : string.Empty;

    public IReadOnlyList<ChoiceKey> DriveKeys { get; }

    public ICommand SelectDriveCommand { get; }

    /// <summary>The crystal correction to set, ppb; Enter sets it.</summary>
    public string CorrectionText
    {
        get => correctionText;
        set => SetProperty(ref correctionText, value ?? string.Empty);
    }

    public ICommand SetCorrectionCommand { get; }

    public ICommand NullImageCommand { get; }

    /// <summary>The image null's progress or result.</summary>
    public string NullText
    {
        get => nullText;
        private set => SetProperty(ref nullText, value);
    }

    /// <summary>The board is the simulator: the simulated front end is offered too.</summary>
    public bool IsSimulatedBoard
    {
        get => isSimulatedBoard;
        set
        {
            if (SetProperty(ref isSimulatedBoard, value))
            {
                RefreshFrontEndPorts();
            }
        }
    }

    public void RefreshFrontEndPorts()
    {
        string? keep = selectedFrontEndPort;
        FrontEndPorts.Clear();
        if (isSimulatedBoard)
        {
            FrontEndPorts.Add(SimulatedFrontEndPort.SimulatorName);
        }

        foreach (string port in SerialLineTransport.PortNames())
        {
            FrontEndPorts.Add(port);
        }

        SelectedFrontEndPort = keep is not null && FrontEndPorts.Contains(keep)
            ? keep
            : isSimulatedBoard ? SimulatedFrontEndPort.SimulatorName : FrontEndPorts.FirstOrDefault();
    }

    public async Task ConnectFrontEndAsync()
    {
        if (frontEnd is not null || isConnectingFrontEnd || selectedFrontEndPort is not { } name)
        {
            return;
        }

        isConnectingFrontEnd = true;
        CommandManager.InvalidateRequerySuggested();
        FrontEndText = $"Opening {name} …";
        var candidate = new FrontEndClient(name == SimulatedFrontEndPort.SimulatorName ? new SimulatedFrontEndPort() : new SerialFrontEndPort(name));
        try
        {
            await candidate.OpenAsync().ConfigureAwait(true);
            FrontEndStatus status = await candidate.StatusAsync().ConfigureAwait(true);
            frontEnd = candidate;
            tuner = null;
            frontEndStatus = status;
            CorrectionText = status.CorrectionPpb.ToString(CultureInfo.InvariantCulture);
            FrontEndText = $"{candidate.Identity} on {candidate.Name}" + (candidate.SynthesizerFound ? "." : ": no Si5351A found!");
            if (preferences is not null && name != SimulatedFrontEndPort.SimulatorName)
            {
                preferences.FrontEndPort = name;
                preferences.Save();
            }

            RaiseFrontEnd();
            RaiseDial();
            // the station stays on the dial: the front end's frequency plus the offset in force
            if (IsRfDial && IsRunning && Offset.Value is double offset)
            {
                await TuneAsync(status.FrequencyHz + offset).ConfigureAwait(true);
            }
        }
        catch (Exception exception) when (exception is FrontEndException or TimeoutException or IOException or UnauthorizedAccessException
                                              or InvalidOperationException or ArgumentException or FormatException)
        {
            await candidate.DisposeAsync().ConfigureAwait(true);
            FrontEndText = $"{name}: {exception.Message}";
        }
        finally
        {
            isConnectingFrontEnd = false;
            CommandManager.InvalidateRequerySuggested();
        }
    }

    public async Task DisconnectFrontEndAsync(string message)
    {
        FrontEndClient? closing = frontEnd;
        frontEnd = null;
        tuner = null;
        tunerBoard = null;
        frontEndStatus = null;
        if (closing is not null)
        {
            await closing.DisposeAsync().ConfigureAwait(true);
        }

        FrontEndText = message;
        RaiseFrontEnd();
        RaiseDial();
    }

    public async Task SetDriveAsync(int milliamps)
    {
        if (frontEnd is not { } client || frontEndStatus is not { } status)
        {
            return;
        }

        await FrontEndActionAsync(async () =>
        {
            int drive = await client.SetDriveAsync(milliamps).ConfigureAwait(true);
            frontEndStatus = status with { DriveMa = drive };
            return drive == 0 ? "LO drive off." : $"LO drive {drive} mA.";
        }).ConfigureAwait(true);
    }

    public async Task SetCorrectionAsync()
    {
        if (frontEnd is not { } client || frontEndStatus is not { } status)
        {
            return;
        }

        if (!long.TryParse(correctionText.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out long ppb))
        {
            CorrectionText = status.CorrectionPpb.ToString(CultureInfo.InvariantCulture);
            FrontEndText = "The correction is a whole number of ppb, -100000 to 100000.";
            return;
        }

        await FrontEndActionAsync(async () =>
        {
            long taken = await client.SetCorrectionAsync(ppb).ConfigureAwait(true);
            frontEndStatus = status with { CorrectionPpb = taken };
            CorrectionText = taken.ToString(CultureInfo.InvariantCulture);
            return $"Crystal correction {taken} ppb.";
        }).ConfigureAwait(true);
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
        tuner = null;
        tunerBoard = null;
        ApplyMeter(null);
        ApplyFilterStatus(null);
        RaiseDial();
    }

    protected override void OnRunningChanged()
    {
        RaiseDial();
        CommandManager.InvalidateRequerySuggested();
    }

    public void Dispose()
    {
        cancel?.Cancel();
        _ = DisconnectFrontEndAsync("Closed.");
    }

    // --- inside ----------------------------------------------------------------------------------

    private EqualizerBandViewModel Band(EqualizerBand band, string name, double minimumHertz, double maximumHertz) =>
        new(band,
            Number($"{name}-gain", $"{band} gain", -12, 12, "-- dB --"),
            Number($"{name}-frequency", band == EqualizerBand.Middle ? "Centre" : "Corner", minimumHertz, maximumHertz, "-- Hz --", 0));

    private async Task<ReceiverTuner> TunerAsync(BoardClient board)
    {
        if (tuner is null || tunerBoard != board)
        {
            var created = ReceiverTuner.ForBoard(board, frontEnd!);
            await created.InitializeAsync().ConfigureAwait(true);
            tuner = created;
            tunerBoard = board;
        }

        return tuner;
    }

    private async Task FrontEndActionAsync(Func<Task<string>> action)
    {
        try
        {
            FrontEndText = await action().ConfigureAwait(true);
        }
        catch (Exception exception) when (exception is FrontEndException or FormatException)
        {
            FrontEndText = exception.Message;
        }
        catch (Exception exception) when (exception is TimeoutException or IOException or InvalidOperationException)
        {
            await DisconnectFrontEndAsync($"The front end does not answer: {exception.Message}").ConfigureAwait(true);
        }

        RaiseFrontEnd();
    }

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

    private void RaiseDial()
    {
        RaisePropertyChanged(nameof(IsRfDial));
        RaisePropertyChanged(nameof(IsOffsetDial));
        RaisePropertyChanged(nameof(RfDialValue));
        RaisePropertyChanged(nameof(DialTitle));
        RaisePropertyChanged(nameof(DialUnitLine));
        RaisePropertyChanged(nameof(TuningText));
        RaisePropertyChanged(nameof(BandText));
        CommandManager.InvalidateRequerySuggested();
    }

    private void RaiseFrontEnd()
    {
        RaisePropertyChanged(nameof(IsFrontEndConnected));
        RaisePropertyChanged(nameof(IsFrontEndDisconnected));
        RaisePropertyChanged(nameof(FrontEndStatusText));
        foreach (ChoiceKey key in DriveKeys)
        {
            key.IsSelected = frontEndStatus is { } s && key.Name == s.DriveMa.ToString(CultureInfo.InvariantCulture);
        }

        RaiseDial();
    }

    private static string Decibel(double value, string unit, bool plus = false) =>
        value.ToString(plus ? "+0.0;-0.0;0.0" : "0.0", CultureInfo.CurrentCulture) + " " + unit;
}
