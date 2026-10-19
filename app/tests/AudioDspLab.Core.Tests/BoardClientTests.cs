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
        Assert.Equal(["waveform", "frequency", "amplitude", "output", "p2-range", "width"], parameters.Select(p => p.Name));
        ParameterDescriptor frequency = parameters[1];
        Assert.Equal(ParameterKind.Number, frequency.Kind);
        Assert.Equal((20, 20000), (frequency.Minimum, frequency.Maximum));
        Assert.Equal("Hz", frequency.Unit);
        Assert.Equal("P2", frequency.Pot);
    }

    [Fact]
    public async Task Set_ReturnsTheValueTakenAndRefusesOutOfRange()
    {
        await client.SelectApplicationAsync(Applications.Generator);
        Assert.Equal("440.13", await client.SetAsync("frequency", "440.126"));
        Assert.Equal("rectangle", await client.SetAsync("waveform", "1"));
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

        // switching pot control on takes where the pot stands now
        await client.SetPotControlAsync(true);
        Assert.True(changes.TryTake(out (string Name, string Value) change, 2000));
        Assert.Equal(("volume", "0"), change);

        board.SetPot(3, 0);
        Assert.True(changes.TryTake(out change, 2000));
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
    public async Task Distortion_ReportsResultsHarmonicsAndSpectrum()
    {
        await client.SelectApplicationAsync(Applications.Analyzer);
        await client.SetAsync("tone", "1000");
        var result = new TaskCompletionSource<DistortionStatus>();
        client.DistortionChanged += status =>
        {
            if (status.Results > 0)
            {
                result.TrySetResult(status);
            }
        };

        DistortionStatus started = await client.StartDistortionAsync();
        Assert.Equal((DistortionStatus.Running, 0), (started.State, started.Results));
        Assert.Null(started.Left);
        Assert.Equal("on", await client.GetAsync("output"));

        DistortionStatus first = await result.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Equal(1001.95, first.FrequencyHertz);           // the tone on the nearest FFT bin
        Assert.NotNull(first.Left);
        Assert.InRange(first.Left.ThdDecibel!.Value, -80, -70);
        Assert.True(first.Left.ThdnDecibel >= first.Left.ThdDecibel);   // the noise on top of the harmonics

        IReadOnlyList<Harmonic> harmonics = await client.GetHarmonicsAsync();
        Assert.Equal(Enumerable.Range(2, 9), harmonics.Select(harmonic => harmonic.Order));
        Assert.Equal(2003.9, harmonics[0].FrequencyHertz);

        IReadOnlyList<SpectrumPoint> spectrum = await client.GetSpectrumAsync();
        Assert.Equal(256, spectrum.Count);
        SpectrumPoint peak = spectrum.MaxBy(point => point.LeftDecibel)!;
        Assert.InRange(peak.FrequencyHertz, 950, 1050);
        Assert.Equal(first.Left.LevelDecibel, peak.LeftDecibel);
        Assert.Equal(32, (await client.GetSpectrumAsync(32)).Count);

        DistortionStatus stopped = await client.StopDistortionAsync();
        Assert.Equal(DistortionStatus.Idle, stopped.State);
        Assert.NotNull(stopped.Left);                          // the last result stays
    }

    [Fact]
    public async Task Spectrum_ReportsRmsAndPeaksWithoutATone()
    {
        await client.SelectApplicationAsync(Applications.Analyzer);
        var result = new TaskCompletionSource<SpectrumStatus>();
        client.SpectrumChanged += status =>
        {
            if (status.Results > 0)
            {
                result.TrySetResult(status);
            }
        };

        SpectrumStatus started = await client.StartSpectrumAsync();
        Assert.Equal((DistortionStatus.Running, 0), (started.State, started.Results));
        Assert.Equal("off", await client.GetAsync("output"));        // no tone of its own

        SpectrumStatus first = await result.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.InRange(first.LeftRmsDecibel!.Value, -98, -97);     // the floor alone
        IReadOnlyList<SpectrumPeak> peaks = await client.GetPeaksAsync();
        Assert.Equal(5, peaks.Count);
        Assert.Equal(50.0, peaks[0].LeftHertz);                    // the hum
        Assert.Empty(await client.GetHarmonicsAsync());            // a Spectrum result has no tone
        Assert.Equal(256, (await client.GetSpectrumAsync()).Count);

        DistortionStatus thd = await client.StartDistortionAsync();
        Assert.True(thd.IsRunning);
        Assert.Equal(DistortionStatus.Idle, (await client.GetSpectrumStatusAsync()).State);   // one part at a time
    }

    [Fact]
    public void SpectrumStatus_AndPeaks_ParseTheBoardsLines()
    {
        Assert.Equal(new SpectrumStatus("running", 2, -97.53, -98.08), SpectrumStatus.Parse(["running", "2", "-97.53", "-98.08"]));
        Assert.Equal(new SpectrumStatus("idle", 0, null, null), SpectrumStatus.Parse(["idle", "0", "-", "-"]));
        Assert.Equal(new SpectrumPeak(1, 47.1, -104.56, 48.1, -104.78), SpectrumPeak.Parse(["1", "47.1", "-104.56", "48.1", "-104.78"]));
    }

    [Fact]
    public void DistortionStatus_ParsesTheBoardsLine()
    {
        DistortionStatus status = DistortionStatus.Parse(
            ["running", "4", "1007.81", "-27.37", "-81.10", "-66.37", "-27.40", "-", "-67.01"]);
        Assert.True(status.IsRunning);
        Assert.Equal(new DistortionChannel(-27.37, -81.10, -66.37), status.Left);
        Assert.Equal(new DistortionChannel(-27.40, null, -67.01), status.Right);
        Assert.Null(DistortionStatus.Parse(["idle", "0", "-", "-", "-", "-", "-", "-", "-"]).Left);
        Assert.Equal(0.01, DistortionChannel.Percent(-80), 6);
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
    public async Task AResponse_KeepsItsLinesAsTheBoardSentThem()
    {
        BoardResponse response = await client.SendAsync("stations");
        Assert.Equal(9, response.RawLines.Count);
        Assert.Equal("* 0 \"Deutschlandfunk\" \"http://st01.dlf.de/dlf/01/128/mp3/stream.mp3\"", response.RawLines[0]);
        Assert.Equal("ok", response.RawLines[^1]);
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
