using System.Globalization;
using AudioDspLab.Protocol;

namespace AudioDspLab;

/// <summary>
/// The board's commands as methods, its events as .NET events
/// (docs/Protocol.md). Events are raised on the transport's thread.
/// </summary>
public sealed class BoardClient : IDisposable
{
    /// <summary>Selecting an application writes the EEPROM and boots the DSP; copying a SigmaStudio program takes longer.</summary>
    public static readonly TimeSpan ApplicationTimeout = TimeSpan.FromSeconds(12);

    private readonly BoardConnection connection;

    public BoardClient(ILineTransport transport)
    {
        connection = new BoardConnection(transport);
        connection.EventReceived += OnEvent;
        connection.Faulted += exception => Disconnected?.Invoke(exception);
    }

    public event Action<BoardInfo>? Restarted;

    public event Action<BoardStatus>? StatusChanged;

    /// <summary>A parameter changed on the board (a pot, or the generator's P1 range): name, value.</summary>
    public event Action<string, string>? ValueChanged;

    public event Action<IReadOnlyList<int>>? PotsChanged;

    public event Action<string, int>? RadioChanged;

    public event Action<string>? TitleChanged;

    public event Action<SweepStatus>? SweepChanged;

    public event Action<SweepPoint>? PointMeasured;

    public event Action<SynthVoice>? VoiceChanged;

    public event Action<Exception>? Disconnected;

    /// <summary>Every event line as it came, for a console.</summary>
    public event Action<BoardEvent>? EventReceived;

    /// <summary>The port's name, or "Simulator".</summary>
    public string Name => connection.Name;

    public BoardInfo? Info { get; private set; }

    /// <summary>
    /// Opens the transport, finds the firmware and turns events on.
    /// </summary>
    /// <exception cref="NotSupportedException">The port answers, but not as an Audio-DSP-Lab board.</exception>
    public async Task<BoardInfo> ConnectAsync(CancellationToken cancellationToken = default)
    {
        connection.Open();
        BoardInfo info = await WaitForBoardAsync(cancellationToken).ConfigureAwait(false);
        if (info.Name != BoardInfo.ExpectedName)
        {
            throw new NotSupportedException($"{connection.Name} runs '{info.Name}', not the Audio-DSP-Lab firmware.");
        }

        if (info.Protocol != BoardInfo.SupportedProtocol)
        {
            throw new NotSupportedException(
                $"The board speaks protocol {info.Protocol}; this application speaks protocol {BoardInfo.SupportedProtocol}.");
        }

        Info = info;
        await SendAsync("events on", cancellationToken).ConfigureAwait(false);
        return info;
    }

    /// <summary>Any request, for the console; the response as the board sent it.</summary>
    public Task<BoardResponse> SendAsync(string request, CancellationToken cancellationToken = default) =>
        connection.SendAsync(request, cancellationToken: cancellationToken);

    public async Task<BoardInfo> GetInfoAsync(CancellationToken cancellationToken = default) =>
        BoardInfo.Parse((await SendAsync("info", cancellationToken).ConfigureAwait(false)).Values);

    public async Task<BoardStatus> GetStatusAsync(CancellationToken cancellationToken = default) =>
        BoardStatus.Parse((await SendAsync("status", cancellationToken).ConfigureAwait(false)).Values);

    public async Task SelectApplicationAsync(string application, CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(application);
        await connection.SendAsync($"app {application}", ApplicationTimeout, cancellationToken).ConfigureAwait(false);
    }

    public async Task<IReadOnlyList<ParameterDescriptor>> GetParametersAsync(CancellationToken cancellationToken = default)
    {
        BoardResponse response = await SendAsync("params", cancellationToken).ConfigureAwait(false);
        return [.. response.Data.Select(ParameterDescriptor.Parse)];
    }

    public async Task<IReadOnlyDictionary<string, string>> GetValuesAsync(CancellationToken cancellationToken = default)
    {
        BoardResponse response = await SendAsync("get", cancellationToken).ConfigureAwait(false);
        return response.Data.Where(words => words.Count >= 2).ToDictionary(words => words[0], words => words[1]);
    }

    public async Task<string> GetAsync(string parameter, CancellationToken cancellationToken = default) =>
        (await SendAsync($"get {parameter}", cancellationToken).ConfigureAwait(false)).Value;

    /// <summary>Sets a parameter; returns the value as the board took it.</summary>
    public async Task<string> SetAsync(string parameter, string value, CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(parameter);
        ArgumentException.ThrowIfNullOrWhiteSpace(value);
        return (await SendAsync($"set {parameter} {value}", cancellationToken).ConfigureAwait(false)).Value;
    }

    public Task<string> SetAsync(string parameter, double value, CancellationToken cancellationToken = default) =>
        SetAsync(parameter, ProtocolText.FormatNumber(value), cancellationToken);

    public Task<string> SetAsync(string parameter, bool on, CancellationToken cancellationToken = default) =>
        SetAsync(parameter, ProtocolText.FormatSwitch(on), cancellationToken);

    public async Task<IReadOnlyList<int>> GetPotsAsync(CancellationToken cancellationToken = default) =>
        [.. (await SendAsync("pots", cancellationToken).ConfigureAwait(false)).Values.Select(ProtocolText.ParseInteger)];

    public Task<bool> GetPotControlAsync(CancellationToken cancellationToken = default) =>
        SwitchAsync("potctl", null, cancellationToken);

    public Task<bool> SetPotControlAsync(bool on, CancellationToken cancellationToken = default) =>
        SwitchAsync("potctl", on, cancellationToken);

    public Task<bool> GetServiceAsync(CancellationToken cancellationToken = default) =>
        SwitchAsync("service", null, cancellationToken);

    public async Task<bool> SetServiceAsync(bool on, CancellationToken cancellationToken = default)
    {
        BoardResponse response = await connection
            .SendAsync($"service {ProtocolText.FormatSwitch(on)}", ApplicationTimeout, cancellationToken)
            .ConfigureAwait(false);
        return ProtocolText.ParseSwitch(response.Value);
    }

    /// <summary>Whether a power-up starts the application selected last, and which one that is.</summary>
    public async Task<(bool On, string Application)> GetStartupAsync(CancellationToken cancellationToken = default)
    {
        BoardResponse response = await SendAsync("startup", cancellationToken).ConfigureAwait(false);
        return (ProtocolText.ParseSwitch(response.Values[0]), response.Values[1]);
    }

    public async Task<(bool On, string Application)> SetStartupAsync(bool on, CancellationToken cancellationToken = default)
    {
        BoardResponse response = await SendAsync($"startup {ProtocolText.FormatSwitch(on)}", cancellationToken)
            .ConfigureAwait(false);
        return (ProtocolText.ParseSwitch(response.Values[0]), response.Values[1]);
    }

    // --- radio ------------------------------------------------------------------

    public async Task<RadioStatus> GetRadioAsync(CancellationToken cancellationToken = default)
    {
        BoardResponse response = await SendAsync("radio", cancellationToken).ConfigureAwait(false);
        return new RadioStatus(response.Values[0], ProtocolText.ParseInteger(response.Values[1]),
            response.Values.Count > 2 ? response.Values[2] : string.Empty);
    }

    public async Task<IReadOnlyList<Station>> GetStationsAsync(CancellationToken cancellationToken = default)
    {
        BoardResponse response = await SendAsync("stations", cancellationToken).ConfigureAwait(false);
        return [.. response.Data.Select(words => new Station(ProtocolText.ParseInteger(words[0]), words[1], words[2]))];
    }

    public Task SetStationNameAsync(int slot, string name, CancellationToken cancellationToken = default) =>
        SendAsync(Invariant($"station {slot} name {ProtocolText.Quote(name)}"), cancellationToken);

    public Task SetStationUrlAsync(int slot, string url, CancellationToken cancellationToken = default) =>
        SendAsync(Invariant($"station {slot} url {ProtocolText.Quote(url)}"), cancellationToken);

    /// <summary>One slot back to its default, or every slot with null.</summary>
    public Task RestoreStationAsync(int? slot, CancellationToken cancellationToken = default) =>
        SendAsync(slot is int number ? Invariant($"station {number} default") : "station all default", cancellationToken);

    public async Task<(IReadOnlyList<WifiNetwork> Networks, WifiConnection Connection)> GetWifiAsync(
        CancellationToken cancellationToken = default)
    {
        BoardResponse response = await SendAsync("wifi", cancellationToken).ConfigureAwait(false);
        IReadOnlyList<WifiNetwork> networks =
            [.. response.Data.Select(words => new WifiNetwork(ProtocolText.ParseInteger(words[0]), words[1], words[2] == "yes"))];
        int? slot = response.Values.Count > 0 && response.Values[0] != ProtocolText.None
            ? ProtocolText.ParseInteger(response.Values[0])
            : null;
        string address = response.Values.Count > 1 && response.Values[1] != ProtocolText.None ? response.Values[1] : string.Empty;
        return (networks, new WifiConnection(slot, address));
    }

    /// <summary>Stores a network; an empty password means an open network.</summary>
    public Task SetWifiAsync(int slot, string ssid, string password, CancellationToken cancellationToken = default) =>
        SendAsync(
            string.IsNullOrEmpty(password)
                ? Invariant($"wifi {slot} {ProtocolText.Quote(ssid)}")
                : Invariant($"wifi {slot} {ProtocolText.Quote(ssid)} {ProtocolText.Quote(password)}"),
            cancellationToken);

    public Task ClearWifiAsync(int slot, CancellationToken cancellationToken = default) =>
        SendAsync(Invariant($"wifi {slot} clear"), cancellationToken);

    // --- response analyzer --------------------------------------------------------

    public async Task<SweepStatus> GetSweepAsync(CancellationToken cancellationToken = default) =>
        SweepStatus.Parse((await SendAsync("sweep", cancellationToken).ConfigureAwait(false)).Values);

    public async Task<SweepStatus> StartSweepAsync(CancellationToken cancellationToken = default) =>
        SweepStatus.Parse((await SendAsync("sweep start", cancellationToken).ConfigureAwait(false)).Values);

    public async Task<SweepStatus> StopSweepAsync(CancellationToken cancellationToken = default) =>
        SweepStatus.Parse((await SendAsync("sweep stop", cancellationToken).ConfigureAwait(false)).Values);

    public async Task<IReadOnlyList<SweepPoint>> GetPointsAsync(int from = 0, CancellationToken cancellationToken = default)
    {
        BoardResponse response = await SendAsync(from == 0 ? "points" : Invariant($"points {from}"), cancellationToken)
            .ConfigureAwait(false);
        return [.. response.Data.Select(SweepPoint.Parse)];
    }

    public async Task<IReadOnlyList<SweepPoint>> GetReferenceAsync(CancellationToken cancellationToken = default)
    {
        BoardResponse response = await SendAsync("reference", cancellationToken).ConfigureAwait(false);
        return [.. response.Data.Select(SweepPoint.Parse)];
    }

    public Task KeepReferenceAsync(CancellationToken cancellationToken = default) =>
        SendAsync("reference keep", cancellationToken);

    /// <summary>The input levels at the tone playing now, in dBFS.</summary>
    public async Task<(double? Left, double? Right)> MeasureLevelAsync(CancellationToken cancellationToken = default)
    {
        BoardResponse response = await SendAsync("level", cancellationToken).ConfigureAwait(false);
        return (ProtocolText.ParseNumberOrNone(response.Values[0]), ProtocolText.ParseNumberOrNone(response.Values[1]));
    }

    // --- synthesizer and SigmaStudio mode -------------------------------------------

    public async Task<SynthVoice> GetVoiceAsync(CancellationToken cancellationToken = default) =>
        SynthVoice.Parse((await SendAsync("voice", cancellationToken).ConfigureAwait(false)).Values);

    public async Task<AuxInputs> GetAuxAsync(CancellationToken cancellationToken = default)
    {
        BoardResponse response = await SendAsync("aux", cancellationToken).ConfigureAwait(false);
        return new AuxInputs([.. response.Values.Take(4).Select(ProtocolText.ParseInteger)],
            response.Values.Count > 4 && ProtocolText.ParseSwitch(response.Values[4]));
    }

    public async Task<StudioBackup> GetBackupAsync(CancellationToken cancellationToken = default)
    {
        BoardResponse response = await SendAsync("backup", cancellationToken).ConfigureAwait(false);
        return new StudioBackup(response.Values[0] == "yes", ProtocolText.ParseInteger(response.Values[1]), response.Values[2]);
    }

    public void Dispose() => connection.Dispose();

    private async Task<bool> SwitchAsync(string command, bool? on, CancellationToken cancellationToken)
    {
        string request = on is bool value ? $"{command} {ProtocolText.FormatSwitch(value)}" : command;
        return ProtocolText.ParseSwitch((await SendAsync(request, cancellationToken).ConfigureAwait(false)).Value);
    }

    // a board that was just reset (some USB adapters pulse DTR when the port
    // opens) needs a moment: ask a few times before giving up
    private async Task<BoardInfo> WaitForBoardAsync(CancellationToken cancellationToken)
    {
        Exception? last = null;
        for (int attempt = 0; attempt < 4; attempt++)
        {
            try
            {
                return await GetInfoAsync(cancellationToken).ConfigureAwait(false);
            }
            catch (Exception exception) when (exception is TimeoutException or IOException or FormatException
                                                  or BoardErrorException)
            {
                last = exception;
                if (connection.IsFaulted)
                {
                    break;
                }
            }
        }

        throw new TimeoutException($"No Audio-DSP-Lab board answers on {connection.Name}.", last);
    }

    private void OnEvent(BoardEvent boardEvent)
    {
        EventReceived?.Invoke(boardEvent);
        try
        {
            IReadOnlyList<string> values = boardEvent.Values;
            switch (boardEvent.Name)
            {
                case "ready":
                    BoardInfo info = BoardInfo.Parse(values);
                    Info = info;
                    Restarted?.Invoke(info);
                    break;
                case "status":
                    StatusChanged?.Invoke(BoardStatus.Parse(values));
                    break;
                case "value" when values.Count >= 2:
                    ValueChanged?.Invoke(values[0], values[1]);
                    break;
                case "pots" when values.Count >= 4:
                    PotsChanged?.Invoke([.. values.Take(4).Select(ProtocolText.ParseInteger)]);
                    break;
                case "radio" when values.Count >= 2:
                    RadioChanged?.Invoke(values[0], ProtocolText.ParseInteger(values[1]));
                    break;
                case "title" when values.Count >= 1:
                    TitleChanged?.Invoke(values[0]);
                    break;
                case "sweep" when values.Count >= 4:
                    SweepChanged?.Invoke(SweepStatus.Parse(values));
                    break;
                case "point" when values.Count >= 4:
                    PointMeasured?.Invoke(SweepPoint.Parse(values));
                    break;
                case "voice" when values.Count >= 6:
                    VoiceChanged?.Invoke(SynthVoice.Parse(values));
                    break;
            }
        }
        catch (FormatException)
        {
            // an event this version cannot read is left out
        }
    }

    private static string Invariant(FormattableString text) => text.ToString(CultureInfo.InvariantCulture);
}
