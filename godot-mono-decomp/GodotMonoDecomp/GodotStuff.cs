using System.Collections.Immutable;
using System.Reflection.Metadata;
using ICSharpCode.Decompiler;
using ICSharpCode.Decompiler.CSharp;
using ICSharpCode.Decompiler.CSharp.Syntax;
using ICSharpCode.Decompiler.CSharp.Transforms;
using ICSharpCode.Decompiler.Metadata;
using ICSharpCode.Decompiler.Semantics;
using ICSharpCode.Decompiler.TypeSystem;

namespace GodotMonoDecomp;

public class GetFieldInitializerValueVisitor : DepthFirstAstVisitor
{
	public string? strVal = null;

	private IMember targetMember;
	private GodotProjectDecompiler godotDecompiler;
	private bool found = false;
	private SyntaxTree syntaxTree;

	public GetFieldInitializerValueVisitor(IMember member, GodotProjectDecompiler godotDecompiler)
	{
		this.godotDecompiler = godotDecompiler;
		this.targetMember = member;
	}

	public override void VisitSyntaxTree(SyntaxTree syntaxTree)
	{
		this.syntaxTree = syntaxTree;
		base.VisitSyntaxTree(syntaxTree);
	}

	protected override void VisitChildren(AstNode node)
	{
		if (found)
		{
			return;
		}

		base.VisitChildren(node);
	}

	public override void VisitPropertyDeclaration(PropertyDeclaration propertyDeclaration)
	{
		if (found)
		{
			return;
		}
		if (targetMember is IProperty property && Equals(propertyDeclaration.GetSymbol(), property))
		{
			var initializer = propertyDeclaration.Initializer;
			if (!(initializer == null || initializer == Expression.Null))
			{
				strVal = GetInitializerValue(initializer);
			}
			else
			{
				if (propertyDeclaration.ExpressionBody != null)
				{
					strVal = GetInitializerValue(propertyDeclaration.ExpressionBody);
				}

			}

			found = true;
			return;
		}

		base.VisitPropertyDeclaration(propertyDeclaration);
	}

	public string? GetInitializerValue(Expression initializer)
	{
		string? value = null;
		if (initializer == null || initializer == Expression.Null)
		{
			return null;
		}
		else
		{
			var init = initializer;
			var sym = initializer.GetSymbol();
			if (sym is IVariable f && f.IsConst)
			{
				value = GodotExpressionTokenWriter.PrintPrimitiveValue(f.GetConstantValue(false));
			}
			else if (init is PrimitiveExpression primitiveExpression)
			{
				value = GodotExpressionTokenWriter.PrintPrimitiveValue(primitiveExpression.Value);
			}
			else if (init is InterpolatedStringExpression interpolatedStringExpression)
			{
				value = GodotExpressionOutputVisitor.GetString(interpolatedStringExpression);
			}
			else if (init is IdentifierExpression identifierExpression)
			{
				if (sym is IMember symMem && symMem.DeclaringType.Equals(targetMember.DeclaringType))
				{
					var vis = new GetFieldInitializerValueVisitor(symMem, godotDecompiler);
					this.syntaxTree.AcceptVisitor(vis);
					value = vis.strVal;
				}
				else
				{
					value = identifierExpression.Identifier;
				}
			}
			else if (init is ObjectCreateExpression oce)
			{
				if (oce.Children.Count() == 1  && oce.LastChild is SimpleType simpleType)
				{
					if (simpleType.IdentifierToken.Name == "Array")
					{
						value = "[]";
					}
					else if (simpleType.IdentifierToken.Name == "Dictionary")
					{
						value = "{}";
					}
					else
					{
						if (GodotStuff.IsGodotVariantType(simpleType.IdentifierToken.Name))
						{
							value = GodotStuff.CSharpTypeToGodotType(simpleType.IdentifierToken.Name) + "()";
						}
						else
						{
							// TODO: this?
							// value = GodotExpressionOutputVisitor.GetString(oce.LastChild) + ".new()";
							value = "";
						}
					}
				}
				else
				{
					var sr = GodotExpressionOutputVisitor.GetString(oce);
					value = Common.TrimPrefix(sr, "new").Trim();
				}
			}
			else if (init is MemberReferenceExpression memberReferenceExpression)
			{
				value = GodotStuff.ReplaceMemberReference(memberReferenceExpression);
				if (value == null)
				{
					var s = memberReferenceExpression.GetSymbol();
					if (s is IProperty || s is IField)
					{
						IMember prop = (IMember)s;
						var declaringType = prop.DeclaringType.GetDefinition();
						if (declaringType == null || declaringType.ParentModule == null)
						{
							return GodotExpressionOutputVisitor.GetString(memberReferenceExpression);
						}
						var decompiler = godotDecompiler.CreateDecompilerWithPartials(declaringType.ParentModule.MetadataFile, [(TypeDefinitionHandle)declaringType.MetadataToken]);
						var tree = decompiler.Decompile([declaringType.MetadataToken]);
						GetFieldInitializerValueVisitor vis;
						if (memberReferenceExpression.GetSymbol() is IMember m)
						{
							vis =  new GetFieldInitializerValueVisitor(m, godotDecompiler);
							tree.AcceptVisitor(vis);
							value = vis.strVal;
						}

						if (value == null)
						{
							value = GodotExpressionOutputVisitor.GetString(memberReferenceExpression);
						}
					}
				}
			}

			else
			{
				value = GodotExpressionOutputVisitor.GetString(init);
			}
		}
		return value;
	}


	public override void VisitFieldDeclaration(FieldDeclaration fieldDeclaration)
	{
		if (found)
		{
			return;
		}

		if (targetMember is IField field && Equals(fieldDeclaration.GetSymbol(), field))
		{
			// TODO: instantiate it and just get the value that way?
			// var declaringType = field.DeclaringType;
			// var declaringTypeDefinition = declaringType.GetDefinition();
			// var module = declaringTypeDefinition.ParentModule;
			// var obj = Activator.CreateInstance(module.AssemblyName, declaringType.FullName);

			var intializers = fieldDeclaration.Variables;
			foreach (var initializer in intializers)
			{
				strVal = GetInitializerValue(initializer.Initializer);
				if (strVal != null)
				{
					break;
				}
			}

			found = true;
			return;
		}

		base.VisitFieldDeclaration(fieldDeclaration);
	}
}

/// <summary>
/// This transform is used to remove Godot.ScriptPathAttribute from the AST.
/// </summary>
public class RemoveGodotScriptPathAttribute : IAstTransform
{

	public void Run(AstNode rootNode, TransformContext context)
	{

		foreach (var section in rootNode.Children.OfType<NamespaceDeclaration>())
		{
			Run(section, context);
		}
		foreach (var section in rootNode.Children.OfType<TypeDeclaration>())
		{
			Run(section, context);
		}
		foreach (var section in rootNode.Children.OfType<AttributeSection>())
		{
			foreach (var attribute in section.Attributes)
			{
				var trr = attribute.Type.Annotation<TypeResolveResult>();
				if (trr == null)
					continue;

				string fullName = trr.Type.FullName;
				var arguments = attribute.Arguments;
				switch (fullName)
				{
					case "Godot.ScriptPathAttribute":
					{
						attribute.Remove();
						break;
					}
				}
			}

			if (section.Attributes.Count == 0)
			{
				section.Remove();
			}
		}
	}
}


public static class GodotStuff
{
	public const string BACKING_FIELD_PREFIX = "backing_";


	public static string? GetScriptPathAttributeValue(MetadataReader metadata, TypeDefinitionHandle h)
	{
		var type = metadata.GetTypeDefinition(h);
		var attrs = type.GetCustomAttributes();
		foreach (var attrHandle in attrs)
		{
			var customAttr = metadata.GetCustomAttribute(attrHandle);
			var str = customAttr.ToString();
			var attrName = customAttr.GetAttributeType(metadata).GetFullTypeName(metadata).ToString();
			if (attrName != "Godot.ScriptPathAttribute")
			{
				continue;
			}
			var value = customAttr.DecodeValue(MetadataExtensions.MinimalAttributeTypeProvider);
			if (value.FixedArguments.Length == 0)
			{
				continue;
			}
			return value.FixedArguments[0].Value as string;
		}

		return null;
	}

	public static IEnumerable<string> GetCanonicalGodotScriptPaths(MetadataFile module,
		IEnumerable<TypeDefinitionHandle> typesToDecompile,
		Dictionary<string, GodotScriptMetadata>? scriptMetadata)
	{
		Dictionary<string, string>? metadataFQNToFileMap = scriptMetadata?.ToDictionary(
			pair => pair.Value.Class.GetFullClassName(),
			pair => pair.Key,
			StringComparer.OrdinalIgnoreCase);

		var metadata = module.Metadata;
		foreach (var h in typesToDecompile)
		{
			var scriptPath = Common.TrimPrefix(GetScriptPathAttributeValue(metadata, h) ?? "", "res://");
			if (!string.IsNullOrEmpty(scriptPath))
			{
				yield return scriptPath;
			}
			else if (metadataFQNToFileMap != null)
			{
				var fqn = metadata.GetTypeDefinition(h).GetFullTypeName(metadata).ToString();
				if (metadataFQNToFileMap.TryGetValue(fqn, out var filePath))
				{
					yield return Common.TrimPrefix(filePath, "res://");
				}
			}
		}
	}


	public static Dictionary<string, TypeDefinitionHandle> CreateFileMap(MetadataFile module,
		IEnumerable<TypeDefinitionHandle> typesToDecompile,
		List<string> filesInOriginal,
		Dictionary<string, GodotScriptMetadata>? scriptMetadata,
		IEnumerable<string>? excludedSubdirectories,
		bool useNestedDirectoriesForNamespaces)
	{
		var fileMap = new Dictionary<string, TypeDefinitionHandle>();
		var metadata = module.Metadata;
		Dictionary<string, string> metadataFQNToFileMap = null;
		if (scriptMetadata != null)
		{
			// create a map of metadata FQN to file path
			metadataFQNToFileMap = scriptMetadata.ToDictionary(
				pair => pair.Value.Class.GetFullClassName(),
				pair => pair.Key,
				StringComparer.OrdinalIgnoreCase);
		}

		excludedSubdirectories = excludedSubdirectories?.Select(e => {
			if (e.EndsWith('\\'))
			{
				// replace it
				return e.Substring(0, e.Length - 1) + '/';
			} else if (e.EndsWith('/')) {
				return e;
			}
			return e + '/';
		}).ToList();

		bool nosubdirs = excludedSubdirectories == null || !excludedSubdirectories.Any();

		// ensure that all paths use forward slashes in fileMap to match Godot's path style
		string _NormalizePath(string path)
		{
			return path.Replace(Path.DirectorySeparatorChar, '/');
		}

		string _PathCombine(string a, string b)
		{
			return _NormalizePath(Path.Combine(a, b));
		}

		bool IsExcludedSubdir(string? dir)
		{
			if (nosubdirs || string.IsNullOrEmpty(dir))
			{
				return false;
			}
			return excludedSubdirectories!.Any(e => dir.StartsWith(e, StringComparison.OrdinalIgnoreCase));
		}

		bool IsInExcludedSubdir(string? file)
		{
			if (nosubdirs || string.IsNullOrEmpty(file))
			{
				return false;
			}
			return IsExcludedSubdir(Path.GetDirectoryName(file));
		}

		var processAgain = new HashSet<TypeDefinitionHandle>();
		var namespaceToFile = new Dictionary<string, List<string>>();
		var namespaceToDirectory = new Dictionary<string, HashSet<string>>();
		void addToNamespaceToFile(string ns, string file)
		{
			var dir = Path.GetDirectoryName(file) ?? "";
			if (namespaceToFile.ContainsKey(ns))
			{
				namespaceToFile[ns].Add(file);
				namespaceToDirectory[ns].Add(dir);
			}
			else
			{
				namespaceToFile[ns] = new List<string> { file };
				namespaceToDirectory[ns] = new HashSet<string> { dir };
			}
		}
		foreach (var h in typesToDecompile)
		{
			var type = metadata.GetTypeDefinition(h);
			var scriptPath = Common.TrimPrefix(GetScriptPathAttributeValue(metadata, h) ?? "", "res://");

			// we explicitly don't check if it's in an excluded subdirectory here because a script path attribute means
			// that the file is referenced by other files in the project, so the path MUST match.
			if (!string.IsNullOrEmpty(scriptPath))
			{
				addToNamespaceToFile(metadata.GetString(type.Namespace),scriptPath);
				fileMap[scriptPath] = h;
			}
			else
			{
				// Same here.
				if (metadataFQNToFileMap != null)
				{
					// check if the type has a metadata FQN in the script metadata
					var fqn = type.GetFullTypeName(metadata).ToString();
					if (metadataFQNToFileMap.TryGetValue(fqn, out var filePath))
					{
						filePath = Common.TrimPrefix(filePath, "res://");
						addToNamespaceToFile(metadata.GetString(type.Namespace), filePath);
						fileMap[filePath] = h;
						continue;
					}
				}
				processAgain.Add(h);
			}
		}

		string default_dir = "src";
		while (IsExcludedSubdir(default_dir)){
			default_dir = "_" + default_dir;
		}


		string GetAutoFileNameForHandle(TypeDefinitionHandle h)
		{
			var type = metadata.GetTypeDefinition(h);

			string file = GodotProjectDecompiler.CleanUpFileName(metadata.GetString(type.Name), ".cs");
			string ns = metadata.GetString(type.Namespace);
			if (string.IsNullOrEmpty(ns))
			{
				return file;
			}
			else
			{
				string dir = useNestedDirectoriesForNamespaces ? GodotProjectDecompiler.CleanUpPath(ns) : GodotProjectDecompiler.CleanUpDirectoryName(ns);
				// ensure dir separator is '/'
				dir = dir.Replace(Path.DirectorySeparatorChar, '/');
				// TODO: come back to this
				if (IsExcludedSubdir(dir) /*|| dir == ""*/)
				{
					dir = default_dir;
				}
				return _PathCombine(dir, file);
			}
		}

		string GetPathFromOriginalFiles(string file_path)
		{
			// otherwise, try to find it in the original directory files
			string scriptPath = "";
			// empty vector of strings
			var possibles = filesInOriginal.Where(f =>
					!fileMap.ContainsKey(f) &&
					Path.GetFileName(f) == Path.GetFileName(file_path)
					&& !IsInExcludedSubdir(f)
				)
				.ToList();

			if (possibles.Count == 0)
			{
				possibles = filesInOriginal.Where(f =>
					!fileMap.ContainsKey(f) &&
					Path.GetFileName(f).ToLower() == Path.GetFileName(file_path).ToLower()
					&& !IsInExcludedSubdir(f)
				).ToList();
			}

			if (possibles.Count == 1)
			{
				scriptPath = possibles[0];
			}
			else if (possibles.Count > 1)
			{
				possibles = possibles.Where(f => f.EndsWith(file_path, StringComparison.OrdinalIgnoreCase)).ToList();
				if (possibles.Count == 1)
				{
					scriptPath = possibles[0];
				}
			}
			if (string.IsNullOrEmpty(scriptPath) && possibles.Count > 1){
				return "<multiple>";
			}
			return scriptPath;
		}

		var potentialMap = new Dictionary<string, List<TypeDefinitionHandle>>();


		var processAgainAgain = new HashSet<TypeDefinitionHandle>();
		var dupes = new HashSet<TypeDefinitionHandle>();

		foreach (var h in processAgain)
		{
			var path = GetAutoFileNameForHandle(h);
			var real_path = GetPathFromOriginalFiles(path);
			if (real_path != "" && real_path != "<multiple>" && !IsInExcludedSubdir(real_path))
			{
				if (!potentialMap.ContainsKey(real_path))
				{
					potentialMap[real_path] = new List<TypeDefinitionHandle>();
				}
				potentialMap[real_path].Add(h);
			} else {
				if (real_path == "<multiple>" && !dupes.Contains(h))
				{
					dupes.Add(h);
				}
				else
				{
					processAgainAgain.Add(h);
				}
			}
		}

		foreach (var pair in potentialMap)
		{
			if (pair.Value.Count == 1)
			{
				var type = metadata.GetTypeDefinition(pair.Value[0]);
				addToNamespaceToFile(metadata.GetString(type.Namespace), pair.Key);
				fileMap[pair.Key] = pair.Value[0];
			} else {
				foreach (var h in pair.Value)
				{
					processAgainAgain.Add(h);
				}
			}
		}


		HashSet<string> GetNamespaceDirectories(string ns)
		{
			return namespaceToDirectory.TryGetValue(ns, out var v1)
				? v1
				: [];
		}

		foreach (var h in processAgainAgain)
		{
			var type = metadata.GetTypeDefinition(h);
			var ns = metadata.GetString(type.Namespace);
			var auto_path = GetAutoFileNameForHandle(h);
			string? p = null;
			var namespaceParts = ns.Split('.');
			var parentNamespace = ns.Contains('.') ? ns.Split('.')[0] : "";
			if (ns != "" && ns != null)
			{
				// pop off the first part of the path, if necessary
				var fileStem = Common.RemoveNamespacePartOfPath(auto_path, ns);
				var directories = GetNamespaceDirectories(ns).Where(d => !string.IsNullOrEmpty(d) && !IsInExcludedSubdir(d)).ToHashSet();

				if (directories.Count == 1)
				{
					p = _PathCombine(directories.First(), fileStem);
				}
				// check if the namespace has a parent
				else if (string.IsNullOrEmpty(p) && directories.Count <= 1 && parentNamespace.Length != 0 &&
						namespaceToFile.ContainsKey(parentNamespace))
				{
					var parentDirectories = GetNamespaceDirectories(parentNamespace).Where(d => !string.IsNullOrEmpty(d) && !IsInExcludedSubdir(d)).ToHashSet();
					var child = ns.Substring(parentNamespace.Length + 1).Replace('.', '/');
					fileStem = _PathCombine(child, Path.GetFileName(auto_path));
					if (parentDirectories.Count == 1)
					{
						p = _PathCombine(parentDirectories.First(), fileStem);
					}

					directories = parentDirectories;
				}

				else if (string.IsNullOrEmpty(p) && directories.Count > 1)
				{
					var commonRoot = Common.FindCommonRoot(directories);
					if (!string.IsNullOrEmpty(commonRoot))
					{
						p = _PathCombine(commonRoot, fileStem);
					}
				}
			}
			if (string.IsNullOrEmpty(p) || fileMap.ContainsKey(p))
			{
				p = auto_path;
			}
			p = _NormalizePath(p);
			fileMap[p] = h;
		}

		foreach (var h in dupes)
		{
			var auto_path = GetAutoFileNameForHandle(h);
			var scriptPath = GetPathFromOriginalFiles(auto_path);

			if (scriptPath == "" || scriptPath == "<multiple>" || fileMap.ContainsKey(scriptPath))
			{
				scriptPath = auto_path;
			}

			fileMap[scriptPath] = h;
		}
		return fileMap.ToDictionary(
			pair => pair.Key,
			pair => pair.Value,
			StringComparer.OrdinalIgnoreCase);
	}


	public static List<PartialTypeInfo> GetPartialGodotTypes(DecompilerTypeSystem ts,
		IEnumerable<TypeDefinitionHandle> typesToDecompile)
	{

		var partialTypes = new List<PartialTypeInfo>();

		void addPartialTypeInfo(ITypeDefinition typeDef)
		{
			if (GodotStuff.IsGodotPartialClass(typeDef))
			{
				IEnumerable<IMember> fieldsAndProperties = typeDef.Fields.Concat<IMember>(typeDef.Properties);

				IEnumerable<IMember> allOrderedMembers =
					fieldsAndProperties.Concat(typeDef.Events).Concat(typeDef.Methods);

				var allOrderedEntities = typeDef.NestedTypes.Concat<IEntity>(allOrderedMembers).ToArray();

				var partialTypeInfo = new PartialTypeInfo(typeDef);
				foreach (var member in allOrderedEntities)
				{
					if (GodotStuff.IsBannedGodotTypeMember(member))
					{
						partialTypeInfo.AddDeclaredMember(member.MetadataToken);
					}
				}

				partialTypes.Add(partialTypeInfo);
			}
			// embedded types, too
			for (int i = 0; i < typeDef.NestedTypes.Count; i++)
			{
				var nestedType = typeDef.NestedTypes[i];
				if (GodotStuff.IsGodotPartialClass(nestedType))
				{
					addPartialTypeInfo(nestedType);
				}
			}
		}
		foreach (var type in typesToDecompile)
		{
			// get the type definition from allTypeDefs where the metadata token matches
			var typeDef = ts.GetAllTypeDefinitions()
				.FirstOrDefault(td => td.MetadataToken.Equals(type));
			if (typeDef == null)
			{
				continue;
			}
			addPartialTypeInfo(typeDef);

		}

		return partialTypes;
	}

	public static bool ModuleDependsOnGodotSharp(MetadataFile module)
	{
		return module.AssemblyReferences.Any(r => r.Name == "GodotSharp");
	}

	public static bool ModuleDependsOnGodotSourceGenerators(MetadataFile module)
	{
		return module.AssemblyReferences.Any(r => r.Name == "Godot.SourceGenerators");
	}

	public static bool IsGodotClass(ITypeDefinition entity)
	{
		if (entity == null)
		{
			return false;
		}
		return entity.GetAllBaseTypes().Any(t => t.Name == "GodotObject" || t.FullName == "Godot.Object");
	}

	public static bool IsGodotPartialClass(ITypeDefinition entity)
	{
		// check if the entity is a member of a type that derives from GodotObject
		if (!IsGodotClass(entity)) return false;

		// check if it's version 3 or lower; version 3 had no partial classes
		if (entity.ParentModule?.MetadataFile != null)
		{
			if (GetGodotVersion(entity.ParentModule.MetadataFile)?.Major <= 3){
				// Just in case the creator somehow built GodotSharp themselves without a version number
				return ModuleDependsOnGodotSourceGenerators(entity.ParentModule.MetadataFile);
			}
		}

		return true;
	}


	// list all .cs files in the directory and subdirectories

	public static bool IsSignalDelegate(IEntity entity)
	{
		var attributes = entity.GetAttributes();

		// check if any of the attributes are "Signal"
		if (attributes.Any(a => a.AttributeType.FullName == "Godot.SignalAttribute"))
		{
			return true;
		}

		if (attributes.Any(a => a.AttributeType.Name == "SignalAttribute"))
		{
			return true;
		}

		return false;
	}

	public static IEnumerable<IType> GetSignalsInClass(ITypeDefinition entity)
	{
		return entity.NestedTypes.Where(IsSignalDelegate);
	}

	public static bool IsBackingSignalDelegateField(IEntity entity)
	{
		if (entity is IField field)
		{
			return field.Name.StartsWith(BACKING_FIELD_PREFIX) && field.DeclaringTypeDefinition != null &&
			       GetSignalsInClass(field.DeclaringTypeDefinition).Contains(field.Type.GetDefinition());
		}

		return false;
	}

	public static IEnumerable<IEntity> GetBackingSignalDelegateFieldsInClass(ITypeDefinition entity)
	{
		return entity.Fields.Where(IsBackingSignalDelegateField);
	}

	public static IEnumerable<string> GetBackingSignalDelegateFieldNames(ITypeDefinition entity)
	{
		return GetBackingSignalDelegateFieldsInClass(entity).Select(f => f.Name);
	}

	public static Version? GetGodotVersion(MetadataFile file)
	{
		if (file == null)
		{
			return null;
		}
		// look through all the assembly references in the file until we find one named "GodotSharp"
		var godotSharpReference = file.AssemblyReferences.FirstOrDefault(r => r.Name == "GodotSharp");
		return godotSharpReference?.Version;
	}

	public static Version? ParseGodotVersionFromString(string versionString)
	{
		if (string.IsNullOrWhiteSpace(versionString))
		{
			return null;
		}

		if (Version.TryParse(versionString, out var v))
		{
			return v;
		}
		// else, split the string by '.' and parse each part
		var parts = versionString.Split('.');
		int verMajor = 0;
		int verMinor = 0;
		int verBuild = 0;
		int verRevision = -1;
		int len = parts.Length > 4 ? 4 : parts.Length;
		for (int i = 0; i < len; i++)
		{
			var part = parts[i];
			// check if it's a valid integer
			if (int.TryParse(part, out var intPart))
			{
				switch (i)
				{
					case 0:
						verMajor = intPart;
						break;
					case 1:
						verMinor = intPart;
						break;
					case 2:
						verBuild = intPart;
						break;
					case 3:
						verRevision = intPart;
						break;
				}
			}
			else
			{
				break;
			}
		}

		if (verMajor == 0)
		{
			return null;
		}
		if (verRevision == -1)
		{
			return new Version(verMajor, verMinor, verBuild);
		}
		return new Version(verMajor, verMinor, verBuild, verRevision);
	}

	/// <summary>
	/// Check to see if any of the attributes are System.ComponentModel.EditorBrowsableAttribute
	/// with a System.ComponentModel.EditorBrowsableState.Never argument
	/// </summary>
	public static bool HasEditorNonBrowsableAttribute(IEntity entity)
	{
		return entity.GetAttributes().Any(a =>
			    a.AttributeType.FullName == "System.ComponentModel.EditorBrowsableAttribute"
			    && a.FixedArguments is [
			    {
				    Value: (int)System.ComponentModel.EditorBrowsableState.Never or System.ComponentModel.EditorBrowsableState.Never
			    } _]);
	}

	public static bool IsCompilerGeneratedAccessorMethod(IMethod method)
	{
		// if it's compiler generated, it won't be marked as virtual and it won't be an actual accessor method
		if (!method.Name.Contains('.') || method.IsVirtual || method.IsAccessor)
		{
			return false;
		}

		bool isAdder = method.Name.Contains(".add_");
		bool isRemover = method.Name.Contains(".remove_");
		bool isInvoker = method.Name.Contains(".invoke_");
		bool isGetter = method.Name.Contains(".get_");
		bool isSetter = method.Name.Contains(".set_");
		if (isGetter || isSetter || isAdder || isRemover || isInvoker)
		{
			var lastDot = method.Name.LastIndexOf('.');
			var parentClass = method.Name.Substring(0, lastDot).Split("<")[0];
			var methodName = method.Name.Substring(lastDot + 1);
			var usidx = methodName.IndexOf('_');
			if (string.IsNullOrEmpty(parentClass) || string.IsNullOrEmpty(methodName) || usidx < 0)
			{
				return false;
			}
			var memberName = methodName.Substring(usidx + 1);
			var baseTypes = method.DeclaringType.GetAllBaseTypes();
			var baseType = baseTypes.FirstOrDefault(t => t.FullName == parentClass);
			if (baseType == null)
			{
				return false;
			}
			IMember member = baseType.GetMembers().FirstOrDefault(m => m.Name == memberName);

			if ((isGetter || isSetter) && member is IProperty prop)
			{
				var memberAccessorName = isGetter ? prop.Getter?.Name : prop.Setter?.Name;

				if (memberAccessorName == methodName)
				{
					return true;
				}
			} else if (member is IEvent ev)
			{
				var memberAccessorName = isInvoker ? ev.InvokeAccessor?.Name :
					isAdder ? ev.AddAccessor?.Name : isRemover ? ev.RemoveAccessor?.Name : null;
				if (memberAccessorName == methodName)
				{
					return true;
				}
			}
		}
		return false;

	}


	public static bool IsBannedGodotTypeMember(IEntity entity)
	{
		if (entity.DeclaringTypeDefinition == null || !IsGodotPartialClass(entity.DeclaringTypeDefinition))
		{
			return false;
		}

		switch (entity)
		{
			case IField field:
				if (IsBackingSignalDelegateField(field))
				{
					return true;
				}

				break;
			case IProperty property:

				break;
			case IMethod method:
				// check if the method is a method that is generated by the Godot source generator
				if (BANNED_GODOT_METHODS.Contains(method.Name))
				{
					return true;
				}

				// if the method name is EmitSignal<SignalName> and it's a protected or private void method, then it's an auto-generated signal emitter
				if (
					method is { IsVirtual: false, Accessibility: Accessibility.Internal or Accessibility.Protected or Accessibility.ProtectedOrInternal or Accessibility.ProtectedAndInternal or Accessibility.Private } &&
					method.Name.StartsWith("EmitSignal") && method.ReturnType.FullName == "System.Void")
				{
					return true;
				}
				if (IsCompilerGeneratedAccessorMethod(method))
				{
					return true;
				}

				// auto-generated getter methods for properties of parent classes


				break;
			case IEvent @event:
				if (@event.DeclaringTypeDefinition != null && GetSignalsInClass(@event.DeclaringTypeDefinition).Contains(@event.ReturnType.GetDefinition()) &&
				    GetBackingSignalDelegateFieldNames(@event.DeclaringTypeDefinition)
					    .Contains(BACKING_FIELD_PREFIX + @event.Name))
				{
					return true;
				}

				break;
			case ITypeDefinition type:
				var bannedEmbeddedClasses = new List<string> { "MethodName", "PropertyName", "SignalName" };
				var enclosingClass = type.DeclaringTypeDefinition;
				// check if the type is a nested type
				var enclosingClassBase = enclosingClass?.DirectBaseTypes;
				// check if the type is one of the banned embedded classes, and also derives from the base class's embedded class
				if (enclosingClass != null && enclosingClassBase != null && bannedEmbeddedClasses.Contains(type.Name) &&
				    type.DirectBaseTypes.Any(t => t.FullName.Contains(enclosingClassBase.First().Name)))
				{
					return true;
				}

				break;
			default:
				break;
		}

		// I think we got all the banned methods and generated classes, so we don't need this anymore
		// return HasEditorNonBrowsableAttribute(entity)

		return false;
	}

	/// <summary>
	/// Detects whether or not the type is the auto-generated GodotPlugins.Game.Main class.
	/// </summary>
	/// <param name="module"></param>
	/// <param name="type"></param>
	/// <returns></returns>
	public static bool IsGodotGameMainClass(MetadataFile module, TypeDefinitionHandle type)
	{
		// check if the entity is a member of a type that derives from Godot.GameMain
		var fullTypeName = module.Metadata.GetTypeDefinition(type).GetFullTypeName(module.Metadata).ToString();
		return fullTypeName == "GodotPlugins.Game.Main";
	}

	public static readonly ImmutableHashSet<string> BANNED_GODOT_METHODS = [
		"GetGodotSignalList",
		"GetGodotMethodList",
		"GetGodotPropertyList",
		"GetGodotPropertyDefaultValues",
		"InvokeGodotClassStaticMethod",
		"InvokeGodotClassMethod",
		"AddEditorConstructors",
		"InternalCreateInstance",
		"HasGodotClassSignal",
		"HasGodotClassMethod",
		"GetGodotClassPropertyValue",
		"SetGodotClassPropertyValue",
		"SaveGodotObjectData",
		"RestoreGodotObjectData",
		"RaiseGodotClassSignalCallbacks"
	];

	public static MetadataFile? GetGodotSharpAssembly(MetadataFile file, IAssemblyResolver resolver)
	{
		var godotSharpReference = file.AssemblyReferences.FirstOrDefault(r => r.Name == "GodotSharp");
		var godotSharpAssembly = godotSharpReference != null
			? resolver.Resolve(godotSharpReference)
			: null;
		return godotSharpAssembly;
	}

	public static bool IsGodotVariantType(string type)
	{
		type = type.Trim().TrimEnd(']').TrimEnd('[');
		switch (type)
		{
			case "Variant":
			case "void":
			case "bool":
			case "int":
			case "float":
			case "String":
			case "Vector2":
			case "Vector2i":
			case "Rect2":
			case "Rect2i":
			case "Vector3":
			case "Vector3i":
			case "Transform2D":
			case "Vector4":
			case "Vector4i":
			case "Plane":
			case "Quaternion":
			case "AABB":
			case "Basis":
			case "Transform3D":
			case "Projection":
			case "Color":
			case "StringName":
			case "NodePath":
			case "RID":
			case "Object":
			case "Callable":
			case "Signal":
			case "Dictionary":
			case "Array":
			case "PackedByteArray":
			case "PackedInt32Array":
			case "PackedInt64Array":
			case "PackedFloat32Array":
			case "PackedFloat64Array":
			case "PackedStringArray":
			case "PackedVector2Array":
			case "PackedVector3Array":
			case "PackedColorArray":
			case "PackedVector4Array":
			// 3.x types
			case "Quat":
			case "Transform":
				return true;
		}
		if (type.StartsWith("Array<"))
		{
			return true;
		}
		if (type.StartsWith("Dictionary<"))
		{
			return true;
		}
		return false;
	}



	public static string CSharpTypeToGodotType(string _type)
	{
		var csharpType = Common.TrimPrefix(_type.Trim().Replace("&", "").Trim(), "System.");
		var subCSharpType = csharpType;
		var newType = csharpType;
		var subType = csharpType;
		bool isArray = false;
		bool isDictionary = false;
		// Godot 3.x PackedArray types
		if (csharpType.StartsWith("Pool") && csharpType.EndsWith("Array"))
		{
			if (csharpType.StartsWith("PoolInt"))
			{
				return "PackedInt32Array";
			}
			if (csharpType.StartsWith("PoolReal"))
			{
				return "PackedFloat32Array";
			}
			return csharpType.Replace("Pool", "Packed");
		}

		if (csharpType.EndsWith("[]"))
		{
			isArray = true;
			subCSharpType = csharpType.Substring(0, csharpType.Length - 2);
			subType = subCSharpType;
		}
		else if (csharpType.Contains("<"))
		{
			if (csharpType.StartsWith("Array"))
			{
				isArray = true;
				subCSharpType = csharpType.Split('<')[1].TrimEnd('>');
				subType = subCSharpType;
			} else if (csharpType.StartsWith("Dictionary"))
			{
				// TODO: subtypes
				return "Dictionary";
				// isDictionary = true;
				// var parts = csharpType.Split('<', 1)[1].TrimEnd('>').Split(',');
			}
			else
			{
				// unknown, return "Variant"
				return "Variant";
			}
		}

		switch (subType)
		{
			case "Void":
				subType = "void";
				break;
			case "Boolean":
				subType = "bool";
				break;
			case "UInt32":
			case "UInt64":
			case "Int32":
			case "Int64":
				subType = "int";
				break;
			case "Single":
			case "Double":
				subType = "float";
				break;
			case "string":
				subType = "String";
				break;
			case "godot_string_name":
				subType = "StringName";
				break;
			// 3.x types
			case "Quat":
				subType = "Quaternion";
				break;
			case "Transform":
				subType = "Transform3D";
				break;
			default:
				break;
		}
		if (isArray)
		{
			switch (subCSharpType)
			{
				case "uint8_t":
				case "byte":
				case "Byte":
					newType = "PackedByteArray";
					break;
				case "Boolean":
					newType = "PackedBoolArray";
					break;
				case "UInt32":
				case "Int32":
					newType = "PackedInt32Array";
					break;
				case "UInt64":
				case "Int64":
					newType = "PackedInt64Array";
					break;
				case "Single":
					newType = "PackedFloat32Array";
					break;
				case "Color":
					newType = "PackedColorArray";
					break;
				case "Vector2":
					newType = "PackedVector2Array";
					break;
				case "Vector3":
					newType = "PackedVector3Array";
					break;
				case "string":
				case "String":
					newType = "PackedStringArray";
					break;

				default:
					newType = "Array[" + subType + "]";
					break;

			}
		}
		else
		{
			newType = subType;
		}
		return newType;
	}

	public static string? ReplaceMemberReference(MemberReferenceExpression memberReferenceExpression)
	{
		string? text = null;
		if (memberReferenceExpression.GetSymbol() is IMember ne)
		{
			if (ne.DeclaringType.FullName == "Godot.Colors")
			{
				text = "Color(\"" + Common.CamelCaseToSnakeCase(ne.Name).ToUpper() + "\")";

			}
			else if (ne.FullName.Contains(".Math"))
			{
				text = Common.CamelCaseToSnakeCase(ne.Name).ToUpper();
			}
			else if (ne.DeclaringType.FullName.StartsWith("Godot."))
			{
				// remove the Godot. prefix
				string dtname = ne.DeclaringType.Name;
				if (dtname.EndsWith("s"))
				{
					// remove the trailing 's' for plural types
					dtname = dtname.Substring(0, dtname.Length - 1);
				}
				// text = dtname + "(\"" + Common.CamelCaseToSnakeCase(ne.Name).ToUpper() + "\")";
				text = dtname + "." + Common.CamelCaseToSnakeCase(ne.Name).ToUpper();

			}
			else if (ne is IVariable iv && iv.IsConst)
			{
				text = GodotExpressionTokenWriter.PrintPrimitiveValue(iv.GetConstantValue());
			}
			else if (ne.FullName.EndsWith("tring.Empty"))
			{
				text = "\"\"";
			}
		}

		return text;
	}

}
