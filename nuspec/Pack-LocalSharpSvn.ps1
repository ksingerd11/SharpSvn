[CmdletBinding()]
param(
    [string]$Version = "1.14005.390-fetch.1",
    [ValidateSet("x64")]
    [string]$Platform = "x64",
    [string]$Configuration = "ReleaseCore",
    [string]$OutputDirectory,
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$nuspecRoot = $PSScriptRoot
$sharpSvnRoot = Resolve-Path (Join-Path $nuspecRoot "..")
$fetchRoot = Resolve-Path (Join-Path $sharpSvnRoot "..\..")

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $fetchRoot "lib\SharpSvn"
}

function Require-File {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required package input is missing: $Path"
    }
}

function Find-MSBuild {
    $knownPaths = @(
        "${env:ProgramFiles}\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\amd64\MSBuild.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\18\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\amd64\MSBuild.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"
    )

    foreach ($path in $knownPaths) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            return $path
        }
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\amd64\MSBuild.exe" | Select-Object -First 1
        if ($msbuild) {
            return $msbuild
        }
    }

    throw "MSBuild.exe was not found. Install Visual Studio Build Tools or pass -NoBuild after building SharpSvn."
}

function Copy-PackageFile {
    param(
        [string]$Source,
        [string]$TargetRelativePath
    )

    Require-File $Source
    $target = Join-Path $packageRoot $TargetRelativePath
    $targetDirectory = Split-Path -Parent $target
    New-Item -ItemType Directory -Force -Path $targetDirectory | Out-Null
    Copy-Item -LiteralPath $Source -Destination $target -Force
}

function Write-TextFile {
    param(
        [string]$Path,
        [string]$Content
    )

    $directory = Split-Path -Parent $Path
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Content, $utf8NoBom)
}

if (-not $NoBuild) {
    $msbuild = Find-MSBuild
    $solution = Join-Path $sharpSvnRoot "src\SharpSvn.sln"
    Write-Host "==> Building SharpSvn $Configuration|$Platform"
    & $msbuild $solution "/p:Configuration=$Configuration" "/p:Platform=$Platform" "/m:1" "/clp:ErrorsOnly"
    if ($LASTEXITCODE -ne 0) {
        throw "MSBuild failed with exit code $LASTEXITCODE."
    }
}

$buildOutput = Join-Path $sharpSvnRoot "src\SharpSvn\bin\$Platform\$Configuration"
$nativeOutput = Join-Path $sharpSvnRoot "imports\release\bin"
$targetFramework = "net10.0-windows"
$rid = "win-x64"

$sharpSvnDll = Join-Path $buildOutput "SharpSvn.dll"
$sharpSvnPdb = Join-Path $buildOutput "SharpSvn.pdb"
$sharpSvnXml = Join-Path $buildOutput "SharpSvn.xml"
$ijwHost = Join-Path $buildOutput "ijwhost.dll"
if (-not (Test-Path -LiteralPath $ijwHost -PathType Leaf)) {
    $ijwHost = Join-Path $buildOutput "Ijwhost.dll"
}
$sharpPlink = Join-Path $buildOutput "SharpPlink-x64.svnExe"
$dbDll = Join-Path $nativeOutput "SharpSvn-DB44-20-x64.svnDll"
$targetsFile = Join-Path $nuspecRoot "builds\SharpSvn.targets"
$readme = Join-Path $sharpSvnRoot "README.md"

Require-File $sharpSvnDll
Require-File $ijwHost
Require-File $sharpPlink
Require-File $dbDll
Require-File $targetsFile
Require-File $readme

$packageId = "SharpSvn"
$stagingRoot = Join-Path $nuspecRoot "obj\local-package"
$packageRoot = Join-Path $stagingRoot "$packageId.$Version"
$nupkgPath = Join-Path $OutputDirectory "$packageId.$Version.nupkg"

if (Test-Path -LiteralPath $packageRoot) {
    Remove-Item -LiteralPath $packageRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

Copy-PackageFile $sharpSvnDll "ref\$targetFramework\SharpSvn.dll"
if (Test-Path -LiteralPath $sharpSvnXml -PathType Leaf) {
    Copy-PackageFile $sharpSvnXml "ref\$targetFramework\SharpSvn.xml"
}

Copy-PackageFile $sharpSvnDll "runtimes\$rid\lib\$targetFramework\SharpSvn.dll"
Copy-PackageFile $ijwHost "runtimes\$rid\lib\$targetFramework\ijwhost.dll"
Copy-PackageFile $sharpPlink "runtimes\$rid\lib\$targetFramework\SharpPlink-x64.svnExe"
Copy-PackageFile $dbDll "runtimes\$rid\lib\$targetFramework\SharpSvn-DB44-20-x64.svnDll"
if (Test-Path -LiteralPath $sharpSvnPdb -PathType Leaf) {
    Copy-PackageFile $sharpSvnPdb "runtimes\$rid\lib\$targetFramework\SharpSvn.pdb"
}
if (Test-Path -LiteralPath $sharpSvnXml -PathType Leaf) {
    Copy-PackageFile $sharpSvnXml "runtimes\$rid\lib\$targetFramework\SharpSvn.xml"
}

Copy-PackageFile $targetsFile "build\SharpSvn.targets"
Copy-PackageFile $targetsFile "buildTransitive\SharpSvn.targets"
Copy-PackageFile $readme "docs\README.md"

$nuspec = @"
<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://schemas.microsoft.com/packaging/2012/06/nuspec.xsd">
  <metadata>
    <id>$packageId</id>
    <version>$Version</version>
    <title>SharpSvn - Local Fetch Build</title>
    <authors>SharpSvn</authors>
    <owners>Fetch</owners>
    <license type="expression">Apache-2.0</license>
    <projectUrl>https://github.com/AmpScm/SharpSvn/</projectUrl>
    <requireLicenseAcceptance>false</requireLicenseAcceptance>
    <description>Local Fetch build of SharpSvn for net10.0-windows x64.</description>
    <readme>docs\README.md</readme>
    <tags>Subversion Svn SharpSvn Fetch Local</tags>
    <dependencies>
      <group targetFramework="$targetFramework" />
    </dependencies>
    <references>
      <group targetFramework="$targetFramework">
        <reference file="SharpSvn.dll" />
      </group>
    </references>
  </metadata>
</package>
"@
Write-TextFile (Join-Path $packageRoot "$packageId.nuspec") $nuspec

$relationships = @"
<?xml version="1.0" encoding="utf-8"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Type="http://schemas.microsoft.com/packaging/2010/07/manifest" Target="/$packageId.nuspec" Id="R1" />
</Relationships>
"@
Write-TextFile (Join-Path $packageRoot "_rels\.rels") $relationships

$contentTypes = @"
<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml" />
  <Default Extension="nuspec" ContentType="application/octet" />
  <Default Extension="dll" ContentType="application/octet" />
  <Default Extension="pdb" ContentType="application/octet" />
  <Default Extension="xml" ContentType="application/xml" />
  <Default Extension="md" ContentType="text/markdown" />
  <Default Extension="targets" ContentType="application/xml" />
  <Default Extension="svnExe" ContentType="application/octet" />
  <Default Extension="svnDll" ContentType="application/octet" />
</Types>
"@
Write-TextFile (Join-Path $packageRoot "[Content_Types].xml") $contentTypes

if (Test-Path -LiteralPath $nupkgPath) {
    Remove-Item -LiteralPath $nupkgPath -Force
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($packageRoot, $nupkgPath, [System.IO.Compression.CompressionLevel]::Optimal, $false)

Write-Host "==> Wrote $nupkgPath"

$globalPackageCache = Join-Path $env:USERPROFILE ".nuget\packages\sharpsvn\$($Version.ToLowerInvariant())"
if (Test-Path -LiteralPath $globalPackageCache) {
    Remove-Item -LiteralPath $globalPackageCache -Recurse -Force
    Write-Host "==> Cleared NuGet global package cache entry $globalPackageCache"
}
