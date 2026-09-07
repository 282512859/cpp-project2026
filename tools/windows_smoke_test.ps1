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
$docxSource = Join-Path $testRoot 'conversion-source.docx'
$markdownDownload = Join-Path $testRoot 'converted.md'
$pdfSource = Join-Path $testRoot 'conversion-source.pdf'
$pdfMarkdownDownload = Join-Path $testRoot 'converted-pdf.md'
$serverLog = Join-Path $testRoot 'server.log'
$serverError = Join-Path $testRoot 'server-error.log'
$port = Get-Random -Minimum 20000 -Maximum 45000

New-Item -ItemType Directory -Path $testRoot | Out-Null
[System.IO.File]::WriteAllText($source, "LanCloudDrive Windows smoke test`r`nUTF-8 content: 文件传输验证", [System.Text.UTF8Encoding]::new($false))
$docxRoot = Join-Path $testRoot 'docx-source'
New-Item -ItemType Directory -Path (Join-Path $docxRoot '_rels') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $docxRoot 'word') -Force | Out-Null
[System.IO.File]::WriteAllText((Join-Path $docxRoot '[Content_Types].xml'), @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
  <Default Extension="xml" ContentType="application/xml"/>
  <Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
</Types>
'@, [System.Text.UTF8Encoding]::new($false))
[System.IO.File]::WriteAllText((Join-Path $docxRoot '_rels\.rels'), @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
</Relationships>
'@, [System.Text.UTF8Encoding]::new($false))
[System.IO.File]::WriteAllText((Join-Path $docxRoot 'word\document.xml'), @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
  <w:body><w:p><w:pPr><w:pStyle w:val="Title"/></w:pPr><w:r><w:t>LanCloudDrive conversion test</w:t></w:r></w:p><w:sectPr/></w:body>
</w:document>
'@, [System.Text.UTF8Encoding]::new($false))
$docxZip = Join-Path $testRoot 'conversion-source.zip'
Compress-Archive -Path (Join-Path $docxRoot '*') -DestinationPath $docxZip
Move-Item -LiteralPath $docxZip -Destination $docxSource

$pdfObjects = @(
    '<< /Type /Catalog /Pages 2 0 R >>',
    '<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
    '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>',
    '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>',
    "<< /Length 58 >>`nstream`nBT /F1 18 Tf 72 720 Td (PDF conversion test) Tj ET`nendstream"
)
$pdf = [System.Text.StringBuilder]::new("%PDF-1.4`n")
$offsets = [System.Collections.Generic.List[int]]::new()
for ($index = 0; $index -lt $pdfObjects.Count; $index++) {
    $offsets.Add($pdf.Length)
    [void]$pdf.AppendFormat("{0} 0 obj`n{1}`nendobj`n", $index + 1, $pdfObjects[$index])
}
$xrefOffset = $pdf.Length
[void]$pdf.Append("xref`n0 6`n0000000000 65535 f `n")
foreach ($offset in $offsets) { [void]$pdf.AppendFormat("{0:D10} 00000 n `n", $offset) }
[void]$pdf.Append("trailer`n<< /Size 6 /Root 1 0 R >>`nstartxref`n$xrefOffset`n%%EOF`n")
[System.IO.File]::WriteAllBytes($pdfSource, [System.Text.Encoding]::ASCII.GetBytes($pdf.ToString()))

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
put "$docxSource" 1 "conversion-source.docx"
convert 3
put "$pdfSource" 1 "conversion-source.pdf"
convert 5 "converted-pdf.md"
ls 1
get 2 "$download"
get 4 "$markdownDownload"
get 6 "$pdfMarkdownDownload"
quit
"@
    $output = $commands | & $clientExe 127.0.0.1 $port 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) { throw "Client exited with $LASTEXITCODE.`n$output" }
    if (-not (Test-Path $download -PathType Leaf)) {
        $serverStdout = if (Test-Path $serverLog) { Get-Content -Raw $serverLog } else { '' }
        $serverStderr = if (Test-Path $serverError) { Get-Content -Raw $serverError } else { '' }
        throw "Downloaded file was not created.`nClient output:`n$output`nServer output:`n$serverStdout`nServer errors:`n$serverStderr"
    }
    $sourceHash = (Get-FileHash -Algorithm SHA256 $source).Hash
    $downloadHash = (Get-FileHash -Algorithm SHA256 $download).Hash
    if ($sourceHash -ne $downloadHash) { throw 'Downloaded file SHA-256 does not match the source.' }
    if ($output -notmatch 'verify\.txt') { throw "Directory listing did not contain verify.txt.`n$output" }
    if ($output -notmatch 'conversion-source\.md') { throw "Directory listing did not contain conversion-source.md.`n$output" }
    if (-not (Test-Path $markdownDownload -PathType Leaf)) { throw "Converted Markdown was not downloaded.`n$output" }
    $markdown = Get-Content -Raw -LiteralPath $markdownDownload
    if ($markdown -notmatch 'LanCloudDrive conversion test') {
        throw "Converted Markdown content is incorrect.`n$markdown"
    }
    if (-not (Test-Path $pdfMarkdownDownload -PathType Leaf)) { throw "PDF Markdown was not downloaded.`n$output" }
    $pdfMarkdown = Get-Content -Raw -LiteralPath $pdfMarkdownDownload
    if ($pdfMarkdown -notmatch 'PDF conversion test') {
        throw "Converted PDF Markdown content is incorrect.`n$pdfMarkdown"
    }
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
