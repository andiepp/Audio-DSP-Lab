using AudioDspLab.Receiver;

namespace AudioDspLab.FrontEnd;

/// <summary>What "Null image" found.</summary>
/// <param name="GainDb">The correction now in force (iq-gain).</param>
/// <param name="PhaseDeg">The correction now in force (iq-phase).</param>
/// <param name="SignalDbfs">The carrier's level in the channel.</param>
/// <param name="ImageBeforeDbfs">Its image before the search.</param>
/// <param name="ImageAfterDbfs">... and after.</param>
public sealed record ImageNullResult(double GainDb, double PhaseDeg, double SignalDbfs, double ImageBeforeDbfs, double ImageAfterDbfs)
{
    public double RejectionBeforeDb => SignalDbfs - ImageBeforeDbfs;

    public double RejectionAfterDb => SignalDbfs - ImageAfterDbfs;
}

/// <summary>
/// Finds the I/Q correction with a real signal (ported from DSP-Box-Lab): a
/// steady carrier (a generator) tuned into the channel. Its image sits at the
/// mirror of its offset from the front end's frequency; the receiver is tuned
/// there (the offset negated) and the gain and phase corrections are searched
/// for the lowest S-meter reading -- golden-section searches, phase and gain in
/// turn, three rounds with narrowing ranges.
/// </summary>
/// <remarks>
/// While searching, the receiver runs in AM (no CW pitch shift) with a ±150 Hz
/// filter around the carrier (a low noise floor), the AGC off and the S-meter
/// fast (hang 0, decay 200 dB/s). Mode, filter edges, offset and AGC are
/// restored afterwards, also on cancel; the corrections found stay. Avoid an
/// offset of 12 kHz: there third-order distortion folds onto the image and
/// caps the null (design/receiver.md, R3).
/// </remarks>
public static class ImageNull
{
    /// <summary>Half the width of the filter around the carrier during the search, Hz.</summary>
    public const double NarrowHz = 150;

    private static readonly string[] Touched = ["mode", "pitch", "low", "high", "offset", "agc", "hang", "decay"];

    public static async Task<ImageNullResult> RunAsync(BoardClient board, IProgress<string>? progress = null,
        CancellationToken cancellation = default)
    {
        var before = new Dictionary<string, string>();
        foreach (string p in Touched)
        {
            before[p] = await board.GetAsync(p, cancellation).ConfigureAwait(false);
        }

        if (await board.GetAsync("input", cancellation).ConfigureAwait(false) == "if")
        {
            throw new InvalidOperationException("The image null needs the I/Q input.");
        }

        double offset = await board.GetNumberAsync("offset", cancellation).ConfigureAwait(false);
        if (before["mode"] == "cw")
        {
            offset -= await board.GetNumberAsync("pitch", cancellation).ConfigureAwait(false);   // the carrier sits pitch below the dial
        }

        try
        {
            await board.SetAsync("mode", "am", cancellation).ConfigureAwait(false);   // no pitch shift
            await board.SetAsync("low", -NarrowHz, cancellation).ConfigureAwait(false);
            await board.SetAsync("high", NarrowHz, cancellation).ConfigureAwait(false);
            await board.SetAsync("agc", "off", cancellation).ConfigureAwait(false);
            await board.SetAsync("hang", 0, cancellation).ConfigureAwait(false);
            await board.SetAsync("decay", 200, cancellation).ConfigureAwait(false);
            await board.SetAsync("offset", Math.Round(offset), cancellation).ConfigureAwait(false);
            double signal = await FilterSweep.SettledSMeterAsync(board, cancellation).ConfigureAwait(false);

            await board.SetAsync("offset", Math.Round(-offset), cancellation).ConfigureAwait(false);   // the mirror
            double imageBefore = await FilterSweep.SettledSMeterAsync(board, cancellation).ConfigureAwait(false);
            progress?.Report(FormattableString.Invariant($"carrier {signal:0.0} dBFS, image {imageBefore:0.0} dBFS -- searching"));
            if (signal - imageBefore < 3)
            {
                throw new InvalidOperationException(
                    "No carrier to null: tune a steady signal (a generator, S-meter well above the noise) into the channel first.");
            }

            double gain = await board.GetNumberAsync("iq-gain", cancellation).ConfigureAwait(false);
            double phase = await board.GetNumberAsync("iq-phase", cancellation).ConfigureAwait(false);
            double gainSpan = 3, phaseSpan = 10;
            for (int round = 0; round < 3; round++)
            {
                double g0 = gain;
                phase = await GoldenAsync(board, "iq-phase", Math.Max(-10, phase - phaseSpan), Math.Min(10, phase + phaseSpan), 0.02, cancellation)
                    .ConfigureAwait(false);
                gain = await GoldenAsync(board, "iq-gain", Math.Max(-3, g0 - gainSpan), Math.Min(3, g0 + gainSpan), 0.01, cancellation)
                    .ConfigureAwait(false);
                gainSpan /= 4;
                phaseSpan /= 4;
                progress?.Report(FormattableString.Invariant($"round {round + 1}: gain {gain:0.00} dB, phase {phase:0.00} deg"));
            }

            gain = Math.Round(gain, 2);
            phase = Math.Round(phase, 2);
            await board.SetAsync("iq-gain", gain, cancellation).ConfigureAwait(false);
            await board.SetAsync("iq-phase", phase, cancellation).ConfigureAwait(false);
            double imageAfter = await FilterSweep.SettledSMeterAsync(board, cancellation).ConfigureAwait(false);
            return new ImageNullResult(gain, phase, signal, imageBefore, imageAfter);
        }
        finally
        {
            // the mode first (it loads its edges), then pitch, the edges (high,
            // then low: an edge pushes the other along), offset and AGC
            await board.SetAsync("mode", before["mode"]).ConfigureAwait(false);
            await board.SetAsync("pitch", before["pitch"]).ConfigureAwait(false);
            await board.SetAsync("low", -ChannelFilter.EdgeLimit).ConfigureAwait(false);
            await board.SetAsync("high", before["high"]).ConfigureAwait(false);
            await board.SetAsync("low", before["low"]).ConfigureAwait(false);
            await board.SetAsync("offset", before["offset"]).ConfigureAwait(false);
            await board.SetAsync("agc", before["agc"]).ConfigureAwait(false);
            await board.SetAsync("hang", before["hang"]).ConfigureAwait(false);
            await board.SetAsync("decay", before["decay"]).ConfigureAwait(false);
        }
    }

    /// <summary>The value of one correction in [a, b] with the lowest image reading.</summary>
    private static async Task<double> GoldenAsync(BoardClient board, string parameter, double a, double b, double tolerance,
        CancellationToken cancellation)
    {
        const double r = 0.6180339887;
        async Task<double> At(double v)
        {
            await board.SetAsync(parameter, Math.Round(v, 2), cancellation).ConfigureAwait(false);
            return await FilterSweep.SettledSMeterAsync(board, cancellation).ConfigureAwait(false);
        }

        double c = b - r * (b - a), d = a + r * (b - a);
        double fc = await At(c).ConfigureAwait(false), fd = await At(d).ConfigureAwait(false);
        while (b - a > tolerance)
        {
            if (fc < fd)
            {
                b = d; d = c; fd = fc;
                c = b - r * (b - a);
                fc = await At(c).ConfigureAwait(false);
            }
            else
            {
                a = c; c = d; fc = fd;
                d = a + r * (b - a);
                fd = await At(d).ConfigureAwait(false);
            }
        }

        return (a + b) / 2;
    }
}
