using System.Collections.ObjectModel;
using System.Globalization;
using System.Windows.Input;

namespace AudioDspLab.ViewModels;

/// <summary>One station key: lit while the board plays that station.</summary>
public sealed class StationKey : ObservableObject
{
    private string name;
    private string url;
    private bool isSelected;

    internal StationKey(int index, string name, string url, ICommand select)
    {
        Index = index;
        this.name = name;
        this.url = url;
        SelectCommand = select;
    }

    public int Index { get; }

    public string Name
    {
        get => name;
        internal set => SetProperty(ref name, value);
    }

    public string Url
    {
        get => url;
        internal set => SetProperty(ref url, value);
    }

    public ICommand SelectCommand { get; }

    public bool IsSelected
    {
        get => isSelected;
        internal set => SetProperty(ref isSelected, value);
    }
}

/// <summary>The radio strip: eight stations, the connection, the stream's rate and title.</summary>
public sealed class RadioViewModel : ApplicationPageViewModel
{
    private string? state;
    private int? sampleRate;
    private string title = string.Empty;
    private string wifiText = string.Empty;

    public RadioViewModel(ParameterWrite write, BoardAction run)
        : base(write, run, Applications.Radio)
    {
        Station = Integer("station", "Station", 0, 7, string.Empty);
        Station.ValueChanged += _ => MarkSelected();
        SelectStationCommand = new RelayCommand(
            index => _ = Station.Write(Convert.ToString(index, CultureInfo.InvariantCulture)!),
            _ => IsRunning);
    }

    public ParameterViewModel Station { get; }

    public ObservableCollection<StationKey> Stations { get; } = [];

    /// <summary>Takes a station's index.</summary>
    public ICommand SelectStationCommand { get; }

    public string StationName =>
        Station.Value is double index && (int)index < Stations.Count ? Stations[(int)index].Name : "---";

    /// <summary>The connection: connecting-wifi, connecting-stream, playing, no-network, stream-error.</summary>
    public string? State
    {
        get => state;
        private set
        {
            if (!SetProperty(ref state, value))
            {
                return;
            }

            RaisePropertyChanged(nameof(ConnectionText));
            RaisePropertyChanged(nameof(IsPlaying));
            RaisePropertyChanged(nameof(IsConnecting));
            RaisePropertyChanged(nameof(IsConnectionFailed));
        }
    }

    public string ConnectionText =>
        state switch
        {
            null => "---",
            "connecting-wifi" => "WiFi …",
            "connecting-stream" => "Stream …",
            "playing" => "Playing",
            "no-network" => "No network",
            "stream-error" => "Stream error",
            "idle" => "Idle",
            _ => state
        };

    public bool IsPlaying => state == RadioStatus.Playing;

    public bool IsConnecting => state is "connecting-wifi" or "connecting-stream";

    public bool IsConnectionFailed => state is "no-network" or "stream-error";

    public string SampleRateText =>
        sampleRate is int rate && IsPlaying
            ? string.Format(CultureInfo.CurrentCulture, "{0:0.0} kHz", rate / 1000.0)
            : "---";

    public string StreamTitle
    {
        get => title;
        private set
        {
            if (SetProperty(ref title, value))
            {
                RaisePropertyChanged(nameof(StreamTitleText));
            }
        }
    }

    public string StreamTitleText => title.Length > 0 ? title : IsPlaying ? "(no title)" : string.Empty;

    /// <summary>Which network the radio uses, or why it has none.</summary>
    public string WifiText
    {
        get => wifiText;
        internal set => SetProperty(ref wifiText, value);
    }

    internal void SetStations(IReadOnlyList<Station> stations)
    {
        foreach (Station station in stations)
        {
            StationKey? key = Stations.FirstOrDefault(candidate => candidate.Index == station.Slot);
            if (key is null)
            {
                Stations.Add(new StationKey(station.Slot, station.Name, station.Url, SelectStationCommand));
            }
            else
            {
                key.Name = station.Name;
                key.Url = station.Url;
            }
        }

        MarkSelected();
    }

    internal void ApplyStatus(string newState, int rate)
    {
        sampleRate = rate;
        State = newState;
        RaisePropertyChanged(nameof(SampleRateText));
        RaisePropertyChanged(nameof(StreamTitleText));
    }

    internal void ApplyTitle(string text) => StreamTitle = text;

    internal override void Forget()
    {
        base.Forget();
        State = null;
        sampleRate = null;
        StreamTitle = string.Empty;
        WifiText = string.Empty;
        RaisePropertyChanged(nameof(SampleRateText));
    }

    private void MarkSelected()
    {
        foreach (StationKey key in Stations)
        {
            key.IsSelected = Station.Value is double index && (int)index == key.Index;
        }

        RaisePropertyChanged(nameof(StationName));
    }
}
