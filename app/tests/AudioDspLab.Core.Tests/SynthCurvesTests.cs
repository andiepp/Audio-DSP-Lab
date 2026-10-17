namespace AudioDspLab.Tests;

public class SynthCurvesTests
{
    [Fact]
    public void Ladder_PassesLowsAndIsFourTimesThreeDecibelsDownAtTheCutoff()
    {
        Assert.Equal(0, SynthCurves.LadderGainDecibel(10, 1000, 0), 1);
        // the bilinear stages are prewarped: each is exactly -3 dB at the cutoff
        Assert.Equal(-12.04, SynthCurves.LadderGainDecibel(1000, 1000, 0), 2);
    }

    [Fact]
    public void Ladder_FallsTwentyFourDecibelsPerOctave()
    {
        double slope = SynthCurves.LadderGainDecibel(3200, 200, 0) - SynthCurves.LadderGainDecibel(1600, 200, 0);
        Assert.InRange(slope, -24.5, -23.5);
    }

    [Fact]
    public void Ladder_EmphasisPeaksAtTheCutoffAndMakesUpPartOfThePassband()
    {
        // k = 2.1 at 50 %: the passband is (1 + k/4) / (1 + k) as the firmware's make-up leaves it
        double passband = SynthCurves.LadderGainDecibel(10, 1000, 50);
        Assert.Equal(20 * Math.Log10(1.525 / 3.1), passband, 1);
        Assert.True(SynthCurves.LadderGainDecibel(1000, 1000, 50) > passband + 3);
    }

    [Fact]
    public void Contour_RaisesTheCutoffUpToFiveOctavesWithinTheFiltersRange()
    {
        Assert.Equal(1000 * Math.Pow(2, 2.5), SynthCurves.ContourPeakCutoff(1000, 50), 6);
        Assert.Equal(SynthCurves.CutoffHighest, SynthCurves.ContourPeakCutoff(1000, 100));
    }

    [Fact]
    public void Envelope_AttacksLinearlyAndDecaysWithinOnePercentAtItsTime()
    {
        Assert.Equal(0.5, SynthCurves.HeldLevel(5, 10, 200, 0.6), 6);
        Assert.Equal(1, SynthCurves.HeldLevel(10, 10, 200, 0.6), 6);
        Assert.Equal(0.6 + (0.4 * 0.01), SynthCurves.HeldLevel(210, 10, 200, 0.6), 4);
        Assert.Equal(0.006, SynthCurves.ReleasedLevel(500, 0.6, 500), 4);
    }

    [Fact]
    public void Envelopes_SpanTheLongerOfLoudnessAndContour()
    {
        var envelopes = new SynthEnvelopes(5, 400, 0.6, 500, 5, 600, 0.3);
        Assert.Equal(605, envelopes.HeldSpan);
        Assert.Equal(600, envelopes.ReleaseSpan);
    }
}
