using System.Windows;
using AudioDspLab.ViewModels;

namespace AudioDspLab.Views;

public partial class StationsWindow : Window
{
    private readonly StationEditorViewModel editor;

    public StationsWindow(StationEditorViewModel editor)
    {
        this.editor = editor ?? throw new ArgumentNullException(nameof(editor));
        InitializeComponent();
        DataContext = editor;
        editor.Confirm = question =>
            MessageBox.Show(this, question, Title, MessageBoxButton.YesNo, MessageBoxImage.Question) == MessageBoxResult.Yes;
    }

    private void Window_Loaded(object sender, RoutedEventArgs eventArgs) => _ = editor.LoadAsync();

    private void Close_Click(object sender, RoutedEventArgs eventArgs) => Close();
}
