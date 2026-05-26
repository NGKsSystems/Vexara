#Requires -Version 5.1
<#
.SYNOPSIS
  Diagnose Vexara hierarchical pipeline failures (Aider Worker, queue, artifacts).

.USAGE
  powershell -ExecutionPolicy Bypass -File tools\diagnose_pipeline.ps1
  powershell -ExecutionPolicy Bypass -File tools\diagnose_pipeline.ps1 -ProjectRoot "C:\path\to\project"
  powershell -ExecutionPolicy Bypass -File tools\diagnose_pipeline.ps1 -RunAiderSmokeTest
#>
param(
    [string]$ProjectRoot = "",
    [string]$VexaraConfig = "",
    [switch]$RunAiderSmokeTest,
    [int]$QueueTaskLimit = 20,
    [int]$AiderTimeoutSec = 120
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Section([string]$Title) {
    Write-Host ""
    Write-Host ("=== $Title ===") -ForegroundColor Cyan
}

function Write-Ok([string]$Message) { Write-Host "[OK] $Message" -ForegroundColor Green }
function Write-Warn([string]$Message) { Write-Host "[WARN] $Message" -ForegroundColor Yellow }
function Write-Fail([string]$Message) { Write-Host "[FAIL] $Message" -ForegroundColor Red }

function Resolve-RepoRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

function Get-VexaraConfigPath {
    param([string]$Override)
    if ($Override -and (Test-Path $Override)) { return (Resolve-Path $Override).Path }
    $default = Join-Path $env:APPDATA "NGKsSystems\Vexara\vexara.json"
    if (Test-Path $default) { return (Resolve-Path $default).Path }
    return $default
}

function Expand-AiderArgs {
    param(
        [string[]]$ArgsTemplate,
        [string]$Model,
        [string]$Prompt,
        [string]$WorkingDirectory
    )
    $expanded = @()
    foreach ($arg in $ArgsTemplate) {
        $value = $arg
        $value = $value.Replace("{model}", $Model)
        $value = $value.Replace("{prompt}", $Prompt)
        $value = $value.Replace("{cwd}", $WorkingDirectory)
        $expanded += $value
    }
    return $expanded
}

function Invoke-AiderLikeVexara {
    param(
        [string]$Program,
        [string[]]$Arguments,
        [string]$WorkingDirectory,
        [int]$TimeoutSec
    )

    Write-Host "Program:           $Program"
    Write-Host "Working directory: $WorkingDirectory"
    Write-Host "Arguments:"
    foreach ($arg in $Arguments) {
        Write-Host "  $arg"
    }

    if (-not (Test-Path $Program)) {
        Write-Fail "Executable does not exist: $Program"
        return 1
    }

    $stdoutFile = [System.IO.Path]::GetTempFileName()
    $stderrFile = [System.IO.Path]::GetTempFileName()
    try {
        $proc = Start-Process -FilePath $Program -ArgumentList $Arguments `
            -WorkingDirectory $WorkingDirectory -NoNewWindow -PassThru `
            -RedirectStandardOutput $stdoutFile -RedirectStandardError $stderrFile

        if (-not $proc.WaitForExit($TimeoutSec * 1000)) {
            try { $proc.Kill() } catch {}
            Write-Fail "Timed out after ${TimeoutSec}s (Vexara default is 600000 ms)."
            return 2
        }

        if (Test-Path $stdoutFile) {
            $stdout = Get-Content -Path $stdoutFile -Raw -ErrorAction SilentlyContinue
            if ($stdout -and $stdout.Trim()) {
                Write-Host "--- stdout ---"
                Write-Host $stdout.TrimEnd()
            }
        }
        if (Test-Path $stderrFile) {
            $stderr = Get-Content -Path $stderrFile -Raw -ErrorAction SilentlyContinue
            if ($stderr -and $stderr.Trim()) {
                Write-Host "--- stderr ---" -ForegroundColor DarkYellow
                Write-Host $stderr.TrimEnd() -ForegroundColor DarkYellow
            }
        }

        if ($proc.ExitCode -eq 0) {
            Write-Ok "Aider smoke test exited 0."
            return 0
        }

        Write-Fail "Aider smoke test exited with code $($proc.ExitCode)."
        return [int]$proc.ExitCode
    } finally {
        Remove-Item $stdoutFile, $stderrFile -ErrorAction SilentlyContinue
    }
}

function Show-QueueReport {
    param(
        [string]$QueueDbPath,
        [int]$Limit
    )

    if (-not (Test-Path $QueueDbPath)) {
        Write-Warn "No pipeline queue database at: $QueueDbPath"
        return
    }

    $python = Get-Command python -ErrorAction SilentlyContinue
    if (-not $python) {
        Write-Warn "Python not found; skipping queue.db inspection. Install Python or inspect $QueueDbPath manually."
        return
    }

    $queueScript = Join-Path $PSScriptRoot "diagnose_queue.py"
    & $python.Source $queueScript $QueueDbPath $Limit
}

function Show-ArtifactHints {
    param([string]$ArtifactsRoot)

    if (-not (Test-Path $ArtifactsRoot)) {
        Write-Warn "No artifacts folder: $ArtifactsRoot"
        return
    }

    $taskDirs = Get-ChildItem -Path $ArtifactsRoot -Directory | Sort-Object {
        [int]$_.Name
    } -Descending | Select-Object -First 8

    if (-not $taskDirs) {
        Write-Warn "Artifacts root exists but contains no task folders."
        return
    }

    Write-Host "Recent artifact task folders:"
    foreach ($dir in $taskDirs) {
        $files = (Get-ChildItem -Path $dir.FullName -File | Select-Object -ExpandProperty Name) -join ", "
        Write-Host "  task $($dir.Name): $files"
    }
}

$repoRoot = Resolve-RepoRoot
if (-not $ProjectRoot) {
    $ProjectRoot = $repoRoot
}
$ProjectRoot = (Resolve-Path $ProjectRoot).Path
$configPath = Get-VexaraConfigPath -Override $VexaraConfig
$queueDb = Join-Path $ProjectRoot ".vexara\pipeline\queue.db"
$artifactsRoot = Join-Path $ProjectRoot ".vexara\pipeline\artifacts"

Write-Section "Vexara pipeline diagnostic"
Write-Host "Repo root:     $repoRoot"
Write-Host "Project root:  $ProjectRoot"
Write-Host "Config path:   $configPath"
Write-Host "Queue DB:      $queueDb"
Write-Host "Artifacts:     $artifactsRoot"

Write-Section "Global settings (agent_execution)"
if (-not (Test-Path $configPath)) {
    Write-Fail "vexara.json not found. Open Vexara once or create Settings."
    exit 1
}

$config = Get-Content $configPath -Raw | ConvertFrom-Json
$aider = $config.agent_execution.aider
$roles = $config.agent_execution.role_backends

$program = $aider.command
if (-not $program) { $program = $aider.executable }

Write-Host "Builder role backend: $($roles.builder)"
Write-Host "Orchestrator backend:   $($roles.orchestrator)"
Write-Host "Supervisor backend:   $($roles.supervisor)"
Write-Host "Aider command:        $program"
Write-Host "Aider model:          $($aider.model)"
Write-Host "Aider args template:  $($aider.args -join ' ')"

if (-not $program -or $program -eq "aider") {
    Write-Fail "Aider command is missing or still the bare name 'aider'. Set the full path in Settings -> Aider CLI -> Executable."
} elseif (-not (Test-Path $program)) {
    Write-Fail "Configured Aider path does not exist: $program"
} else {
    Write-Ok "Aider executable exists."
}

if ($aider.args -notcontains "{prompt}" -and ($aider.args -join " ") -notmatch "\{prompt\}") {
    Write-Warn "Aider args do not include {prompt}. Worker tasks may run without a message unless --message is present."
}

Write-Section "Pipeline queue + artifacts"
Show-QueueReport -QueueDbPath $queueDb -Limit $QueueTaskLimit
Show-ArtifactHints -ArtifactsRoot $artifactsRoot

Write-Section "Aider launch (same as AiderBridge / Worker)"
$smokePrompt = "Vexara pipeline diagnostic smoke test. Reply with the word OK and exit."
$aiderArgs = @($aider.args | ForEach-Object {
    if ($_ -eq "--no-sanity-checks") { "--skip-sanity-check-repo" } else { $_ }
})
$expandedArgs = Expand-AiderArgs -ArgsTemplate $aiderArgs -Model $aider.model `
    -Prompt $smokePrompt -WorkingDirectory $ProjectRoot

if ($RunAiderSmokeTest) {
    $exitCode = Invoke-AiderLikeVexara -Program $program -Arguments $expandedArgs `
        -WorkingDirectory $ProjectRoot -TimeoutSec $AiderTimeoutSec
} else {
    Write-Host "Dry run only. Re-run with -RunAiderSmokeTest to launch Aider exactly like the Worker stage."
    Write-Host "Command that would run:"
    Write-Host "  `"$program`" $($expandedArgs -join ' ')"
    $exitCode = 0
}

Write-Section "Done"
if ($RunAiderSmokeTest -and $exitCode -ne 0) {
    Write-Fail "Diagnostic detected problems (exit $exitCode)."
    exit $exitCode
}
Write-Ok "Diagnostic completed."
exit 0
