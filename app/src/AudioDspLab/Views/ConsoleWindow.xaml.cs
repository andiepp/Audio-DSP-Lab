using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using AudioDspLab.ViewModels;

namespace AudioDspLab.Views;

/// <summary>
/// The protocol by hand; the log follows its last line, Up and Down recall
/// requests. The log is a read-only text box: its text can be selected and
/// copied.
/// </summary>
public partial class ConsoleWindow : Window
{
    private readonly ConsoleViewModel console;

    public ConsoleWindow(ConsoleViewModel console)
    {
        this.console = console ?? throw new ArgumentNullException(nameof(console));
        InitializeComponent();
        DataContext = console;
        Loaded += (_, _) => InputBox.Focus();
    }

    // the text box has taken the new text by now; follow it to its end
    private void Log_TextChanged(object sender, TextChangedEventArgs eventArgs) => Log.ScrollToEnd();

    private void InputBox_PreviewKeyDown(object sender, KeyEventArgs eventArgs)
    {
        if (eventArgs.Key is Key.Up or Key.Down)
        {
            console.Recall(eventArgs.Key == Key.Up ? -1 : 1);
            InputBox.CaretIndex = InputBox.Text.Length;
            eventArgs.Handled = true;
        }
    }

    private void Window_Closed(object? sender, EventArgs eventArgs)
    {
        console.Dispose();
    }
}
