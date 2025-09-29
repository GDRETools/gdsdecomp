// See https://aka.ms/new-console-template for more information

using System.Runtime.InteropServices;
using ICSharpCode.Decompiler;
using ICSharpCode.Decompiler.CSharp.ProjectDecompiler;
using ICSharpCode.Decompiler.Metadata;

namespace GodotMonoDecomp
{
    public static class Lib
    {
        public static int DecompileProject(string assemblyPath, string outputCSProjectPath, string projectPath, string[]? ReferencePaths = null, GodotMonoDecompSettings settings = default)
        {
            try
            {
                var files = Common.ListCSharpFiles(projectPath, false);
				GodotModuleDecompiler decompiler = new GodotModuleDecompiler(assemblyPath, [.. files], ReferencePaths, settings);
                return decompiler.DecompileModule(outputCSProjectPath);
            }
            catch (Exception e)
            {
                Console.WriteLine("Decompilation failed: " + e.Message);
                return -1;
            }
        }
    }

}
