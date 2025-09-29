
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace GodotMonoDecomp;
using System.Text.Json.Serialization;
using System.Text.Json;
using System.IO;
using System.Text.Json.Serialization.Metadata;

// [JsonSourceGenerationOptions(WriteIndented = false)]
[JsonSerializable(typeof(Dictionary<string, GodotScriptInfo>))]
[JsonSerializable(typeof(PropertyInfo))]
[JsonSerializable(typeof(MethodInfo))]
[JsonSerializable(typeof(GodotScriptInfo))]
internal partial class SISrcGenContext : JsonSerializerContext
{
}

public class PropertyInfo
{
	[JsonPropertyName("name")]
	public string Name { get; set; }
	[JsonPropertyName("type")]
	public string Type { get; set; }
	[JsonPropertyName("default_value")]
	public string DefaultValue { get; set; }

	[JsonPropertyName("property_hint")]
	public string PropertyHint { get; set; }
	[JsonPropertyName("property_hint_string")]
	public string PropertyHintString { get; set; }


	public PropertyInfo(string name, string type, string defaultValue, string propertyHint, string propertyUsage)
	{
		Name = name;
		Type = GodotStuff.CSharpTypeToGodotType(type);
		DefaultValue = defaultValue;
		PropertyHint = Common.CamelCaseToSnakeCase(propertyHint).ToUpper();
		PropertyHintString = propertyUsage;
	}
}

public class MethodInfo
{
	[JsonPropertyName("name")]
	public string Name { get; set; }
	[JsonPropertyName("return_type")]
	public string ReturnType { get; set; }
	[JsonPropertyName("parameter_names")]
	public string[] ParameterNames { get; set; }
	[JsonPropertyName("parameter_types")]
	public string[] ParameterTypes { get; set; }

	[JsonPropertyName("is_static")]
	public bool IsStatic { get; set; }

	[JsonPropertyName("is_abstract")]
	public bool IsAbstract { get; set; }

	[JsonPropertyName("is_virtual")]
	public bool IsVirtual { get; set; }

	public MethodInfo(string name, string returnType, string[] parameterNames, string[] parameterTypes, bool isStatic, bool isAbstract, bool isVirtual)
	{
		Name = name;
		if (name.Contains("ContainsSelectedPatch"))
		{
			var f = false;
		}
		ReturnType = GodotStuff.CSharpTypeToGodotType(returnType);
		ParameterNames = parameterNames;
		ParameterTypes = parameterTypes.Select(GodotStuff.CSharpTypeToGodotType).ToArray();
		IsStatic = isStatic;
		IsAbstract = isAbstract;
	}
}

public class GodotScriptInfo
{
	[JsonPropertyName("path")]

	public string Path { get; set; }
	[JsonPropertyName("namespace")]
	public string Namespace { get; set; }
	[JsonPropertyName("class_name")]
	public string ClassName { get; set; }
	[JsonPropertyName("base_classes")]
	public string[] BaseClasses { get; set; }

	[JsonPropertyName("base_type_paths")]
	public string[] BaseTypePaths { get; set; }

	[JsonPropertyName("properties")]
	public PropertyInfo[] Properties { get; set; }
	[JsonPropertyName("signals")]
	public MethodInfo[] Signals { get; set; }
	[JsonPropertyName("methods")]
	public MethodInfo[] Methods { get; set; }

	[JsonPropertyName("is_tool")]
	public bool IsTool { get; set; }

	[JsonPropertyName("is_abstract")]
	public bool IsAbstract { get; set; }

	[JsonPropertyName("is_global_class")]
	public bool IsGlobalClass { get; set; }

	[JsonPropertyName("icon_path")]
	public string IconPath { get; set; }

	public GodotScriptInfo(string path, string @namespace, string className, string[] baseClasses, string[] baseTypePaths	,
		PropertyInfo[] properties, MethodInfo[] signals, MethodInfo[] methods, bool isTool, bool isAbstract, bool isGlobalClass, string iconPath)
	{
		Path = path;
		Namespace = @namespace;
		ClassName = className;
		BaseClasses = baseClasses;
		BaseTypePaths = baseTypePaths;
		Properties = properties;
		Signals = signals;
		Methods = methods;
		IsTool = isTool;
		IsAbstract = isAbstract;
		IsGlobalClass = isGlobalClass;
		IconPath = iconPath;
	}
	public static readonly JsonSerializerOptions DefaultJsonOptions = new()
	{
		WriteIndented = false,
		Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
		DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull
	};

	public string ToJson(bool indented = false)
	{
		var buf = new MemoryStream();
		var opts = new JsonWriterOptions();
		opts.Indented = indented;
		opts.Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping;
		var strWriter = new Utf8JsonWriter(buf, opts);
		JsonSerializer.Serialize(strWriter, this, SISrcGenContext.Default.GodotScriptInfo);
		strWriter.Flush();
		var arr = buf.ToArray();
		var str = System.Text.Encoding.UTF8.GetString(arr);
		buf.Dispose();
		return str;
	}
}

