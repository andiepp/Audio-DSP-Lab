using System.IO;
using System.ComponentModel;
using System.Diagnostics;
using System.Windows;
using System.Windows.Input;
using AudioDspLab.ViewModels;

namespace AudioDspLab.Views;

/// <summary>
/// The main window. Everything on it is bound; the code-behind opens and
/// closes the view model's life, asks confirmations and opens the dialogs.
/// </summary>
public partial class MainWindow : Window
{
    public const string ManualUrl = "https://github.com/andiepp/Audio-DSP-Lab/blob/main/docs/Manual.md";

    private readonly MainViewModel viewModel;

    public MainWindow()
        : this(new MainViewModel())
    {
    }

    public MainWindow(MainViewModel viewModel)
    {
        this.viewModel = viewModel ?? throw new ArgumentNullException(nameof(viewModel));
        InitializeComponent();
        DataContext = viewModel;
        viewModel.Confirm = question =>
            MessageBox.Show(this, question, "Audio-DSP-Lab", MessageBoxButton.YesNo, MessageBoxImage.Question) == MessageBoxResult.Yes;
    }

    private void Window_Loaded(object sender, RoutedEventArgs eventArgs) => _ = viewModel.ConnectToLastPortAsync();

    private void Window_Closing(object sender, CancelEventArgs eventArgs) => viewModel.Dispose();

    private void SettingsList_MouseDoubleClick(object sender, MouseButtonEventArgs eventArgs)
    {
        if (viewModel.ApplySettingsCommand.CanExecute(null))
        {
            viewModel.ApplySettingsCommand.Execute(null);
        }
    }

    private async void EditStations_Click(object sender, RoutedEventArgs eventArgs)
    {
        if (viewModel.Client is not { } client)
        {
            return;
        }

        var editor = new StationEditorViewModel(client);
        var window = new StationsWindow(editor) { Owner = this };
        window.ShowDialog();
        await viewModel.ReloadStationsAsync();
    }

    private void Wifi_Click(object sender, RoutedEventArgs eventArgs)
    {
        if (viewModel.Client is { } client)
        {
            new WifiWindow(new WifiEditorViewModel(client)) { Owner = this }.ShowDialog();
        }
    }

    private void Console_Click(object sender, RoutedEventArgs eventArgs)
    {
        if (viewModel.Client is { } client)
        {
            new ConsoleWindow(new ConsoleViewModel(client, viewModel.RefreshFromBoardAsync)) { Owner = this }.Show();
        }
    }

    private void Manual_Click(object sender, RoutedEventArgs eventArgs)
    {
        string local = Path.Combine(AppContext.BaseDirectory, "docs", "Manual.pdf");
        string target = File.Exists(local) ? local : ManualUrl;
        Process.Start(new ProcessStartInfo(target) { UseShellExecute = true });
    }
}
