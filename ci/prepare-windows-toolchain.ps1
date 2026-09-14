param(
    [Parameter(Mandatory = $true)]
    [string]$LlvmRelease,

    [Parameter(Mandatory = $true)]
    [string]$LlvmArchive,

    [Parameter(Mandatory = $true)]
    [string]$LlvmSha256
)

$ErrorActionPreference = 'Stop'

function Add-EnvironmentLine {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        [string]$Value
    )

    "$Name=$Value" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
}

if (-not (Get-Command ninja.exe -ErrorAction SilentlyContinue)) {
    choco install ninja --no-progress --yes
    $machinePath = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = "$machinePath;$userPath"
}
$null = Get-Command ninja.exe -ErrorAction Stop

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw 'vswhere.exe was not found'
}

$vsPath = (& $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath | Select-Object -First 1).Trim()
if ([string]::IsNullOrWhiteSpace($vsPath)) {
    throw 'No Visual Studio installation with the x64 MSVC toolset was found'
}

$vsDevCmd = Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path $vsDevCmd)) {
    throw 'VsDevCmd.bat was not found under the discovered Visual Studio installation'
}

# Persist the discovered developer environment for the distinct CMake steps.
cmd.exe /s /c "`"$vsDevCmd`" -arch=x64 && set" | ForEach-Object {
    if ($_ -match '^([A-Za-z_][A-Za-z0-9_]*)=(.*)$') {
        $name = $matches[1]
        $value = $matches[2]
        if ($name -notmatch '^(GITHUB_|RUNNER_)') {
            Add-EnvironmentLine -Name $name -Value $value
        }
    }
}

$llvmCache = Join-Path $env:RUNNER_TEMP 'bha-llvm-cache'
$llvmArchivePath = Join-Path $llvmCache $LlvmArchive
$llvmRoot = Join-Path $env:RUNNER_TEMP 'bha-llvm-tooling'
$expectedHash = $LlvmSha256.ToLowerInvariant()

New-Item -ItemType Directory -Force -Path $llvmCache | Out-Null
if (Test-Path $llvmArchivePath) {
    $actualHash = (Get-FileHash -Algorithm SHA256 -Path $llvmArchivePath).Hash.ToLowerInvariant()
    if ($actualHash -ne $expectedHash) {
        Write-Host "Discarding LLVM archive with unexpected SHA-256: $actualHash"
        Remove-Item -Force $llvmArchivePath
    }
}

if (-not (Test-Path $llvmArchivePath)) {
    $escapedArchive = [Uri]::EscapeDataString($LlvmArchive)
    $downloadUri = "https://github.com/llvm/llvm-project/releases/download/$LlvmRelease/$escapedArchive"
    Write-Host "Downloading LLVM LibTooling archive: $LlvmArchive"
    Invoke-WebRequest -UseBasicParsing -Uri $downloadUri -OutFile $llvmArchivePath `
        -ConnectionTimeoutSeconds 30 -OperationTimeoutSeconds 1800 `
        -MaximumRetryCount 3 -RetryIntervalSec 10
}

$actualHash = (Get-FileHash -Algorithm SHA256 -Path $llvmArchivePath).Hash.ToLowerInvariant()
if ($actualHash -ne $expectedHash) {
    throw "LLVM archive SHA-256 mismatch. Expected $expectedHash, got $actualHash"
}

if (Test-Path $llvmRoot) {
    Remove-Item -Recurse -Force $llvmRoot
}
New-Item -ItemType Directory -Force -Path $llvmRoot | Out-Null
Write-Host 'Extracting LLVM LibTooling archive'
if (-not $LlvmArchive.EndsWith('.tar.zst', [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Unsupported LLVM archive format: $LlvmArchive"
}
$zstd = Get-Command zstd.exe -ErrorAction Stop
$extractCommand = "`"$($zstd.Source)`" --decompress --stdout --quiet " +
    "`"$llvmArchivePath`" | tar.exe -xf - -C `"$llvmRoot`" --strip-components=1"
& cmd.exe /d /s /c $extractCommand
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$toolingRoots = @(
    Get-ChildItem -Path $llvmRoot -Recurse -File -Filter 'Tooling.h' |
        Where-Object {
            $_.FullName.EndsWith('\include\clang\Tooling\Tooling.h', [System.StringComparison]::OrdinalIgnoreCase)
        } |
        ForEach-Object {
            Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $_.FullName)))
        } |
        Where-Object {
            (Test-Path (Join-Path $_ 'bin\clang-tidy.exe')) -and
            (Test-Path (Join-Path $_ 'lib\clang-cpp.lib'))
        } |
        Sort-Object -Unique
)
if ($toolingRoots.Count -ne 1) {
    $topLevelEntries = @(
        Get-ChildItem -Path $llvmRoot -Force | Select-Object -ExpandProperty Name
    ) -join ', '
    throw "LLVM archive does not contain exactly one complete Clang LibTooling root. Top-level entries: $topLevelEntries"
}

$llvmRoot = $toolingRoots[0]
$clangTidy = Join-Path $llvmRoot 'bin\clang-tidy.exe'
$clangToolingHeader = Join-Path $llvmRoot 'include\clang\Tooling\Tooling.h'
$clangToolingLibrary = Join-Path $llvmRoot 'lib\clang-cpp.lib'

"$llvmRoot\bin" | Out-File -FilePath $env:GITHUB_PATH -Encoding utf8 -Append
Add-EnvironmentLine -Name 'BHA_CLANG_TIDY' -Value $clangTidy
Add-EnvironmentLine -Name 'BHA_CLANG_TOOLING_ROOT' -Value $llvmRoot
Write-Host "LLVM LibTooling is ready at $llvmRoot"
