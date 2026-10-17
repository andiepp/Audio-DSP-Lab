using System.Globalization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Shapes;

namespace AudioDspLab.Controls;

/// <summary>
/// A number shown digit by digit, each digit adjustable on its own, as the
/// RF-Lab application's frequency and amplitude entries are: the mouse
/// wheel over a digit, or a click on its upper or lower half, steps the
/// value by that digit's weight. A clicked digit stays selected for the
/// keyboard: Up and Down step it, Left and Right move the selection.
/// </summary>
/// <remarks>
/// Every value is a dependency property and <see cref="Value"/> binds two
/// way, so a step reaches the view model as a property write and the
/// authoritative value comes back the same way. A step is clamped to the
/// range rather than refused, and an unknown value cannot be stepped.
/// </remarks>
public partial class DigitDial : UserControl
{
    public static readonly DependencyProperty ValueProperty = DependencyProperty.Register(
        nameof(Value),
        typeof(double?),
        typeof(DigitDial),
        new FrameworkPropertyMetadata(
            null,
            FrameworkPropertyMetadataOptions.BindsTwoWayByDefault,
            (dial, _) => ((DigitDial)dial).ShowValue())
        {
            DefaultUpdateSourceTrigger = UpdateSourceTrigger.PropertyChanged
        });

    public static readonly DependencyProperty MinimumProperty = DependencyProperty.Register(
        nameof(Minimum), typeof(double), typeof(DigitDial), new PropertyMetadata(double.MinValue));

    public static readonly DependencyProperty MaximumProperty = DependencyProperty.Register(
        nameof(Maximum), typeof(double), typeof(DigitDial), new PropertyMetadata(double.MaxValue));

    public static readonly DependencyProperty IntegerDigitsProperty = DependencyProperty.Register(
        nameof(IntegerDigits), typeof(int), typeof(DigitDial), new PropertyMetadata(5, Rebuild));

    public static readonly DependencyProperty FractionDigitsProperty = DependencyProperty.Register(
        nameof(FractionDigits), typeof(int), typeof(DigitDial), new PropertyMetadata(2, Rebuild));

    public static readonly DependencyProperty ShowSignProperty = DependencyProperty.Register(
        nameof(ShowSign), typeof(bool), typeof(DigitDial), new PropertyMetadata(false, Rebuild));

    public static readonly DependencyProperty ShowPlusProperty = DependencyProperty.Register(
        nameof(ShowPlus), typeof(bool), typeof(DigitDial), new PropertyMetadata(false, (dial, _) => ((DigitDial)dial).ShowValue()));

    public static readonly DependencyProperty DigitFontSizeProperty = DependencyProperty.Register(
        nameof(DigitFontSize), typeof(double), typeof(DigitDial), new PropertyMetadata(56.0, Rebuild));

    public static readonly DependencyProperty SelectedExponentProperty = DependencyProperty.Register(
        nameof(SelectedExponent),
        typeof(int?),
        typeof(DigitDial),
        new PropertyMetadata(null, (dial, _) => ((DigitDial)dial).ShowSelection()));

    private static readonly Brush SelectionBrush = Brushes.Gold;
    private static readonly Brush ArrowBrush = Frozen(new SolidColorBrush(Color.FromArgb(0x90, 0xFF, 0xFF, 0xFF)));

    private readonly List<DigitCell> cells = [];
    private TextBlock? signText;

    public DigitDial()
    {
        InitializeComponent();
        BuildCells();
    }

    /// <summary>The number shown, or null while unknown.</summary>
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

    public int IntegerDigits
    {
        get => (int)GetValue(IntegerDigitsProperty);
        set => SetValue(IntegerDigitsProperty, value);
    }

    public int FractionDigits
    {
        get => (int)GetValue(FractionDigitsProperty);
        set => SetValue(FractionDigitsProperty, value);
    }

    /// <summary>Whether a cell before the digits shows the minus sign.</summary>
    public bool ShowSign
    {
        get => (bool)GetValue(ShowSignProperty);
        set => SetValue(ShowSignProperty, value);
    }

    /// <summary>Whether the sign cell shows a plus for a value above zero, as a gain does.</summary>
    public bool ShowPlus
    {
        get => (bool)GetValue(ShowPlusProperty);
        set => SetValue(ShowPlusProperty, value);
    }

    public double DigitFontSize
    {
        get => (double)GetValue(DigitFontSizeProperty);
        set => SetValue(DigitFontSizeProperty, value);
    }

    /// <summary>
    /// The power of ten of the digit the keyboard steps, or null when no
    /// digit was chosen.
    /// </summary>
    public int? SelectedExponent
    {
        get => (int?)GetValue(SelectedExponentProperty);
        set => SetValue(SelectedExponentProperty, value);
    }

    /// <summary>What the dial shows, separators included, for tests.</summary>
    internal string DisplayedText =>
        string.Concat(DigitsPanel.Children.OfType<FrameworkElement>().Select(TextOf));

    /// <summary>
    /// Steps the value by one unit of the digit with this power of ten,
    /// up for a positive direction, clamped to the range.
    /// </summary>
    internal void Step(int exponent, int direction)
    {
        if (Value is not double current || !IsEnabled || direction == 0)
        {
            return;
        }

        double stepped = Math.Round(
            current + Math.Sign(direction) * Math.Pow(10, exponent),
            FractionDigits,
            MidpointRounding.AwayFromZero);
        stepped = Math.Clamp(stepped, Minimum, Maximum);
        if (stepped != current)
        {
            SetCurrentValue(ValueProperty, stepped);
        }
    }

    protected override void OnPreviewKeyDown(KeyEventArgs eventArgs)
    {
        base.OnPreviewKeyDown(eventArgs);
        int exponent = SelectedExponent ?? 0;
        switch (eventArgs.Key)
        {
            case Key.Up:
                Step(exponent, +1);
                break;
            case Key.Down:
                Step(exponent, -1);
                break;
            case Key.Left:
                SelectedExponent = Math.Min(exponent + 1, IntegerDigits - 1);
                break;
            case Key.Right:
                SelectedExponent = Math.Max(exponent - 1, -FractionDigits);
                break;
            default:
                return;
        }

        eventArgs.Handled = true;
    }

    private static Brush Frozen(Brush brush)
    {
        brush.Freeze();
        return brush;
    }

    private static void Rebuild(DependencyObject dial, DependencyPropertyChangedEventArgs _) =>
        ((DigitDial)dial).BuildCells();

    private void BuildCells()
    {
        DigitsPanel.Children.Clear();
        cells.Clear();
        signText = null;

        if (ShowSign)
        {
            signText = DigitText();
            signText.Width = DigitFontSize * 0.42;
            signText.TextAlignment = TextAlignment.Right;
            DigitsPanel.Children.Add(signText);
        }

        for (int exponent = IntegerDigits - 1; exponent >= -FractionDigits; exponent--)
        {
            var cell = new DigitCell(this, exponent);
            cells.Add(cell);
            DigitsPanel.Children.Add(cell.Root);

            if (exponent > 0 && exponent % 3 == 0)
            {
                DigitsPanel.Children.Add(Separator("·", 0.45));
            }
            else if (exponent == 0 && FractionDigits > 0)
            {
                DigitsPanel.Children.Add(
                    Separator(CultureInfo.CurrentCulture.NumberFormat.NumberDecimalSeparator, 1.0));
            }
        }

        ShowValue();
        ShowSelection();
    }

    private void ShowValue()
    {
        if (Value is not double value)
        {
            foreach (DigitCell cell in cells)
            {
                cell.Show("-", leading: false);
            }

            if (signText is not null)
            {
                signText.Text = " ";
            }

            return;
        }

        double magnitude = Math.Round(Math.Abs(value), FractionDigits, MidpointRounding.AwayFromZero);
        string digits = magnitude
            .ToString("F" + FractionDigits.ToString(CultureInfo.InvariantCulture), CultureInfo.InvariantCulture)
            .Replace(".", string.Empty, StringComparison.Ordinal)
            .PadLeft(cells.Count, '0');
        digits = digits[^cells.Count..];

        bool leading = true;
        for (int index = 0; index < cells.Count; index++)
        {
            DigitCell cell = cells[index];
            leading &= digits[index] == '0' && cell.Exponent > 0;
            cell.Show(digits[index].ToString(), leading);
        }

        if (signText is not null)
        {
            signText.Text = magnitude == 0 ? " " : value < 0 ? "-" : ShowPlus ? "+" : " ";
        }
    }

    private void ShowSelection()
    {
        foreach (DigitCell cell in cells)
        {
            cell.Select(cell.Exponent == SelectedExponent);
        }
    }

    private TextBlock DigitText() =>
        new()
        {
            FontFamily = new FontFamily("Calibri"),
            FontSize = DigitFontSize,
            Foreground = Brushes.White,
            VerticalAlignment = VerticalAlignment.Center
        };

    private TextBlock Separator(string text, double opacity)
    {
        TextBlock separator = DigitText();
        separator.Text = text;
        separator.Opacity = opacity;
        return separator;
    }

    private static string TextOf(FrameworkElement element) =>
        element switch
        {
            TextBlock text => text.Text,
            Border { Child: Grid grid } => grid.Children.OfType<TextBlock>().First().Text,
            _ => string.Empty
        };

    /// <summary>One digit: its text, the arrows shown on hover, the selection bar.</summary>
    private sealed class DigitCell
    {
        private readonly DigitDial dial;
        private readonly TextBlock text;
        private readonly Path up;
        private readonly Path down;
        private readonly Rectangle bar;

        public DigitCell(DigitDial dial, int exponent)
        {
            this.dial = dial;
            Exponent = exponent;

            text = dial.DigitText();
            text.HorizontalAlignment = HorizontalAlignment.Center;
            up = Arrow(pointingUp: true);
            down = Arrow(pointingUp: false);
            bar = new Rectangle
            {
                Height = 3,
                Fill = SelectionBrush,
                VerticalAlignment = VerticalAlignment.Bottom,
                Visibility = Visibility.Hidden
            };

            var grid = new Grid();
            grid.Children.Add(text);
            grid.Children.Add(up);
            grid.Children.Add(down);
            grid.Children.Add(bar);

            Root = new Border
            {
                Child = grid,
                Background = Brushes.Transparent,
                Width = dial.DigitFontSize * 0.52,
                Cursor = Cursors.Hand,
                ToolTip = "Wheel or click the upper or lower half to step this digit"
            };
            Root.MouseEnter += (_, _) => ShowArrows(true);
            Root.MouseLeave += (_, _) => ShowArrows(false);
            Root.MouseWheel += OnWheel;
            Root.MouseLeftButtonDown += OnClick;
        }

        public int Exponent { get; }

        public Border Root { get; }

        public void Show(string digit, bool leading)
        {
            text.Text = digit;
            text.Opacity = leading ? 0.35 : 1.0;
        }

        public void Select(bool selected) =>
            bar.Visibility = selected ? Visibility.Visible : Visibility.Hidden;

        private void ShowArrows(bool visible)
        {
            Visibility visibility = visible && dial.IsEnabled ? Visibility.Visible : Visibility.Hidden;
            up.Visibility = visibility;
            down.Visibility = visibility;
        }

        private void OnWheel(object sender, MouseWheelEventArgs eventArgs)
        {
            dial.SelectedExponent = Exponent;
            dial.Step(Exponent, eventArgs.Delta > 0 ? +1 : -1);
            eventArgs.Handled = true;
        }

        private void OnClick(object sender, MouseButtonEventArgs eventArgs)
        {
            dial.Focus();
            dial.SelectedExponent = Exponent;
            bool upper = eventArgs.GetPosition(Root).Y < Root.ActualHeight / 2;
            dial.Step(Exponent, upper ? +1 : -1);
            eventArgs.Handled = true;
        }

        private static Path Arrow(bool pointingUp) =>
            new()
            {
                Data = Geometry.Parse(pointingUp ? "M 0,6 L 6,0 L 12,6 Z" : "M 0,0 L 6,6 L 12,0 Z"),
                Fill = ArrowBrush,
                HorizontalAlignment = HorizontalAlignment.Center,
                VerticalAlignment = pointingUp ? VerticalAlignment.Top : VerticalAlignment.Bottom,
                Margin = pointingUp ? new Thickness(0, 2, 0, 0) : new Thickness(0, 0, 0, 6),
                Visibility = Visibility.Hidden,
                IsHitTestVisible = false
            };
    }
}
