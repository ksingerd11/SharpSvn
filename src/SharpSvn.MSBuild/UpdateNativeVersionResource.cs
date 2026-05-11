using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Text;
using Microsoft.Build.Framework;

namespace SharpSvn.MSBuild;

public class UpdateNativeVersionResource : ITask
{
    const int RtVersion = 16;
    const int VsFfiSignature = unchecked((int)0xFEEF04BD);
    const int VsFfiStrucVersion = 0x00010000;
    const int VsFfiFileFlagsMask = 0x0000003F;
    const int VosNtWindows32 = 0x00040004;
    const int VftDll = 0x00000002;

    public IBuildEngine BuildEngine { get; set; }

    [Required]
    public ITaskItem Source { get; set; }

    [Required]
    public string TempDir { get; set; }

    public string KeyContainer { get; set; }

    public ITaskItem KeyFile { get; set; }

    public string StrongNameToolPath { get; set; }

    public string CompanyName { get; set; }

    public string FileDescription { get; set; }

    public string ProductName { get; set; }

    public string LegalCopyright { get; set; }

    public string FileVersion { get; set; }

    public string ProductVersion { get; set; }

    [Output]
    public bool SourceUpdated { get; set; }

    public ITaskHost HostObject { get; set; }

    public bool Execute()
    {
        if (NativeMethods.GetFileVersionInfoSize(Source.ItemSpec) > 0)
        {
            return true;
        }

        try
        {
            string sourcePath = Source.ItemSpec;
            if (string.IsNullOrWhiteSpace(sourcePath) || !File.Exists(sourcePath))
            {
                Log("Assembly not found: " + sourcePath, MessageImportance.High);
                return false;
            }

            VersionInfo versionInfo = VersionInfo.FromAssembly(
                sourcePath,
                CompanyName,
                FileDescription,
                ProductName,
                LegalCopyright,
                FileVersion,
                ProductVersion);

            byte[] versionResource = VersionResourceBuilder.Build(versionInfo);

            Log("Updating version resources", MessageImportance.Normal);
            using (ResourceUpdateHandle resHandle = NativeMethods.BeginUpdateResource(sourcePath, false))
            {
                if (resHandle == null || resHandle.IsInvalid)
                {
                    Log("Unable to open assembly resources for update", MessageImportance.High);
                    return false;
                }

                bool ok = NativeMethods.UpdateResource(
                    resHandle,
                    (IntPtr)RtVersion,
                    (IntPtr)1,
                    versionInfo.Language,
                    versionResource,
                    versionResource.Length);

                ok = resHandle.Commit() && ok;

                if (!ok)
                {
                    Log("Updating version resources failed", MessageImportance.High);
                    return false;
                }
            }

            if (!string.IsNullOrEmpty(KeyContainer) || (KeyFile != null && !string.IsNullOrEmpty(KeyFile.ItemSpec)))
            {
                Log("Resigning assembly", MessageImportance.Normal);

                if (!ResignAssemblyWithFileOrContainer(sourcePath, KeyFile != null ? KeyFile.ItemSpec : null, KeyContainer))
                {
                    Log("Resigning assembly failed", MessageImportance.High);
                    return false;
                }
            }

            SourceUpdated = true;
            return true;
        }
        catch (Exception e)
        {
            Log("Updating version resources failed: " + e.Message, MessageImportance.High);
            return false;
        }
        finally
        {
            if (!SourceUpdated && Source != null && File.Exists(Source.ItemSpec))
            {
                File.Delete(Source.ItemSpec);
            }
        }
    }

    void Log(string message, MessageImportance importance)
    {
        BuildEngine?.LogMessageEvent(new BuildMessageEventArgs(message, null, nameof(UpdateNativeVersionResource), importance));
    }

    bool ResignAssemblyWithFileOrContainer(string assembly, string keyFile, string keyContainer)
    {
        if (!string.IsNullOrEmpty(keyContainer))
        {
            if (ResignAssembly(assembly, "-q -Rca \"" + assembly + "\" \"" + keyContainer + "\""))
            {
                return true;
            }
        }

        if (!string.IsNullOrEmpty(keyFile))
        {
            return ResignAssembly(assembly, "-q -Ra \"" + assembly + "\" \"" + keyFile + "\"");
        }

        return string.IsNullOrEmpty(keyContainer);
    }

#pragma warning disable S1172 // Unused method parameters should be removed
#pragma warning disable IDE0060 // Remove unused parameter
    bool ResignAssembly(string assembly, string arguments)
#pragma warning restore IDE0060 // Remove unused parameter
#pragma warning restore S1172 // Unused method parameters should be removed
    {
        string snExe = FindStrongNameTool();
        if (string.IsNullOrEmpty(snExe))
        {
            throw new FileNotFoundException("sn.exe was not found. Set StrongNameToolPath or put sn.exe on PATH.", "sn.exe");
        }

        ProcessStartInfo psi = new ProcessStartInfo(snExe, arguments)
        {
            UseShellExecute = false,
            WindowStyle = ProcessWindowStyle.Hidden,
            CreateNoWindow = true
        };

        using Process p = Process.Start(psi);
        p.WaitForExit();
        return p.ExitCode == 0;
    }

    string FindStrongNameTool()
    {
        if (!string.IsNullOrWhiteSpace(StrongNameToolPath) && File.Exists(StrongNameToolPath))
        {
            return StrongNameToolPath;
        }

        return FindFileInPath("sn.exe");
    }

    static string FindFileInPath(string file)
    {
        string path = Environment.GetEnvironmentVariable("PATH");
        if (string.IsNullOrEmpty(path))
        {
            return null;
        }

        foreach (string entry in path.Split(Path.PathSeparator))
        {
            if (string.IsNullOrWhiteSpace(entry))
            {
                continue;
            }

            string fullPath = Path.GetFullPath(Path.Combine(entry, file));
            if (File.Exists(fullPath))
            {
                return fullPath;
            }
        }

        return null;
    }

    sealed class VersionInfo
    {
        public ushort Language { get; } = 0x0409;
        public ushort CodePage { get; } = 1200;
        public string AssemblyName { get; private set; }
        public Version AssemblyVersion { get; private set; }
        public string FileName { get; private set; }
        public string CompanyName { get; private set; }
        public string FileDescription { get; private set; }
        public string FileVersion { get; private set; }
        public string InternalName { get; private set; }
        public string LegalCopyright { get; private set; }
        public string OriginalFilename { get; private set; }
        public string ProductName { get; private set; }
        public string ProductVersion { get; private set; }

        public static VersionInfo FromAssembly(
            string sourcePath,
            string companyName,
            string fileDescription,
            string productName,
            string legalCopyright,
            string fileVersion,
            string productVersion)
        {
            System.Reflection.AssemblyName assemblyName = System.Reflection.AssemblyName.GetAssemblyName(sourcePath);
            Version version = assemblyName.Version ?? new Version(0, 0, 0, 0);
            string versionText = NormalizeVersionText(version);
            string name = assemblyName.Name ?? Path.GetFileNameWithoutExtension(sourcePath);

            return new VersionInfo
            {
                AssemblyName = name,
                AssemblyVersion = NormalizeVersion(version),
                FileName = Path.GetFileName(sourcePath),
                CompanyName = ValueOrDefault(companyName, string.Empty),
                FileDescription = ValueOrDefault(fileDescription, name),
                FileVersion = ValueOrDefault(fileVersion, versionText),
                InternalName = name,
                LegalCopyright = ValueOrDefault(legalCopyright, string.Empty),
                OriginalFilename = Path.GetFileName(sourcePath),
                ProductName = ValueOrDefault(productName, name),
                ProductVersion = ValueOrDefault(productVersion, versionText)
            };
        }

        static string ValueOrDefault(string value, string fallback)
        {
            return string.IsNullOrWhiteSpace(value) ? fallback : value;
        }

        static Version NormalizeVersion(Version version)
        {
            return new Version(
                Math.Max(version.Major, 0),
                Math.Max(version.Minor, 0),
                Math.Max(version.Build, 0),
                Math.Max(version.Revision, 0));
        }

        static string NormalizeVersionText(Version version)
        {
            return string.Format(
                CultureInfo.InvariantCulture,
                "{0}.{1}.{2}.{3}",
                Math.Max(version.Major, 0),
                Math.Max(version.Minor, 0),
                Math.Max(version.Build, 0),
                Math.Max(version.Revision, 0));
        }
    }

    sealed class VersionResourceBuilder
    {
        readonly List<byte> _bytes = new List<byte>();

        public static byte[] Build(VersionInfo info)
        {
            VersionResourceBuilder builder = new VersionResourceBuilder();
            builder.WriteRoot(info);
            return builder._bytes.ToArray();
        }

        void WriteRoot(VersionInfo info)
        {
            WriteBlock("VS_VERSION_INFO", 52, 0, () =>
            {
                WriteFixedFileInfo(info.AssemblyVersion);
                AlignToDword();
                WriteStringFileInfo(info);
                WriteVarFileInfo(info);
            });
        }

        void WriteFixedFileInfo(Version version)
        {
            WriteInt32(VsFfiSignature);
            WriteInt32(VsFfiStrucVersion);
            WriteInt32(MakeVersionPart(version.Major, version.Minor));
            WriteInt32(MakeVersionPart(version.Build, version.Revision));
            WriteInt32(MakeVersionPart(version.Major, version.Minor));
            WriteInt32(MakeVersionPart(version.Build, version.Revision));
            WriteInt32(VsFfiFileFlagsMask);
            WriteInt32(0);
            WriteInt32(VosNtWindows32);
            WriteInt32(VftDll);
            WriteInt32(0);
            WriteInt32(0);
            WriteInt32(0);
        }

        static int MakeVersionPart(int high, int low)
        {
            return ((high & 0xFFFF) << 16) | (low & 0xFFFF);
        }

        void WriteStringFileInfo(VersionInfo info)
        {
            WriteBlock("StringFileInfo", 0, 1, () =>
            {
                WriteBlock(
                    string.Format(CultureInfo.InvariantCulture, "{0:X4}{1:X4}", info.Language, info.CodePage),
                    0,
                    1,
                    () =>
                    {
                        WriteString("CompanyName", info.CompanyName);
                        WriteString("FileDescription", info.FileDescription);
                        WriteString("FileVersion", info.FileVersion);
                        WriteString("InternalName", info.InternalName);
                        WriteString("LegalCopyright", info.LegalCopyright);
                        WriteString("OriginalFilename", info.OriginalFilename);
                        WriteString("ProductName", info.ProductName);
                        WriteString("ProductVersion", info.ProductVersion);
                        WriteString("Assembly Version", info.AssemblyVersion.ToString());
                    });
            });
        }

        void WriteVarFileInfo(VersionInfo info)
        {
            WriteBlock("VarFileInfo", 0, 1, () =>
            {
                WriteBlock("Translation", 4, 0, () =>
                {
                    WriteUInt16(info.Language);
                    WriteUInt16(info.CodePage);
                });
            });
        }

        void WriteString(string key, string value)
        {
            string text = value ?? string.Empty;
            WriteBlock(key, (ushort)(text.Length + 1), 1, () => WriteUnicodeString(text));
        }

        void WriteBlock(string key, ushort valueLength, ushort type, Action writeValueAndChildren)
        {
            int start = _bytes.Count;
            WriteUInt16(0);
            WriteUInt16(valueLength);
            WriteUInt16(type);
            WriteUnicodeString(key);
            AlignToDword();

            writeValueAndChildren();
            AlignToDword();

            int length = _bytes.Count - start;
            _bytes[start] = (byte)(length & 0xFF);
            _bytes[start + 1] = (byte)((length >> 8) & 0xFF);
        }

        void AlignToDword()
        {
            while ((_bytes.Count % 4) != 0)
            {
                _bytes.Add(0);
            }
        }

        void WriteUnicodeString(string value)
        {
            byte[] encoded = Encoding.Unicode.GetBytes((value ?? string.Empty) + "\0");
            _bytes.AddRange(encoded);
        }

        void WriteUInt16(int value)
        {
            _bytes.Add((byte)(value & 0xFF));
            _bytes.Add((byte)((value >> 8) & 0xFF));
        }

        void WriteInt32(int value)
        {
            _bytes.Add((byte)(value & 0xFF));
            _bytes.Add((byte)((value >> 8) & 0xFF));
            _bytes.Add((byte)((value >> 16) & 0xFF));
            _bytes.Add((byte)((value >> 24) & 0xFF));
        }
    }
}
