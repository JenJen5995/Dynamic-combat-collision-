# Build Dynamic Combat Collision for Flatrim (SE/AE) and VR.
param(
  [switch]$Reconfigure,
  [ValidateSet("flatrim", "vr", "all")]
  [string]$Target = "all"
)

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
$pathsFile = Join-Path $Root "local.paths.ps1"
if (-not (Test-Path $pathsFile)) {
  throw "Missing local.paths.ps1. Copy local.paths.ps1.example to local.paths.ps1 and edit the paths."
}
. $pathsFile

function Find-DccCMake {
  $fromPath = Get-Command cmake.exe -ErrorAction SilentlyContinue
  if ($fromPath) {
    return $fromPath.Source
  }

  $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
  if (Test-Path $vswhere) {
    $vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath 2>$null
    if ($vsRoot) {
      $candidate = Join-Path $vsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
      if (Test-Path $candidate) {
        return $candidate
      }
    }
  }

  foreach ($edition in @("Community", "Professional", "Enterprise", "BuildTools")) {
    $candidate = Join-Path ${env:ProgramFiles} "Microsoft Visual Studio\2022\$edition\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (Test-Path $candidate) {
      return $candidate
    }
  }

  throw "CMake not found. Install Visual Studio 2022 with the C++ CMake tools, or add cmake.exe to PATH."
}

$cmake = Find-DccCMake

if (-not (Test-Path $CommonLibSSEPath)) {
  throw "CommonLibSSE not found: $CommonLibSSEPath (edit local.paths.ps1)"
}

if (-not (Test-Path $VcpkgRoot)) {
  throw "vcpkg not found: $VcpkgRoot (edit local.paths.ps1)"
}

$env:VCPKG_ROOT = $VcpkgRoot
$env:CommonLibSSEPath_NG = $CommonLibSSEPath

function Get-DccVersion {
  $cmakeLists = Get-Content (Join-Path $Root "CMakeLists.txt") -Raw
  if ($cmakeLists -match 'project\(\s*DynamicCombatCollision\s+VERSION\s+(\d+)\.(\d+)\.(\d+)') {
    return "$($Matches[1]).$($Matches[2]).$($Matches[3])"
  }
  throw "Could not read project VERSION from CMakeLists.txt"
}

$DccVersion = Get-DccVersion
$vcpkgJson = Join-Path $Root "vcpkg.json"
if (Test-Path $vcpkgJson) {
  $vcpkg = Get-Content $vcpkgJson -Raw | ConvertFrom-Json
  if ($vcpkg.version -ne $DccVersion) {
    throw "vcpkg.json version $($vcpkg.version) != CMake $DccVersion"
  }
}

$Profiles = @{
  flatrim = @{
    BuildDir = Join-Path $Root "build"
    ZipName  = "DynamicCombatCollision-$DccVersion.zip"
    CmakeArgs = @(
      "-DENABLE_SKYRIM_SE=ON"
      "-DENABLE_SKYRIM_AE=ON"
      "-DENABLE_SKYRIM_VR=OFF"
    )
  }
  vr = @{
    BuildDir = Join-Path $Root "build-vr"
    ZipName  = "DynamicCombatCollision-$DccVersion-vr.zip"
    CmakeArgs = @(
      "-DENABLE_SKYRIM_SE=OFF"
      "-DENABLE_SKYRIM_AE=OFF"
      "-DENABLE_SKYRIM_VR=ON"
    )
  }
}

function Invoke-DccBuild {
  param(
    [string]$Name,
    [hashtable]$Profile
  )

  $commonArgs = @(
    "-S", $Root,
    "-G", "Visual Studio 17 2022",
    "-A", "x64",
    "-T", "v143,version=14.44.35207",
    "-DCMAKE_VS_GLOBALS=VCToolsVersion=14.44.35207",
    "-DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot/scripts/buildsystems/vcpkg.cmake",
    "-DVCPKG_TARGET_TRIPLET=x64-windows-static-md",
    "-DCOMMONLIB_SSE_PATH=$CommonLibSSEPath"
  )

  $prevEap = $ErrorActionPreference
  $ErrorActionPreference = "Continue"

  if ($Reconfigure -and (Test-Path $Profile.BuildDir)) {
    Remove-Item -Recurse -Force $Profile.BuildDir
  }

  if ($Reconfigure -or -not (Test-Path (Join-Path $Profile.BuildDir "CMakeCache.txt"))) {
    Write-Host "Configuring $Name..."
    & $cmake -B $Profile.BuildDir @commonArgs @($Profile.CmakeArgs) 2>&1 | Write-Host
    if ($LASTEXITCODE -ne 0) {
      $ErrorActionPreference = $prevEap
      exit $LASTEXITCODE
    }
  }

  Write-Host "Building $Name Release..."
  & $cmake --build $Profile.BuildDir --config Release --target DynamicCombatCollision -- /p:VCToolsVersion=14.44.35207 2>&1 | Write-Host
  if ($LASTEXITCODE -ne 0) {
    $ErrorActionPreference = $prevEap
    exit $LASTEXITCODE
  }

  $ErrorActionPreference = $prevEap

  $dll = Join-Path $Profile.BuildDir "Release\DynamicCombatCollision.dll"
  if (-not (Test-Path $dll)) {
    throw "Build finished but DLL missing: $dll"
  }
  $pdb = [System.IO.Path]::ChangeExtension($dll, ".pdb")
  if (-not (Test-Path $pdb)) {
    throw "Build finished but PDB missing: $pdb"
  }

  return $dll
}

function Copy-DccItems {
  param(
    [string]$PackageDir,
    [array]$Items,
    [string]$MissingPrefix
  )

  $DataRoot = Join-Path $Root "data"
  if (Test-Path $PackageDir) {
    Remove-Item -Recurse -Force $PackageDir
  }

  foreach ($item in $Items) {
    $srcPath = if ($item.Src) { $item.Src } else { Join-Path $DataRoot $item.Rel }
    if (-not (Test-Path $srcPath)) {
      throw "$MissingPrefix missing required file: $srcPath"
    }
    $destPath = Join-Path $PackageDir $item.Rel
    New-Item -ItemType Directory -Force -Path (Split-Path $destPath) | Out-Null
    if ($item.IsDir -or (Get-Item $srcPath).PSIsContainer) {
      Copy-Item -Force -Recurse $srcPath (Split-Path $destPath)
    } else {
      Copy-Item -Force $srcPath $destPath
    }
  }
}

function Get-DccPluginItems {
  param([string]$DllPath)
  return @(
    @{ Rel = "SKSE\Plugins\DynamicCombatCollision.dll"; Src = $DllPath },
    @{ Rel = "SKSE\Plugins\DynamicCombatCollision.pdb"; Src = [System.IO.Path]::ChangeExtension($DllPath, ".pdb") },
    @{ Rel = "Interface\Translations"; IsDir = $true },
    @{ Rel = "LICENSE"; Src = (Join-Path $Root "LICENSE") },
    @{ Rel = "THIRD_PARTY.md"; Src = (Join-Path $Root "THIRD_PARTY.md") }
  )
}

function Get-DccMcmItems {
  return @(
    @{ Rel = "DynamicCombatCollision.esp" },
    @{ Rel = "Scripts\DynamicCombatCollisionMCM.pex" },
    @{ Rel = "MCM\Config\DynamicCombatCollision"; IsDir = $true },
    @{ Rel = "LICENSE"; Src = (Join-Path $Root "LICENSE") },
    @{ Rel = "THIRD_PARTY.md"; Src = (Join-Path $Root "THIRD_PARTY.md") }
  )
}

function Pack-DccZip {
  param(
    [string]$PackageDir,
    [string]$ZipPath
  )

  if (Test-Path $ZipPath) {
    Remove-Item -Force $ZipPath
  }
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  [IO.Compression.ZipFile]::CreateFromDirectory(
    $PackageDir,
    $ZipPath,
    [IO.Compression.CompressionLevel]::Optimal,
    $false
  )
  Write-Host "Packed: $ZipPath"
}

$selected = if ($Target -eq "all") { @("flatrim", "vr") } else { @($Target) }
$built = @{}

foreach ($name in $selected) {
  $profile = $Profiles[$name]
  $built[$name] = Invoke-DccBuild -Name $name -Profile $profile

  $packageDir = Join-Path $Root "dist\package-$name"
  $zipPath = Join-Path $Root "dist\$($profile.ZipName)"
  Copy-DccItems -PackageDir $packageDir -Items (Get-DccPluginItems -DllPath $built[$name]) -MissingPrefix "Plugin package"
  Pack-DccZip -PackageDir $packageDir -ZipPath $zipPath
}

$mcmZipName = "DynamicCombatCollision-$DccVersion-mcm.zip"
$mcmPackageDir = Join-Path $Root "dist\package-mcm"
$mcmZipPath = Join-Path $Root "dist\$mcmZipName"
Copy-DccItems -PackageDir $mcmPackageDir -Items (Get-DccMcmItems) -MissingPrefix "MCM package"
Pack-DccZip -PackageDir $mcmPackageDir -ZipPath $mcmZipPath

if ($built.ContainsKey("flatrim") -and $VortexModPath -and (Test-Path $VortexModPath)) {
  $vortexDir = Join-Path $Root "dist\package-vortex-sync"
  $vortexItems = (Get-DccPluginItems -DllPath $built["flatrim"]) + (Get-DccMcmItems)
  Copy-DccItems -PackageDir $vortexDir -Items $vortexItems -MissingPrefix "Vortex sync"
  Copy-Item -Force -Recurse (Join-Path $vortexDir "*") $VortexModPath
  Write-Host "Synced flatrim plugin + MCM to Vortex staging: $VortexModPath"
}

Write-Host ""
Write-Host "Done."
if ($built.ContainsKey("flatrim")) {
  Write-Host "  Flatrim zip: dist\$($Profiles.flatrim.ZipName)"
}
if ($built.ContainsKey("vr")) {
  Write-Host "  VR zip:      dist\$($Profiles.vr.ZipName)"
}
Write-Host "  MCM zip:     dist\$mcmZipName"
