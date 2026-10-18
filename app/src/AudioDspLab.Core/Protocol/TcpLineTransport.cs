using System.Net.Sockets;
using System.Text;

namespace AudioDspLab.Protocol;

/// <summary>
/// The board through its front panel over WiFi: the panel (an ESP32 CYD on
/// the board's panel port) takes one PC on TCP port 7000 and passes its
/// protocol lines on. Lines are UTF-8, ending in LF, as on the serial port.
/// </summary>
public sealed class TcpLineTransport : ILineTransport
{
    public const int DefaultPort = 7000;
    public const string DefaultHost = "audio-dsp-lab.local";
    private static readonly TimeSpan ConnectTimeout = TimeSpan.FromSeconds(4);

    private readonly string host;
    private readonly int port;
    private readonly TcpClient tcp = new() { NoDelay = true };
    private readonly object writeLock = new();
    private NetworkStream? stream;
    private Thread? reader;
    private bool disposed;

    /// <param name="address">"192.168.0.205", "audio-dsp-lab.local", or either with ":port".</param>
    public TcpLineTransport(string address)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(address);
        (host, port) = Split(address.Trim());
    }

    public event Action<string>? LineReceived;

    public event Action<Exception>? Faulted;

    public string Name => port == DefaultPort ? $"the panel at {host}" : $"the panel at {host}:{port}";

    public void Open()
    {
        ObjectDisposedException.ThrowIf(disposed, this);
        using (var timeout = new CancellationTokenSource(ConnectTimeout))
        {
            try
            {
                tcp.ConnectAsync(host, port, timeout.Token).AsTask().GetAwaiter().GetResult();
            }
            catch (OperationCanceledException)
            {
                throw new IOException($"{host} does not answer on port {port}.");
            }
        }

        stream = tcp.GetStream();
        reader = new Thread(ReadLoop) { IsBackground = true, Name = $"{host} reader" };
        reader.Start();
    }

    public void WriteLine(string line)
    {
        ArgumentNullException.ThrowIfNull(line);
        NetworkStream target = stream ?? throw new InvalidOperationException("Not open.");
        byte[] bytes = Encoding.UTF8.GetBytes(line + "\n");
        lock (writeLock)
        {
            target.Write(bytes, 0, bytes.Length);
        }
    }

    public void Dispose()
    {
        if (disposed)
        {
            return;
        }

        disposed = true;
        tcp.Dispose();
        reader?.Join(500);
    }

    /// <summary>"host" or "host:port" (an IPv4 address or a name).</summary>
    public static (string Host, int Port) Split(string address)
    {
        int colon = address.LastIndexOf(':');
        return colon > 0 && int.TryParse(address.AsSpan(colon + 1), out int number) && number is > 0 and < 65536
            ? (address[..colon], number)
            : (address, DefaultPort);
    }

    private void ReadLoop()
    {
        var line = new List<byte>(256);
        byte[] buffer = new byte[4096];
        try
        {
            while (!disposed)
            {
                int count = stream!.Read(buffer, 0, buffer.Length);
                if (count <= 0)
                {
                    throw new IOException("The panel closed the connection.");
                }

                for (int i = 0; i < count; i++)
                {
                    byte b = buffer[i];
                    if (b == (byte)'\n')
                    {
                        string text = Encoding.UTF8.GetString([.. line]).TrimEnd('\r');
                        line.Clear();
                        LineReceived?.Invoke(text);
                    }
                    else if (line.Count < 4096)
                    {
                        line.Add(b);
                    }
                }
            }
        }
        catch (Exception exception) when (exception is IOException or ObjectDisposedException or SocketException
                                              or InvalidOperationException)
        {
            if (!disposed)
            {
                Faulted?.Invoke(exception);
            }
        }
    }
}
