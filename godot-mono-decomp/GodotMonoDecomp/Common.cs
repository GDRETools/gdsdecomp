using System.Text;
using ICSharpCode.Decompiler.CSharp;
using ICSharpCode.Decompiler.CSharp.ProjectDecompiler;
using ICSharpCode.Decompiler.Metadata;
using ICSharpCode.Decompiler.TypeSystem;
using ICSharpCode.Decompiler.Util;

namespace GodotMonoDecomp;

public static class Common
{
	/// <summary>
	/// Gets the default C# language version for the given module based on its target framework.
	/// </summary>
	/// <param name="module">The module to determine the language version for.</param>
	/// <returns>The default C# language version for the module.</returns>
	public static LanguageVersion GetDefaultCSharpLanguageLevel(MetadataFile module){
		// Based on this table:
		// Target 			Version   C# language version default
		// .NET 			10.x 	  C# 14
		// .NET 			9.x 	  C# 13
		// .NET 			8.x 	  C# 12
		// .NET 			7.x 	  C# 11
		// .NET 			6.x 	  C# 10
		// .NET 			5.x 	  C# 9.0
		// .NET Core 		3.x 	  C# 8.0
		// .NET Core 		2.x 	  C# 7.3
		// .NET Standard 	2.1 	  C# 8.0
		// .NET Standard 	2.0 	  C# 7.3
		// .NET Standard 	1.x 	  C# 7.3
		// .NET Framework 	all 	  C# 7.3

		// determine the dotnet version
		var dotnetVersion = TargetServices.DetectTargetFramework(module);
		if (dotnetVersion == null)
		{
			return LanguageVersion.CSharp7_3;
		}
		int verMajor = dotnetVersion.VersionNumber / 100;
		int verMinor = dotnetVersion.VersionNumber % 100 / 10;

		// .NET Framework
		// ".NETFramework" applies to all the newer ".NET" as well as the legacy ".NET Framework", so check that the version is less than 5
		if (dotnetVersion.Identifier == ".NETFramework" && verMajor < 5){
			return LanguageVersion.CSharp7_3;
		}

		// .NET Core
		// ".NETCoreApp" applies to all the newer ".NET" if the module is an app, so check if the version number is less than 4
		if ((dotnetVersion.Identifier == ".NETCoreApp" || dotnetVersion.Identifier == ".NETCore") && verMajor < 4){
			if (verMajor == 3) {
				return LanguageVersion.CSharp8_0;
			}
			return LanguageVersion.CSharp7_3;
		}

		// .NET Standard
		if (dotnetVersion.Identifier == ".NETStandard" && verMajor <= 2){
			if (verMinor >= 1){
				return LanguageVersion.CSharp8_0;
			}
			return LanguageVersion.CSharp7_3;
		}
		// .NET
		switch (verMajor){
			case 10:
			// not yet supported
			// return LanguageVersion.CSharp14_0;
			case 9:
			// not yet supported
			// return LanguageVersion.CSharp13_0;
			case 8:
				return LanguageVersion.CSharp12_0;
			case 7:
				return LanguageVersion.CSharp11_0;
			case 6:
				return LanguageVersion.CSharp10_0;
			case 5:
				return LanguageVersion.CSharp9_0;
			default:
			{
				if (verMajor > 8){
					return LanguageVersion.CSharp12_0;
				}
			}
				return LanguageVersion.CSharp7_3;
		}
	}

	public static string TrimPrefix(string path, string prefix)
	{
		if (!string.IsNullOrEmpty(path) && !string.IsNullOrEmpty(prefix) && path.StartsWith(prefix))
		{
			return path[prefix.Length..];
		}
		return path;
	}

	public static void RemoveDirIfEmpty(string dir)
	{
		if (Directory.Exists(dir) && Directory.GetFiles(dir).Length == 0)
		{
			try
			{
				Directory.Delete(dir);
			}
			catch (IOException)
			{
				// ignore
			}
		}
	}

	public static IEnumerable<string> ListCSharpFiles(string directory, bool absolute)
	{
		try
		{
			// check if the directory exists
			if (!Directory.Exists(directory))
			{
				return Enumerable.Empty<string>();
			}

			var files = Directory.GetFiles(directory, "*.cs", SearchOption.AllDirectories);
			if (!absolute)
			{
				// if not absolute, return the relative paths
				files = files.Select(f => FileUtility.GetRelativePath(directory, f)).ToArray();
			}

			return files;
		}
		catch (IOException)
		{
			// if the directory doesn't exist, return an empty list
			return Enumerable.Empty<string>();
		}
	}

	public static string RemoveNamespacePartOfPath(string path, string ns)
	{
		// remove the namespace part of the path
		if (ns == "")
		{
			return path;
		}

		// find the ns in the path
		if (!path.StartsWith(ns))
		{
			ns = ns.Replace('.', '/');
		}

		if (path.StartsWith(ns))
		{
			return path.Substring(ns.Length + 1);
		}

		return path;
	}

	public static string? FindCommonRoot(IEnumerable<string> paths)
	{
		if (paths == null || !paths.Any())
		{
			return null;
		}

		// sort by length to find the shortest path first
		paths = paths.OrderBy(p => p.Length);

		var commonRoot = paths.First();
		foreach (var path in paths.Skip(1))
		{
			while (!path.StartsWith(commonRoot, StringComparison.OrdinalIgnoreCase))
			{
				commonRoot = Path.GetDirectoryName(commonRoot);
				if (commonRoot == null)
				{
					return null;
				}
			}
		}

		return commonRoot;
	}

	public static void EnsureDir(string targetDirectory)
	{

		// ensure the directory exists for new_path
		if (string.IsNullOrEmpty(targetDirectory) || Directory.Exists(targetDirectory)) return;
		try
		{
			Directory.CreateDirectory(targetDirectory);
		}
		catch (IOException)
		{
			// File.Delete(dir);
			try
			{
				Directory.CreateDirectory(targetDirectory);
			}
			catch (IOException)
			{
				try
				{
					Directory.CreateDirectory(targetDirectory);
				}
				catch (IOException)
				{
					// ignore
				}
			}
		}
	}

	public static string CamelCaseToSnakeCase(string input)
	{
		if (string.IsNullOrEmpty(input))
			return input;

		StringBuilder sb = new StringBuilder();
		for (int i = 0; i < input.Length; i++)
		{
			char c = input[i];
			if (char.IsUpper(c) && i > 0 && char.IsLower(input[i - 1]))
			{
				sb.Append('_');
			}
			sb.Append(char.ToLowerInvariant(c));
		}
		return sb.ToString();
	}

	public static string[] GetEnumValueNames(IType type)
	{
		if (type.Kind != TypeKind.Enum)
		{
			return [];
		}

		return type
			.GetFields()
			.Where(field => !field.FullName.StartsWith("System.Enum") && !field.Name.EndsWith("value__"))
			.Select(field => field.Name).ToArray();
	}

	public static string GetEnumValueName(IType type, int value, string defaultValue = "")
	{
		var names = GetEnumValueNames(type);
		if (names.Length == 0 || value >= names.Length)
		{
			return defaultValue;
		}
		return names[value];
	}
}
