using System.Globalization;
using System.Numerics;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace AudioDspLab.Receiver;

/// <summary>A band of the mask: from .. to (Hz re the carrier) at a gain (dB).</summary>
public sealed record MaskBand(double From, double To, double GainDb);

/// <summary>A PC filter design: bands with gains, stop band, transition width.</summary>
public sealed record MaskSpec(string Name, IReadOnlyList<MaskBand> Bands, double StopDb, double TransitionHz, string Notes = "")
{
    public DateTime Saved { get; init; } = DateTime.Now;
}

/// <summary>
/// Designs a complex FIR at 12 kHz to a mask of bands (ported from
/// DSP-Box-Lab): the ideal response (the gain inside each band, nothing
/// outside) as an exact sum of shifted sincs, times a Kaiser window -- the
/// firmware's own method, generalised to several bands of any gain. Band edges
/// come out at the -6 dB points of their gain step; the transition width and
/// the stop band set the length and the window, as Kaiser's formula says.
/// </summary>
public static class MaskDesign
{
    public static int Taps(double stopDb, double transitionHz) =>
        Math.Min(ChannelFilter.KaiserTaps(stopDb, transitionHz, ChannelFilter.SampleRate), ChannelFilter.MaxTaps);

    public static Complex[] Design(IReadOnlyList<MaskBand> bands, double stopDb, double transitionHz)
    {
        int n = Taps(stopDb, transitionHz);
        double beta = ChannelFilter.KaiserBeta(stopDb), i0b = ChannelFilter.BesselI0(beta), m = (n - 1) / 2.0;
        double fs = ChannelFilter.SampleRate;
        var h = new Complex[n];
        for (int k = 0; k < n; k++)
        {
            double t = k - m;
            Complex ideal = Complex.Zero;
            foreach (MaskBand b in bands)
            {
                double g = Math.Pow(10, b.GainDb / 20), f1 = Math.Min(b.From, b.To), f2 = Math.Max(b.From, b.To);
                ideal += t == 0
                    ? g * (f2 - f1) / fs
                    : g * (Complex.FromPolarCoordinates(1, 2 * Math.PI * f2 * t / fs) - Complex.FromPolarCoordinates(1, 2 * Math.PI * f1 * t / fs))
                      / new Complex(0, 2 * Math.PI * t);
            }

            double r = m > 0 ? t / m : 0;
            h[k] = ideal * (ChannelFilter.BesselI0(beta * Math.Sqrt(Math.Max(0, 1 - r * r))) / i0b);
        }

        return h;
    }

    /// <summary>Bands that overlap, or reach beyond the usable ±5500 Hz; null when the mask is fine.</summary>
    public static string? Check(IReadOnlyList<MaskBand> bands)
    {
        if (bands.Count == 0)
        {
            return "No band.";
        }

        foreach (MaskBand b in bands)
        {
            if (b.To <= b.From)
            {
                return $"Band {b.From:0} .. {b.To:0} Hz: 'to' must be above 'from'.";
            }

            if (b.From < -ChannelFilter.EdgeLimit || b.To > ChannelFilter.EdgeLimit)
            {
                return $"Band {b.From:0} .. {b.To:0} Hz reaches beyond ±{ChannelFilter.EdgeLimit:0} Hz.";
            }
        }

        MaskBand[] sorted = [.. bands.OrderBy(b => b.From)];
        for (int i = 1; i < sorted.Length; i++)
        {
            if (sorted[i].From < sorted[i - 1].To)
            {
                return $"Bands {sorted[i - 1].From:0} .. {sorted[i - 1].To:0} and {sorted[i].From:0} .. {sorted[i].To:0} Hz overlap.";
            }
        }

        return null;
    }
}

/// <summary>Coefficient files: "k,re,im" (as DSP-Box-Lab writes them), "re,im", or one real value per line.</summary>
public static class CoefficientFile
{
    public static Complex[] Read(string path)
    {
        var list = new List<Complex>();
        foreach (string raw in File.ReadAllLines(path))
        {
            string line = raw.Trim();
            if (line.Length == 0 || line.StartsWith('#') || char.IsLetter(line[0]))
            {
                continue;   // comment or header
            }

            double[] f = [.. line.Split([',', ';', '\t', ' '], StringSplitOptions.RemoveEmptyEntries)
                .Select(s => double.Parse(s, NumberStyles.Float, CultureInfo.InvariantCulture))];
            list.Add(f.Length switch
            {
                1 => new Complex(f[0], 0),
                2 => new Complex(f[0], f[1]),
                _ => new Complex(f[1], f[2]),
            });
        }

        if (list.Count is < 1 or > ChannelFilter.MaxTaps)
        {
            throw new FormatException($"{list.Count} coefficients; the receiver takes 1 .. {ChannelFilter.MaxTaps}.");
        }

        return [.. list];
    }

    public static void Write(string path, IReadOnlyList<Complex> h)
    {
        var text = new StringBuilder("k,re,im\n");
        for (int k = 0; k < h.Count; k++)
        {
            text.Append(FormattableString.Invariant($"{k},{h[k].Real:R},{h[k].Imaginary:R}\n"));
        }

        File.WriteAllText(path, text.ToString());
    }
}

/// <summary>Named items as JSON files, one per item, in a folder of %LocalAppData%\Audio-DSP-Lab.</summary>
public class NamedJsonStore<T>(string directory, Func<T, string> nameOf, Func<T, string, T> withName)
    where T : class
{
    public const string Extension = ".json";

    private static readonly JsonSerializerOptions Json = new()
    {
        WriteIndented = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingDefault,
    };

    public static string AppFolder(string folder) =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Audio-DSP-Lab", folder);

    public string Directory { get; } = directory;

    public static bool IsValidName(string? name) =>
        !string.IsNullOrWhiteSpace(name)
        && name.Length <= 80
        && name.Trim() == name
        && name.IndexOfAny(Path.GetInvalidFileNameChars()) < 0
        && !name.StartsWith('.');

    public IReadOnlyList<string> Names() =>
        System.IO.Directory.Exists(Directory)
            ? [.. System.IO.Directory.EnumerateFiles(Directory, "*" + Extension)
                .Select(Path.GetFileNameWithoutExtension).OfType<string>()
                .Order(StringComparer.CurrentCultureIgnoreCase)]
            : [];

    public void Save(T item)
    {
        string name = nameOf(item);
        if (!IsValidName(name))
        {
            throw new ArgumentException($"'{name}' is not a usable name.", nameof(item));
        }

        System.IO.Directory.CreateDirectory(Directory);
        File.WriteAllText(PathOf(name), JsonSerializer.Serialize(item, Json));
    }

    public T? Read(string name)
    {
        if (!IsValidName(name) || !File.Exists(PathOf(name)))
        {
            return null;
        }

        try
        {
            T? item = JsonSerializer.Deserialize<T>(File.ReadAllText(PathOf(name)), Json);
            return item is null ? null : withName(item, name);
        }
        catch (JsonException)
        {
            return null;
        }
    }

    public void Delete(string name)
    {
        if (IsValidName(name))
        {
            File.Delete(PathOf(name));
        }
    }

    private string PathOf(string name) => Path.Combine(Directory, name + Extension);
}

/// <summary>Mask designs as JSON files in %LocalAppData%\Audio-DSP-Lab\Filter designs.</summary>
public sealed class MaskSpecStore(string directory)
    : NamedJsonStore<MaskSpec>(directory, s => s.Name, (s, name) => s with { Name = name })
{
    public static string DefaultDirectory => AppFolder("Filter designs");
}
