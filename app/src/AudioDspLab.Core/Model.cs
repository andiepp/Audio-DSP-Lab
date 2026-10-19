using AudioDspLab.Protocol;

namespace AudioDspLab;

/// <summary>The applications the firmware knows, by their protocol names.</summary>
public static class Applications
{
    public const string None = "none";
    public const string Generator = "generator";
    public const string Equalizer = "equalizer";
    public const string Radio = "radio";
    public const string Analyzer = "analyzer";
    public const string SigmaStudio = "sigmastudio";
    public const string Synthesizer = "synthesizer";

    /// <summary>In the order the application offers them.</summary>
    public static IReadOnlyList<string> All { get; } =
        [Generator, Equalizer, Radio, Analyzer, Synthesizer, SigmaStudio];

    public static string DisplayName(string? name) =>
        name switch
        {
            Generator => "Generator",
            Equalizer => "Equalizer",
            Radio => "Radio",
            Analyzer => "Analysis",
            SigmaStudio => "SigmaStudio mode",
            Synthesizer => "Synthesizer",
            None or null => "none",
            _ => name
        };
}

/// <summary>What <c>info</c> and <c>! ready</c> report.</summary>
public sealed record BoardInfo(string Name, string Firmware, int Protocol, string Kit = "")
{
    public const string ExpectedName = "Audio-DSP-Lab";
    public const int SupportedProtocol = 1;

    /// <summary>The ESP32 kit for people: "ESP32-PICO-KIT-1 (PICO-V3)", or empty when the board did not say.</summary>
    public string KitDisplayName =>
        Kit switch
        {
            "esp32-pico-kit" => "ESP32-PICO-KIT (PICO-D4)",
            "esp32-pico-kit-1" => "ESP32-PICO-KIT-1 (PICO-V3)",
            _ => Kit
        };

    /// <summary>From <c>info</c> (with the kit) or <c>! ready</c> (without).</summary>
    public static BoardInfo Parse(IReadOnlyList<string> values) =>
        values.Count >= 3
            ? new BoardInfo(values[0], values[1], ProtocolText.ParseInteger(values[2]), values.Count > 3 ? values[3] : string.Empty)
            : throw new FormatException("info needs a name, a firmware version and a protocol version.");
}

/// <summary>The running application and its state (<c>status</c>, <c>! status</c>).</summary>
public sealed record BoardStatus(string Application, string State, string LastError)
{
    public const string Idle = "idle";
    public const string Loading = "loading";
    public const string Running = "running";
    public const string Failed = "failed";
    public const string Service = "service";

    public bool IsRunning => State == Running;

    public static BoardStatus Parse(IReadOnlyList<string> values) =>
        values.Count >= 3
            ? new BoardStatus(values[0], values[1], values[2])
            : throw new FormatException("status needs an application, a state and an error.");
}

public enum ParameterKind
{
    Number,
    Integer,
    Switch,
    Choice,
}

/// <summary>One line of <c>params</c>: a parameter of the running application.</summary>
public sealed record ParameterDescriptor(
    string Name,
    ParameterKind Kind,
    string Value,
    double Minimum,
    double Maximum,
    string Default,
    string Unit,
    IReadOnlyList<string> Choices,
    string? Pot)
{
    public static ParameterDescriptor Parse(IReadOnlyList<string> words)
    {
        if (words.Count < 9)
        {
            throw new FormatException($"A params line has 9 words, not {words.Count}.");
        }

        ParameterKind kind = words[1] switch
        {
            "number" => ParameterKind.Number,
            "switch" => ParameterKind.Switch,
            "choice" => ParameterKind.Choice,
            _ => ParameterKind.Integer
        };
        return new ParameterDescriptor(
            words[0],
            kind,
            words[2],
            ProtocolText.ParseNumberOrNone(words[3]) ?? 0,
            ProtocolText.ParseNumberOrNone(words[4]) ?? 0,
            words[5],
            words[6] == ProtocolText.None ? string.Empty : words[6],
            words[7] == ProtocolText.None ? [] : words[7].Split(','),
            words[8] == ProtocolText.None ? null : words[8]);
    }
}

/// <summary>The radio's connection (<c>radio</c>, <c>! radio</c>).</summary>
public sealed record RadioStatus(string State, int SampleRate, string Title)
{
    public const string Playing = "playing";
}

public sealed record Station(int Slot, string Name, string Url);

public sealed record WifiNetwork(int Slot, string Ssid, bool HasPassword)
{
    public bool IsEmpty => Ssid.Length == 0;
}

/// <summary>Which network the radio is on, and its address; Slot null while not connected.</summary>
public sealed record WifiConnection(int? Slot, string Address);

/// <summary>
/// What the front panel reports of its network (<c>panel</c>): its state
/// (<c>none</c>, <c>connecting</c>, <c>online</c>, <c>failed</c>), its address,
/// and whether a PC works through it.
/// </summary>
public sealed record PanelStatus(string State, string Address, int Clients)
{
    public bool IsOnline => State == "online";
}

/// <summary>The analyzer's sweep (<c>sweep</c>, <c>! sweep</c>).</summary>
public sealed record SweepStatus(string State, int Measured, int Total, int ReferencePoints)
{
    public const string Idle = "idle";
    public const string Running = "running";
    public const string Done = "done";
    public const string Stopped = "stopped";
    public const string Failed = "failed";

    public static SweepStatus Parse(IReadOnlyList<string> values) =>
        new(values[0], ProtocolText.ParseInteger(values[1]), ProtocolText.ParseInteger(values[2]),
            ProtocolText.ParseInteger(values[3]));
}

/// <summary>
/// The THD measurement (<c>thd</c>, <c>! thd</c>): its state, the
/// results so far and the latest result, null before the first.
/// </summary>
public sealed record DistortionStatus(
    string State,
    int Results,
    double? FrequencyHertz,
    DistortionChannel? Left,
    DistortionChannel? Right)
{
    public const string Idle = "idle";
    public const string Running = "running";
    public const string Failed = "failed";

    public bool IsRunning => State == Running;

    public static DistortionStatus Parse(IReadOnlyList<string> values) =>
        new(values[0],
            ProtocolText.ParseInteger(values[1]),
            values.Count > 2 ? ProtocolText.ParseNumberOrNone(values[2]) : null,
            values.Count > 5 ? DistortionChannel.Parse(values, 3) : null,
            values.Count > 8 ? DistortionChannel.Parse(values, 6) : null);
}

/// <summary>One channel's result: the fundamental's peak in dBFS, THD and THD+N in dB.</summary>
public sealed record DistortionChannel(double LevelDecibel, double? ThdDecibel, double? ThdnDecibel)
{
    /// <summary>A ratio in dB as a percentage.</summary>
    public static double Percent(double decibel) => 100.0 * Math.Pow(10.0, decibel / 20.0);

    internal static DistortionChannel? Parse(IReadOnlyList<string> values, int first) =>
        ProtocolText.ParseNumberOrNone(values[first]) is double level
            ? new DistortionChannel(level, ProtocolText.ParseNumberOrNone(values[first + 1]),
                ProtocolText.ParseNumberOrNone(values[first + 2]))
            : null;
}

/// <summary>A harmonic of the latest result (<c>harmonics</c>), in dB against the fundamental.</summary>
public sealed record Harmonic(int Order, double FrequencyHertz, double? LeftDecibel, double? RightDecibel)
{
    public static Harmonic Parse(IReadOnlyList<string> words) =>
        new(ProtocolText.ParseInteger(words[0]),
            ProtocolText.ParseNumberOrNone(words[1]) ?? 0,
            ProtocolText.ParseNumberOrNone(words[2]),
            ProtocolText.ParseNumberOrNone(words[3]));
}

/// <summary>A point of the latest result's spectrum (<c>spectrum</c>): the strongest bin as a sine's peak in dBFS.</summary>
public sealed record SpectrumPoint(int Index, double FrequencyHertz, double LeftDecibel, double RightDecibel)
{
    public static SpectrumPoint Parse(IReadOnlyList<string> words) =>
        new(ProtocolText.ParseInteger(words[0]),
            ProtocolText.ParseNumberOrNone(words[1]) ?? 0,
            ProtocolText.ParseNumberOrNone(words[2]) ?? double.NaN,
            ProtocolText.ParseNumberOrNone(words[3]) ?? double.NaN);
}

/// <summary>One point of a sweep: levels in dBFS, relative levels in dB against the reference.</summary>
public sealed record SweepPoint(
    int Index,
    double FrequencyHertz,
    double LeftDecibel,
    double RightDecibel,
    double? LeftRelativeDecibel = null,
    double? RightRelativeDecibel = null)
{
    public static SweepPoint Parse(IReadOnlyList<string> words) =>
        new(ProtocolText.ParseInteger(words[0]),
            ProtocolText.ParseNumberOrNone(words[1]) ?? 0,
            ProtocolText.ParseNumberOrNone(words[2]) ?? double.NaN,
            ProtocolText.ParseNumberOrNone(words[3]) ?? double.NaN,
            words.Count > 4 ? ProtocolText.ParseNumberOrNone(words[4]) : null,
            words.Count > 5 ? ProtocolText.ParseNumberOrNone(words[5]) : null);
}

/// <summary>The synthesizer's voice (<c>voice</c>, <c>! voice</c>).</summary>
public sealed record SynthVoice(int? Note, bool Gate, int Held, int Channel, int ModStrip, string Stage)
{
    public static SynthVoice Parse(IReadOnlyList<string> values) =>
        new(values[0] == ProtocolText.None ? null : ProtocolText.ParseInteger(values[0]),
            ProtocolText.ParseSwitch(values[1]),
            ProtocolText.ParseInteger(values[2]),
            ProtocolText.ParseInteger(values[3]),
            ProtocolText.ParseInteger(values[4]),
            values[5]);

    private static readonly string[] NoteNames = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

    /// <summary>A MIDI note as people name it: 60 is C4, 69 is A4.</summary>
    public static string NoteName(int note) => $"{NoteNames[note % 12]}{(note / 12) - 1}";
}

public sealed record StudioBackup(bool Exists, int Length, string Crc);

public sealed record AuxInputs(IReadOnlyList<int> Values, bool IsForwarding);
