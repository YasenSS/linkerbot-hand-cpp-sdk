# Run the Windows CI checks locally, without hardware or vendor drivers.
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'Both')]
    [string]$Configuration = 'Release',
    [ValidateSet('all', 'no-drivers', 'native-loader')]
    [string]$Suite = 'all',
    [string]$CommSource = (Join-Path $PSScriptRoot '../../linkerbot-transport-cpp'),
    [string]$PythonExecutable = 'python',
    [ValidateRange(1, 64)]
    [int]$Jobs = 4
)

$ErrorActionPreference = 'Stop'
$OriginalPath = $env:PATH
try {
    $Root = Split-Path $PSScriptRoot -Parent
    $CommSource = (Resolve-Path -LiteralPath $CommSource).Path
    if (!(Test-Path (Join-Path $CommSource 'CMakeLists.txt'))) {
        throw "Communication library not found at $CommSource"
    }
    $VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (!(Test-Path $VsWhere)) { throw 'Install Visual Studio 2022 with Desktop development with C++.' }
    $VsRoot = & $VsWhere -latest -version '[17.0,18.0)' -products '*' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$VsRoot) { throw 'Visual Studio 2022 C++ tools are required.' }
    # Prefer a user-provided CMake; otherwise use the copy bundled with VS.
    if (!(Get-Command cmake -ErrorAction SilentlyContinue)) {
        $CMakeBin = Join-Path $VsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'
        if (!(Test-Path (Join-Path $CMakeBin 'cmake.exe'))) { throw 'Install CMake 3.21 or newer.' }
        $env:PATH = "$CMakeBin;$env:PATH"
    }
    $Python = (Get-Command $PythonExecutable -ErrorAction Stop).Source
    & $Python -c "import sys; assert sys.platform == 'win32' and sys.version_info >= (3, 9), 'Use Windows Python 3.9+'"
    if ($LASTEXITCODE -ne 0) {
        throw 'Windows Python 3.9+ is required. Use -PythonExecutable with its full path if python resolves to a Windows Store alias.'
    }
    # Leave room for the package's toolchain-qualified root and public headers
    # on Windows hosts that still enforce the traditional MAX_PATH limit.
    $RunRoot = Join-Path $Root ('build/wci-' + [guid]::NewGuid().ToString('N').Substring(0, 12))
    $Configurations = if ($Configuration -eq 'Both') { @('Debug', 'Release') } else { @($Configuration) }
    foreach ($Config in $Configurations) {
        $Work = Join-Path $RunRoot $Config
        Write-Host "Windows x64 / $Config / $Suite; evidence: $Work/evidence"
        & $Python (Join-Path $PSScriptRoot 'run_release_validation.py') `
            --comm-source $CommSource --work-dir $Work --config $Config --suite $Suite `
            --generator 'Visual Studio 17 2022' --architecture x64 --toolset v143 `
            --allow-downloads --jobs $Jobs
        if ($LASTEXITCODE -ne 0) { throw "Validation failed; see $Work/evidence/summary.json" }
    }
    Write-Host "Windows validation passed. Evidence: $RunRoot"
} finally {
    $env:PATH = $OriginalPath
}
