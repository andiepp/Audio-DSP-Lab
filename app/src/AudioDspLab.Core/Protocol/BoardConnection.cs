namespace AudioDspLab.Protocol;

/// <summary>
/// Requests and responses over a line transport: one request at a time,
/// its data lines collected until <c>ok</c> or <c>err</c>; event lines
/// handed on as they come.
/// </summary>
/// <remarks>
/// A request that times out leaves the connection unsure which response
/// belongs to which request. Before the next request the connection then
/// sends <c>echo &lt;word&gt;</c> and drops every line until the word comes
/// back.
/// </remarks>
public sealed class BoardConnection : IDisposable
{
    public static readonly TimeSpan DefaultTimeout = TimeSpan.FromSeconds(2);

    private readonly ILineTransport transport;
    private readonly SemaphoreSlim gate = new(1, 1);
    private readonly object sync = new();
    private Pending? pending;
    private volatile bool resyncNeeded;
    private int echoCounter;
    private bool disposed;

    public BoardConnection(ILineTransport transport)
    {
        this.transport = transport ?? throw new ArgumentNullException(nameof(transport));
        transport.LineReceived += OnLine;
        transport.Faulted += OnFaulted;
    }

    /// <summary>An event line arrived; raised on the transport's thread.</summary>
    public event Action<BoardEvent>? EventReceived;

    /// <summary>The transport broke; every later request fails.</summary>
    public event Action<Exception>? Faulted;

    public string Name => transport.Name;

    public bool IsFaulted { get; private set; }

    public void Open() => transport.Open();

    /// <summary>Sends a request and waits for its response.</summary>
    /// <exception cref="BoardErrorException">The board answered <c>err</c>.</exception>
    /// <exception cref="TimeoutException">No response in time.</exception>
    /// <exception cref="IOException">The connection broke or the board restarted.</exception>
    public async Task<BoardResponse> SendAsync(
        string request,
        TimeSpan? timeout = null,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(request);
        if (request.Contains('\n') || request.Contains('\r'))
        {
            throw new ArgumentException("A request is one line.", nameof(request));
        }

        await gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            if (IsFaulted)
            {
                throw new IOException("The connection to the board is broken.");
            }

            if (resyncNeeded)
            {
                await ResynchronizeAsync(cancellationToken).ConfigureAwait(false);
            }

            return await ExchangeAsync(new Pending(request, null), timeout ?? DefaultTimeout, cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            gate.Release();
        }
    }

    public void Dispose()
    {
        if (disposed)
        {
            return;
        }

        disposed = true;
        transport.LineReceived -= OnLine;
        transport.Faulted -= OnFaulted;
        Fail(new ObjectDisposedException(nameof(BoardConnection)));
        transport.Dispose();
    }

    private async Task<BoardResponse> ExchangeAsync(Pending request, TimeSpan timeout, CancellationToken cancellationToken)
    {
        lock (sync)
        {
            pending = request;
        }

        try
        {
            transport.WriteLine(request.Line);
        }
        catch (Exception exception) when (exception is IOException or InvalidOperationException or TimeoutException)
        {
            Fail(exception);
            throw new IOException($"Sending '{request.Line}' failed: {exception.Message}", exception);
        }

        try
        {
            return await request.Completion.Task.WaitAsync(timeout, cancellationToken).ConfigureAwait(false);
        }
        catch (Exception exception) when (exception is TimeoutException or OperationCanceledException)
        {
            lock (sync)
            {
                if (pending == request)
                {
                    pending = null;
                }
            }

            resyncNeeded = true;
            if (exception is TimeoutException)
            {
                throw new TimeoutException(
                    $"The board did not answer '{request.Line}' within {timeout.TotalSeconds:0.#} s.");
            }

            throw;
        }
    }

    private async Task ResynchronizeAsync(CancellationToken cancellationToken)
    {
        string word = $"sync{Interlocked.Increment(ref echoCounter)}";
        await ExchangeAsync(new Pending($"echo {word}", word), DefaultTimeout, cancellationToken).ConfigureAwait(false);
        resyncNeeded = false;
    }

    private void OnLine(string line)
    {
        if (line.StartsWith("! ", StringComparison.Ordinal))
        {
            IReadOnlyList<string>? words = ProtocolText.Tokenize(line[2..]);
            if (words is null || words.Count == 0)
            {
                return;
            }

            var boardEvent = new BoardEvent(words[0], [.. words.Skip(1)]);
            if (boardEvent.Name == "ready")
            {
                // whatever was asked before the restart is lost with it
                Fail(new IOException("The board restarted."), keepConnection: true);
            }

            EventReceived?.Invoke(boardEvent);
            return;
        }

        Pending? request;
        lock (sync)
        {
            request = pending;
        }

        if (request is null)
        {
            return;
        }

        if (line.StartsWith("* ", StringComparison.Ordinal))
        {
            if (request.Echo is null && ProtocolText.Tokenize(line[2..]) is { } data)
            {
                request.Data.Add(data);
                request.RawLines.Add(line);
            }

            return;
        }

        bool isOk = line == "ok" || line.StartsWith("ok ", StringComparison.Ordinal);
        bool isError = line.StartsWith("err ", StringComparison.Ordinal);
        if (!isOk && !isError)
        {
            return;                                   // boot text, noise
        }

        IReadOnlyList<string> words2 = ProtocolText.Tokenize(line) ?? [];
        if (request.Echo is not null)
        {
            // everything up to our own word belongs to requests given up
            if (isOk && words2.Count == 2 && words2[1] == request.Echo)
            {
                Complete(request, new BoardResponse([], [request.Echo]));
            }

            return;
        }

        if (isOk)
        {
            Complete(request, new BoardResponse([.. request.Data], [.. words2.Skip(1)]) { RawLines = [.. request.RawLines, line] });
        }
        else
        {
            string code = words2.Count > 1 ? words2[1] : "failed";
            int detailStart = line.IndexOf(code, 4, StringComparison.Ordinal) + code.Length;
            string detail = detailStart < line.Length ? line[detailStart..].Trim() : string.Empty;
            Complete(request, new BoardErrorException(request.Line, code, detail));
        }
    }

    private void Complete(Pending request, object outcome)
    {
        lock (sync)
        {
            if (pending != request)
            {
                return;
            }

            pending = null;
        }

        if (outcome is BoardResponse response)
        {
            request.Completion.TrySetResult(response);
        }
        else
        {
            request.Completion.TrySetException((Exception)outcome);
        }
    }

    private void OnFaulted(Exception exception)
    {
        IsFaulted = true;
        Fail(new IOException($"The connection to the board broke: {exception.Message}", exception));
        Faulted?.Invoke(exception);
    }

    private void Fail(Exception exception, bool keepConnection = false)
    {
        Pending? request;
        lock (sync)
        {
            request = pending;
            pending = null;
        }

        request?.Completion.TrySetException(exception);
        if (!keepConnection && exception is not ObjectDisposedException)
        {
            IsFaulted = true;
        }
    }

    private sealed class Pending(string line, string? echo)
    {
        public string Line { get; } = line;

        /// <summary>The word a resynchronising echo waits for; null for a request.</summary>
        public string? Echo { get; } = echo;

        public List<IReadOnlyList<string>> Data { get; } = [];

        public List<string> RawLines { get; } = [];

        public TaskCompletionSource<BoardResponse> Completion { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }
}
