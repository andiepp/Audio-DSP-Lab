using System.Collections.ObjectModel;
using System.Windows;
using System.Windows.Media;

namespace AudioDspLab.Themes;

/// <summary>
/// The themes the application offers. Every theme defines the same keys
/// (Themes/Classic.xaml lists them all); switching a theme replaces the
/// application's dictionaries and rebuilds the main window, so controls
/// may read a theme value once, when they are built or drawn.
/// </summary>
public static class ThemeResources
{
    public const string Classic = "Classic";
    public const string StudioDark = "Studio dark";
    public const string StudioLight = "Studio light";

    public static IReadOnlyList<string> Names { get; } = [Classic, StudioDark, StudioLight];

    /// <summary>The name to use for a stored or unknown name.</summary>
    public static string Normalize(string? name) => Names.Contains(name) ? name! : Classic;

    /// <summary>Replaces the application's theme dictionaries.</summary>
    public static void Apply(Application application, string name)
    {
        ArgumentNullException.ThrowIfNull(application);
        name = Normalize(name);
        Collection<ResourceDictionary> merged = application.Resources.MergedDictionaries;
        merged.Clear();
        if (name == Classic)
        {
            merged.Add(Load("Classic.xaml"));
            return;
        }

        merged.Add(Load(name == StudioDark ? "StudioDark.xaml" : "StudioLight.xaml"));
        merged.Add(Load("StudioStyles.xaml"));
    }

    /// <summary>A theme brush, or the fallback when the key is missing (a designer, a test without themes).</summary>
    public static Brush Brush(FrameworkElement element, string key, Brush fallback) =>
        element.TryFindResource(key) as Brush ?? fallback;

    public static T Value<T>(FrameworkElement element, string key, T fallback) =>
        element.TryFindResource(key) is T value ? value : fallback;

    /// <summary>The colour of a pot's knob cap, by its name P1 to P4.</summary>
    public static Brush PotBrush(FrameworkElement element, string? pot, Brush fallback) =>
        pot is { Length: 2 } && pot[0] == 'P' && pot[1] is >= '1' and <= '4'
            ? Brush(element, "AudioLabPot" + pot[1], fallback)
            : fallback;

    private static ResourceDictionary Load(string file) =>
        new() { Source = new Uri($"pack://application:,,,/AudioDspLab;component/Themes/{file}", UriKind.Absolute) };
}
