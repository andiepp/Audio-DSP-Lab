using System.Globalization;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using AudioDspLab.Receiver;
using AudioDspLab.Themes;

namespace AudioDspLab.Controls;

/// <summary>
/// The receiver's channel filter over the baseband, -6 .. +6 kHz re the
/// carrier (ported from DSP-Box-Lab's ResponseChart): the predicted curve,
/// the measured points, the passband or a mask's bands, the stop band. When
/// editable, drag an edge to move it, the passband to move both, the dashed
/// stop-band line to change the attenuation; the change is handed to
/// <see cref="EdgesCommand"/> or <see cref="StopCommand"/> on release (the
/// chart shows the board's values otherwise). The readout follows the mouse.
/// </summary>
public sealed class FilterChart : FrameworkElement
{
    public static readonly DependencyProperty LowProperty = Number(nameof(Low), -3000.0);
    public static readonly DependencyProperty HighProperty = Number(nameof(High), 3000.0);
    public static readonly DependencyProperty StopDbProperty = Number(nameof(StopDb), 70.0);
    public static readonly DependencyProperty BottomDbProperty = Number(nameof(BottomDb), -120.0);
    public static readonly DependencyProperty PredictedProperty = Points(nameof(Predicted));
    public static readonly DependencyProperty MeasuredProperty = Points(nameof(Measured));

    public static readonly DependencyProperty ShowEdgesProperty = DependencyProperty.Register(
        nameof(ShowEdges), typeof(bool), typeof(FilterChart), new FrameworkPropertyMetadata(true, FrameworkPropertyMetadataOptions.AffectsRender));

    public static readonly DependencyProperty IsEditableProperty = DependencyProperty.Register(
        nameof(IsEditable), typeof(bool), typeof(FilterChart), new FrameworkPropertyMetadata(false, FrameworkPropertyMetadataOptions.AffectsRender));

    public static readonly DependencyProperty CompactProperty = DependencyProperty.Register(
        nameof(Compact), typeof(bool), typeof(FilterChart), new FrameworkPropertyMetadata(false, FrameworkPropertyMetadataOptions.AffectsRender));

    public static readonly DependencyProperty MaskProperty = DependencyProperty.Register(
        nameof(Mask), typeof(IReadOnlyList<MaskBand>), typeof(FilterChart), new FrameworkPropertyMetadata(null, FrameworkPropertyMetadataOptions.AffectsRender));

    public static readonly DependencyProperty PredictedLabelProperty = DependencyProperty.Register(
        nameof(PredictedLabel), typeof(string), typeof(FilterChart),
        new FrameworkPropertyMetadata("predicted (firmware design + down-converter)", FrameworkPropertyMetadataOptions.AffectsRender));

    public static readonly DependencyProperty EdgesCommandProperty = DependencyProperty.Register(
        nameof(EdgesCommand), typeof(ICommand), typeof(FilterChart));

    public static readonly DependencyProperty StopCommandProperty = DependencyProperty.Register(
        nameof(StopCommand), typeof(ICommand), typeof(FilterChart));

    private const double FMin = -6000, FMax = 6000, EdgeSnap = 10, Grab = 6, Gap = 50;
    private Drag drag;
    private double dragStartF, dragStartLow, dragStartHigh;
    private double? dragLow, dragHigh, dragStop;
    private Point? mouse;

    private enum Drag
    {
        None,
        Low,
        High,
        Band,
        Stop,
    }

    public FilterChart()
    {
        Focusable = true;
        ClipToBounds = true;
    }

    public double Low
    {
        get => (double)GetValue(LowProperty);
        set => SetValue(LowProperty, value);
    }

    public double High
    {
        get => (double)GetValue(HighProperty);
        set => SetValue(HighProperty, value);
    }

    public double StopDb
    {
        get => (double)GetValue(StopDbProperty);
        set => SetValue(StopDbProperty, value);
    }

    public double BottomDb
    {
        get => (double)GetValue(BottomDbProperty);
        set => SetValue(BottomDbProperty, value);
    }

    /// <summary>The predicted response, Hz re the carrier and dB.</summary>
    public IReadOnlyList<FilterPoint>? Predicted
    {
        get => (IReadOnlyList<FilterPoint>?)GetValue(PredictedProperty);
        set => SetValue(PredictedProperty, value);
    }

    /// <summary>The measured response, drawn as dots on a thin line.</summary>
    public IReadOnlyList<FilterPoint>? Measured
    {
        get => (IReadOnlyList<FilterPoint>?)GetValue(MeasuredProperty);
        set => SetValue(MeasuredProperty, value);
    }

    /// <summary>The passband and its edge lines (the Filter view); false for the Designer.</summary>
    public bool ShowEdges
    {
        get => (bool)GetValue(ShowEdgesProperty);
        set => SetValue(ShowEdgesProperty, value);
    }

    /// <summary>Edges and stop band can be dragged.</summary>
    public bool IsEditable
    {
        get => (bool)GetValue(IsEditableProperty);
        set => SetValue(IsEditableProperty, value);
    }

    /// <summary>A small preview: no legend, no axis title, fewer labels.</summary>
    public bool Compact
    {
        get => (bool)GetValue(CompactProperty);
        set => SetValue(CompactProperty, value);
    }

    /// <summary>A mask of bands to draw (the Designer), each at its gain.</summary>
    public IReadOnlyList<MaskBand>? Mask
    {
        get => (IReadOnlyList<MaskBand>?)GetValue(MaskProperty);
        set => SetValue(MaskProperty, value);
    }

    public string PredictedLabel
    {
        get => (string)GetValue(PredictedLabelProperty);
        set => SetValue(PredictedLabelProperty, value);
    }

    /// <summary>Takes a <see cref="FilterEdges"/> when a drag of the edges ends.</summary>
    public ICommand? EdgesCommand
    {
        get => (ICommand?)GetValue(EdgesCommandProperty);
        set => SetValue(EdgesCommandProperty, value);
    }

    /// <summary>Takes the stop band in dB (a double) when a drag of its line ends.</summary>
    public ICommand? StopCommand
    {
        get => (ICommand?)GetValue(StopCommandProperty);
        set => SetValue(StopCommandProperty, value);
    }

    private double ShownLow => dragLow ?? Low;

    private double ShownHigh => dragHigh ?? High;

    private double ShownStop => dragStop ?? StopDb;

    private Rect Plot =>
        Compact
            ? new(30, 4, Math.Max(10, ActualWidth - 36), Math.Max(10, ActualHeight - 20))
            : new(42, 8, Math.Max(10, ActualWidth - 54), Math.Max(10, ActualHeight - 38));

    private double X(double f) => Plot.Left + ((f - FMin) / (FMax - FMin) * Plot.Width);

    private double Y(double db) => Plot.Top + (Math.Clamp(db / BottomDb, 0, 1.02) * Plot.Height);

    private double F(double x) => FMin + ((x - Plot.Left) / Plot.Width * (FMax - FMin));

    private double Db(double y) => (y - Plot.Top) / Plot.Height * BottomDb;

    protected override Size MeasureOverride(Size availableSize) =>
        new(double.IsInfinity(availableSize.Width) ? 600 : availableSize.Width,
            double.IsInfinity(availableSize.Height) ? 240 : availableSize.Height);

    protected override void OnRender(DrawingContext context)
    {
        Rect p = Plot;
        Brush background = ThemeResources.Brush(this, "AudioLabChartBackground", Brushes.Black);
        Brush grid = ThemeResources.Brush(this, "AudioLabChartGrid", Brushes.DimGray);
        Brush zero = ThemeResources.Brush(this, "AudioLabChartZero", Brushes.Gray);
        Brush label = ThemeResources.Brush(this, "AudioLabChartLabel", Brushes.Silver);
        Brush accent = ThemeResources.Brush(this, "AudioLabChartLeft", Brushes.Gold);
        Brush measured = ThemeResources.Brush(this, "AudioLabChartRight", Brushes.DeepSkyBlue);
        Brush caution = ThemeResources.Brush(this, "AudioLabWarningText", Brushes.Orange);
        Brush text = ThemeResources.Brush(this, "AudioLabChartMarker", Brushes.White);
        double pixels = VisualTreeHelper.GetDpi(this).PixelsPerDip;

        context.DrawRectangle(background, null, new Rect(RenderSize));

        // the passband, or the mask's bands
        Brush band = accent.CloneCurrentValue();
        band.Opacity = 0.14;
        if (ShowEdges)
        {
            context.DrawRectangle(band, null, new Rect(new Point(X(ShownLow), p.Top), new Point(X(ShownHigh), p.Bottom)));
        }

        foreach (MaskBand b in Mask ?? [])
        {
            double top = Y(Math.Min(0, b.GainDb));
            context.DrawRectangle(band, null, new Rect(new Point(X(b.From), top), new Point(X(b.To), p.Bottom)));
            context.DrawLine(new Pen(caution, 2), new Point(X(b.From), top), new Point(X(b.To), top));
        }

        // the grid
        var gridPen = new Pen(grid, 1);
        var zeroPen = new Pen(zero, 1);
        for (double f = FMin; f <= FMax; f += 1000)
        {
            double x = Math.Round(X(f)) + 0.5;
            context.DrawLine(f == 0 ? zeroPen : gridPen, new Point(x, p.Top), new Point(x, p.Bottom));
            if (!Compact || f % 2000 == 0)
            {
                Label(context, f == 0 ? "0" : string.Create(CultureInfo.CurrentCulture, $"{f / 1000:+0;-0}k"), x, p.Bottom + 2, label, pixels, center: true);
            }
        }

        double dbStep = Compact ? 20 : 10;
        for (double db = 0; db >= BottomDb; db -= dbStep)
        {
            double y = Math.Round(Y(db)) + 0.5;
            context.DrawLine(db == 0 ? zeroPen : gridPen, new Point(p.Left, y), new Point(p.Right, y));
            Label(context, db.ToString("0", CultureInfo.CurrentCulture), p.Left - 4, y - 7, label, pixels, center: false, right: true);
        }

        if (!Compact)
        {
            Label(context, "Hz re the carrier (baseband)", p.Right, p.Bottom + 16, label, pixels, center: false, right: true);
        }

        context.PushClip(new RectangleGeometry(p));

        // the stop band
        var stopPen = new Pen(caution, 1.2) { DashStyle = new DashStyle([5, 4], 0) };
        context.DrawLine(stopPen, new Point(p.Left, Y(-ShownStop)), new Point(p.Right, Y(-ShownStop)));
        if (!Compact)
        {
            Label(context, string.Create(CultureInfo.CurrentCulture, $"stop band {ShownStop:0} dB"), p.Right - 6, Y(-ShownStop) - 15, caution, pixels,
                center: false, right: true);
        }

        // the edges, with handles when they can be dragged
        var edgePen = new Pen(accent, 1.5);
        foreach (double edge in ShowEdges ? new[] { ShownLow, ShownHigh } : [])
        {
            double x = X(edge);
            context.DrawLine(edgePen, new Point(x, p.Top), new Point(x, p.Bottom));
            if (IsEditable)
            {
                context.DrawRoundedRectangle(accent, null, new Rect(x - 4, p.Top + 2, 8, 18), 3, 3);
            }
        }

        Curve(context, Predicted, new Pen(accent, Compact ? 1.5 : 2));
        if (Measured is { Count: > 0 } points)
        {
            Curve(context, points, new Pen(measured, 1.2));
            foreach (FilterPoint point in points)
            {
                context.DrawEllipse(measured, null, new Point(X(point.Hz), Y(point.GainDb)), 2.2, 2.2);
            }
        }

        context.Pop();

        if (Compact)
        {
            return;
        }

        // the legend and the readout
        double lx = p.Left + 10, ly = p.Top + 6;
        context.DrawLine(new Pen(accent, 2), new Point(lx, ly + 7), new Point(lx + 18, ly + 7));
        Label(context, PredictedLabel, lx + 24, ly, text, pixels, center: false);
        if (Measured is { Count: > 0 })
        {
            context.DrawEllipse(measured, null, new Point(lx + 9, ly + 25), 3, 3);
            Label(context, "measured on the board", lx + 24, ly + 18, text, pixels, center: false);
        }

        if (mouse is { } at && p.Contains(at))
        {
            double f = F(at.X);
            string line = string.Create(CultureInfo.CurrentCulture, $"{f:0} Hz");
            if (Nearest(Predicted, f) is double predicted)
            {
                line += string.Create(CultureInfo.CurrentCulture, $"   predicted {Math.Round(predicted, 1) + 0.0:0.0} dB");
            }

            if (Nearest(Measured, f, 60) is double measuredDb)
            {
                line += string.Create(CultureInfo.CurrentCulture, $"   measured {Math.Round(measuredDb, 1) + 0.0:0.0} dB");
            }

            FormattedText readout = Text(line, text, pixels, 11);
            var box = new Rect(p.Right - readout.Width - 18, p.Top + 6, readout.Width + 12, readout.Height + 6);
            context.DrawRoundedRectangle(background, new Pen(grid, 1), box, 4, 4);
            context.DrawText(readout, new Point(box.Left + 6, box.Top + 3));
            context.DrawLine(new Pen(zero, 1) { DashStyle = DashStyles.Dot }, new Point(at.X, p.Top), new Point(at.X, p.Bottom));
        }
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        base.OnMouseMove(e);
        Point at = e.GetPosition(this);
        mouse = at;
        if (drag != Drag.None)
        {
            double f = F(at.X);
            switch (drag)
            {
                case Drag.Low:
                    dragLow = Math.Clamp(Snap(f), -ChannelFilter.EdgeLimit, ShownHigh - Gap);
                    break;
                case Drag.High:
                    dragHigh = Math.Clamp(Snap(f), ShownLow + Gap, ChannelFilter.EdgeLimit);
                    break;
                case Drag.Band:
                    double shift = Math.Clamp(Snap(f - dragStartF), -ChannelFilter.EdgeLimit - dragStartLow, ChannelFilter.EdgeLimit - dragStartHigh);
                    dragLow = dragStartLow + shift;
                    dragHigh = dragStartHigh + shift;
                    break;
                case Drag.Stop:
                    dragStop = Math.Clamp(Math.Round(-Db(at.Y)), 40, 100);
                    break;
            }
        }
        else
        {
            Cursor = HitTest(at) switch
            {
                Drag.Low or Drag.High => Cursors.SizeWE,
                Drag.Stop => Cursors.SizeNS,
                Drag.Band => Cursors.Hand,
                _ => null,
            };
        }

        InvalidateVisual();
    }

    protected override void OnMouseLeftButtonDown(MouseButtonEventArgs e)
    {
        base.OnMouseLeftButtonDown(e);
        Point at = e.GetPosition(this);
        drag = HitTest(at);
        if (drag != Drag.None)
        {
            dragStartF = F(at.X);
            dragStartLow = Low;
            dragStartHigh = High;
            CaptureMouse();
            Focus();
            e.Handled = true;
        }
    }

    protected override void OnMouseLeftButtonUp(MouseButtonEventArgs e)
    {
        base.OnMouseLeftButtonUp(e);
        Drag ended = drag;
        drag = Drag.None;
        ReleaseMouseCapture();
        if (ended is Drag.Low or Drag.High or Drag.Band && (dragLow ?? Low) is double low && (dragHigh ?? High) is double high
            && (low != Low || high != High))
        {
            var edges = new FilterEdges(low, high);
            if (EdgesCommand?.CanExecute(edges) == true)
            {
                EdgesCommand.Execute(edges);
            }
        }
        else if (ended == Drag.Stop && dragStop is double stop && stop != StopDb && StopCommand?.CanExecute(stop) == true)
        {
            StopCommand.Execute(stop);
        }

        dragLow = dragHigh = dragStop = null;
        InvalidateVisual();
    }

    protected override void OnMouseLeave(MouseEventArgs e)
    {
        base.OnMouseLeave(e);
        mouse = null;
        InvalidateVisual();
    }

    private static double Snap(double f) => Math.Round(f / EdgeSnap) * EdgeSnap;

    private Drag HitTest(Point at)
    {
        Rect p = Plot;
        if (!IsEditable || !IsEnabled || !p.Contains(at))
        {
            return Drag.None;
        }

        if (ShowEdges && Math.Abs(at.X - X(Low)) <= Grab)
        {
            return Drag.Low;
        }

        if (ShowEdges && Math.Abs(at.X - X(High)) <= Grab)
        {
            return Drag.High;
        }

        if (StopCommand is not null && Math.Abs(at.Y - Y(-StopDb)) <= Grab)
        {
            return Drag.Stop;
        }

        return ShowEdges && at.X > X(Low) && at.X < X(High) ? Drag.Band : Drag.None;
    }

    private void Curve(DrawingContext context, IReadOnlyList<FilterPoint>? points, Pen pen)
    {
        if (points is not { Count: > 1 })
        {
            return;
        }

        var geometry = new StreamGeometry();
        using (StreamGeometryContext figure = geometry.Open())
        {
            figure.BeginFigure(new Point(X(points[0].Hz), Y(points[0].GainDb)), false, false);
            figure.PolyLineTo([.. points.Skip(1).Select(q => new Point(X(q.Hz), Y(q.GainDb)))], true, true);
        }

        geometry.Freeze();
        context.DrawGeometry(null, pen, geometry);
    }

    private static double? Nearest(IReadOnlyList<FilterPoint>? points, double f, double within = double.MaxValue)
    {
        if (points is not { Count: > 0 })
        {
            return null;
        }

        FilterPoint best = points.MinBy(q => Math.Abs(q.Hz - f));
        return Math.Abs(best.Hz - f) <= within ? best.GainDb : null;
    }

    private static FormattedText Text(string s, Brush brush, double pixels, double size = 10) =>
        new(s, CultureInfo.CurrentCulture, FlowDirection.LeftToRight, new Typeface("Segoe UI"), size, brush, pixels);

    private void Label(DrawingContext context, string s, double x, double y, Brush brush, double pixels, bool center, bool right = false)
    {
        FormattedText t = Text(s, brush, pixels);
        double left = center ? x - (t.Width / 2) : right ? x - t.Width : x;
        context.DrawText(t, new Point(Math.Max(0, Math.Min(left, ActualWidth - t.Width)), y));
    }

    private static DependencyProperty Number(string name, double fallback) =>
        DependencyProperty.Register(name, typeof(double), typeof(FilterChart),
            new FrameworkPropertyMetadata(fallback, FrameworkPropertyMetadataOptions.AffectsRender));

    private static DependencyProperty Points(string name) =>
        DependencyProperty.Register(name, typeof(IReadOnlyList<FilterPoint>), typeof(FilterChart),
            new FrameworkPropertyMetadata(null, FrameworkPropertyMetadataOptions.AffectsRender));
}
