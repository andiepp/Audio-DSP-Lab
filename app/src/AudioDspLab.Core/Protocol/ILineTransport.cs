namespace AudioDspLab.Protocol;

/// <summary>
/// Carries protocol lines to and from a board: the USB serial port, or a
/// simulated board.
/// </summary>
public interface ILineTransport : IDisposable
{
    /// <summary>
    /// One line from the board, without its line end. Raised on a
    /// background thread, in the order the lines arrived.
    /// </summary>
    event Action<string>? LineReceived;

    /// <summary>The connection broke (the cable was pulled, the port vanished).</summary>
    event Action<Exception>? Faulted;

    /// <summary>A name for people: the port, or "Simulator".</summary>
    string Name { get; }

    void Open();

    /// <summary>Sends one line; the transport adds the line end.</summary>
    void WriteLine(string line);
}
