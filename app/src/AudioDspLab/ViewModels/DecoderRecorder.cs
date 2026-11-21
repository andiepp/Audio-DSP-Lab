using System.IO;
using System.Text;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using AudioDspLab.Receiver;

namespace AudioDspLab.ViewModels;

/// <summary>A telex message saved on its own: its title, when it ended, its file.</summary>
public sealed record DecoderMessage(string Title, DateTime Time, string Path)
{
    public string Text => $"{Time.ToLocalTime():HH:mm}  {Title}";
}

/// <summary>
/// Keeps what the decoders bring, as the SDR-IQ Receiver does: every RTTY
/// character goes into a daily log (%LocalAppData%\Audio-DSP-Lab\DWD\yyyy-MM-dd.txt,
/// each line with its UTC time); every message between "ZCZC" and "NNNN" is
/// also saved on its own in DWD\messages. Weather charts are saved as PNG in
/// %LocalAppData%\Audio-DSP-Lab\Fax.
/// </summary>
public sealed class DecoderRecorder
{
    public const int MinimumChartLines = 50;

    /// <summary>Where the logs, messages and charts go (the screenshot tool points it elsewhere).</summary>
    public static string Root { get; set; } =
        System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Audio-DSP-Lab");

    private readonly TelexMessages telex = new();
    private bool atLineStart = true;

    public DecoderRecorder()
    {
        telex.Message += Save;
    }

    public static string RttyFolder => System.IO.Path.Combine(Root, "DWD");

    public static string MessageFolder => System.IO.Path.Combine(RttyFolder, "messages");

    public static string FaxFolder => System.IO.Path.Combine(Root, "Fax");

    /// <summary>A message was saved.</summary>
    public event Action<DecoderMessage>? MessageSaved;

    /// <summary>The last file problem, for the status line (null: none).</summary>
    public string? Problem { get; private set; }

    /// <summary>Decoded text, and whether a line ends after it.</summary>
    public void Add(string text, bool lineEnd)
    {
        string piece = lineEnd ? text + "\n" : text;
        telex.Add(piece);
        try
        {
            Directory.CreateDirectory(RttyFolder);
            DateTime now = DateTime.UtcNow;
            var line = new StringBuilder();
            foreach (char c in piece)
            {
                if (atLineStart)
                {
                    line.Append(now.ToString("HH:mm:ss", System.Globalization.CultureInfo.InvariantCulture)).Append("Z  ");
                    atLineStart = false;
                }

                line.Append(c == '\n' ? Environment.NewLine : c.ToString());
                atLineStart = c == '\n';
            }

            File.AppendAllText(System.IO.Path.Combine(RttyFolder, now.ToString("yyyy-MM-dd", System.Globalization.CultureInfo.InvariantCulture) + ".txt"),
                line.ToString(), Encoding.UTF8);
            Problem = null;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            Problem = $"The RTTY log could not be written: {exception.Message}";
        }
    }

    /// <summary>The chart as a PNG in the fax folder; null when it is too short to keep or could not be written.</summary>
    public string? SaveChart(FaxChart chart)
    {
        if (chart.Height < MinimumChartLines || chart.Width == 0)
        {
            return null;
        }

        try
        {
            Directory.CreateDirectory(FaxFolder);
            string path = System.IO.Path.Combine(FaxFolder,
                chart.Started.ToLocalTime().ToString("yyyyMMdd-HHmmss", System.Globalization.CultureInfo.InvariantCulture) + ".png");
            var encoder = new PngBitmapEncoder();
            encoder.Frames.Add(BitmapFrame.Create(ToBitmap(chart)));
            using FileStream file = File.Create(path);
            encoder.Save(file);
            Problem = null;
            return path;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            Problem = $"The chart could not be saved: {exception.Message}";
            return null;
        }
    }

    public static BitmapSource ToBitmap(FaxChart chart)
    {
        BitmapSource bitmap = BitmapSource.Create(chart.Width, Math.Max(1, chart.Height), 96, 96, PixelFormats.Gray8, null,
            chart.Height == 0 ? new byte[chart.Width] : chart.ToGray8(), chart.Width);
        bitmap.Freeze();
        return bitmap;
    }

    private void Save(string text)
    {
        DateTime now = DateTime.UtcNow;
        string title = TelexMessages.Title(text);
        string safe = string.Concat(title.Select(c => System.IO.Path.GetInvalidFileNameChars().Contains(c) ? '_' : c)).Trim();
        try
        {
            Directory.CreateDirectory(MessageFolder);
            string path = System.IO.Path.Combine(MessageFolder,
                $"{now.ToString("yyyyMMdd-HHmmss", System.Globalization.CultureInfo.InvariantCulture)} {safe}.txt");
            File.WriteAllText(path, text.Replace("\n", Environment.NewLine, StringComparison.Ordinal), Encoding.UTF8);
            MessageSaved?.Invoke(new DecoderMessage(title, now, path));
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            Problem = $"The message could not be saved: {exception.Message}";
        }
    }
}
