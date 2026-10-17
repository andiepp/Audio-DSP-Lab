using System.Collections.Concurrent;
using AudioDspLab.Protocol;
using AudioDspLab.Simulation;

namespace AudioDspLab.Tests;

public sealed class BoardClientTests : IAsyncLifetime
{
    private readonly SimulatedBoard board = new() { ApplicationStartTime = TimeSpan.FromMilliseconds(20) };
    private BoardClient client = null!;

    public async Task InitializeAsync()
    {
        client = new BoardClient(board);
        await client.ConnectAsync();
    }

    public Task DisposeAsync()
    {
        client.Dispose();
        return Task.CompletedTask;
    }

    [Fact]
    public async Task Connect_FindsTheFirmwareAndTurnsEventsOn()
    {
        Assert.Equal(new BoardInfo(BoardInfo.ExpectedName, SimulatedBoard.FirmwareVersion, 1, "esp32-pico-kit"), client.Info);
        Assert.Contains("events on", board.Received);
        BoardStatus status = await client.GetStatusAsync();
        Assert.Equal(Applications.SigmaStudio, status.Application);
    }

    [Fact]
    public async Task SelectApplication_ServesItsParameters()
    {
        await client.SelectApplicationAsync(Applications.Generator);
        IReadOnlyList<ParameterDescriptor> parameters = await client.GetParametersAsync();
        Assert.Equal(["waveform", "frequency", "amplitude", "output", "p1-range"], parameters.Select(p => p.Name));
        ParameterDescriptor frequency = parameters[1];
        Assert.Equal(ParameterKind.Number, frequency.Kind);
        Assert.Equal((20, 20000), (frequency.Minimum, frequency.Maximum));
        Assert.Equal("Hz", frequency.Unit);
        Assert.Equal("P1", frequency.Pot);
    }

    [Fact]
    public async Task Set_ReturnsTheValueTakenAndRefusesOutOfRange()
    {
        await client.SelectApplicationAsync(Applications.Generator);
        Assert.Equal("440.13", await client.SetAsync("frequency", "440.126"));
        Assert.Equal("square", await client.SetAsync("waveform", "1"));
        BoardErrorException error = await Assert.ThrowsAsync<BoardErrorException>(() => client.SetAsync("frequency", "30000"));
        Assert.Equal("out-of-range", error.Code);
        Assert.Equal("frequency 20..20000 Hz", error.Detail);
        error = await Assert.ThrowsAsync<BoardErrorException>(() => client.SetAsync("cutoff", "100"));
        Assert.Equal("unknown-parameter", error.Code);
    }

    [Fact]
    public async Task Pots_DriveParametersOnlyWithPotControl()
    {
        await client.SelectApplicationAsync(Applications.Equalizer);
        var changes = new BlockingCollection<(string, string)>();
        client.ValueChanged += (name, value) => changes.Add((name, value));

        board.SetPot(3, 4095);
        Assert.False(changes.TryTake(out _, 300));

        await client.SetPotControlAsync(true);
        board.SetPot(3, 0);
        Assert.True(changes.TryTake(out (string Name, string Value) change, 2000));
        Assert.Equal(("volume", "-60"), change);
    }

    [Fact]
    public async Task Sweep_ReportsEveryPointAndItsEnd()
    {
        await client.SelectApplicationAsync(Applications.Analyzer);
        await client.SetAsync("start", "100");
        await client.SetAsync("stop", "10000");
        await client.SetAsync("points-per-decade", "5");
        var measured = new ConcurrentQueue<SweepPoint>();
        var done = new TaskCompletionSource();
        client.PointMeasured += measured.Enqueue;
        client.SweepChanged += status =>
        {
            if (status.State == SweepStatus.Done)
            {
                done.TrySetResult();
            }
        };

        SweepStatus started = await client.StartSweepAsync();
        Assert.Equal((SweepStatus.Running, 11), (started.State, started.Total));
        await done.Task.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(11, measured.Count);
        IReadOnlyList<SweepPoint> points = await client.GetPointsAsync();
        Assert.Equal(100, points[0].FrequencyHertz);
        Assert.Equal(10000, points[^1].FrequencyHertz);
        Assert.Null(points[0].LeftRelativeDecibel);

        await client.KeepReferenceAsync();
        Assert.Equal(11, (await client.GetReferenceAsync()).Count);
        Assert.Equal("on", await client.GetAsync("output"));
    }

    [Fact]
    public async Task Stations_RoundTripQuotedTexts()
    {
        await client.SetStationNameAsync(2, "Radio \"Drei\"");
        IReadOnlyList<Station> stations = await client.GetStationsAsync();
        Assert.Equal(8, stations.Count);
        Assert.Equal("Radio \"Drei\"", stations[2].Name);
        await client.RestoreStationAsync(null);
        Assert.Equal("SWR3", (await client.GetStationsAsync())[2].Name);
    }

    [Fact]
    public async Task Wifi_NeverReturnsThePassword()
    {
        await client.SetWifiAsync(1, "Home net", "secret-password");
        (IReadOnlyList<WifiNetwork> networks, WifiConnection connection) = await client.GetWifiAsync();
        Assert.Equal(new WifiNetwork(1, "Home net", true), networks[1]);
        Assert.Null(connection.Slot);
        Assert.DoesNotContain(board.Received, line => line == "wifi" && line.Contains("secret"));
    }

    [Fact]
    public async Task ATimeout_IsFollowedByAResynchronisedConnection()
    {
        board.Swallow = line => line == "pots";
        await Assert.ThrowsAsync<TimeoutException>(() => client.GetPotsAsync());
        board.Swallow = null;

        BoardStatus status = await client.GetStatusAsync();
        Assert.Equal(Applications.SigmaStudio, status.Application);
        Assert.Contains(board.Received, line => line.StartsWith("echo sync", StringComparison.Ordinal));
    }

    [Fact]
    public async Task ARestart_IsReported()
    {
        var restarted = new TaskCompletionSource<BoardInfo>();
        client.Restarted += info => restarted.TrySetResult(info);
        await client.SendAsync("reset");
        BoardInfo info = await restarted.Task.WaitAsync(TimeSpan.FromSeconds(2));
        Assert.Equal(BoardInfo.ExpectedName, info.Name);
    }

    [Fact]
    public async Task Unplugging_FailsRequestsAndReportsTheLoss()
    {
        var lost = new TaskCompletionSource();
        client.Disconnected += _ => lost.TrySetResult();
        board.Unplug();
        await lost.Task.WaitAsync(TimeSpan.FromSeconds(2));
        await Assert.ThrowsAsync<IOException>(() => client.GetStatusAsync());
    }
}
