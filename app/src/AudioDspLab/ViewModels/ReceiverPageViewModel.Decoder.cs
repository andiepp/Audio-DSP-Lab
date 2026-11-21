using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;
using System.Windows.Input;
using System.Windows.Media.Imaging;
using AudioDspLab.Protocol;
using AudioDspLab.Receiver;

namespace AudioDspLab.ViewModels;

/// <summary>
/// The Decoder view (design/rtty-fax.md): the board's RTTY and weather fax
/// decoders, the SDR-IQ Receiver's DWD RTTY and WEATHER FAX tabs on the
/// receiver backend. The board decodes; the page shows the text, keeps the
/// daily log and the ZCZC..NNNN messages, builds the chart from the board's
/// fax lines and saves it as a PNG at the stop tone, at a new start tone,
/// at "New chart", when the chart is full and when the receiver stops.
/// </summary>
public sealed partial class ReceiverPageViewModel
{
    public const string DecoderView = "decoder";

    /// <summary>Fax lines from the board at full width (IOC 576 x pi).</summary>
    public const int FaxEventWidth = 1810;

    private const int TextKept = 20_000;
    private static readonly TimeSpan FaxDrawInterval = TimeSpan.FromMilliseconds(500);

    private readonly StringBuilder rttyText = new();
    private readonly DecoderRecorder recorder = new();
    private DecoderStatus decoderStatus = DecoderStatus.Off;
    private FaxChart? chart;
    private bool chartChanged;
    private DateTime chartDrawnAt;
    private BitmapSource? faxImage;
    private string faxNote = string.Empty;

    private void InitializeDecoder()
    {
        Decoder = Choice("decoder", "Decoder", ("off", "Off"), ("rtty", "RTTY"), ("fax", "Fax"));
        RttyShift = Choice("rtty-shift", "Shift", ("85", "85 Hz"), ("170", "170 Hz"), ("450", "450 Hz"), ("850", "850 Hz"));
        RttyPolarity = Choice("rtty-polarity", "Polarity", ("auto", "Auto"), ("normal", "Normal"), ("reverse", "Reverse"));
        FaxLpm = Choice("fax-lpm", "Lines a minute", ("60", "60"), ("90", "90"), ("120", "120"), ("240", "240"));
        FaxIoc = Choice("fax-ioc", "IOC", ("288", "288"), ("576", "576"));
        FaxSlant = Integer("fax-slant", "Slant", -200, 200, "-- ppm --");
        DecoderAfc = Switch("decoder-afc", "AFC");
        TestMode = Choice("test-mode", "Test signal", ("tone", "Tone"), ("rtty", "RTTY"), ("fax", "Fax"));

        Decoder.ValueChanged += _ =>
        {
            RaisePropertyChanged(nameof(IsRtty));
            RaisePropertyChanged(nameof(IsFax));
            RaisePropertyChanged(nameof(IsDecoderOff));
            RaiseDecoder();
        };
        recorder.MessageSaved += message => Messages.Insert(0, message);

        ClearTextCommand = new RelayCommand(() =>
        {
            rttyText.Clear();
            RaisePropertyChanged(nameof(RttyText));
        });
        NewChartCommand = new RelayCommand(() => StartChart(save: true));
        SaveChartCommand = new RelayCommand(() => SaveChart(), () => chart is { Height: >= DecoderRecorder.MinimumChartLines });
        OpenFolderCommand = new RelayCommand(folder => OpenFolder(folder as string == "fax" ? DecoderRecorder.FaxFolder : DecoderRecorder.RttyFolder));
        OpenMessageCommand = new RelayCommand(message =>
        {
            if (message is DecoderMessage saved && File.Exists(saved.Path))
            {
                Process.Start(new ProcessStartInfo(saved.Path) { UseShellExecute = true });
            }
        });
    }

    // --- parameters ------------------------------------------------------------------------------

    public ParameterViewModel Decoder { get; private set; } = null!;

    public ParameterViewModel RttyShift { get; private set; } = null!;

    public ParameterViewModel RttyPolarity { get; private set; } = null!;

    public ParameterViewModel FaxLpm { get; private set; } = null!;

    public ParameterViewModel FaxIoc { get; private set; } = null!;

    public ParameterViewModel FaxSlant { get; private set; } = null!;

    public ParameterViewModel DecoderAfc { get; private set; } = null!;

    /// <summary>The test signal: the tone, RTTY (DWD's polarity) or a test chart.</summary>
    public ParameterViewModel TestMode { get; private set; } = null!;

    public bool IsDecoderView => View == DecoderView;

    public bool IsRtty => Decoder.Selected == "rtty";

    public bool IsFax => Decoder.Selected == "fax";

    /// <summary>No decoder runs: the page explains what the two do.</summary>
    public bool IsDecoderOff => !IsRtty && !IsFax;

    // --- RTTY ------------------------------------------------------------------------------------

    /// <summary>The decoded text (the last 20,000 characters).</summary>
    public string RttyText => rttyText.ToString();

    public ObservableCollection<DecoderMessage> Messages { get; } = [];

    /// <summary>0 noise .. 1 clean.</summary>
    public double RttyQuality => decoderStatus.IsRtty ? decoderStatus.Quality : 0;

    public string RttyStatusText => decoderStatus.IsRtty
        ? string.Create(CultureInfo.CurrentCulture,
            $"Quality {decoderStatus.Quality:0.00}   AFC {decoderStatus.AfcHz:+0;-0;0} Hz   mark {(decoderStatus.Reverse ? "lower" : "upper")} tone   frames {decoderStatus.Good} good, {decoderStatus.Bad} rejected")
        : DecoderProblem ?? "--";

    public ICommand ClearTextCommand { get; private set; } = null!;

    public ICommand OpenMessageCommand { get; private set; } = null!;

    public ICommand OpenFolderCommand { get; private set; } = null!;

    // --- fax -------------------------------------------------------------------------------------

    /// <summary>The chart so far (8-bit grey, full width), redrawn twice a second while lines come.</summary>
    public BitmapSource? FaxImage
    {
        get => faxImage;
        private set => SetProperty(ref faxImage, value);
    }

    public string FaxStatusText => decoderStatus.IsFax
        ? string.Create(CultureInfo.CurrentCulture,
            $"{(decoderStatus.Phasing ? "Phasing" : "Lines")}   chart {chart?.Height ?? 0} lines   AFC {decoderStatus.AfcHz:+0;-0;0} Hz   start tone {decoderStatus.StartTone:0.00}   stop tone {decoderStatus.StopTone:0.00}")
          + (faxNote.Length > 0 ? "   " + faxNote : string.Empty)
        : DecoderProblem ?? "--";

    public ICommand NewChartCommand { get; private set; } = null!;

    public ICommand SaveChartCommand { get; private set; } = null!;

    private string? DecoderProblem => decoderStatus.Kind switch
    {
        "no-memory" => "The board has no memory for the decoder.",
        "off" or "rtty" or "fax" => recorder.Problem,
        string other => other,
    };

    // --- from the board --------------------------------------------------------------------------

    public void ApplyRtty(string text, bool lineEnd)
    {
        recorder.Add(text, lineEnd);
        rttyText.Append(text);
        if (lineEnd)
        {
            rttyText.Append(Environment.NewLine);
        }

        if (rttyText.Length > TextKept)
        {
            rttyText.Remove(0, rttyText.Length - TextKept);
        }

        RaisePropertyChanged(nameof(RttyText));
    }

    public void ApplyDecoder(DecoderStatus status)
    {
        decoderStatus = status;
        RaiseDecoder();
    }

    public void ApplyFaxLine(FaxLine line)
    {
        if (chart is null || chart.IsFull)
        {
            StartChart(save: true);
        }

        chart!.Add(line.Pixels);
        chartChanged = true;
    }

    public void ApplyFaxMark(string kind, int value)
    {
        switch (kind)
        {
            case "start":
                StartChart(save: true);
                faxNote = "start tone: a new chart";
                break;
            case "stop":
                SaveChart();
                faxNote = "stop tone: the chart is complete";
                break;
            case "phase" when value >= 0:
                faxNote = string.Create(CultureInfo.CurrentCulture, $"phasing found the line start at pixel {value}");
                break;
        }

        RaiseDecoder();
    }

    /// <summary>The window's timer: draws the chart when lines came, at most twice a second.</summary>
    public void DrawFax()
    {
        if (!chartChanged || chart is null || DateTime.UtcNow - chartDrawnAt < FaxDrawInterval)
        {
            return;
        }

        chartChanged = false;
        chartDrawnAt = DateTime.UtcNow;
        FaxImage = DecoderRecorder.ToBitmap(chart);
        RaisePropertyChanged(nameof(FaxStatusText));
        CommandManager.InvalidateRequerySuggested();
    }

    /// <summary>A click on the chart: that column (a share of the width) becomes the left edge.</summary>
    public void SetFaxLeftEdge(double share)
    {
        if (chart is null || chart.Width == 0)
        {
            return;
        }

        chart.SetLeftEdge((int)Math.Round(share * chart.Width));
        chartChanged = true;
        chartDrawnAt = DateTime.MinValue;
        DrawFax();
    }

    /// <summary>The receiver started: fax lines to this connection, and the text the board kept.</summary>
    internal async Task LoadDecoderAsync(BoardClient board)
    {
        try
        {
            await board.SetFaxEventsAsync(FaxEventWidth).ConfigureAwait(true);
        }
        catch (BoardErrorException exception) when (exception.Code == "unknown-command")
        {
            ApplyDecoder(DecoderStatus.Off with { Kind = "older firmware: no decoders (1.8 has them)" });
            return;
        }

        (string text, DecoderStatus status) = await board.GetRttyAsync().ConfigureAwait(true);
        rttyText.Clear();
        rttyText.Append(text.Replace("\n", Environment.NewLine, StringComparison.Ordinal));
        RaisePropertyChanged(nameof(RttyText));
        ApplyDecoder(status);
    }

    private void ForgetDecoder()
    {
        SaveChart();
        chart = null;
        ApplyDecoder(DecoderStatus.Off);
    }

    private void StartChart(bool save)
    {
        if (save)
        {
            SaveChart();
        }

        chart = new FaxChart(DateTime.UtcNow);
        chartChanged = true;
        chartDrawnAt = DateTime.MinValue;
        FaxImage = null;
    }

    private void SaveChart()
    {
        if (chart is null)
        {
            return;
        }

        if (recorder.SaveChart(chart) is string path)
        {
            faxNote = $"saved {Path.GetFileName(path)}";
            chart = new FaxChart(DateTime.UtcNow);
            FaxImage = null;
        }

        RaiseDecoder();
    }

    private static void OpenFolder(string folder)
    {
        Directory.CreateDirectory(folder);
        Process.Start(new ProcessStartInfo(folder) { UseShellExecute = true });
    }

    private void RaiseDecoder()
    {
        RaisePropertyChanged(nameof(RttyQuality));
        RaisePropertyChanged(nameof(RttyStatusText));
        RaisePropertyChanged(nameof(FaxStatusText));
    }
}
