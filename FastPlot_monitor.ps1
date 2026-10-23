#---------------------------------------------------------------------
# FastPlot_monitor.ps1
# Shows what a FastPlot run is doing.  The macro starts this window
# (without waiting for it) and reports through a progress file that
# both the macro and the engine append to.  12d is busy while it plots;
# the engine then writes the PDF while 12d is free, and ends the run
# with FASTPLOT_DONE.  Closes itself when the run ends.
#---------------------------------------------------------------------
param(
    [Parameter(Mandatory = $true)][string]$Progress
)

try { $host.UI.RawUI.WindowTitle = '12d FastPlot - progress' } catch { }
Write-Host '12d FastPlot is plotting.' -ForegroundColor Cyan
Write-Host '12d is busy while it plots: Windows may show it as Not Responding.' -ForegroundColor Cyan
Write-Host 'That is expected - do not close 12d. It is free again once the engine' -ForegroundColor Cyan
Write-Host 'starts writing the PDF. Leave the project alone until this window says Done.' -ForegroundColor Cyan
Write-Host ''

$pos = [long]0
$partial = ''
$done = $false
$idle = [Diagnostics.Stopwatch]::StartNew()
$clock = [Diagnostics.Stopwatch]::StartNew()

while (-not $done) {
    if (Test-Path -LiteralPath $Progress) {
        try {
            $fs = [IO.File]::Open($Progress, 'Open', 'Read', 'ReadWrite')
            try {
                if ($fs.Length -gt $pos) {
                    [void]$fs.Seek($pos, 'Begin')
                    $buf = New-Object byte[] ($fs.Length - $pos)
                    $n = $fs.Read($buf, 0, $buf.Length)
                    $pos += $n
                    $text = $partial + [Text.Encoding]::UTF8.GetString($buf, 0, $n)
                    $lines = $text -split "`r?`n"
                    $partial = $lines[$lines.Count - 1]
                    for ($i = 0; $i -lt $lines.Count - 1; $i++) {
                        $l = $lines[$i].TrimStart([char]0xFEFF)
                        if ($l -eq 'FASTPLOT_DONE') { $done = $true; continue }
                        if ($l.Length -eq 0) { continue }
                        $stamp = '{0,4:0}s  ' -f $clock.Elapsed.TotalSeconds
                        if ($l -match 'FAILED|ERROR|Cannot|NOT ADDED') { Write-Host ($stamp + $l) -ForegroundColor Red }
                        elseif ($l -match 'WARN|SKIPPED|Untick') { Write-Host ($stamp + $l) -ForegroundColor Yellow }
                        elseif ($l -match '^Wrote|^Done') { Write-Host ($stamp + $l) -ForegroundColor Green }
                        else { Write-Host ($stamp + $l) }
                    }
                    $idle.Restart()
                }
            }
            finally { $fs.Close() }
        }
        catch { }
    }
    # a run that stops reporting for half an hour has gone wrong somewhere else
    if ($idle.Elapsed.TotalMinutes -gt 30) { break }
    Start-Sleep -Milliseconds 300
}

Write-Host ''
Write-Host 'Finished. This window closes in 15 seconds.' -ForegroundColor Cyan
Start-Sleep -Seconds 15
