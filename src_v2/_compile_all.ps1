param([switch]$verbose)
$ErrorActionPreference = 'Continue'

$cl = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.50.35717\bin\HostX64\x64\cl.exe"
$inc = "C:\Users\aurel\Desktop\leekwars_generator_c\include_v2"
$src = "C:\Users\aurel\Desktop\leekwars_generator_c\src_v2"
$tmp = "$env:TEMP\lw_v2_objs"
$winsdk = "C:\Program Files (x86)\Windows Kits\10\include\10.0.26100.0"
$msvcinc = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.50.35717\include"

if (-not (Test-Path $tmp)) { New-Item -ItemType Directory -Path $tmp -Force | Out-Null }

$ok = 0
$fail = 0
$failures = @()

Get-ChildItem -Path $src -Filter "lw_*.c" | Sort-Object Name | ForEach-Object {
    $name = $_.BaseName
    $obj = "$tmp\$name.obj"
    $output = & $cl /c /nologo /W3 /TC /D_CRT_SECURE_NO_WARNINGS "/I$inc" "/I$msvcinc" "/I$winsdk\ucrt" "/I$winsdk\um" "/I$winsdk\shared" "/Fo$obj" $_.FullName 2>&1
    if ($LASTEXITCODE -eq 0) {
        $ok++
        if ($verbose) { Write-Host "OK   $name" }
    } else {
        $fail++
        $failures += $name
        Write-Host "FAIL $name"
        $output | Where-Object { $_ -match 'error C' } | Select-Object -First 5 | ForEach-Object { Write-Host "  $_" }
    }
}

Write-Host ""
Write-Host "==> $ok OK, $fail FAIL"
if ($fail -gt 0) {
    Write-Host "Failed files:"
    $failures | ForEach-Object { Write-Host "  $_" }
}
