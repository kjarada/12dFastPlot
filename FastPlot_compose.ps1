#---------------------------------------------------------------------
# FastPlot_compose.ps1
# Runs the FastPlot PDF/raster engine (FastPlotEngine.cs) for the 12d
# FastPlot macro.  The macro carries both files and writes them into
# %TEMP%\FastPlot\engine before each run (see build_embed.ps1).
#
#   powershell -ExecutionPolicy Bypass -NoProfile -File FastPlot_compose.ps1 -Job <job.txt>
#
# The engine is compiled once per version into %TEMP%\FastPlot and
# reused; if a cached DLL cannot be loaded it is compiled in memory.
# Exit code 0 = success.  Details go to the job's result file.
#
# 12d does not wait for a plot run's engine: this script ends the run.
# It writes the outcome and FASTPLOT_DONE to the progress window and
# opens the PDF when the job asks for it (open_when_done).
#---------------------------------------------------------------------
param(
    [Parameter(Mandatory = $true)][string]$Job
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$src = Join-Path $here 'FastPlotEngine.cs'
try { $host.UI.RawUI.WindowTitle = '12d FastPlot' } catch { }

# What is needed even if the engine never starts
$resultPath = $null
$progressPath = $null
$openWhenDone = '0'
$output = ''
$outputMode = 'single'
foreach ($line in [IO.File]::ReadAllLines($Job)) {
    $f = $line -split "`t", 2
    if ($f.Count -lt 2) { continue }
    $v = $f[1].Trim()
    switch ($f[0]) {
        'result' { $resultPath = $v }
        'progress' { $progressPath = $v }
        'open_when_done' { $openWhenDone = $v }
        'output' { $output = $v }
        'output_mode' { $outputMode = $v }
    }
}

function Write-Failure([string]$msg) {
    Write-Host "FastPlot failed: $msg" -ForegroundColor Red
    $script:failure = $msg
    if ($resultPath) {
        try {
            [IO.File]::WriteAllText($resultPath, "STATUS`tERROR`t$($msg -replace '[\t\r\n]', ' ')`n", (New-Object Text.UTF8Encoding $false))
        }
        catch { Write-Host $_.Exception.Message -ForegroundColor Red }
    }
}

# One line for the progress window; 12d or the window may hold the file open
function Add-ProgressLine([string]$line) {
    if (-not $progressPath) { return }
    try {
        $fs = New-Object IO.FileStream($progressPath, [IO.FileMode]::Append, [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
        try {
            $bytes = (New-Object Text.UTF8Encoding $false).GetBytes($line + "`r`n")
            $fs.Write($bytes, 0, $bytes.Length)
        }
        finally { $fs.Close() }
    }
    catch { }
}

# The end of a plot run: its outcome to the progress window, and the PDF opened
function Complete-Run {
    if (-not $progressPath) { return }      # a probe, which 12d waits for
    $ok = $false
    $secs = ''
    $msg = ''
    $files = @()
    if ($resultPath -and (Test-Path -LiteralPath $resultPath)) {
        foreach ($l in [IO.File]::ReadAllLines($resultPath)) {
            $f = $l -split "`t"
            if ($f[0] -eq 'STATUS') {
                if ($f.Count -gt 1 -and $f[1] -eq 'OK') { $ok = $true; if ($f.Count -gt 2) { $secs = $f[2] } }
                elseif ($f.Count -gt 2) { $msg = $f[2] }
            }
            elseif ($f[0] -eq 'FILE' -and $f.Count -gt 1) { $files += $f[1] }
        }
    }
    if ($ok) {
        $inv = [Globalization.CultureInfo]::InvariantCulture
        $s = 0.0
        if ([double]::TryParse($secs, [Globalization.NumberStyles]::Float, $inv, [ref]$s)) { $secs = $s.ToString('0.0', $inv) }
        Add-ProgressLine ("Done - the engine took " + $secs + " s.")
        if ($openWhenDone -eq '1') {
            $target = $null
            if ($outputMode -eq 'per_sheet') { $target = $output }
            elseif ($files.Count -gt 0) { $target = $files[0] }
            if ($target -and (Test-Path -LiteralPath $target)) {
                Add-ProgressLine "Opening $target"
                try { Start-Process -FilePath $target } catch { Add-ProgressLine ("Cannot open the PDF: " + $_.Exception.Message) }
            }
        }
    }
    else {
        if (-not $msg) { $msg = $script:failure }
        if (-not $msg) { $msg = 'see ' + (Join-Path $env:TEMP 'FastPlot\engine.log') }
        Add-ProgressLine ("FAILED: " + ($msg -replace '[\r\n]+', ' '))
    }
    Add-ProgressLine 'FASTPLOT_DONE'
}

$failure = ''
$rc = 2
try {
    if (-not ('FastPlot.Runner' -as [type])) {
        $refs = @('System.Drawing', 'System.IO.Compression')
        $hash = (Get-FileHash -LiteralPath $src -Algorithm SHA1).Hash.Substring(0, 12)
        $cache = Join-Path $env:TEMP 'FastPlot'
        $dll = Join-Path $cache "FastPlotEngine_$hash.dll"
        $loaded = $false
        if (Test-Path -LiteralPath $dll) {
            try { Add-Type -LiteralPath $dll; $loaded = $true } catch { $loaded = $false }
        }
        if (-not $loaded) {
            $code = [IO.File]::ReadAllText($src)
            try {
                New-Item -ItemType Directory -Force -Path $cache | Out-Null
                Write-Host 'Compiling FastPlot engine (first run only)...'
                Add-Type -TypeDefinition $code -ReferencedAssemblies $refs -OutputAssembly $dll -OutputType Library
                Add-Type -LiteralPath $dll
            }
            catch {
                Add-Type -TypeDefinition $code -ReferencedAssemblies $refs
            }
        }
    }
    Write-Host "Job: $Job"
    $rc = [FastPlot.Runner]::Run($Job)
    if ($rc -ne 0) {
        Write-Host ([FastPlot.Runner]::LastLog()) -ForegroundColor Yellow
        # a job the engine could not even read leaves no result file of its own
        if ($resultPath -and -not (Test-Path -LiteralPath $resultPath)) { Write-Failure ([FastPlot.Runner]::LastLog()) }
    }
}
catch {
    Write-Failure $_.Exception.Message
    $rc = 2
}
# the progress window waits for this, whatever went wrong before
try { Complete-Run } catch { Add-ProgressLine 'FASTPLOT_DONE' }
exit $rc
