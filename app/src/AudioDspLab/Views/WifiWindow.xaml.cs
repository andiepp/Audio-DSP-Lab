using System.Windows;
using System.Windows.Controls;
using AudioDspLab.ViewModels;

namespace AudioDspLab.Views;

/// <summary>
/// The WiFi networks. A PasswordBox cannot be bound, so the code-behind
/// hands each box's password to its row and empties the box after a save.
/// </summary>
public partial class WifiWindow : Window
{
    private readonly WifiEditorViewModel editor;

    public WifiWindow(WifiEditorViewModel editor)
    {
        this.editor = editor ?? throw new ArgumentNullException(nameof(editor));
        InitializeComponent();
        DataContext = editor;
        editor.Confirm = question =>
            MessageBox.Show(this, question, Title, MessageBoxButton.YesNo, MessageBoxImage.Question) == MessageBoxResult.Yes;
    }

    private void Window_Loaded(object sender, RoutedEventArgs eventArgs) => _ = editor.LoadAsync();

    private void PasswordBox_Loaded(object sender, RoutedEventArgs eventArgs)
    {
        if (sender is PasswordBox { DataContext: WifiRow row } box)
        {
            row.PasswordCleared += () => box.Clear();
        }
    }

    private void PasswordBox_PasswordChanged(object sender, RoutedEventArgs eventArgs)
    {
        if (sender is PasswordBox { DataContext: WifiRow row } box)
        {
            row.Password = box.Password;
        }
    }

    private void Close_Click(object sender, RoutedEventArgs eventArgs) => Close();
}
