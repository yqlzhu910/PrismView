param(
    [string]$OsgRoot='D:\OSG(x64)\OSG(x64)',
    [string]$VcRoot='C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC',
    [string]$RuntimeDirectory='',
    [string]$SamplePly='',
    [switch]$Render
)
$ErrorActionPreference='Stop'
$workspace=Split-Path $PSScriptRoot -Parent
$build=Join-Path $workspace 'work\build'
$fixtures=Join-Path $workspace 'work\ply-fixtures'
if(!$RuntimeDirectory){$RuntimeDirectory=Join-Path $workspace 'outputs\STLQuickView'}
if(!(Test-Path -LiteralPath (Join-Path $build 'PlyLoader.obj'))){throw 'Build the application first.'}
& (Join-Path $PSScriptRoot 'make-ply-fixtures.ps1') -OutputDirectory $fixtures
$code=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'ply-test.cpp') -Raw -Encoding UTF8
[IO.File]::WriteAllText((Join-Path $build 'ply-test.cpp'),$code,(New-Object Text.UTF8Encoding($true)))
$command=@"
@echo off
call "$VcRoot\vcvarsall.bat" x86_amd64
if errorlevel 1 exit /b 1
set "INCLUDE=C:\Program Files (x86)\Windows Kits\8.1\Include\um;C:\Program Files (x86)\Windows Kits\8.1\Include\shared;%INCLUDE%"
set "LIB=C:\Program Files (x86)\Windows Kits\8.1\Lib\winv6.3\um\x64;%LIB%"
cd /d "$build"
cl /nologo /c /EHsc /MD /O2 /W3 /DUNICODE /D_UNICODE /I"$OsgRoot\include" ply-test.cpp
if errorlevel 1 exit /b 1
link /nologo ply-test.obj PlyLoader.obj /OUT:ply-test.exe /SUBSYSTEM:CONSOLE /LIBPATH:"$OsgRoot\lib" osgDB.lib osg.lib OpenThreads.lib
exit /b %errorlevel%
"@
[IO.File]::WriteAllText((Join-Path $build 'ply-test.cmd'),$command,[Text.Encoding]::Default)
& $env:ComSpec /d /c ('"'+(Join-Path $build 'ply-test.cmd')+'"')
if($LASTEXITCODE){throw 'PLY test build failed.'}
if($Render){
    $code=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'ply-render-test.cpp') -Raw -Encoding UTF8
    [IO.File]::WriteAllText((Join-Path $build 'ply-render-test.cpp'),$code,(New-Object Text.UTF8Encoding($true)))
    $renderCommand=$command.Replace('ply-test.cpp','ply-render-test.cpp').Replace('link /nologo ply-test.obj PlyLoader.obj','link /nologo ply-render-test.obj StlLoader.obj ObjLoader.obj PlyLoader.obj').Replace('/OUT:ply-test.exe','/OUT:ply-render-test.exe').Replace('osgDB.lib osg.lib OpenThreads.lib','osgFX.lib user32.lib gdi32.lib comdlg32.lib shell32.lib shlwapi.lib osgViewer.lib osgGA.lib osgUtil.lib osgDB.lib osg.lib opengl32.lib OpenThreads.lib')
    [IO.File]::WriteAllText((Join-Path $build 'ply-render-test.cmd'),$renderCommand,[Text.Encoding]::Default)
    & $env:ComSpec /d /c ('"'+(Join-Path $build 'ply-render-test.cmd')+'"')
    if($LASTEXITCODE){throw 'PLY render test build failed.'}
}
$previousPath=$env:PATH;$previousLibrary=$env:OSG_LIBRARY_PATH;$previousData=$env:OSG_FILE_PATH
try {
    $env:PATH=$RuntimeDirectory+';'+$env:SystemRoot+'\System32;'+$env:SystemRoot
    $env:OSG_LIBRARY_PATH=Join-Path $RuntimeDirectory 'osgPlugins-3.4.0';$env:OSG_FILE_PATH=''
    if($SamplePly){& (Join-Path $build 'ply-test.exe') $fixtures $SamplePly}
    else{& (Join-Path $build 'ply-test.exe') $fixtures}
    if($LASTEXITCODE){throw 'PLY tests failed.'}
    if($Render){
        Push-Location $build
        try {
            if($SamplePly){& .\ply-render-test.exe $fixtures $SamplePly}
            else{& .\ply-render-test.exe $fixtures}
            if($LASTEXITCODE){throw 'PLY render tests failed.'}
        } finally {Pop-Location}
    }
} finally {$env:PATH=$previousPath;$env:OSG_LIBRARY_PATH=$previousLibrary;$env:OSG_FILE_PATH=$previousData}
