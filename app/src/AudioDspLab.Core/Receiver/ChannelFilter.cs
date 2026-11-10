using System.Numerics;

namespace AudioDspLab.Receiver;

/// <summary>A channel filter as the firmware designs it, with its numbers.</summary>
/// <param name="Coefficients">Natural order: tap k multiplies x[m - k].</param>
/// <param name="Widened">The tap limit forced a wider transition than the rule asks for.</param>
public sealed record ChannelFilterDesign(
    double Low,
    double High,
    double StopDb,
    int Taps,
    double TransitionHz,
    double Beta,
    bool Widened,
    Complex[] Coefficients)
{
    /// <summary>Delay through the filter, ms (half the length at 12 kHz).</summary>
    public double GroupDelayMs => (Taps - 1) / 2.0 / ChannelFilter.SampleRate * 1000;

    public double Bandwidth => High - Low;
}

/// <summary>
/// The channel filter design of the firmware's RxChain.cpp (the DSP-Box's
/// chan.c), ported from DSP-Box-Lab: a Kaiser-windowed sinc low-pass of half
/// the bandwidth (edges at -6 dB), shifted to the passband centre. The
/// transition is a quarter of the bandwidth, held to 60 .. 300 Hz; the length
/// follows Kaiser's formula and is capped at 511 taps (the transition then
/// widens). Also the down-converter's low-pass, which the test tone passes on
/// its way to the S-meter.
/// </summary>
public static class ChannelFilter
{
    public const double SampleRate = 12000;
    public const int MaxTaps = 511;

    /// <summary>|edge| limit, Hz: the down-converter rolls off above (firmware RX_EDGE_MAX).</summary>
    public const double EdgeLimit = 5500;

    private const double InputRate = 48000;
    private const int DdcTaps = 43;
    private static readonly double[] Ddc = KaiserLowpass(DdcTaps, 6000, InputRate, 80);

    public static ChannelFilterDesign Design(double low, double high, double stopDb)
    {
        double bw = high - low, a = stopDb;
        double tw = Math.Clamp(0.25 * bw, 60, 300);
        int n = KaiserTaps(a, tw, SampleRate);
        bool widened = false;
        if (n > MaxTaps)
        {
            n = MaxTaps;
            tw = (a - 7.95) / (2.285 * (n - 1)) * SampleRate / (2 * Math.PI);
            widened = true;
        }

        double beta = KaiserBeta(a), i0b = BesselI0(beta);
        double wc = 2 * Math.PI * (0.5 * bw) / SampleRate, m = 0.5 * (n - 1), fc = 0.5 * (low + high);
        var proto = new double[n];
        double sum = 0;
        for (int k = 0; k < n; k++)
        {
            double t = k - m, r = t / m;
            double sinc = t == 0 ? wc / Math.PI : Math.Sin(wc * t) / (Math.PI * t);
            proto[k] = sinc * BesselI0(beta * Math.Sqrt(Math.Max(0, 1 - r * r))) / i0b;
            sum += proto[k];
        }

        var h = new Complex[n];
        for (int k = 0; k < n; k++)
        {
            double phase = 2 * Math.PI * fc * (k - m) / SampleRate;
            h[k] = Complex.FromPolarCoordinates(proto[k] / sum, phase);
        }

        return new ChannelFilterDesign(low, high, stopDb, n, tw, beta, widened, h);
    }

    /// <summary>|H(f)| in dB of a complex FIR at 12 kHz; f relative to the carrier.</summary>
    public static double ResponseDb(IReadOnlyList<Complex> h, double f)
    {
        double w = -2 * Math.PI * f / SampleRate;
        Complex sum = Complex.Zero;
        for (int k = 0; k < h.Count; k++)
        {
            sum += h[k] * Complex.FromPolarCoordinates(1, w * k);
        }

        return 20 * Math.Log10(Math.Max(sum.Magnitude, 1e-9));
    }

    /// <summary>The down-converter low-pass at baseband offset f, dB.</summary>
    public static double DownConverterDb(double f)
    {
        double w = -2 * Math.PI * f / InputRate;
        Complex sum = Complex.Zero;
        for (int k = 0; k < Ddc.Length; k++)
        {
            sum += Ddc[k] * Complex.FromPolarCoordinates(1, w * k);
        }

        return 20 * Math.Log10(Math.Max(sum.Magnitude, 1e-9));
    }

    /// <summary>What the S-meter shows for a test tone at f, relative to its level: filter plus down-converter.</summary>
    public static double AtSMeterDb(IReadOnlyList<Complex> h, double f) => ResponseDb(h, f) + DownConverterDb(f);

    public static int KaiserTaps(double a, double df, double fs)
    {
        double dw = 2 * Math.PI * df / fs;
        int n = (int)Math.Ceiling((a - 7.95) / (2.285 * dw)) + 1;
        return n | 1;
    }

    public static double KaiserBeta(double a) =>
        a > 50 ? 0.1102 * (a - 8.7)
        : a >= 21 ? 0.5842 * Math.Pow(a - 21, 0.4) + 0.07886 * (a - 21)
        : 0;

    public static double BesselI0(double x)
    {
        double sum = 1, term = 1, q = x * x / 4;
        for (int k = 1; k < 60; k++)
        {
            term *= q / ((double)k * k);
            sum += term;
            if (term < sum * 1e-14)
            {
                break;
            }
        }

        return sum;
    }

    private static double[] KaiserLowpass(int n, double fc, double fs, double a)
    {
        double beta = KaiserBeta(a), i0b = BesselI0(beta), m = (n - 1) / 2.0, wc = 2 * Math.PI * fc / fs, sum = 0;
        var h = new double[n];
        for (int k = 0; k < n; k++)
        {
            double t = k - m, r = t / m;
            double sinc = t == 0 ? wc / Math.PI : Math.Sin(wc * t) / (Math.PI * t);
            h[k] = sinc * BesselI0(beta * Math.Sqrt(Math.Max(0, 1 - r * r))) / i0b;
            sum += h[k];
        }

        for (int k = 0; k < n; k++)
        {
            h[k] /= sum;
        }

        return h;
    }
}
