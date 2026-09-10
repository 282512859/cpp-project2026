param(
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'
$venv = Join-Path $ProjectRoot 'out\document-converter-venv'
$python = Join-Path $venv 'Scripts\python.exe'
$requirements = Join-Path $PSScriptRoot 'document_converter_requirements.txt'
$entryPoint = Join-Path $PSScriptRoot 'document_converter.py'
$output = Join-Path $ProjectRoot 'out\tools'
$work = Join-Path $ProjectRoot 'out\document-converter-build'
$converterExe = Join-Path $output 'document_converter.exe'
$licenseFile = Join-Path $output 'document_converter_licenses.txt'

if ((Test-Path $converterExe -PathType Leaf) -and
    (Test-Path $licenseFile -PathType Leaf)) {
    $builtAt = (Get-Item $converterExe).LastWriteTimeUtc
    $inputs = @($entryPoint, $requirements) | ForEach-Object { (Get-Item $_).LastWriteTimeUtc }
    if (($inputs | Measure-Object -Maximum).Maximum -le $builtAt) {
        Write-Host "Document converter is up to date: $converterExe"
        exit 0
    }
}

if (-not (Test-Path $python -PathType Leaf)) {
    $basePython = $null
    $pathPython = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($pathPython) {
        & $pathPython.Source -c "import sys; raise SystemExit(0 if sys.version_info >= (3,10) else 1)"
        if ($LASTEXITCODE -eq 0) { $basePython = $pathPython.Source }
    }
    if ($basePython) {
        & $basePython -m venv $venv
    } else {
        $created = $false
        foreach ($tag in @('3.13','3.12','3.11','3.10')) {
            & py -$tag -m venv $venv 2>$null
            if ($LASTEXITCODE -eq 0) { $created = $true; break }
        }
        if (-not $created) { throw 'Python 3.10+ is required to build document_converter.exe.' }
    }
    if (-not (Test-Path $python -PathType Leaf)) { throw 'Unable to create the converter Python environment.' }
}

$installed = $false
for ($attempt = 1; $attempt -le 3; $attempt++) {
    & $python -m pip install --disable-pip-version-check -r $requirements
    if ($LASTEXITCODE -eq 0) {
        $installed = $true
        break
    }
    Write-Host "Converter dependency installation failed (attempt $attempt of 3)."
}
if (-not $installed) { throw 'Unable to install converter dependencies after three attempts.' }

New-Item -ItemType Directory -Force -Path $output | Out-Null
& $python -m PyInstaller --noconfirm --clean --onefile --noupx `
    --name document_converter `
    --distpath $output `
    --workpath (Join-Path $work 'work') `
    --specpath (Join-Path $work 'spec') `
    --collect-all markitdown `
    --collect-all magika `
    --collect-all mammoth `
    --collect-all pypdfium2 `
    --collect-all imageio_ffmpeg `
    $entryPoint
if ($LASTEXITCODE -ne 0) { throw 'Unable to package document_converter.exe.' }

& $python -m piplicenses --format=plain-vertical `
    --with-license-file `
    --output-file $licenseFile
if ($LASTEXITCODE -ne 0) { throw 'Unable to generate converter license notices.' }

Write-Host "Document converter built: $converterExe"
