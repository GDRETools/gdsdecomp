namespace GodotMonoDecomp;

using System.Text.Json.Serialization;
using System.Text.Json;
using System.IO;
using System.Text.Json.Serialization.Metadata;

// Metadata for Godot 3.x Mono scripts is stored in a JSON file.
// metadata is in a json format that is formatted like this:
// {
// 	"res://Data/Camera/VisualisationMainSun.cs": {
// 		"modified_time": "1713969616",
// 		"class": {
// 			"namespace": "ZVK.GameCamera",
// 			"class_name": "VisualisationMainSun",
// 			"nested": false
// 		}
// 	},
// 	"res://Data/Inventory/InteractiveLimitedInventory.cs": {
// 		"modified_time": "1713969617",
// 		"class": {
// 			"namespace": "ZVK.Inventory",
// 			"class_name": "InteractiveLimitedInventory",
// 			"nested": false
// 		}
// 	},
// 	"res://Data/Inventory/PendingInventory.cs": {
// 		"modified_time": "1713969617",
// 		"class": {
// 			"namespace": "ZVK.Inventory",
// 			"class_name": "PendingInventory",
// 			"nested": false
// 		}
// 	},
// ...
// }

//Use System.Text.Json source generation for native AOT applications.
[JsonSourceGenerationOptions(WriteIndented = true)]
[JsonSerializable(typeof(Dictionary<string, GodotScriptMetadata>))]
[JsonSerializable(typeof(GodotScriptMetadata))]
[JsonSerializable(typeof(ClassInfo))]
internal partial class SourceGenerationContext : JsonSerializerContext
{
}


public class GodotScriptMetadata
{
    [JsonPropertyName("modified_time")]
    public string ModifiedTime { get; set; } = string.Empty;

    [JsonPropertyName("class")]
    public ClassInfo Class { get; set; } = new ClassInfo();

}

public class ClassInfo
{
    [JsonPropertyName("namespace")]
    public string Namespace { get; set; } = string.Empty;

    [JsonPropertyName("class_name")]
    public string ClassName { get; set; } = string.Empty;

    [JsonPropertyName("nested")]
    public bool Nested { get; set; } = false;


	public string GetFullClassName()
	{
		if (string.IsNullOrEmpty(Namespace))
		{
			return ClassName;
		}
		return $"{Namespace}.{ClassName}";
	}
}


public static class GodotScriptMetadataLoader
{
    /// <summary>
    /// Loads the metadata dictionary from a JSON file
    /// </summary>
    /// <param name="filePath">Path to the JSON metadata file</param>
    /// <returns>Dictionary with file paths as keys and GodotScriptMetadata as values</returns>
    public static Dictionary<string, GodotScriptMetadata>? LoadFromFile(string filePath)
    {
        if (string.IsNullOrEmpty(filePath) ||!File.Exists(filePath))
        {
            return null;
        }

        string jsonContent = File.ReadAllText(filePath);
        return JsonSerializer.Deserialize<Dictionary<string, GodotScriptMetadata>>(jsonContent, SourceGenerationContext.Default.DictionaryStringGodotScriptMetadata)
               ?? new Dictionary<string, GodotScriptMetadata>();
    }

    public static string? FindGodotScriptMetadataFile(string assemblyPath)
	{
		if (string.IsNullOrEmpty(assemblyPath))
		{
			return null;
		}
		var metadataDir = Path.Combine(Path.GetDirectoryName(Path.GetDirectoryName(Path.GetDirectoryName(assemblyPath))) ?? "", "metadata");
		// check for "scripts_metadata.{release,debug}" files
		var godot3xMetadataFile = Path.Combine(metadataDir, "scripts_metadata.release");

		if (!File.Exists(godot3xMetadataFile))
		{
			godot3xMetadataFile = Path.Combine(metadataDir, "scripts_metadata.debug");
		}

		if (File.Exists(godot3xMetadataFile))
		{
			return godot3xMetadataFile;
		}

		// try again but only one level up
		metadataDir = Path.Combine(Path.GetDirectoryName(Path.GetDirectoryName(assemblyPath)) ?? "", "metadata");
		godot3xMetadataFile = Path.Combine(metadataDir, "scripts_metadata.release");
		if (!File.Exists(godot3xMetadataFile))
		{
			godot3xMetadataFile = Path.Combine(metadataDir, "scripts_metadata.debug");
		}
		if (File.Exists(godot3xMetadataFile))
		{
			return godot3xMetadataFile;
		}


		return null;
	}
}
