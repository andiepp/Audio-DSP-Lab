using System.Globalization;
using System.Text;
using AudioDspLab.Protocol;
using AudioDspLab.Receiver;

namespace AudioDspLab.Simulation;

/// <summary>
/// The receiver's decoders on the simulated board (design/rtty-fax.md): with
/// "decoder rtty" a DWD-like text arrives at 50 baud (about 6.7 characters a
/// second), with "decoder fax" a weather chart at the lines per minute set,
/// with its start tone, phasing and stop tone; the status twice a second.
/// "rtty" and "fax events" as the firmware answers them.
/// </summary>
public sealed partial class SimulatedBoard
{
    private const string RttyDemoText =
        "RYRYRYRY CQ CQ CQ DE DDH47 DDH9 DDH8\n" +
        "FREQUENCIES 147.3 KHZ 11039 KHZ 14467.3 KHZ\n" +
        "ZCZC 129\n" +
        "FEBQ52 EDZW 211200\n" +
        "MITTELFRIST - SEEWETTERBERICHT FUER DIE OSTSEE\n" +
        "DES DEUTSCHEN WETTERDIENSTES HAMBURG\n" +
        "WETTERLAGE: TIEF 985 NORDNORWEGEN, ZIEHT OSTWAERTS.\n" +
        "WESTLICHE OSTSEE: WEST 5 BIS 6, SPAETER NORDWEST 7, SEEGANG 2 METER.\n" +
        "NNNN\n";

    private const int FaxDemoChartLines = 360;
    private const int FaxDemoPhasingLines = 20;
    private const int FaxDemoWhiteLines = 20;

    private readonly StringBuilder rttyHistory = new();
    private readonly StringBuilder rttyPending = new();
    private int rttyTextAt;
    private long rttyNextCharAt, rttySentAt, decoderSentAt, faxNextLineAt;
    private long rttyGood, rttyBad;
    private long faxLines;
    private int faxStageLine;
    private int faxPhase = -1;
    private bool faxPhasing;
    private int faxEventsWidth;

    private string DecoderKind => application == Applications.Receiver && state == BoardStatus.Running
        ? ((int)values.GetValueOrDefault("decoder", 0)) switch { 1 => "rtty", 2 => "fax", _ => "off" }
        : "off";

    private int FaxWidth => (int)Math.Round((values.GetValueOrDefault("fax-ioc", 1) == 0 ? 288 : 576) * Math.PI);

    private double FaxLineMilliseconds => 60_000.0 / ((int)values.GetValueOrDefault("fax-lpm", 2) switch { 0 => 60, 1 => 90, 3 => 240, _ => 120 });

    private string DecoderText() => DecoderKind switch
    {
        "rtty" => Invariant($"rtty 0.93 -3 reverse {rttyGood} {rttyBad}"),
        "fax" => Invariant($"fax {faxLines} 12 0.00 0.00 {(faxPhasing ? "phasing" : "lines")} {faxPhase}"),
        _ => "off",
    };

    private static string Invariant(FormattableString text) => text.ToString(CultureInfo.InvariantCulture);

    private void Rtty()
    {
        if (!Running(Applications.Receiver))
        {
            return;
        }

        string text = rttyHistory.Length > 1000 ? rttyHistory.ToString(rttyHistory.Length - 1000, 1000) : rttyHistory.ToString();
        string[] lines = text.Split('\n');
        for (int i = 0; i < lines.Length; i++)
        {
            bool ends = i < lines.Length - 1;
            if (ends || lines[i].Length > 0)
            {
                Data($"{ProtocolText.Quote(lines[i])} {(ends ? 1 : 0)}");
            }
        }

        Ok(Invariant($"{rttyHistory.Length} {DecoderText()}"));
    }

    private void Fax(IReadOnlyList<string> args)
    {
        if (args.Count == 2 && args[0] == "events")
        {
            if (args[1] == "off")
            {
                faxEventsWidth = 0;
            }
            else if (args[1] == "full")
            {
                faxEventsWidth = 1810;
            }
            else if (int.TryParse(args[1], NumberStyles.Integer, CultureInfo.InvariantCulture, out int width) && width is >= 16 and <= 1810)
            {
                faxEventsWidth = width;
            }
            else
            {
                Error("bad-argument", "missing or malformed argument");
                return;
            }

            Ok(faxEventsWidth == 0 ? "off" : faxEventsWidth.ToString(CultureInfo.InvariantCulture));
            return;
        }

        if (args.Count != 0)
        {
            Error("bad-argument", "missing or malformed argument");
            return;
        }

        if (Running(Applications.Receiver))
        {
            Ok(DecoderText());
        }
    }

    private void DecoderEvents()
    {
        long now = Now;
        string kind = DecoderKind;
        if (kind == "rtty")
        {
            while (now >= rttyNextCharAt)
            {
                char c = RttyDemoText[rttyTextAt];
                rttyTextAt = (rttyTextAt + 1) % RttyDemoText.Length;
                rttyHistory.Append(c);
                rttyPending.Append(c);
                rttyGood++;
                rttyNextCharAt = Math.Max(rttyNextCharAt, now - 1000) + 150;
            }

            if (now - rttySentAt >= 200 && rttyPending.Length > 0)
            {
                rttySentAt = now;
                string[] pieces = rttyPending.ToString().Split('\n');
                rttyPending.Clear();
                for (int i = 0; i < pieces.Length; i++)
                {
                    bool ends = i < pieces.Length - 1;
                    if (ends || pieces[i].Length > 0)
                    {
                        Emit($"! rtty {ProtocolText.Quote(pieces[i])} {(ends ? 1 : 0)}");
                    }
                }
            }
        }
        else
        {
            rttyNextCharAt = now;
        }

        if (kind == "fax")
        {
            while (now >= faxNextLineAt)
            {
                FaxDemoLine();
                faxNextLineAt = Math.Max(faxNextLineAt, now - 2000) + (long)FaxLineMilliseconds;
            }
        }
        else
        {
            faxNextLineAt = now;
        }

        if (kind != "off" && now - decoderSentAt >= 500)
        {
            decoderSentAt = now;
            Emit($"! decoder {DecoderText()}");
        }
    }

    // start, phasing, the chart, stop, white paper, again
    private void FaxDemoLine()
    {
        int width = FaxWidth;
        int cycle = FaxDemoPhasingLines + FaxDemoChartLines + FaxDemoWhiteLines;
        int at = faxStageLine;
        faxStageLine = (faxStageLine + 1) % cycle;
        if (at == 0)
        {
            Emit("! fax start");
            faxPhasing = true;
            faxPhase = -1;
        }

        if (at == FaxDemoPhasingLines)
        {
            faxPhasing = false;
            faxPhase = 45;
            Emit("! fax phase 45");
        }

        if (at == FaxDemoPhasingLines + FaxDemoChartLines)
        {
            Emit("! fax stop");
        }

        var pixels = new byte[width];
        for (int x = 0; x < width; x++)
        {
            pixels[x] = at < FaxDemoPhasingLines ? (byte)(x < width / 20 ? 255 : 0)
                : at < FaxDemoPhasingLines + FaxDemoChartLines ? DemoChart(x / (double)width, (at - FaxDemoPhasingLines) / (double)FaxDemoChartLines)
                : (byte)255;
        }

        long number = faxLines++;
        if (eventsOn && faxEventsWidth > 0)
        {
            int w = Math.Min(faxEventsWidth, width);
            var line = new byte[w];
            for (int j = 0; j < w; j++)
            {
                int from = j * width / w, to = Math.Max((j + 1) * width / w, from + 1);
                int sum = 0;
                for (int i = from; i < to; i++)
                {
                    sum += pixels[i];
                }

                line[j] = (byte)(sum / (to - from));
            }

            string text = FaxLineAssembler.Encode(line);
            int parts = (text.Length + 299) / 300;
            for (int p = 0; p < parts; p++)
            {
                string piece = text.Substring(p * 300, Math.Min(300, text.Length - p * 300));
                Emit(Invariant($"! fax line {number} {p + 1} {parts} {w} {piece}"));
            }
        }
    }

    // a surface analysis: isobars around a low and a high, a frame of meridians
    // and parallels, a front; black lines on white paper
    private static byte DemoChart(double x, double y)
    {
        if (x < 0.03 || x > 0.97)
        {
            return 255;
        }

        if (Math.Abs(x - 0.03) < 0.002 || Math.Abs(x - 0.97) < 0.002 || y < 0.01 || y > 0.99)
        {
            return 0;
        }

        if (Math.Abs((x * 8) - Math.Round(x * 8)) < 0.0025 || Math.Abs((y * 6) - Math.Round(y * 6)) < 0.004)
        {
            return 150;
        }

        double low = 1000 - 28 * Math.Exp(-(Square((x - 0.35) / 0.18) + Square((y - 0.4) / 0.25)));
        double high = 18 * Math.Exp(-(Square((x - 0.75) / 0.2) + Square((y - 0.65) / 0.3)));
        double pressure = low + high + 6 * y;
        double phase = pressure / 4 - Math.Floor(pressure / 4);
        if (phase < 0.035 || phase > 0.965)
        {
            return 0;
        }

        double front = 0.42 + 0.25 * y + 0.04 * Math.Sin(y * 20);
        return Math.Abs(x - front) < 0.003 && y > 0.3 ? (byte)0 : (byte)255;
    }

    private static double Square(double v) => v * v;
}
