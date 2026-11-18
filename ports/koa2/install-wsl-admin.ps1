# Run once from an elevated PowerShell. No automatic reboot or distro deletion.
$ErrorActionPreference = 'Stop'
$taskIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not ([Security.Principal.WindowsPrincipal]::new($taskIdentity)).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Open PowerShell as administrator, then run this script again.'
}
$taskMsi = 'E:\Readom-Downloads\wsl.3.0.5.0.x64.msi'
if (-not (Test-Path -LiteralPath $taskMsi)) { throw "Missing official installer: $taskMsi" }
$taskSignature = Get-AuthenticodeSignature -LiteralPath $taskMsi
if ($taskSignature.Status -ne 'Valid' -or
    $taskSignature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation(?:,|$)') {
    throw 'Installer must have a valid Microsoft signature.'
}
$taskInstall = Start-Process msiexec.exe -ArgumentList @('/i', $taskMsi, '/passive', '/norestart', '/L*v', 'E:\Readom-Downloads\wsl-install.log') -Wait -PassThru -WindowStyle Hidden
if ($taskInstall.ExitCode -notin 0,3010) { throw "MSI failed ($($taskInstall.ExitCode)); see E:\Readom-Downloads\wsl-install.log" }
& dism.exe /online /enable-feature /featurename:VirtualMachinePlatform /all /norestart
if ($LASTEXITCODE -notin 0,3010) { throw "VirtualMachinePlatform enable failed: $LASTEXITCODE" }
Write-Host 'WSL offline installation completed. Restart Windows, then return to Codex.'
Write-Host 'Do not install a default Ubuntu distro; Readom-KOA2 will be created on E: separately.'
