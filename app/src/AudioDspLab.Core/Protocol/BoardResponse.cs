namespace AudioDspLab.Protocol;

/// <summary>A successful response: the data lines and the words after <c>ok</c>.</summary>
public sealed record BoardResponse(IReadOnlyList<IReadOnlyList<string>> Data, IReadOnlyList<string> Values)
{
    /// <summary>The first word after <c>ok</c>, or an empty string.</summary>
    public string Value => Values.Count > 0 ? Values[0] : string.Empty;
}

/// <summary>The board answered <c>err &lt;code&gt; &lt;message&gt;</c>.</summary>
public sealed class BoardErrorException(string command, string code, string detail)
    : Exception($"{command}: {code} {detail}".TrimEnd())
{
    public string Command { get; } = command;

    /// <summary>The error word, such as <c>out-of-range</c>.</summary>
    public string Code { get; } = code;

    /// <summary>The rest of the line: the board's explanation.</summary>
    public string Detail { get; } = detail;
}

/// <summary>An event line, <c>! &lt;name&gt; ...</c>.</summary>
public sealed record BoardEvent(string Name, IReadOnlyList<string> Values);
