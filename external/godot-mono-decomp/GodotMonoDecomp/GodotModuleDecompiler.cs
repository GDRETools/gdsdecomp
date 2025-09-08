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
	public readonly IDebugInfoProvider? debugInfoProvider;
	public readonly string? SubDirectory;
	public Dictionary<string, TypeDefinitionHandle> fileMap;




	public GodotModule(PEFile module, DotNetCoreDepInfo? depInfo, string? subdir = null, GodotMonoDecompSettings? p_settings = null)
	{
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
	}

	public MetadataReader Metadata => Module.Metadata;
	public string Name => Module.Name;
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

	private readonly List<Task> packageHashTasks;
	private readonly CancellationTokenSource packageHashTaskCancelSrc;


	public void AddSubProjects(string assemblyPath, DotNetCoreDepInfo depInfo, List<string> names, HashSet<string> canonicalSubDirs)
	{
		foreach (var dep in depInfo.deps.Where(d => d is { Type: "project" })
			         .OrderBy(d => d.Name, StringComparer.OrdinalIgnoreCase))
		{

			if (names.Contains(dep.Name)) // already added
			{
				continue;
			}

			names.Add(dep.Name);

			var assemblynameRef = dep.AssemblyRef;
			var supposedPath = Path.Combine(Path.GetDirectoryName(assemblyPath) ?? "", assemblynameRef.Name + ".dll");
			MetadataFile reference = File.Exists(supposedPath) ? new PEFile(supposedPath) : null;

			if (reference == null)
			{
				reference = AssemblyResolver.Resolve(assemblynameRef);
			}

			if (reference == null)
			{
				Console.Error.WriteLine(
					$"Warning: Could not resolve project reference '{dep.Name}' for assembly '{MainModule.Name}'.");
				continue;
			}

			if (reference is PEFile module)
			{
				var subdir = canonicalSubDirs.Contains(module.Name) ? "subprojects" + "/" + module.Name : module.Name;
				while (subdir != null && canonicalSubDirs.Contains(subdir))
				{
					subdir = "_" + subdir;
				}

				AdditionalModules.Add(new GodotModule(module, dep, subdir, Settings));
			}

			AddSubProjects(assemblyPath, dep, names, canonicalSubDirs);
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
		MainModule = new GodotModule(mod, _mainDepInfo, null, Settings);
		AssemblyResolver = new UniversalAssemblyResolver(assemblyPath, false, MainModule.Metadata.DetectTargetFrameworkId());
		foreach (var path in (ReferencePaths ?? System.Array.Empty<string>()))
		{
			AssemblyResolver.AddSearchDirectory(path);
		}


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
			HashSet<string> canonicalSubDirs = GodotStuff.GetCanonicalGodotScriptPaths(MainModule.Module,
			 	CreateProjectDecompiler(MainModule).GetTypesToDecompile(MainModule.Module), godot3xMetadata)
				.Where(p => !string.IsNullOrEmpty(Path.GetDirectoryName(p)))
				.Select(p => Path.GetDirectoryName(p)!)
				.ToHashSet();

			AddSubProjects(assemblyPath, MainModule.depInfo, names, canonicalSubDirs);

			if (Settings.VerifyNuGetPackageIsFromNugetOrg)
			{
				foreach (var dep in MainModule.depInfo.deps.Where(d => d is { Type: "package" } && !ProjectFileWriterGodotStyle.ImplicitGodotReferences.Contains(d.Name)))
				{
					packageHashTasks.Add(
						Task.Run(async () => await dep.StartResolvePackageAndCheckHash(packageHashTaskCancelSrc.Token), packageHashTaskCancelSrc.Token)
						);
				}
				foreach (var module in AdditionalModules)
				{
					foreach (var dep in module.depInfo?.deps.Where(d => d is { Type: "package" } && !ProjectFileWriterGodotStyle.ImplicitGodotReferences.Contains(d.Name)) ?? [])
					{
						packageHashTasks.Add(
							Task.Run(async () => await dep.StartResolvePackageAndCheckHash(packageHashTaskCancelSrc.Token), packageHashTaskCancelSrc.Token)
							);
					}
				}

			}
		}


		HashSet<string> excludeSubdirs = AdditionalModules.Select(module => module.Name).ToHashSet();

		var typesToDecompile = CreateProjectDecompiler(MainModule).GetTypesToDecompile(MainModule.Module).ToHashSet();
		MainModule.fileMap = GodotStuff.CreateFileMap(MainModule.Module, typesToDecompile, this.originalProjectFiles, godot3xMetadata, excludeSubdirs, true);
		var additionalModuleCount = 0;
		var fileToModuleMap = MainModule.fileMap.ToDictionary(
			pair => pair.Key,
			pair => MainModule,//.Module.FileName,
			StringComparer.OrdinalIgnoreCase);
		// var moduleFileNameToMouduleMap = new Dictionary<string, GodotModule>(StringComparer.OrdinalIgnoreCase);
		foreach (var module in AdditionalModules)
		{
			// TODO: make CreateFileMap() work with multiple modules
			typesToDecompile = CreateProjectDecompiler(MainModule).GetTypesToDecompile(module.Module).ToHashSet();

			var nfileMap = GodotStuff.CreateFileMap(module.Module, typesToDecompile, this.originalProjectFiles, godot3xMetadata, null, true);
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
				module.fileMap.Add(fixedPath, pair.Value);
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

	GodotProjectDecompiler CreateProjectDecompiler(GodotModule module, IProgress<DecompilationProgress>? progress_reporter = null)
	{
		var moduleSettings = Settings.Clone();
		moduleSettings.SetLanguageVersion(module.languageVersion);
		var decompiler = new GodotProjectDecompiler(moduleSettings, AssemblyResolver, AssemblyResolver, module.debugInfoProvider);
		decompiler.ProgressIndicator = progress_reporter;
		return decompiler;
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
				var godotProjectDecompiler = CreateProjectDecompiler(module, progress_reporter);
				Common.EnsureDir(Path.GetDirectoryName(csprojPath));

				removeIfExists(csprojPath);

				ProjectId projectId;
				var typesToExclude = excludeFiles?.Select(file => Common.TrimPrefix(file, "res://")).Where(module.fileMap.ContainsKey).Select(file => module.fileMap[file]).ToHashSet() ?? [];

				using (var projectFileWriter = new StreamWriter(File.OpenWrite(csprojPath)))
				{
					projectId = godotProjectDecompiler.DecompileGodotProject(
						module.Module, targetDirectory, projectFileWriter, typesToExclude, module.fileMap.ToDictionary(pair => pair.Value, pair => pair.Key), module.depInfo, token, moduleToCsProjPath);
				}

				ProjectItem item = new ProjectItem(csprojPath, projectId.PlatformName, projectId.Guid, projectId.TypeGuid);
				return item;

			}

			var projectIDs = new List<ProjectItem>();
			projectIDs.Add(decompileFile(MainModule, outputCSProjectPath));
			foreach (var module in AdditionalModules)
			{
				projectIDs.Add(decompileFile(module, moduleToCsProjPath[module.Name]));
			}
			var solutionPath = Path.ChangeExtension(outputCSProjectPath, ".sln");
			removeIfExists(solutionPath);

			GodotMonoDecomp.SolutionCreator.WriteSolutionFile(solutionPath, projectIDs);
		}
		catch (Exception e)
		{
			Console.Error.WriteLine($"Decompilation failed: {e.Message}");
			return -1;

		}
		return 0;
	}
	public const string error_message = "// ERROR: Could not find file '{0}' in assembly '{1}.dll'.";

	private (GodotModule?, TypeDefinitionHandle) GetScriptModuleAndType(string file)
	{
		var path = Common.TrimPrefix(file, "res://");
		if (!string.IsNullOrEmpty(path))
		{
			TypeDefinitionHandle foundType;
			GodotModule? module = MainModule;
			if (!module.fileMap.TryGetValue(path, out foundType))
			{
				module = null;
				foreach (var m in AdditionalModules)
				{
					if (m.fileMap.TryGetValue(path, out foundType))
					{
						module = m;
						break;
					}
				}
			}

			return (module, foundType);
		}

		return (null, default(TypeDefinitionHandle));
	}

	public string DecompileIndividualFile(string file)
	{
		var path = Common.TrimPrefix(file, "res://");
		var (module, type) = GetScriptModuleAndType(file);
		if (module == null || type == default)
		{
			return string.Format(error_message, file, MainModule.Name) + (
				originalProjectFiles.Contains(path)
					? "\n// The associated class(es) may have not been compiled into the assembly."
					: "\n// The file is not present in the original project."
			);
		}

		var projectDecompiler = CreateProjectDecompiler(module);
		var decompiler = projectDecompiler.CreateDecompilerWithPartials(module.Module, [type]);
		return decompiler.DecompileTypesAsString([type]);
	}

	private string GetPathForType(ITypeDefinition? typeDef){
		if (typeDef == null || typeDef.ParentModule == null){
			return "";
		}
		if (typeDef.ParentModule.AssemblyName == MainModule.Name){
			return MainModule.fileMap.FirstOrDefault(pair => pair.Value == (TypeDefinitionHandle)typeDef.MetadataToken).Key;
		}
		foreach (var module in AdditionalModules){
			if (module.Name == typeDef.ParentModule.AssemblyName){
				return module.fileMap.FirstOrDefault(pair => pair.Value == (TypeDefinitionHandle)typeDef.MetadataToken).Key;
			}
		}
		return "";
	}

	public GodotScriptInfo? GetScriptInfo(string file)
	{
		var (module, type) = GetScriptModuleAndType(file);
		if (module == null || type == default)
		{
			return null;
		}

		var projectDecompiler = CreateProjectDecompiler(module);
		var decompiler = projectDecompiler.CreateDecompilerWithPartials(module.Module, [type]);
		var allTypeDefs = decompiler.TypeSystem.GetAllTypeDefinitions();
		var typeDef = allTypeDefs.FirstOrDefault(t => t.MetadataToken == type);

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
		var signals = GodotStuff.GetSignalsInClass(typeDef);
		var syntaxTree = decompiler.Decompile([type]);
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
				int hintValue = exportAttr.FixedArguments[0].Value as int? ?? 0;
				propHint = Common.GetEnumValueName(exportAttr.FixedArguments[0].Type, hintValue, "None");
				propUsage = exportAttr.FixedArguments[1].Value.ToString();
			}

			string defaultValue = "";
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
			signalsInfo.Add(new MethodInfo(signal.Name, "void", args, argTypes, false, false, false));
		}

		foreach (var method in typeDef.Methods)
		{
			if (CSharpDecompiler.MemberIsHidden(method.ParentModule.MetadataFile, method.MetadataToken,
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



		var scriptInfo = new GodotScriptInfo(
			file,
			typeDef.Namespace,
			typeDef.Name,
			baseTypes.Select(t => t.Name).ToArray(),
			baseTypePaths,
			propsInfos.ToArray(),
			signalsInfo.ToArray(),
			methodsInfo.ToArray(),
			isTool,
			typeDef.IsAbstract
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
			if (interpolatedStringText.Text != null)
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
			else
			{
				var bof = false;
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
			VisitChildren(anyNode);
		}

		void VisitBackreference(Backreference backreference)
		{
			VisitChildren(backreference);
		}

		void VisitIdentifierExpressionBackreference(IdentifierExpressionBackreference identifierExpressionBackreference)
		{
			VisitChildren(identifierExpressionBackreference);
		}

		void VisitChoice(Choice choice)
		{
			VisitChildren(choice);		}

		void VisitNamedNode(NamedNode namedNode)
		{
			VisitChildren(namedNode);
		}

		void VisitRepeat(Repeat repeat)
		{
			VisitChildren(repeat);
		}

		void VisitOptionalNode(OptionalNode optionalNode)
		{
			VisitChildren(optionalNode);
		}

		void VisitNodeInPattern(INode childNode)
		{
			if (childNode is AstNode)
			{
				((AstNode)childNode).AcceptVisitor(this);
			}
			else if (childNode is IdentifierExpressionBackreference)
			{
				VisitIdentifierExpressionBackreference((IdentifierExpressionBackreference)childNode);
			}
			else if (childNode is Choice)
			{
				VisitChoice((Choice)childNode);
			}
			else if (childNode is AnyNode)
			{
				VisitAnyNode((AnyNode)childNode);
			}
			else if (childNode is Backreference)
			{
				VisitBackreference((Backreference)childNode);
			}
			else if (childNode is NamedNode)
			{
				VisitNamedNode((NamedNode)childNode);
			}
			else if (childNode is OptionalNode)
			{
				VisitOptionalNode((OptionalNode)childNode);
			}
			else if (childNode is Repeat)
			{
				VisitRepeat((Repeat)childNode);
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
		var regex = new Regex(@"(?:^|[^\\\$])""((?:\\""|[^""])*?)""");
		List<GodotModule> list = [MainModule];
		foreach (var module in list.Concat(AdditionalModules))
		{
			var projectDecompiler = CreateProjectDecompiler(module);
			var types = projectDecompiler.GetTypesToDecompile(module.Module).ToHashSet();
			var decompiler = projectDecompiler.CreateDecompilerWithPartials(module.Module, types);
			var typeDefs = decompiler.TypeSystem.GetAllTypeDefinitions();
			var filteredTypeDefs = typeDefs.Where(t =>
				t.ParentModule.AssemblyName == module.Name &&
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


}
