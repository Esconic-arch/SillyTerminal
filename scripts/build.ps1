param(
    [string]$QtRoot,
    [string]$MinGwRoot,
    [switch]$Package
)

$ErrorActionPreference = 'Stop'
$taskProjectRoot = Split-Path -Parent $PSScriptRoot

if (-not $QtRoot) {
    $taskCandidates = @()
    foreach ($taskInstallRoot in @('D:\Qt', 'C:\Qt', (Join-Path $env:USERPROFILE 'Qt'))) {
        if (Test-Path -LiteralPath $taskInstallRoot) {
            $taskCandidates += Get-ChildItem -LiteralPath $taskInstallRoot -Directory |
                Where-Object { $_.Name -match '^6\.\d+\.\d+$' } |
                Sort-Object { [version]$_.Name } -Descending |
                ForEach-Object { Join-Path $_.FullName 'mingw_64' } |
                Where-Object { Test-Path -LiteralPath (Join-Path $_ 'bin\qmake6.exe') }
        }
    }
    if ($taskCandidates.Count -eq 0) {
        throw 'Qt 6 MinGW was not found. Pass -QtRoot with the full path to the mingw_64 directory.'
    }
    $QtRoot = $taskCandidates[0]
}
$QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
$taskInstallRoot = Split-Path -Parent (Split-Path -Parent $QtRoot)
$taskToolsRoot = Join-Path $taskInstallRoot 'Tools'
$taskCmake = Join-Path $taskToolsRoot 'CMake_64\bin\cmake.exe'
$taskCtest = Join-Path $taskToolsRoot 'CMake_64\bin\ctest.exe'
$taskNinja = Join-Path $taskToolsRoot 'Ninja\ninja.exe'

if (-not $MinGwRoot) {
    $taskCompilers = @(Get-ChildItem -LiteralPath $taskToolsRoot -Directory -Filter 'mingw*' |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'bin\g++.exe') } |
        Sort-Object Name -Descending)
    if ($taskCompilers.Count -eq 0) { throw 'Qt MinGW compiler was not found. Pass -MinGwRoot.' }
    if ($taskCompilers.Count -gt 1) { throw 'Multiple MinGW kits found. Pass -MinGwRoot for the compiler matching this Qt kit.' }
    $MinGwRoot = $taskCompilers[0].FullName
}
$MinGwRoot = (Resolve-Path -LiteralPath $MinGwRoot).Path
$taskCompiler = Join-Path $MinGwRoot 'bin\g++.exe'
$taskDeploy = Join-Path $QtRoot 'bin\windeployqt.exe'
foreach ($taskTool in @($taskCmake, $taskCtest, $taskNinja, $taskCompiler, $taskDeploy)) {
    if (-not (Test-Path -LiteralPath $taskTool)) { throw "Missing tool: $taskTool" }
}

$taskBuildDir = Join-Path $taskProjectRoot 'build\windows'
$taskDistDir = Join-Path $taskProjectRoot 'dist\SillyTerminal'
$taskOriginalPath = $env:PATH
$taskOriginalPluginPath = $env:QT_PLUGIN_PATH
$taskOriginalPlatformPath = $env:QT_QPA_PLATFORM_PLUGIN_PATH
$taskOriginalPlatform = $env:QT_QPA_PLATFORM
try {
    # Only change this process; Anaconda and unrelated MinGW installations remain untouched.
    $env:PATH = (Join-Path $QtRoot 'bin') + ';' + (Join-Path $MinGwRoot 'bin') + ';' + (Split-Path -Parent $taskNinja) + ';' + $taskOriginalPath
    $env:QT_PLUGIN_PATH = Join-Path $QtRoot 'plugins'
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $QtRoot 'plugins\platforms'
    $env:QT_QPA_PLATFORM = 'windows'
    Write-Output "Qt: $QtRoot"
    Write-Output "Compiler: $taskCompiler"
    & $taskCmake -S $taskProjectRoot -B $taskBuildDir -G Ninja '-DCMAKE_BUILD_TYPE=Release' "-DCMAKE_PREFIX_PATH=$QtRoot" "-DCMAKE_CXX_COMPILER=$taskCompiler" "-DCMAKE_MAKE_PROGRAM=$taskNinja" '-DBUILD_TESTING=ON'
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & $taskCmake --build $taskBuildDir --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    & $taskCtest --test-dir $taskBuildDir --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
    New-Item -ItemType Directory -Path $taskDistDir -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $taskBuildDir 'SillyTerminal.exe') -Destination $taskDistDir -Force
    & $taskDeploy --release --no-translations --compiler-runtime --dir $taskDistDir (Join-Path $taskDistDir 'SillyTerminal.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed.' }
    Write-Output "Ready: $(Join-Path $taskDistDir 'SillyTerminal.exe')"

    if ($Package) {
        $taskTarget = & $taskCompiler -dumpmachine
        if ($LASTEXITCODE -ne 0 -or $taskTarget -notmatch '^x86_64-') {
            throw 'Windows playtest packaging requires the x86_64 MinGW kit.'
        }
        $taskPackageName = 'SillyTerminal-Windows-x64-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
        $taskPackageRoot = Join-Path $taskBuildDir ('packages\' + $taskPackageName)
        $taskPayload = Join-Path $taskPackageRoot 'SillyTerminal'
        # Deploy from the built executable into a new folder. Never copy an
        # existing dist/runtime directory or the developer's application settings.
        New-Item -ItemType Directory -Path $taskPayload | Out-Null
        Copy-Item -LiteralPath (Join-Path $taskBuildDir 'SillyTerminal.exe') -Destination $taskPayload
        & $taskDeploy --release --no-translations --compiler-runtime --dir $taskPayload (Join-Path $taskPayload 'SillyTerminal.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Clean package deployment failed.' }
        Copy-Item -LiteralPath (Join-Path $taskProjectRoot 'docs\windows-playtest.txt') -Destination (Join-Path $taskPayload 'README.txt')
        $taskQtLicenses = Join-Path $taskInstallRoot 'Licenses'
        if (Test-Path -LiteralPath $taskQtLicenses -PathType Container) {
            $taskLicenseDir = Join-Path $taskPayload 'licenses\Qt'
            New-Item -ItemType Directory -Path $taskLicenseDir -Force | Out-Null
            Get-ChildItem -LiteralPath $taskQtLicenses -File | ForEach-Object {
                Copy-Item -LiteralPath $_.FullName -Destination $taskLicenseDir
            }
        }
        $taskArchive = Join-Path $taskProjectRoot ('dist\' + $taskPackageName + '.zip')
        Compress-Archive -LiteralPath $taskPayload -DestinationPath $taskArchive -CompressionLevel Optimal

        # Verify the actual ZIP after extraction, with Qt and the compiler removed
        # from PATH. Keep all smoke-test data outside the distributable payload.
        $taskVerification = Join-Path $taskPackageRoot 'verification'
        Expand-Archive -LiteralPath $taskArchive -DestinationPath $taskVerification
        $taskExtracted = Join-Path $taskVerification 'SillyTerminal'
        $taskPreview = Join-Path $taskVerification 'package-check.png'
        $taskDeploymentPath = $env:PATH
        $taskDeploymentPluginPath = $env:QT_PLUGIN_PATH
        $taskDeploymentPlatformPath = $env:QT_QPA_PLATFORM_PLUGIN_PATH
        try {
            $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
            $env:QT_PLUGIN_PATH = $null
            $env:QT_QPA_PLATFORM_PLUGIN_PATH = $null
            $taskProcess = Start-Process -FilePath (Join-Path $taskExtracted 'SillyTerminal.exe') -WorkingDirectory $taskExtracted `
                -ArgumentList @('--smoke-test', ('"' + $taskPreview + '"')) -WindowStyle Hidden -PassThru
            if (-not $taskProcess.WaitForExit(35000)) {
                $taskProcess.Kill()
                throw 'Extracted playtest package check timed out.'
            }
            $taskProcess.Refresh()
            if ($taskProcess.ExitCode -ne 0) { throw "Extracted playtest package failed: $($taskProcess.ExitCode)" }
            if ((Get-Content -LiteralPath ($taskPreview + '.ok') -Raw).Trim() -ne 'UI_CHECK_OK') {
                throw 'Extracted playtest package did not confirm successful startup.'
            }
        } finally {
            $env:PATH = $taskDeploymentPath
            $env:QT_PLUGIN_PATH = $taskDeploymentPluginPath
            $env:QT_QPA_PLATFORM_PLUGIN_PATH = $taskDeploymentPlatformPath
        }
        $taskArchiveHash = (Get-FileHash -LiteralPath $taskArchive -Algorithm SHA256).Hash
        ($taskArchiveHash + '  ' + (Split-Path -Leaf $taskArchive)) | Set-Content -LiteralPath ($taskArchive + '.sha256.txt') -Encoding ASCII
        Write-Output "Playtest ZIP verified: $taskArchive"
    }
} finally {
    $env:PATH = $taskOriginalPath
    $env:QT_PLUGIN_PATH = $taskOriginalPluginPath
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $taskOriginalPlatformPath
    $env:QT_QPA_PLATFORM = $taskOriginalPlatform
}
