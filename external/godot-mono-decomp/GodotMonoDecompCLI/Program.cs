// See https://aka.ms/new-console-template for more information

using System.Text;
using GodotMonoDecomp;

// import a CLI parser
using CommandLine;

// specify this is the main entry point
int Main(string[] args)
{
	var result = CommandLine.Parser.Default.ParseArguments<Options>(args);
	if (result.Errors.Any())
	{
		Console.WriteLine("Error: " + result.Errors.First().ToString());

		return 1;
	}
	if (string.IsNullOrWhiteSpace(result.Value.AssemblyPath))
	{
		Console.WriteLine("Error: Assembly path is required");
		return 1;
	}


    // get the assembly path from the command line
    string assemblyPath = Path.GetFullPath(result.Value.AssemblyPath!);

    // get the output path from the command line
    string outputDir = result.Value.OutputProject ?? Path.GetDirectoryName(assemblyPath)!;

	string assemblyName = result.Value.ProjectName ?? Path.GetFileNameWithoutExtension(assemblyPath);

	string outputCSProj = Path.Combine(outputDir, assemblyName + ".csproj");

    string projectPath = result.Value.ExtractedProject ?? outputDir;
    // get the reference paths from the command line
    string[] referencePaths = result.Value.ReferencePath != null ? [Path.GetFullPath(result.Value.ReferencePath)] : [];

	GodotMonoDecompSettings settings = new GodotMonoDecompSettings();
	settings.WriteNuGetPackageReferences = !result.Value.NoWriteNuGetPackageReferences;
	settings.CopyOutOfTreeReferences = !result.Value.NoCopyOutOfTreeReferences;
	settings.CreateAdditionalProjectsForProjectReferences = !result.Value.NoCreateAdditionalProjectsForProjectReferences;
	settings.GodotVersionOverride = result.Value.GodotVersion == null ? null : GodotStuff.ParseGodotVersionFromString(result.Value.GodotVersion);
	// get the current time
	var startTime = DateTime.Now;
	// call the DecompileProject function
	if (result.Value.DumpStrings || result.Value.WriteScriptInfo)
	{
		var files = Common.ListCSharpFiles(projectPath, false).ToList();
		GodotModuleDecompiler decompiler = new GodotModuleDecompiler(assemblyPath, [.. files], referencePaths, settings);
		Common.EnsureDir(outputDir);

		if (result.Value.DumpStrings)
		{
			var outputStrings = Path.Join(outputDir, "godot_strings.txt");
			Console.WriteLine($"Dumping strings to {outputStrings}");
			decompiler.DumpStrings(outputStrings);
		}
		else
		{
			var outputJson = Path.Join(outputDir, "godot_script_info.json");
			decompiler.WriteWholeScriptInfo(outputJson);
		}
	}
    int resultCode = Lib.DecompileProject(assemblyPath, outputCSProj, projectPath, referencePaths, settings);
    var timeTaken = DateTime.Now - startTime;
    Console.WriteLine($"Decompilation completed in {timeTaken.TotalSeconds} seconds.");
    return resultCode;
}

return Main(args);

public class Options
{
	[Value(0,MetaName = "AssemblyPath",HelpText = "path to the assembly to decompile")]
	public string? AssemblyPath { get; set; }

	[Option("force-version", Required = false, HelpText = "Set the Godot version to use for the decompilation.")]
	public string? GodotVersion { get; set; }

	[Option("output-dir", Required = false, HelpText = "Directory to output the project files to. If not specified, the project file will be output to the same directory as the assembly.")]
	public string? OutputProject { get; set; }

	[Option("project-name", Required = false, HelpText = "Name of the csproj file to output. If not specified, the name of the assembly will be used.")]
	public string? ProjectName { get; set; }

	[Option("reference-path", Required = false, HelpText = "Path to search for references. If not specified, the references will be searched for in the same directory as the assembly.")]
	public string ReferencePath { get; set; }

	[Option("extracted-project", Required = false, HelpText = "Path to the extracted Godot project directory, used for determining original file locations. If not provided, will be assumed to be the same directory as output-project.")]
	public string? ExtractedProject { get; set; }

	// the rest of the options in the GodotMonoDecompSettings class

	[Option("no-write-package-references", Required = false, HelpText = "Whether to write NuGet package references to the project file if dependency information is available.")]
	public bool NoWriteNuGetPackageReferences { get; set; }

	[Option("no-copy-out-of-tree-references", Required = false, HelpText = "Whether to copy out-of-tree references (i.e. references that are not within the same directory structure as the project file) to the project file.")]
	public bool NoCopyOutOfTreeReferences { get; set; }

	[Option("no-multi-project", Required = false, HelpText = "Whether to create additional projects for project references in main module.")]
	public bool NoCreateAdditionalProjectsForProjectReferences { get; set; }

	[Option("write-script-info", Required = false, HelpText = "Write script info to a JSON file in the output directory.")]
	public bool WriteScriptInfo { get; set; }
	// dump strings option
	[Option("dump-strings", Required = false, HelpText = "Dump all strings in the assembly to a text file in the output directory.")]
	public bool DumpStrings { get; set; }

	[Option("verbose", Required = false, HelpText = "Set output to verbose messages.")]
	public bool Verbose { get; set; }

	[Option("help", Required = false, HelpText = "Show this help message and exit.")]
	public bool Help { get; set; }


}
