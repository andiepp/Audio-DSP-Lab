// RxTuneCheck <board port> <front-end port> <carrier Hz>
//
// Bench check of the receiver's tuning (design/receiver.md, R4): the board
// runs the receiver, the SDR front end's Uno sets the LO, a generator puts a
// carrier on the band.
//   1. The carrier on the dial from three front-end positions: the FM tuning
//      indicator (narrow filter) must read the same small offset each time --
//      the dial arithmetic and LO = 4 x f hold wherever the front end sits.
//   2. USB, 100 Hz steps across the window's upper edge with the carrier in
//      the passband: the front end re-centres once, the S-meter stays.
using System.Globalization;
using AudioDspLab;
using AudioDspLab.FrontEnd;
using AudioDspLab.Protocol;

if (args.Length is not (3 or 4))
{
    Console.WriteLine("RxTuneCheck <board port> <front-end port> <carrier Hz> [core]");
    return 2;
}

if (args.Length == 4 && args[3] == "core")
{
    return await CoreCheck.RunAsync(args[0], args[1], long.Parse(args[2], CultureInfo.InvariantCulture));
}

long carrier = long.Parse(args[2], CultureInfo.InvariantCulture);
using var board = new BoardClient(new SerialLineTransport(args[0]));
await board.ConnectAsync();
await using var fe = new FrontEndClient(new SerialFrontEndPort(args[1]));
await fe.OpenAsync();
Console.WriteLine($"board {board.Info}, front end '{fe.Identity}', {await fe.StatusAsync()}");

await board.SelectApplicationAsync("receiver");
foreach (var (p, v) in new[]
         {
             ("test", "off"), ("input", "iq"), ("iq-gain", "0.41"), ("iq-phase", "1"), ("nr", "off"),
             ("agc", "fast"), ("hang", "0"), ("decay", "200"), ("stopband", "80"),
         })
{
    await board.SetAsync(p, v);
}

var tuner = ReceiverTuner.ForBoard(board, fe);
await tuner.InitializeAsync();

async Task<double[]> Meter(int waitMs = 800, int n = 5)
{
    await Task.Delay(waitMs);
    var sum = new double[7];
    for (int i = 0; i < n; i++)
    {
        var v = (await board.SendAsync("meter")).Values;
        for (int k = 0; k < sum.Length && k < v.Count; k++)
        {
            sum[k] += double.Parse(v[k], CultureInfo.InvariantCulture);
        }

        await Task.Delay(100);
    }

    return [.. sum.Select(s => s / n)];
}

// 1. the carrier on the dial, the front end in three places
Console.WriteLine("\n1. FM tuning indicator, 300 Hz filter, carrier on the dial");
await board.SetAsync("mode", "fm");
await board.SetAsync("width", "300");
var readings = new List<double>();
foreach (int approach in new[] { 0, -6000, 7500 })
{
    // tune beside the carrier first, so the front end lands where wanted
    await tuner.TuneAsync(carrier + approach);
    SdrTuning t = await tuner.TuneAsync(carrier);
    var fs = await fe.StatusAsync();
    double[] m = await Meter(1500, 8);
    readings.Add(m[2]);
    Console.WriteLine($"   front end {fs.FrequencyHz} Hz (LO {fs.LoHz} Hz = 4 x f: {fs.LoHz == 4 * fs.FrequencyHz}), " +
                      $"offset {t.OffsetHz} Hz: S-meter {m[0]:0.0} dBFS, carrier {m[2]:+0.0;-0.0} Hz from the dial");
}

double spread = readings.Max() - readings.Min();
Console.WriteLine($"   spread {spread:0.0} Hz -> {(spread < 2 ? "consistent" : "INCONSISTENT")}");

// 2. across the window's upper edge with the carrier in the USB passband
Console.WriteLine("\n2. USB 300 .. 2700 Hz, 100 Hz steps across the window's edge");
await board.SetAsync("mode", "usb");
await board.SetAsync("width", "2400");
long start = carrier - 3000, edgeDial = carrier - 1500;
// the front end placed so the offset reaches 18 kHz at edgeDial
await tuner.TuneAsync(edgeDial - SdrTuner.WindowHigh + SdrTuner.Centre);
int retunesBefore = tuner.Retunes;
var levels = new List<(long Dial, double S, long Fe)>();
for (long dial = start; dial <= carrier - 400; dial += 100)
{
    await tuner.TuneAsync(dial);
    double[] m = await Meter(500, 3);
    levels.Add((dial, m[0], tuner.FrontEndHz));
}

foreach (var (dial, s, f) in levels)
{
    long audio = carrier - dial;
    string mark = f != levels[0].Fe ? " (front end moved)" : "";
    Console.WriteLine($"   dial {dial}  audio {audio,5} Hz  front end {f}  S-meter {s,7:0.0} dBFS{mark}");
}

var inPass = levels.Where(l => carrier - l.Dial is >= 500 and <= 2500).Select(l => l.S).ToList();
Console.WriteLine($"   re-centres {tuner.Retunes - retunesBefore}; S-meter in the passband {inPass.Min():0.0} .. {inPass.Max():0.0} dBFS " +
                  $"(spread {inPass.Max() - inPass.Min():0.0} dB)");

await board.SetAsync("agc", "medium");
await board.SetAsync("iq-gain", "0");
await board.SetAsync("iq-phase", "0");
return 0;
