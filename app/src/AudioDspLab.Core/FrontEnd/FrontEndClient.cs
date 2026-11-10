using System.Globalization;
using System.IO.Ports;

namespace AudioDspLab.FrontEnd;

/// <summary>A line link to the front end's Uno: the serial port or the simulated front end.</summary>
public interface IFrontEndPort : IAsyncDisposable
{
    string Name { get; }

    Task OpenAsync(CancellationToken cancellation = default);

    /// <summary>Sends one command line and returns the answer line (trimmed).</summary>
    /// <exception cref="TimeoutException">No answer.</exception>
    Task<string> CommandAsync(string line, CancellationToken cancellation = default);
}

/// <summary>The front end's settings as it reports them (S?).</summary>
public sealed record FrontEndStatus(long FrequencyHz, int DriveMa, long CorrectionPpb)
{
    public long LoHz => FrequencyHz * 4;
}

/// <summary>An answer "E ..." from the front end, or an answer that makes no sense.</summary>
public sealed class FrontEndException(string message) : Exception(message);

/// <summary>
/// The SDR front end's local oscillator (sketch SDR-Rx\SDRFrontend 1.0 of the
/// TMS320-DSP-Box, on an Arduino Uno with an Si5351A): the receive frequency in
/// Hz (the LO runs at 4 x f), the drive, the crystal correction; the Uno keeps
/// them in its EEPROM. One command at a time. Ported from DSP-Box-Lab
/// (design/receiver.md, R4).
/// </summary>
public sealed class FrontEndClient(IFrontEndPort port) : IAsyncDisposable
{
    public const long MinHz = 100_000, MaxHz = 50_000_000;
    private readonly SemaphoreSlim gate = new(1, 1);

    public string Name => port.Name;

    /// <summary>The identity line, e.g. "SDRFrontend 1.0 Si5351A ok".</summary>
    public string Identity { get; private set; } = "";

    public bool SynthesizerFound => Identity.EndsWith(" ok", StringComparison.Ordinal);

    public async Task OpenAsync(CancellationToken cancellation = default)
    {
        await port.OpenAsync(cancellation).ConfigureAwait(false);
        string id = await CommandAsync("I?", cancellation).ConfigureAwait(false);
        if (!id.StartsWith("SDRFrontend", StringComparison.Ordinal))
        {
            throw new FrontEndException($"Not the SDR front end on {port.Name}: '{id}'.");
        }

        Identity = id;
    }

    public async Task<FrontEndStatus> StatusAsync(CancellationToken cancellation = default)
    {
        string s = await CommandAsync("S?", cancellation).ConfigureAwait(false);
        // "F=14074000 D=8 C=325 LO=56296000"
        var fields = s.Split(' ', StringSplitOptions.RemoveEmptyEntries)
            .Select(f => f.Split('='))
            .Where(f => f.Length == 2)
            .ToDictionary(f => f[0], f => long.Parse(f[1], CultureInfo.InvariantCulture));
        if (!fields.TryGetValue("F", out long f) || !fields.TryGetValue("D", out long d) || !fields.TryGetValue("C", out long c))
        {
            throw new FrontEndException($"Front end: unexpected status '{s}'.");
        }

        return new FrontEndStatus(f, (int)d, c);
    }

    /// <summary>Sets the receive frequency; answers the frequency in force.</summary>
    public async Task<long> SetFrequencyAsync(long hz, CancellationToken cancellation = default) =>
        long.Parse(await CommandAsync(FormattableString.Invariant($"F {hz}"), cancellation).ConfigureAwait(false), CultureInfo.InvariantCulture);

    /// <summary>The LO's drive: 0 (off), 2, 4, 6 or 8 mA.</summary>
    public async Task<int> SetDriveAsync(int milliamps, CancellationToken cancellation = default) =>
        int.Parse(await CommandAsync(FormattableString.Invariant($"D {milliamps}"), cancellation).ConfigureAwait(false), CultureInfo.InvariantCulture);

    /// <summary>The crystal correction in ppb, -100000 .. 100000.</summary>
    public async Task<long> SetCorrectionAsync(long ppb, CancellationToken cancellation = default) =>
        long.Parse(await CommandAsync(FormattableString.Invariant($"C {ppb}"), cancellation).ConfigureAwait(false), CultureInfo.InvariantCulture);

    /// <summary>One command; returns the answer without its letter ("F 14074000" -> "14074000").</summary>
    private async Task<string> CommandAsync(string line, CancellationToken cancellation)
    {
        await gate.WaitAsync(cancellation).ConfigureAwait(false);
        try
        {
            string answer = await port.CommandAsync(line, cancellation).ConfigureAwait(false);
            if (answer.StartsWith("E ", StringComparison.Ordinal))
            {
                throw new FrontEndException("Front end: " + answer[2..]);
            }

            char expect = char.ToUpperInvariant(line[0]);
            if (answer.Length < 2 || answer[0] != expect || answer[1] != ' ')
            {
                throw new FrontEndException($"Front end: unexpected answer '{answer}' to '{line}'.");
            }

            return answer[2..];
        }
        finally
        {
            gate.Release();
        }
    }

    public ValueTask DisposeAsync() => port.DisposeAsync();
}

/// <summary>
/// The front end on its serial port: 115200 Bd, lines end with CR LF. .NET opens
/// the port without a DTR pulse, so opening does not reset the Uno (a program
/// that pulses DTR, as pyserial does by default, resets it and loses the first
/// command in its boot).
/// </summary>
public sealed class SerialFrontEndPort(string portName) : IFrontEndPort
{
    private readonly SerialPort serial = new(portName, 115200, Parity.None, 8, StopBits.One)
    {
        NewLine = "\n",
        ReadTimeout = 1000,
        WriteTimeout = 500,
        DtrEnable = false,
        RtsEnable = false,
    };

    public string Name => portName;

    public Task OpenAsync(CancellationToken cancellation = default) =>
        Task.Run(() =>
        {
            serial.Open();
            Thread.Sleep(100);
            serial.DiscardInBuffer();       // a start-up identity line, if the Uno was just plugged in
        }, cancellation);

    public Task<string> CommandAsync(string line, CancellationToken cancellation = default) =>
        Task.Run(() =>
        {
            serial.DiscardInBuffer();
            serial.Write(line + "\n");
            try
            {
                return serial.ReadLine().Trim();
            }
            catch (TimeoutException)
            {
                // the Uno may have just been reset: its identity line comes about 1.5 s later
                Thread.Sleep(1600);
                serial.DiscardInBuffer();
                serial.Write(line + "\n");
                return serial.ReadLine().Trim();
            }
        }, cancellation);

    public ValueTask DisposeAsync()
    {
        try
        {
            serial.Close();
        }
        catch (IOException)
        {
        }

        serial.Dispose();
        return ValueTask.CompletedTask;
    }
}

/// <summary>The front end without hardware: answers as SDRFrontend 1.0 does.</summary>
public sealed class SimulatedFrontEndPort : IFrontEndPort
{
    public const string SimulatorName = "Simulated front end";

    public long FrequencyHz { get; private set; } = 14_074_000;

    public int DriveMa { get; private set; } = 8;

    public long CorrectionPpb { get; private set; }

    /// <summary>Commands received, for tests.</summary>
    public List<string> Log { get; } = [];

    public string Name => SimulatorName;

    public Task OpenAsync(CancellationToken cancellation = default) => Task.CompletedTask;

    public Task<string> CommandAsync(string line, CancellationToken cancellation = default)
    {
        string c = line.Trim();
        Log.Add(c);
        char op = char.ToUpperInvariant(c[0]);
        bool query = c.Length > 1 && c[1] == '?';
        string Value() => c[1..].Trim();
        string answer = op switch
        {
            'I' => "I SDRFrontend 1.0 Si5351A ok",
            'S' => FormattableString.Invariant($"S F={FrequencyHz} D={DriveMa} C={CorrectionPpb} LO={FrequencyHz * 4}"),
            'F' when query => FormattableString.Invariant($"F {FrequencyHz}"),
            'F' => long.TryParse(Value(), out long f) && f is >= FrontEndClient.MinHz and <= FrontEndClient.MaxHz
                ? Set(() => FrequencyHz = f, $"F {f}") : "E F 100000 .. 50000000",
            'D' when query => FormattableString.Invariant($"D {DriveMa}"),
            'D' => int.TryParse(Value(), out int d) && d is 0 or 2 or 4 or 6 or 8 ? Set(() => DriveMa = d, $"D {d}") : "E D 0 2 4 6 8",
            'C' when query => FormattableString.Invariant($"C {CorrectionPpb}"),
            'C' => long.TryParse(Value(), out long k) && k is >= -100_000 and <= 100_000 ? Set(() => CorrectionPpb = k, $"C {k}") : "E C -100000 .. 100000",
            _ => "E unknown command (F D C I S)",
        };
        return Task.FromResult(answer);
    }

    public ValueTask DisposeAsync() => ValueTask.CompletedTask;

    private static string Set(Action change, string answer)
    {
        change();
        return answer;
    }
}
