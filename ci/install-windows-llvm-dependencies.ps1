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

$vcpkgRoots = @(
    $env:VCPKG_INSTALLATION_ROOT
    $env:VCPKG_ROOT
) |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_) }

$vcpkgCommand = Get-Command vcpkg.exe -ErrorAction SilentlyContinue
if ($vcpkgCommand) {
    $vcpkgRoots += Split-Path -Parent $vcpkgCommand.Source
}

$vcpkgCandidates = $vcpkgRoots |
    ForEach-Object { Join-Path $_ 'vcpkg.exe' } |
    Select-Object -Unique
$vcpkgPath = $vcpkgCandidates |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if (-not $vcpkgPath) {
    $vcpkgCandidates | Set-Content -Encoding utf8 'ci\vcpkg-install.log'
    throw 'A vcpkg installation was not discovered on the Windows runner'
}

$vcpkgRoot = Split-Path -Parent $vcpkgPath
$toolchainFile = Join-Path $vcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
if (-not (Test-Path $toolchainFile)) {
    throw "vcpkg CMake toolchain file was not found: $toolchainFile"
}

Write-Host "Using vcpkg executable: $vcpkgPath"
Write-Host "Installing LLVM component dependencies with vcpkg root: $vcpkgRoot"
& $vcpkgPath install --classic --vcpkg-root $vcpkgRoot --triplet x64-windows-static zlib zstd libxml2 2>&1 |
    Tee-Object 'ci\vcpkg-install.log'
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Add-EnvironmentLine -Name 'VCPKG_ROOT' -Value $vcpkgRoot
Add-EnvironmentLine -Name 'BHA_VCPKG_TOOLCHAIN_FILE' -Value $toolchainFile
