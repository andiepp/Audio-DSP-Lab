namespace AudioDspLab.Simulation;

/// <summary>A parameter as the firmware defines it (Applications.cpp, ParamInfo.cpp).</summary>
internal sealed record SimulatedParameter(
    string Name,
    ParameterKind Kind,
    double Minimum,
    double Maximum,
    double Default,
    string Unit = "",
    string[]? Choices = null,
    string? Pot = null);

/// <summary>The firmware 1.0 parameter tables, in the firmware's order.</summary>
internal static class SimulatedParameters
{
    private static readonly string[] GeneratorWaveforms = ["sine", "rectangle", "triangle", "saw", "inverse-saw", "noise"];
    private static readonly string[] SynthWaveforms = ["sine", "saw", "rectangle", "triangle"];
    private static readonly string[] LfoWaveforms = ["sine", "triangle", "square"];

    public static IReadOnlyList<SimulatedParameter> For(string application) =>
        application switch
        {
            Applications.Generator => Generator,
            Applications.Equalizer => Equalizer,
            Applications.Radio => [.. Equalizer, new("station", ParameterKind.Integer, 0, 7, 0)],
            Applications.Analyzer => Analyzer,
            Applications.Synthesizer => Synthesizer,
            Applications.Receiver => Receiver,
            _ => []
        };

    private static readonly SimulatedParameter[] Generator =
    [
        new("waveform", ParameterKind.Choice, 0, 5, 0, Choices: GeneratorWaveforms, Pot: "P1"),
        new("frequency", ParameterKind.Number, 20, 20000, 1000, "Hz", Pot: "P2"),
        new("amplitude", ParameterKind.Number, -60, 0, -20, "dBFS", Pot: "P4"),
        new("output", ParameterKind.Switch, 0, 1, 0),
        new("p2-range", ParameterKind.Choice, 0, 2, 1, Choices: ["20-200", "200-2k", "2k-20k"]),
        new("width", ParameterKind.Integer, 5, 95, 50, "%", Pot: "P3"),
    ];

    private static readonly SimulatedParameter[] Equalizer =
    [
        new("bass-gain", ParameterKind.Number, -12, 12, 0, "dB", Pot: "P1"),
        new("bass-frequency", ParameterKind.Number, 50, 400, 200, "Hz"),
        new("middle-gain", ParameterKind.Number, -12, 12, 0, "dB", Pot: "P2"),
        new("middle-frequency", ParameterKind.Number, 300, 5000, 1000, "Hz"),
        new("treble-gain", ParameterKind.Number, -12, 12, 0, "dB", Pot: "P3"),
        new("treble-frequency", ParameterKind.Number, 500, 8000, 2000, "Hz"),
        new("volume", ParameterKind.Number, -60, 0, -20, "dB", Pot: "P4"),
        new("output", ParameterKind.Switch, 0, 1, 0),
    ];

    private static readonly SimulatedParameter[] Receiver =
    [
        new("bass-gain", ParameterKind.Number, -12, 12, 0, "dB"),
        new("bass-frequency", ParameterKind.Number, 50, 400, 200, "Hz"),
        new("middle-gain", ParameterKind.Number, -12, 12, 0, "dB"),
        new("middle-frequency", ParameterKind.Number, 300, 5000, 1000, "Hz"),
        new("treble-gain", ParameterKind.Number, -12, 12, 0, "dB"),
        new("treble-frequency", ParameterKind.Number, 500, 8000, 2000, "Hz"),
        new("volume", ParameterKind.Number, -60, 0, -20, "dB", Pot: "P4"),
        new("output", ParameterKind.Switch, 0, 1, 0),
        new("mode", ParameterKind.Choice, 0, 5, 2, Choices: ["off", "am", "usb", "lsb", "cw", "fm"]),
        new("offset", ParameterKind.Integer, -21000, 21000, 10000, "Hz"),
        new("low", ParameterKind.Integer, -5500, 5450, 300, "Hz"),
        new("high", ParameterKind.Integer, -5450, 5500, 2700, "Hz"),
        new("width", ParameterKind.Integer, 100, 11000, 2400, "Hz", Pot: "P1"),
        new("stopband", ParameterKind.Integer, 40, 100, 80, "dB"),
        new("pitch", ParameterKind.Integer, 300, 1000, 600, "Hz"),
        new("agc", ParameterKind.Choice, 0, 3, 2, Choices: ["off", "fast", "medium", "slow"]),
        new("agc-gain", ParameterKind.Integer, 0, 90, 60, "dB"),
        new("hang", ParameterKind.Number, 0, 5, 0.5, "s"),
        new("decay", ParameterKind.Integer, 1, 200, 20, "dB/s"),
        new("fm-deviation", ParameterKind.Integer, 100, 5000, 2500, "Hz"),
        new("deemphasis", ParameterKind.Integer, 0, 2000, 0, "us"),
        new("squelch", ParameterKind.Integer, -150, 0, -150, "dBFS", Pot: "P3"),
        new("input", ParameterKind.Choice, 0, 2, 0, Choices: ["iq", "qi", "if"]),
        new("iq-gain", ParameterKind.Number, -3, 3, 0, "dB"),
        new("iq-phase", ParameterKind.Number, -10, 10, 0, "deg"),
        new("test", ParameterKind.Switch, 0, 1, 0),
        new("test-offset", ParameterKind.Integer, -45000, 45000, 1000, "Hz"),
        new("test-level", ParameterKind.Number, -120, 0, -20, "dBFS"),
        new("test-noise", ParameterKind.Integer, -150, 0, -150, "dBFS"),
        new("test-key", ParameterKind.Integer, 0, 5000, 0, "ms"),
        new("nr", ParameterKind.Choice, 0, 2, 1, Choices: ["off", "lms", "spectral"]),
        new("nr-level", ParameterKind.Integer, 0, 30, 0, "dB", Pot: "P2"),
        new("test-iq-gain", ParameterKind.Number, -3, 3, 0, "dB"),
        new("test-iq-phase", ParameterKind.Number, -10, 10, 0, "deg"),
        new("filter", ParameterKind.Choice, 0, 1, 0, Choices: ["designed", "downloaded"]),
    ];

    private static readonly SimulatedParameter[] Analyzer =
    [
        new("level", ParameterKind.Number, -60, 0, -20, "dBFS", Pot: "P2"),
        new("output", ParameterKind.Switch, 0, 1, 0),
        new("start", ParameterKind.Number, 20, 20000, 20, "Hz"),
        new("stop", ParameterKind.Number, 20, 20000, 20000, "Hz"),
        new("points-per-decade", ParameterKind.Integer, 1, 40, 10),
        new("tone", ParameterKind.Number, 20, 20000, 1000, "Hz"),
    ];

    private static readonly SimulatedParameter[] Synthesizer =
    [
        new("osc1-waveform", ParameterKind.Choice, 0, 3, 1, Choices: SynthWaveforms),
        new("volume", ParameterKind.Number, -60, 0, -20, "dB", Pot: "P4"),
        new("output", ParameterKind.Switch, 0, 1, 0),
        new("channel", ParameterKind.Integer, 1, 16, 15),
        new("osc1-octave", ParameterKind.Integer, -2, 2, 0),
        new("osc1-level", ParameterKind.Integer, 0, 100, 100, "%"),
        new("osc2-waveform", ParameterKind.Choice, 0, 3, 1, Choices: SynthWaveforms),
        new("osc2-octave", ParameterKind.Integer, -2, 2, 0),
        new("osc2-detune", ParameterKind.Integer, -700, 700, 0, "cents"),
        new("osc2-level", ParameterKind.Integer, 0, 100, 0, "%"),
        new("osc3-waveform", ParameterKind.Choice, 0, 3, 1, Choices: SynthWaveforms),
        new("osc3-octave", ParameterKind.Integer, -2, 2, 0),
        new("osc3-detune", ParameterKind.Integer, -700, 700, 0, "cents"),
        new("osc3-level", ParameterKind.Integer, 0, 100, 0, "%"),
        new("noise-level", ParameterKind.Integer, 0, 100, 0, "%"),
        new("lfo1-rate", ParameterKind.Number, 0.1, 20, 5, "Hz"),
        new("lfo1-waveform", ParameterKind.Choice, 0, 2, 1, Choices: LfoWaveforms),
        new("lfo1-depth", ParameterKind.Integer, 0, 1200, 50, "cents"),
        new("lfo2-rate", ParameterKind.Number, 0.1, 20, 4, "Hz"),
        new("lfo2-waveform", ParameterKind.Choice, 0, 2, 0, Choices: LfoWaveforms),
        new("lfo2-depth", ParameterKind.Integer, 0, 100, 0, "%"),
        new("attack", ParameterKind.Integer, 1, 10000, 5, "ms"),
        new("decay", ParameterKind.Integer, 1, 10000, 200, "ms"),
        new("sustain", ParameterKind.Integer, 0, 100, 100, "%"),
        new("release", ParameterKind.Integer, 1, 10000, 100, "ms"),
        new("cutoff", ParameterKind.Integer, 20, 20000, 20000, "Hz", Pot: "P2"),
        new("emphasis", ParameterKind.Integer, 0, 100, 0, "%", Pot: "P3"),
        new("contour-amount", ParameterKind.Integer, 0, 100, 0, "%"),
        new("contour-attack", ParameterKind.Integer, 1, 10000, 5, "ms"),
        new("contour-decay", ParameterKind.Integer, 1, 10000, 300, "ms"),
        new("contour-sustain", ParameterKind.Integer, 0, 100, 50, "%"),
        new("key-tracking", ParameterKind.Integer, 0, 100, 0, "%"),
        new("glide", ParameterKind.Integer, 0, 5000, 0, "ms"),
        new("bend-range", ParameterKind.Integer, 0, 12, 2, "semitones"),
        new("velocity-loudness", ParameterKind.Integer, 0, 100, 0, "%"),
        new("velocity-brightness", ParameterKind.Integer, 0, 100, 0, "%"),
        new("filter-lfo", ParameterKind.Integer, 0, 100, 0, "%"),
        new("osc1-width", ParameterKind.Integer, 5, 95, 50, "%", Pot: "P1"),
        new("osc2-width", ParameterKind.Integer, 5, 95, 50, "%"),
        new("osc3-width", ParameterKind.Integer, 5, 95, 50, "%"),
        new("osc1-symmetry", ParameterKind.Integer, 10, 90, 50, "%"),
        new("osc2-symmetry", ParameterKind.Integer, 10, 90, 50, "%"),
        new("osc3-symmetry", ParameterKind.Integer, 10, 90, 50, "%"),
        new("lfo1-width", ParameterKind.Integer, 5, 95, 50, "%"),
        new("lfo2-width", ParameterKind.Integer, 5, 95, 50, "%"),
        new("lfo1-symmetry", ParameterKind.Integer, 0, 100, 50, "%"),
        new("lfo2-symmetry", ParameterKind.Integer, 0, 100, 50, "%"),
        new("pwm-depth", ParameterKind.Integer, 0, 100, 0, "%"),
    ];
}
