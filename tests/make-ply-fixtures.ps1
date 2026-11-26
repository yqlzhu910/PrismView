param([string]$OutputDirectory = (Join-Path (Split-Path $PSScriptRoot -Parent) 'work\ply-fixtures'))
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$utf8=New-Object Text.UTF8Encoding($false)
$header=@'
ply
format FORMAT 1.0
comment synthetic regression fixture
element vertex 4
property float x
property float y
property float z
property uchar red
property uchar green
property uchar blue
property uchar alpha
property float nx
property float ny
property float nz
element face 1
property list uchar uint vertex_indices
property uint process_id
end_header
'@
$header=$header.Replace("`r`n","`n")+"`n"
$vertices=@(@(0,0,0,255,0,0),@(2,0,0,0,255,0),@(2,2,0,0,0,255),@(0,2,0,255,255,255))
$body="0 0 0 255 0 0 255 0 0 1`n2 0 0 0 255 0 255 0 0 1`n2 2 0 0 0 255 255 0 0 1`n0 2 0 255 255 255 255 0 0 1`n4 0 1 2 3 7`n"
[IO.File]::WriteAllText((Join-Path $OutputDirectory '彩色四边形.PLY'),$header.Replace('FORMAT','ascii')+$body,$utf8)
foreach($format in @('binary_little_endian','binary_big_endian')){
    $path=Join-Path $OutputDirectory ($format+'.ply')
    $writer=New-Object IO.BinaryWriter([IO.File]::Create($path))
    try {
        $writer.Write([Text.Encoding]::ASCII.GetBytes($header.Replace('FORMAT',$format)))
        foreach($v in $vertices){
            for($i=0;$i -lt 3;$i++){$bytes=[BitConverter]::GetBytes([single]$v[$i]);if($format -eq 'binary_big_endian'){[Array]::Reverse($bytes)};$writer.Write($bytes)}
            for($i=3;$i -lt 6;$i++){$writer.Write([byte]$v[$i])};$writer.Write([byte]255)
            foreach($normal in @(0,0,1)){$bytes=[BitConverter]::GetBytes([single]$normal);if($format -eq 'binary_big_endian'){[Array]::Reverse($bytes)};$writer.Write($bytes)}
        }
        $writer.Write([byte]4)
        foreach($index in @(0,1,2,3,7)){$bytes=[BitConverter]::GetBytes([uint32]$index);if($format -eq 'binary_big_endian'){[Array]::Reverse($bytes)};$writer.Write($bytes)}
    } finally {$writer.Dispose()}
}
$pointHeader="ply`nformat ascii 1.0`nelement vertex COUNT`nproperty float x`nproperty float y`nproperty float z`nproperty uchar red`nproperty uchar green`nproperty uchar blue`nelement face 0`nproperty list uchar uint vertex_indices`nend_header`n"
$points=New-Object Text.StringBuilder
for($y=0;$y -lt 3;$y++){for($x=0;$x -lt 3;$x++){ $color=@('255 0 0','0 255 0','0 0 255')[$x];[void]$points.Append("$x $y 0 $color`n") }}
[IO.File]::WriteAllText((Join-Path $OutputDirectory '点云.ply'),$pointHeader.Replace('COUNT','9')+$points.ToString(),$utf8)
$plain="ply`nformat ascii 1.0`nelement vertex 3`nproperty float x`nproperty float y`nproperty float z`nelement face 1`nproperty list uchar int vertex_indices`nend_header`n0 0 0`n2 0 0`n0 2 0`n3 0 1 2`n"
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'plain-triangle.ply'),$plain,$utf8)
$writer=New-Object IO.StreamWriter((Join-Path $OutputDirectory 'cancel-large.ply'),$false,$utf8)
try {$writer.Write($pointHeader.Replace('COUNT','200000'));$block="0 0 0 255 0 0`n"*10000;for($i=0;$i -lt 20;$i++){$writer.Write($block)}}finally{$writer.Dispose()}
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'invalid.ply'),'not a ply file',$utf8)
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'empty.ply'),'',$utf8)
Write-Output "PLY fixtures: $OutputDirectory"
