// Renders the manual's screenshots from the simulated board, every page in
// every theme: Studio dark, the manual's, into docs/images/screenshots,
// Classic and Studio light into its classic and studio-light folders.
//   dotnet run --project app/tools/Screenshots [-- <folder>]
using System.IO;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using AudioDspLab;
using AudioDspLab.Settings;
using AudioDspLab.Simulation;
using AudioDspLab.Themes;
using AudioDspLab.ViewModels;
using AudioDspLab.Views;

static class Program
{
    [STAThread]
    static void Main(string[] args)
    {
        string root = args.Length > 0 ? args[0] : Path.Combine(RepositoryRoot(), "docs", "images", "screenshots");
        var app = new App();
        app.InitializeComponent();
        app.OpensMainWindow = false;
        app.ShutdownMode = ShutdownMode.OnExplicitShutdown;
        app.Startup += async (_, _) =>
        {
            try
            {
              foreach (string theme in ThemeResources.Names)
              {
                ThemeResources.Apply(app, theme);
                string output = theme == ThemeResources.StudioDark ? root : Path.Combine(root, theme.ToLowerInvariant().Replace(' ', '-'));
                Directory.CreateDirectory(output);
                Console.WriteLine($"--- {theme} -> {output}");
                SimulatedBoard? board = null;
                var vm = new MainViewModel(_ => board = new SimulatedBoard(demo: true), new SettingsStore(Path.Combine(Path.GetTempPath(), "Audio-DSP-Lab-screenshots")), new AppPreferences { Theme = theme });
                var window = new MainWindow(vm);
                window.Show();
                Save(window, Path.Combine(output, "disconnected.png"));
                vm.SelectedPort = MainViewModel.SimulatorPort;
                await vm.ConnectAsync();
                await Task.Delay(800);
                Save(window, Path.Combine(output, "sigmastudio.png"));
                Console.WriteLine($"connected: {vm.ConnectionText} | {vm.StatusInfo}");
                foreach (string name in new[] { "generator", "equalizer", "radio", "analyzer", "synthesizer" })
                {
                    await vm.SelectApplicationAsync(name);
                    await Task.Delay(1500);
                    if (name == "analyzer")
                    {
                        await vm.Analyzer.StartSweepAsync();
                        await Task.Delay(2500);
                    }
                    if (name == "generator")
                    {
                        await vm.SetPotentiometerControlAsync(true);
                        board!.SetPot(2, 1100);           // P3: the triangle's rise
                        await Task.Delay(300);
                    }
                    Save(window, Path.Combine(output, $"{name}.png"));
                    Console.WriteLine($"{name}: {vm.ApplicationStateText} | {vm.StatusInfo} | {window.ActualWidth}x{window.ActualHeight}");
                }

                window.Close();
              }
            }
            catch (Exception exception)
            {
                Console.WriteLine(exception);
            }
            finally
            {
                app.Shutdown();
            }
        };
        app.Run();
    }

    static string RepositoryRoot()
    {
        string? directory = AppContext.BaseDirectory;
        while (directory is not null && !Directory.Exists(Path.Combine(directory, "docs", "images")))
        {
            directory = Path.GetDirectoryName(directory);
        }

        return directory ?? ".";
    }

    static void Save(Window window, string path)
    {
        window.UpdateLayout();
        window.Dispatcher.Invoke(() => { }, System.Windows.Threading.DispatcherPriority.Render);
        var content = (FrameworkElement)window.Content;
        int width = (int)Math.Ceiling(content.ActualWidth);
        int height = (int)Math.Ceiling(content.ActualHeight);
        var bitmap = new RenderTargetBitmap(width, height, 96, 96, PixelFormats.Pbgra32);
        var ground = new DrawingVisual();
        using (DrawingContext context = ground.RenderOpen())
        {
            context.DrawRectangle(window.Background ?? Brushes.Black, null, new Rect(0, 0, width, height));
        }
        bitmap.Render(ground);
        bitmap.Render(content);
        var encoder = new PngBitmapEncoder();
        encoder.Frames.Add(BitmapFrame.Create(bitmap));
        using FileStream file = File.Create(path);
        encoder.Save(file);
    }
}
