using AudioDspLab.Protocol;
using AudioDspLab.Receiver;
using AudioDspLab.Simulation;

namespace AudioDspLab.Tests;

/// <summary>The receiver's RTTY and fax decoders on the PC side (design/rtty-fax.md).</summary>
public sealed class DecoderTests : IAsyncLifetime
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
    public void AFaxLineRoundTripsInFourBits()
    {
        // white paper: one digit and runs of up to 20 more
        byte[] white = Enumerable.Repeat((byte)255, 1810).ToArray();
        string text = FaxLineAssembler.Encode(white);
        Assert.Equal(1 + (int)Math.Ceiling(1809 / 20.0), text.Length);
        Assert.Equal(white, FaxLineAssembler.Decode(text, 1810));

        // a busy line keeps its upper four bits
        byte[] busy = [.. Enumerable.Range(0, 300).Select(x => (byte)(x * 37 % 256))];
        byte[] back = FaxLineAssembler.Decode(FaxLineAssembler.Encode(busy), 300);
        Assert.Equal(busy.Select(p => (byte)((p >> 4) * 17)), back);
    }

    [Fact]
    public void TheAssemblerPutsThePartsTogether()
    {
        var assembler = new FaxLineAssembler();
        Assert.Null(assembler.Add(["7", "1", "2", "40", "0z"]));        // 0 and 20 more
        FaxLine? line = assembler.Add(["7", "2", "2", "40", "fr"]);     // f and 12 more, then the rest stays white
        Assert.NotNull(line);
        Assert.Equal(7, line.Number);
        Assert.Equal(Enumerable.Repeat((byte)0, 21).Concat(Enumerable.Repeat((byte)255, 19)), line.Pixels);
    }

    [Fact]
    public void TheStatusIsRead()
    {
        DecoderStatus rtty = DecoderStatus.Parse(["rtty", "0.93", "-3", "reverse", "147", "2"]);
        Assert.True(rtty.IsRtty);
        Assert.Equal(0.93, rtty.Quality, 6);
        Assert.Equal(-3, rtty.AfcHz);
        Assert.True(rtty.Reverse);
        Assert.Equal(147, rtty.Good);
        DecoderStatus fax = DecoderStatus.Parse(["fax", "190", "110", "0.05", "0.00", "phasing", "-1"]);
        Assert.True(fax.IsFax);
        Assert.Equal(190, fax.Lines);
        Assert.True(fax.Phasing);
        Assert.Equal(-1, fax.Phase);
        Assert.Equal("no-memory", DecoderStatus.Parse(["no-memory"]).Kind);
    }

    [Fact]
    public void TelexMessagesAreCutOut()
    {
        var telex = new TelexMessages();
        var messages = new List<string>();
        telex.Message += messages.Add;
        telex.Add("RYRY CQ DE DDH47\nZCZC 129\nFEBQ52 EDZW 211200\nWESTLICHE OSTSEE: WEST 5\nNNNN\nRYRY");
        Assert.Single(messages);
        Assert.StartsWith("ZCZC 129", messages[0], StringComparison.Ordinal);
        Assert.EndsWith("NNNN", messages[0], StringComparison.Ordinal);
        Assert.Equal("FEBQ52 EDZW 211200", TelexMessages.Title(messages[0]));
    }

    [Fact]
    public async Task RttyTextComesAsEventsAndOnRequest()
    {
        var text = new System.Text.StringBuilder();
        var status = new TaskCompletionSource<DecoderStatus>(TaskCreationOptions.RunContinuationsAsynchronously);
        client.RttyReceived += (piece, lineEnd) =>
        {
            lock (text)
            {
                text.Append(piece).Append(lineEnd ? "\n" : string.Empty);
            }
        };
        client.DecoderChanged += reported => status.TrySetResult(reported);
        await client.SetAsync("decoder", "rtty");
        DecoderStatus first = await status.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.True(first.IsRtty);
        await Task.Delay(1500);
        lock (text)
        {
            Assert.StartsWith("RYRY", text.ToString(), StringComparison.Ordinal);
        }

        (string kept, DecoderStatus now) = await client.GetRttyAsync();
        Assert.StartsWith("RYRY", kept, StringComparison.Ordinal);
        Assert.True(now.IsRtty);
    }

    [Fact]
    public async Task FaxLinesComeAtTheWidthAskedFor()
    {
        var lines = new List<FaxLine>();
        var marks = new List<string>();
        client.FaxLineReceived += line =>
        {
            lock (lines)
            {
                lines.Add(line);
            }
        };
        client.FaxMarked += (kind, _) =>
        {
            lock (marks)
            {
                marks.Add(kind);
            }
        };
        await client.SetFaxEventsAsync(300);
        await client.SetAsync("fax-lpm", "240");
        await client.SetAsync("decoder", "fax");
        await Task.Delay(1600);
        lock (lines)
        {
            Assert.True(lines.Count >= 3, $"{lines.Count} lines");
            Assert.All(lines, line => Assert.Equal(300, line.Pixels.Length));
        }

        lock (marks)
        {
            Assert.Contains("start", marks);
        }

        await client.SetFaxEventsAsync(0);
        await Assert.ThrowsAsync<BoardErrorException>(() => client.SendAsync("fax events 8"));
    }
}
