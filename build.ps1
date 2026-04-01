param(
    [string]$Output = "duosort.exe"
)

$ErrorActionPreference = "Stop"

$sources = Get-ChildItem -Path "src" -Filter "*.cpp" | ForEach-Object { $_.FullName }
if (-not $sources -or $sources.Count -eq 0) {
    throw "No C++ source files found in src/."
}

# -mwindows builds a GUI subsystem app so Windows does not open a console window.
$args = @(
    "-std=c++17",
    "-O2",
    "-Wall",
    "-Wextra",
    "-mwindows",
    "-Iinclude"
) + $sources + @(
    "-lole32",
    "-lshell32",
    "-o",
    $Output
)

& "C:\msys64\mingw64\bin\g++.exe" @args
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE"
}

Write-Host "Built $Output"
