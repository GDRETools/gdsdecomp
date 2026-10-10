using System.Runtime.InteropServices;
using System.Text;
using ICSharpCode.Decompiler;
using ICSharpCode.Decompiler.CSharp;

namespace GodotMonoDecomp.NativeLibrary;

static public class Lib
{
	static IntPtr StringToHGlobalUtf8(string value)
	{
		byte[] bytes = Encoding.UTF8.GetBytes(value);
		IntPtr ptr = Marshal.AllocHGlobal(bytes.Length + 1);
		Marshal.Copy(bytes, 0, ptr, bytes.Length);
		Marshal.WriteByte(ptr, bytes.Length, 0);
		return ptr;
	}

	static string[]? GetStringArray(IntPtr ptr, int count)
	{
		if (count == 0 || ptr == IntPtr.Zero) return null;
		string[] strs = new string[count];
		for (int i = 0; i < count; i++)
		{
			IntPtr pathPtr = Marshal.ReadIntPtr(ptr, i * IntPtr.Size);
			strs[i] = Marshal.PtrToStringUTF8(pathPtr) ?? string.Empty;
		}
		return strs;
	}

    // nativeAOT function; oneshot, intended to be used by a CLI

    [UnmanagedCallersOnly(EntryPoint = "GodotMonoDecomp_DecompileProject")]
    public static int AOTDecompileProject(
        IntPtr assemblyPath,
        IntPtr outputCSProjectPath,
        IntPtr projectPath,
        IntPtr AssemblyReferenceDirs,
        int referencePathsCount
    )
    {
        string assemblyFileNameStr = Marshal.PtrToStringUTF8(assemblyPath) ?? string.Empty;
        string outputPathStr = Marshal.PtrToStringUTF8(outputCSProjectPath) ?? string.Empty;
        string  projectFileNameStr = Marshal.PtrToStringUTF8(projectPath) ?? string.Empty;
        string[]? referencePathsStrs = GetStringArray(AssemblyReferenceDirs, referencePathsCount);
        return GodotMonoDecomp.Lib.DecompileProject(assemblyFileNameStr, outputPathStr,projectFileNameStr, referencePathsStrs);
    }


	[UnmanagedCallersOnly(EntryPoint = "GodotMonoDecomp_FreeObjectHandle")]
	public static void FreeObjectHandle(IntPtr v)
	{
		GCHandle h = GCHandle.FromIntPtr(v);
		h.Free();
	}

    // wrapper methods for GodotModuleDecompiler; constructor, destructor, all the public methods
	[UnmanagedCallersOnly(EntryPoint = "GodotMonoDecomp_CreateGodotModuleDecompiler")]
	public static IntPtr AOTCreateGodotModuleDecompiler(
		IntPtr assemblyPath,
		IntPtr originalProjectFiles,
		int originalProjectFilesCount,
		IntPtr referencePaths,
		int referencePathsCount,
		IntPtr GodotVersionOverride,
		bool writeNuGetPackageReferences,
		bool verifyNuGetPackageIsFromNugetOrg,
		bool copyOutOfTreeReferences,
		bool createAdditionalProjectsForProjectReferences,
		bool removeGeneratedJsonContextBody,
		bool enableCollectionInitializerLifting,
		bool emitILAnnotationComments,
		bool automaticallySearchWorkshopDependencies,
		int OverrideLanguageVersion
	)
	{
		string assemblyFileNameStr = Marshal.PtrToStringUTF8(assemblyPath) ?? string.Empty;
		var originalProjectFilesStrs = GetStringArray(originalProjectFiles, originalProjectFilesCount);
		var referencePathsStrs = GetStringArray(referencePaths, referencePathsCount);
		var godotVersionOverrideStr = GodotVersionOverride == IntPtr.Zero ? null : Marshal.PtrToStringUTF8(GodotVersionOverride) ?? null;
		var settings = new GodotMonoDecompSettings
		{
			WriteNuGetPackageReferences = writeNuGetPackageReferences,
			VerifyNuGetPackageIsFromNugetOrg = verifyNuGetPackageIsFromNugetOrg,
			CopyOutOfTreeReferences = copyOutOfTreeReferences,
			CreateAdditionalProjectsForProjectReferences = createAdditionalProjectsForProjectReferences,
			RemoveGeneratedJsonContextBody = removeGeneratedJsonContextBody,
			EnableCollectionInitializerLifting = enableCollectionInitializerLifting,
			EmitILAnnotationComments = emitILAnnotationComments,
			AutomaticallySearchWorkshopDependencies = automaticallySearchWorkshopDependencies,
			OverrideLanguageVersion = OverrideLanguageVersion == 0 ? null : (LanguageVersion)OverrideLanguageVersion,
			GodotVersionOverride = godotVersionOverrideStr == null ? null : GodotStuff.ParseGodotVersionFromString(godotVersionOverrideStr)
		};
		try {
			var decompiler = new GodotModuleDecompiler(assemblyFileNameStr, originalProjectFilesStrs, referencePathsStrs, settings);
			var handle = GCHandle.Alloc(decompiler);
			return GCHandle.ToIntPtr(handle);
		}

		catch (Exception e)
		{
			Console.Error.WriteLine("Failed to create GodotModuleDecompiler: " + e.Message);
			return IntPtr.Zero;
		}
	}

	[UnmanagedCallersOnly(EntryPoint = "GodotMonoDecomp_DecompileModule")]
	public static int AOTDecompileModule(
		IntPtr decompilerHandle,
		IntPtr outputCSProjectPath,
		IntPtr excludeFiles,
		int excludeFilesCount
	)
	{
		var decompiler = GCHandle.FromIntPtr(decompilerHandle).Target as GodotModuleDecompiler;
		if (decompiler == null)
		{
			return -1;
		}
		var outputCSProjectPathStr = Marshal.PtrToStringUTF8(outputCSProjectPath) ?? string.Empty;
		var excludeFilesStrs = GetStringArray(excludeFiles, excludeFilesCount);
		return decompiler.DecompileModule(outputCSProjectPathStr, excludeFilesStrs);
	}

	struct AOTGodotModuleDecompilerProgress : IProgress<DecompilationProgress>
	{
		private delegate int ProgressFunction(IntPtr userData, int current, int total, IntPtr status);

		private event Action<DecompilationProgress>? OnProgress = null;

		private readonly ProgressFunction? progressFunction;
		private readonly IntPtr userData;

		private readonly CancellationTokenSource cancelSource;

		public CancellationToken CancellationToken => cancelSource.Token;


		public AOTGodotModuleDecompilerProgress(IntPtr reportFunc, IntPtr userData)
		{
			this.userData = userData;
			this.cancelSource = new CancellationTokenSource();
			if (reportFunc == IntPtr.Zero) return;
			progressFunction = Marshal.GetDelegateForFunctionPointer<ProgressFunction>(reportFunc);
		}

		public void Report(DecompilationProgress value)
		{
			if (progressFunction == null) return;
			var statusCStr = StringToHGlobalUtf8(value.Status ?? string.Empty);
			var ret = progressFunction(userData, value.UnitsCompleted, value.TotalUnits, statusCStr);
			if (ret != 0) {
				try {
					cancelSource.Cancel();
				}
				catch (Exception e)
				{
					Console.Error.WriteLine("Failed to cancel decompilation: " + e.Message);
				}
			}
			// free the string after calling the function
			Marshal.FreeHGlobal(statusCStr);
		}

	}

	[UnmanagedCallersOnly(EntryPoint = "GodotMonoDecomp_DecompileModuleWithProgress")]
	public static int AOTDecompileModuleWithProgress(
		IntPtr decompilerHandle,
		IntPtr outputCSProjectPath,
		IntPtr excludeFiles,
		int excludeFilesCount,
		IntPtr reportFunc, // function pointer for progress reporting that takes in a void* (userdata), integer (current step), an integer (total steps), a char* (status string), and a void* (cancellation token source) and returns void
		IntPtr userData
	)
	{
		var decompiler = GCHandle.FromIntPtr(decompilerHandle).Target as GodotModuleDecompiler;
		if (decompiler == null)
		{
			return -1;
		}
		var outputCSProjectPathStr = Marshal.PtrToStringUTF8(outputCSProjectPath) ?? string.Empty;
		var excludeFilesStrs = GetStringArray(excludeFiles, excludeFilesCount);
		var progress = new AOTGodotModuleDecompilerProgress(reportFunc, userData);
		return decompiler.DecompileModule(outputCSProjectPathStr, excludeFilesStrs, progress, progress.CancellationToken);
	}

	[UnmanagedCallersOnly(EntryPoint = "GodotMonoDecomp_CancelDecompilation")]
	public static int AOTCancelDecompilation(
		IntPtr cancellationSourcePtr
	)
	{
		var cancellationToken = GCHandle.FromIntPtr(cancellationSourcePtr).Target as CancellationTokenSource;
		cancellationToken?.Cancel();
		return 0;
	}


	[UnmanagedCallersOnly(EntryPoint = "GodotMonoDecomp_DecompileIndividualFile")]
	public static IntPtr AOTDecompileIndividualFile(
		IntPtr decompilerHandle,
		IntPtr file
	)
	{
		var decompiler = GCHandle.FromIntPtr(decompilerHandle).Target as GodotModuleDecompiler;
		if (decompiler == null)
		{
			return IntPtr.Zero;
		}
		var fileStr = Marshal.PtrToStringUTF8(file) ?? string.Empty;
		var code = decompiler.DecompileIndividualFile(fileStr);
		return StringToHGlobalUtf8(code);
	}

	[UnmanagedCallersOnly(EntryPoint = "GodotMonoDecomp_GetNumberOfFilesNotPresentInFileMap")]
	public static int AOTGetNumberOfFilesNotPresentInFileMap(
		IntPtr decompilerHandle
	)
	{
		var decompiler = GCHandle.FromIntPtr(decompilerHandle).Target as GodotModuleDecompiler;
		if (decompiler == null)
		{
			return -1;
		}
		return decompiler.GetNumberOfFilesNotPresentInFileMap();
	}

	[UnmanagedCallersOnly(EntryPoint = "GodotMonoDecomp_GetFilesNotPresentInFileMap")]
	public static IntPtr AOTGetFilesNotPresentInFileMap(
		IntPtr decompilerHandle
	)
	{
		var decompiler = GCHandle.FromIntPtr(decompilerHandle).Target as GodotModuleDecompiler;
		if (decompiler == null)
		{
			return IntPtr.Zero;
		}
		var files = decompiler.GetFilesNotPresentInFileMap();
		var arrayPtr = Marshal.AllocHGlobal(files.Length * IntPtr.Size);
		for (int i = 0; i < files.Length; i++)
		{
			Marshal.WriteIntPtr(arrayPtr + i * IntPtr.Size, StringToHGlobalUtf8(files[i]));
		}
		return arrayPtr;
	}

	[UnmanagedCallersOnly(EntryPoint = "GodotMonoDecomp_GetNumberOfFilesInFileMap")]
	public static int AOTGetNumberOfFilesInFileMap(
		IntPtr decompilerHandle
	)
	{
		var decompiler = GCHandle.FromIntPtr(decompilerHandle).Target as GodotModuleDecompiler;
		if (decompiler == null)
		{
			return -1;
		}
		return decompiler.GetNumberOfFilesInFileMap();
	}

	[UnmanagedCallersOnly(EntryPoint = "GodotMonoDecomp_GetFilesInFileMap")]
	public static IntPtr AOTGetFilesInFileMap(
		IntPtr decompilerHandle
	)
	{
		var decompiler = GCHandle.FromIntPtr(decompilerHandle).Target as GodotModuleDecompiler;
		if (decompiler == null)
		{
			return IntPtr.Zero;
		}
		var files = decompiler.GetFilesInFileMap();
		var arrayPtr = Marshal.AllocHGlobal(files.Length * IntPtr.Size);
		for (int i = 0; i < files.Length; i++)
		{
			Marshal.WriteIntPtr(arrayPtr + i * IntPtr.Size, StringToHGlobalUtf8(files[i]));
		}
		return arrayPtr;
	}

	[UnmanagedCallersOnly(EntryPoint = "GodotMonoDecomp_IsCustomVersionDetected")]
	public static int AOTIsCustomVersionDetected(
		IntPtr decompilerHandle
	)
	{
		var decompiler = GCHandle.FromIntPtr(decompilerHandle).Target as GodotModuleDecompiler;
		if (decompiler == null)
		{
			return 0;
		}
		return decompiler.IsCustomVersionDetected() ? 1 : 0;
	}

	[UnmanagedCallersOnly(EntryPoint = "GodotMonoDecomp_GetAllUtf32StringsInModule")]
	public static IntPtr AOTGetAllUtf32StringsInModule(
		IntPtr decompilerHandle,
		IntPtr r_num_strings // pointer to an integer
	)
	{
		var decompiler = GCHandle.FromIntPtr(decompilerHandle).Target as GodotModuleDecompiler;
		if (decompiler == null)
		{
			return IntPtr.Zero;
		}
		IEnumerable<byte[]> strings = decompiler.GetAllUtf32StringsInModule();
		Marshal.WriteInt32(r_num_strings, strings.Count());
		var arrayPtr = Marshal.AllocHGlobal(strings.Count() * IntPtr.Size);
		int i = 0;
		foreach (var u32strAsByteArray in strings)
		{
			Marshal.WriteIntPtr(arrayPtr + i * IntPtr.Size, Marshal.AllocHGlobal(u32strAsByteArray.Length + 4)); // +4 for the null terminator
			Marshal.Copy(u32strAsByteArray, 0, Marshal.ReadIntPtr(arrayPtr + i * IntPtr.Size), u32strAsByteArray.Length);
			Marshal.WriteInt32(Marshal.ReadIntPtr(arrayPtr + i * IntPtr.Size) + u32strAsByteArray.Length, 0);
			i++;
		}
		return arrayPtr;
	}

	[UnmanagedCallersOnly(EntryPoint = "GodotMonoDecomp_GetScriptInfo")]
	public static IntPtr AOTGetScriptInfo(
		IntPtr decompilerHandle,
		IntPtr file
	)
	{
		var decompiler = GCHandle.FromIntPtr(decompilerHandle).Target as GodotModuleDecompiler;
		if (decompiler == null)
		{
			return IntPtr.Zero;
		}
		var fileStr = Marshal.PtrToStringUTF8(file) ?? string.Empty;
		var scriptInfo = decompiler.GetScriptInfo(fileStr);
		if (scriptInfo == null)
		{
			return IntPtr.Zero;
		}
		var jsons = scriptInfo.ToJson(false);
		return StringToHGlobalUtf8(jsons);
	}


	[UnmanagedCallersOnly(EntryPoint = "GodotMonoDecomp_FreeArray")]
	public static void FreeArray(IntPtr v, int length)
	{
		for (int i = 0; i < length; i++)
		{
			Marshal.FreeHGlobal(Marshal.ReadIntPtr(v, i * IntPtr.Size));
		}
		Marshal.FreeHGlobal(v);
	}

	[UnmanagedCallersOnly(EntryPoint = "GodotMonoDecomp_FreeString")]
	public static void FreeString(IntPtr v)
	{
		Marshal.FreeHGlobal(v);
	}

	[UnmanagedCallersOnly(EntryPoint = "GodotMonoDecomp_GetLanguageVersions")]
	public static IntPtr AOTGetLanguageVersions(IntPtr r_num_versions)
	{
		var values = Enum.GetValues<LanguageVersion>();
		Marshal.WriteInt32(r_num_versions, values.Length);
		var arrayPtr = Marshal.AllocHGlobal(values.Length * IntPtr.Size);
		for (int i = 0; i < values.Length; i++)
		{
			Marshal.WriteInt32(arrayPtr + i * sizeof(int), (int)values[i]);
		}
		return arrayPtr;
	}

	[UnmanagedCallersOnly(EntryPoint = "GodotMonoDecomp_CheckFileIsAssembly")]
	public static bool AOTCheckFileIsAssembly(
		IntPtr file
	)
	{
		var fileStr = Marshal.PtrToStringUTF8(file) ?? string.Empty;
		try
		{
			var assemblyName = System.Reflection.AssemblyName.GetAssemblyName(fileStr);
			return !string.IsNullOrEmpty(assemblyName.Name);
		}
		catch
		{
			return false;
		}
	}

	[UnmanagedCallersOnly(EntryPoint = "GodotMonoDecomp_GetGlobalClassFiles")]
	public static IntPtr AOTGetGlobalClassFiles(
		IntPtr decompilerHandle,
		IntPtr r_num_files
	)
	{
		var decompiler = GCHandle.FromIntPtr(decompilerHandle).Target as GodotModuleDecompiler;
		if (decompiler == null)
		{
			return IntPtr.Zero;
		}
		var files = decompiler.GetGlobalClassFiles();
		Marshal.WriteInt32(r_num_files, files.Length);
		var arrayPtr = Marshal.AllocHGlobal(files.Length * IntPtr.Size);
		for (int i = 0; i < files.Length; i++)
		{
			Marshal.WriteIntPtr(arrayPtr + i * IntPtr.Size, StringToHGlobalUtf8(files[i]));
		}
		return arrayPtr;
	}
}
