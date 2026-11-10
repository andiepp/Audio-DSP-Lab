namespace AudioDspLab.FrontEnd;

/// <summary>
/// Tunes the receiver application (0x07) to a dial frequency with the SDR front
/// end on its own serial port (design/receiver.md, section 6 option B): the
/// front end moves only when the carrier would leave its window
/// (<see cref="SdrTuner"/>), otherwise only the board's offset changes. On a
/// re-centre the front end moves first, then the offset, so a station on the
/// dial stays where it is.
/// </summary>
public sealed class ReceiverTuner(FrontEndClient frontEnd, Func<int, CancellationToken, Task> setOffset)
{
    /// <summary>A tuner for the board's receiver: its offset is the firmware parameter "offset".</summary>
    public static ReceiverTuner ForBoard(BoardClient board, FrontEndClient frontEnd) =>
        new(frontEnd, (hz, cancellation) => board.SetAsync("offset", hz, cancellation));

    /// <summary>The front end's frequency, as it last answered.</summary>
    public long FrontEndHz { get; private set; }

    /// <summary>The dial: the carrier frequency the receiver listens to (0 until tuned).</summary>
    public long DialHz { get; private set; }

    public int OffsetHz => (int)(DialHz - FrontEndHz);

    /// <summary>Re-centres made since the start, for tests and diagnostics.</summary>
    public int Retunes { get; private set; }

    /// <summary>Reads where the front end stands; the next tune starts from there.</summary>
    public async Task InitializeAsync(CancellationToken cancellation = default)
    {
        FrontEndHz = (await frontEnd.StatusAsync(cancellation).ConfigureAwait(false)).FrequencyHz;
    }

    public async Task<SdrTuning> TuneAsync(long dialHz, CancellationToken cancellation = default)
    {
        if (dialHz < FrontEndClient.MinHz + SdrTuner.Centre || dialHz > FrontEndClient.MaxHz + SdrTuner.Centre)
        {
            throw new ArgumentOutOfRangeException(nameof(dialHz), dialHz, "Outside the front end's range.");
        }

        SdrTuning t = SdrTuner.Tune(dialHz, FrontEndHz);
        if (t.Retune)
        {
            FrontEndHz = await frontEnd.SetFrequencyAsync(t.FrontEndHz, cancellation).ConfigureAwait(false);
            Retunes++;
        }

        // the offset from the frequency the front end confirmed
        long offset = dialHz - FrontEndHz;
        if (Math.Abs(offset) > SdrTuner.OffsetLimit)
        {
            throw new FrontEndException($"Front end at {FrontEndHz} Hz leaves {offset} Hz for the receiver, more than ±{SdrTuner.OffsetLimit}.");
        }

        await setOffset((int)offset, cancellation).ConfigureAwait(false);
        DialHz = dialHz;
        return t with { FrontEndHz = FrontEndHz, OffsetHz = (int)offset };
    }
}
