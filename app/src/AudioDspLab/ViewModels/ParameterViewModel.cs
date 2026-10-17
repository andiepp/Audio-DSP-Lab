using System.Globalization;
using System.Windows.Input;
using AudioDspLab.Protocol;

namespace AudioDspLab.ViewModels;

/// <summary>Writes one parameter's value as protocol text.</summary>
public delegate Task ParameterWrite(ParameterViewModel parameter, string value);

/// <summary>One key of a choice: lit while the board runs that choice.</summary>
public sealed class ChoiceKey : ObservableObject
{
    private bool isSelected;

    internal ChoiceKey(string name, string label, ICommand select)
    {
        Name = name;
        Label = label;
        SelectCommand = select;
    }

    /// <summary>The choice's protocol name, the command's parameter.</summary>
    public string Name { get; }

    public string Label { get; }

    public ICommand SelectCommand { get; }

    public bool IsSelected
    {
        get => isSelected;
        internal set => SetProperty(ref isSelected, value);
    }
}

/// <summary>
/// One parameter of the running application as a page shows it: a dial
/// for a number, keys for a choice, a toggle for a switch. It shows what
/// the board reports, never what was asked for.
/// </summary>
public sealed class ParameterViewModel : ObservableObject
{
    /// <summary>How long a "changed on the board" badge stays lit.</summary>
    public static readonly TimeSpan MarkerDuration = TimeSpan.FromSeconds(1);

    private readonly ParameterWrite write;
    private readonly double defaultMinimum;
    private readonly double defaultMaximum;
    private ParameterDescriptor? descriptor;
    private string? text;
    private bool isAvailable;
    private DateTime markedUntil;
    private bool isMarked;

    public ParameterViewModel(
        string name,
        string title,
        ParameterKind kind,
        ParameterWrite write,
        double minimum = 0,
        double maximum = 0,
        int fractionDigits = 0,
        string unitLine = "",
        IReadOnlyList<(string Name, string Label)>? choices = null,
        string offOnLabel = "Output")
    {
        Name = name;
        Title = title;
        Kind = kind;
        this.write = write ?? throw new ArgumentNullException(nameof(write));
        defaultMinimum = minimum;
        defaultMaximum = maximum;
        FractionDigits = fractionDigits;
        UnitLine = unitLine;
        SwitchLabel = offOnLabel;
        IntegerDigits = Math.Max(1, (int)Math.Floor(Math.Log10(Math.Max(1, Math.Max(Math.Abs(minimum), Math.Abs(maximum))))) + 1);
        SelectCommand = new RelayCommand(
            choice => _ = SelectAsync(choice as string),
            choice => isAvailable && choice is string);
        ToggleCommand = new RelayCommand(() => _ = SetAsync(IsOn != true), () => isAvailable && IsOn is not null);
        Keys = [.. (choices ?? []).Select(choice => new ChoiceKey(choice.Name, choice.Label, SelectCommand))];
    }

    /// <summary>The protocol name, e.g. <c>frequency</c>.</summary>
    public string Name { get; }

    public string Title { get; }

    public ParameterKind Kind { get; }

    /// <summary>The line under a dial, e.g. "-- Hz --".</summary>
    public string UnitLine { get; }

    public double Minimum => descriptor?.Minimum ?? defaultMinimum;

    public double Maximum => descriptor?.Maximum ?? defaultMaximum;

    /// <summary>The digits a dial shows before the point, from its range (fixed, so the layout never jumps).</summary>
    public int IntegerDigits { get; }

    public int FractionDigits { get; }

    public bool ShowSign => defaultMinimum < 0;

    /// <summary>The pot that drives it in the running application (P1..P4), or null.</summary>
    public string? Knob => descriptor?.Pot;

    /// <summary>Whether the running application has it.</summary>
    public bool IsAvailable
    {
        get => isAvailable;
        private set => SetProperty(ref isAvailable, value);
    }

    /// <summary>The value as the board wrote it, or null while unknown.</summary>
    public string? Text
    {
        get => text;
        private set
        {
            if (!SetProperty(ref text, value))
            {
                return;
            }

            RaisePropertyChanged(nameof(Value));
            RaisePropertyChanged(nameof(DialValue));
            RaisePropertyChanged(nameof(IsOn));
            RaisePropertyChanged(nameof(OnOffText));
            RaisePropertyChanged(nameof(Selected));
            foreach (ChoiceKey key in Keys)
            {
                key.IsSelected = string.Equals(key.Name, value, StringComparison.OrdinalIgnoreCase);
            }

            ValueChanged?.Invoke(this);
        }
    }

    /// <summary>Raised whenever the value the board reports changes.</summary>
    public event Action<ParameterViewModel>? ValueChanged;

    /// <summary>A number's or an integer's value; null while unknown or for other kinds.</summary>
    public double? Value =>
        Kind is ParameterKind.Number or ParameterKind.Integer ? ProtocolText.ParseNumberOrNone(text) : null;

    /// <summary>The value for a digit dial; setting it writes the value.</summary>
    public double? DialValue
    {
        get => Value;
        set
        {
            if (value is double number && number != Value && isAvailable)
            {
                _ = SetAsync(number);
            }
        }
    }

    /// <summary>A switch's state; null while unknown.</summary>
    public bool? IsOn => Kind == ParameterKind.Switch && text is not null ? text == "on" : null;

    public string SwitchLabel { get; }

    /// <summary>"Output on", "Output off", "Output ?".</summary>
    public string OnOffText =>
        IsOn switch
        {
            true => $"{SwitchLabel} on",
            false => $"{SwitchLabel} off",
            null => $"{SwitchLabel} ?"
        };

    /// <summary>A choice's name, or null while unknown.</summary>
    public string? Selected => Kind == ParameterKind.Choice ? text : null;

    public IReadOnlyList<ChoiceKey> Keys { get; }

    /// <summary>Takes a choice's protocol name.</summary>
    public ICommand SelectCommand { get; }

    public ICommand ToggleCommand { get; }

    /// <summary>Lit for a moment after a pot changed it.</summary>
    public bool IsChangedOnBoard
    {
        get => isMarked;
        private set => SetProperty(ref isMarked, value);
    }

    /// <summary>The value for a status line: "440 Hz", "on", "square".</summary>
    public string DescribeValue(string? value = null)
    {
        string shown = value ?? text ?? "?";
        string unit = descriptor?.Unit ?? string.Empty;
        return unit.Length == 0 ? shown : $"{shown} {unit}";
    }

    public Task SetAsync(double number) =>
        double.IsFinite(number) ? Write(ProtocolText.FormatNumber(Math.Clamp(number, Minimum, Maximum))) : Task.CompletedTask;

    public Task SetAsync(bool on) => Write(ProtocolText.FormatSwitch(on));

    public Task SelectAsync(string? choice) =>
        choice is null || string.Equals(choice, text, StringComparison.OrdinalIgnoreCase) ? Task.CompletedTask : Write(choice);

    /// <summary>Writes a value as protocol text, e.g. from stored settings.</summary>
    public Task Write(string value) => isAvailable ? write(this, value) : Task.CompletedTask;

    internal void Attach(ParameterDescriptor parameter)
    {
        descriptor = parameter;
        RaisePropertyChanged(nameof(Minimum));
        RaisePropertyChanged(nameof(Maximum));
        RaisePropertyChanged(nameof(Knob));
        Text = parameter.Value;
        IsAvailable = true;
    }

    /// <summary>The board reports a value: after a write, a read, or an event (fromBoard: a pot moved it).</summary>
    internal void Apply(string value, bool fromBoard)
    {
        if (fromBoard && text is not null && value != text)
        {
            markedUntil = DateTime.UtcNow + MarkerDuration;
            IsChangedOnBoard = true;
        }

        Text = value;
    }

    internal void DecayMarker(DateTime now)
    {
        if (isMarked && now >= markedUntil)
        {
            IsChangedOnBoard = false;
        }
    }

    /// <summary>The application stopped serving it: dashes, no badge.</summary>
    internal void Forget()
    {
        descriptor = null;
        IsAvailable = false;
        Text = null;
        IsChangedOnBoard = false;
        RaisePropertyChanged(nameof(Knob));
        RaisePropertyChanged(nameof(Minimum));
        RaisePropertyChanged(nameof(Maximum));
    }

    public override string ToString() => string.Create(CultureInfo.InvariantCulture, $"{Name}={text}");
}
