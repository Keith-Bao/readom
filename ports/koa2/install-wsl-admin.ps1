# Run once from an elevated PowerShell. No automatic reboot or distro deletion.
$ErrorActionPreference = 'Stop'
$taskIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not ([Security.Principal.WindowsPrincipal]::new($taskIdentity)).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Open PowerShell as administrator, then run this script again.'
}
& wsl.exe --install --no-distribution
if ($LASTEXITCODE -ne 0) {
    throw "WSL installation did not finish successfully (exit $LASTEXITCODE). Preserve the output for diagnosis."
}
Write-Host 'WSL base installation requested. Restart Windows if requested, then return to Codex.'
Write-Host 'Do not install a default Ubuntu distro; Readom-KOA2 will be created on E: separately.'
