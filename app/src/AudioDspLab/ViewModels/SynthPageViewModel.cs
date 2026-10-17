using System.Globalization;

namespace AudioDspLab.ViewModels;

/// <summary>One oscillator: waveform keys, octave, detune (not oscillator 1), level, and its shape.</summary>
public sealed class OscillatorViewModel : ObservableObject
{
    internal OscillatorViewModel(
        int index,
        ParameterViewModel waveform,
        ParameterViewModel octave,
        ParameterViewModel? detune,
        ParameterViewModel level,
        ParameterViewModel width,
        ParameterViewModel symmetry)
    {
        Title = $"Oscillator {index + 1}";
        Waveform = waveform;
        Octave = octave;
        Detune = detune;
        Level = level;
        Width = width;
        Symmetry = symmetry;
        waveform.ValueChanged += _ =>
        {
            RaisePropertyChanged(nameof(Shape));
            RaisePropertyChanged(nameof(HasShape));
        };
    }

    public string Title { get; }

    public ParameterViewModel Waveform { get; }

    public ParameterViewModel Octave { get; }

    public ParameterViewModel? Detune { get; }

    public bool HasDetune => Detune is not null;

    public ParameterViewModel Level { get; }

    public ParameterViewModel Width { get; }

    public ParameterViewModel Symmetry { get; }

    /// <summary>The rectangle's width or the triangle's symmetry; null for the other waveforms.</summary>
    public ParameterViewModel? Shape =>
        Waveform.Selected switch
        {
            "rectangle" => Width,
            "triangle" => Symmetry,
            _ => null
        };

    public bool HasShape => Shape is not null;
}

/// <summary>One LFO: waveform keys, rate, depth, its shape, and its reach into the filter or the width.</summary>
public sealed class LfoViewModel : ObservableObject
{
    internal LfoViewModel(
        string title,
        ParameterViewModel waveform,
        ParameterViewModel rate,
        ParameterViewModel depth,
        ParameterViewModel width,
        ParameterViewModel symmetry,
        ParameterViewModel? filterDepth = null,
        ParameterViewModel? pwmDepth = null)
    {
        Title = title;
        Waveform = waveform;
        Rate = rate;
        Depth = depth;
        Width = width;
        Symmetry = symmetry;
        FilterDepth = filterDepth;
        PwmDepth = pwmDepth;
        waveform.ValueChanged += _ =>
        {
            RaisePropertyChanged(nameof(Shape));
            RaisePropertyChanged(nameof(HasShape));
        };
    }

    public string Title { get; }

    public ParameterViewModel Waveform { get; }

    public ParameterViewModel Rate { get; }

    public ParameterViewModel Depth { get; }

    public ParameterViewModel Width { get; }

    public ParameterViewModel Symmetry { get; }

    public ParameterViewModel? Shape =>
        Waveform.Selected switch
        {
            "square" => Width,
            "triangle" => Symmetry,
            _ => null
        };

    public bool HasShape => Shape is not null;

    public ParameterViewModel? FilterDepth { get; }

    public bool HasFilterDepth => FilterDepth is not null;

    public ParameterViewModel? PwmDepth { get; }

    public bool HasPwmDepth => PwmDepth is not null;
}

/// <summary>
/// The MiniMoog-style synthesizer: three oscillators and noise, a ladder
/// filter with its contour, an envelope, two LFOs, glide, bend and
/// velocity, played from MIDI on K5.
/// </summary>
public sealed class SynthPageViewModel : ApplicationPageViewModel
{
    private static readonly (string, string)[] Waveforms =
        [("sine", "Sine"), ("saw", "Saw"), ("rectangle", "Rectangle"), ("triangle", "Triangle")];

    private static readonly (string, string)[] LfoWaveforms = [("sine", "Sine"), ("triangle", "Triangle"), ("square", "Square")];

    // the filter's curve: points on the equalizer's logarithmic grid
    private static readonly IReadOnlyList<double> CurveFrequencies = EqualizerResponse.LogarithmicFrequencies(160);

    private SynthVoice? voice;

    public SynthPageViewModel(ParameterWrite write, BoardAction run)
        : base(write, run, Applications.Synthesizer)
    {
        Volume = Number("volume", "Volume", -60, 0, "-- dB --");
        Output = Switch("output");
        Channel = Integer("channel", "MIDI channel", 1, 16, "-- 1 to 16 --");
        Oscillators = [.. Enumerable.Range(1, 3).Select(Oscillator)];
        Noise = Integer("noise-level", "Noise", 0, 100, "-- % --");
        Lfos =
        [
            new LfoViewModel(
                "LFO 1 · vibrato",
                Choice("lfo1-waveform", "Waveform", LfoWaveforms),
                Number("lfo1-rate", "Rate", 0.1, 20, "-- Hz --"),
                Integer("lfo1-depth", "Depth", 0, 1200, "-- cents --"),
                Integer("lfo1-width", "Width", 5, 95, "-- % --"),
                Integer("lfo1-symmetry", "Symmetry", 0, 100, "-- % rise --"),
                filterDepth: Integer("filter-lfo", "Filter", 0, 100, "-- % --")),
            new LfoViewModel(
                "LFO 2 · tremolo, PWM",
                Choice("lfo2-waveform", "Waveform", LfoWaveforms),
                Number("lfo2-rate", "Rate", 0.1, 20, "-- Hz --"),
                Integer("lfo2-depth", "Depth", 0, 100, "-- % --"),
                Integer("lfo2-width", "Width", 5, 95, "-- % --"),
                Integer("lfo2-symmetry", "Symmetry", 0, 100, "-- % rise --"),
                pwmDepth: Integer("pwm-depth", "PWM", 0, 100, "-- % of 45 --")),
        ];
        Filter =
        [
            Integer("cutoff", "Cutoff", 20, 20000, "-- Hz --"),
            Integer("emphasis", "Emphasis", 0, 100, "-- % --"),
            Integer("contour-amount", "Contour amount", 0, 100, "-- % of 5 octaves --"),
            Integer("key-tracking", "Keyboard tracking", 0, 100, "-- % --"),
            Integer("contour-attack", "Contour attack", 1, 10000, "-- ms --"),
            Integer("contour-decay", "Contour decay", 1, 10000, "-- ms, also release --"),
            Integer("contour-sustain", "Contour sustain", 0, 100, "-- % --"),
        ];
        Performance =
        [
            Integer("glide", "Glide", 0, 5000, "-- ms, 0 off --"),
            Integer("bend-range", "Bend range", 0, 12, "-- semitones --"),
            Integer("velocity-loudness", "Velocity to loudness", 0, 100, "-- % --"),
            Integer("velocity-brightness", "Velocity to brightness", 0, 100, "-- % of 3 octaves --"),
        ];
        Envelope =
        [
            Integer("attack", "Attack", 1, 10000, "-- ms --"),
            Integer("decay", "Decay", 1, 10000, "-- ms --"),
            Integer("sustain", "Sustain", 0, 100, "-- % --"),
            Integer("release", "Release", 1, 10000, "-- ms --"),
        ];

        foreach (ParameterViewModel parameter in Filter.Take(3))
        {
            parameter.ValueChanged += _ =>
            {
                RaisePropertyChanged(nameof(FilterResponse));
                RaisePropertyChanged(nameof(ContourResponse));
            };
        }

        foreach (ParameterViewModel parameter in Filter.Skip(4).Concat(Envelope))
        {
            parameter.ValueChanged += _ => RaisePropertyChanged(nameof(Envelopes));
        }
    }

    public ParameterViewModel Volume { get; }

    public ParameterViewModel Output { get; }

    public ParameterViewModel Channel { get; }

    public IReadOnlyList<OscillatorViewModel> Oscillators { get; }

    public ParameterViewModel Noise { get; }

    public IReadOnlyList<LfoViewModel> Lfos { get; }

    public IReadOnlyList<ParameterViewModel> Filter { get; }

    public IReadOnlyList<ParameterViewModel> Performance { get; }

    public IReadOnlyList<ParameterViewModel> Envelope { get; }

    /// <summary>The filter's response at the cutoff set (at middle C, the contour at rest).</summary>
    public IReadOnlyList<double>? FilterResponse =>
        Filter[0].Value is double cutoff && Filter[1].Value is double emphasis
            ? SynthCurves.LadderResponse(cutoff, emphasis, CurveFrequencies)
            : null;

    /// <summary>The response where the contour takes the cutoff at its peak; null without a contour.</summary>
    public IReadOnlyList<double>? ContourResponse =>
        Filter[0].Value is double cutoff && Filter[1].Value is double emphasis && Filter[2].Value is double amount && amount > 0
            ? SynthCurves.LadderResponse(SynthCurves.ContourPeakCutoff(cutoff, amount), emphasis, CurveFrequencies)
            : null;

    /// <summary>The loudness envelope and the filter's contour, for the envelope graph.</summary>
    public SynthEnvelopes? Envelopes =>
        Envelope[0].Value is double attack && Envelope[1].Value is double decay
        && Envelope[2].Value is double sustain && Envelope[3].Value is double release
        && Filter[4].Value is double contourAttack && Filter[5].Value is double contourDecay
        && Filter[6].Value is double contourSustain
            ? new SynthEnvelopes(attack, decay, sustain / 100, release, contourAttack, contourDecay, contourSustain / 100)
            : null;

    /// <summary>The note sounding, as "C4 · 60", or dashes.</summary>
    public string NoteText =>
        voice?.Note is int note
            ? string.Format(CultureInfo.CurrentCulture, "{0} · {1}", SynthVoice.NoteName(note), note)
            : "---";

    public bool IsNoteSounding => voice?.Gate == true;

    public string StageText => voice?.Stage ?? string.Empty;

    public string ModStripText =>
        voice is { } current
            ? string.Format(CultureInfo.CurrentCulture, "{0:0} %", current.ModStrip * 100.0 / 127)
            : "-- %";

    internal void ApplyVoice(SynthVoice? read)
    {
        voice = read;
        RaisePropertyChanged(nameof(NoteText));
        RaisePropertyChanged(nameof(IsNoteSounding));
        RaisePropertyChanged(nameof(StageText));
        RaisePropertyChanged(nameof(ModStripText));
    }

    internal override void Forget()
    {
        base.Forget();
        ApplyVoice(null);
    }

    private OscillatorViewModel Oscillator(int number) =>
        new(number - 1,
            Choice($"osc{number}-waveform", "Waveform", Waveforms),
            Integer($"osc{number}-octave", "Octave", -2, 2, "-- oct --"),
            number == 1 ? null : Integer($"osc{number}-detune", "Detune", -700, 700, "-- ct --"),
            Integer($"osc{number}-level", "Level", 0, 100, "-- % --"),
            Integer($"osc{number}-width", "Width", 5, 95, "-- % --"),
            Integer($"osc{number}-symmetry", "Rise", 10, 90, "-- % --"));
}
