using System.IO;
using System.Text;

namespace AudioDspLab.Settings;

/// <summary>A stored set: an application and its parameters' values, in protocol text.</summary>
public sealed record StoredSettings(string Name, string Application, IReadOnlyList<KeyValuePair<string, string>> Values);

/// <summary>
/// Stored settings as text files, one per set:
/// <c>application=generator</c> and then <c>name=value</c> per parameter.
/// </summary>
public sealed class SettingsStore(string directory)
{
    public const string Extension = ".txt";
    private const string ApplicationKey = "application";

    /// <summary>%LocalAppData%\Audio-DSP-Lab\Settings.</summary>
    public static string DefaultDirectory =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Audio-DSP-Lab", "Settings");

    public string Directory { get; } = directory;

    /// <summary>A name that makes a plain file name.</summary>
    public static bool IsValidName(string? name) =>
        !string.IsNullOrWhiteSpace(name)
        && name.Length <= 80
        && name.Trim() == name
        && name.IndexOfAny(Path.GetInvalidFileNameChars()) < 0
        && !name.StartsWith('.');

    public IReadOnlyList<string> Names()
    {
        if (!System.IO.Directory.Exists(Directory))
        {
            return [];
        }

        return [.. System.IO.Directory.EnumerateFiles(Directory, "*" + Extension)
            .Select(Path.GetFileNameWithoutExtension)
            .OfType<string>()
            .Order(StringComparer.CurrentCultureIgnoreCase)];
    }

    public bool Save(StoredSettings settings)
    {
        if (!IsValidName(settings.Name))
        {
            return false;
        }

        var text = new StringBuilder();
        text.Append("# Audio-DSP-Lab settings\n");
        text.Append(ApplicationKey).Append('=').Append(settings.Application).Append('\n');
        foreach ((string key, string value) in settings.Values)
        {
            text.Append(key).Append('=').Append(value).Append('\n');
        }

        try
        {
            System.IO.Directory.CreateDirectory(Directory);
            File.WriteAllText(PathOf(settings.Name), text.ToString(), new UTF8Encoding(false));
            return true;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return false;
        }
    }

    public StoredSettings? Read(string name)
    {
        if (!IsValidName(name) || !File.Exists(PathOf(name)))
        {
            return null;
        }

        string? application = null;
        var values = new List<KeyValuePair<string, string>>();
        foreach (string line in File.ReadAllLines(PathOf(name)))
        {
            if (line.StartsWith('#') || !line.Contains('='))
            {
                continue;
            }

            int split = line.IndexOf('=');
            string key = line[..split].Trim();
            string value = line[(split + 1)..].Trim();
            if (key == ApplicationKey)
            {
                application = value;
            }
            else if (key.Length > 0 && value.Length > 0)
            {
                values.Add(new(key, value));
            }
        }

        return application is null ? null : new StoredSettings(name, application, values);
    }

    public bool Delete(string name)
    {
        try
        {
            File.Delete(PathOf(name));
            return true;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return false;
        }
    }

    private string PathOf(string name) => Path.Combine(Directory, name + Extension);
}
