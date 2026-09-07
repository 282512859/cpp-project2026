<# 负责人：成员1：服务端架构/组长 #>
param(
    [Parameter(Mandatory = $true)]
    [string]$PackageRoot
)

$ErrorActionPreference = 'Stop'
$server = Join-Path $PackageRoot 'bin\cloud_server.exe'
$client = Join-Path $PackageRoot 'bin\cloud_client.exe'
$converter = Join-Path $PackageRoot 'tools\document_converter.exe'
$converterLicenses = Join-Path $PackageRoot 'tools\document_converter_licenses.txt'

foreach ($file in @($server, $client)) {
    if (-not (Test-Path $file -PathType Leaf)) {
        throw "Missing executable: $file"
    }
    $bytes = [System.IO.File]::ReadAllBytes($file)
    if ($bytes.Length -lt 2 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) {
        throw "Not a Windows PE executable: $file"
    }
    $hash = (Get-FileHash -Algorithm SHA256 $file).Hash
    Write-Host "Verified $([System.IO.Path]::GetFileName($file)) SHA256=$hash"

    $dependencies = (& dumpbin /nologo /dependents $file | Out-String)
    foreach ($forbidden in @('sqlite3.dll', 'vcruntime', 'msvcp')) {
        if ($dependencies -match [regex]::Escape($forbidden)) {
            throw "Unexpected runtime dependency '$forbidden' in $file"
        }
    }
}

if (-not (Test-Path $converter -PathType Leaf)) {
    throw "Missing document converter helper: $converter"
}
$converterBytes = [System.IO.File]::ReadAllBytes($converter)
if ($converterBytes.Length -lt 2 -or $converterBytes[0] -ne 0x4d -or $converterBytes[1] -ne 0x5a) {
    throw "Not a Windows PE executable: $converter"
}
if (-not (Test-Path $converterLicenses -PathType Leaf)) {
    throw "Missing document converter license notices: $converterLicenses"
}

Write-Host 'Windows package verification passed.'
