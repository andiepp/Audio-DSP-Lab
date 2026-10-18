using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Text;
using AudioDspLab.Protocol;

namespace AudioDspLab.Tests;

public class TcpLineTransportTests
{
    [Fact]
    public void Split_TakesAnAddressWithOrWithoutItsPort()
    {
        Assert.Equal(("192.168.0.205", 7000), TcpLineTransport.Split("192.168.0.205"));
        Assert.Equal(("audio-dsp-lab.local", 7001), TcpLineTransport.Split("audio-dsp-lab.local:7001"));
        Assert.Equal(("host:port", 7000), TcpLineTransport.Split("host:port"));   // no number: one name, which will not resolve
    }

    [Fact]
    public async Task Lines_GoBothWays_AndAClosedPanelFaults()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        using var transport = new TcpLineTransport($"127.0.0.1:{port}");
        var lines = new BlockingCollection<string>();
        var faulted = new TaskCompletionSource<Exception>();
        transport.LineReceived += lines.Add;
        transport.Faulted += exception => faulted.TrySetResult(exception);

        transport.Open();
        using TcpClient panel = await listener.AcceptTcpClientAsync();
        NetworkStream stream = panel.GetStream();
        transport.WriteLine("echo hello");
        using var reader = new StreamReader(stream, Encoding.UTF8, leaveOpen: true);
        Assert.Equal("echo hello", await reader.ReadLineAsync());

        byte[] answer = Encoding.UTF8.GetBytes("ok hello\r\n! title \"Zwischentöne\"\n");
        await stream.WriteAsync(answer);
        Assert.True(lines.TryTake(out string? first, 2000));
        Assert.Equal("ok hello", first);
        Assert.True(lines.TryTake(out string? second, 2000));
        Assert.Equal("! title \"Zwischentöne\"", second);

        panel.Close();
        listener.Stop();
        Assert.IsAssignableFrom<IOException>(await faulted.Task.WaitAsync(TimeSpan.FromSeconds(3)));
    }

    [Fact]
    public void Open_ReportsAPanelThatDoesNotAnswer()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        listener.Stop();                               // nothing listens there now
        using var transport = new TcpLineTransport($"127.0.0.1:{port}");
        Assert.ThrowsAny<Exception>(transport.Open);
    }
}
