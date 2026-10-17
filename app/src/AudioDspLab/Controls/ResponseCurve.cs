using System.Globalization;
using System.Windows;
using System.Windows.Media;

namespace AudioDspLab.Controls;

/// <summary>
/// Draws the equalizer's response on a logarithmic frequency axis from
/// 20 Hz to 20 kHz and a gain axis of +/-15 dB, in the RF-Lab chart's
/// colours: black ground, grey grid, the curve in gold, a dot for each
/// band's frequency. Both inputs are dependency properties; the control
/// computes nothing about filters.
/// </summary>
public sealed class ResponseCurve : FrameworkElement
{
    public const double MinimumHertz = 20.0;
    public const double MaximumHertz = 20_000.0;
    public const double RangeDecibel = 15.0;

    public static readonly DependencyProperty ResponseProperty = DependencyProperty.Register(
        nameof(Response),
        typeof(IReadOnlyList<double>),
        typeof(ResponseCurve),
        new FrameworkPropertyMetadata(null, FrameworkPropertyMetadataOptions.AffectsRender));

    public static readonly DependencyProperty MarkerFrequenciesProperty = DependencyProperty.Register(
        nameof(MarkerFrequencies),
        typeof(IReadOnlyList<double>),
        typeof(ResponseCurve),
        new FrameworkPropertyMetadata(null, FrameworkPropertyMetadataOptions.AffectsRender));

    private const double LeftMargin = 34;
    private const double RightMargin = 8;
    private const double TopMargin = 8;
    private const double BottomMargin = 20;

    private static readonly double[] GridHertz = [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000];
    private static readonly double[] GridDecibel = [-12, -6, 0, 6, 12];
    private static readonly Pen GridPen = Frozen(new Pen(new SolidColorBrush(Color.FromRgb(0x33, 0x33, 0x33)), 1));
    private static readonly Pen ZeroPen = Frozen(new Pen(new SolidColorBrush(Color.FromRgb(0x77, 0x77, 0x77)), 1));
    private static readonly Pen CurvePen = Frozen(new Pen(Brushes.Gold, 2));
    private static readonly Brush LabelBrush = Frozen(new SolidColorBrush(Color.FromRgb(0xB0, 0xB0, 0xB0)));
    private static readonly Typeface LabelFace = new("Segoe UI");

    /// <summary>
    /// The response in dB at frequencies spaced evenly on the logarithmic
    /// axis, the first at 20 Hz and the last at 20 kHz; empty or null draws
    /// the grid alone.
    /// </summary>
    public IReadOnlyList<double>? Response
    {
        get => (IReadOnlyList<double>?)GetValue(ResponseProperty);
        set => SetValue(ResponseProperty, value);
    }

    /// <summary>Where to put a band's dot on the 0 dB line, in Hertz.</summary>
    public IReadOnlyList<double>? MarkerFrequencies
    {
        get => (IReadOnlyList<double>?)GetValue(MarkerFrequenciesProperty);
        set => SetValue(MarkerFrequenciesProperty, value);
    }

    /// <summary>The curve's points as last drawn, for tests.</summary>
    internal IReadOnlyList<Point> DrawnCurve { get; private set; } = [];

    internal double X(double hertz) =>
        LeftMargin + ((ActualWidth - LeftMargin - RightMargin) * Math.Log10(hertz / MinimumHertz) / 3.0);

    internal double Y(double decibel) =>
        TopMargin + ((ActualHeight - TopMargin - BottomMargin)
                     * (RangeDecibel - Math.Clamp(decibel, -RangeDecibel, RangeDecibel)) / (2 * RangeDecibel));

    protected override Size MeasureOverride(Size availableSize) =>
        new(double.IsInfinity(availableSize.Width) ? 600 : availableSize.Width,
            double.IsInfinity(availableSize.Height) ? 170 : availableSize.Height);

    protected override void OnRender(DrawingContext context)
    {
        context.DrawRectangle(Brushes.Black, null, new Rect(0, 0, ActualWidth, ActualHeight));
        double pixels = VisualTreeHelper.GetDpi(this).PixelsPerDip;

        foreach (double hertz in GridHertz)
        {
            double x = X(hertz);
            context.DrawLine(GridPen, new Point(x, TopMargin), new Point(x, ActualHeight - BottomMargin));
            string label = hertz >= 1000
                ? (hertz / 1000).ToString(CultureInfo.CurrentCulture) + "k"
                : hertz.ToString(CultureInfo.CurrentCulture);
            FormattedText text = Label(label, pixels);
            double left = Math.Clamp(x - (text.Width / 2), 0, ActualWidth - text.Width - 2);
            context.DrawText(text, new Point(left, ActualHeight - BottomMargin + 3));
        }

        foreach (double decibel in GridDecibel)
        {
            double y = Y(decibel);
            context.DrawLine(decibel == 0 ? ZeroPen : GridPen, new Point(LeftMargin, y), new Point(ActualWidth - RightMargin, y));
            context.DrawText(Label(decibel.ToString("+0;-0;0", CultureInfo.CurrentCulture), pixels), new Point(4, y - 8));
        }

        IReadOnlyList<double>? response = Response;
        DrawnCurve = response is { Count: >= 2 }
            ? [.. response.Select((decibel, index) => new Point(
                X(MinimumHertz * Math.Pow(1000.0, index / (double)(response.Count - 1))),
                Y(decibel)))]
            : [];
        if (DrawnCurve.Count >= 2)
        {
            var geometry = new StreamGeometry();
            using (StreamGeometryContext figure = geometry.Open())
            {
                figure.BeginFigure(DrawnCurve[0], isFilled: false, isClosed: false);
                figure.PolyLineTo([.. DrawnCurve.Skip(1)], isStroked: true, isSmoothJoin: true);
            }

            geometry.Freeze();
            context.DrawGeometry(null, CurvePen, geometry);
        }

        foreach (double hertz in MarkerFrequencies ?? [])
        {
            context.DrawEllipse(Brushes.White, null, new Point(X(hertz), Y(0)), 3, 3);
        }
    }

    private static FormattedText Label(string text, double pixelsPerDip) =>
        new(text, CultureInfo.CurrentCulture, FlowDirection.LeftToRight, LabelFace, 11, LabelBrush, pixelsPerDip);

    private static T Frozen<T>(T freezable) where T : Freezable
    {
        freezable.Freeze();
        return freezable;
    }
}
