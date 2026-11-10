namespace AudioDspLab.FrontEnd;

/// <summary>Where to put the front end and the receiver's offset for a dial frequency.</summary>
/// <param name="FrontEndHz">The front end's receive frequency (LO / 4).</param>
/// <param name="OffsetHz">The receiver's tuning offset (parameter "offset") from it.</param>
/// <param name="Retune">The front end has to move.</param>
public readonly record struct SdrTuning(long FrontEndHz, int OffsetHz, bool Retune);

/// <summary>
/// The dial split for the I/Q input (design/receiver.md, sections 5 and 14): the
/// carrier on the dial sits 6 .. 18 kHz above the front end's frequency, away
/// from its DC, hum and noise near the LO. Small steps move only the receiver's
/// offset (no click); leaving the window re-centres the front end with the
/// carrier at +10 kHz -- not the DSP-Box's +12 kHz, which is a quarter of the
/// 48 kHz sample rate: there third-order distortion folds exactly onto the
/// image (R3).
/// </summary>
public static class SdrTuner
{
    public const int WindowLow = 6000, WindowHigh = 18000, Centre = 10000;

    /// <summary>The receiver's offset range (firmware parameter "offset").</summary>
    public const int OffsetLimit = 21000;

    public static SdrTuning Tune(long dialHz, long frontEndHz)
    {
        long offset = dialHz - frontEndHz;
        if (offset is >= WindowLow and <= WindowHigh)
        {
            return new SdrTuning(frontEndHz, (int)offset, false);
        }

        long fe = Math.Clamp(dialHz - Centre, FrontEndClient.MinHz, FrontEndClient.MaxHz);
        return new SdrTuning(fe, (int)Math.Clamp(dialHz - fe, -OffsetLimit, OffsetLimit), true);
    }

    /// <summary>The 20 m band-pass filter's useful range (QRP Labs kit, about 0.6 .. 0.8 MHz wide at 14.2 MHz).</summary>
    public const long BandLowHz = 13_900_000, BandHighHz = 14_500_000;

    public static bool InBand(long hz) => hz is >= BandLowHz and <= BandHighHz;
}
