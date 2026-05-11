# SharpSvn
[![latest version](https://img.shields.io/nuget/v/SharpSvn)](https://www.nuget.org/packages/SharpSvn)
[![MSBuild-CI](https://github.com/AmpScm/SharpSvn/actions/workflows/MSBuild.yml/badge.svg)](https://github.com/AmpScm/SharpSvn/actions/workflows/MSBuild.yml)

Subversion wrapped for .Net 4.0+ and .Net Core

SharpSvn wraps the Subversion client (and a few more low level) apis in an easy to use .Net friendly client.


## Usage
The whole idea behind SharpSvn is that it should be as easy to install a simple NuGet package in your
C#/.Net application and use Subversion, without thinking about all the backing dependencies.

SharpSvn implements this idea by compiling all needed libraries via C++/CLI and including them in
the final library. (A pretty modern design decision around 2008 when the work on this library started)

The whole library survived from .Net 2.0 to the most recent .Net Core and now .Net 5/6 libraries,
but without a complete rewrite it is not really possible to support Linux/Mac/OS. So unless somebody
has a nice solution for this, those platforms are out of scope.

## History
This project started as a one man project at CollabNet's open.collab.net site around 2008. A few years
later Bert Huijben was hired by CollabNet to work on Subversion and AnkhSVN full-time and as AnkhSVN
was a SharpSvn user there was quite a focus on SharpSvn.

In 2017 Bert left CollabNet, and the project mostly stalled. After many requests Bert moved the hosting
to GitHub and wrote scripts to automate the builds there. Patches are very welcome to co-maintain
SharpSvn there, but releases will now most likely follow the Subversion releases again.

## NuGet
The last packages are now just available on
https://nuget.org/packages/SharpSvn

This package contains x86, x64 and ARM64 binaries that are automatically selected by the NuGet tool for you.
We are no longer building .Net 2.0 compatible versions as the build support for that is not available on GitHub.

## Local Fetch NuGet package

Fetch consumes the local SharpSvn build through the repo-local `lib\SharpSvn` NuGet feed. From the
Fetch repository root, build and pack the x64 .NET 10 package with:

```powershell
powershell -ExecutionPolicy Bypass -File .\Fetch.Vendored\SharpSvn\nuspec\Pack-LocalSharpSvn.ps1
```

The script builds `ReleaseCore|x64` and writes `lib\SharpSvn\SharpSvn.1.14005.390-fetch.1.nupkg`.
Pass `-NoBuild` to repack from existing `src\SharpSvn\bin\x64\ReleaseCore` outputs.

## Building from source

SharpSvn's C++/CLI project needs a generated native dependency tree under `imports\release`.
That tree contains the Subversion/APR/Serf/LibSSH2 headers, libraries, gettext `.po` files, and
runtime dependency binaries used by the Visual Studio projects.

Before building `src\SharpSvn.sln`, install the required command-line build tools globally:

```powershell
powershell -ExecutionPolicy Bypass -File .\imports\Install-BuildTools.ps1
```

This installs NAnt 0.92 from NuGet into a user-global tools folder, and installs the winget
packages `StrawberryPerl.StrawberryPerl` and `Python.Python.2`.
Open a fresh shell after install so the updated `PATH` is visible, then build the native imports.
From a regular command prompt or PowerShell, let NAnt bootstrap the Visual Studio environment:

```cmd
cd imports
nant -buildfile:Default.build x64
```

From an already-initialized Visual Studio Developer Command Prompt, this direct form also works:

```cmd
cd imports
nant -buildfile:Default.build /D:platform=x64 build
```

Use `win32` or `ARM64` instead of `x64` when building those platforms. If `imports\release` has
not been generated, `SharpSvn.vcxproj` stops before its custom build steps and reports the missing
native import path.
