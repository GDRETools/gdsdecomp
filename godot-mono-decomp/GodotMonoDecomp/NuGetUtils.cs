using System.Net;
using System.Xml.Linq;
using NuGet.Packaging;
using NuGet.Common;
using Newtonsoft.Json;

namespace GodotMonoDecomp;

public static class HttpClientExtensions
{
	public static async Task DownloadFileTaskAsync(this HttpClient client, Uri uri, string FileName, CancellationToken cancellationToken)
	{
		using (var s = await client.GetStreamAsync(uri, cancellationToken))
		{
			using (var fs = new FileStream(FileName, FileMode.CreateNew))
			{
				await s.CopyToAsync(fs, cancellationToken);
			}
		}
		if (cancellationToken.IsCancellationRequested)
		{
			throw new OperationCanceledException(cancellationToken);
		}
	}
}

public static class NugetDetails
{
    public static async Task<string?> ResolvePackageAndGetContentHash(string name, string version, CancellationToken cancellationToken)
    {
        // download the package to the local cache
        string? p = null;
        try
        {
			if (IsPackageInLocalCache(name, version))
			{
				var dir = Path.Combine(UserPackagesPath, name.ToLower(), version.ToLower());
				// check the metadata file
				var metadataPath = Path.Combine(dir, $".nupkg.metadata");
				// {
				// "version": 2,
				// "contentHash": "FRQlhMAcHf0GjAXIfhN6RydfZncLLXNNTOtpLL1bt57kp59vu40faW+dr6Vwl7ef/IUFfF38aiB5jvhAA/9Aow==",
				// "source": "https://api.nuget.org/v3/index.json"
				// }
				// we can't use the nupkgemetadatafileformat reader because it doesn't work on NativeAOT, so just read it as JSON and get the fields manually
				if (File.Exists(metadataPath))
				{
					var metadataJson = File.ReadAllText(metadataPath);
					if (!string.IsNullOrEmpty(metadataJson))
					{
						var metadata = JsonConvert.DeserializeObject<Dictionary<string, object>>(metadataJson);
						if (metadata != null && metadata.TryGetValue("contentHash", out var contentHash) && metadata.TryGetValue("source", out var source) && source != null && source.ToString().StartsWith("https://api.nuget.org"))
						{
							return $"sha512-{contentHash}";
						}
					}
				}

				// either we didn't have a metadata file or it wasn't from nuget.org, so we need to download it again
				// save it to a temporary directory so as not to clobber the local cache
				var tempDir = Path.Combine(NuGetEnvironment.GetFolderPath(NuGetFolderPath.Temp), name.ToLower(), version.ToLower());
				p = await DownloadPackageFromNugetAsync(name, version, tempDir, cancellationToken);

			}
			else
			{
				p = await DownloadPackageToLocalCache(name, version, cancellationToken);
			}
        }
        catch (Exception e)
        {
            Console.WriteLine($"Error downloading package {name}.{version}: {e.Message}");
        }

        if (p == null)
        {
            return null;
        }
        return GetPackageContentHash(p);
    }

    public static string? GetPackageContentHash(string path)
    {
        if (string.IsNullOrEmpty(path))
        {
            Console.WriteLine("Path is null or empty.");
            return null;
        }
        var file = Path.GetFileName(path);
        var package = new PackageArchiveReader(path);
        if (package == null)
        {
            Console.WriteLine($"Failed to read package {file}");
            return null;
        }
        var hash = package.GetContentHash(default);
        if (string.IsNullOrEmpty(hash))
        {
            Console.WriteLine($"Failed to read content hash for package {file}");
            return null;
        }
        return $"sha512-{hash}";
    }
    // Since most packages are signed, this will almost never match the hash from the dependency context.
    private static async Task<(string PackageHash, string PackageHashAlgorithm)> GetHashFromNugetAsync(string name, string version)
    {
        using var client = new HttpClient(new HttpClientHandler
        {
            AutomaticDecompression = DecompressionMethods.GZip
        });
        var url = $"https://www.nuget.org/api/v2/Packages(Id='{name}',Version='{version}')";
        var data = "";
        var AcceptGzipEncoding = true;
        try
        {
            data = await client.GetStringAsync(url);
        }
        catch (HttpRequestException e)
        {
            Console.WriteLine($"Error fetching data from NuGet: {e.Message}");
            return ("", "");
        }

        var xDocument = XDocument.Parse(data);

        string GetAttribute(string attribute)
        {
            var rootNs = XName.Get("entry", "http://www.w3.org/2005/Atom");
            var propertiesNs = XName.Get("properties", "http://schemas.microsoft.com/ado/2007/08/dataservices/metadata");
            XName AttributesNs(string attr) => XName.Get(attr, "http://schemas.microsoft.com/ado/2007/08/dataservices");

            var entry = xDocument.Element(rootNs);
            var properties = entry?.Element(propertiesNs);
            return properties?.Element(AttributesNs(attribute))?.Value;
        }

        var packageHash = GetAttribute("PackageHash");
        var packageHashAlgorithm = GetAttribute("PackageHashAlgorithm");

        return (packageHash, packageHashAlgorithm);
    }
    // Download the package from NuGet


    public static async Task<string?> DownloadPackageFromNugetAsync(string name, string version, string outputPath, CancellationToken cancellationToken)
    {
        var p = Path.Combine(outputPath, $"{name}.{version}.nupkg".ToLower());
        if (File.Exists(p))
        {
	        return p;
        }
        // ensure the output directory exists
        if (!Directory.Exists(outputPath))
        {
            Directory.CreateDirectory(outputPath);
        }

        // Console.WriteLine($"Downloading package {name}.{version} to {p}");

        using var client = new HttpClient(new HttpClientHandler
        {
            AutomaticDecompression = DecompressionMethods.GZip
        });
        // use v3 api
        var url = $"https://api.nuget.org/v3-flatcontainer/{name}/{version}/{name}.{version}.nupkg".ToLower();
        Uri uri =  new Uri(url);
        try {
            await client.DownloadFileTaskAsync(uri, p, cancellationToken);
        }
		catch (OperationCanceledException)
		{
			Console.WriteLine($"Download of package {name}.{version} was cancelled.");
			return null;
		}
        catch (HttpRequestException e)
		{
			Console.WriteLine($"Error downloading package: {e.Message}");
			// check the inner exception for more details
			if (e.InnerException != null)
			{
				Console.WriteLine($"Inner exception: {e.InnerException.Message}");
			}
			return null;
		}

        return p;
    }

    public static readonly string UserPackagesPath = Path.Join(NuGetEnvironment.GetFolderPath(NuGetFolderPath.NuGetHome), "packages");

    public static bool IsPackageInLocalCache(string name, string version)
	{
		string p = Path.Combine(UserPackagesPath, name, version);
		if (!Directory.Exists(p))
		{
			return false;
		}
		var existing = Directory.GetFiles(p, $"{name}.{version}.nupkg");
		return existing.Length > 0;
	}

	public static async Task<string?> DownloadPackageToLocalCache(string name, string version,
		CancellationToken cancellationToken)
	{
		string p = Path.Combine(UserPackagesPath, name, version);
		if (File.Exists(p))
		{
			return p;
		}
		if (!Directory.Exists(p))
        {
            Directory.CreateDirectory(p);
        }
        return await DownloadPackageFromNugetAsync(name, version, p, cancellationToken);
    }
}
