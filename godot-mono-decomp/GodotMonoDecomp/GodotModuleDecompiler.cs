using System.Reflection.Metadata;
using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.RegularExpressions;
using ICSharpCode.Decompiler;
using ICSharpCode.Decompiler.CSharp;
using ICSharpCode.Decompiler.CSharp.OutputVisitor;
using ICSharpCode.Decompiler.CSharp.ProjectDecompiler;
using ICSharpCode.Decompiler.CSharp.Syntax;
using ICSharpCode.Decompiler.CSharp.Syntax.PatternMatching;
using ICSharpCode.Decompiler.DebugInfo;
using ICSharpCode.Decompiler.Metadata;
using ICSharpCode.Decompiler.Solution;
using ICSharpCode.Decompiler.TypeSystem;
using ICSharpCode.Decompiler.Util;
using ICSharpCode.ILSpyX.PdbProvider;

namespace GodotMonoDecomp;

public class GodotModule
{
	public readonly PEFile Module;
	public readonly DotNetCoreDepInfo? depInfo;
	public readonly LanguageVersion languageVersion;
	public readonly Guid moduleGuid;
	public readonly IDebugInfoProvider? debugInfoProvider;
	public readonly string? SubDirectory;
	public Dictionary<string, List<TypeDefinitionHandle>> fileMap;

	private GodotProjectDecompiler ProjectDecompiler;

	private DecompilerTypeSystem TypeSystem;

	public GodotModule(PEFile module, DotNetCoreDepInfo? depInfo, string? subdir, GodotMonoDecompSettings p_settings, UniversalAssemblyResolver assemblyResolver)
	{
		fileMap = [];
		SubDirectory = subdir;
		Module = module ?? throw new ArgumentNullException(nameof(module));
		this.depInfo = depInfo;
		debugInfoProvider = DebugInfoUtils.LoadSymbols(module);
		if (p_settings?.OverrideLanguageVersion != null)
		{
			languageVersion = p_settings.OverrideLanguageVersion.Value;
		}
		else
		{
			languageVersion = Common.GetDefaultCSharpLanguageLevel(module);
			// default to C# 9.0 if the language version is less than that; Mono compiler supports up to C# 9.0
			if (languageVersion < LanguageVersion.CSharp9_0)
			{
				languageVersion = LanguageVersion.CSharp9_0;
			}
		}
		var moduleSettings = p_settings!.Clone();
		moduleSettings.SetLanguageVersion(languageVersion);


		// generate a UUID for the module based on its name
		moduleGuid = Common.GenerateDeterministicGuidFromString(Module.FileName);

		ProjectDecompiler = new GodotProjectDecompiler(moduleSettings, moduleGuid, assemblyResolver, assemblyResolver, debugInfoProvider);
		TypeSystem = ProjectDecompiler.CreateTypeSystem(Module);
	}

	public MetadataReader Metadata => Module.Metadata;
	public string Name => Module.Name;

	public GodotProjectDecompiler GetProjectDecompiler(IProgress<DecompilationProgress>? progress_reporter = null)
	{
		ProjectDecompiler.ProgressIndicator = progress_reporter;
		return ProjectDecompiler;
	}

	public CSharpDecompiler CreateCSharpDecompilerWithPartials(IEnumerable<TypeDefinitionHandle> typesToDecompile)
	{
		var decompiler = ProjectDecompiler.CreateDecompiler(TypeSystem);
		GodotStuff.GetPartialGodotTypes(TypeSystem.MainModule, typesToDecompile).ForEach(p => decompiler.AddPartialTypeDefinition(p));
		return decompiler;
	}

}

public class GodotModuleDecompiler
{
	public readonly GodotModule MainModule;
	public readonly List<GodotModule> AdditionalModules;
	public readonly UniversalAssemblyResolver AssemblyResolver;
	public readonly GodotMonoDecompSettings Settings;
	public readonly List<string> originalProjectFiles;
	public readonly Version godotVersion;
	public readonly Dictionary<string, GodotScriptMetadata>? godot3xMetadata;
	public bool CustomVersionDetected { get; private set; }

	private readonly List<Task> packageHashTasks;
	private readonly CancellationTokenSource packageHashTaskCancelSrc;


	private static string? GetPreReleaseLabel(string? versionText)
	{
		if (string.IsNullOrWhiteSpace(versionText))
		{
			return null;
		}

		int dashIndex = versionText.IndexOf('-');
		if (dashIndex < 0 || dashIndex >= versionText.Length - 1)
		{
			return null;
		}

		string prereleaseText = versionText[(dashIndex + 1)..];
		int buildMetadataSeparator = prereleaseText.IndexOf('+');
		if (buildMetadataSeparator >= 0)
		{
			prereleaseText = prereleaseText[..buildMetadataSeparator];
		}

		if (string.IsNullOrWhiteSpace(prereleaseText))
		{
			return null;
		}

		var label = prereleaseText.Split('.')[0].Trim();
		return string.IsNullOrEmpty(label) ? null : label.ToLowerInvariant();
	}

	private static bool IsAllowedGodotPreReleaseLabel(string label)
	{
		return label.ToLower().StartsWith("alpha") || label.ToLower().StartsWith("beta") || label.ToLower().StartsWith("rc") || label.ToLower().StartsWith("dev");
	}

	private bool DetectCustomVersionFromPrerelease(DotNetCoreDepInfo? mainDepInfo)
	{
		var versionText = GodotStuff.GetGodotSharpPackageDep(mainDepInfo)?.Version
		                  ?? Settings.GodotVersionOverride?.ToString();
		var prereleaseLabel = GetPreReleaseLabel(versionText);
		return prereleaseLabel != null && !IsAllowedGodotPreReleaseLabel(prereleaseLabel);
	}

	private bool DetectCustomVersionFromPackageHash()
	{
		if (!Settings.VerifyNuGetPackageIsFromNugetOrg)
		{
			return false;
		}

		return new List<GodotModule> { MainModule }
			.Concat(AdditionalModules)
			.SelectMany(module => module.depInfo?.deps ?? [])
			.Any(dep =>
				dep.Type == "package" &&
				string.Equals(dep.Name, "GodotSharp", StringComparison.OrdinalIgnoreCase) &&
				dep.HashMatchesNugetOrgStatus == DotNetCoreDepInfo.HashMatchesNugetOrg.NoMatch);
	}

	public bool IsCustomVersionDetected()
	{
		return CustomVersionDetected;
	}


	public void AddSubProjects(string assemblyPath, DotNetCoreDepInfo depInfo, List<string> names, HashSet<string> mainModuleSubDirs, HashSet<string> otherSubDirs)
	{
		foreach (var dep in depInfo.deps.Where(d => d is { Type: "project" })
			         .OrderBy(d => d.Name, StringComparer.OrdinalIgnoreCase))
		{

			if (names.Contains(dep.Name)) // already added
			{
				continue;
			}

			names.Add(dep.Name);
			var existingSubdirs = otherSubDirs.Where(subdir => subdir == dep.Name || Path.GetFileName(subdir) == dep.Name).ToHashSet();

			var assemblynameRef = dep.AssemblyRef;
			var supposedPath = Path.Combine(Path.GetDirectoryName(assemblyPath) ?? "", assemblynameRef.Name + ".dll");
			MetadataFile? reference = File.Exists(supposedPath) ? new PEFile(supposedPath) : AssemblyResolver.Resolve(assemblynameRef);

			if (reference == null)
			{
				Console.Error.WriteLine(
					$"Warning: Could not resolve project reference '{dep.Name}' for assembly '{MainModule.Name}'.");
				continue;
			}

			if (reference is PEFile module)
			{
				var subdir = existingSubdirs.Count == 1 ? existingSubdirs.First() : (mainModuleSubDirs.Contains(module.Name) ? "subprojects" + "/" + module.Name : module.Name);
				while (subdir != null && mainModuleSubDirs.Contains(subdir))
				{
					subdir = "_" + subdir;
				}

				AdditionalModules.Add(new GodotModule(module, dep, subdir, Settings, AssemblyResolver));
			}

			AddSubProjects(assemblyPath, dep, names, mainModuleSubDirs, otherSubDirs);
		}
	}

	public GodotModuleDecompiler(string assemblyPath, string[]? originalProjectFiles, string[]? ReferencePaths = null,
		GodotMonoDecompSettings? settings = default(GodotMonoDecompSettings))
	{
		packageHashTasks = [];
		packageHashTaskCancelSrc = new CancellationTokenSource();

		AdditionalModules = [];
		this.originalProjectFiles = [.. (originalProjectFiles ?? [])
			.Where(file => !string.IsNullOrEmpty(file))
			.Select(file => Common.TrimPrefix(file, "res://").Replace('\\', '/'))
			.Where(file => !string.IsNullOrEmpty(file) && !file.StartsWith(".godot/mono/temp"))
			.OrderBy(file => file, StringComparer.OrdinalIgnoreCase)];
		var mod = new PEFile(assemblyPath);
		var _mainDepInfo = DotNetCoreDepInfo.LoadDepInfoFromFile(DotNetCoreDepInfo.GetDepPath(assemblyPath), mod.Name);
		Settings = settings ?? new GodotMonoDecompSettings();
		AssemblyResolver = new UniversalAssemblyResolver(assemblyPath, false, mod.Metadata.DetectTargetFrameworkId());
		foreach (var path in (ReferencePaths ?? System.Array.Empty<string>()))
		{
			AssemblyResolver.AddSearchDirectory(path);
		}
		if (settings!.AutomaticallySearchWorkshopDependencies && SteamHelpers.IsAssemblyPathInWorkshopFolder(assemblyPath))
		{
			foreach (var path in SteamHelpers.GetAdditionalAssemblySearchPathsForWorkshopModule(mod, ReferencePaths ?? []))
			{
				AssemblyResolver.AddSearchDirectory(path);
			}
		}
		MainModule = new GodotModule(mod, _mainDepInfo, null, Settings, AssemblyResolver);
		CustomVersionDetected = DetectCustomVersionFromPrerelease(_mainDepInfo);

		godotVersion = Settings.GodotVersionOverride ?? GodotStuff.GetGodotVersion(MainModule.Module) ?? new Version(0, 0, 0, 0);
		if (godotVersion.Major <= 3)
		{
			// check for "script_metadata.{release,debug}" files
			var godot3xMetadataFile = GodotScriptMetadataLoader.FindGodotScriptMetadataFile(assemblyPath);
			if (godot3xMetadataFile != null && File.Exists(godot3xMetadataFile))
			{
				godot3xMetadata = GodotScriptMetadataLoader.LoadFromFile(godot3xMetadataFile);
			}
		}

		List<string> names = [];
		if (Settings.CreateAdditionalProjectsForProjectReferences && MainModule.depInfo != null)
		{
			HashSet<string> mainModuleSubDirs = GodotStuff.GetCanonicalGodotScriptPaths(MainModule.Module,
			 	MainModule.GetProjectDecompiler().GetTypesToDecompile(MainModule.Module), godot3xMetadata)
				.Where(p => !string.IsNullOrEmpty(Path.GetDirectoryName(p)))
				.Select(p => Path.GetDirectoryName(p)!)
				.ToHashSet();

			HashSet<string> otherSubDirs = this.originalProjectFiles.Select(path => Path.GetDirectoryName(path) ?? "").Where(p => !string.IsNullOrEmpty(p) && !mainModuleSubDirs.Contains(p)).ToHashSet() ?? [];

			AddSubProjects(assemblyPath, MainModule.depInfo, names, mainModuleSubDirs, otherSubDirs);

		}

		foreach (var module in new List<GodotModule> { MainModule }.Concat(AdditionalModules))
		{
			foreach (var dep in module.depInfo?.deps.Where(d =>
							d is { Type: "package" } &&
							(!ProjectFileWriterGodotStyle.ImplicitGodotReferences.Contains(d.Name) ||
							string.Equals(d.Name, "GodotSharp", StringComparison.OrdinalIgnoreCase))) ?? [])
			{
				packageHashTasks.Add(
					Task.Run(async () => await dep.StartResolvePackageAndCheckHash(Settings.VerifyNuGetPackageIsFromNugetOrg, packageHashTaskCancelSrc.Token), packageHashTaskCancelSrc.Token)
					);
			}
		}


		HashSet<string> excludeSubdirs = AdditionalModules.Select(module => module.SubDirectory ?? "").Where(subdir => !string.IsNullOrEmpty(subdir)).ToHashSet();

		HashSet<TypeDefinitionHandle> GetGodotClassHandles(GodotModule module, IEnumerable<TypeDefinitionHandle> handles)
		{
			var handleSet = handles.ToHashSet();
			var decompiler = module.CreateCSharpDecompilerWithPartials(handleSet);
			var godotHandles = new HashSet<TypeDefinitionHandle>();
			foreach (var h in handleSet)
			{
				var typeDef = decompiler.TypeSystem.MainModule.GetDefinition(h);
				if (typeDef != null && GodotStuff.IsGodotClass(typeDef))
				{
					godotHandles.Add(h);
				}
			}
			return godotHandles;
		}

		var typesToDecompile = MainModule.GetProjectDecompiler().GetTypesToDecompile(MainModule.Module).ToHashSet();
		var mainGodotClassHandles = GetGodotClassHandles(MainModule, typesToDecompile);
		MainModule.fileMap = GodotStuff.CreateFileMap(MainModule.Module, typesToDecompile, this.originalProjectFiles, godot3xMetadata, excludeSubdirs, true, mainGodotClassHandles);
		var additionalModuleCount = 0;
		// var moduleFileNameToMouduleMap = new Dictionary<string, GodotModule>(StringComparer.OrdinalIgnoreCase);
		foreach (var module in AdditionalModules)
		{
			// TODO: make CreateFileMap() work with multiple modules
			typesToDecompile = module.GetProjectDecompiler().GetTypesToDecompile(module.Module).ToHashSet();
			var moduleGodotClassHandles = GetGodotClassHandles(module, typesToDecompile);

			var nfileMap = GodotStuff.CreateFileMap(module.Module, typesToDecompile, this.originalProjectFiles, godot3xMetadata, null, true, moduleGodotClassHandles);
			additionalModuleCount += nfileMap.Count;

			string moduleName = module.Module.FileName;
			module.fileMap = [];

			foreach (var pair in nfileMap.ToList())
			{
				string path = pair.Key;
				string fixedPath = path;
				if (!path.StartsWith(module.SubDirectory + "/", StringComparison.CurrentCultureIgnoreCase))
				{
					fixedPath = module.SubDirectory + "/" + pair.Key;
				}
				if (!module.fileMap.TryGetValue(fixedPath, out var existingHandles))
				{
					module.fileMap.Add(fixedPath, pair.Value);
				}
				else
				{
					existingHandles.AddRange(pair.Value.Where(h => !existingHandles.Contains(h)));
				}
			}

			if (module.fileMap.Count == 0)
			{
				Console.Error.WriteLine($"Warning: Module '{moduleName}' has no files to decompile. It may not be a Godot module or it may not contain any scripts.");
			}
			else
			{
				Console.WriteLine($"Module '{moduleName}' has {module.fileMap.Count} files to decompile.");
			}
		}

	}

	void removeIfExists(string path)
	{
		if (File.Exists(path))
		{
			try
			{
				File.Delete(path);
			}
			catch (Exception e)
			{
				Console.Error.WriteLine($"Error: Failed to delete existing file {path}: {e.Message}");
			}
		}

	}

	public int DecompileModule(string outputCSProjectPath, string[]? excludeFiles = null, IProgress<DecompilationProgress>? progress_reporter = null, CancellationToken token = default(CancellationToken))
	{
		if (packageHashTasks.Count > 0)
		{
			if (Settings.VerifyNuGetPackageIsFromNugetOrg)
			{
				var waitTask = Task.WhenAll(packageHashTasks)
					.ContinueWith(_ =>
					{
						if (token.IsCancellationRequested)
						{
							packageHashTaskCancelSrc.Cancel();
						}
					}, token);
				Console.WriteLine("Waiting for package hash tasks to complete...");
				Task.WaitAll([waitTask], token);
				Console.WriteLine("Package hash tasks completed.");
				CustomVersionDetected = CustomVersionDetected || DetectCustomVersionFromPackageHash();
				packageHashTasks.Clear();
			}
			else
			{
				packageHashTaskCancelSrc.Cancel();
				packageHashTasks.Clear();
			}
		}
		try
		{
			outputCSProjectPath = Path.GetFullPath(outputCSProjectPath);
			var targetDirectory = Path.GetDirectoryName(outputCSProjectPath);
			if (string.IsNullOrEmpty(targetDirectory))
			{
				Console.Error.WriteLine("Error: Output path is invalid.");
				return -1;
			}
			Common.EnsureDir(targetDirectory);

			Dictionary<string, string> moduleToCsProjPath = [];
			moduleToCsProjPath.Add(MainModule.Name, outputCSProjectPath);

			foreach (var module in AdditionalModules)
			{
				var csProjPath = Path.Combine(targetDirectory, module.SubDirectory!, module.Name + ".csproj");
				moduleToCsProjPath.Add(module.Name, csProjPath);
			}

			ProjectItem decompileFile(GodotModule module, string csprojPath)
			{
				var godotProjectDecompiler = module.GetProjectDecompiler(progress_reporter);
				Common.EnsureDir(Path.GetDirectoryName(csprojPath) ?? "");

				removeIfExists(csprojPath);

				ProjectId projectId;
				var typesToExclude = excludeFiles?.Select(file => Common.TrimPrefix(file, "res://"))
					.Where(module.fileMap.ContainsKey)
					.SelectMany(file => module.fileMap[file])
					.ToHashSet() ?? [];
				var handleToFileMap = module.fileMap
					.SelectMany(pair => pair.Value.Select(h => new KeyValuePair<TypeDefinitionHandle, string>(h, pair.Key)))
					.GroupBy(p => p.Key)
					.ToDictionary(group => group.Key, group => group.First().Value);

				using (var projectFileWriter = new StreamWriter(File.OpenWrite(csprojPath)))
				{
					projectId = godotProjectDecompiler.DecompileGodotProject(
						module.Module, targetDirectory, projectFileWriter, typesToExclude, handleToFileMap, moduleToCsProjPath, module.depInfo, CustomVersionDetected, token);
				}

				ProjectItem item = new ProjectItem(csprojPath, projectId.PlatformName, projectId.Guid, projectId.TypeGuid);
				return item;

			}

			var projectIDs = new List<ProjectItem>();
			projectIDs.Add(decompileFile(MainModule, outputCSProjectPath));
			foreach (var module in AdditionalModules)
			{
				projectIDs.Add(decompileFile(module, moduleToCsProjPath[module.Name]));
				var dir = Path.GetDirectoryName(moduleToCsProjPath[module.Name]);
				if (dir != null)
				{
					Common.EnsureDir(dir);
					// create a .gitignore file in the directory
					var gitignorePath = Path.Combine(dir, ".gitignore");
					if (!File.Exists(gitignorePath))
					{
						var gitignore = File.CreateText(gitignorePath);
						gitignore.WriteLine("bin/*");
						gitignore.WriteLine("obj/*");
						gitignore.Close();
					}
				}
			}
			var solutionPath = Path.ChangeExtension(outputCSProjectPath, ".sln");
			removeIfExists(solutionPath);

			GodotMonoDecomp.SolutionCreator.WriteSolutionFile(solutionPath, projectIDs);
		}
		catch (Exception e)
		{
			Console.Error.WriteLine($"Decompilation failed: {e.Message}");
			// print stacktrace
			Console.Error.WriteLine(e.StackTrace);
			return -1;

		}
		return 0;
	}
	public const string error_message = "// ERROR: Could not find file '{0}' in assembly '{1}.dll'.";

	private (GodotModule?, List<TypeDefinitionHandle>) GetScriptModuleAndTypes(string file)
	{
		var path = Common.TrimPrefix(file, "res://");
		if (!string.IsNullOrEmpty(path))
		{
			List<TypeDefinitionHandle>? foundTypes;
			GodotModule? module = MainModule;
			if (!module.fileMap.TryGetValue(path, out foundTypes))
			{
				module = null;
				foreach (var m in AdditionalModules)
				{
					if (m.fileMap.TryGetValue(path, out foundTypes))
					{
						module = m;
						break;
					}
				}
			}

			return (module, foundTypes ?? []);
		}

		return (null, []);
	}

	public string DecompileIndividualFile(string file)
	{
		var path = Common.TrimPrefix(file, "res://");
		var (module, types) = GetScriptModuleAndTypes(file);
		if (module == null || types.Count == 0)
		{
			return string.Format(error_message, file, MainModule.Name) + (
				originalProjectFiles.Contains(path)
					? "\n// The associated class(es) may have not been compiled into the assembly."
					: "\n// The file is not present in the original project."
			);
		}

		var decompiler = module.CreateCSharpDecompilerWithPartials(types);
		var tree = decompiler.DecompileTypes(types);
		var stringWriter = new StringWriter();
		tree.AcceptVisitor(new GodotCSharpOutputVisitor(stringWriter, Settings, Settings.EmitILAnnotationComments, decompiler));
		return stringWriter.ToString();
	}

	private string GetPathForType(ITypeDefinition? typeDef){
		if (typeDef == null || typeDef.ParentModule == null){
			return "";
		}
		if (typeDef.ParentModule.AssemblyName == MainModule.Name){
			return MainModule.fileMap.FirstOrDefault(pair => pair.Value.Contains((TypeDefinitionHandle)typeDef.MetadataToken)).Key;
		}
		foreach (var module in AdditionalModules){
			if (module.Name == typeDef.ParentModule.AssemblyName){
				return module.fileMap.FirstOrDefault(pair => pair.Value.Contains((TypeDefinitionHandle)typeDef.MetadataToken)).Key;
			}
		}
		return "";
	}

	public GodotScriptInfo? GetScriptInfo(string file)
	{
		var (module, types) = GetScriptModuleAndTypes(file);
		if (module == null || types.Count == 0)
		{
			return null;
		}

		var projectDecompiler = module.GetProjectDecompiler();
		var decompiler = module.CreateCSharpDecompilerWithPartials(types);
		var type = types.FirstOrDefault(h =>
		{
			var maybeTypeDef = decompiler.TypeSystem.MainModule.GetDefinition(h);
			return maybeTypeDef != null && GodotStuff.IsGodotClass(maybeTypeDef);
		});
		if (type == default)
		{
			return null;
		}
		var typeDef = decompiler.TypeSystem.MainModule.GetDefinition(type);

		if (typeDef == null)
		{
			return null;
		}
		if (!GodotStuff.IsGodotClass(typeDef))
		{
			return null;
		}

		var props = typeDef.Fields.Concat<IMember>(typeDef.Properties)
			.Where(p => p.GetAttributes().Any(a => a.AttributeType.Name == "ExportAttribute")).ToList();
		var fields = typeDef.Fields.Where(p => p.GetAttributes().Any(a => a.AttributeType.Name.Contains("Export")))
			.ToList();
		var signals = GodotStuff.GetSignalDelegatesInClass(typeDef);
		var syntaxTree = decompiler.DecompileTypes([type]);
		var isTool = typeDef.GetAttributes().FirstOrDefault(a => a.AttributeType.Name == "ToolAttribute") != null;

		List<PropertyInfo> propsInfos = [];
		List<MethodInfo> signalsInfo = [];
		List<MethodInfo> methodsInfo = [];
		foreach (var prop in props)
		{
			// No need, Export attribute guarantees that the member is public.
			// if (CSharpDecompiler.MemberIsHidden(prop.ParentModule.MetadataFile, prop.MetadataToken,
			// 	    projectDecompiler.Settings))
			// {
			// 	continue;
			// }

			var exportAttr = prop.GetAttributes().FirstOrDefault(a => a.AttributeType.Name == "ExportAttribute");
			string propHint = "";
			string propUsage = "";
			if (exportAttr?.FixedArguments.Length >= 2)
			{
				long hintValue = 0;
				if (exportAttr.FixedArguments[0].Value != null)
				{
					if (exportAttr.FixedArguments[0].Value is int intVal)
					{
						hintValue = intVal;
					}
					else if (exportAttr.FixedArguments[0].Value is long longVal)
					{
						hintValue = longVal;
					}
				}
				propHint = Common.GetEnumValueName(exportAttr.FixedArguments[0].Type, hintValue, "None");
				propUsage = exportAttr.FixedArguments[1].Value?.ToString() ?? "";
			}

			string defaultValue = "";
			// Godot nodes and resources set to instances of CSharp scripts do not have properties set in the scene/resource files if it is set to a default value;
			// As such, we need to get the default value of the property from the CSharp script.
			// This is primarily being done to allow for scenes with CSharp scripts to be instanced for scene export.
			GetFieldInitializerValueVisitor? visitor = null;
			visitor = new GetFieldInitializerValueVisitor(prop, projectDecompiler);
			syntaxTree.AcceptVisitor(visitor);

			if (visitor == null)
			{
				defaultValue = "";
			}
			else if (string.IsNullOrEmpty(visitor.strVal))
			{
				defaultValue = "";
			}
			else
			{
				defaultValue = visitor.strVal;
			}

			propsInfos.Add(new PropertyInfo(prop.Name, prop.ReturnType.Name, defaultValue, propHint, propUsage ?? ""));
		}

		foreach (var signal in signals)
		{
			var invokeMethod = signal.GetMethods().FirstOrDefault(m => m.Name == "Invoke");
			string[] args = [];
			string[] argTypes = [];
			if (invokeMethod != null)
			{
				args = invokeMethod.Parameters.Select(p => p.Name).ToArray();
				argTypes = invokeMethod.Parameters.Select(p => p.Type.Name).ToArray();
			}
			var name = signal.Name;
			 // 4.x signal delegate name requires the suffix, 3.x signals delegate name is the same as the signal name
			if (signal.Name.EndsWith(GodotStuff.SIGNAL_DELEGATE_SUFFIX))
			{
				name = name.Substring(0, name.Length - GodotStuff.SIGNAL_DELEGATE_SUFFIX.Length);
			}
			signalsInfo.Add(new MethodInfo(name, "void", args, argTypes, false, false, false));
		}

		foreach (var method in typeDef.Methods)
		{
			if (CSharpDecompiler.MemberIsHidden(method.ParentModule?.MetadataFile, method.MetadataToken,
				    projectDecompiler.Settings))
			{
				continue;
			}
			if (!GodotStuff.IsBannedGodotTypeMember(method) && (method.Accessibility & Accessibility.Public) != 0)
			{
				var name = method.Name == ".ctor" ? "_init" : method.Name;
				if (method.Name == ".ctor")
				{
					if (method.Parameters.Count == 0)
					{
						continue;
					}
				}
				// _process, _ready, etc.
				if (name.StartsWith("_"))
				{
					name = Common.CamelCaseToSnakeCase(name).ToLower();
				}
				methodsInfo.Add(new MethodInfo(
					name,
					method.ReturnType.Name,
					method.Parameters.Select(p => p.Name).ToArray(),
					method.Parameters.Select(p => p.Type.Name).ToArray(),
					method.IsStatic,
					method.IsAbstract,
					method.IsVirtual
				));
			}
		}

		if (!file.StartsWith("res://"))
		{
			file = "res://" + file;
		}

		var baseTypes = typeDef.DirectBaseTypes.ToList();
		var baseTypePaths = baseTypes.Select(t => GetPathForType(t.GetDefinition())).Select(
			p => string.IsNullOrEmpty(p) ? "" : "res://" + p
		).ToArray();

		ITypeDefinition? GetDirectBaseType(List<IType> baseTypes){
			ITypeDefinition? godotSharpBaseType = null;
			foreach (var baseType in baseTypes)
			{
				var baseTypeDef = baseType.GetDefinition();
				if (baseTypeDef != null && GodotStuff.IsGodotClass(baseTypeDef))
				{
					if (baseTypeDef.GetAttributes().Any(a => a.AttributeType.Name == "GlobalClassAttribute") || baseTypeDef.ParentModule?.AssemblyName == "GodotSharp"){
						godotSharpBaseType = baseTypeDef;
						break;
					} else {
						return GetDirectBaseType(baseTypeDef.DirectBaseTypes.ToList());
					}
				}
			}
			return godotSharpBaseType;
		}

		var godotSharpBaseType = GetDirectBaseType(baseTypes);

		string iconPath = "";
		if (typeDef.GetAttributes().Any(a => a.AttributeType.Name == "IconAttribute"))
		{
			var attr = typeDef.GetAttributes().First(a => a.AttributeType.Name == "IconAttribute");
			if (attr.FixedArguments.Length > 0) {
				iconPath = attr.FixedArguments[0].Value as string ?? "";
			}
		}
		// if we had more than one top-level-type in the file, we need to re-deccompile for all types
		if (types.Count > 1)
		{
			syntaxTree = decompiler.DecompileTypes(types);
		}
        StringWriter stringWriter = new StringWriter();
        syntaxTree.AcceptVisitor(new GodotCSharpOutputVisitor(stringWriter, Settings, Settings.EmitILAnnotationComments, decompiler));
		var scriptText = stringWriter.ToString();

		var scriptInfo = new GodotScriptInfo(
			file,
			typeDef.Namespace,
			typeDef.Name,
			baseTypes.Select(t => t.Name).ToArray(),
			baseTypePaths,
			godotSharpBaseType?.Name ?? null,
			propsInfos.ToArray(),
			signalsInfo.ToArray(),
			methodsInfo.ToArray(),
			isTool,
			typeDef.IsAbstract,
			typeDef.GetAttributes().Any(a => a.AttributeType.Name == "GlobalClassAttribute"),
			iconPath,
			scriptText
		);
		return scriptInfo;
	}

	public void WriteWholeScriptInfo(string file)
	{
		Dictionary<string, GodotScriptInfo> scriptInfos = [];
		foreach (var path in GetFilesInFileMap())
		{
			var scriptInfo = GetScriptInfo(path);
			if (scriptInfo != null)
			{
				scriptInfos.Add(scriptInfo.Path, scriptInfo);
			}
		}
		if (scriptInfos.Count == 0)
		{
			Console.WriteLine("// No script info found.");
			return;
		}

		using (var writer = new StreamWriter(file))
		{
			var opts = new JsonWriterOptions();
			opts.Indented = true;
			opts.Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping;
			var strWriter = new Utf8JsonWriter(writer.BaseStream, opts);
			JsonSerializer.Serialize(strWriter, scriptInfos, SISrcGenContext.Default.DictionaryStringGodotScriptInfo);
			strWriter.Flush();
		}
	}



	public bool anyFileMapsContainsFile(string file)
	{
		var path = Common.TrimPrefix(file, "res://");
		if (!string.IsNullOrEmpty(path))
		{
			if (MainModule.fileMap.ContainsKey(path))
			{
				return true;
			}
			foreach (var module in AdditionalModules)
			{
				if (module.fileMap.ContainsKey(path))
				{
					return true;
				}
			}
		}
		return false;
	}

	public int GetNumberOfFilesNotPresentInFileMap()
	{
		return this.originalProjectFiles.Count(file => !anyFileMapsContainsFile(file));
	}

	public string[] GetFilesNotPresentInFileMap()
	{
		return this.originalProjectFiles.Where(file => !anyFileMapsContainsFile(file)).ToArray();
	}

	public int GetNumberOfFilesInFileMap()
	{
		return MainModule.fileMap.Count + AdditionalModules.Sum(module => module.fileMap.Count);
	}

	public string[] GetFilesInFileMap()
	{
		return MainModule.fileMap.Keys
			.Concat(AdditionalModules.SelectMany(module => module.fileMap.Keys))
			.ToArray();
	}


	private class StringCollectorVisitor : DepthFirstAstVisitor
	{
		private HashSet<string> strings;

		public StringCollectorVisitor(HashSet<string> s)
		{
			strings = s;
		}

		public override void VisitInterpolatedStringText(InterpolatedStringText interpolatedStringText)
		{
			if (!string.IsNullOrEmpty(interpolatedStringText.Text))
			{
				strings.Add(interpolatedStringText.Text);
			}
			base.VisitInterpolatedStringText(interpolatedStringText);
		}

		public override void VisitPrimitiveExpression(PrimitiveExpression primitiveExpression)
		{
			if (primitiveExpression.Value is string str)
			{
				strings.Add(str);
			}
			base.VisitPrimitiveExpression(primitiveExpression);
		}

		public override void VisitPrimitiveType(PrimitiveType primitiveType)
		{
			if (primitiveType.KnownTypeCode == KnownTypeCode.String)
			{
				strings.Add(primitiveType.ToString());
			}
			base.VisitPrimitiveType(primitiveType);
		}


		public override void VisitPatternPlaceholder(AstNode placeholder, Pattern pattern)
		{
			VisitChildren(placeholder);
			VisitNodeInPattern(pattern);
		}

		void VisitAnyNode(AnyNode anyNode)
		{
			if (anyNode is not null)
			{
				VisitChildren(anyNode!);
			}
		}

		void VisitBackreference(Backreference backreference)
		{
			if (backreference is not null)
			{
				VisitChildren(backreference!);
			}
		}

		void VisitIdentifierExpressionBackreference(IdentifierExpressionBackreference identifierExpressionBackreference)
		{
			if (identifierExpressionBackreference is not null)
			{
				VisitChildren(identifierExpressionBackreference!);
			}
		}

		void VisitChoice(Choice choice)
		{
			if (choice is not null)
			{
				VisitChildren(choice!);
			}
		}

		void VisitNamedNode(NamedNode namedNode)
		{
			if (namedNode is not null)
			{
				VisitChildren(namedNode!);
			}
		}

		void VisitRepeat(Repeat repeat)
		{
			if (repeat is not null)
			{
				VisitChildren(repeat!);
			}
		}

		void VisitOptionalNode(OptionalNode optionalNode)
		{
			if (optionalNode is not null)
			{
				VisitChildren(optionalNode!);
			}
		}

		void VisitNodeInPattern(INode childNode)
		{
			if (childNode is AstNode astNode)
			{
				astNode.AcceptVisitor(this);
			}
			else if (childNode is IdentifierExpressionBackreference backreference)
			{
				VisitIdentifierExpressionBackreference(backreference);
			}
			else if (childNode is Choice choice)
			{
				VisitChoice(choice);
			}
			else if (childNode is AnyNode node)
			{
				VisitAnyNode(node);
			}
			else if (childNode is Backreference backreference1)
			{
				VisitBackreference(backreference1);
			}
			else if (childNode is NamedNode node1)
			{
				VisitNamedNode(node1);
			}
			else if (childNode is OptionalNode node2)
			{
				VisitOptionalNode(node2);
			}
			else if (childNode is Repeat repeat)
			{
				VisitRepeat(repeat);
			}
		}
	}

	public IEnumerable<ITypeDefinition> RecursiveGetNestedTypes(ITypeDefinition type)
	{
		if (type == null)
		{
			yield break;
		}
		foreach (var nestedType in type.NestedTypes)
		{
			yield return nestedType;
			foreach (var subNestedType in RecursiveGetNestedTypes(nestedType))
			{
				yield return subNestedType;
			}
		}
	}

	public HashSet<string> GetAllStringsInModule()
	{
		var strings = new HashSet<string>();
		List<GodotModule> list = [MainModule];
		foreach (var module in list.Concat(AdditionalModules))
		{
			var projectDecompiler = module.GetProjectDecompiler();
			var types = projectDecompiler.GetTypesToDecompile(module.Module).ToHashSet();
			var decompiler = module.CreateCSharpDecompilerWithPartials(types);
			var typeDefs = decompiler.TypeSystem.GetAllTypeDefinitions();
			var filteredTypeDefs = typeDefs.Where(t =>
				t.ParentModule?.AssemblyName == module.Name &&
				types.Contains((TypeDefinitionHandle)t.MetadataToken)
				).ToList();

			var enums = filteredTypeDefs.Concat(filteredTypeDefs.SelectMany(RecursiveGetNestedTypes)).Where(t => t.GetAllBaseTypes().Any(b => b.FullName.Contains("System.Enum")));
			// we want to add enum values because you can convert an enum to a string and then use it as a translation key
			strings.AddRange(
				enums
					.SelectMany(t => Common.GetEnumValueNames(t)
					));

			var visitor = new StringCollectorVisitor(strings);
			decompiler.DecompileTypes(types).AcceptVisitor(visitor);
			strings.AddRange(module.fileMap.Keys.Select(f => "res://" + f));
			strings.AddRange(
				filteredTypeDefs.Where(t =>
						types.Contains((TypeDefinitionHandle)t.MetadataToken)
						&& GodotStuff.IsGodotClass(t)
					)
					.SelectMany(t =>
						// only the properties; otherwise there's too much noise
						t.Properties.Select(f => f.Name)
							// .Concat(t.Fields.Select(p => p.Name))
							// .Concat(t.Methods.Select(m => m.Name))
							// .Concat(t.Events.Select(e => e.Name))
							// .Concat(t.NestedTypes.Select(nt => nt.Name))
							.Concat([t.Name])));

		}
		return strings;
	}

	public IEnumerable<byte[]> GetAllUtf32StringsInModule()
	{
		var allstrs = GetAllStringsInModule();
		return allstrs.Select(s => Encoding.UTF32.GetBytes(s))
			.Where(b => b.Length > 0);
	}

	public void DumpStrings(string outputFile)
	{
		var strings = GetAllStringsInModule();
		using (var writer = new StreamWriter(outputFile, false, Encoding.UTF8))
		{
			foreach (var str in strings)
			{
				writer.WriteLine(str);
			}
		}
		Console.WriteLine($"Dumped {strings.Count} strings to {outputFile}");
	}

	public string[] GetGlobalClassFiles() {
		var godotObjectFiles = new List<string>();
		List<GodotModule> list = [MainModule];
		foreach (var module in list.Concat(AdditionalModules))
		{
			var decompiler = module.CreateCSharpDecompilerWithPartials(module.fileMap.SelectMany(kv => kv.Value).ToHashSet());
			foreach (var kv in module.fileMap){
				if (kv.Value.Any(type => {
					var typeDef = decompiler.TypeSystem.MainModule.GetDefinition(type);
					return typeDef != null && GodotStuff.IsGodotClass(typeDef) && typeDef.GetAttributes().Any(a => a.AttributeType.Name == "GlobalClassAttribute");
					})){
					godotObjectFiles.Add(kv.Key);
				}
			}
		}
		return godotObjectFiles.ToArray();
	}

}
