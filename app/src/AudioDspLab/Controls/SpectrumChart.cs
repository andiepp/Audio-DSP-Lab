using System.Globalization;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using AudioDspLab.Themes;

namespace AudioDspLab.Controls;

/// <summary>
/// Draws the THD measurement's spectrum on a logarithmic frequency
/// axis, 20 Hz to 24 kHz, from -140 to 0 dBFS: left in gold, right in cyan
/// (the sweep chart's colours), a mark over the fundamental and each
/// harmonic with its order, or over each peak with its rank. Hovering
/// shows the nearest point.
/// </summary>
public sealed class SpectrumChart : FrameworkElement
{
    public const double MinimumHertz = 20.0;
    public const double MaximumHertz = 24_000.0;
    public const double BottomDecibel = -140.0;
    public const double TopDecibel = 0.0;

    public static readonly DependencyProperty PointsProperty = DependencyProperty.Register(
        nameof(Points),
        typeof(IReadOnlyList<SpectrumPoint>),
        typeof(SpectrumChart),
        new FrameworkPropertyMetadata(null, FrameworkPropertyMetadataOptions.AffectsRender));

    public static readonly DependencyProperty HarmonicsProperty = DependencyProperty.Register(
        nameof(Harmonics),
        typeof(IReadOnlyList<Harmonic>),
        typeof(SpectrumChart),
        new FrameworkPropertyMetadata(null, FrameworkPropertyMetadataOptions.AffectsRender));

    public static readonly DependencyProperty PeaksProperty = DependencyProperty.Register(
        nameof(Peaks),
        typeof(IReadOnlyList<SpectrumPeak>),
        typeof(SpectrumChart),
        new FrameworkPropertyMetadata(null, FrameworkPropertyMetadataOptions.AffectsRender));

    public static readonly DependencyProperty FundamentalHertzProperty = DependencyProperty.Register(
        nameof(FundamentalHertz),
        typeof(double?),
        typeof(SpectrumChart),
        new FrameworkPropertyMetadata(null, FrameworkPropertyMetadataOptions.AffectsRender));

    private const double LeftMargin = 40;
    private const double RightMargin = 8;
    private const double TopMargin = 8;
    private const double BottomMargin = 20;
    private const double GridStep = 20;
    private static readonly double[] GridHertz = [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000];
    private static readonly Typeface LabelFace = new("Segoe UI");

    // the colours, from the theme when the chart is drawn (LoadPalette)
    private Pen gridPen = new(Brushes.DimGray, 1);
    private Pen leftPen = new(Brushes.Gold, 1.5);
    private Pen rightPen = new(Brushes.DeepSkyBlue, 1.5);
    private Pen markPen = new(Brushes.Gray, 1);
    private Brush labelBrush = Brushes.Silver;
    private Brush markBrush = Brushes.White;
    private Brush readoutBackground = Brushes.Black;
    private Brush ground = Brushes.Black;

    private int? hoverIndex;

    public SpectrumChart()
    {
        Focusable = false;
    }

    /// <summary>The spectrum's points, in frequency order; null or empty draws the grid alone.</summary>
    public IReadOnlyList<SpectrumPoint>? Points
    {
        get => (IReadOnlyList<SpectrumPoint>?)GetValue(PointsProperty);
        set => SetValue(PointsProperty, value);
    }

    /// <summary>The harmonics, marked with their order.</summary>
    public IReadOnlyList<Harmonic>? Harmonics
    {
        get => (IReadOnlyList<Harmonic>?)GetValue(HarmonicsProperty);
        set => SetValue(HarmonicsProperty, value);
    }

    /// <summary>The Spectrum's strongest peaks, marked with their rank: the left channel's, and the right's where it lies elsewhere.</summary>
    public IReadOnlyList<SpectrumPeak>? Peaks
    {
        get => (IReadOnlyList<SpectrumPeak>?)GetValue(PeaksProperty);
        set => SetValue(PeaksProperty, value);
    }

    /// <summary>The fundamental, marked "1".</summary>
    public double? FundamentalHertz
    {
        get => (double?)GetValue(FundamentalHertzProperty);
        set => SetValue(FundamentalHertzProperty, value);
    }

    /// <summary>What the hover box shows, or null without one.</summary>
    internal string? Readout { get; private set; }

    internal double X(double hertz) =>
        LeftMargin + ((ActualWidth - LeftMargin - RightMargin)
                      * Math.Log10(Math.Clamp(hertz, MinimumHertz, MaximumHertz) / MinimumHertz)
                      / Math.Log10(MaximumHertz / MinimumHertz));

    internal double Y(double decibel) =>
        TopMargin + ((ActualHeight - TopMargin - BottomMargin)
                     * (TopDecibel - Math.Clamp(double.IsFinite(decibel) ? decibel : BottomDecibel, BottomDecibel, TopDecibel))
                     / (TopDecibel - BottomDecibel));

    internal void HoverAt(double x)
    {
        IReadOnlyList<SpectrumPoint>? points = Points;
        hoverIndex = points is { Count: > 0 }
            ? points.Select((point, index) => (Distance: Math.Abs(X(point.FrequencyHertz) - x), Index: index))
                .MinBy(candidate => candidate.Distance).Index
            : null;
        InvalidateVisual();
    }

    protected override Size MeasureOverride(Size availableSize) =>
        new(double.IsInfinity(availableSize.Width) ? 600 : availableSize.Width,
            double.IsInfinity(availableSize.Height) ? 200 : availableSize.Height);

    protected override HitTestResult HitTestCore(PointHitTestParameters hitTestParameters) =>
        new PointHitTestResult(this, hitTestParameters.HitPoint);

    protected override void OnMouseMove(MouseEventArgs eventArgs)
    {
        base.OnMouseMove(eventArgs);
        HoverAt(eventArgs.GetPosition(this).X);
    }

    protected override void OnMouseLeave(MouseEventArgs eventArgs)
    {
        base.OnMouseLeave(eventArgs);
        hoverIndex = null;
        InvalidateVisual();
    }

    protected override void OnRender(DrawingContext context)
    {
        LoadPalette();
        context.DrawRectangle(ground, null, new Rect(0, 0, ActualWidth, ActualHeight));
        double pixels = VisualTreeHelper.GetDpi(this).PixelsPerDip;
        IReadOnlyList<SpectrumPoint> points = Points ?? [];
        DrawGrid(context, pixels);

        Polyline(context, rightPen, [.. points.Select(point => new Point(X(point.FrequencyHertz), Y(point.RightDecibel)))]);
        Polyline(context, leftPen, [.. points.Select(point => new Point(X(point.FrequencyHertz), Y(point.LeftDecibel)))]);

        if (points.Count > 0 && FundamentalHertz is double fundamental)
        {
            Mark(context, pixels, points, fundamental, "1");
            foreach (Harmonic harmonic in Harmonics ?? [])
            {
                Mark(context, pixels, points, harmonic.FrequencyHertz, harmonic.Order.ToString(CultureInfo.CurrentCulture));
            }
        }

        if (points.Count > 0)
        {
            foreach (SpectrumPeak peak in Peaks ?? [])
            {
                string rank = peak.Rank.ToString(CultureInfo.CurrentCulture);
                if (peak.LeftHertz is double left)
                {
                    Mark(context, pixels, points, left, rank);
                }

                if (peak.RightHertz is double right && (peak.LeftHertz is not double other || Math.Abs(Math.Log(right / other)) > 0.02))
                {
                    Mark(context, pixels, points, right, rank);
                }
            }
        }

        DrawReadout(context, pixels, points);
    }

    // the order over the stronger channel's point nearest to the frequency
    private void Mark(DrawingContext context, double pixels, IReadOnlyList<SpectrumPoint> points, double hertz, string label)
    {
        SpectrumPoint nearest = points.MinBy(point => Math.Abs(Math.Log(point.FrequencyHertz / hertz)))!;
        double level = Math.Max(nearest.LeftDecibel, nearest.RightDecibel);
        double x = X(hertz);
        double y = Y(level);
        context.DrawLine(markPen, new Point(x, y - 3), new Point(x, y - 9));
        FormattedText text = Label(label, pixels, markBrush);
        context.DrawText(text, new Point(x - (text.Width / 2), Math.Max(0, y - 9 - text.Height)));
    }

    private void DrawGrid(DrawingContext context, double pixels)
    {
        foreach (double hertz in GridHertz)
        {
            double x = X(hertz);
            context.DrawLine(gridPen, new Point(x, TopMargin), new Point(x, ActualHeight - BottomMargin));
            string label = hertz >= 1000
                ? (hertz / 1000).ToString(CultureInfo.CurrentCulture) + "k"
                : hertz.ToString(CultureInfo.CurrentCulture);
            FormattedText text = Label(label, pixels, labelBrush);
            double left = Math.Clamp(x - (text.Width / 2), 0, ActualWidth - text.Width - 2);
            context.DrawText(text, new Point(left, ActualHeight - BottomMargin + 3));
        }

        for (double decibel = BottomDecibel; decibel <= TopDecibel + (GridStep / 2); decibel += GridStep)
        {
            double y = Y(decibel);
            context.DrawLine(gridPen, new Point(LeftMargin, y), new Point(ActualWidth - RightMargin, y));
            context.DrawText(Label(decibel.ToString("0", CultureInfo.CurrentCulture), pixels, labelBrush), new Point(4, y - 8));
        }
    }

    private void DrawReadout(DrawingContext context, double pixels, IReadOnlyList<SpectrumPoint> points)
    {
        if (hoverIndex is not int index || index >= points.Count)
        {
            Readout = null;
            return;
        }

        SpectrumPoint point = points[index];
        string frequency = point.FrequencyHertz.ToString("#,0.#", CultureInfo.CurrentCulture) + " Hz";
        string left = "L " + point.LeftDecibel.ToString("0.0", CultureInfo.CurrentCulture) + " dBFS";
        string right = "R " + point.RightDecibel.ToString("0.0", CultureInfo.CurrentCulture) + " dBFS";
        Readout = $"{frequency}  {left}  {right}";

        double x = X(point.FrequencyHertz);
        context.DrawLine(markPen, new Point(x, TopMargin), new Point(x, ActualHeight - BottomMargin));
        FormattedText first = Label(frequency, pixels, markBrush);
        FormattedText second = Label(left, pixels, leftPen.Brush);
        FormattedText third = Label(right, pixels, rightPen.Brush);
        double width = Math.Max(first.Width, second.Width + third.Width + 12) + 12;
        double boxLeft = x + width + 8 < ActualWidth - RightMargin ? x + 6 : x - width - 6;
        var box = new Rect(boxLeft, TopMargin + 4, width, 36);
        context.DrawRectangle(readoutBackground, markPen, box);
        context.DrawText(first, new Point(box.Left + 6, box.Top + 2));
        context.DrawText(second, new Point(box.Left + 6, box.Top + 18));
        context.DrawText(third, new Point(box.Left + 18 + second.Width, box.Top + 18));
    }

    private void LoadPalette()
    {
        ground = ThemeResources.Brush(this, "AudioLabChartBackground", Brushes.Black);
        gridPen = Frozen(new Pen(ThemeResources.Brush(this, "AudioLabChartGrid", Brushes.DimGray), 1));
        leftPen = Frozen(new Pen(ThemeResources.Brush(this, "AudioLabChartLeft", Brushes.Gold), 1.5));
        rightPen = Frozen(new Pen(ThemeResources.Brush(this, "AudioLabChartRight", Brushes.DeepSkyBlue), 1.5));
        markPen = Frozen(new Pen(ThemeResources.Brush(this, "AudioLabChartReference", Brushes.Gray), 1) { DashStyle = DashStyles.Dot });
        labelBrush = ThemeResources.Brush(this, "AudioLabChartLabel", Brushes.Silver);
        markBrush = ThemeResources.Brush(this, "AudioLabChartMarker", Brushes.White);
        readoutBackground = ThemeResources.Brush(this, "AudioLabChartReadout", Brushes.Black);
    }

    private static void Polyline(DrawingContext context, Pen pen, IReadOnlyList<Point> points)
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

    private static FormattedText Label(string text, double pixelsPerDip, Brush brush) =>
        new(text, CultureInfo.CurrentCulture, FlowDirection.LeftToRight, LabelFace, 11, brush, pixelsPerDip);

    private static T Frozen<T>(T freezable) where T : Freezable
    {
        freezable.Freeze();
        return freezable;
    }
}
