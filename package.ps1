param(
    [ValidatePattern('^\d+\.\d+\.\d+(-[A-Za-z0-9.-]+)?$')]
    [string]$Version = '1.2.0'
)
$ErrorActionPreference = 'Stop'
$package = Join-Path $PSScriptRoot 'outputs\STLQuickView'
$required = @(
    'STLQuickView.exe', 'msvcp110.dll', 'msvcr110.dll',
    'osg130-osg.dll', 'osg130-osgDB.dll', 'osg130-osgViewer.dll',
    'osg130-osgGA.dll', 'osg130-osgUtil.dll', 'osg130-osgFX.dll',
    'osg130-osgText.dll', 'ot20-OpenThreads.dll', 'libpng16.dll', 'zlib.dll',
    'osgPlugins-3.4.0\osgdb_obj.dll', 'osgPlugins-3.4.0\osgdb_stl.dll',
    'osgPlugins-3.4.0\osgdb_ply.dll',
    'osgPlugins-3.4.0\osgdb_png.dll', 'osgPlugins-3.4.0\osgdb_jpeg.dll',
    'osgPlugins-3.4.0\osgdb_bmp.dll', 'osgPlugins-3.4.0\osgdb_tga.dll',
    'assets\STLQuickView.ico', 'assets\STLQuickView.png',
    'licenses\OpenSceneGraph-LICENSE.txt', 'licenses\libpng-LICENSE.txt',
    'licenses\zlib-LICENSE.txt', 'licenses\IJG-README.txt',
    'licenses\PLY-NOTICES.txt', 'licenses\LGPL-2.1.txt',
    'THIRD_PARTY_NOTICES.md', '使用说明.txt'
)
foreach ($relative in $required) {
    if (!(Test-Path -LiteralPath (Join-Path $package $relative) -PathType Leaf)) {
        throw "Missing package file: $relative. Run source\build.ps1 first."
    }
}
$dist = Join-Path $PSScriptRoot 'dist'
New-Item -ItemType Directory -Path $dist -Force | Out-Null
$archive = Join-Path $dist "PrismView-v$Version-windows-x64.zip"
if (Test-Path -LiteralPath $archive) { throw "Release package already exists: $archive" }
$staging = Join-Path $PSScriptRoot ('work\package-' + [Guid]::NewGuid().ToString('N') + '\PrismView')
New-Item -ItemType Directory -Path $staging -Force | Out-Null
foreach ($relative in $required) {
    $destination = Join-Path $staging $relative
    if ($relative -eq 'STLQuickView.exe') { $destination = Join-Path $staging 'PrismView.exe' }
    New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $package $relative) -Destination $destination
}
Compress-Archive -LiteralPath $staging -DestinationPath $archive -CompressionLevel Optimal
$checksum = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText($archive + '.sha256', $checksum + '  ' + [IO.Path]::GetFileName($archive) + "`n", [Text.Encoding]::ASCII)
Write-Output "Package: $archive"
Write-Output "SHA256: $checksum"
