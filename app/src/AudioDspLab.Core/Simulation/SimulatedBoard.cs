using System.Collections.Concurrent;
using System.Diagnostics;
using System.Globalization;
using AudioDspLab.Protocol;

namespace AudioDspLab.Simulation;

/// <summary>
/// A board in software: answers the protocol as firmware 1.0 does
/// (docs/Protocol.md), with made-up measurements. The application's demo
/// mode and the tests run against it.
/// </summary>
/// <remarks>
/// Requests are handled one at a time on a thread of its own, as the
/// firmware's loop does; events follow between responses.
/// </remarks>
public sealed class SimulatedBoard : ILineTransport
{
    public const string FirmwareVersion = "1.1";

    private static readonly string[] DefaultStationNames =
        ["Deutschlandfunk", "Classic FM UK", "SWR3", "NDR 2", "WDR 2", "Rock Antenne", "Oldies", "SWR1"];

    private static readonly string[] DefaultStationUrls =
    [
        "http://st01.dlf.de/dlf/01/128/mp3/stream.mp3",
        "http://media-ice.musicradio.com/ClassicFMMP3",
        "http://liveradio.swr.de/sw282p3/swr3/play.mp3",
        "http://icecast.ndr.de/ndr/ndr2/niedersachsen/mp3/128/stream.mp3",
        "http://wdr-wdr2-rheinland.icecast.wdr.de/wdr/wdr2/rheinland/mp3/128/stream.mp3",
        "http://s1-webradio.rockantenne.de/70er-rock/stream/mp3",
        "http://listen.181fm.com/181-70s_128k.mp3",
        "http://liveradio.swr.de/sw282p3/swr1bw/play.mp3",
    ];

    private static readonly int[] Arpeggio = [48, 55, 60, 64, 67, 72, 67, 64, 60, 55];

    private readonly bool demo;
    private readonly BlockingCollection<string> requests = new();
    private readonly ConcurrentQueue<Action> actions = new();
    private readonly Stopwatch clock = Stopwatch.StartNew();
    private readonly Dictionary<string, double> values = new(StringComparer.OrdinalIgnoreCase);
    private readonly HashSet<string> pendingValueEvents = new(StringComparer.OrdinalIgnoreCase);
    private readonly int[] pots = [2048, 2048, 2048, 2048];
    private readonly int[] reportedPots = new int[4];
    private readonly string[] stationNames = [.. DefaultStationNames];
    private readonly string[] stationUrls = [.. DefaultStationUrls];
    private readonly string[] wifiSsids = ["", ""];
    private readonly string[] wifiPasswords = ["", ""];
    private readonly List<SweepPoint> points = [];
    private readonly List<SweepPoint> reference = [];
    private readonly List<double> sweepFrequencies = [];
    private Thread? worker;
    private volatile bool stopped;
    private bool led;

    private string application = Applications.SigmaStudio;
    private string state = BoardStatus.Running;
    private string lastError = "none";
    private bool eventsOn;
    private bool potControl;
    private bool startupOn;
    private string startupApplication = Applications.SigmaStudio;
    private string seenStatus = string.Empty;

    private string radioState = "idle";
    private int radioRate = 44100;
    private string title = string.Empty;
    private long radioChangeAt;
    private string seenRadio = string.Empty;
    private string seenTitle = string.Empty;
    private int titleNumber = 1;
    private long titleAt;

    private string sweepState = SweepStatus.Idle;
    private int sweepTotal;
    private long nextPointAt;
    private string seenSweep = string.Empty;
    private string thdState = DistortionStatus.Idle;
    private int thdResults;
    private long nextThdAt;
    private string seenThd = string.Empty;
    private string measuring = PartThd;                // the part measuring, or that measured last
    private string seenSpectrum = string.Empty;

    private int? note;
    private bool gate;
    private int arpeggioIndex;
    private long noteAt;
    private string seenVoice = string.Empty;

    /// <param name="demo">
    /// A demonstration board: a WiFi network set, a stream title that
    /// changes, an arpeggio on the synthesizer's voice.
    /// </param>
    public SimulatedBoard(bool demo = false)
    {
        this.demo = demo;
        if (demo)
        {
            wifiSsids[0] = "Demo network";
            wifiPasswords[0] = "demo-password";
            pots[0] = 1500;
            pots[1] = 2900;
            pots[2] = 700;
            pots[3] = 3300;
        }
    }

    public event Action<string>? LineReceived;

    public event Action<Exception>? Faulted;

    public string Name => "Simulator";

    /// <summary>How long selecting an application takes (the firmware writes the EEPROM).</summary>
    public TimeSpan ApplicationStartTime { get; set; } = TimeSpan.FromMilliseconds(150);

    /// <summary>Requests the board swallows without an answer, for timeout tests.</summary>
    public Func<string, bool>? Swallow { get; set; }

    /// <summary>Every request the board received, in order.</summary>
    public ConcurrentQueue<string> Received { get; } = new();

    public void Open()
    {
        if (worker is not null)
        {
            return;
        }

        worker = new Thread(Run) { IsBackground = true, Name = "Simulated board" };
        worker.Start();
    }

    public void WriteLine(string line)
    {
        ObjectDisposedException.ThrowIf(stopped, this);
        requests.Add(line);
    }

    /// <summary>Turns a pot (0..3 = P1..P4) to a raw position 0..4095.</summary>
    public void SetPot(int index, int raw) => actions.Enqueue(() =>
    {
        pots[index] = Math.Clamp(raw, 0, 4095);
        if (potControl && state == BoardStatus.Running)
        {
            ApplyPot(index);
        }
    });

    /// <summary>A key pressed (a note) or released (null) on the MIDI input.</summary>
    public void PlayNote(int? midiNote) => actions.Enqueue(() =>
    {
        note = midiNote ?? note;
        gate = midiNote is not null;
    });

    /// <summary>The board restarts as after a power cycle.</summary>
    public void Restart() => actions.Enqueue(Reboot);

    /// <summary>The USB cable is pulled.</summary>
    public void Unplug()
    {
        stopped = true;
        Faulted?.Invoke(new IOException("The port vanished."));
    }

    public void Dispose()
    {
        stopped = true;
        worker?.Join(500);
        requests.Dispose();
    }

    private long Now => clock.ElapsedMilliseconds;

    private void Run()
    {
        Thread.Sleep(30);
        Emit("ets Jun  8 2016 00:22:57");             // what the boot ROM leaves
        Emit(string.Empty);
        Emit($"! ready {BoardInfo.ExpectedName} {FirmwareVersion} 1");
        while (!stopped)
        {
            try
            {
                if (requests.TryTake(out string? line, 10))
                {
                    Received.Enqueue(line);
                    if (Swallow?.Invoke(line) != true)
                    {
                        Handle(line);
                    }
                }

                while (actions.TryDequeue(out Action? action))
                {
                    action();
                }

                Tick();
            }
            catch (ObjectDisposedException)
            {
                return;
            }
        }
    }

    private void Emit(string line)
    {
        if (!stopped)
        {
            LineReceived?.Invoke(line);
        }
    }

    private void Ok(string? values = null) => Emit(string.IsNullOrEmpty(values) ? "ok" : $"ok {values}");

    private void Data(string values) => Emit($"* {values}");

    private void Error(string code, string message) => Emit($"err {code} {message}");

    private void BadArgument() => Error("bad-argument", "missing or malformed argument");

    private void WrongApplication() => Error("wrong-application", "not available in the running application");

    // --- requests ------------------------------------------------------------------

    private void Handle(string line)
    {
        if (line.Length > 320)
        {
            Error("line-too-long", "request too long");
            return;
        }

        IReadOnlyList<string>? words = ProtocolText.Tokenize(line);
        if (words is null)
        {
            BadArgument();
            return;
        }

        if (words.Count == 0)
        {
            return;
        }

        string command = words[0].ToLowerInvariant();
        IReadOnlyList<string> args = [.. words.Skip(1)];
        switch (command)
        {
            case "ping": Ok(); break;
            case "echo" when args.Count == 1: Ok(args[0]); break;
            case "info": Ok($"{BoardInfo.ExpectedName} {FirmwareVersion} 1 esp32-pico-kit"); break;
            case "help": Help(); break;
            case "events": Switch(args, ref eventsOn, SyncEvents); break;
            case "status": Ok(StatusText()); break;
            case "apps": Apps(); break;
            case "app": SelectApplication(args); break;
            case "params": Params(); break;
            case "get": Get(args); break;
            case "set": Set(args); break;
            case "pots": Ok(string.Join(' ', pots)); break;
            case "potctl": Switch(args, ref potControl, TakePots); break;
            case "startup": Startup(args); break;
            case "service": Service(args); break;
            case "led": Switch(args, ref led, null); break;
            case "stations": Stations(); break;
            case "station": EditStation(args); break;
            case "radio": Ok($"{(application == Applications.Radio ? radioState : "idle")} {radioRate} {ProtocolText.Quote(title)}"); break;
            case "wifi": Wifi(args); break;
            case "panel": Ok("none - 0"); break;      // no front panel on a simulated board
            case "sweep": Sweep(args); break;
            case "points": Points(args); break;
            case "reference": Reference(args); break;
            case "level": Level(); break;
            case "thd": Thd(args); break;
            case "harmonics": Harmonics(); break;
            case "spectrum": SpectrumPart(args); break;
            case "voice": if (Running(Applications.Synthesizer)) Ok(VoiceText()); break;
            case "midi": if (Running(Applications.Synthesizer)) Ok("0 0 0 0"); break;
            case "load": if (Running(Applications.Synthesizer)) Ok("12.5 18.3"); break;
            case "aux": Ok($"{pots[0] / 16} {pots[1] / 16} {pots[2] / 16} {pots[3] / 16} {ProtocolText.FormatSwitch(application == Applications.SigmaStudio)}"); break;
            case "backup": Ok("yes 505 8B3D2CD2"); break;
            case "diag": Ok("0 0x00000000"); break;
            case "reset":
                Ok();
                actions.Enqueue(Reboot);
                break;
            default: Error("unknown-command", "no such command (try help)"); break;
        }
    }

    private void Help()
    {
        foreach (string usage in new[]
                 {
                     "help", "ping", "echo <word>", "info", "events [on|off]", "status", "apps", "app [<application>]",
                     "params", "get [<parameter>]", "set <parameter> <value>", "pots", "potctl [on|off]",
                     "startup [on|off]", "service [on|off]", "led [on|off]", "stations", "station ...", "radio",
                     "wifi ...", "sweep [start|stop]", "points [<from>]", "reference [keep]", "level",
                     "thd [start|stop]", "harmonics", "spectrum [start|stop|points [<n>]|peaks]", "voice",
                     "midi", "load", "aux", "backup", "diag ...", "reset",
                 })
        {
            Data(usage);
        }

        Ok();
    }

    private void Switch(IReadOnlyList<string> args, ref bool target, Action? onEnable)
    {
        if (args.Count > 1)
        {
            BadArgument();
            return;
        }

        if (args.Count == 1)
        {
            bool on;
            try
            {
                on = ProtocolText.ParseSwitch(args[0]);
            }
            catch (FormatException)
            {
                BadArgument();
                return;
            }

            if (on && !target)
            {
                onEnable?.Invoke();
            }

            target = on;
        }

        Ok(ProtocolText.FormatSwitch(target));
    }

    private string StatusText() => $"{application} {state} {lastError}";

    private void Apps()
    {
        Data("generator 1 \"Signal generator\"");
        Data("equalizer 2 \"Equalizer\"");
        Data("radio 3 \"Internet radio\"");
        Data("analyzer 4 \"Analysis\"");
        Data("sigmastudio 5 \"SigmaStudio mode\"");
        Data("synthesizer 6 \"Synthesizer\"");
        Ok();
    }

    private void SelectApplication(IReadOnlyList<string> args)
    {
        if (args.Count == 0)
        {
            Ok(StatusText());
            return;
        }

        string name = args[0].ToLowerInvariant();
        if (!Applications.All.Contains(name))
        {
            Error("unknown-application", "no such application");
            return;
        }

        if (state == BoardStatus.Service)
        {
            Error("service-mode", "the DSP belongs to SigmaStudio now");
            return;
        }

        Start(name);
        if (startupOn)
        {
            startupApplication = name;
        }

        Ok($"{application} {state}");
    }

    private void Start(string name)
    {
        state = BoardStatus.Loading;
        Thread.Sleep(ApplicationStartTime);
        application = name;
        values.Clear();
        foreach (SimulatedParameter parameter in SimulatedParameters.For(name))
        {
            values[parameter.Name] = parameter.Default;
        }

        pendingValueEvents.Clear();
        points.Clear();
        sweepState = SweepStatus.Idle;
        sweepTotal = 0;
        thdState = DistortionStatus.Idle;
        thdResults = 0;
        measuring = PartThd;
        radioState = name == Applications.Radio ? "connecting-wifi" : "idle";
        radioChangeAt = Now + 400;
        title = string.Empty;
        note = null;
        gate = false;
        state = BoardStatus.Running;
        lastError = "none";
    }

    private SimulatedParameter? Find(string name) =>
        SimulatedParameters.For(application).FirstOrDefault(parameter =>
            string.Equals(parameter.Name, name, StringComparison.OrdinalIgnoreCase));

    private static string Format(SimulatedParameter parameter, double value) =>
        parameter.Kind switch
        {
            ParameterKind.Number => ProtocolText.FormatNumber(value),
            ParameterKind.Switch => ProtocolText.FormatSwitch(value != 0),
            ParameterKind.Choice => parameter.Choices![(int)value],
            _ => ((int)value).ToString(CultureInfo.InvariantCulture)
        };

    private static string FormatRange(SimulatedParameter parameter, double value) =>
        parameter.Kind == ParameterKind.Number
            ? ProtocolText.FormatNumber(value)
            : ((int)value).ToString(CultureInfo.InvariantCulture);

    private void Params()
    {
        foreach (SimulatedParameter parameter in SimulatedParameters.For(application))
        {
            string kind = parameter.Kind.ToString().ToLowerInvariant();
            string unit = parameter.Unit.Length == 0 ? "-" : parameter.Unit;
            string choices = parameter.Choices is null ? "-" : string.Join(',', parameter.Choices);
            Data($"{parameter.Name} {kind} {Format(parameter, values[parameter.Name])} {FormatRange(parameter, parameter.Minimum)} " +
                 $"{FormatRange(parameter, parameter.Maximum)} {Format(parameter, parameter.Default)} {unit} {choices} {parameter.Pot ?? "-"}");
        }

        Ok();
    }

    private void Get(IReadOnlyList<string> args)
    {
        if (args.Count == 0)
        {
            foreach (SimulatedParameter parameter in SimulatedParameters.For(application))
            {
                Data($"{parameter.Name} {Format(parameter, values[parameter.Name])}");
            }

            Ok();
            return;
        }

        if (args.Count != 1)
        {
            BadArgument();
            return;
        }

        if (Find(args[0]) is not { } found)
        {
            Error("unknown-parameter", "the running application has no such parameter");
            return;
        }

        Ok(Format(found, values[found.Name]));
    }

    private static bool TryParse(SimulatedParameter parameter, string text, out double value)
    {
        value = 0;
        switch (parameter.Kind)
        {
            case ParameterKind.Number:
                if (!ProtocolText.TryParseNumber(text, out value))
                {
                    return false;
                }

                value = Math.Round(value, 2, MidpointRounding.AwayFromZero);
                return true;
            case ParameterKind.Integer:
                if (!int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out int integer))
                {
                    return false;
                }

                value = integer;
                return true;
            case ParameterKind.Switch:
                try
                {
                    value = ProtocolText.ParseSwitch(text) ? 1 : 0;
                    return true;
                }
                catch (FormatException)
                {
                    return false;
                }

            default:
                int index = Array.FindIndex(parameter.Choices!, name => string.Equals(name, text, StringComparison.OrdinalIgnoreCase));
                if (index < 0 && (!int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out index)
                                  || index < 0 || index >= parameter.Choices!.Length))
                {
                    return false;
                }

                value = index;
                return true;
        }
    }

    private void Set(IReadOnlyList<string> args)
    {
        if (args.Count != 2)
        {
            BadArgument();
            return;
        }

        if (state == BoardStatus.Service)
        {
            Error("service-mode", "the DSP belongs to SigmaStudio now");
            return;
        }

        if (Find(args[0]) is not { } parameter)
        {
            Error("unknown-parameter", "the running application has no such parameter");
            return;
        }

        if (!TryParse(parameter, args[1], out double value))
        {
            BadArgument();
            return;
        }

        if (value < parameter.Minimum - 1e-9 || value > parameter.Maximum + 1e-9)
        {
            Error("out-of-range",
                $"{args[0]} {FormatRange(parameter, parameter.Minimum)}..{FormatRange(parameter, parameter.Maximum)} {parameter.Unit}".TrimEnd());
            return;
        }

        values[parameter.Name] = value;
        if (parameter.Name == "station")
        {
            radioState = "connecting-stream";
            radioChangeAt = Now + 300;
            title = string.Empty;
        }

        if (parameter.Name == "p2-range" && potControl)
        {
            ApplyPot(1);                                  // the frequency follows P2 into the decade
        }

        Ok(Format(parameter, value));
    }

    // pot control switched on: the values go to where the pots stand
    private void TakePots()
    {
        if (state != BoardStatus.Running)
        {
            return;
        }

        for (int index = 0; index < pots.Length; index++)
        {
            ApplyPot(index);
        }
    }

    private void ApplyPot(int index)
    {
        string? name = SimulatedParameters.For(application).FirstOrDefault(parameter => parameter.Pot == $"P{index + 1}")?.Name;
        if (name is null)
        {
            return;
        }

        double raw = pots[index];
        double share = raw / 4095.0;
        double value = (application, name) switch
        {
            (Applications.Generator, "frequency") => Math.Round(20 * Math.Pow(10, values["p2-range"]) * Math.Pow(10, share), 2),
            (Applications.Generator, "waveform") => Math.Min(5, (int)(raw * 6 / 4096)),
            (Applications.Generator, "width") => 5 + Math.Round(90 * share),
            (Applications.Synthesizer, "cutoff") => Math.Round(20 * Math.Pow(10, 3 * share)),
            (Applications.Synthesizer, "emphasis") => Math.Round(100 * share),
            (Applications.Synthesizer, "osc1-width") => 5 + Math.Round(90 * share),
            (_, "bass-gain" or "middle-gain" or "treble-gain") => Math.Round(-12 + (24 * share), 2),
            _ => Math.Round(-60 + (60 * share), 2),
        };
        if (values[name] != value)
        {
            values[name] = value;
            pendingValueEvents.Add(name);
        }
    }

    private void Startup(IReadOnlyList<string> args)
    {
        if (args.Count == 1)
        {
            bool on = ProtocolText.ParseSwitch(args[0]);
            startupApplication = on && state is BoardStatus.Running or BoardStatus.Service ? application : Applications.SigmaStudio;
            startupOn = on;
        }

        Ok($"{ProtocolText.FormatSwitch(startupOn)} {startupApplication}");
    }

    private void Service(IReadOnlyList<string> args)
    {
        if (args.Count == 1)
        {
            bool on = ProtocolText.ParseSwitch(args[0]);
            if (on)
            {
                state = BoardStatus.Service;
            }
            else if (state == BoardStatus.Service)
            {
                Dictionary<string, double> kept = new(values, StringComparer.OrdinalIgnoreCase);
                Start(application);
                foreach ((string key, double value) in kept)
                {
                    values[key] = key == "output" ? 0 : value;
                }
            }
        }

        Ok(ProtocolText.FormatSwitch(state == BoardStatus.Service));
    }

    private void Stations()
    {
        for (int slot = 0; slot < stationNames.Length; slot++)
        {
            Data($"{slot} {ProtocolText.Quote(stationNames[slot])} {ProtocolText.Quote(stationUrls[slot])}");
        }

        Ok();
    }

    private void EditStation(IReadOnlyList<string> args)
    {
        if (args.Count == 2 && args[0] == "all" && args[1] == "default")
        {
            Array.Copy(DefaultStationNames, stationNames, stationNames.Length);
            Array.Copy(DefaultStationUrls, stationUrls, stationUrls.Length);
            Ok();
            return;
        }

        if (args.Count < 2 || !int.TryParse(args[0], out int slot) || slot is < 0 or > 7)
        {
            BadArgument();
            return;
        }

        switch (args[1])
        {
            case "default" when args.Count == 2:
                stationNames[slot] = DefaultStationNames[slot];
                stationUrls[slot] = DefaultStationUrls[slot];
                Ok();
                return;
            case "name" when args.Count == 3:
                if (args[2].Length == 0 || System.Text.Encoding.UTF8.GetByteCount(args[2]) > 63)
                {
                    Error("out-of-range", "value out of range");
                    return;
                }

                stationNames[slot] = args[2];
                Ok();
                return;
            case "url" when args.Count == 3:
                if (!args[2].StartsWith("http://", StringComparison.Ordinal) || args[2].Length > 200 || args[2].Contains(' '))
                {
                    Error("out-of-range", "value out of range");
                    return;
                }

                stationUrls[slot] = args[2];
                Ok();
                return;
            default:
                BadArgument();
                return;
        }
    }

    private void Wifi(IReadOnlyList<string> args)
    {
        if (args.Count == 0)
        {
            for (int index = 0; index < 2; index++)
            {
                Data($"{index} {ProtocolText.Quote(wifiSsids[index])} {(wifiPasswords[index].Length > 0 ? "yes" : "no")}");
            }

            int connected = application == Applications.Radio && radioState is "playing" or "connecting-stream"
                ? Array.FindIndex(wifiSsids, ssid => ssid.Length > 0)
                : -1;
            Ok(connected < 0 ? "- -" : $"{connected} 192.168.1.77");
            return;
        }

        if (!int.TryParse(args[0], out int slot) || slot is < 0 or > 1 || args.Count < 2)
        {
            BadArgument();
            return;
        }

        if (args.Count == 2 && args[1] == "clear")
        {
            wifiSsids[slot] = string.Empty;
            wifiPasswords[slot] = string.Empty;
            Ok();
            return;
        }

        string password = args.Count > 2 ? args[2] : string.Empty;
        if (args[1].Length is 0 or > 32 || (password.Length != 0 && password.Length is < 8 or > 63) || args.Count > 3)
        {
            Error("out-of-range", "value out of range");
            return;
        }

        wifiSsids[slot] = args[1];
        wifiPasswords[slot] = password;
        if (application == Applications.Radio)
        {
            radioState = "connecting-wifi";
            radioChangeAt = Now + 400;
        }

        Ok();
    }

    // --- analyzer -----------------------------------------------------------------------

    private bool Running(string name)
    {
        if (application == name && state == BoardStatus.Running)
        {
            return true;
        }

        WrongApplication();
        return false;
    }

    private string SweepText() => $"{sweepState} {points.Count} {sweepTotal} {reference.Count}";

    private void Sweep(IReadOnlyList<string> args)
    {
        if (args.Count == 0)
        {
            Ok(SweepText());
            return;
        }

        if (!Running(Applications.Analyzer))
        {
            return;
        }

        switch (args[0])
        {
            case "start":
                double start = values["start"];
                double stop = values["stop"];
                if (stop <= start)
                {
                    Error("out-of-range", "value out of range");
                    return;
                }

                int perDecade = (int)values["points-per-decade"];
                sweepTotal = Math.Clamp((int)Math.Round(Math.Log10(stop / start) * perDecade, MidpointRounding.AwayFromZero) + 1, 2, 128);
                sweepFrequencies.Clear();
                for (int index = 0; index < sweepTotal; index++)
                {
                    sweepFrequencies.Add(Math.Round(start * Math.Pow(stop / start, index / (double)(sweepTotal - 1)), 2));
                }

                points.Clear();
                values["output"] = 1;
                thdState = DistortionStatus.Idle;
                sweepState = SweepStatus.Running;
                nextPointAt = Now + 150;
                Ok(SweepText());
                return;
            case "stop":
                if (sweepState == SweepStatus.Running)
                {
                    sweepState = SweepStatus.Stopped;
                }

                Ok(SweepText());
                return;
            default:
                BadArgument();
                return;
        }
    }

    // a made-up device under test: a first-order low-pass at 5 kHz behind the output level
    private double Response(double hertz, double offset) =>
        Math.Round(values.GetValueOrDefault("level", -20) - (10 * Math.Log10(1 + Math.Pow(hertz / 5000, 2))) + offset, 2);

    private (double? Left, double? Right) Relative(double hertz, double left, double right)
    {
        if (reference.Count < 2 || hertz < reference[0].FrequencyHertz - 0.01 || hertz > reference[^1].FrequencyHertz + 0.01)
        {
            return (null, null);
        }

        int upper = reference.FindIndex(point => point.FrequencyHertz >= hertz - 0.001);
        upper = Math.Max(1, upper);
        SweepPoint a = reference[upper - 1];
        SweepPoint b = reference[upper];
        double t = Math.Log(hertz / a.FrequencyHertz) / Math.Log(b.FrequencyHertz / a.FrequencyHertz);
        t = double.IsFinite(t) ? Math.Clamp(t, 0, 1) : 0;
        return (Math.Round(left - (a.LeftDecibel + (t * (b.LeftDecibel - a.LeftDecibel))), 2),
                Math.Round(right - (a.RightDecibel + (t * (b.RightDecibel - a.RightDecibel))), 2));
    }

    private static string FormatLevel(double? level) => level is double value ? ProtocolText.FormatNumber(value) : "-";

    private static string PointText(SweepPoint point) =>
        $"{point.Index} {ProtocolText.FormatNumber(point.FrequencyHertz)} {FormatLevel(point.LeftDecibel)} {FormatLevel(point.RightDecibel)} " +
        $"{FormatLevel(point.LeftRelativeDecibel)} {FormatLevel(point.RightRelativeDecibel)}";

    private void Points(IReadOnlyList<string> args)
    {
        int from = args.Count == 1 && int.TryParse(args[0], out int start) ? start : 0;
        foreach (SweepPoint point in points.Skip(from))
        {
            Data(PointText(point));
        }

        Ok();
    }

    private void Reference(IReadOnlyList<string> args)
    {
        if (args.Count == 1 && args[0] == "keep")
        {
            if (!Running(Applications.Analyzer))
            {
                return;
            }

            if (sweepState != SweepStatus.Done || points.Count < 2)
            {
                Error("no-sweep", "no complete sweep to keep");
                return;
            }

            reference.Clear();
            reference.AddRange(points.Select(point => point with { LeftRelativeDecibel = null, RightRelativeDecibel = null }));
            Ok();
            return;
        }

        foreach (SweepPoint point in reference)
        {
            Data($"{point.Index} {ProtocolText.FormatNumber(point.FrequencyHertz)} {FormatLevel(point.LeftDecibel)} {FormatLevel(point.RightDecibel)}");
        }

        Ok();
    }

    private void Level()
    {
        if (!Running(Applications.Analyzer))
        {
            return;
        }

        double tone = values["tone"];
        bool output = values["output"] != 0;
        Ok(output ? $"{FormatLevel(Response(tone, 0))} {FormatLevel(Response(tone, -0.05))}" : "-110.5 -110.5");
    }

    // --- analysis: THD and Spectrum ------------------------------------------------------

    // the firmware's FFT: 8192 frames at 48 kHz; a result about every 1.6 s
    private const double ThdBinHertz = 48000.0 / 8192;
    private const int ThdPeriodMilliseconds = 1600;
    private const string PartThd = "thd";
    private const string PartSpectrum = "spectrum";

    // a made-up device: these harmonics (H2 .. H10, dBc), mains hum, and a
    // noise floor of -97.5 dBFS RMS from 20 Hz to 20 kHz, -120 dBFS a bin
    private static readonly double[] SimulatedHarmonics = [-82, -78, -95, -91, -99, -97, -103, -101, -106];
    private static readonly (double Hertz, double Decibel)[] SimulatedHum = [(50.0, -104.8), (150.1, -113.6), (4232.5, -116.5)];
    private const double NoiseDecibel = -97.5;
    private const double BinFloorDecibel = -120;

    private double ThdHertz => Math.Round(Math.Max(1, Math.Round(values.GetValueOrDefault("tone", 1000) / ThdBinHertz)) * ThdBinHertz, 2);

    // the tone as the part plays it: THD on an FFT bin, Spectrum as set
    private double ToneHertz => measuring == PartThd ? ThdHertz : values.GetValueOrDefault("tone", 1000);

    private double ToneLevel(double offset) => values["output"] != 0 ? Response(ToneHertz, offset) : -140;

    // the harmonics below 20 kHz of the latest result, a little different each time
    private List<(int Order, double Hertz, double Decibel)> ThdHarmonics(double offset)
    {
        List<(int, double, double)> harmonics = [];
        double hertz = ToneHertz;
        for (int order = 2; order <= 10 && order * hertz <= 20000; order++)
        {
            double wobble = 0.4 * Math.Sin((thdResults * 1.7) + order + offset);
            harmonics.Add((order, order * hertz, Math.Round(SimulatedHarmonics[order - 2] + wobble + (offset * 10), 2)));
        }

        return harmonics;
    }

    // every line in the spectrum: the tone and its harmonics while the output plays, and the hum
    private List<(double Hertz, double Decibel)> Lines(double offset)
    {
        List<(double, double)> lines = [.. SimulatedHum.Select(hum => (hum.Hertz, hum.Decibel + (offset * 4)))];
        double level = ToneLevel(offset);
        if (values["output"] != 0)
        {
            lines.Add((ToneHertz, level));
            lines.AddRange(ThdHarmonics(offset).Select(harmonic => (harmonic.Hertz, level + harmonic.Decibel)));
        }

        return lines;
    }

    private string PartState(string part) => measuring == part ? thdState : DistortionStatus.Idle;

    private int PartResults(string part) => measuring == part ? thdResults : 0;

    private string ThdChannelText(double offset)
    {
        double level = ToneLevel(offset);
        List<(int Order, double Hertz, double Decibel)> harmonics = ThdHarmonics(offset);
        double sum = harmonics.Sum(harmonic => Math.Pow(10, harmonic.Decibel / 10));
        double noise = Math.Pow(10, (NoiseDecibel - (level - 3.01)) / 10);
        string thd = harmonics.Count > 0 ? ProtocolText.FormatNumber(Math.Round(10 * Math.Log10(sum), 2)) : "-";
        return $"{ProtocolText.FormatNumber(level)} {thd} {ProtocolText.FormatNumber(Math.Round(10 * Math.Log10(sum + noise), 2))}";
    }

    private string ThdText() =>
        PartResults(PartThd) == 0
            ? $"{PartState(PartThd)} 0 - - - - - - -"
            : $"{PartState(PartThd)} {thdResults} {ProtocolText.FormatNumber(ThdHertz)} {ThdChannelText(0)} {ThdChannelText(-0.05)}";

    // everything from 20 Hz to 20 kHz: the lines' mean squares and the noise
    private double Rms(double offset) =>
        Math.Round(10 * Math.Log10(Lines(offset).Where(line => line.Hertz <= 20000).Sum(line => Math.Pow(10, line.Decibel / 10) / 2)
                                   + Math.Pow(10, NoiseDecibel / 10)), 2);

    private string SpectrumText() =>
        PartResults(PartSpectrum) == 0
            ? $"{PartState(PartSpectrum)} 0 - -"
            : $"{PartState(PartSpectrum)} {thdResults} {ProtocolText.FormatNumber(Rms(0))} {ProtocolText.FormatNumber(Rms(-0.05))}";

    private void StartPart(string part)
    {
        if (sweepState == SweepStatus.Running)
        {
            sweepState = SweepStatus.Stopped;
        }

        measuring = part;
        thdState = DistortionStatus.Running;
        thdResults = 0;
        nextThdAt = Now + ThdPeriodMilliseconds;
    }

    private void Thd(IReadOnlyList<string> args)
    {
        if (args.Count == 0)
        {
            Ok(ThdText());
            return;
        }

        if (!Running(Applications.Analyzer))
        {
            return;
        }

        switch (args[0])
        {
            case "start":
                values["output"] = 1;
                StartPart(PartThd);
                Ok(ThdText());
                return;
            case "stop":
                if (measuring == PartThd)
                {
                    thdState = DistortionStatus.Idle;
                }

                Ok(ThdText());
                return;
            default:
                BadArgument();
                return;
        }
    }

    private void SpectrumPart(IReadOnlyList<string> args)
    {
        if (args.Count >= 1 && args[0] == "points")
        {
            SpectrumPoints(args);
            return;
        }

        if (args.Count == 1 && args[0] == "peaks")
        {
            Peaks();
            return;
        }

        if (args.Count == 0)
        {
            Ok(SpectrumText());
            return;
        }

        if (!Running(Applications.Analyzer))
        {
            return;
        }

        switch (args[0])
        {
            case "start":
                StartPart(PartSpectrum);
                Ok(SpectrumText());
                return;
            case "stop":
                if (measuring == PartSpectrum)
                {
                    thdState = DistortionStatus.Idle;
                }

                Ok(SpectrumText());
                return;
            default:
                BadArgument();
                return;
        }
    }

    private void Harmonics()
    {
        if (PartResults(PartThd) > 0)
        {
            List<(int Order, double Hertz, double Decibel)> left = ThdHarmonics(0);
            List<(int Order, double Hertz, double Decibel)> right = ThdHarmonics(-0.05);
            for (int index = 0; index < left.Count; index++)
            {
                Data($"{left[index].Order} {ProtocolText.FormatNumber(Math.Round(left[index].Hertz, 2))} " +
                     $"{ProtocolText.FormatNumber(left[index].Decibel)} {ProtocolText.FormatNumber(right[index].Decibel)}");
            }
        }

        Ok();
    }

    // the strongest lines per channel, strongest first; the floor's bumps make up five
    private void Peaks()
    {
        if (thdResults > 0)
        {
            (double Hertz, double Decibel)[] Ranked(double offset) =>
                [.. Lines(offset).Where(line => line.Hertz <= 20000)
                    .Concat([(Hertz: 171.9, Decibel: -116.9 + offset), (Hertz: 343.3, Decibel: -117.2 + offset), (Hertz: 887.1, Decibel: -117.6 + offset)])
                    .OrderByDescending(line => line.Decibel).Take(5)];
            (double Hertz, double Decibel)[] left = Ranked(0);
            (double Hertz, double Decibel)[] right = Ranked(-0.05);
            for (int rank = 0; rank < 5; rank++)
            {
                Data($"{rank + 1} {ProtocolText.FormatNumber(Math.Round(left[rank].Hertz, 1))} {ProtocolText.FormatNumber(Math.Round(left[rank].Decibel, 2))} " +
                     $"{ProtocolText.FormatNumber(Math.Round(right[rank].Hertz, 1))} {ProtocolText.FormatNumber(Math.Round(right[rank].Decibel, 2))}");
            }
        }

        Ok();
    }

    private void SpectrumPoints(IReadOnlyList<string> args)
    {
        int count = 256;
        if (args.Count == 2 && (!int.TryParse(args[1], NumberStyles.Integer, CultureInfo.InvariantCulture, out count)
                                || count < 16 || count > 256 || (count & (count - 1)) != 0))
        {
            BadArgument();
            return;
        }

        if (thdResults > 0)
        {
            double SpectrumHertz(int point) => 20 * Math.Pow(1200, point / 255.0);
            double Peak(double offset, double low, double high)
            {
                double strongest = BinFloorDecibel + (1.5 * Math.Sin(low * 7.3));
                foreach ((double hertz, double decibel) in Lines(offset))
                {
                    if (hertz >= low && hertz < high)
                    {
                        strongest = Math.Max(strongest, decibel);
                    }
                }

                return Math.Round(strongest, 2);
            }

            int group = 256 / count;
            double edge = Math.Pow(1200, 0.5 / 255);
            for (int index = 0; index < count; index++)
            {
                double low = SpectrumHertz(index * group) / edge;
                double high = SpectrumHertz(((index + 1) * group) - 1) * edge;
                double hertz = Math.Sqrt(SpectrumHertz(index * group) * SpectrumHertz(((index + 1) * group) - 1));
                Data($"{index} {ProtocolText.FormatNumber(Math.Round(hertz, 1))} " +
                     $"{ProtocolText.FormatNumber(Peak(0, low, high))} {ProtocolText.FormatNumber(Peak(-0.05, low, high))}");
            }
        }

        Ok();
    }

    // --- synthesizer ------------------------------------------------------------------

    private string VoiceText() =>
        $"{(note is int playing ? playing.ToString(CultureInfo.InvariantCulture) : "-")} {ProtocolText.FormatSwitch(gate)} " +
        $"{(gate ? 1 : 0)} {(int)values.GetValueOrDefault("channel", 15)} {(gate ? 64 : 0)} {(gate ? "sustain" : note is null ? "idle" : "release")}";

    // --- time ---------------------------------------------------------------------------

    private void Tick()
    {
        long now = Now;
        if (application == Applications.Radio && state == BoardStatus.Running && now >= radioChangeAt)
        {
            AdvanceRadio(now);
        }

        if (sweepState == SweepStatus.Running && now >= nextPointAt)
        {
            int index = points.Count;
            double hertz = sweepFrequencies[index];
            double left = Response(hertz, 0);
            double right = Response(hertz, -0.05);
            (double? relativeLeft, double? relativeRight) = Relative(hertz, left, right);
            points.Add(new SweepPoint(index, hertz, left, right, relativeLeft, relativeRight));
            if (eventsOn)
            {
                Emit($"! point {PointText(points[^1])}");
            }

            if (points.Count == sweepTotal)
            {
                sweepState = SweepStatus.Done;
            }

            nextPointAt = now + 40;
        }

        if (thdState == DistortionStatus.Running && now >= nextThdAt)
        {
            thdResults++;
            nextThdAt = now + ThdPeriodMilliseconds;
        }

        if (demo && application == Applications.Synthesizer && state == BoardStatus.Running && now >= noteAt)
        {
            noteAt = now + 300;
            if (gate)
            {
                gate = false;
            }
            else
            {
                note = Arpeggio[arpeggioIndex++ % Arpeggio.Length];
                gate = true;
            }
        }

        if (eventsOn)
        {
            Events();
        }
    }

    private void AdvanceRadio(long now)
    {
        bool hasNetwork = wifiSsids.Any(ssid => ssid.Length > 0);
        switch (radioState)
        {
            case "connecting-wifi":
                radioState = hasNetwork ? "connecting-stream" : "no-network";
                radioChangeAt = hasNetwork ? now + 400 : long.MaxValue;
                break;
            case "connecting-stream":
                radioState = "playing";
                radioRate = (int)values.GetValueOrDefault("station", 0) % 2 == 0 ? 48000 : 44100;
                title = $"{stationNames[(int)values.GetValueOrDefault("station", 0)]} - Simulated track {titleNumber}";
                titleAt = now + 15000;
                radioChangeAt = now + 500;
                break;
            case "playing" when demo && now >= titleAt:
                title = $"{stationNames[(int)values.GetValueOrDefault("station", 0)]} - Simulated track {++titleNumber}";
                titleAt = now + 15000;
                radioChangeAt = now + 500;
                break;
            default:
                radioChangeAt = now + 500;
                break;
        }
    }

    private void SyncEvents()
    {
        seenStatus = StatusText();
        pendingValueEvents.Clear();
        Array.Copy(pots, reportedPots, pots.Length);
        seenRadio = $"{radioState} {radioRate}";
        seenTitle = title;
        seenSweep = SweepText();
        seenVoice = VoiceText();
    }

    private void Events()
    {
        string status = StatusText();
        if (status != seenStatus)
        {
            if (!seenStatus.StartsWith(application + " ", StringComparison.Ordinal))
            {
                seenSweep = SweepText();
                seenTitle = string.Empty;
                seenRadio = string.Empty;
            }

            seenStatus = status;
            Emit($"! status {status}");
        }

        if (state == BoardStatus.Service)
        {
            return;
        }

        foreach (string name in pendingValueEvents)
        {
            if (Find(name) is { } parameter)
            {
                Emit($"! value {parameter.Name} {Format(parameter, values[parameter.Name])}");
            }
        }

        pendingValueEvents.Clear();
        if (pots.Where((raw, index) => Math.Abs(raw - reportedPots[index]) >= 80).Any())
        {
            Array.Copy(pots, reportedPots, pots.Length);
            Emit($"! pots {string.Join(' ', pots)}");
        }

        if (application == Applications.Radio && state == BoardStatus.Running)
        {
            string radio = $"{radioState} {radioRate}";
            if (radio != seenRadio)
            {
                seenRadio = radio;
                Emit($"! radio {radio}");
            }

            if (title != seenTitle)
            {
                seenTitle = title;
                Emit($"! title {ProtocolText.Quote(title)}");
            }
        }

        if (application == Applications.Analyzer && state == BoardStatus.Running)
        {
            string sweep = SweepText();
            if (sweep != seenSweep)
            {
                seenSweep = sweep;
                Emit($"! sweep {sweep}");
            }

            string thd = ThdText();
            if (thd != seenThd)
            {
                seenThd = thd;
                Emit($"! thd {thd}");
            }

            string spectrum = SpectrumText();
            if (spectrum != seenSpectrum)
            {
                seenSpectrum = spectrum;
                Emit($"! spectrum {spectrum}");
            }
        }

        if (application == Applications.Synthesizer && state == BoardStatus.Running)
        {
            string voice = VoiceText();
            if (voice != seenVoice)
            {
                seenVoice = voice;
                Emit($"! voice {voice}");
            }
        }
    }

    private void Reboot()
    {
        Emit("rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)");
        eventsOn = false;
        potControl = false;
        state = BoardStatus.Running;
        application = Applications.SigmaStudio;
        if (startupOn && startupApplication != Applications.SigmaStudio)
        {
            Start(startupApplication);
            if (startupApplication is Applications.Radio or Applications.Synthesizer)
            {
                values["output"] = 1;
            }
        }

        Emit(string.Empty);
        Emit($"! ready {BoardInfo.ExpectedName} {FirmwareVersion} 1");
    }
}
