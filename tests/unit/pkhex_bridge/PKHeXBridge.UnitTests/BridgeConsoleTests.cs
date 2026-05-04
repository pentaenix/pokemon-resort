using System.Text.Json;
using PKHeXBridge;
using Xunit;

namespace PKHeXBridge.UnitTests;

public class BridgeConsoleTests
{
    [Fact]
    public void Run_ReturnsFailure_WhenArgumentIsMissing()
    {
        var output = new StringWriter();

        var exitCode = BridgeConsole.Run([], output);

        using var json = JsonDocument.Parse(output.ToString());
        Assert.Equal(1, exitCode);
        Assert.False(json.RootElement.GetProperty("success").GetBoolean());
        Assert.Equal("missing_argument", json.RootElement.GetProperty("error").GetString());
    }

    [Fact]
    public void Probe_ReturnsFailure_WhenFileIsMissing()
    {
        var result = BridgeProbe.Probe("/tmp/definitely-missing-save.sav");

        Assert.False(result.Success);
        Assert.Equal("missing_file", result.Error);
        Assert.Null(result.Box1);
    }

    [Fact]
    public void Run_EmitsExpandedReaderFields_WhenFileIsMissing()
    {
        var output = new StringWriter();

        _ = BridgeConsole.Run(["/tmp/definitely-missing-save.sav"], output);

        using var json = JsonDocument.Parse(output.ToString());
        Assert.True(json.RootElement.TryGetProperty("trainer", out _));
        Assert.True(json.RootElement.TryGetProperty("pokedex", out _));
        Assert.True(json.RootElement.TryGetProperty("all_pokemon", out _));
        Assert.True(json.RootElement.TryGetProperty("boxes", out _));
        Assert.True(json.RootElement.TryGetProperty("bag", out _));
    }

    [Fact]
    public void Run_Import_ReturnsFailure_WhenPathIsMissing()
    {
        var output = new StringWriter();

        var exitCode = BridgeConsole.Run(["import", "/tmp/definitely-missing-save.sav"], output);

        using var json = JsonDocument.Parse(output.ToString());
        Assert.Equal(1, exitCode);
        Assert.Equal(1, json.RootElement.GetProperty("bridge_import_schema").GetInt32());
        Assert.False(json.RootElement.GetProperty("success").GetBoolean());
        Assert.Equal("missing_file", json.RootElement.GetProperty("error").GetString());
    }

    [Fact]
    public void Run_WriteProjection_ReturnsFailure_WhenArgumentsAreMissing()
    {
        var output = new StringWriter();

        var exitCode = BridgeConsole.Run(["write-projection", "/tmp/definitely-missing-save.sav"], output);

        using var json = JsonDocument.Parse(output.ToString());
        Assert.Equal(1, exitCode);
        Assert.Equal(1, json.RootElement.GetProperty("bridge_write_schema").GetInt32());
        Assert.False(json.RootElement.GetProperty("success").GetBoolean());
        Assert.Equal("missing_argument", json.RootElement.GetProperty("error").GetString());
    }

    [Fact]
    public void Run_PkmPatchHeldItem_ReturnsFailure_WhenPathArgumentIsMissing()
    {
        var output = new StringWriter();

        var exitCode = BridgeConsole.Run(["pkm-patch-held-item"], output);

        using var json = JsonDocument.Parse(output.ToString());
        Assert.Equal(1, exitCode);
        Assert.Equal(1, json.RootElement.GetProperty("bridge_held_item_patch_schema").GetInt32());
        Assert.False(json.RootElement.GetProperty("success").GetBoolean());
        Assert.Equal("missing_argument", json.RootElement.GetProperty("error").GetString());
    }

    [Fact]
    public void Run_Project_ReturnsFailure_WhenPathArgumentIsMissing()
    {
        var output = new StringWriter();

        var exitCode = BridgeConsole.Run(["project"], output);

        using var json = JsonDocument.Parse(output.ToString());
        Assert.Equal(1, exitCode);
        Assert.Equal(1, json.RootElement.GetProperty("bridge_project_schema").GetInt32());
        Assert.False(json.RootElement.GetProperty("success").GetBoolean());
        Assert.Equal("missing_argument", json.RootElement.GetProperty("error").GetString());
    }

    [Fact]
    public void Run_Project_ReturnsFailure_WhenRequestFileIsMissing()
    {
        var output = new StringWriter();

        var exitCode = BridgeConsole.Run(["project", "/tmp/definitely-missing-project-request.json"], output);

        using var json = JsonDocument.Parse(output.ToString());
        Assert.Equal(1, exitCode);
        Assert.Equal(1, json.RootElement.GetProperty("bridge_project_schema").GetInt32());
        Assert.False(json.RootElement.GetProperty("success").GetBoolean());
        Assert.Equal("missing_file", json.RootElement.GetProperty("error").GetString());
    }

    [Fact]
    public void Run_Project_ValidatesRequiredRequestFields()
    {
        var requestPath = Path.GetTempFileName();
        File.WriteAllText(requestPath, """
            {
              "bridge_project_schema": 1,
              "source_format_name": "PK7"
            }
            """);
        try
        {
            var output = new StringWriter();

            var exitCode = BridgeConsole.Run(["project", requestPath], output);

            using var json = JsonDocument.Parse(output.ToString());
            Assert.Equal(1, exitCode);
            Assert.Equal("missing_field", json.RootElement.GetProperty("error").GetString());
            Assert.Equal("source_raw_payload_base64", json.RootElement.GetProperty("details").GetString());
        }
        finally
        {
            File.Delete(requestPath);
        }
    }

}
