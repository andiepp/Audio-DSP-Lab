using AudioDspLab.FrontEnd;

namespace AudioDspLab.Tests;

public class FrontEndTests
{
    [Fact]
    public void SmallStepsMoveTheOffsetOnly()
    {
        SdrTuning t = SdrTuner.Tune(14_084_000, 14_074_000);
        Assert.Equal(new SdrTuning(14_074_000, 10_000, false), t);
        t = SdrTuner.Tune(14_091_500, 14_074_000);           // +17.5 kHz: still inside
        Assert.False(t.Retune);
        Assert.Equal(17_500, t.OffsetHz);
        t = SdrTuner.Tune(14_080_000, 14_074_000);           // +6 kHz: the window's edge
        Assert.False(t.Retune);
    }

    [Fact]
    public void LeavingTheWindowRecentresAtTenKilohertz()
    {
        SdrTuning t = SdrTuner.Tune(14_200_000, 14_074_000);
        Assert.True(t.Retune);
        Assert.Equal(14_190_000, t.FrontEndHz);
        Assert.Equal(10_000, t.OffsetHz);
        t = SdrTuner.Tune(14_077_000, 14_074_000);           // only 3 kHz: too close to the LO
        Assert.True(t.Retune);
        Assert.Equal(14_067_000, t.FrontEndHz);
        t = SdrTuner.Tune(14_092_001, 14_074_000);           // 18.001 kHz: just outside
        Assert.True(t.Retune);
        Assert.Equal(10_000, t.OffsetHz);
    }

    [Fact]
    public void TheCentreIsNotAQuarterOfTheSampleRate()
    {
        // 12 kHz = 48 kHz / 4: third-order distortion folds onto the image (R3)
        Assert.NotEqual(12_000, SdrTuner.Centre);
        Assert.NotEqual(16_000, SdrTuner.Centre);           // 48 kHz / 3: second order
        Assert.InRange(SdrTuner.Centre, SdrTuner.WindowLow, SdrTuner.WindowHigh);
    }

    [Fact]
    public async Task ClientTalksToTheSimulatedFrontEnd()
    {
        var port = new SimulatedFrontEndPort();
        await using var fe = new FrontEndClient(port);
        await fe.OpenAsync();
        Assert.True(fe.SynthesizerFound);
        Assert.Equal(14_100_000, await fe.SetFrequencyAsync(14_100_000));
        Assert.Equal(new FrontEndStatus(14_100_000, 8, 0), await fe.StatusAsync());
        Assert.Equal(56_400_000, (await fe.StatusAsync()).LoHz);
        await Assert.ThrowsAsync<FrontEndException>(() => fe.SetDriveAsync(5));
        Assert.Equal(0, await fe.SetDriveAsync(0));
        Assert.Equal(325, await fe.SetCorrectionAsync(325));
        await Assert.ThrowsAsync<FrontEndException>(() => fe.SetFrequencyAsync(60_000_000));
    }

    [Fact]
    public async Task TunerMovesTheFrontEndOnlyWhenItMust()
    {
        var port = new SimulatedFrontEndPort();
        await using var fe = new FrontEndClient(port);
        await fe.OpenAsync();
        var offsets = new List<int>();
        var tuner = new ReceiverTuner(fe, (hz, _) =>
        {
            offsets.Add(hz);
            return Task.CompletedTask;
        });
        await tuner.InitializeAsync();
        Assert.Equal(14_074_000, tuner.FrontEndHz);

        await tuner.TuneAsync(14_100_000);                    // 26 kHz away: re-centre
        Assert.Equal(14_090_000, port.FrequencyHz);
        Assert.Equal(10_000, tuner.OffsetHz);

        // 100 Hz steps up to the window's top: the front end stays
        port.Log.Clear();
        for (long dial = 14_100_100; dial <= 14_108_000; dial += 100)
        {
            await tuner.TuneAsync(dial);
        }

        Assert.DoesNotContain(port.Log, c => c.StartsWith('F'));
        Assert.Equal(18_000, tuner.OffsetHz);

        // one step more: the front end follows, the dial stays where it was asked
        await tuner.TuneAsync(14_108_100);
        Assert.Equal(14_098_100, port.FrequencyHz);
        Assert.Equal(10_000, offsets[^1]);
        Assert.Equal(14_108_100, tuner.FrontEndHz + tuner.OffsetHz);
        Assert.Equal(2, tuner.Retunes);
    }

    [Fact]
    public async Task TunerRefusesWhatTheFrontEndCannotReach()
    {
        await using var fe = new FrontEndClient(new SimulatedFrontEndPort());
        await fe.OpenAsync();
        var tuner = new ReceiverTuner(fe, (_, _) => Task.CompletedTask);
        await tuner.InitializeAsync();
        await Assert.ThrowsAsync<ArgumentOutOfRangeException>(() => tuner.TuneAsync(60_000_000));
        Assert.Equal(0, tuner.DialHz);
    }
}
