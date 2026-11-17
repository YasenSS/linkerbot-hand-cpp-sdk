param(
    [Parameter(Mandatory)][ValidateSet('x64', 'ARM64')][string]$Architecture
)
$ErrorActionPreference = 'Stop'
$NativeArch = if ($Architecture -eq 'ARM64') { 'arm64' } else { 'x64' }
$Expected = if ($Architecture -eq 'ARM64') { 'Arm64' } else { 'X64' }
if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString() -ne $Expected) {
    throw 'Native runner architecture mismatch'
}

# Ninja does not select a VS toolset with -A/-T. Import the same native v143
# 14.44 environment explicitly, including the SDK include and library paths.
$VsWhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$Installation = & $VsWhere -latest -products '*' -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $Installation) { throw 'Visual Studio discovery failed' }
$DevCmd = Join-Path $Installation 'Common7/Tools/VsDevCmd.bat'
# Pass cmd.exe its raw command line. PowerShell's native argument conversion
# otherwise changes the nested quotes around a batch path containing spaces.
$StartInfo = [System.Diagnostics.ProcessStartInfo]::new()
$StartInfo.FileName = $env:COMSPEC
$StartInfo.Arguments = "/d /s /c `"`"$DevCmd`" -no_logo -arch=$NativeArch -host_arch=$NativeArch -vcvars_ver=14.44 && set`""
$StartInfo.UseShellExecute = $false
$StartInfo.RedirectStandardOutput = $true
$StartInfo.RedirectStandardError = $true
$Process = [System.Diagnostics.Process]::Start($StartInfo)
$OutputTask = $Process.StandardOutput.ReadToEndAsync()
$ErrorTask = $Process.StandardError.ReadToEndAsync()
$Process.WaitForExit()
if ($Process.ExitCode -ne 0) {
    throw "MSVC v143 14.44 environment initialization failed: $($ErrorTask.GetAwaiter().GetResult())"
}
$Environment = $OutputTask.GetAwaiter().GetResult() -split '\r?\n'
$Names = @('PATH', 'INCLUDE', 'LIB', 'LIBPATH', 'VCToolsVersion', 'VCToolsInstallDir',
    'WindowsSdkDir', 'WindowsSDKVersion', 'WindowsSdkBinPath', 'UniversalCRTSdkDir',
    'UCRTVersion', 'VSINSTALLDIR', 'VCINSTALLDIR', 'VSCMD_ARG_TGT_ARCH', 'VSCMD_ARG_HOST_ARCH')
foreach ($Line in $Environment) {
    $Pair = $Line -split '=', 2
    if ($Pair.Count -eq 2 -and $Names -contains $Pair[0]) {
        [Environment]::SetEnvironmentVariable($Pair[0], $Pair[1], 'Process')
        "$($Pair[0])=$($Pair[1])" >> $env:GITHUB_ENV
    }
}
if ($env:VCToolsVersion -notlike '14.44.*' -or $env:VSCMD_ARG_TGT_ARCH -ne $NativeArch -or
    $env:VSCMD_ARG_HOST_ARCH -ne $NativeArch) { throw 'Unexpected MSVC toolset or architecture' }
$CompilerHash = (Get-FileHash (Get-Command cl.exe).Source -Algorithm SHA256).Hash.ToLowerInvariant()
"toolchain=$($env:VCToolsVersion)-$($env:WindowsSDKVersion.TrimEnd('\'))-$CompilerHash" >> $env:GITHUB_OUTPUT
"SCCACHE_DIR=$env:RUNNER_TEMP/sccache" >> $env:GITHUB_ENV

# Use a pinned native Ninja binary on both runner architectures.
$NinjaVersion = '1.13.2'
if ($Architecture -eq 'ARM64') {
    $Archive = 'ninja-winarm64.zip'
    $Checksum = 'e52f0bdef9dfb1003229dbd6508a508c4073fd017247002adc66e5e806cb0391'
} else {
    $Archive = 'ninja-win.zip'
    $Checksum = '07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65'
}
$Download = Join-Path $env:RUNNER_TEMP $Archive
Invoke-WebRequest "https://github.com/ninja-build/ninja/releases/download/v$NinjaVersion/$Archive" `
    -OutFile $Download -MaximumRetryCount 5 -RetryIntervalSec 5 -TimeoutSec 120
if ((Get-FileHash $Download -Algorithm SHA256).Hash -ne $Checksum) { throw 'Ninja SHA256 mismatch' }
$Destination = Join-Path $env:RUNNER_TEMP 'linkerbot-ninja'
Expand-Archive $Download -DestinationPath $Destination -Force
"$Destination" >> $env:GITHUB_PATH
& "$Destination/ninja.exe" --version
if ($LASTEXITCODE -ne 0) { throw 'Ninja execution failed' }
