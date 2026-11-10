using System.Numerics;
using System.Text;
using AudioDspLab.Protocol;
using AudioDspLab.Receiver;
using AudioDspLab.Simulation;

namespace AudioDspLab.Tests;

public sealed class ReceiverTests : IAsyncLifetime
{
    private readonly SimulatedBoard board = new() { ApplicationStartTime = TimeSpan.FromMilliseconds(20) };
    private BoardClient client = null!;

    public async Task InitializeAsync()
    {
        client = new BoardClient(board);
        await client.ConnectAsync();
        await client.SelectApplicationAsync(Applications.Receiver);
    }

    public Task DisposeAsync()
    {
        client.Dispose();
        return Task.CompletedTask;
    }

    [Fact]
    public void TheDesignMatchesTheFirmwaresNumbers()
    {
        // measured on the board (design/receiver.md, R2): USB 203 taps, 300 Hz; 240 Hz at 100 dB 511 taps, 151 Hz
        ChannelFilterDesign usb = ChannelFilter.Design(300, 2700, 80);
        Assert.Equal(203, usb.Taps);
        Assert.Equal(300, usb.TransitionHz, 6);
        ChannelFilterDesign narrow = ChannelFilter.Design(-120, 120, 100);
        Assert.Equal(511, narrow.Taps);
        Assert.True(narrow.Widened);
        Assert.Equal(151, narrow.TransitionHz, 0);
        Assert.InRange(ChannelFilter.ResponseDb(usb.Coefficients, 1500), -0.01, 0.01);
        Assert.True(ChannelFilter.ResponseDb(usb.Coefficients, 0) < -80);
    }

    [Fact]
    public void TheCrcIsZlibs()
    {
        Assert.Equal(0xCBF43926u, ReceiverRequests.Crc32(Encoding.ASCII.GetBytes("123456789")));
        // the float32 bytes in the order sent, re then im
        (float[] values, bool complex, uint crc) = ReceiverRequests.Prepare([new Complex(1, 0), new Complex(0.5, -0.25)]);
        Assert.True(complex);
        Assert.Equal([1f, 0f, 0.5f, -0.25f], values);
        Assert.Equal(ReceiverRequests.Crc32([0, 0, 0x80, 0x3F, 0, 0, 0, 0, 0, 0, 0, 0x3F, 0, 0, 0x80, 0xBE]), crc);
        Assert.False(ReceiverRequests.Prepare([new Complex(1, 0)]).IsComplex);
    }

    [Fact]
    public async Task AModeLoadsItsFilter()
    {
        await client.SetAsync("mode", "cw");
        Assert.Equal("350", await client.GetAsync("low"));
        Assert.Equal("850", await client.GetAsync("high"));
        Assert.Equal("500", await client.GetAsync("width"));
        await client.SetAsync("pitch", 700);                 // CW: the filter follows the pitch
        Assert.Equal("450", await client.GetAsync("low"));
        await client.SetAsync("agc", "slow");
        Assert.Equal("1.5", await client.GetAsync("hang"));
        Assert.Equal("10", await client.GetAsync("decay"));
    }

    [Fact]
    public async Task ADownloadGoesLiveAndADesignReplacesIt()
    {
        Complex[] h = MaskDesign.Design([new MaskBand(500, 1500, 0), new MaskBand(1800, 2500, -6)], 70, 150);
        var sent = new List<int>();
        await client.DownloadFilterAsync(h, new Progress<int>(sent.Add));
        ReceiverFilterStatus status = await client.GetFilterAsync();
        Assert.True(status.Downloaded);
        Assert.Equal(h.Length, status.Taps);
        Assert.Equal("downloaded", await client.GetAsync("filter"));

        await client.SetAsync("width", 2000);
        Assert.False((await client.GetFilterAsync()).Downloaded);
        Assert.Equal("designed", await client.GetAsync("filter"));
        await Assert.ThrowsAsync<BoardErrorException>(() => client.SetAsync("filter", "downloaded"));
    }

    [Fact]
    public async Task AWrongCrcLeavesTheFilter()
    {
        await client.SendAsync("rxfilter load 1 real");
        await client.SendAsync("rxfilter data 1");
        BoardErrorException e = await Assert.ThrowsAsync<BoardErrorException>(() => client.SendAsync("rxfilter end 0x12345678"));
        Assert.Equal("crc-wrong", e.Code);
        Assert.False((await client.GetFilterAsync()).Downloaded);
    }

    [Fact]
    public async Task TheSweepMeasuresTheDesign()
    {
        await client.SetAsync("agc", "medium");
        IReadOnlyList<FilterPoint> points = await FilterSweep.MeasureAsync(client, [500, 1000, 1500, 2000, 2500, -1000, 4000]);
        ChannelFilterDesign design = ChannelFilter.Design(300, 2700, 80);
        foreach (FilterPoint p in points.Where(p => p.Hz is >= 500 and <= 2500))
        {
            Assert.InRange(p.GainDb - ChannelFilter.AtSMeterDb(design.Coefficients, p.Hz), -0.6, 0.6);
        }

        Assert.All(points.Where(p => p.Hz is -1000 or 4000), p => Assert.True(p.GainDb < -70));
        // the sweep's settings are restored
        Assert.Equal("off", await client.GetAsync("test"));
        Assert.Equal("medium", await client.GetAsync("agc"));
    }

    [Fact]
    public async Task TheMeterParses()
    {
        await client.SetAsync("test", true);
        ReceiverMeter m = await client.GetMeterAsync();
        Assert.InRange(m.SMeterDbfs, -21, -19);              // -20 dBFS at +1000 Hz in USB
        Assert.True(m.SquelchOpen);
    }
}
