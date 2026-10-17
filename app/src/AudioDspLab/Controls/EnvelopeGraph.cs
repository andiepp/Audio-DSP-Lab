using System.Globalization;
using System.Windows;
using System.Windows.Media;
using AudioDspLab.Themes;

namespace AudioDspLab.Controls;

/// <summary>
/// Draws the synthesizer's two envelopes against time: the loudness
/// envelope filled, the filter's contour dashed. Real time runs from the
/// key down until both have reached their sustain; a stretch of held
/// key follows, then the release at the same scale.
/// </summary>
public sealed class EnvelopeGraph : FrameworkElement
{
    public static readonly DependencyProperty EnvelopesProperty = DependencyProperty.Register(
        nameof(Envelopes),
        typeof(SynthEnvelopes),
        typeof(EnvelopeGraph),
        new FrameworkPropertyMetadata(null, FrameworkPropertyMetadataOptions.AffectsRender));

    private const double LeftMargin = 8;
    private const double RightMargin = 8;
    private const double TopMargin = 6;
    private const double BottomMargin = 18;
    private const double HeldShare = 0.2;               // of the width: the key held at the sustain
    private static readonly Typeface LabelFace = new("Segoe UI");

    public SynthEnvelopes? Envelopes
    {
        get => (SynthEnvelopes?)GetValue(EnvelopesProperty);
        set => SetValue(EnvelopesProperty, value);
    }

    // takes the room its row gives it: the dials beside it set the height
    protected override Size MeasureOverride(Size availableSize) => new(0, 0);

    protected override void OnRender(DrawingContext context)
    {
        double width = ActualWidth;
        double height = ActualHeight;
        Brush labelBrush = ThemeResources.Brush(this, "AudioLabChartLabel", Brushes.Silver);
        Brush loudnessBrush = ThemeResources.Brush(this, "AudioLabChartLeft", Brushes.Gold);
        Brush contourBrush = ThemeResources.Brush(this, "AudioLabChartRight", Brushes.DeepSkyBlue);
        var gridPen = Frozen(new Pen(ThemeResources.Brush(this, "AudioLabChartGrid", Brushes.DimGray), 1));
        var keyPen = Frozen(new Pen(ThemeResources.Brush(this, "AudioLabChartZero", Brushes.Gray), 1) { DashStyle = DashStyles.Dash });
        context.DrawRectangle(ThemeResources.Brush(this, "AudioLabChartBackground", Brushes.Black), null, new Rect(0, 0, width, height));
        double pixels = VisualTreeHelper.GetDpi(this).PixelsPerDip;

        double left = LeftMargin;
        double right = width - RightMargin;
        double top = TopMargin;
        double bottom = height - BottomMargin;
        if (right - left < 40 || bottom - top < 20)
        {
            return;
        }

        context.DrawLine(gridPen, new Point(left, top), new Point(right, top));
        context.DrawLine(gridPen, new Point(left, bottom), new Point(right, bottom));
        // the legend top right, where the releases have fallen
        FormattedText contourLabel = Label("contour", contourBrush, pixels);
        FormattedText loudnessLabel = Label("loudness", loudnessBrush, pixels);
        context.DrawText(contourLabel, new Point(right - contourLabel.Width, top + 1));
        context.DrawText(loudnessLabel, new Point(right - contourLabel.Width - loudnessLabel.Width - 8, top + 1));

        if (Envelopes is not { } envelopes)
        {
            return;
        }

        // the scale: held span and release at the same ms per pixel
        double span = right - left;
        double held = span * HeldShare;
        double scale = (span - held) / (envelopes.HeldSpan + envelopes.ReleaseSpan);
        double keyUpX = left + (envelopes.HeldSpan * scale) + held;
        context.DrawLine(keyPen, new Point(left + (envelopes.HeldSpan * scale), top), new Point(left + (envelopes.HeldSpan * scale), bottom));
        context.DrawLine(keyPen, new Point(keyUpX, top), new Point(keyUpX, bottom));
        context.DrawText(Label("key down", labelBrush, pixels), new Point(left, bottom + 2));
        FormattedText keyUp = Label("key up", labelBrush, pixels);
        context.DrawText(keyUp, new Point(Math.Min(keyUpX - (keyUp.Width / 2), right - keyUp.Width), bottom + 2));
        FormattedText total = Label(Time(envelopes.HeldSpan), labelBrush, pixels);
        double heldEnd = left + (envelopes.HeldSpan * scale);
        if (heldEnd - total.Width / 2 > left + 50)
        {
            context.DrawText(total, new Point(heldEnd - (total.Width / 2), bottom + 2));
        }

        FormattedText release = Label("+" + Time(envelopes.ReleaseSpan), labelBrush, pixels);
        context.DrawText(release, new Point(right - release.Width, bottom + 2));

        Point[] loudness = Curve(envelopes.Attack, envelopes.Decay, envelopes.Sustain, envelopes.Release, envelopes, scale, held, left, top, bottom, right);
        Point[] contour = Curve(envelopes.ContourAttack, envelopes.ContourDecay, envelopes.ContourSustain, envelopes.ContourDecay, envelopes, scale, held, left, top, bottom, right);

        Color fill = loudnessBrush is SolidColorBrush solid ? solid.Color : Colors.Gold;
        var fillBrush = Frozen(new SolidColorBrush(Color.FromArgb(0x38, fill.R, fill.G, fill.B)));
        context.DrawGeometry(fillBrush, Frozen(new Pen(loudnessBrush, 2)), Polyline(loudness, closeTo: bottom));
        context.DrawGeometry(null, Frozen(new Pen(contourBrush, 1.5) { DashStyle = DashStyles.Dash }), Polyline(contour, closeTo: null));
    }

    // one envelope, a point per pixel: held from the key down, then released
    private static Point[] Curve(double attack, double decay, double sustain, double releaseTime, SynthEnvelopes envelopes,
                                 double scale, double held, double left, double top, double bottom, double right)
    {
        double Y(double level) => bottom - ((bottom - top) * Math.Clamp(level, 0, 1));
        var points = new List<Point>();
        double heldEnd = left + (envelopes.HeldSpan * scale);
        for (double x = left; x <= heldEnd; x += 1)
        {
            points.Add(new Point(x, Y(SynthCurves.HeldLevel((x - left) / scale, attack, decay, sustain))));
        }

        double keyLevel = SynthCurves.HeldLevel(envelopes.HeldSpan, attack, decay, sustain);
        points.Add(new Point(heldEnd + held, Y(keyLevel)));
        for (double x = heldEnd + held + 1; x <= right; x += 1)
        {
            points.Add(new Point(x, Y(SynthCurves.ReleasedLevel((x - heldEnd - held) / scale, keyLevel, releaseTime))));
        }

        return [.. points];
    }

    private static StreamGeometry Polyline(Point[] points, double? closeTo)
    {
        var geometry = new StreamGeometry();
        using (StreamGeometryContext figure = geometry.Open())
        {
            figure.BeginFigure(points[0], isFilled: closeTo is not null, isClosed: false);
            figure.PolyLineTo([.. points.Skip(1)], isStroked: true, isSmoothJoin: true);
            if (closeTo is double floor)
            {
                figure.LineTo(new Point(points[^1].X, floor), isStroked: false, isSmoothJoin: false);
                figure.LineTo(new Point(points[0].X, floor), isStroked: false, isSmoothJoin: false);
            }
        }

        geometry.Freeze();
        return geometry;
    }

    private static string Time(double milliseconds) =>
        milliseconds >= 1000
            ? (milliseconds / 1000).ToString("0.##", CultureInfo.CurrentCulture) + " s"
            : milliseconds.ToString("0", CultureInfo.CurrentCulture) + " ms";

    private static FormattedText Label(string text, Brush brush, double pixelsPerDip) =>
        new(text, CultureInfo.CurrentCulture, FlowDirection.LeftToRight, LabelFace, 11, brush, pixelsPerDip);

    private static T Frozen<T>(T freezable) where T : Freezable
    {
        freezable.Freeze();
        return freezable;
    }
}
