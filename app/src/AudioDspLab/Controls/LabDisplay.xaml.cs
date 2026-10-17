using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using AudioDspLab.Themes;

namespace AudioDspLab.Controls;

/// <summary>
/// One signal display in the RF-Lab style: a title, a body (the digits)
/// and a marker badge that lights when the value changed on the board.
/// Every value is a dependency property, so the display is bound, never
/// pushed.
/// </summary>
public partial class LabDisplay : UserControl
{
    public static readonly DependencyProperty TitleProperty = DependencyProperty.Register(
        nameof(Title), typeof(string), typeof(LabDisplay), new PropertyMetadata(string.Empty));

    public static readonly DependencyProperty BodyProperty = DependencyProperty.Register(
        nameof(Body), typeof(object), typeof(LabDisplay), new PropertyMetadata(null));

    public static readonly DependencyProperty MarkerProperty = DependencyProperty.Register(
        nameof(Marker), typeof(string), typeof(LabDisplay),
        new PropertyMetadata(string.Empty, (display, _) => ((LabDisplay)display).ColourBadge()));

    public static readonly DependencyProperty IsMarkedProperty = DependencyProperty.Register(
        nameof(IsMarked), typeof(bool), typeof(LabDisplay), new PropertyMetadata(false));

    public static readonly DependencyProperty DisplayBackgroundProperty = DependencyProperty.Register(
        nameof(DisplayBackground), typeof(Brush), typeof(LabDisplay), new PropertyMetadata(Brushes.Maroon));

    public static readonly DependencyProperty TitleFontSizeProperty = DependencyProperty.Register(
        nameof(TitleFontSize), typeof(double), typeof(LabDisplay), new PropertyMetadata(22.0));

    public static readonly DependencyProperty MarkerFontSizeProperty = DependencyProperty.Register(
        nameof(MarkerFontSize), typeof(double), typeof(LabDisplay), new PropertyMetadata(12.0));

    public LabDisplay()
    {
        InitializeComponent();
        Loaded += (_, _) => ColourBadge();
    }

    public string Title
    {
        get => (string)GetValue(TitleProperty);
        set => SetValue(TitleProperty, value);
    }

    /// <summary>
    /// What the inset frame shows: the value's digits. Like any content of
    /// a XAML user control, the body cannot carry a name of the window.
    /// </summary>
    public object? Body
    {
        get => GetValue(BodyProperty);
        set => SetValue(BodyProperty, value);
    }

    /// <summary>The badge text, the knob that drives this value: P1, P2.</summary>
    public string Marker
    {
        get => (string)GetValue(MarkerProperty);
        set => SetValue(MarkerProperty, value);
    }

    public bool IsMarked
    {
        get => (bool)GetValue(IsMarkedProperty);
        set => SetValue(IsMarkedProperty, value);
    }

    public Brush DisplayBackground
    {
        get => (Brush)GetValue(DisplayBackgroundProperty);
        set => SetValue(DisplayBackgroundProperty, value);
    }

    public double TitleFontSize
    {
        get => (double)GetValue(TitleFontSizeProperty);
        set => SetValue(TitleFontSizeProperty, value);
    }

    /// <summary>The badge's text size, smaller with a smaller title.</summary>
    public double MarkerFontSize
    {
        get => (double)GetValue(MarkerFontSizeProperty);
        set => SetValue(MarkerFontSizeProperty, value);
    }

    // each theme gives every pot's badge its colour (Studio: the knob cap's, P2 green ...)
    private void ColourBadge()
    {
        Brush fallback = ThemeResources.Brush(this, "AudioLabMarkerBackground", Brushes.Gold);
        MarkerBadge.Background = string.IsNullOrEmpty(Marker) ? fallback : ThemeResources.Brush(this, "AudioLabMarker" + Marker, fallback);
    }

    /// <summary>Whether the badge is visible, for tests.</summary>
    internal bool IsMarkerVisible => MarkerBadge.Visibility == Visibility.Visible;
}
