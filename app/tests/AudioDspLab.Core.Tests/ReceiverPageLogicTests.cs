using AudioDspLab.FrontEnd;
using AudioDspLab.Receiver;
using AudioDspLab.Simulation;

namespace AudioDspLab.Tests;

public class ReceiverPageLogicTests
{
    [Fact]
    public void TheDialIsTheOffsetWithoutAFrontEndAndTheRfFrequencyWithIt()
    {
        Assert.Null(ReceiverDial.Show(null, 14_074_000));
        Assert.Equal(10_000, ReceiverDial.Show(10_000, null));
        Assert.Equal(14_084_000, ReceiverDial.Show(10_000, 14_074_000));
        Assert.Equal(14_070_000, ReceiverDial.Show(-4_000, 14_074_000));

        Assert.Equal(-SdrTuner.OffsetLimit, ReceiverDial.Minimum(false));
        Assert.Equal(SdrTuner.OffsetLimit, ReceiverDial.Maximum(false));
        Assert.Equal(FrontEndClient.MinHz + SdrTuner.Centre, ReceiverDial.Minimum(true));
        Assert.Equal(FrontEndClient.MaxHz + SdrTuner.Centre, ReceiverDial.Maximum(true));

        Assert.Equal(SdrTuner.OffsetLimit, ReceiverDial.OffsetFor(30_000));
        Assert.Equal(-1_235, ReceiverDial.OffsetFor(-1_234.6));
        Assert.Equal(FrontEndClient.MaxHz + SdrTuner.Centre, ReceiverDial.RfFor(99_000_000));
        Assert.Equal(14_074_001, ReceiverDial.RfFor(14_074_000.7));
    }

    [Fact]
    public async Task ASpinningDialTunesTheNewestValueOnly()
    {
        var latest = new LatestRequest<long>();
        var gate = new TaskCompletionSource();
        var tuned = new List<long>();
        async Task Tune(long hz)
        {
            tuned.Add(hz);
            if (tuned.Count == 1)
            {
                await gate.Task;                               // the first answer is slow
            }
        }

        Task first = latest.SubmitAsync(1, Tune);
        Assert.True(latest.IsRunning);
        Task second = latest.SubmitAsync(2, Tune);
        Task third = latest.SubmitAsync(3, Tune);
        Assert.True(second.IsCompleted && third.IsCompleted); // queued, not run
        gate.SetResult();
        await first;
        Assert.Equal([1, 3], tuned);
        Assert.False(latest.IsRunning);

        await latest.SubmitAsync(4, Tune);
        Assert.Equal([1, 3, 4], tuned);
    }

    [Fact]
    public async Task AFailedTuneDoesNotBlockTheNext()
    {
        var latest = new LatestRequest<long>();
        await Assert.ThrowsAsync<InvalidOperationException>(() => latest.SubmitAsync(1, _ => throw new InvalidOperationException()));
        Assert.False(latest.IsRunning);
        long done = 0;
        await latest.SubmitAsync(2, hz =>
        {
            done = hz;
            return Task.CompletedTask;
        });
        Assert.Equal(2, done);
    }

    [Fact]
    public async Task TheRfDialFollowsTheBoardsOffsetThroughTheTuner()
    {
        // the page's tuning: front end (simulated) plus the board's offset (simulated board)
        using var board = new BoardClient(new SimulatedBoard { ApplicationStartTime = TimeSpan.FromMilliseconds(20) });
        await board.ConnectAsync();
        await board.SelectApplicationAsync(Applications.Receiver);
        var port = new SimulatedFrontEndPort();
        await using var frontEnd = new FrontEndClient(port);
        await frontEnd.OpenAsync();
        ReceiverTuner tuner = ReceiverTuner.ForBoard(board, frontEnd);
        await tuner.InitializeAsync();

        foreach (long dial in new long[] { 14_084_000, 14_084_500, 14_200_000, 14_195_000, 7_074_000 })
        {
            SdrTuning tuning = await tuner.TuneAsync(ReceiverDial.RfFor(dial));
            double offset = await board.GetNumberAsync("offset");
            Assert.Equal(tuning.OffsetHz, offset);
            Assert.Equal(dial, ReceiverDial.Show(offset, port.FrequencyHz));
            Assert.InRange(offset, SdrTuner.WindowLow, SdrTuner.WindowHigh);
        }
    }

    [Fact]
    public void TheChartFloorLeavesRoomBelowTheStopBand()
    {
        Assert.Equal(-100, ReceiverResponse.BottomDb(40));
        Assert.Equal(-100, ReceiverResponse.BottomDb(70));
        Assert.Equal(-110, ReceiverResponse.BottomDb(80));
        Assert.Equal(-130, ReceiverResponse.BottomDb(100));
    }

    [Fact]
    public void EdgesMoveInTheOrderThatNeverCrossesThem()
    {
        Assert.True(ReceiverResponse.LowEdgeFirst(300, 100));     // moving down: low first
        Assert.True(ReceiverResponse.LowEdgeFirst(300, 300));
        Assert.False(ReceiverResponse.LowEdgeFirst(300, 2000));   // moving up: high first
    }

    [Fact]
    public void AMeasurementThatMatchesThePredictionComparesClean()
    {
        ChannelFilterDesign design = ChannelFilter.Design(300, 2700, 80);
        IReadOnlyList<FilterPoint> predicted = ReceiverResponse.AtSMeter(design.Coefficients, -5900, 5900, 100);
        Assert.Equal(119, predicted.Count);
        FilterComparison clean = ReceiverResponse.Compare(design, predicted);
        Assert.Equal(0, clean.WorstPassbandDb, 9);
        Assert.NotNull(clean.MinimumStopDb);
        Assert.True(clean.MinimumStopDb > 75);

        // 1 dB low in the passband, a leak in the stop band
        FilterPoint[] off = [.. predicted.Select(p => p.Hz switch
        {
            1500 => p with { GainDb = p.GainDb - 1 },
            -3000 => p with { GainDb = -40 },
            _ => p,
        })];
        FilterComparison worse = ReceiverResponse.Compare(design, off);
        Assert.Equal(1, worse.WorstPassbandDb, 6);
        Assert.Equal(40, worse.MinimumStopDb!.Value, 6);
    }

    [Fact]
    public void EveryTemplateIsADesignableMask()
    {
        foreach (string name in MaskTemplates.Names)
        {
            MaskSpec spec = MaskTemplates.For(name, -2500, 2500, 75);
            Assert.Null(MaskDesign.Check(spec.Bands));
            Assert.InRange(MaskDesign.Taps(spec.StopDb, spec.TransitionHz), 1, ChannelFilter.MaxTaps);
        }

        MaskSpec like = MaskTemplates.For(MaskTemplates.LikeTheFilter, 300, 2700, 80);
        Assert.Equal([new MaskBand(300, 2700, 0)], like.Bands);
        Assert.Equal(80, like.StopDb);
        Assert.Equal(ChannelFilter.Design(300, 2700, 80).TransitionHz, like.TransitionHz);
        Assert.Equal(MaskTemplates.NotchInUsb, MaskTemplates.For("unknown", 0, 1, 70).Name);
    }
}
