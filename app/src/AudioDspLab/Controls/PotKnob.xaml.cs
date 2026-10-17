using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using AudioDspLab.Themes;

namespace AudioDspLab.Controls;

/// <summary>
/// Shows one board potentiometer. Every value is a dependency property.
/// </summary>
public partial class PotKnob : UserControl
{
    public static readonly DependencyProperty LabelProperty = DependencyProperty.Register(
        nameof(Label), typeof(string), typeof(PotKnob),
        new PropertyMetadata(string.Empty, (knob, _) => ((PotKnob)knob).ApplyTheme()));

    public static readonly DependencyProperty RoleProperty = DependencyProperty.Register(
        nameof(Role), typeof(string), typeof(PotKnob), new PropertyMetadata(string.Empty));

    public static readonly DependencyProperty AngleProperty = DependencyProperty.Register(
        nameof(Angle), typeof(double), typeof(PotKnob),
        new PropertyMetadata(0.0, (knob, _) => ((PotKnob)knob).DrawValueArc()));

    public static readonly DependencyProperty IsAssignedProperty = DependencyProperty.Register(
        nameof(IsAssigned), typeof(bool), typeof(PotKnob), new PropertyMetadata(true));

    // the gauge: a 270-degree arc around the knob, from -135 (left stop) to +135 (right stop)
    private const double Centre = 24;
    private const double Radius = 22;
    private const double Travel = 135;

    public PotKnob()
    {
        InitializeComponent();
        TrackArc.Data = Arc(-Travel, Travel);
        Loaded += (_, _) => ApplyTheme();
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

    /// <summary>The pointer's angle in degrees, 0 at top centre, -135 to +135.</summary>
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

    private void ApplyTheme()
    {
        Brush cap = ThemeResources.PotBrush(this, Label, Brushes.White);
        Cap.Fill = cap;
        ValueArc.Stroke = cap;
        ValueArc.Visibility = ThemeResources.Value(this, "AudioLabKnobShowsArc", false) ? Visibility.Visible : Visibility.Collapsed;
        DrawValueArc();
    }

    private void DrawValueArc() => ValueArc.Data = Angle > -Travel + 0.5 ? Arc(-Travel, Math.Min(Angle, Travel)) : null;

    private static Geometry Arc(double fromDegrees, double toDegrees)
    {
        static Point At(double degrees)
        {
            double radians = degrees * Math.PI / 180;
            return new Point(Centre + (Radius * Math.Sin(radians)), Centre - (Radius * Math.Cos(radians)));
        }

        var figure = new PathFigure { StartPoint = At(fromDegrees), IsClosed = false, IsFilled = false };
        figure.Segments.Add(new ArcSegment(At(toDegrees), new Size(Radius, Radius), 0,
            toDegrees - fromDegrees > 180, SweepDirection.Clockwise, isStroked: true));
        var geometry = new PathGeometry([figure]);
        geometry.Freeze();
        return geometry;
    }
}
