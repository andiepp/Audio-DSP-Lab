namespace AudioDspLab.ViewModels;

/// <summary>Runs one board action for a page, with the outcome on the status line.</summary>
public delegate Task BoardAction(Func<BoardClient, Task> action, string successMessage);

/// <summary>
/// A page of the window: the parameters of one or more applications, bound
/// by name to what <c>params</c> reports while the application runs.
/// </summary>
public abstract class ApplicationPageViewModel : ObservableObject
{
    private readonly Dictionary<string, ParameterViewModel> parameters = new(StringComparer.OrdinalIgnoreCase);
    private bool isRunning;

    protected ApplicationPageViewModel(ParameterWrite write, BoardAction run, params string[] applications)
    {
        Write = write ?? throw new ArgumentNullException(nameof(write));
        Run = run ?? throw new ArgumentNullException(nameof(run));
        ServedApplications = applications;
    }

    /// <summary>The applications whose parameters this page shows.</summary>
    public IReadOnlyList<string> ServedApplications { get; }

    public IEnumerable<ParameterViewModel> Parameters => parameters.Values;

    /// <summary>One of its applications runs: the page is enabled.</summary>
    public bool IsRunning
    {
        get => isRunning;
        internal set
        {
            if (SetProperty(ref isRunning, value))
            {
                OnRunningChanged();
            }
        }
    }

    protected ParameterWrite Write { get; }

    protected BoardAction Run { get; }

    public bool Serves(string? application) => application is not null && ServedApplications.Contains(application);

    public bool TryGetParameter(string name, out ParameterViewModel parameter) =>
        parameters.TryGetValue(name, out parameter!);

    /// <summary>The application started: its parameters as <c>params</c> reports them.</summary>
    internal virtual void Attach(IReadOnlyList<ParameterDescriptor> descriptors)
    {
        foreach (ParameterDescriptor descriptor in descriptors)
        {
            if (parameters.TryGetValue(descriptor.Name, out ParameterViewModel? parameter))
            {
                parameter.Attach(descriptor);
            }
        }
    }

    /// <summary>The application stopped: dashes everywhere.</summary>
    internal virtual void Forget()
    {
        foreach (ParameterViewModel parameter in parameters.Values)
        {
            parameter.Forget();
        }
    }

    internal void DecayMarkers(DateTime now)
    {
        foreach (ParameterViewModel parameter in parameters.Values)
        {
            parameter.DecayMarker(now);
        }
    }

    protected virtual void OnRunningChanged()
    {
    }

    protected ParameterViewModel Number(
        string name, string title, double minimum, double maximum, string unitLine, int fractionDigits = 2) =>
        Add(new ParameterViewModel(name, title, ParameterKind.Number, Write, minimum, maximum, fractionDigits, unitLine));

    protected ParameterViewModel Integer(string name, string title, double minimum, double maximum, string unitLine) =>
        Add(new ParameterViewModel(name, title, ParameterKind.Integer, Write, minimum, maximum, 0, unitLine));

    protected ParameterViewModel Switch(string name, string title = "Output") =>
        Add(new ParameterViewModel(name, title, ParameterKind.Switch, Write, 0, 1, offOnLabel: title));

    protected ParameterViewModel Choice(string name, string title, params (string Name, string Label)[] choices) =>
        Add(new ParameterViewModel(name, title, ParameterKind.Choice, Write, 0, choices.Length - 1, choices: choices));

    private ParameterViewModel Add(ParameterViewModel parameter)
    {
        parameters.Add(parameter.Name, parameter);
        return parameter;
    }
}
