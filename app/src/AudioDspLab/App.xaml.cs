using System.Windows;
using AudioDspLab.ViewModels;
using AudioDspLab.Views;

namespace AudioDspLab;

public partial class App : Application
{
    /// <summary>Whether the start opens the main window; a test harness opens its own.</summary>
    public bool OpensMainWindow { get; set; } = true;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        if (!OpensMainWindow)
        {
            return;
        }

        // --simulator: connect to the demonstration board at once
        var viewModel = new MainViewModel();
        if (e.Args.Contains("--simulator", StringComparer.OrdinalIgnoreCase))
        {
            viewModel.SelectedPort = MainViewModel.SimulatorPort;
            _ = viewModel.ConnectAsync();
        }

        MainWindow = new MainWindow(viewModel);
        MainWindow.Show();
    }
}
