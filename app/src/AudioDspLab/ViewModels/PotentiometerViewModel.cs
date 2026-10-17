using System.Globalization;

namespace AudioDspLab.ViewModels;

/// <summary>
/// One of the board's four pots as the pot strip shows it: where the knob
/// stands and what it drives in the running application.
/// </summary>
public sealed class PotentiometerViewModel : ObservableObject
{
    public const double HalfTravelDegrees = 135.0;
    public const double RawMaximum = 4095.0;
    internal const string Unassigned = "—";

    private double? value;
    private string role = Unassigned;
    private bool isControlOn;
    private bool isForwarded;

    public PotentiometerViewModel(int index)
    {
        Index = index;
        Name = $"P{index + 1}";
    }

    public int Index { get; }

    /// <summary>P1 to P4, as printed on the board.</summary>
    public string Name { get; }

    /// <summary>The raw reading, 0 to 4095, or null while unknown.</summary>
    public double? Value
    {
        get => value;
        set
        {
            if (SetProperty(ref this.value, value))
            {
                RaisePropertyChanged(nameof(Angle));
                RaisePropertyChanged(nameof(PercentText));
            }
        }
    }

    /// <summary>The pointer: fully left -135°, fully right +135°, top while unknown.</summary>
    public double Angle =>
        value is double reading ? -HalfTravelDegrees + (2 * HalfTravelDegrees * Math.Clamp(reading / RawMaximum, 0, 1)) : 0;

    public string Role
    {
        get => role;
        set
        {
            if (SetProperty(ref role, value))
            {
                RaisePropertyChanged(nameof(IsAssigned));
                RaisePropertyChanged(nameof(IsDriving));
            }
        }
    }

    public bool IsAssigned => role != Unassigned;

    /// <summary>Whether the board lets the pots set values (pot control).</summary>
    public bool IsControlOn
    {
        get => isControlOn;
        set
        {
            if (SetProperty(ref isControlOn, value))
            {
                RaisePropertyChanged(nameof(IsDriving));
            }
        }
    }

    /// <summary>Whether the board passes the pot to the DSP's aux ADC (SigmaStudio mode), which pot control does not govern.</summary>
    public bool IsForwarded
    {
        get => isForwarded;
        set
        {
            if (SetProperty(ref isForwarded, value))
            {
                RaisePropertyChanged(nameof(IsDriving));
            }
        }
    }

    /// <summary>Turning the knob changes something right now.</summary>
    public bool IsDriving => IsAssigned && (isControlOn || isForwarded);

    public string PercentText =>
        value is double reading
            ? string.Format(CultureInfo.CurrentCulture, "{0:0} %", 100.0 * Math.Clamp(reading / RawMaximum, 0, 1))
            : "-- %";

    /// <summary>The DSP pin the knob reaches as an auxiliary ADC input (Elektor's wiring).</summary>
    public string AuxPin =>
        Index switch
        {
            0 => "MP9 · AUX ADC0",
            1 => "MP2 · AUX ADC1",
            2 => "MP3 · AUX ADC2",
            _ => "MP8 · AUX ADC3"
        };
}
