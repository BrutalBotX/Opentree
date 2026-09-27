# Stages the deployed OpenTree runtime into a clean folder for the installer.
#
# build-msvc also holds CMake/ninja intermediates, the test binary and autogen output, none of
# which belong in an installed copy. robocopy does the filtering (its exit codes 0-7 are
# success, 8+ means a real failure).
param(
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [Parameter(Mandatory = $true)][string]$StageDir
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path (Join-Path $BuildDir 'OpenTree.exe'))) {
    Write-Error "No OpenTree.exe in $BuildDir - build the app first."
    exit 1
}

if (Test-Path $StageDir) {
    Remove-Item $StageDir -Recurse -Force
}
New-Item -ItemType Directory -Path $StageDir -Force | Out-Null

$excludeDirs = @('CMakeFiles', 'OpenTree_autogen', 'OpenTreeTests_autogen', 'Testing', '.qt')
$excludeFiles = @(
    'CMakeCache.txt', 'Makefile', 'build.ninja', 'build.ninja_deps', 'build.ninja_log',
    'cmake_install.cmake', 'CTestTestfile.cmake', 'QtDeploySupport.cmake', 'QtDeployTargets.cmake',
    'OpenTreeTests.exe', '*.obj', '*.pdb', '*.lib', '*.exp', '*.ilk', '*.idb', '*.rsp', '*.tlog', '*.log'
)

robocopy $BuildDir $StageDir /E /NFL /NDL /NJH /NJS /R:1 /W:1 /XD @excludeDirs /XF @excludeFiles | Out-Null
$code = $LASTEXITCODE
if ($code -ge 8) {
    Write-Error "robocopy failed with code $code"
    exit 1
}

# The graph needs these; catch a broken deployment here instead of after shipping.
$required = @(
    'OpenTree.exe',
    'Qt6WebEngineWidgets.dll',
    'Qt6WebEngineCore.dll',
    'QtWebEngineProcess.exe',
    'resources\qtwebengine_resources.pak',
    'translations\qtwebengine_locales\en-US.pak'
)
$missing = @()
foreach ($file in $required) {
    if (-not (Test-Path (Join-Path $StageDir $file))) {
        $missing += $file
    }
}
if ($missing.Count -gt 0) {
    Write-Error ("The staged runtime is missing files the graph needs: " + ($missing -join ', ') +
        ". Build with the MSVC + WebEngine kit (build_msvc.bat).")
    exit 1
}

Write-Host "Staged runtime in $StageDir"
exit 0
