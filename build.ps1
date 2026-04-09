param(
    [string]$Output = "duosort.exe"
)

$ErrorActionPreference = "Stop"

function Find-Gpp {
    $command = Get-Command g++ -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $candidates = @(
        "C:\msys64\mingw64\bin\g++.exe",
        "C:\Program Files\MSYS2\mingw64\bin\g++.exe"
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    return $null
}

function Install-Msys2Toolchain {
    $winget = Get-Command winget -ErrorAction SilentlyContinue
    if (-not $winget) {
        throw "g++ was not found and winget is unavailable. Install MSYS2 with the MinGW-w64 toolchain, then rerun build.ps1."
    }

    Write-Host "Installing MSYS2 and the MinGW-w64 C++ toolchain..."
    & $winget.Source install --id MSYS2.MSYS2 --exact --accept-package-agreements --accept-source-agreements --disable-interactivity
    if ($LASTEXITCODE -ne 0) {
        throw "winget failed to install MSYS2."
    }

    $msysShells = @(
        "C:\msys64\usr\bin\bash.exe",
        "C:\Program Files\MSYS2\usr\bin\bash.exe"
    )

    $bash = $msysShells | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $bash) {
        throw "MSYS2 installed, but bash.exe was not found."
    }

    & $bash -lc "pacman --noconfirm -Sy --needed mingw-w64-x86_64-gcc"
    if ($LASTEXITCODE -ne 0) {
        throw "MSYS2 package installation failed."
    }
}

$gpp = Find-Gpp
if (-not $gpp) {
    Install-Msys2Toolchain
    $gpp = Find-Gpp
}
if (-not $gpp) {
    throw "Unable to find g++ after attempting toolchain installation."
}

$sources = Get-ChildItem -Path "src" -Filter "*.cpp" | ForEach-Object { $_.FullName }
if (-not $sources -or $sources.Count -eq 0) {
    throw "No C++ source files found in src/."
}

# -mwindows builds a GUI subsystem app so Windows does not open a console window.
# Static runtime flags reduce machine-specific DLL requirements for new users.
$args = @(
    "-std=c++17",
    "-O2",
    "-Wall",
    "-Wextra",
    "-mwindows",
    "-static",
    "-static-libgcc",
    "-static-libstdc++",
    "-Iinclude"
) + $sources + @(
    "-lole32",
    "-lshell32",
    "-lcomdlg32",
    "-lgdiplus",
    "-o",
    $Output
)

& $gpp @args
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE"
}

Write-Host "Built $Output"
