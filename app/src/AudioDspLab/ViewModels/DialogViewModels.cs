using System.IO;
using System.Collections.ObjectModel;
using System.Globalization;
using System.Windows.Input;
using AudioDspLab.Protocol;

namespace AudioDspLab.ViewModels;

/// <summary>A dialog's message line.</summary>
public abstract class DialogViewModel : ObservableObject
{
    private string message = string.Empty;
    private bool isError;
    private bool isBusy;

    protected DialogViewModel(BoardClient client) => Client = client ?? throw new ArgumentNullException(nameof(client));

    protected BoardClient Client { get; }

    public string Message
    {
        get => message;
        private set => SetProperty(ref message, value);
    }

    public bool IsError
    {
        get => isError;
        private set => SetProperty(ref isError, value);
    }

    public bool IsBusy
    {
        get => isBusy;
        private set => SetProperty(ref isBusy, value);
    }

    /// <summary>Asks the person; the window sets it to a message box.</summary>
    public Func<string, bool> Confirm { get; set; } = _ => true;

    protected async Task RunAsync(Func<Task> action, string success)
    {
        if (isBusy)
        {
            return;
        }

        IsBusy = true;
        try
        {
            await action().ConfigureAwait(true);
            Message = success;
            IsError = false;
        }
        catch (BoardErrorException exception)
        {
            Message = exception.Code == "out-of-range" ? $"Refused: {Explain()}" : $"Refused: {exception.Code} {exception.Detail}";
            IsError = true;
        }
        catch (Exception exception) when (exception is IOException or TimeoutException)
        {
            Message = exception.Message;
            IsError = true;
        }
        finally
        {
            IsBusy = false;
            CommandManager.InvalidateRequerySuggested();
        }
    }

    /// <summary>What an out-of-range refusal means in this dialog.</summary>
    protected abstract string Explain();
}

/// <summary>One station slot in the station window.</summary>
public sealed class StationRow : ObservableObject
{
    private string name;
    private string url;
    private string savedName;
    private string savedUrl;

    internal StationRow(Station station, StationEditorViewModel editor)
    {
        Slot = station.Slot;
        name = savedName = station.Name;
        url = savedUrl = station.Url;
        SaveCommand = new RelayCommand(() => _ = editor.SaveAsync(this), () => IsDirty && !editor.IsBusy);
        RestoreCommand = new RelayCommand(() => _ = editor.RestoreAsync(this), () => !editor.IsBusy);
    }

    public int Slot { get; }

    public string Name
    {
        get => name;
        set
        {
            if (SetProperty(ref name, value ?? string.Empty))
            {
                RaisePropertyChanged(nameof(IsDirty));
            }
        }
    }

    public string Url
    {
        get => url;
        set
        {
            if (SetProperty(ref url, value ?? string.Empty))
            {
                RaisePropertyChanged(nameof(IsDirty));
            }
        }
    }

    public bool IsDirty => name != savedName || url != savedUrl;

    public ICommand SaveCommand { get; }

    public ICommand RestoreCommand { get; }

    internal void Load(Station station)
    {
        savedName = station.Name;
        savedUrl = station.Url;
        Name = station.Name;
        Url = station.Url;
        RaisePropertyChanged(nameof(IsDirty));
    }

    internal bool NameChanged => name != savedName;

    internal bool UrlChanged => url != savedUrl;
}

/// <summary>The eight stations the board keeps: name and http:// URL each.</summary>
public sealed class StationEditorViewModel : DialogViewModel
{
    public StationEditorViewModel(BoardClient client)
        : base(client)
    {
        RestoreAllCommand = new RelayCommand(() => _ = RestoreAllAsync(), () => !IsBusy);
    }

    public ObservableCollection<StationRow> Rows { get; } = [];

    public ICommand RestoreAllCommand { get; }

    public async Task LoadAsync() =>
        await RunAsync(async () =>
        {
            IReadOnlyList<Station> stations = await Client.GetStationsAsync().ConfigureAwait(true);
            foreach (Station station in stations)
            {
                StationRow? row = Rows.FirstOrDefault(candidate => candidate.Slot == station.Slot);
                if (row is null)
                {
                    Rows.Add(new StationRow(station, this));
                }
                else
                {
                    row.Load(station);
                }
            }
        }, "Stations read from the board.").ConfigureAwait(true);

    internal Task SaveAsync(StationRow row) =>
        RunAsync(async () =>
        {
            if (row.NameChanged)
            {
                await Client.SetStationNameAsync(row.Slot, row.Name.Trim()).ConfigureAwait(true);
            }

            if (row.UrlChanged)
            {
                await Client.SetStationUrlAsync(row.Slot, row.Url.Trim()).ConfigureAwait(true);
            }

            await ReloadAsync().ConfigureAwait(true);
        }, string.Format(CultureInfo.CurrentCulture, "Station {0} saved on the board.", row.Slot));

    internal Task RestoreAsync(StationRow row) =>
        RunAsync(async () =>
        {
            await Client.RestoreStationAsync(row.Slot).ConfigureAwait(true);
            await ReloadAsync().ConfigureAwait(true);
        }, string.Format(CultureInfo.CurrentCulture, "Station {0} is back to its default.", row.Slot));

    private Task RestoreAllAsync() =>
        Confirm("Give every station its default name and URL back?")
            ? RunAsync(async () =>
            {
                await Client.RestoreStationAsync(null).ConfigureAwait(true);
                await ReloadAsync().ConfigureAwait(true);
            }, "Every station is back to its default.")
            : Task.CompletedTask;

    private async Task ReloadAsync()
    {
        foreach (Station station in await Client.GetStationsAsync().ConfigureAwait(true))
        {
            Rows.FirstOrDefault(row => row.Slot == station.Slot)?.Load(station);
        }
    }

    protected override string Explain() =>
        "a name has 1 to 63 bytes; a URL starts with http:// (no https), has no spaces and at most 200 characters.";
}

/// <summary>One WiFi network slot: the SSID shown, the password only ever entered.</summary>
public sealed class WifiRow : ObservableObject
{
    private string ssid;
    private bool hasPassword;

    internal WifiRow(WifiNetwork network, WifiEditorViewModel editor)
    {
        Slot = network.Slot;
        ssid = network.Ssid;
        hasPassword = network.HasPassword;
        SaveCommand = new RelayCommand(() => _ = editor.SaveAsync(this), () => !editor.IsBusy && ssid.Trim().Length > 0);
        ClearCommand = new RelayCommand(() => _ = editor.ClearAsync(this), () => !editor.IsBusy);
    }

    public int Slot { get; }

    public string Title => Slot == 0 ? "Network 1 (tried first)" : "Network 2";

    public string Ssid
    {
        get => ssid;
        set => SetProperty(ref ssid, value ?? string.Empty);
    }

    /// <summary>What the password box holds; the window sets it, nothing reads it back from the board.</summary>
    public string Password { get; set; } = string.Empty;

    public string PasswordState => hasPassword ? "a password is stored" : "no password stored";

    public ICommand SaveCommand { get; }

    public ICommand ClearCommand { get; }

    internal void Load(WifiNetwork network)
    {
        Ssid = network.Ssid;
        hasPassword = network.HasPassword;
        Password = string.Empty;
        RaisePropertyChanged(nameof(PasswordState));
        PasswordCleared?.Invoke();
    }

    /// <summary>The window empties its password box.</summary>
    public event Action? PasswordCleared;
}

/// <summary>The networks the radio may use, kept on the board.</summary>
public sealed class WifiEditorViewModel : DialogViewModel
{
    private readonly Action<string>? usePanelAddress;
    private string connectionText = string.Empty;
    private string panelText = string.Empty;
    private string panelAddress;

    /// <param name="panelAddress">The front panel's address the application knows, if any.</param>
    /// <param name="usePanelAddress">Puts an address in the port list ("Network: ...").</param>
    public WifiEditorViewModel(BoardClient client, string? panelAddress = null, Action<string>? usePanelAddress = null)
        : base(client)
    {
        this.panelAddress = panelAddress ?? string.Empty;
        this.usePanelAddress = usePanelAddress;
        UsePanelCommand = new RelayCommand(UsePanel, () => this.usePanelAddress is not null && this.panelAddress.Trim().Length > 0);
    }

    /// <summary>What the front panel reports of its network.</summary>
    public string PanelText
    {
        get => panelText;
        private set => SetProperty(ref panelText, value);
    }

    /// <summary>The address to reach the front panel at: what it reports, its .local name, or one the router keeps for it.</summary>
    public string PanelAddress
    {
        get => panelAddress;
        set => SetProperty(ref panelAddress, value ?? string.Empty);
    }

    public ICommand UsePanelCommand { get; }

    public ObservableCollection<WifiRow> Rows { get; } = [];

    public string ConnectionText
    {
        get => connectionText;
        private set => SetProperty(ref connectionText, value);
    }

    public Task LoadAsync() => RunAsync(ReloadAsync, "Networks read from the board.");

    internal Task SaveAsync(WifiRow row) =>
        RunAsync(async () =>
        {
            await Client.SetWifiAsync(row.Slot, row.Ssid.Trim(), row.Password).ConfigureAwait(true);
            await ReloadAsync().ConfigureAwait(true);
        }, $"Network {row.Slot + 1} saved; the radio connects again.");

    internal Task ClearAsync(WifiRow row) =>
        Confirm($"Forget network {row.Slot + 1}?")
            ? RunAsync(async () =>
            {
                await Client.ClearWifiAsync(row.Slot).ConfigureAwait(true);
                await ReloadAsync().ConfigureAwait(true);
            }, $"Network {row.Slot + 1} forgotten.")
            : Task.CompletedTask;

    private async Task ReloadAsync()
    {
        (IReadOnlyList<WifiNetwork> networks, WifiConnection connection) = await Client.GetWifiAsync().ConfigureAwait(true);
        foreach (WifiNetwork network in networks)
        {
            WifiRow? row = Rows.FirstOrDefault(candidate => candidate.Slot == network.Slot);
            if (row is null)
            {
                Rows.Add(new WifiRow(network, this));
            }
            else
            {
                row.Load(network);
            }
        }

        ConnectionText = connection.Slot is int slot
            ? $"The radio is connected through network {slot + 1}, address {connection.Address}."
            : "The radio is not connected (it connects while the radio application runs).";

        try
        {
            PanelStatus panel = await Client.GetPanelAsync().ConfigureAwait(true);
            PanelText = panel.State switch
            {
                "online" => $"The front panel is online at {panel.Address}{(panel.Clients > 0 ? ", and a PC works through it" : string.Empty)}.",
                "connecting" => "The front panel is joining the WiFi.",
                "failed" => "The front panel cannot join either network.",
                _ => "No front panel reports a network: none is fitted, or no network is stored.",
            };
            if (panel.IsOnline && PanelAddress.Trim().Length == 0)
            {
                PanelAddress = panel.Address;
            }
        }
        catch (BoardErrorException)
        {
            PanelText = "This board's firmware has no front panel port; update it for WiFi control.";
        }
    }

    private void UsePanel()
    {
        usePanelAddress?.Invoke(PanelAddress.Trim());
        PanelText = $"\"{MainViewModel.NetworkPortPrefix}{PanelAddress.Trim()}\" is in the port list: choose it under Board on, then Connect.";
    }

    protected override string Explain() =>
        "an SSID has 1 to 32 characters; a password has 8 to 63 characters, or none for an open network.";
}

/// <summary>Talks the protocol by hand: requests typed in, responses and events shown.</summary>
public sealed class ConsoleViewModel : ObservableObject, IDisposable
{
    private const int MaximumLines = 2000;

    // requests that only read: the window need not read the board again
    // after them. A command in the second set only reads when typed bare.
    private static readonly HashSet<string> ReadOnlyCommands = new(StringComparer.OrdinalIgnoreCase)
    {
        "help", "ping", "echo", "info", "status", "apps", "params", "get", "pots", "stations", "radio",
        "points", "level", "voice", "midi", "load", "aux", "backup", "diag",
    };

    private static readonly HashSet<string> QueriesWhenBare = new(StringComparer.OrdinalIgnoreCase)
    {
        "app", "potctl", "startup", "service", "led", "events", "wifi", "sweep", "reference",
    };

    private readonly BoardClient client;
    private readonly Func<Task>? afterChange;
    private readonly SynchronizationContext ui;
    private readonly List<string> history = [];
    private readonly List<string> lines = [];
    private string input = string.Empty;
    private int historyIndex;
    private bool showEvents = true;

    /// <param name="afterChange">
    /// Called after a request that may have changed the board (a set, another
    /// application, a station ...), so that the main window reads it again.
    /// </param>
    public ConsoleViewModel(BoardClient client, Func<Task>? afterChange = null)
    {
        this.client = client ?? throw new ArgumentNullException(nameof(client));
        this.afterChange = afterChange;
        ui = SynchronizationContext.Current ?? new SynchronizationContext();
        client.EventReceived += OnEvent;
        SendCommand = new RelayCommand(() => _ = SendAsync());
        Add("Type a request and press Enter; 'help' lists them. Events are shown as they come.");
    }

    /// <summary>The log's lines, oldest first, at most <see cref="MaximumLines"/>.</summary>
    public IReadOnlyList<string> Lines => lines;

    /// <summary>The log as one text, for a read-only text box (selectable, copyable).</summary>
    public string Text => string.Join(Environment.NewLine, lines);

    public string Input
    {
        get => input;
        set => SetProperty(ref input, value ?? string.Empty);
    }

    public bool ShowEvents
    {
        get => showEvents;
        set => SetProperty(ref showEvents, value);
    }

    public ICommand SendCommand { get; }

    /// <summary>Up and Down walk through the requests sent before.</summary>
    public void Recall(int step)
    {
        if (history.Count == 0)
        {
            return;
        }

        historyIndex = Math.Clamp(historyIndex + step, 0, history.Count);
        Input = historyIndex < history.Count ? history[historyIndex] : string.Empty;
    }

    public void Dispose() => client.EventReceived -= OnEvent;

    private async Task SendAsync()
    {
        string request = input.Trim();
        if (request.Length == 0)
        {
            return;
        }

        Input = string.Empty;
        history.Remove(request);
        history.Add(request);
        historyIndex = history.Count;
        Add($"> {request}");
        try
        {
            BoardResponse response = await client.SendAsync(request).ConfigureAwait(true);
            foreach (string line in response.RawLines)
            {
                Add(line);
            }

            if (afterChange is not null && MayChange(request))
            {
                await afterChange().ConfigureAwait(true);
            }
        }
        catch (BoardErrorException exception)
        {
            Add($"err {exception.Code} {exception.Detail}".TrimEnd());
        }
        catch (Exception exception) when (exception is IOException or TimeoutException or ArgumentException)
        {
            Add($"(no answer: {exception.Message})");
        }
    }

    /// <summary>Whether a request may have changed the board; queries do not.</summary>
    public static bool MayChange(string request)
    {
        IReadOnlyList<string> words = ProtocolText.Tokenize(request) ?? [];
        if (words.Count == 0 || ReadOnlyCommands.Contains(words[0]))
        {
            return false;
        }

        return !(words.Count == 1 && QueriesWhenBare.Contains(words[0]));
    }

    private void OnEvent(BoardEvent boardEvent)
    {
        string line = "! " + boardEvent.Name + (boardEvent.Values.Count > 0 ? " " + string.Join(' ', boardEvent.Values.Select(Show)) : string.Empty);
        ui.Post(_ =>
        {
            if (showEvents)
            {
                Add(line);
            }
        }, null);
    }

    // a word with spaces or quotes was a quoted text on the wire
    private static string Show(string word) =>
        word.Length == 0 || word.Any(c => c is ' ' or '"' or '\\') ? ProtocolText.Quote(word) : word;

    private void Add(string line)
    {
        lines.Add(line);
        if (lines.Count > MaximumLines)
        {
            lines.RemoveRange(0, lines.Count - MaximumLines);
        }

        RaisePropertyChanged(nameof(Text));
    }
}
