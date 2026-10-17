using System.Windows;
using System.Windows.Controls;

namespace AudioDspLab.Controls;

/// <summary>
/// Shows one board potentiometer. Every value is a dependency property.
/// </summary>
public partial class PotKnob : UserControl
{
    public static readonly DependencyProperty LabelProperty = DependencyProperty.Register(
        nameof(Label), typeof(string), typeof(PotKnob), new PropertyMetadata(string.Empty));

    public static readonly DependencyProperty RoleProperty = DependencyProperty.Register(
        nameof(Role), typeof(string), typeof(PotKnob), new PropertyMetadata(string.Empty));

    public static readonly DependencyProperty AngleProperty = DependencyProperty.Register(
        nameof(Angle), typeof(double), typeof(PotKnob), new PropertyMetadata(0.0));

    public static readonly DependencyProperty IsAssignedProperty = DependencyProperty.Register(
        nameof(IsAssigned), typeof(bool), typeof(PotKnob), new PropertyMetadata(true));

    public PotKnob()
    {
        InitializeComponent();
    }

    public string Label
    {
        get => (string)GetValue(LabelProperty);
        set => SetValue(LabelProperty, value);
    }

    public string Role
    {
        get => (string)GetValue(RoleProperty);
        set => SetValue(RoleProperty, value);
    }

    /// <summary>The pointer's angle in degrees, 0 at top centre.</summary>
    public double Angle
    {
        get => (double)GetValue(AngleProperty);
        set => SetValue(AngleProperty, value);
    }

    public bool IsAssigned
    {
        get => (bool)GetValue(IsAssignedProperty);
        set => SetValue(IsAssignedProperty, value);
    }

    /// <summary>The pointer's rendered angle, for tests.</summary>
    internal double PointerAngle => PointerRotation.Angle;
}
