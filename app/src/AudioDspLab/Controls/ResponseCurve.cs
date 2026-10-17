using System.Globalization;
using System.Windows;
using System.Windows.Media;
using AudioDspLab.Themes;

namespace AudioDspLab.Controls;

/// <summary>
/// Draws a response on a logarithmic frequency axis from 20 Hz to 20 kHz
/// and a gain axis (the equalizer's +/-15 dB unless set), in the theme's
/// chart colours: the curve, a dashed second curve if given, a dot for
/// each marker frequency. The inputs are dependency properties; the
/// control computes nothing about filters.
/// </summary>
public sealed class ResponseCurve : FrameworkElement
{
    public const double MinimumHertz = 20.0;
    public const double MaximumHertz = 20_000.0;
    public const double RangeDecibel = 15.0;

    public static readonly DependencyProperty SecondaryResponseProperty = DependencyProperty.Register(
        nameof(SecondaryResponse),
        typeof(IReadOnlyList<double>),
        typeof(ResponseCurve),
        new FrameworkPropertyMetadata(null, FrameworkPropertyMetadataOptions.AffectsRender));

    public static readonly DependencyProperty TopDecibelProperty = DependencyProperty.Register(
        nameof(TopDecibel), typeof(double), typeof(ResponseCurve),
        new FrameworkPropertyMetadata(RangeDecibel, FrameworkPropertyMetadataOptions.AffectsRender));

    public static readonly DependencyProperty BottomDecibelProperty = DependencyProperty.Register(
        nameof(BottomDecibel), typeof(double), typeof(ResponseCurve),
        new FrameworkPropertyMetadata(-RangeDecibel, FrameworkPropertyMetadataOptions.AffectsRender));

    public static readonly DependencyProperty DecibelStepProperty = DependencyProperty.Register(
        nameof(DecibelStep), typeof(double), typeof(ResponseCurve),
        new FrameworkPropertyMetadata(6.0, FrameworkPropertyMetadataOptions.AffectsRender));

    public static readonly DependencyProperty CompactLabelsProperty = DependencyProperty.Register(
        nameof(CompactLabels), typeof(bool), typeof(ResponseCurve),
        new FrameworkPropertyMetadata(false, FrameworkPropertyMetadataOptions.AffectsRender));

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
    // the colours, from the theme when the curve is drawn
    private Pen GridPen = new(Brushes.DimGray, 1);
    private Pen ZeroPen = new(Brushes.Gray, 1);
    private Pen CurvePen = new(Brushes.Gold, 2);
    private Pen SecondaryPen = new(Brushes.DeepSkyBlue, 1.5);
    private Brush LabelBrush = Brushes.Silver;
    private Brush MarkerBrush = Brushes.White;
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
    /// <summary>A second response on the same frequencies, drawn dashed.</summary>
    public IReadOnlyList<double>? SecondaryResponse
    {
        get => (IReadOnlyList<double>?)GetValue(SecondaryResponseProperty);
        set => SetValue(SecondaryResponseProperty, value);
    }

    /// <summary>The gain at the top of the chart; responses above it are drawn at the top.</summary>
    public double TopDecibel
    {
        get => (double)GetValue(TopDecibelProperty);
        set => SetValue(TopDecibelProperty, value);
    }

    public double BottomDecibel
    {
        get => (double)GetValue(BottomDecibelProperty);
        set => SetValue(BottomDecibelProperty, value);
    }

    /// <summary>The gain between two grid lines, counted from 0 dB.</summary>
    public double DecibelStep
    {
        get => (double)GetValue(DecibelStepProperty);
        set => SetValue(DecibelStepProperty, value);
    }

    /// <summary>Labels at 100 Hz, 1 kHz and 10 kHz only, for a narrow chart.</summary>
    public bool CompactLabels
    {
        get => (bool)GetValue(CompactLabelsProperty);
        set => SetValue(CompactLabelsProperty, value);
    }

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
                     * (TopDecibel - Math.Clamp(decibel, BottomDecibel, TopDecibel)) / (TopDecibel - BottomDecibel));

    protected override Size MeasureOverride(Size availableSize) =>
        new(double.IsInfinity(availableSize.Width) ? 600 : availableSize.Width,
            double.IsInfinity(availableSize.Height) ? 170 : availableSize.Height);

    protected override void OnRender(DrawingContext context)
    {
        GridPen = Frozen(new Pen(ThemeResources.Brush(this, "AudioLabChartGrid", Brushes.DimGray), 1));
        ZeroPen = Frozen(new Pen(ThemeResources.Brush(this, "AudioLabChartZero", Brushes.Gray), 1));
        CurvePen = Frozen(new Pen(ThemeResources.Brush(this, "AudioLabChartLeft", Brushes.Gold), 2));
        SecondaryPen = Frozen(new Pen(ThemeResources.Brush(this, "AudioLabChartRight", Brushes.DeepSkyBlue), 1.5) { DashStyle = DashStyles.Dash });
        LabelBrush = ThemeResources.Brush(this, "AudioLabChartLabel", Brushes.Silver);
        MarkerBrush = ThemeResources.Brush(this, "AudioLabChartMarker", Brushes.White);
        context.DrawRectangle(ThemeResources.Brush(this, "AudioLabChartBackground", Brushes.Black), null, new Rect(0, 0, ActualWidth, ActualHeight));
        double pixels = VisualTreeHelper.GetDpi(this).PixelsPerDip;

        foreach (double hertz in GridHertz)
        {
            double x = X(hertz);
            context.DrawLine(GridPen, new Point(x, TopMargin), new Point(x, ActualHeight - BottomMargin));
            if (CompactLabels && hertz is not (100 or 1000 or 10000))
            {
                continue;
            }

            string label = hertz >= 1000
                ? (hertz / 1000).ToString(CultureInfo.CurrentCulture) + "k"
                : hertz.ToString(CultureInfo.CurrentCulture);
            FormattedText text = Label(label, pixels);
            double left = Math.Clamp(x - (text.Width / 2), 0, ActualWidth - text.Width - 2);
            context.DrawText(text, new Point(left, ActualHeight - BottomMargin + 3));
        }

        double step = DecibelStep > 0 ? DecibelStep : 6;
        for (double decibel = Math.Ceiling(BottomDecibel / step) * step; decibel <= TopDecibel - (step / 4); decibel += step)
        {
            if (decibel < BottomDecibel + (step / 4))
            {
                continue;
            }

            double y = Y(decibel);
            context.DrawLine(decibel == 0 ? ZeroPen : GridPen, new Point(LeftMargin, y), new Point(ActualWidth - RightMargin, y));
            context.DrawText(Label(decibel.ToString("+0;-0;0", CultureInfo.CurrentCulture), pixels), new Point(4, y - 8));
        }

        DrawCurve(context, Points(SecondaryResponse), SecondaryPen);
        DrawnCurve = Points(Response);
        DrawCurve(context, DrawnCurve, CurvePen);

        foreach (double hertz in MarkerFrequencies ?? [])
        {
            context.DrawEllipse(MarkerBrush, null, new Point(X(hertz), Y(0)), 3, 3);
        }
    }

    private IReadOnlyList<Point> Points(IReadOnlyList<double>? response) =>
        response is { Count: >= 2 }
            ? [.. response.Select((decibel, index) => new Point(
                X(MinimumHertz * Math.Pow(1000.0, index / (double)(response.Count - 1))),
                Y(decibel)))]
            : [];

    private static void DrawCurve(DrawingContext context, IReadOnlyList<Point> points, Pen pen)
    {
        if (points.Count < 2)
        {
            return;
        }

        var geometry = new StreamGeometry();
        using (StreamGeometryContext figure = geometry.Open())
        {
            figure.BeginFigure(points[0], isFilled: false, isClosed: false);
            figure.PolyLineTo([.. points.Skip(1)], isStroked: true, isSmoothJoin: true);
        }

        geometry.Freeze();
        context.DrawGeometry(null, pen, geometry);
    }

    private FormattedText Label(string text, double pixelsPerDip) =>
        new(text, CultureInfo.CurrentCulture, FlowDirection.LeftToRight, LabelFace, 11, LabelBrush, pixelsPerDip);

    private static T Frozen<T>(T freezable) where T : Freezable
    {
        freezable.Freeze();
        return freezable;
    }
}
