using System.Windows;
using AudioDspLab.Themes;
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

        var viewModel = new MainViewModel();
        ThemeResources.Apply(this, viewModel.SelectedTheme);

        // --simulator: connect to the demonstration board at once
        if (e.Args.Contains("--simulator", StringComparer.OrdinalIgnoreCase))
        {
            viewModel.SelectedPort = MainViewModel.SimulatorPort;
            _ = viewModel.ConnectAsync();
        }

        MainWindow = new MainWindow(viewModel);
        MainWindow.Show();
    }

    /// <summary>
    /// Applies a theme and rebuilds the main window over the same view
    /// model: the connection, the values and the pages stay as they are.
    /// </summary>
    public void SwitchTheme(MainWindow current, string theme)
    {
        ThemeResources.Apply(this, theme);
        var next = new MainWindow(current.ViewModel)
        {
            WindowStartupLocation = WindowStartupLocation.Manual,
            Left = current.Left,
            Top = current.Top,
            Width = current.Width,
        };
        MainWindow = next;
        next.Show();
        next.WindowState = current.WindowState;
        current.CloseKeepingViewModel();
    }
}
