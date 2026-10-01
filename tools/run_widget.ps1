# Launch the DeskBuddy widget pointed at the mock server.
# Usage: run_widget.ps1
#
# The widget reads its server list from a JSON config file. This helper writes
# a mock config (one server at 127.0.0.1:11445) and points DESKBUDDY_CONFIG at
# it, so no recompile is needed. See widget/WINDOWS_WIDGET_SPEC.md §14.
$wd = Join-Path $PSScriptRoot "..\widget"
$wd = (Resolve-Path $wd).Path
$cfg = Join-Path $wd "mock_config.json"
@"
{
  "servers": [
    { "host": "127.0.0.1:11445", "label": "mock" }
  ],
  "poll": { "working_ms": 500, "idle_ms": 2000, "error_ms": 5000 }
}
"@ | Set-Content -Encoding UTF8 $cfg
$env:DESKBUDDY_CONFIG = $cfg
Start-Process -WindowStyle Hidden -WorkingDirectory $wd `
  -FilePath "$wd\target\debug\deskbuddy_widget.exe" `
  -RedirectStandardError "$wd\widget_stderr.log"
Write-Output "widget launched -> config at $cfg (mock 127.0.0.1:11445)"
