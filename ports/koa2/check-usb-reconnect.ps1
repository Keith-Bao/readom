# SPDX-License-Identifier: Apache-2.0
param(
    [Parameter(Mandatory)]
    [string] $ReferenceReport
)

$ErrorActionPreference = 'Stop'
$reference = Get-Item -LiteralPath $ReferenceReport
if ($reference.PSIsContainer -or $reference.Name -ne 'report.txt' -or
    $reference.Directory.Name -notmatch '^[0-9]{8}-[0-9]{6}-[0-9]+$') {
    throw 'Supply the locally backed-up report.txt inside its original run directory.'
}

$expected = (Get-FileHash -LiteralPath $reference.FullName -Algorithm SHA256).Hash
$relative = "koa2-quiesce-dryrun\$($reference.Directory.Name)\report.txt"
$verified = @(
    foreach ($drive in [System.IO.DriveInfo]::GetDrives()) {
        if (-not $drive.IsReady -or $drive.DriveType -ne 'Removable' -or $drive.VolumeLabel -ne 'Kindle') {
            continue
        }
        $deviceReport = Join-Path $drive.RootDirectory.FullName $relative
        if (Test-Path -LiteralPath $deviceReport -PathType Leaf) {
            $actual = (Get-FileHash -LiteralPath $deviceReport -Algorithm SHA256).Hash
            if ($actual -eq $expected) {
                [pscustomobject]@{ Drive = $drive.Name; ReadVerified = $true }
            }
        }
    }
)

if ($verified.Count -ne 1) {
    throw 'Expected exactly one mounted Kindle with a readable, matching device report.'
}
$verified
