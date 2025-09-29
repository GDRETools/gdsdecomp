using ICSharpCode.Decompiler.Metadata;
using ICSharpCode.Decompiler.Util;
using LightJson;
using LightJson.Serialization;
using System;
using System.Collections.Generic;
using System.Linq;

namespace GodotMonoDecomp;

public class DotNetCoreDepInfo
{

	public enum HashMatchesNugetOrg
	{
		// This enum is used to determine if the SHA512 hash matches the package downloaded from nuget.org.
		// If it does, we can use the hash to verify the integrity of the package.
		Unknown,
		NoMatch,
		Match
	}
	public readonly string Name;
	public readonly string Version;
	public readonly string Type;
	public readonly string Path;
	public readonly string Sha512;
	public readonly bool Serviceable;
	public readonly DotNetCoreDepInfo[] deps;
	public readonly string[] runtimeComponents;
	public HashMatchesNugetOrg HashMatchesNugetOrgStatus { get; private set; } = HashMatchesNugetOrg.Unknown;
	public AssemblyNameReference AssemblyRef => AssemblyNameReference.Parse($"{Name}, Version={GetCorrectVersion(Version)}, Culture=neutral, PublicKeyToken=null");


	static string GetCorrectVersion(string ver)
	{
		// if it contains less than 4 parts, add ".0" to the end
		var parts = ver.Split('.').ToList();
		while (parts.Count < 4)
		{
			parts.Add("0");
		}
		return string.Join(".", parts);
	}


	public DotNetCoreDepInfo(
		string fullName,
		string version,
		string type,
		bool serviceable,
		string path,
		string sha512,
		DotNetCoreDepInfo[] deps, string[] runtimeComponents)
	{
		var parts = fullName.Split('/');
		this.Name = parts[0];
		if (parts.Length > 1)
		{
			this.Version = parts[1];
		}
		else
		{
			this.Version = version;
		}

		this.Type = type;
		this.Serviceable = serviceable;
		this.Path = path;
		this.Sha512 = sha512;

		this.deps = deps;
		this.runtimeComponents = runtimeComponents;
	}

	static DotNetCoreDepInfo CreateFromJson(string fullName, string version, string target, JsonObject blob)
	{
		return Create(fullName, version, target, blob, []);
	}

	static DotNetCoreDepInfo Create(string fullName, string version, string target, JsonObject blob,
		Dictionary<string, DotNetCoreDepInfo> _deps)
	{
		var parts = fullName.Split('/');
		var Name = parts[0];
		var Version = "<UNKNOWN>";
		if (parts.Length > 1)
		{
			Version = parts[1];
		}
		else
		{
			Version = version;
		}

		var type = "runtimedll";
		var serviceable = false;
		var path = "";
		var sha512 = "";
		var libraryBlob = blob["libraries"][Name + "/" + Version].AsJsonObject;
		if (libraryBlob != null)
		{
			type = libraryBlob["type"].AsString;
			serviceable = libraryBlob["serviceable"].AsBoolean;
			path = libraryBlob["path"].AsString ?? "";
			sha512 = libraryBlob["sha512"].AsString ?? "";
		}

		string[] runtimeComponents = Array.Empty<string>();
		var runtimeBlob = blob["targets"][target].AsJsonObject?[Name + "/" + Version].AsJsonObject?["runtime"]
			.AsJsonObject;
		if (runtimeBlob != null)
		{
			runtimeComponents = new string[runtimeBlob.Count];
			int i = 0;
			foreach (var component in runtimeBlob)
			{
				runtimeComponents[i] = System.IO.Path.GetFileNameWithoutExtension(component.Key);
				i++;
			}
		}

		var deps = getDeps(Name, Version, target, blob, _deps);
		return new DotNetCoreDepInfo(Name, Version, type, serviceable, path, sha512, deps, runtimeComponents);
	}


	static DotNetCoreDepInfo[] getDeps(string Name, string Version, string target, JsonObject blob,
		Dictionary<string, DotNetCoreDepInfo>? _deps = null)
	{
		if (_deps == null)
		{
			_deps = [];
		}

		var targetBlob = blob["targets"][target].AsJsonObject;
		if (targetBlob == null)
		{
			return Empty<DotNetCoreDepInfo>.Array;
		}

		var depsBlob = targetBlob[Name + "/" + Version].AsJsonObject?["dependencies"].AsJsonObject;
		var runtimeBlob = targetBlob[Name + "/" + Version].AsJsonObject?["runtime"].AsJsonObject;
		if (depsBlob == null && runtimeBlob == null)
		{
			return Empty<DotNetCoreDepInfo>.Array;
		}

		List<DotNetCoreDepInfo> result = new List<DotNetCoreDepInfo>();
		Dictionary<String, String> deps = new Dictionary<string, string>();
		if (depsBlob != null)
		{
			foreach (var dep in depsBlob)
			{
				var dep_key = dep.Key + "/" + dep.Value.AsString;
				if (_deps.ContainsKey(dep_key))
				{
					result.Add(_deps[dep_key]);
				}
				else
				{
					var new_dep = Create(dep.Key, dep.Value.AsString, target, blob, _deps);
					_deps.Add(dep_key, new_dep);
					result.Add(new_dep);
				}
			}
		}

		return result.ToArray();
	}

	public bool HasDep(string name, string? type, bool serviceableAndNuGetOnly = false)
	{
		if (runtimeComponents.Contains(name) && !((!string.IsNullOrEmpty(type) && Type != type) || (serviceableAndNuGetOnly && (!Serviceable || HashMatchesNugetOrgStatus == HashMatchesNugetOrg.NoMatch))))
		{
			return true;
		}
		for (int i = 0; i < deps.Length; i++)
		{
			if ((!string.IsNullOrEmpty(type) && deps[i].Type != type) ||
			    (serviceableAndNuGetOnly && (!deps[i].Serviceable || deps[i].HashMatchesNugetOrgStatus == HashMatchesNugetOrg.NoMatch)))
			{
				// skip non-package dependencies if parent is a package
				continue;
			}

			if (deps[i].Name == name)
			{
				return true;
			}

			if (deps[i].HasDep(name, null, false))
			{
				return true;
			}
		}

		return false;
	}

	public static string GetDepPath(string assemblyPath)
	{
		return System.IO.Path.ChangeExtension(assemblyPath, ".deps.json");
	}


	public static DotNetCoreDepInfo? LoadDepInfoFromFile(string depsJsonFileName, string moduleName)
	{
		// remove the .dll extension
		if (string.IsNullOrEmpty(depsJsonFileName) || !System.IO.File.Exists(depsJsonFileName))
		{
			return null;
		}
		var depsJson = File.ReadAllText(depsJsonFileName);
		var dependencies = JsonReader.Parse(depsJson);
		// go through each target framework, find the one that matches the module
		foreach (var target in dependencies["targets"].AsJsonObject)
		{
			foreach (var dependency in target.Value.AsJsonObject)
			{
				if (dependency.Key.StartsWith(moduleName))
				{
					return DotNetCoreDepInfo.CreateFromJson(dependency.Key, "", target.Key, dependencies.AsJsonObject);
				}
			}
		}

		return null;
	}

	public async Task StartResolvePackageAndCheckHash(CancellationToken cancellationToken)
	{
		if (!Serviceable || Type != "package" || string.IsNullOrEmpty(Sha512) || !Sha512.StartsWith("sha512-"))
		{
			// only resolve packages that are serviceable and of type package
			HashMatchesNugetOrgStatus = HashMatchesNugetOrg.Unknown;
			return;
		}

		var hash = await NugetDetails.ResolvePackageAndGetContentHash(Name, Version, cancellationToken);
		if (hash == null)
		{
			HashMatchesNugetOrgStatus = HashMatchesNugetOrg.Unknown;
		}
		else if (hash == Sha512)
		{
			HashMatchesNugetOrgStatus = HashMatchesNugetOrg.Match;
		}
		else
		{
			HashMatchesNugetOrgStatus = HashMatchesNugetOrg.NoMatch;
		}

	}
}

