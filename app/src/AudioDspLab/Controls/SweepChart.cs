using System.Globalization;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using AudioDspLab.Themes;


namespace AudioDspLab.Controls;

/// <summary>
/// Draws the response analyzer's sweep on the equalizer curve's
/// logarithmic frequency axis, 20 Hz to 20 kHz: left in gold, right in
/// cyan, a dot at every point measured; either absolute in dBFS, with the
/// reference dashed in grey, or relative to the reference in dB. The level
/// axis follows the values on show. Hovering shows the nearest point.
/// </summary>
public sealed class SweepChart : FrameworkElement
{
    public const double MinimumHertz = 20.0;
    public const double MaximumHertz = 20_000.0;

    public static readonly DependencyProperty PointsProperty = DependencyProperty.Register(
        nameof(Points),
        typeof(IReadOnlyList<SweepPoint>),
        typeof(SweepChart),
        new FrameworkPropertyMetadata(null, FrameworkPropertyMetadataOptions.AffectsRender, ClearHover));

    public static readonly DependencyProperty ReferenceProperty = DependencyProperty.Register(
        nameof(Reference),
        typeof(IReadOnlyList<SweepPoint>),
        typeof(SweepChart),
        new FrameworkPropertyMetadata(null, FrameworkPropertyMetadataOptions.AffectsRender));

    public static readonly DependencyProperty ShowRelativeProperty = DependencyProperty.Register(
        nameof(ShowRelative),
        typeof(bool),
        typeof(SweepChart),
        new FrameworkPropertyMetadata(false, FrameworkPropertyMetadataOptions.AffectsRender));

    private const double LeftMargin = 40;
    private const double RightMargin = 8;
    private const double TopMargin = 8;
    private const double BottomMargin = 20;

    private static readonly double[] GridHertz = [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000];
    private static readonly double[] GridSteps = [1, 2, 5, 10, 20, 50];
    // the colours, from the theme when the chart is drawn (LoadPalette)
    private Pen GridPen = new(Brushes.DimGray, 1);
    private Pen ZeroPen = new(Brushes.Gray, 1);
    private Pen LeftPen = new(Brushes.Gold, 2);
    private Pen RightPen = new(Brushes.DeepSkyBlue, 2);
    private Pen ReferencePen = new(Brushes.Gray, 1);
    private Pen HoverPen = new(Brushes.Gray, 1);
    private Brush LabelBrush = Brushes.Silver;
    private Brush ReadoutBackground = Brushes.Black;
    private Brush ReadoutText = Brushes.White;
    private Brush Ground = Brushes.Black;
    private static readonly Typeface LabelFace = new("Segoe UI");

    private int? hoverIndex;

    public SweepChart()
    {
        // so the mouse finds the chart between its lines
        Focusable = false;
    }

    /// <summary>The sweep's points, in frequency order; null or empty draws the grid alone.</summary>
    public IReadOnlyList<SweepPoint>? Points
    {
        get => (IReadOnlyList<SweepPoint>?)GetValue(PointsProperty);
        set => SetValue(PointsProperty, value);
    }

    /// <summary>The reference, drawn dashed in absolute mode.</summary>
    public IReadOnlyList<SweepPoint>? Reference
    {
        get => (IReadOnlyList<SweepPoint>?)GetValue(ReferenceProperty);
        set => SetValue(ReferenceProperty, value);
    }

    /// <summary>True: the points relative to the reference; false: in dBFS.</summary>
    public bool ShowRelative
    {
        get => (bool)GetValue(ShowRelativeProperty);
        set => SetValue(ShowRelativeProperty, value);
    }

    /// <summary>The level axis as last drawn: bottom, top and grid step, in dB.</summary>
    internal (double Bottom, double Top, double Step) Scale { get; private set; } = (-30, 10, 10);

    /// <summary>The left channel's points as last drawn, for tests.</summary>
    internal IReadOnlyList<Point> DrawnLeft { get; private set; } = [];

    internal IReadOnlyList<Point> DrawnRight { get; private set; } = [];

    /// <summary>What the hover box shows, or null without one.</summary>
    internal string? Readout { get; private set; }

    internal double X(double hertz) =>
        LeftMargin + ((ActualWidth - LeftMargin - RightMargin)
                      * Math.Log10(Math.Clamp(hertz, MinimumHertz, MaximumHertz) / MinimumHertz) / 3.0);

    internal double Y(double decibel)
    {
        (double bottom, double top, _) = Scale;
        return TopMargin + ((ActualHeight - TopMargin - BottomMargin)
                            * (top - Math.Clamp(decibel, bottom, top)) / (top - bottom));
    }

    /// <summary>Shows the point nearest to a horizontal position, as the mouse does.</summary>
    internal void HoverAt(double x)
    {
        IReadOnlyList<SweepPoint>? points = Points;
        hoverIndex = points is { Count: > 0 }
            ? points.Select((point, index) => (Distance: Math.Abs(X(point.FrequencyHertz) - x), Index: index))
                .MinBy(candidate => candidate.Distance).Index
            : null;
        InvalidateVisual();
    }

    internal void EndHover()
    {
        hoverIndex = null;
        InvalidateVisual();
    }

    /// <summary>
    /// A level axis around the values on show: a step of 1, 2, 5, 10, 20 or
    /// 50 dB giving about four to eight lines, the ends on the grid.
    /// </summary>
    internal static (double Bottom, double Top, double Step) ScaleFor(IEnumerable<double> values, bool relative)
    {
        double[] all = [.. values.Where(double.IsFinite)];
        double low = all.Length > 0 ? all.Min() : relative ? -30 : -100;
        double high = all.Length > 0 ? all.Max() : relative ? 5 : 0;
        if (relative)
        {
            high = Math.Max(high, 0);                          // 0 dB stays in sight
        }

        double span = Math.Max(high - low, 2);
        double step = GridSteps.FirstOrDefault(candidate => span / candidate <= 7, GridSteps[^1]);
        double bottom = Math.Floor((low - (step * 0.25)) / step) * step;
        double top = Math.Ceiling((high + (step * 0.25)) / step) * step;
        return (bottom, top, step);
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
        EndHover();
    }

    protected override void OnRender(DrawingContext context)
    {
        LoadPalette();
        context.DrawRectangle(Ground, null, new Rect(0, 0, ActualWidth, ActualHeight));
        double pixels = VisualTreeHelper.GetDpi(this).PixelsPerDip;
        IReadOnlyList<SweepPoint> points = Points ?? [];
        IReadOnlyList<SweepPoint> reference = Reference ?? [];
        bool relative = ShowRelative;

        double? LeftOf(SweepPoint point) => relative ? point.LeftRelativeDecibel : point.LeftDecibel;
        double? RightOf(SweepPoint point) => relative ? point.RightRelativeDecibel : point.RightDecibel;

        IEnumerable<double> shown = points.SelectMany(point => new[] { LeftOf(point), RightOf(point) })
            .Where(value => value is not null).Select(value => value!.Value);
        if (!relative)
        {
            shown = shown.Concat(reference.SelectMany(point => new[] { point.LeftDecibel, point.RightDecibel }));
        }

        Scale = ScaleFor(shown, relative);
        DrawGrid(context, pixels, relative);

        if (!relative && reference.Count >= 2)
        {
            Polyline(context, ReferencePen, [.. reference.Select(point => new Point(X(point.FrequencyHertz), Y(point.LeftDecibel)))]);
            Polyline(context, ReferencePen, [.. reference.Select(point => new Point(X(point.FrequencyHertz), Y(point.RightDecibel)))]);
        }

        DrawnRight = Series(points, RightOf);
        DrawnLeft = Series(points, LeftOf);
        Polyline(context, RightPen, DrawnRight);
        Polyline(context, LeftPen, DrawnLeft);
        foreach (Point point in DrawnRight)
        {
            context.DrawEllipse(RightPen.Brush, null, point, 2.5, 2.5);
        }

        foreach (Point point in DrawnLeft)
        {
            context.DrawEllipse(LeftPen.Brush, null, point, 2.5, 2.5);
        }

        DrawReadout(context, pixels, points, relative);
    }

    private IReadOnlyList<Point> Series(
        IReadOnlyList<SweepPoint> points,
        Func<SweepPoint, double?> value) =>
        [.. points.Where(point => value(point) is not null)
            .Select(point => new Point(X(point.FrequencyHertz), Y(value(point)!.Value)))];

    private void DrawGrid(DrawingContext context, double pixels, bool relative)
    {
        foreach (double hertz in GridHertz)
        {
            double x = X(hertz);
            context.DrawLine(GridPen, new Point(x, TopMargin), new Point(x, ActualHeight - BottomMargin));
            string label = hertz >= 1000
                ? (hertz / 1000).ToString(CultureInfo.CurrentCulture) + "k"
                : hertz.ToString(CultureInfo.CurrentCulture);
            FormattedText text = Label(label, pixels, LabelBrush);
            double left = Math.Clamp(x - (text.Width / 2), 0, ActualWidth - text.Width - 2);
            context.DrawText(text, new Point(left, ActualHeight - BottomMargin + 3));
        }

        (double bottom, double top, double step) = Scale;
        for (double decibel = bottom; decibel <= top + (step / 2); decibel += step)
        {
            double y = Y(decibel);
            context.DrawLine(relative && decibel == 0 ? ZeroPen : GridPen,
                new Point(LeftMargin, y), new Point(ActualWidth - RightMargin, y));
            context.DrawText(Label(decibel.ToString("+0;-0;0", CultureInfo.CurrentCulture), pixels, LabelBrush),
                new Point(4, y - 8));
        }
    }

    private void DrawReadout(DrawingContext context, double pixels, IReadOnlyList<SweepPoint> points, bool relative)
    {
        if (hoverIndex is not int index || index >= points.Count)
        {
            Readout = null;
            return;
        }

        SweepPoint point = points[index];
        string unit = relative ? "dB" : "dBFS";
        string Format(double? value) =>
            value is double decibel ? decibel.ToString("0.00", CultureInfo.CurrentCulture) + " " + unit : "--";
        string frequency = point.FrequencyHertz.ToString("#,0.##", CultureInfo.CurrentCulture) + " Hz";
        string left = "L " + Format(relative ? point.LeftRelativeDecibel : point.LeftDecibel);
        string right = "R " + Format(relative ? point.RightRelativeDecibel : point.RightDecibel);
        Readout = $"{frequency}  {left}  {right}";

        double x = X(point.FrequencyHertz);
        context.DrawLine(HoverPen, new Point(x, TopMargin), new Point(x, ActualHeight - BottomMargin));
        FormattedText first = Label(frequency, pixels, ReadoutText);
        FormattedText second = Label(left, pixels, LeftPen.Brush);
        FormattedText third = Label(right, pixels, RightPen.Brush);
        double width = Math.Max(first.Width, second.Width + third.Width + 12) + 12;
        double boxLeft = x + width + 8 < ActualWidth - RightMargin ? x + 6 : x - width - 6;
        var box = new Rect(boxLeft, TopMargin + 4, width, 36);
        context.DrawRectangle(ReadoutBackground, HoverPen, box);
        context.DrawText(first, new Point(box.Left + 6, box.Top + 2));
        context.DrawText(second, new Point(box.Left + 6, box.Top + 18));
        context.DrawText(third, new Point(box.Left + 18 + second.Width, box.Top + 18));
    }

    private void LoadPalette()
    {
        Ground = ThemeResources.Brush(this, "AudioLabChartBackground", Brushes.Black);
        Brush grid = ThemeResources.Brush(this, "AudioLabChartGrid", Brushes.DimGray);
        Brush reference = ThemeResources.Brush(this, "AudioLabChartReference", Brushes.Gray);
        GridPen = Frozen(new Pen(grid, 1));
        ZeroPen = Frozen(new Pen(ThemeResources.Brush(this, "AudioLabChartZero", Brushes.Gray), 1));
        LeftPen = Frozen(new Pen(ThemeResources.Brush(this, "AudioLabChartLeft", Brushes.Gold), 2));
        RightPen = Frozen(new Pen(ThemeResources.Brush(this, "AudioLabChartRight", Brushes.DeepSkyBlue), 2));
        ReferencePen = Frozen(new Pen(reference, 1) { DashStyle = DashStyles.Dash });
        HoverPen = Frozen(new Pen(reference, 1) { DashStyle = DashStyles.Dot });
        LabelBrush = ThemeResources.Brush(this, "AudioLabChartLabel", Brushes.Silver);
        ReadoutBackground = ThemeResources.Brush(this, "AudioLabChartReadout", Brushes.Black);
        ReadoutText = ThemeResources.Brush(this, "AudioLabChartMarker", Brushes.White);
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

    private static void ClearHover(DependencyObject chart, DependencyPropertyChangedEventArgs _)
    {
        var sweepChart = (SweepChart)chart;
        if (sweepChart.hoverIndex is int index && index >= (sweepChart.Points?.Count ?? 0))
        {
            sweepChart.hoverIndex = null;
        }
    }

    private static FormattedText Label(string text, double pixelsPerDip, Brush brush) =>
        new(text, CultureInfo.CurrentCulture, FlowDirection.LeftToRight, LabelFace, 11, brush, pixelsPerDip);

    private static T Frozen<T>(T freezable) where T : Freezable
    {
        freezable.Freeze();
        return freezable;
    }
}
