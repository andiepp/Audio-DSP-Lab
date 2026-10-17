using System.IO.Ports;
using System.Text;

namespace AudioDspLab.Protocol;

/// <summary>
/// The board's USB serial port: 921600 8N1, DTR and RTS deasserted (on the
/// ESP32-PICO-KIT they drive reset and boot mode).
/// </summary>
public sealed class SerialLineTransport : ILineTransport
{
    public const int BaudRate = 921600;

    private readonly SerialPort port;
    private readonly object writeLock = new();
    private CancellationTokenSource? reading;
    private Thread? reader;
    private bool disposed;

    public SerialLineTransport(string portName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(portName);
        port = new SerialPort(portName, BaudRate, Parity.None, 8, StopBits.One)
        {
            Handshake = Handshake.None,
            DtrEnable = false,
            RtsEnable = false,
            ReadTimeout = SerialPort.InfiniteTimeout,
            WriteTimeout = 2000,
            Encoding = Encoding.UTF8,
            ReadBufferSize = 64 * 1024,
        };
    }

    public event Action<string>? LineReceived;

    public event Action<Exception>? Faulted;

    public string Name => port.PortName;

    /// <summary>The serial ports Windows knows now, sorted by number.</summary>
    public static IReadOnlyList<string> PortNames() =>
        [.. SerialPort.GetPortNames().Distinct().OrderBy(PortNumber).ThenBy(name => name, StringComparer.Ordinal)];

    public void Open()
    {
        ObjectDisposedException.ThrowIf(disposed, this);
        port.Open();
        port.DiscardInBuffer();
        reading = new CancellationTokenSource();
        reader = new Thread(() => ReadLoop(reading.Token)) { IsBackground = true, Name = $"{port.PortName} reader" };
        reader.Start();
    }

    public void WriteLine(string line)
    {
        ArgumentNullException.ThrowIfNull(line);
        byte[] bytes = Encoding.UTF8.GetBytes(line + "\n");
        lock (writeLock)
        {
            port.BaseStream.Write(bytes, 0, bytes.Length);
            port.BaseStream.Flush();
        }
    }

    public void Dispose()
    {
        if (disposed)
        {
            return;
        }

        disposed = true;
        reading?.Cancel();
        try
        {
            port.Close();
        }
        catch (IOException)
        {
        }

        port.Dispose();
        reader?.Join(500);
        reading?.Dispose();
    }

    private void ReadLoop(CancellationToken cancellation)
    {
        var line = new List<byte>(256);
        byte[] buffer = new byte[4096];
        try
        {
            while (!cancellation.IsCancellationRequested)
            {
                int count = port.BaseStream.Read(buffer, 0, buffer.Length);
                if (count <= 0)
                {
                    continue;
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
        catch (Exception exception) when (exception is IOException or InvalidOperationException
                                              or UnauthorizedAccessException or OperationCanceledException)
        {
            if (!disposed)
            {
                Faulted?.Invoke(exception);
            }
        }
    }

    private static int PortNumber(string name) =>
        name.StartsWith("COM", StringComparison.OrdinalIgnoreCase) && int.TryParse(name.AsSpan(3), out int number)
            ? number
            : int.MaxValue;
}
