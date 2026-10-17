using System.IO;
using System.Collections.ObjectModel;
using System.Globalization;
using System.Windows.Input;
using System.Windows.Threading;
using AudioDspLab.Protocol;
using AudioDspLab.Settings;
using AudioDspLab.Simulation;

namespace AudioDspLab.ViewModels;

/// <summary>
/// The main window: the connection to a board, the application it runs,
/// one page per application, the pots, power-up, service mode and stored
/// settings.
/// </summary>
/// <remarks>
/// The board owns every value; its pots may change them beside the
/// window. The window shows what the board reports: every write returns
/// the value the board took, and the board's events (a pot moved, the
/// application changed, a sweep point was measured) arrive without
/// polling. Writes to one parameter are coalesced, so a spinning dial
/// sends only the latest value once the previous write is done.
/// </remarks>
public sealed class MainViewModel : ObservableObject, IDisposable
{
    public const string SimulatorPort = "Simulator (demo)";

    private static readonly TimeSpan TickInterval = TimeSpan.FromMilliseconds(250);

    private readonly SynchronizationContext ui;
    private readonly Func<string, ILineTransport> transportFactory;
    private readonly SettingsStore settingsStore;
    private readonly AppPreferences preferences;
    private readonly DispatcherTimer timer;
    private readonly Dictionary<ParameterViewModel, string> pendingWrites = [];
    private readonly HashSet<ParameterViewModel> writing = [];
    private readonly IReadOnlyList<ApplicationPageViewModel> pages;
    private BoardClient? client;
    private string? selectedPort;
    private bool isConnecting;
    private bool isBusy;
    private bool errorStatus;
    private string statusInfo = "Not connected. Choose the board's port and connect.";
    private string firmwareText = string.Empty;
    private string? activeApplication;
    private string? applicationState;
    private string lastError = "none";
    private string? loadedApplication;
    private bool reloading;
    private bool reloadAgain;
    private bool? isPotentiometerControlOn;
    private bool? isStartupRestoring;
    private bool isServiceConfirmed;
    private string backupText = string.Empty;
    private string settingsName = string.Empty;
    private string? selectedSettingsName;
    private int tickCount;
    private bool measuring;
    private bool disposed;

    public MainViewModel(
        Func<string, ILineTransport>? transportFactory = null,
        SettingsStore? settingsStore = null,
        AppPreferences? preferences = null)
    {
        ui = SynchronizationContext.Current ?? new SynchronizationContext();
        this.transportFactory = transportFactory ?? CreateTransport;
        this.settingsStore = settingsStore ?? new SettingsStore(SettingsStore.DefaultDirectory);
        this.preferences = preferences ?? AppPreferences.Load();

        Generator = new GeneratorPageViewModel(WriteParameterAsync, RunAsync);
        Equalizer = new EqualizerPageViewModel(WriteParameterAsync, RunAsync);
        Radio = new RadioViewModel(WriteParameterAsync, RunAsync);
        Analyzer = new AnalyzerPageViewModel(WriteParameterAsync, RunAsync);
        Synth = new SynthPageViewModel(WriteParameterAsync, RunAsync);
        pages = [Generator, Equalizer, Radio, Analyzer, Synth];
        Potentiometers = [.. Enumerable.Range(0, 4).Select(index => new PotentiometerViewModel(index))];

        RefreshPortsCommand = new RelayCommand(RefreshPorts, () => !IsConnected && !isConnecting);
        ConnectCommand = new RelayCommand(() => _ = ConnectAsync(), () => CanConnect);
        DisconnectCommand = new RelayCommand(() => Disconnect("Disconnected."), () => IsConnected);
        SelectApplicationCommand = new RelayCommand(
            application => _ = SelectApplicationAsync(application as string),
            _ => CanSelectApplication);
        ApplicationKeys = [.. Applications.All.Select(name => new ChoiceKey(name, ShortName(name), SelectApplicationCommand))];
        TogglePotentiometerControlCommand = new RelayCommand(
            () => _ = SetPotentiometerControlAsync(isPotentiometerControlOn != true),
            () => IsConnected && !isBusy && isPotentiometerControlOn is not null);
        ToggleStartupCommand = new RelayCommand(
            () => _ = SetStartupAsync(isStartupRestoring != true),
            () => IsConnected && !isBusy && isStartupRestoring is not null);
        EnterServiceCommand = new RelayCommand(() => _ = EnterServiceAsync(), () => CanEnterService);
        LeaveServiceCommand = new RelayCommand(() => _ = LeaveServiceAsync(), () => CanLeaveService);
        SaveSettingsCommand = new RelayCommand(() => _ = SaveSettingsAsync(), () => CanSaveSettings);
        ApplySettingsCommand = new RelayCommand(() => _ = ApplySettingsAsync(), () => CanApplySettings);
        DeleteSettingsCommand = new RelayCommand(DeleteSettings, () => selectedSettingsName is not null && !isBusy);

        RefreshPorts();
        ReloadSettingsNames(null);
        timer = new DispatcherTimer { Interval = TickInterval };
        timer.Tick += (_, _) => Tick();
        timer.Start();
    }

    // --- connection -----------------------------------------------------------------

    public ObservableCollection<string> Ports { get; } = [];

    public string? SelectedPort
    {
        get => selectedPort;
        set => SetProperty(ref selectedPort, value);
    }

    public ICommand RefreshPortsCommand { get; }

    public ICommand ConnectCommand { get; }

    public ICommand DisconnectCommand { get; }

    public bool IsConnected => client is not null;

    public bool IsConnecting
    {
        get => isConnecting;
        private set
        {
            if (SetProperty(ref isConnecting, value))
            {
                RaisePropertyChanged(nameof(CanConnect));
            }
        }
    }

    public bool CanConnect => !IsConnected && !isConnecting && selectedPort is not null;

    public bool IsDisconnected => !IsConnected;

    public string ConnectionText =>
        client is { } connected ? $"{connected.Name}: {firmwareText}" : isConnecting ? "Connecting …" : "Not connected";

    public string FirmwareText
    {
        get => firmwareText;
        private set => SetProperty(ref firmwareText, value);
    }

    public string Title => client is { } connected ? $"Audio-DSP-Lab — {connected.Name}" : "Audio-DSP-Lab";

    /// <summary>The themes to choose from: Classic, Studio dark, Studio light.</summary>
    public IReadOnlyList<string> ThemeNames => Themes.ThemeResources.Names;

    /// <summary>The theme in use; choosing another raises <see cref="ThemeChanged"/> and remembers it.</summary>
    public string SelectedTheme
    {
        get => Themes.ThemeResources.Normalize(preferences.Theme);
        set
        {
            string theme = Themes.ThemeResources.Normalize(value);
            if (theme == SelectedTheme)
            {
                return;
            }

            preferences.Theme = theme;
            preferences.Save();
            RaisePropertyChanged();
            ThemeChanged?.Invoke(theme);
        }
    }

    /// <summary>The window applies the theme (it rebuilds itself).</summary>
    public event Action<string>? ThemeChanged;

    /// <summary>Asks the person; the window sets it to a message box.</summary>
    public Func<string, bool> Confirm { get; set; } = _ => true;

    // --- application ------------------------------------------------------------------

    /// <summary>One lit key per application.</summary>
    public IReadOnlyList<ChoiceKey> ApplicationKeys { get; }

    public GeneratorPageViewModel Generator { get; }

    public EqualizerPageViewModel Equalizer { get; }

    public RadioViewModel Radio { get; }

    public AnalyzerPageViewModel Analyzer { get; }

    public SynthPageViewModel Synth { get; }

    public IReadOnlyList<PotentiometerViewModel> Potentiometers { get; }

    public ICommand SelectApplicationCommand { get; }

    /// <summary>The protocol name of the application the board runs, or null while unknown.</summary>
    public string? ActiveApplication
    {
        get => activeApplication;
        private set
        {
            if (!SetProperty(ref activeApplication, value))
            {
                return;
            }

            RaisePropertyChanged(nameof(ActiveApplicationDisplayName));
            foreach (ChoiceKey key in ApplicationKeys)
            {
                key.IsSelected = key.Name == value;
            }

            RaisePropertyChanged(nameof(IsGeneratorSelected));
            RaisePropertyChanged(nameof(IsEqualizerSelected));
            RaisePropertyChanged(nameof(IsRadioSelected));
            RaisePropertyChanged(nameof(IsAnalyzerSelected));
            RaisePropertyChanged(nameof(IsSigmaStudioSelected));
            RaisePropertyChanged(nameof(IsSynthSelected));
            RaisePropertyChanged(nameof(IsEqualizerPageShown));
            RaisePropertyChanged(nameof(IsGeneratorPageShown));
            RaisePropertyChanged(nameof(SignalHeading));
            RaiseGates();
        }
    }

    public string ActiveApplicationDisplayName => Applications.DisplayName(activeApplication);

    public bool IsGeneratorSelected => activeApplication == Applications.Generator;

    public bool IsEqualizerSelected => activeApplication == Applications.Equalizer;

    public bool IsRadioSelected => activeApplication == Applications.Radio;

    public bool IsAnalyzerSelected => activeApplication == Applications.Analyzer;

    public bool IsSigmaStudioSelected => activeApplication == Applications.SigmaStudio;

    public bool IsSynthSelected => activeApplication == Applications.Synthesizer;

    public bool IsEqualizerPageShown => IsEqualizerSelected || IsRadioSelected;

    public bool IsGeneratorPageShown => !IsEqualizerPageShown && !IsAnalyzerSelected && !IsSigmaStudioSelected && !IsSynthSelected;

    public string SignalHeading =>
        IsSigmaStudioSelected ? "SigmaStudio"
        : IsSynthSelected ? "Synthesizer"
        : IsEqualizerPageShown || IsAnalyzerSelected ? "Response"
        : "Signal";

    /// <summary>idle, loading, running, failed or service; null while unknown.</summary>
    public string? ApplicationState
    {
        get => applicationState;
        private set
        {
            if (!SetProperty(ref applicationState, value))
            {
                return;
            }

            RaisePropertyChanged(nameof(ApplicationStateText));
            RaisePropertyChanged(nameof(IsStateRunning));
            RaisePropertyChanged(nameof(IsStateFailed));
            RaisePropertyChanged(nameof(IsServiceMode));
            RaiseGates();
        }
    }

    public string ApplicationStateText => applicationState ?? "unknown";

    public bool IsStateRunning => applicationState == BoardStatus.Running;

    public bool IsStateFailed => applicationState == BoardStatus.Failed;

    public bool IsServiceMode => applicationState == BoardStatus.Service;

    public bool IsSigmaStudioRunning => IsSigmaStudioSelected && IsStateRunning;

    /// <summary>Why the page on show is unavailable, or nothing.</summary>
    public string GateExplanation
    {
        get
        {
            if (!IsConnected)
            {
                return "not connected";
            }

            string name = Applications.DisplayName(activeApplication).ToLowerInvariant();
            return applicationState switch
            {
                BoardStatus.Loading => $"starting the {name} …",
                BoardStatus.Failed => $"the {name} did not start: {lastError}",
                BoardStatus.Service => "service mode: the DSP belongs to SigmaStudio",
                BoardStatus.Idle => "no application runs",
                _ => string.Empty
            };
        }
    }

    public bool CanSelectApplication => IsConnected && !isBusy && applicationState is not null && !IsServiceMode;

    public bool IsBusy
    {
        get => isBusy;
        private set
        {
            if (SetProperty(ref isBusy, value))
            {
                Analyzer.IsBusy = value;
                RaiseGates();
            }
        }
    }

    // --- pots, power-up, service ---------------------------------------------------------

    public bool? IsPotentiometerControlOn
    {
        get => isPotentiometerControlOn;
        private set
        {
            if (!SetProperty(ref isPotentiometerControlOn, value))
            {
                return;
            }

            RaisePropertyChanged(nameof(PotentiometerControlText));
            foreach (PotentiometerViewModel pot in Potentiometers)
            {
                pot.IsControlOn = value == true;
            }
        }
    }

    public string PotentiometerControlText =>
        isPotentiometerControlOn switch
        {
            true => "Pot control on",
            false => "Pot control off",
            null => "Pot control ?"
        };

    public ICommand TogglePotentiometerControlCommand { get; }

    /// <summary>What a power-up starts: the application selected last (true) or the SigmaStudio mode.</summary>
    public bool? IsStartupRestoring
    {
        get => isStartupRestoring;
        private set
        {
            if (SetProperty(ref isStartupRestoring, value))
            {
                RaisePropertyChanged(nameof(StartupText));
            }
        }
    }

    public string StartupText =>
        isStartupRestoring switch
        {
            true => "Last application",
            false => "SigmaStudio mode",
            null => "Power-up ?"
        };

    public ICommand ToggleStartupCommand { get; }

    /// <summary>The person agrees that service mode hands the DSP to SigmaStudio; consumed by the attempt.</summary>
    public bool IsServiceConfirmed
    {
        get => isServiceConfirmed;
        set => SetProperty(ref isServiceConfirmed, value);
    }

    public bool CanEnterService => IsConnected && !isBusy && applicationState is not null && !IsServiceMode && isServiceConfirmed;

    public bool CanLeaveService => IsConnected && !isBusy && IsServiceMode;

    public ICommand EnterServiceCommand { get; }

    public ICommand LeaveServiceCommand { get; }

    /// <summary>The copy of the person's SigmaStudio program the board keeps in its flash.</summary>
    public string BackupText
    {
        get => backupText;
        private set => SetProperty(ref backupText, value);
    }

    // --- stored settings -------------------------------------------------------------------

    public ObservableCollection<string> SettingsNames { get; } = [];

    public string SettingsName
    {
        get => settingsName;
        set => SetProperty(ref settingsName, value ?? string.Empty);
    }

    public string? SelectedSettingsName
    {
        get => selectedSettingsName;
        set
        {
            if (SetProperty(ref selectedSettingsName, value) && value is not null)
            {
                SettingsName = value;
            }
        }
    }

    public string SettingsDirectory => settingsStore.Directory;

    public bool CanSaveSettings =>
        IsConnected && !isBusy && IsStateRunning && activeApplication is not null
        && activeApplication != Applications.SigmaStudio && SettingsStore.IsValidName(settingsName.Trim());

    public bool CanApplySettings => selectedSettingsName is not null && CanSelectApplication;

    public ICommand SaveSettingsCommand { get; }

    public ICommand ApplySettingsCommand { get; }

    public ICommand DeleteSettingsCommand { get; }

    // --- status line ---------------------------------------------------------------------------

    public bool ErrorStatus
    {
        get => errorStatus;
        private set => SetProperty(ref errorStatus, value);
    }

    public string StatusInfo
    {
        get => statusInfo;
        private set => SetProperty(ref statusInfo, value);
    }

    /// <summary>The client, for the station, WiFi and console windows; null while not connected.</summary>
    public BoardClient? Client => client;

    // --- operations ------------------------------------------------------------------------------

    public void RefreshPorts()
    {
        string? keep = selectedPort ?? preferences.LastPort;
        Ports.Clear();
        foreach (string port in SerialLineTransport.PortNames())
        {
            Ports.Add(port);
        }

        Ports.Add(SimulatorPort);
        SelectedPort = keep is not null && Ports.Contains(keep) ? keep : Ports.FirstOrDefault();
    }

    /// <summary>Connects to the port remembered from last time, if it is there.</summary>
    public Task ConnectToLastPortAsync() =>
        preferences.LastPort is { } port && port != SimulatorPort && Ports.Contains(port) ? ConnectAsync() : Task.CompletedTask;

    public async Task ConnectAsync()
    {
        if (!CanConnect || selectedPort is not { } port)
        {
            return;
        }

        IsConnecting = true;
        RaiseConnection();
        Succeed($"Connecting to {port} …");
        BoardClient? candidate = null;
        try
        {
            candidate = new BoardClient(transportFactory(port));
            candidate.StatusChanged += status => Post(() => OnStatus(status));
            candidate.ValueChanged += (name, value) => Post(() => OnValue(name, value));
            candidate.PotsChanged += values => Post(() => ApplyPots(values));
            candidate.RadioChanged += (state, rate) => Post(() => Radio.ApplyStatus(state, rate));
            candidate.TitleChanged += title => Post(() => Radio.ApplyTitle(title));
            candidate.SweepChanged += sweep => Post(() => Analyzer.ApplySweep(sweep));
            candidate.PointMeasured += point => Post(() => Analyzer.AddPoint(point));
            candidate.VoiceChanged += voice => Post(() => Synth.ApplyVoice(voice));
            candidate.Restarted += info => Post(() => _ = OnRestartedAsync());
            BoardClient connecting = candidate;
            candidate.Disconnected += exception => Post(() =>
            {
                if (client == connecting)
                {
                    Disconnect($"The connection to {connecting.Name} broke: {exception.Message}", failed: true);
                }
            });

            BoardInfo info = await Task.Run(() => connecting.ConnectAsync()).ConfigureAwait(true);
            client = candidate;
            FirmwareText = info.Kit.Length > 0 ? $"{info.Name} {info.Firmware} · {info.KitDisplayName}" : $"{info.Name} {info.Firmware}";
            preferences.LastPort = port;
            preferences.Save();
            RaiseConnection();
            await ReadBoardAsync().ConfigureAwait(true);
            Succeed($"Connected to {port}: {info.Name} {info.Firmware}.");
        }
        catch (Exception exception)
        {
            candidate?.Dispose();
            client = null;
            Fail(exception switch
            {
                UnauthorizedAccessException => $"{port} is in use by another program (Arduino's serial monitor, SigmaStudio, another Audio-DSP-Lab?).",
                FileNotFoundException or IOException when !port.Equals(SimulatorPort, StringComparison.Ordinal) =>
                    $"{port} cannot be opened: {exception.Message}",
                _ => exception.Message
            });
        }
        finally
        {
            IsConnecting = false;
            RaiseConnection();
        }
    }

    public void Disconnect(string message, bool failed = false)
    {
        BoardClient? closing = client;
        client = null;
        closing?.Dispose();
        pendingWrites.Clear();
        loadedApplication = null;
        foreach (ApplicationPageViewModel page in pages)
        {
            page.IsRunning = false;
            page.Forget();
        }

        ActiveApplication = null;
        ApplicationState = null;
        IsPotentiometerControlOn = null;
        IsStartupRestoring = null;
        FirmwareText = string.Empty;
        foreach (PotentiometerViewModel pot in Potentiometers)
        {
            pot.Value = null;
            pot.Role = PotentiometerViewModel.Unassigned;
            pot.IsForwarded = false;
        }

        RaiseConnection();
        if (failed)
        {
            Fail(message);
        }
        else
        {
            Succeed(message);
        }
    }

    public Task SelectApplicationAsync(string? application)
    {
        if (application is null || !CanSelectApplication || (application == activeApplication && IsStateRunning))
        {
            return Task.CompletedTask;
        }

        return RunExclusiveAsync(
            $"{Applications.DisplayName(application)} selected.",
            board => board.SelectApplicationAsync(application));
    }

    public Task SetPotentiometerControlAsync(bool on) =>
        RunExclusiveAsync(
            on ? "Pot control on: a knob takes over once it is turned." : "Pot control off.",
            async board => IsPotentiometerControlOn = await board.SetPotControlAsync(on).ConfigureAwait(true),
            reload: false);

    public Task SetStartupAsync(bool on) =>
        RunExclusiveAsync(
            on ? "At power-up: the application selected last." : "At power-up: the SigmaStudio mode.",
            async board => IsStartupRestoring = (await board.SetStartupAsync(on).ConfigureAwait(true)).On,
            reload: false);

    public async Task EnterServiceAsync()
    {
        if (!CanEnterService)
        {
            return;
        }

        try
        {
            await RunExclusiveAsync(
                "Service mode: the DSP is open for SigmaStudio and the USBi.",
                board => board.SetServiceAsync(true)).ConfigureAwait(true);
        }
        finally
        {
            IsServiceConfirmed = false;
        }
    }

    public Task LeaveServiceAsync() =>
        CanLeaveService
            ? RunExclusiveAsync("Service mode left; the application is restored.", board => board.SetServiceAsync(false))
            : Task.CompletedTask;

    /// <summary>Stores the running application and its values as the board reports them (never the output).</summary>
    public Task SaveSettingsAsync()
    {
        string name = settingsName.Trim();
        if (!CanSaveSettings)
        {
            return Task.CompletedTask;
        }

        if (SettingsNames.Contains(name) && !Confirm($"Replace the stored settings '{name}'?"))
        {
            return Task.CompletedTask;
        }

        string application = activeApplication!;
        return RunExclusiveAsync(
            $"Settings '{name}' saved.",
            async board =>
            {
                IReadOnlyDictionary<string, string> values = await board.GetValuesAsync().ConfigureAwait(true);
                var stored = new StoredSettings(name, application, [.. values.Where(pair => pair.Key != "output")]);
                if (!settingsStore.Save(stored))
                {
                    throw new IOException($"Settings '{name}' could not be stored in {settingsStore.Directory}.");
                }

                ReloadSettingsNames(name);
            },
            reload: false);
    }

    /// <summary>Selects the stored application if another runs, then sets the values; outputs stay as they are.</summary>
    public Task ApplySettingsAsync()
    {
        if (selectedSettingsName is not { } name || !CanApplySettings)
        {
            return Task.CompletedTask;
        }

        if (settingsStore.Read(name) is not { } settings || !Applications.All.Contains(settings.Application))
        {
            Fail($"Settings '{name}' could not be read.");
            return Task.CompletedTask;
        }

        return RunExclusiveAsync(
            $"Settings '{name}' applied.",
            async board =>
            {
                if (activeApplication != settings.Application || !IsStateRunning)
                {
                    await board.SelectApplicationAsync(settings.Application).ConfigureAwait(true);
                }

                // a value refused once is tried again after the others: a sweep's start must stay below its stop
                List<KeyValuePair<string, string>> pending = [.. settings.Values.Where(pair => pair.Key != "output")];
                BoardErrorException? refusal = null;
                for (int pass = 0; pass < 2 && pending.Count > 0; pass++)
                {
                    List<KeyValuePair<string, string>> refused = [];
                    foreach (KeyValuePair<string, string> pair in pending)
                    {
                        try
                        {
                            await board.SetAsync(pair.Key, pair.Value).ConfigureAwait(true);
                        }
                        catch (BoardErrorException exception)
                        {
                            refused.Add(pair);
                            refusal = exception;
                        }
                    }

                    pending = refused;
                }

                if (pending.Count > 0 && refusal is not null)
                {
                    throw refusal;
                }
            });
    }

    public void DeleteSettings()
    {
        if (selectedSettingsName is not { } name || !Confirm($"Delete the stored settings '{name}'?"))
        {
            return;
        }

        if (settingsStore.Delete(name))
        {
            ReloadSettingsNames(null);
            SettingsName = string.Empty;
            Succeed($"Settings '{name}' deleted.");
        }
        else
        {
            Fail($"Settings '{name}' could not be deleted.");
        }
    }

    /// <summary>The stations again, after the station window changed them.</summary>
    public async Task ReloadStationsAsync()
    {
        if (client is { } board)
        {
            try
            {
                Radio.SetStations(await board.GetStationsAsync().ConfigureAwait(true));
            }
            catch (Exception exception) when (exception is BoardErrorException or IOException or TimeoutException)
            {
                Fail($"Stations could not be read: {exception.Message}");
            }
        }
    }

    public void Dispose()
    {
        if (disposed)
        {
            return;
        }

        disposed = true;
        timer.Stop();
        client?.Dispose();
        client = null;
    }

    // --- inside ------------------------------------------------------------------------------------

    private static string ShortName(string application) =>
        application switch
        {
            Applications.Analyzer => "Response",
            Applications.Synthesizer => "Synth",
            Applications.SigmaStudio => "SigmaStudio",
            _ => Applications.DisplayName(application)
        };

    private static ILineTransport CreateTransport(string port) =>
        port == SimulatorPort ? new SimulatedBoard(demo: true) : new SerialLineTransport(port);

    private void Post(Action action) => ui.Post(_ => action(), null);

    private void Succeed(string message)
    {
        StatusInfo = message;
        ErrorStatus = false;
    }

    private void Fail(string message)
    {
        StatusInfo = message;
        ErrorStatus = true;
    }

    private static string Describe(Exception exception) =>
        exception switch
        {
            BoardErrorException board => $"Refused: {board.Code} {board.Detail}".TrimEnd(),
            TimeoutException or IOException => exception.Message,
            _ => $"Failed: {exception.Message}"
        };

    private void RaiseConnection()
    {
        RaisePropertyChanged(nameof(IsConnected));
        RaisePropertyChanged(nameof(IsDisconnected));
        RaisePropertyChanged(nameof(CanConnect));
        RaisePropertyChanged(nameof(ConnectionText));
        RaisePropertyChanged(nameof(Title));
        RaisePropertyChanged(nameof(Client));
        RaiseGates();
    }

    private void RaiseGates()
    {
        RaisePropertyChanged(nameof(GateExplanation));
        RaisePropertyChanged(nameof(CanSelectApplication));
        RaisePropertyChanged(nameof(IsSigmaStudioRunning));
        CommandManager.InvalidateRequerySuggested();
    }

    /// <summary>Everything the board knows, read after connecting or a restart.</summary>
    private async Task ReadBoardAsync()
    {
        if (client is not { } board)
        {
            return;
        }

        IsPotentiometerControlOn = await board.GetPotControlAsync().ConfigureAwait(true);
        IsStartupRestoring = (await board.GetStartupAsync().ConfigureAwait(true)).On;
        ApplyPots(await board.GetPotsAsync().ConfigureAwait(true));
        Radio.SetStations(await board.GetStationsAsync().ConfigureAwait(true));
        loadedApplication = null;
        await ReloadAsync().ConfigureAwait(true);
    }

    /// <summary>
    /// Reads the whole board again, after the console changed something the
    /// window was not told about (a set, a station, events switched off).
    /// </summary>
    public async Task RefreshFromBoardAsync()
    {
        if (client is not { } board || isBusy)
        {
            return;
        }

        try
        {
            await board.SendAsync("events on").ConfigureAwait(true);   // the window lives on them
            await ReadBoardAsync().ConfigureAwait(true);
        }
        catch (Exception exception) when (exception is BoardErrorException or IOException or TimeoutException or FormatException)
        {
            Fail($"Reading the board after the console's request failed: {exception.Message}");
        }
    }

    private async Task OnRestartedAsync()
    {
        if (client is not { } board)
        {
            return;
        }

        Fail("The board restarted.");
        try
        {
            await board.SendAsync("events on").ConfigureAwait(true);
            await ReadBoardAsync().ConfigureAwait(true);
            Succeed("The board restarted; everything read again.");
        }
        catch (Exception exception) when (exception is BoardErrorException or IOException or TimeoutException)
        {
            Fail($"The board restarted and does not answer: {exception.Message}");
        }
    }

    private void OnStatus(BoardStatus status)
    {
        ApplyStatus(status);
        string? running = status.IsRunning ? status.Application : null;
        if (running != loadedApplication && !isBusy)
        {
            _ = ReloadAsync();
        }
    }

    private void ApplyStatus(BoardStatus status)
    {
        lastError = status.LastError;
        ActiveApplication = status.Application;
        ApplicationState = status.State;
        Equalizer.IsRadioSource = status.Application == Applications.Radio;
        foreach (ApplicationPageViewModel page in pages)
        {
            page.IsRunning = status.IsRunning && page.Serves(status.Application);
        }

        RaisePropertyChanged(nameof(GateExplanation));
    }

    private void OnValue(string name, string value)
    {
        foreach (ApplicationPageViewModel page in pages)
        {
            if (page.Serves(activeApplication) && page.TryGetParameter(name, out ParameterViewModel parameter))
            {
                parameter.Apply(value, fromBoard: true);
            }
        }
    }

    private void ApplyPots(IReadOnlyList<int> values)
    {
        for (int index = 0; index < Potentiometers.Count && index < values.Count; index++)
        {
            Potentiometers[index].Value = values[index];
        }
    }

    /// <summary>
    /// Reads the status and the running application's parameters and
    /// state. Calls that arrive meanwhile run it once more afterwards.
    /// </summary>
    private async Task ReloadAsync()
    {
        if (reloading)
        {
            reloadAgain = true;
            return;
        }

        reloading = true;
        try
        {
            do
            {
                reloadAgain = false;
                await LoadOnceAsync().ConfigureAwait(true);
            }
            while (reloadAgain && client is not null);
        }
        catch (Exception exception) when (exception is BoardErrorException or IOException or TimeoutException or FormatException)
        {
            Fail($"Reading the board failed: {exception.Message}");
        }
        finally
        {
            reloading = false;
        }
    }

    private async Task LoadOnceAsync()
    {
        if (client is not { } board)
        {
            return;
        }

        BoardStatus status = await board.GetStatusAsync().ConfigureAwait(true);
        ApplyStatus(status);
        string? running = status.IsRunning ? status.Application : null;
        foreach (ApplicationPageViewModel page in pages.Where(page => !page.Serves(running)))
        {
            page.Forget();
        }

        IReadOnlyList<ParameterDescriptor> descriptors =
            running is null or Applications.SigmaStudio ? [] : await board.GetParametersAsync().ConfigureAwait(true);
        foreach (ApplicationPageViewModel page in pages.Where(page => page.Serves(running)))
        {
            page.Attach(descriptors);
        }

        switch (running)
        {
            case Applications.Radio:
                RadioStatus radio = await board.GetRadioAsync().ConfigureAwait(true);
                Radio.ApplyStatus(radio.State, radio.SampleRate);
                Radio.ApplyTitle(radio.Title);
                await ReadWifiAsync(board).ConfigureAwait(true);
                break;
            case Applications.Analyzer:
                await Analyzer.LoadAsync(board).ConfigureAwait(true);
                break;
            case Applications.Synthesizer:
                Synth.ApplyVoice(await board.GetVoiceAsync().ConfigureAwait(true));
                break;
            case Applications.SigmaStudio:
                StudioBackup backup = await board.GetBackupAsync().ConfigureAwait(true);
                BackupText = backup.Exists
                    ? string.Format(CultureInfo.CurrentCulture, "The board keeps a copy of your program: {0} bytes, CRC {1}.", backup.Length, backup.Crc)
                    : "The board keeps no copy of a program of yours yet.";
                break;
        }

        IsPotentiometerControlOn = await board.GetPotControlAsync().ConfigureAwait(true);
        loadedApplication = running;
        AssignPotentiometerRoles(running);
        CommandManager.InvalidateRequerySuggested();
    }

    private async Task ReadWifiAsync(BoardClient board)
    {
        (IReadOnlyList<WifiNetwork> networks, WifiConnection connection) = await board.GetWifiAsync().ConfigureAwait(true);
        Radio.WifiText = connection.Slot is int slot && slot < networks.Count
            ? $"WiFi: {networks[slot].Ssid} · {connection.Address}"
            : networks.All(network => network.IsEmpty)
                ? "No WiFi network set: open WiFi to enter one."
                : $"WiFi: {string.Join(" or ", networks.Where(network => !network.IsEmpty).Select(network => network.Ssid))}";
    }

    private static readonly Dictionary<string, string> PotRoles = new()
    {
        ["frequency"] = "Frequency",
        ["amplitude"] = "Amplitude",
        ["waveform"] = "Waveform",
        ["bass-gain"] = "Bass",
        ["middle-gain"] = "Middle",
        ["treble-gain"] = "Treble",
        ["volume"] = "Volume",
        ["level"] = "Level",
        ["osc1-width"] = "Osc 1 width",
        ["cutoff"] = "Cutoff",
        ["emphasis"] = "Emphasis",
    };

    private void AssignPotentiometerRoles(string? running)
    {
        bool studio = running == Applications.SigmaStudio;
        foreach (PotentiometerViewModel pot in Potentiometers)
        {
            pot.IsForwarded = studio;
            pot.Role = studio ? $"ADC{pot.Index}" : PotentiometerViewModel.Unassigned;
        }

        if (studio)
        {
            return;
        }

        foreach (ParameterViewModel parameter in pages.Where(page => page.Serves(running)).SelectMany(page => page.Parameters))
        {
            if (parameter.Knob is { Length: 2 } knob && knob[0] == 'P' && knob[1] is >= '1' and <= '4')
            {
                Potentiometers[knob[1] - '1'].Role = PotRoles.GetValueOrDefault(parameter.Name, parameter.Title);
            }
        }
    }

    /// <summary>Writes a value, coalesced: while one write of the parameter is in flight, only the newest waits.</summary>
    private Task WriteParameterAsync(ParameterViewModel parameter, string value)
    {
        if (client is null || isBusy)
        {
            return Task.CompletedTask;
        }

        pendingWrites[parameter] = value;
        return writing.Contains(parameter) ? Task.CompletedTask : DrainWritesAsync(parameter);
    }

    private async Task DrainWritesAsync(ParameterViewModel parameter)
    {
        writing.Add(parameter);
        try
        {
            while (client is { } board && pendingWrites.Remove(parameter, out string? value))
            {
                try
                {
                    string taken = await board.SetAsync(parameter.Name, value).ConfigureAwait(true);
                    parameter.Apply(taken, fromBoard: false);
                    Succeed($"{parameter.Title}: {parameter.DescribeValue(taken)}.");
                }
                catch (Exception exception) when (exception is BoardErrorException or IOException or TimeoutException)
                {
                    Fail($"{parameter.Title}: {Describe(exception)}");
                    await RereadAsync(board, parameter).ConfigureAwait(true);
                }
            }
        }
        finally
        {
            writing.Remove(parameter);
        }
    }

    private static async Task RereadAsync(BoardClient board, ParameterViewModel parameter)
    {
        try
        {
            parameter.Apply(await board.GetAsync(parameter.Name).ConfigureAwait(true), fromBoard: false);
        }
        catch (Exception exception) when (exception is BoardErrorException or IOException or TimeoutException)
        {
        }
    }

    /// <summary>A page's action, with its outcome on the status line.</summary>
    private async Task RunAsync(Func<BoardClient, Task> action, string successMessage)
    {
        if (client is not { } board || isBusy)
        {
            return;
        }

        try
        {
            await action(board).ConfigureAwait(true);
            Succeed(successMessage);
        }
        catch (Exception exception) when (exception is BoardErrorException or IOException or TimeoutException or FormatException)
        {
            Fail(Describe(exception));
        }

        CommandManager.InvalidateRequerySuggested();
    }

    /// <summary>One deliberate act on its own: nothing else meanwhile, the state read again afterwards.</summary>
    private async Task RunExclusiveAsync(string successMessage, Func<BoardClient, Task> action, bool reload = true)
    {
        if (client is not { } board || isBusy)
        {
            return;
        }

        IsBusy = true;
        try
        {
            await action(board).ConfigureAwait(true);
            Succeed(successMessage);
        }
        catch (Exception exception) when (exception is BoardErrorException or IOException or TimeoutException or FormatException)
        {
            Fail(Describe(exception));
        }
        finally
        {
            IsBusy = false;
        }

        if (reload && client is not null)
        {
            loadedApplication = null;
            await ReloadAsync().ConfigureAwait(true);
        }
    }

    private void ReloadSettingsNames(string? select)
    {
        SettingsNames.Clear();
        foreach (string name in settingsStore.Names())
        {
            SettingsNames.Add(name);
        }

        SelectedSettingsName = select is not null && SettingsNames.Contains(select) ? select : null;
    }

    private void Tick()
    {
        DateTime now = DateTime.UtcNow;
        foreach (ApplicationPageViewModel page in pages)
        {
            page.DecayMarkers(now);
        }

        // the analyzer's input is measured between sweeps, twice a second
        if (++tickCount % 2 == 0 && client is { } board && Analyzer.IsRunning && !Analyzer.IsSweeping && !isBusy && !measuring)
        {
            _ = MeasureInputAsync(board);
        }
    }

    private async Task MeasureInputAsync(BoardClient board)
    {
        measuring = true;
        try
        {
            (double? left, double? right) = await board.MeasureLevelAsync().ConfigureAwait(true);
            Analyzer.ApplyInput(left, right);
        }
        catch (Exception exception) when (exception is BoardErrorException or IOException or TimeoutException)
        {
            Analyzer.ApplyInput(null, null);
        }
        finally
        {
            measuring = false;
        }
    }
}

/// <summary>What the application remembers between runs: the last port.</summary>
public sealed class AppPreferences
{
    private static readonly string FilePath =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Audio-DSP-Lab", "preferences.txt");

    public string? LastPort { get; set; }

    /// <summary>The theme's name (Themes.ThemeResources), or null for the default.</summary>
    public string? Theme { get; set; }

    public static AppPreferences Load()
    {
        var preferences = new AppPreferences();
        try
        {
            foreach (string line in File.Exists(FilePath) ? File.ReadAllLines(FilePath) : [])
            {
                if (line.StartsWith("port=", StringComparison.Ordinal))
                {
                    preferences.LastPort = line[5..].Trim();
                }
                else if (line.StartsWith("theme=", StringComparison.Ordinal))
                {
                    preferences.Theme = line[6..].Trim();
                }
            }
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
        }

        return preferences;
    }

    public void Save()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
            File.WriteAllText(FilePath, $"port={LastPort}\ntheme={Theme}\n");
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
        }
    }
}
