using System.Collections.Specialized;
using System.Windows;
using System.Windows.Input;
using AudioDspLab.ViewModels;

namespace AudioDspLab.Views;

/// <summary>The protocol by hand; the log follows its last line, Up and Down recall requests.</summary>
public partial class ConsoleWindow : Window
{
    private readonly ConsoleViewModel console;

    public ConsoleWindow(ConsoleViewModel console)
    {
        this.console = console ?? throw new ArgumentNullException(nameof(console));
        InitializeComponent();
        DataContext = console;
        console.Lines.CollectionChanged += Lines_CollectionChanged;
        Loaded += (_, _) => InputBox.Focus();
    }

    private void Lines_CollectionChanged(object? sender, NotifyCollectionChangedEventArgs eventArgs)
    {
        if (console.Lines.Count > 0)
        {
            Log.ScrollIntoView(console.Lines[^1]);
        }
    }

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
        console.Lines.CollectionChanged -= Lines_CollectionChanged;
        console.Dispose();
    }
}
