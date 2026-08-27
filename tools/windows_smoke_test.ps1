<# 负责人：成员1：服务端架构/组长 #>
param(
    [Parameter(Mandatory = $true)]
    [string]$PackageRoot
)

$ErrorActionPreference = 'Stop'
$serverExe = Join-Path $PackageRoot 'bin\cloud_server.exe'
$clientExe = Join-Path $PackageRoot 'bin\cloud_client.exe'
$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("LanCloudDrive-smoke-" + [guid]::NewGuid().ToString('N'))
$runtime = Join-Path $testRoot 'runtime'
$source = Join-Path $testRoot 'source.txt'
$download = Join-Path $testRoot 'download.txt'
$serverLog = Join-Path $testRoot 'server.log'
$serverError = Join-Path $testRoot 'server-error.log'
$port = Get-Random -Minimum 20000 -Maximum 45000

New-Item -ItemType Directory -Path $testRoot | Out-Null
[System.IO.File]::WriteAllText($source, "LanCloudDrive Windows smoke test`r`nUTF-8 content: 文件传输验证", [System.Text.UTF8Encoding]::new($false))

$server = $null
try {
    $server = Start-Process -FilePath $serverExe `
        -ArgumentList @($port, ('"' + $runtime + '"')) `
        -RedirectStandardOutput $serverLog `
        -RedirectStandardError $serverError `
        -PassThru -WindowStyle Hidden

    $ready = $false
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        Start-Sleep -Milliseconds 100
        try {
            $tcp = [System.Net.Sockets.TcpClient]::new()
            $tcp.Connect('127.0.0.1', $port)
            $tcp.Dispose()
            $ready = $true
            break
        } catch { }
    }
    if (-not $ready) { throw 'Server did not start within five seconds.' }

    $username = 'verify' + [guid]::NewGuid().ToString('N').Substring(0, 8)
    $commands = @"
register $username password123
login $username password123
mkdir 0 "verify-dir"
put "$source" 1 "verify.txt"
ls 1
get 2 "$download"
quit
"@
    $output = $commands | & $clientExe 127.0.0.1 $port 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) { throw "Client exited with $LASTEXITCODE.`n$output" }
    if (-not (Test-Path $download -PathType Leaf)) { throw 'Downloaded file was not created.' }
    $sourceHash = (Get-FileHash -Algorithm SHA256 $source).Hash
    $downloadHash = (Get-FileHash -Algorithm SHA256 $download).Hash
    if ($sourceHash -ne $downloadHash) { throw 'Downloaded file SHA-256 does not match the source.' }
    if ($output -notmatch 'verify\.txt') { throw "Directory listing did not contain verify.txt.`n$output" }
    Write-Host "Windows end-to-end smoke test passed. SHA256=$sourceHash"
}
finally {
    if ($null -ne $server -and -not $server.HasExited) {
        Stop-Process -Id $server.Id -Force
        $server.WaitForExit()
    }
    if (Test-Path $testRoot) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force
    }
}
