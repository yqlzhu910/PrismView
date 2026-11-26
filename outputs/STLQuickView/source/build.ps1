param(
    [string]$OsgRoot = 'D:\OSG(x64)\OSG(x64)',
    [string]$VcRoot = 'C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC'
)
$ErrorActionPreference = 'Stop'
$package = Split-Path $PSScriptRoot -Parent
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$build = Join-Path $workspace 'work\build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
# VS2012 recognizes the BOM and preserves Chinese wide-string literals.
$source = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'STLQuickView.cpp') -Raw -Encoding UTF8
[IO.File]::WriteAllText((Join-Path $build 'STLQuickView.cpp'), $source, (New-Object Text.UTF8Encoding($true)))
foreach ($file in @('StlLoader.h','StlLoader.cpp','ObjLoader.cpp','PlyLoader.cpp')) {
    $text = Get-Content -LiteralPath (Join-Path $PSScriptRoot $file) -Raw -Encoding UTF8
    [IO.File]::WriteAllText((Join-Path $build $file), $text, (New-Object Text.UTF8Encoding($true)))
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'STLQuickView.rc') -Destination $build -Force
Copy-Item -LiteralPath (Join-Path $package 'assets\STLQuickView.ico') -Destination $build -Force
$compile = @"
@echo off
call "$VcRoot\vcvarsall.bat" x86_amd64
if errorlevel 1 exit /b 1
set VSLANG=1033
set "INCLUDE=C:\Program Files (x86)\Windows Kits\8.1\Include\um;C:\Program Files (x86)\Windows Kits\8.1\Include\shared;%INCLUDE%"
set "LIB=C:\Program Files (x86)\Windows Kits\8.1\Lib\winv6.3\um\x64;%LIB%"
cd /d "$build"
"C:\Program Files (x86)\Windows Kits\8.1\bin\x64\rc.exe" /nologo /fo STLQuickView.res STLQuickView.rc
if errorlevel 1 exit /b 1
cl /nologo /c /EHsc /MD /O2 /W3 /DUNICODE /D_UNICODE /I"$OsgRoot\include" STLQuickView.cpp
if errorlevel 1 exit /b 1
cl /nologo /c /EHsc /MD /O2 /W3 /DUNICODE /D_UNICODE /I"$OsgRoot\include" StlLoader.cpp
if errorlevel 1 exit /b 1
cl /nologo /c /EHsc /MD /O2 /W3 /DUNICODE /D_UNICODE /I"$OsgRoot\include" ObjLoader.cpp
if errorlevel 1 exit /b 1
cl /nologo /c /EHsc /MD /O2 /W3 /DUNICODE /D_UNICODE /I"$OsgRoot\include" PlyLoader.cpp
if errorlevel 1 exit /b 1
link /nologo STLQuickView.obj StlLoader.obj ObjLoader.obj PlyLoader.obj STLQuickView.res /OUT:"$package\STLQuickView.exe" /SUBSYSTEM:WINDOWS /LIBPATH:"$OsgRoot\lib" osgViewer.lib osgGA.lib osgFX.lib osgDB.lib osgUtil.lib osg.lib OpenThreads.lib user32.lib gdi32.lib comdlg32.lib shell32.lib shlwapi.lib opengl32.lib
exit /b %errorlevel%
"@
[IO.File]::WriteAllText((Join-Path $build 'compile.cmd'), $compile, [Text.Encoding]::Default)
& $env:ComSpec /d /c ('"' + (Join-Path $build 'compile.cmd') + '"')
if ($LASTEXITCODE -ne 0) { throw 'C++ build failed.' }
$dlls = @('osg130-osgViewer.dll','osg130-osgGA.dll','osg130-osgFX.dll','osg130-osgDB.dll','osg130-osgUtil.dll','osg130-osgText.dll','osg130-osg.dll','ot20-OpenThreads.dll','libpng16.dll','zlib.dll')
foreach ($dll in $dlls) { Copy-Item -LiteralPath (Join-Path $OsgRoot "bin\$dll") -Destination $package -Force }
$pluginFolder = Join-Path $package 'osgPlugins-3.4.0'
New-Item -ItemType Directory -Force -Path $pluginFolder | Out-Null
foreach ($plugin in @('osgdb_stl.dll','osgdb_obj.dll','osgdb_ply.dll','osgdb_png.dll','osgdb_jpeg.dll','osgdb_bmp.dll','osgdb_tga.dll')) {
    Copy-Item -LiteralPath (Join-Path $OsgRoot "bin\osgPlugins-3.4.0\$plugin") -Destination $pluginFolder -Force
}
foreach ($dll in @('msvcp110.dll','msvcr110.dll')) {
    Copy-Item -LiteralPath (Join-Path $VcRoot "redist\x64\Microsoft.VC110.CRT\$dll") -Destination $package -Force
}
Write-Output "Built: $package\STLQuickView.exe"
