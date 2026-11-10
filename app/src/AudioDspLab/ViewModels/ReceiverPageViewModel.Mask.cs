using System.Collections.ObjectModel;
using System.Globalization;
using System.IO;
using System.Numerics;
using System.Windows.Input;
using AudioDspLab.Receiver;

namespace AudioDspLab.ViewModels;

/// <summary>One band of the Designer's mask: from, to (Hz re the carrier) and its gain.</summary>
public sealed class MaskBandRowViewModel : ObservableObject
{
    private readonly Action changed;
    private double from;
    private double to;
    private double gain;

    internal MaskBandRowViewModel(MaskBand band, Action changed, Action<MaskBandRowViewModel> remove)
    {
        (from, to, gain) = (band.From, band.To, band.GainDb);
        this.changed = changed;
        RemoveCommand = new RelayCommand(() => remove(this));
    }

    public double? From
    {
        get => from;
        set => Set(ref from, Math.Round(value ?? from));
    }

    public double? To
    {
        get => to;
        set => Set(ref to, Math.Round(value ?? to));
    }

    public double? GainDb
    {
        get => gain;
        set => Set(ref gain, Math.Round(value ?? gain, 1));
    }

    public double EdgeMinimum => -ChannelFilter.EdgeLimit;

    public double EdgeMaximum => ChannelFilter.EdgeLimit;

    public ICommand RemoveCommand { get; }

    public MaskBand ToBand() => new(from, to, gain);

    private void Set(ref double field, double value)
    {
        if (field != value)
        {
            field = value;
            RaisePropertyChanged(nameof(From));
            RaisePropertyChanged(nameof(To));
            RaisePropertyChanged(nameof(GainDb));
            changed();
        }
    }
}

/// <summary>
/// The Designer view (from DSP-Box-Lab's Designer page): filters the
/// firmware's own design cannot make -- a mask of bands at any gain (notches,
/// several passbands, slopes), designed here, or coefficients from a file --
/// downloaded to the receiver ("rxfilter"), saved as named designs.
/// </summary>
public sealed partial class ReceiverPageViewModel
{
    private readonly MaskSpecStore store = new(MaskSpecStore.DefaultDirectory);
    private double designStop = 70;
    private double designTransition = 100;
    private Complex[] coefficients = [];
    private string? importedFile;
    private IReadOnlyList<FilterPoint> designPredicted = [];
    private IReadOnlyList<MaskBand> mask = [];
    private string designProblem = string.Empty;
    private string designerMessage = string.Empty;
    private double downloadProgress;
    private string? selectedDesign;
    private string designName = string.Empty;
    private bool loadingMask;

    public ObservableCollection<MaskBandRowViewModel> MaskBands { get; } = [];

    public IReadOnlyList<string> Templates => MaskTemplates.Names;

    public double? DesignStop
    {
        get => designStop;
        set
        {
            if (value is double stop && SetProperty(ref designStop, Math.Clamp(Math.Round(stop), 20, 120)))
            {
                MaskChanged();
            }
        }
    }

    public double? DesignTransition
    {
        get => designTransition;
        set
        {
            if (value is double transition && SetProperty(ref designTransition, Math.Clamp(Math.Round(transition), 20, 1000)))
            {
                MaskChanged();
            }
        }
    }

    public IReadOnlyList<FilterPoint> DesignPredicted
    {
        get => designPredicted;
        private set => SetProperty(ref designPredicted, value);
    }

    public IReadOnlyList<MaskBand> Mask
    {
        get => mask;
        private set => SetProperty(ref mask, value);
    }

    public double DesignBottomDb => ReceiverResponse.BottomDb(designStop);

    /// <summary>Why the mask cannot be designed, or nothing.</summary>
    public string DesignProblem
    {
        get => designProblem;
        private set => SetProperty(ref designProblem, value);
    }

    public string DesignSourceText =>
        importedFile is not null
            ? $"Coefficients from {Path.GetFileName(importedFile)} ({coefficients.Length} taps). Use the mask designs from the bands again."
            : "Designed from the bands: a Kaiser-windowed ideal response, edges at the -6 dB points.";

    public string DesignInfoText =>
        coefficients.Length == 0
            ? string.Empty
            : string.Format(CultureInfo.CurrentCulture, "{0} taps, {1}, delay {2:0.0} ms",
                coefficients.Length, coefficients.Any(c => c.Imaginary != 0) ? "complex" : "real",
                (coefficients.Length - 1) / 2.0 / ChannelFilter.SampleRate * 1000);

    public string DesignerBoardText =>
        filterStatus switch
        {
            null => "--",
            { Downloaded: true } s => string.Format(CultureInfo.CurrentCulture,
                "The receiver runs a downloaded filter ({0} taps); a new edge, stop band or mode designs again.", s.Taps),
            { } s => string.Format(CultureInfo.CurrentCulture, "The receiver runs its own design from the edges ({0} taps).", s.Taps)
        };

    /// <summary>The last import, export, save or load, or what went wrong.</summary>
    public string DesignerMessage
    {
        get => designerMessage;
        private set => SetProperty(ref designerMessage, value);
    }

    /// <summary>0 .. 1.</summary>
    public double DownloadProgress
    {
        get => downloadProgress;
        private set => SetProperty(ref downloadProgress, value);
    }

    public ObservableCollection<string> Designs { get; } = [];

    public string? SelectedDesign
    {
        get => selectedDesign;
        set
        {
            if (SetProperty(ref selectedDesign, value) && value is not null)
            {
                DesignName = value;
            }
        }
    }

    public string DesignName
    {
        get => designName;
        set => SetProperty(ref designName, value ?? string.Empty);
    }

    public string DesignsDirectory => store.Directory;

    public bool HasCoefficients => coefficients.Length > 0;

    public ICommand AddBandCommand { get; private set; } = null!;

    public ICommand TemplateCommand { get; private set; } = null!;

    public ICommand DownloadCommand { get; private set; } = null!;

    public ICommand DesignAgainCommand { get; private set; } = null!;

    public ICommand UseMaskCommand { get; private set; } = null!;

    public ICommand SaveDesignCommand { get; private set; } = null!;

    public ICommand LoadDesignCommand { get; private set; } = null!;

    public ICommand DeleteDesignCommand { get; private set; } = null!;

    /// <summary>Downloads the design (or the imported coefficients) and switches it in.</summary>
    public async Task DownloadAsync()
    {
        if (isWorking || coefficients.Length == 0)
        {
            return;
        }

        Complex[] h = coefficients;
        DownloadProgress = 0;
        Begin();
        CancellationToken token = cancel!.Token;
        try
        {
            await Run(
                async board =>
                {
                    try
                    {
                        await board.DownloadFilterAsync(h, new Progress<int>(taps => DownloadProgress = taps / (double)h.Length), token)
                            .ConfigureAwait(true);
                    }
                    catch (OperationCanceledException)
                    {
                        throw new OperationCanceledException("Download cancelled; the receiver keeps its filter.");
                    }

                    downloaded = h;
                    DownloadProgress = 1;
                    Filter.Apply(await board.GetAsync("filter").ConfigureAwait(true), fromBoard: false);
                    FilterChanged();
                    await RefreshFilterStatusAsync(board).ConfigureAwait(true);
                },
                string.Format(CultureInfo.CurrentCulture, "Filter downloaded: {0} taps, live on the receiver.", h.Length)).ConfigureAwait(true);
        }
        finally
        {
            End();
        }
    }

    /// <summary>Back to the filter the firmware designs from its edges.</summary>
    public Task DesignAgainAsync() =>
        Run(
            async board =>
            {
                Filter.Apply(await board.DesignAgainAsync().ConfigureAwait(true), fromBoard: false);
                FilterChanged();
                await RefreshFilterStatusAsync(board).ConfigureAwait(true);
            },
            "The receiver designs its filter from the edges again.");

    /// <summary>Coefficients from a file ("k,re,im", "re,im" or one real value per line).</summary>
    public void ImportCoefficients(string path)
    {
        try
        {
            Complex[] h = CoefficientFile.Read(path);
            importedFile = path;
            DesignProblem = string.Empty;
            Recompute(h);
            DesignerMessage = $"Imported {h.Length} coefficients from {Path.GetFileName(path)}.";
        }
        catch (Exception exception) when (exception is IOException or FormatException or UnauthorizedAccessException or OverflowException)
        {
            DesignerMessage = "Import failed: " + exception.Message;
        }
    }

    public void ExportCoefficients(string path)
    {
        try
        {
            CoefficientFile.Write(path, coefficients);
            DesignerMessage = $"Exported {coefficients.Length} coefficients to {Path.GetFileName(path)}.";
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            DesignerMessage = "Export failed: " + exception.Message;
        }
    }

    private void InitializeDesigner()
    {
        AddBandCommand = new RelayCommand(AddBand, () => MaskBands.Count < 8);
        TemplateCommand = new RelayCommand(name => ApplyTemplate(name as string ?? MaskTemplates.NotchInUsb));
        DownloadCommand = new RelayCommand(() => _ = DownloadAsync(), () => IsRunning && !isWorking && coefficients.Length > 0);
        DesignAgainCommand = new RelayCommand(() => _ = DesignAgainAsync(), () => IsRunning && !isWorking);
        UseMaskCommand = new RelayCommand(
            () =>
            {
                importedFile = null;
                MaskChanged();
            },
            () => importedFile is not null);
        SaveDesignCommand = new RelayCommand(SaveDesign, () => MaskSpecStore.IsValidName(designName.Trim()) && importedFile is null);
        LoadDesignCommand = new RelayCommand(LoadDesign, () => selectedDesign is not null);
        DeleteDesignCommand = new RelayCommand(DeleteDesign, () => selectedDesign is not null);
        ApplyTemplate(MaskTemplates.NotchInUsb);
        RefreshDesigns(null);
    }

    private void MaskChanged()
    {
        if (loadingMask)
        {
            return;
        }

        Mask = [.. MaskBands.Select(row => row.ToBand())];
        RaisePropertyChanged(nameof(DesignBottomDb));
        if (importedFile is not null)
        {
            Recompute(coefficients);
            return;
        }

        string? problem = MaskDesign.Check(Mask);
        DesignProblem = problem ?? string.Empty;
        if (problem is null)
        {
            Recompute(MaskDesign.Design(Mask, designStop, designTransition));
        }
    }

    private void Recompute(Complex[] h)
    {
        coefficients = h;
        DesignPredicted = ReceiverResponse.AtSMeter(h);
        DownloadProgress = 0;
        RaisePropertyChanged(nameof(DesignInfoText));
        RaisePropertyChanged(nameof(DesignSourceText));
        RaisePropertyChanged(nameof(HasCoefficients));
        CommandManager.InvalidateRequerySuggested();
    }

    private void AddBand()
    {
        double start = MaskBands.Count > 0 ? MaskBands.Max(row => row.To ?? 0) + 200 : 300;
        if (start > ChannelFilter.EdgeLimit - 200)
        {
            start = -ChannelFilter.EdgeLimit;
        }

        MaskBands.Add(Row(new MaskBand(start, Math.Min(start + 500, ChannelFilter.EdgeLimit), 0)));
        MaskChanged();
    }

    private void RemoveBand(MaskBandRowViewModel row)
    {
        if (MaskBands.Count > 1 && MaskBands.Remove(row))
        {
            MaskChanged();
        }
    }

    private MaskBandRowViewModel Row(MaskBand band) => new(band, MaskChanged, RemoveBand);

    private void ApplyTemplate(string name) =>
        LoadMask(MaskTemplates.For(name, Low.Value ?? 300, High.Value ?? 2700, Stopband.Value ?? 70));

    private void LoadMask(MaskSpec spec)
    {
        loadingMask = true;
        MaskBands.Clear();
        foreach (MaskBand band in spec.Bands)
        {
            MaskBands.Add(Row(band));
        }

        designStop = Math.Clamp(Math.Round(spec.StopDb), 20, 120);
        designTransition = Math.Clamp(Math.Round(spec.TransitionHz), 20, 1000);
        RaisePropertyChanged(nameof(DesignStop));
        RaisePropertyChanged(nameof(DesignTransition));
        loadingMask = false;
        importedFile = null;
        MaskChanged();
    }

    private void SaveDesign()
    {
        string name = designName.Trim();
        try
        {
            store.Save(new MaskSpec(name, Mask, designStop, designTransition));
            RefreshDesigns(name);
            DesignerMessage = $"Design '{name}' saved.";
        }
        catch (Exception exception) when (exception is IOException or ArgumentException or UnauthorizedAccessException)
        {
            DesignerMessage = exception.Message;
        }
    }

    private void LoadDesign()
    {
        if (selectedDesign is not { } name || store.Read(name) is not { } spec)
        {
            DesignerMessage = $"Design '{selectedDesign}' could not be read.";
            return;
        }

        LoadMask(spec);
        DesignerMessage = $"Design '{name}' loaded.";
    }

    private void DeleteDesign()
    {
        if (selectedDesign is not { } name)
        {
            return;
        }

        try
        {
            store.Delete(name);
            RefreshDesigns(null);
            DesignerMessage = $"Design '{name}' deleted.";
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            DesignerMessage = exception.Message;
        }
    }

    private void RefreshDesigns(string? select)
    {
        Designs.Clear();
        foreach (string name in store.Names())
        {
            Designs.Add(name);
        }

        SelectedDesign = select is not null && Designs.Contains(select) ? select : null;
    }
}
