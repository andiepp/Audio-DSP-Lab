using System.Globalization;
using System.Windows;
using System.Windows.Media;
using AudioDspLab.Themes;

namespace AudioDspLab.Controls;

/// <summary>
/// A horizontal meter (ported from DSP-Box-Lab): a bar from
/// <see cref="Minimum"/> to the value, a scale with ticks every
/// <see cref="TickStep"/>, and an optional mark (the squelch threshold, for
/// the S-meter). A null value draws the scale alone.
/// </summary>
public sealed class LevelBar : FrameworkElement
{
    public static readonly DependencyProperty ValueProperty = DependencyProperty.Register(
        nameof(Value), typeof(double?), typeof(LevelBar), new FrameworkPropertyMetadata(null, FrameworkPropertyMetadataOptions.AffectsRender));

    public static readonly DependencyProperty MinimumProperty = Register(nameof(Minimum), -130.0);
    public static readonly DependencyProperty MaximumProperty = Register(nameof(Maximum), 0.0);
    public static readonly DependencyProperty TickStepProperty = Register(nameof(TickStep), 10.0);

    public static readonly DependencyProperty MarkProperty = DependencyProperty.Register(
        nameof(Mark), typeof(double?), typeof(LevelBar), new FrameworkPropertyMetadata(null, FrameworkPropertyMetadataOptions.AffectsRender));

    private const double BarHeight = 14;

    public double? Value
    {
        get => (double?)GetValue(ValueProperty);
        set => SetValue(ValueProperty, value);
    }

    public double Minimum
    {
        get => (double)GetValue(MinimumProperty);
        set => SetValue(MinimumProperty, value);
    }

    public double Maximum
    {
        get => (double)GetValue(MaximumProperty);
        set => SetValue(MaximumProperty, value);
    }

    public double TickStep
    {
        get => (double)GetValue(TickStepProperty);
        set => SetValue(TickStepProperty, value);
    }

    /// <summary>A threshold drawn across the bar, or null.</summary>
    public double? Mark
    {
        get => (double?)GetValue(MarkProperty);
        set => SetValue(MarkProperty, value);
    }

    protected override Size MeasureOverride(Size availableSize) =>
        new(double.IsInfinity(availableSize.Width) ? 300 : availableSize.Width, 30);

    protected override void OnRender(DrawingContext context)
    {
        double width = ActualWidth;
        if (width < 20 || Maximum <= Minimum)
        {
            return;
        }

        Brush track = ThemeResources.Brush(this, "AudioLabChartBackground", Brushes.Black);
        Brush line = ThemeResources.Brush(this, "AudioLabChartZero", Brushes.Gray);
        Brush accent = ThemeResources.Brush(this, "AudioLabChartLeft", Brushes.Gold);
        Brush caution = ThemeResources.Brush(this, "AudioLabWarningText", Brushes.Orange);
        Brush label = ThemeResources.Brush(this, "AudioLabChartLabel", Brushes.Silver);
        double X(double v) => Math.Clamp((v - Minimum) / (Maximum - Minimum), 0, 1) * width;

        context.DrawRoundedRectangle(track, new Pen(line, 1), new Rect(0.5, 0.5, width - 1, BarHeight), 4, 4);
        if (Value is double value && X(value) > 1)
        {
            context.DrawRoundedRectangle(accent, null, new Rect(1, 1, Math.Max(0, X(value) - 2), BarHeight - 1), 3, 3);
        }

        if (Mark is double mark && mark > Minimum)
        {
            context.DrawRectangle(caution, null, new Rect(X(mark) - 1, -1, 2, BarHeight + 2));
        }

        var pen = new Pen(line, 1);
        double pixels = VisualTreeHelper.GetDpi(this).PixelsPerDip;
        double step = TickStep > 0 ? TickStep : 10;
        for (double v = Math.Ceiling(Minimum / step) * step; v <= Maximum + 1e-9; v += step)
        {
            double x = Math.Round(X(v)) + 0.5;
            context.DrawLine(pen, new Point(x, BarHeight + 1), new Point(x, BarHeight + 4));
            var text = new FormattedText(v.ToString("0", CultureInfo.CurrentCulture), CultureInfo.CurrentCulture,
                FlowDirection.LeftToRight, new Typeface("Segoe UI"), 9, label, pixels);
            context.DrawText(text, new Point(Math.Clamp(x - (text.Width / 2), 0, width - text.Width), BarHeight + 4));
        }
    }

    private static DependencyProperty Register(string name, double fallback) =>
        DependencyProperty.Register(name, typeof(double), typeof(LevelBar),
            new FrameworkPropertyMetadata(fallback, FrameworkPropertyMetadataOptions.AffectsRender));
}
