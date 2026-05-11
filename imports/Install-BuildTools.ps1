param(
    [string] $GlobalToolRoot = (Join-Path $env:LOCALAPPDATA "Fetch\BuildTools"),
    [switch] $Force
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"

$nantVersion = "0.92.0"
$nantPackageUrl = "https://www.nuget.org/api/v2/package/NAnt/$nantVersion"

$packages = @(
    @{
        Name = "Strawberry Perl"
        Id = "StrawberryPerl.StrawberryPerl"
        Command = "perl.exe"
    },
    @{
        Name = "Python 2"
        Id = "Python.Python.2"
        Command = "python.exe"
        VersionArgument = "--version"
        VersionPattern = "^Python 2\."
    }
)

function Write-Step([string] $Message) {
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Resolve-FullPath([string] $Path) {
    return [System.IO.Path]::GetFullPath($Path)
}

function Remove-DirectoryInsideRoot([string] $Path, [string] $Root) {
    $fullRoot = Resolve-FullPath $Root
    $fullPath = Resolve-FullPath $Path

    if (!$fullPath.StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove '$fullPath' because it is outside '$fullRoot'."
    }

    if (Test-Path -LiteralPath $fullPath) {
        Remove-Item -LiteralPath $fullPath -Recurse -Force
    }
}

function Add-WinGetLinksToPath() {
    if (!$env:LOCALAPPDATA) {
        return
    }

    $links = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Links"
    if ((Test-Path -LiteralPath $links) -and (($env:PATH -split [System.IO.Path]::PathSeparator) -notcontains $links)) {
        $env:PATH = $links + [System.IO.Path]::PathSeparator + $env:PATH
    }
}

function Find-CommandPaths([string] $CommandName) {
    Add-WinGetLinksToPath
    return @(Get-Command $CommandName -CommandType Application -All -ErrorAction SilentlyContinue | ForEach-Object { $_.Source })
}

function Test-IsWindowsAppExecutionAlias([string] $Path) {
    if (!$env:LOCALAPPDATA) {
        return $false
    }

    $windowsApps = Join-Path $env:LOCALAPPDATA "Microsoft\WindowsApps"
    return $Path.StartsWith($windowsApps, [System.StringComparison]::OrdinalIgnoreCase)
}

function Find-CommandPath([string] $CommandName) {
    return Find-CommandPaths $CommandName | Select-Object -First 1
}

function Test-PackageCommand([hashtable] $Package, [string] $Path) {
    if (Test-IsWindowsAppExecutionAlias $Path) {
        return $false
    }

    if (!$Package.ContainsKey("VersionPattern")) {
        return $true
    }

    $versionArgument = $Package.VersionArgument
    try {
        $output = & $Path $versionArgument 2>&1 | Out-String
    }
    catch {
        return $false
    }

    if ($LASTEXITCODE -ne 0) {
        return $false
    }

    return $output.Trim() -match $Package.VersionPattern
}

function Find-PackageCommand([hashtable] $Package) {
    foreach ($path in Find-CommandPaths $Package.Command) {
        if (Test-PackageCommand $Package $path) {
            return $path
        }
    }

    return $null
}

function Test-ZipHeader([string] $Path) {
    if (!(Test-Path -LiteralPath $Path)) {
        return $false
    }

    $stream = [System.IO.File]::OpenRead($Path)
    try {
        if ($stream.Length -lt 2) {
            return $false
        }

        return ($stream.ReadByte() -eq 0x50) -and ($stream.ReadByte() -eq 0x4B)
    }
    finally {
        $stream.Dispose()
    }
}

function Invoke-Download([string] $Url, [string] $Destination) {
    Write-Step "Downloading $(Split-Path -Leaf $Destination)"
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

    $request = @{
        Uri = $Url
        OutFile = $Destination
        MaximumRedirection = 10
        UserAgent = "Mozilla/5.0 SharpSvn-BuildTools"
    }

    if ($PSVersionTable.PSVersion.Major -lt 6) {
        $request.UseBasicParsing = $true
    }

    Invoke-WebRequest @request

    if (!(Test-ZipHeader $Destination)) {
        Remove-Item -LiteralPath $Destination -Force
        throw "Downloaded '$Url' did not produce a valid zip archive."
    }
}

function Find-Tool([string] $Root, [string] $Probe) {
    $match = Get-ChildItem -LiteralPath $Root -Recurse -Filter $Probe -File | Select-Object -First 1
    if (!$match) {
        throw "Could not find '$Probe' under '$Root'."
    }

    return $match.FullName
}

function Add-UserPath([string] $Path) {
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $entries = @()
    if ($userPath) {
        $entries = @($userPath -split [System.IO.Path]::PathSeparator | Where-Object { $_ })
    }

    if ($entries -notcontains $Path) {
        $newUserPath = (($entries + @($Path)) -join [System.IO.Path]::PathSeparator)
        [Environment]::SetEnvironmentVariable("Path", $newUserPath, "User")
    }

    if (($env:PATH -split [System.IO.Path]::PathSeparator) -notcontains $Path) {
        $env:PATH = $Path + [System.IO.Path]::PathSeparator + $env:PATH
    }
}

function Ensure-NAnt() {
    $existing = Find-CommandPath "nant.exe"
    if ($existing -and !$Force) {
        Write-Host "Using existing NAnt: $existing"
        return $existing
    }

    if (!$env:LOCALAPPDATA) {
        throw "LOCALAPPDATA is not set, so the user-global NAnt install path cannot be resolved."
    }

    $downloadRoot = Join-Path $GlobalToolRoot "downloads"
    $installRoot = Join-Path $GlobalToolRoot "NAnt-$nantVersion"
    $archivePath = Join-Path $downloadRoot "NAnt.$nantVersion.zip"

    New-Item -ItemType Directory -Path $downloadRoot -Force | Out-Null

    if (!(Test-ZipHeader $archivePath) -or $Force) {
        Invoke-Download $nantPackageUrl $archivePath
    }

    if (!(Test-Path -LiteralPath $installRoot) -or $Force) {
        Write-Step "Installing NAnt $nantVersion"
        Remove-DirectoryInsideRoot $installRoot $GlobalToolRoot
        New-Item -ItemType Directory -Path $installRoot -Force | Out-Null
        Expand-Archive -LiteralPath $archivePath -DestinationPath $installRoot -Force
    }

    $nant = Find-Tool $installRoot "NAnt.exe"
    Add-UserPath (Split-Path -Parent $nant)
    return $nant
}

function Invoke-WinGetInstall([string] $PackageId) {
    $winget = Find-CommandPath "winget.exe"
    if (!$winget) {
        throw "winget.exe was not found. Install or repair Windows App Installer, then rerun this script."
    }

    & $winget install --exact --id $PackageId --accept-source-agreements --accept-package-agreements
    if ($LASTEXITCODE -eq -1978335189) {
        Write-Host "winget reports '$PackageId' has no applicable install action; treating it as already present."
        return
    }

    if ($LASTEXITCODE -ne 0) {
        throw "winget failed to install '$PackageId' with exit code $LASTEXITCODE."
    }
}

$installed = @{}
$installed["NAnt"] = Ensure-NAnt

foreach ($package in $packages) {
    $existing = Find-PackageCommand $package
    if ($existing -and !$Force) {
        Write-Host "Using existing $($package.Name): $existing"
        $installed[$package.Name] = $existing
        continue
    }

    Write-Step "Installing $($package.Name) with winget ($($package.Id))"
    Invoke-WinGetInstall $package.Id

    $installedPath = Find-PackageCommand $package
    if ($installedPath) {
        $installed[$package.Name] = $installedPath
    }
    else {
        Write-Warning "$($package.Name) installed, but '$($package.Command)' is not visible in this shell yet. Open a new Visual Studio Developer Command Prompt before building."
    }
}

Write-Host ""
Write-Host "SharpSvn build tool setup complete." -ForegroundColor Green

if ($installed.Count -gt 0) {
    Write-Host "Visible commands:"
    foreach ($name in $installed.Keys | Sort-Object) {
        Write-Host "  $name -> $($installed[$name])"
    }
}

Write-Host ""
Write-Host "Open a fresh shell, then run:"
Write-Host "  cd `"$PSScriptRoot`""
Write-Host "  nant -buildfile:Default.build x64"
