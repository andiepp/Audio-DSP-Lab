using System.Globalization;
using System.Numerics;
using AudioDspLab.Protocol;
using AudioDspLab.Receiver;

namespace AudioDspLab.Simulation;

/// <summary>
/// The receiver application (0x07) on the simulated board: its parameters
/// with the firmware's dependencies (a mode loads its filter edges, an AGC
/// speed its hang and decay, edges and width follow each other, a design
/// replaces a downloaded filter), the S-meter computed from the test tone
/// through the filter's real response, rxfilter with downloads, and the
/// meter events.
/// </summary>
public sealed partial class SimulatedBoard
{
    private const double NoiseFloorDbfs = -101;   // the open-input floor in 2.4 kHz (R2)
    private static readonly string[] RxModes = ["off", "am", "usb", "lsb", "cw", "fm"];

    private Complex[]? downloadedFilter;
    private List<float>? loading;
    private int loadingTaps;
    private bool loadingComplex;
    private int filterCount = 1;
    private long meterSentAt;
    private readonly Random noise = new(7);

    private string RxMode => RxModes[(int)values["mode"]];

    // a value the firmware itself changes: stored, and reported as an event
    private void SetDependent(string name, double value)
    {
        if (values.TryGetValue(name, out double old) && old == value)
        {
            return;
        }

        values[name] = value;
        pendingValueEvents.Add(name);
    }

    private static (double Low, double High) EdgesForWidth(string mode, double width, double pitch)
    {
        double low, high;
        switch (mode)
        {
            case "usb": low = 300; high = 300 + width; break;
            case "lsb": low = -300 - width; high = -300; break;
            case "cw": low = pitch - Math.Floor(width / 2); high = low + width; break;
            default: low = -Math.Floor(width / 2); high = low + width; break;
        }

        low = Math.Clamp(low, -ChannelFilter.EdgeLimit, ChannelFilter.EdgeLimit - 50);
        high = Math.Clamp(high, low + 50, ChannelFilter.EdgeLimit);
        return (low, high);
    }

    private static double PresetWidth(string mode) => mode switch
    {
        "usb" or "lsb" => 2400,
        "cw" => 500,
        "fm" => 2 * ChannelFilter.EdgeLimit,
        _ => 6000,
    };

    /// <summary>
    /// The receiver's side effects of a set, before the value is stored; false
    /// refuses it. The value may be adjusted (the width to the edges' room).
    /// </summary>
    private bool ReceiverSet(string name, ref double value)
    {
        bool filterChanged = false;
        switch (name)
        {
            case "mode" when value != values["mode"]:
            {
                (double low, double high) = EdgesForWidth(RxModes[(int)value], PresetWidth(RxModes[(int)value]), values["pitch"]);
                SetDependent("low", low);
                SetDependent("high", high);
                SetDependent("width", high - low);
                filterChanged = true;
                break;
            }
            case "low":
                if (values["high"] < value + 50)
                {
                    SetDependent("high", value + 50);
                }

                SetDependent("width", values["high"] - value);
                filterChanged = true;
                break;
            case "high":
                if (values["low"] > value - 50)
                {
                    SetDependent("low", value - 50);
                }

                SetDependent("width", value - values["low"]);
                filterChanged = true;
                break;
            case "width":
            {
                (double low, double high) = EdgesForWidth(RxMode, value, values["pitch"]);
                SetDependent("low", low);
                SetDependent("high", high);
                value = high - low;
                filterChanged = true;
                break;
            }
            case "pitch" when RxMode == "cw":
            {
                double shift = value - values["pitch"];
                SetDependent("low", values["low"] + shift);
                SetDependent("high", values["high"] + shift);
                filterChanged = true;
                break;
            }
            case "stopband":
                filterChanged = true;
                break;
            case "agc" when value != values["agc"] && value > 0:
                SetDependent("hang", value switch { 1 => 0.1, 2 => 0.5, _ => 1.5 });
                SetDependent("decay", value switch { 1 => 100, 2 => 20, _ => 10 });
                break;
            case "test-key" when value is > 0 and < 100:
                value = 100;
                break;
            case "filter" when value == 1:
                return values["filter"] == 1;              // only a download makes it "downloaded"
            case "filter":
                filterChanged = true;
                break;
        }

        if (filterChanged)
        {
            DesignAgain();
        }

        return true;
    }

    private void DesignAgain()
    {
        downloadedFilter = null;
        loading = null;
        SetDependent("filter", 0);
        filterCount++;
    }

    private Complex[] ActiveFilter(out double transition)
    {
        if (downloadedFilter is not null)
        {
            transition = 0;
            return downloadedFilter;
        }

        ChannelFilterDesign design = ChannelFilter.Design(values["low"], values["high"], values["stopband"]);
        transition = design.TransitionHz;
        return design.Coefficients;
    }

    // the S-meter: the test tone through the filter, or the noise floor
    private double SMeter()
    {
        double floor = NoiseFloorDbfs + 10 * Math.Log10(Math.Max(values["width"], 100) / 2400) + (noise.NextDouble() - 0.5);
        if (values["test"] == 0)
        {
            return floor;
        }

        Complex[] h = ActiveFilter(out _);
        double f = values["test-offset"] + (RxMode == "cw" ? values["pitch"] : 0);
        double tone = values["test-level"] + ChannelFilter.AtSMeterDb(h, f);
        return 10 * Math.Log10(Math.Pow(10, tone / 10) + Math.Pow(10, floor / 10));
    }

    private string MeterText()
    {
        double s = SMeter();
        double gain = values["agc"] > 0 ? Math.Min(-12 - s, values["agc-gain"]) : values["agc-gain"];
        bool open = s >= values["squelch"];
        double audio = s + gain - 3;
        Complex[] h = ActiveFilter(out _);
        double cpu = 13 + h.Length / 100.0 + (values["nr"] == 2 ? 11 : values["nr"] == 1 ? 6 : 0);
        double nrGain = values["nr"] == 2 ? -Math.Min(values["nr-level"], 12) : 0;
        return string.Join(' ',
            ProtocolText.FormatNumber(Math.Round(s, 1)), ProtocolText.FormatNumber(Math.Round(gain, 1)), "0",
            open ? "1" : "0", ProtocolText.FormatNumber(Math.Round(audio, 1)), ProtocolText.FormatNumber(Math.Round(cpu, 1)),
            ProtocolText.FormatNumber(Math.Round(nrGain, 1)));
    }

    private void Meter()
    {
        if (Running(Applications.Receiver))
        {
            Ok(MeterText());
        }
    }

    private void ReceiverEvents()
    {
        if (application == Applications.Receiver && state == BoardStatus.Running && Now - meterSentAt >= 200)
        {
            meterSentAt = Now;
            Emit($"! meter {MeterText()}");
        }
    }

    private void RxFilter(IReadOnlyList<string> args)
    {
        if (!Running(Applications.Receiver))
        {
            return;
        }

        switch (args.Count > 0 ? args[0].ToLowerInvariant() : string.Empty)
        {
            case "":
            {
                Complex[] h = ActiveFilter(out double transition);
                Ok(FormattableString.Invariant(
                    $"{(downloadedFilter is null ? "designed" : "downloaded")} {h.Length} {transition:0} ready {filterCount}"));
                break;
            }
            case "load" when args.Count == 3
                             && int.TryParse(args[1], NumberStyles.Integer, CultureInfo.InvariantCulture, out int taps)
                             && taps is >= 1 and <= ChannelFilter.MaxTaps
                             && args[2] is "real" or "complex":
                loading = [];
                loadingTaps = taps;
                loadingComplex = args[2] == "complex";
                Ok();
                break;
            case "data" when args.Count is >= 2 and <= 17:
            {
                if (loading is null)
                {
                    Error("not-loading", "no filter download begun (rxfilter load)");
                    return;
                }

                foreach (string text in args.Skip(1))
                {
                    if (!float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out float v))
                    {
                        BadArgument();
                        return;
                    }

                    loading.Add(v);
                }

                Ok((loading.Count / (loadingComplex ? 2 : 1)).ToString(CultureInfo.InvariantCulture));
                break;
            }
            case "end" when args.Count == 2:
            {
                if (loading is null)
                {
                    Error("not-loading", "no filter download begun (rxfilter load)");
                    return;
                }

                List<float> data = loading;
                loading = null;
                int per = loadingComplex ? 2 : 1;
                if (data.Count != loadingTaps * per)
                {
                    Error("count-wrong", "the filter download's tap count does not match");
                    return;
                }

                byte[] bytes = new byte[data.Count * 4];
                for (int i = 0; i < data.Count; i++)
                {
                    BitConverter.TryWriteBytes(bytes.AsSpan(4 * i), data[i]);
                }

                string crcText = args[1];
                uint crc = crcText.StartsWith("0x", StringComparison.OrdinalIgnoreCase)
                    ? uint.Parse(crcText[2..], NumberStyles.HexNumber, CultureInfo.InvariantCulture)
                    : uint.Parse(crcText, CultureInfo.InvariantCulture);
                if (ReceiverRequests.Crc32(bytes) != crc)
                {
                    Error("crc-wrong", "the filter download's CRC-32 does not match");
                    return;
                }

                downloadedFilter = [.. Enumerable.Range(0, loadingTaps)
                    .Select(k => new Complex(data[k * per], per == 2 ? data[k * per + 1] : 0))];
                filterCount++;
                SetDependent("filter", 1);
                Ok(loadingTaps.ToString(CultureInfo.InvariantCulture));
                break;
            }
            default:
                BadArgument();
                break;
        }
    }
}
