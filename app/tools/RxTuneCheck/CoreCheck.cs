using System.Globalization;
using System.Numerics;
using AudioDspLab;
using AudioDspLab.FrontEnd;
using AudioDspLab.Protocol;
using AudioDspLab.Receiver;

/// <summary>
/// The Core paths the Receiver page drives, on the real board (R5): ImageNull
/// with the generator's carrier, FilterSweep against ChannelFilter's
/// prediction, a MaskDesign download measured against its own response, and
/// "design again".
/// </summary>
internal static class CoreCheck
{
    public static async Task<int> RunAsync(string boardPort, string frontEndPort, long carrier)
    {
        using var board = new BoardClient(new SerialLineTransport(boardPort));
        await board.ConnectAsync();
        await using var fe = new FrontEndClient(new SerialFrontEndPort(frontEndPort));
        await fe.OpenAsync();
        await board.SelectApplicationAsync(ReceiverRequests.Application);
        foreach (var (p, v) in new[] { ("test", "off"), ("input", "iq"), ("iq-gain", "0"), ("iq-phase", "0"), ("mode", "usb"), ("nr", "off") })
        {
            await board.SetAsync(p, v);
        }

        var tuner = ReceiverTuner.ForBoard(board, fe);
        await tuner.InitializeAsync();
        await tuner.TuneAsync(carrier - 20_000);                 // away first: the front end re-centres onto the carrier
        await tuner.TuneAsync(carrier);
        Console.WriteLine($"tuned: front end {tuner.FrontEndHz} Hz, offset {tuner.OffsetHz} Hz");

        Console.WriteLine("\n1. ImageNull");
        ImageNullResult r = await ImageNull.RunAsync(board, new Progress<string>(s => Console.WriteLine("   " + s)));
        await Task.Delay(300);
        Console.WriteLine(FormattableString.Invariant(
            $"   found {r.GainDb:+0.00;-0.00} dB, {r.PhaseDeg:+0.00;-0.00} deg: rejection {r.RejectionBeforeDb:0.0} -> {r.RejectionAfterDb:0.0} dB"));
        Console.WriteLine($"   restored: mode {await board.GetAsync("mode")}, low {await board.GetAsync("low")}, high {await board.GetAsync("high")}, " +
                          $"offset {await board.GetAsync("offset")}, agc {await board.GetAsync("agc")}");

        Console.WriteLine("\n2. FilterSweep, USB, against ChannelFilter");
        double[] freqs = [-500, 0, 200, 300, 500, 1000, 1500, 2000, 2500, 2700, 2800, 3000, 4000];
        IReadOnlyList<FilterPoint> points = await FilterSweep.MeasureAsync(board, freqs);
        ChannelFilterDesign usb = ChannelFilter.Design(300, 2700, 80);
        Report(points, p => ChannelFilter.AtSMeterDb(usb.Coefficients, p), p => p is >= 500 and <= 2500);

        Console.WriteLine("\n3. MaskDesign download (two passbands), swept against its own response");
        Complex[] h = MaskDesign.Design([new MaskBand(300, 1200, 0), new MaskBand(1600, 2500, -6)], 70, 150);
        var started = DateTime.Now;
        await board.DownloadFilterAsync(h);
        Console.WriteLine($"   {h.Length} taps in {(DateTime.Now - started).TotalSeconds:0.00} s: {await board.GetFilterAsync()}");
        points = await FilterSweep.MeasureAsync(board, [-500, 200, 500, 800, 1100, 1400, 1800, 2100, 2400, 2800, 3500]);
        Report(points, p => ChannelFilter.AtSMeterDb(h, p), p => p is >= 500 and <= 1000 or >= 1800 and <= 2300);

        Console.WriteLine("\n4. Design again");
        await board.DesignAgainAsync();
        await Task.Delay(300);
        Console.WriteLine($"   {await board.GetFilterAsync()}, filter = {await board.GetAsync("filter")}");
        return 0;
    }

    private static void Report(IReadOnlyList<FilterPoint> points, Func<double, double> predicted, Func<double, bool> inPassband)
    {
        double worst = 0;
        foreach (FilterPoint p in points)
        {
            double want = predicted(p.Hz);
            bool pass = inPassband(p.Hz);
            if (pass)
            {
                worst = Math.Max(worst, Math.Abs(p.GainDb - want));
            }

            Console.WriteLine(string.Create(CultureInfo.InvariantCulture,
                $"   {p.Hz,6:0} Hz  measured {p.GainDb,7:0.0}  predicted {Math.Max(want, -150),7:0.0} dB{(pass ? "  pass" : "")}"));
        }

        Console.WriteLine(string.Create(CultureInfo.InvariantCulture, $"   worst passband difference {worst:0.00} dB"));
    }
}
