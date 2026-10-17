using AudioDspLab.Protocol;

namespace AudioDspLab.Tests;

public class ProtocolTextTests
{
    [Fact]
    public void Tokenize_SplitsWordsAndQuotedTexts()
    {
        IReadOnlyList<string>? words = ProtocolText.Tokenize("station 3 name \"Radio \\\"Eins\\\" \\\\ Berlin\"");
        Assert.Equal(["station", "3", "name", "Radio \"Eins\" \\ Berlin"], words);
    }

    [Fact]
    public void Tokenize_KeepsEmptyQuotedText()
    {
        Assert.Equal(["wifi", "0", ""], ProtocolText.Tokenize("wifi 0 \"\""));
    }

    [Fact]
    public void Tokenize_RefusesAnOpenQuote()
    {
        Assert.Null(ProtocolText.Tokenize("station 1 name \"open"));
    }

    [Fact]
    public void Quote_RoundTripsThroughTokenize()
    {
        const string text = "A \"quoted\" back\\slash";
        Assert.Equal([text], ProtocolText.Tokenize(ProtocolText.Quote(text)));
    }

    [Theory]
    [InlineData(1000.0, "1000")]
    [InlineData(1000.5, "1000.5")]
    [InlineData(-12.345, "-12.35")]
    [InlineData(0.004, "0")]
    public void FormatNumber_WritesAsTheBoardDoes(double value, string expected)
    {
        Assert.Equal(expected, ProtocolText.FormatNumber(value));
    }

    [Fact]
    public void ParseNumberOrNone_ReadsTheDashAsNoValue()
    {
        Assert.Null(ProtocolText.ParseNumberOrNone("-"));
        Assert.Equal(-3.5, ProtocolText.ParseNumberOrNone("-3.5"));
    }

    [Fact]
    public void ParameterDescriptor_ParsesAParamsLine()
    {
        ParameterDescriptor parameter = ParameterDescriptor.Parse(
            ProtocolText.Tokenize("waveform choice square 0 3 sine - sine,square,triangle,noise P3")!);
        Assert.Equal(ParameterKind.Choice, parameter.Kind);
        Assert.Equal("square", parameter.Value);
        Assert.Equal(["sine", "square", "triangle", "noise"], parameter.Choices);
        Assert.Equal(string.Empty, parameter.Unit);
        Assert.Equal("P3", parameter.Pot);
    }

    [Theory]
    [InlineData(60, "C4")]
    [InlineData(69, "A4")]
    [InlineData(61, "C#4")]
    [InlineData(21, "A0")]
    public void NoteName_NamesMidiNotes(int note, string expected)
    {
        Assert.Equal(expected, SynthVoice.NoteName(note));
    }
}
