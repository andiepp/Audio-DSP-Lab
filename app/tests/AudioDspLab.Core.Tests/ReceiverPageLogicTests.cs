using AudioDspLab.Receiver;

namespace AudioDspLab.Tests;

public class ReceiverPageLogicTests
{
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
