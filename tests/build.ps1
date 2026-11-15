# ============================================================
#  构建脚本：编译 Server / Client（失败即中断，避免误测旧二进制）
#  用法：
#    .\build.ps1              # 构建两端
#    .\build.ps1 -Target Server
#    .\build.ps1 -Target Client
# ============================================================
param(
    [ValidateSet("All", "Server", "Client")]
    [string]$Target = "All",
    [string]$Config = "release",
    [string]$BuildRoot = "D:\QTdm\build-console",
    [string]$QtBin,
    [string]$MingwBin
)

$ErrorActionPreference = "Stop"

# 本机 Qt 5.14.2 + MinGW 7.3.0
if (-not $QtBin)    { $QtBin    = "D:\Qt\5.14.2\mingw73_64\bin" }
if (-not $MingwBin) { $MingwBin = "D:\Qt\Tools\mingw730_64\bin" }

$qmake = Join-Path $QtBin "qmake.exe"
$make  = Join-Path $MingwBin "mingw32-make.exe"
if (-not (Test-Path $qmake)) { throw "找不到 qmake: $qmake" }
if (-not (Test-Path $make))  { throw "找不到 mingw32-make: $make" }

$env:Path = "$QtBin;$MingwBin;$env:Path"
$repoRoot = Split-Path -Parent $PSScriptRoot

$targets = switch ($Target) {
    "Server" { @("Server") }
    "Client" { @("Client") }
    default  { @("Server", "Client") }
}

$failed = @()
foreach ($t in $targets) {
    $pro = Join-Path $repoRoot "$t\$t.pro"
    $out = Join-Path $BuildRoot $t
    if (-not (Test-Path $pro)) { throw "找不到工程文件: $pro" }

    Write-Host "`n>>> 构建 $t  ($pro)" -ForegroundColor Cyan
    New-Item -ItemType Directory -Force -Path $out | Out-Null
    Push-Location $out
    try {
        & $qmake $pro -spec win32-g++ "CONFIG+=$Config" 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "$t 的 qmake 退出码 $LASTEXITCODE" }

        & $make -j4 2>&1 | Tee-Object -Variable makeOut | Out-Null
        if ($LASTEXITCODE -ne 0) {
            $makeOut | Select-String "error:" | Select-Object -First 20 | ForEach-Object { Write-Host $_ -ForegroundColor Red }
            throw "$t 编译失败，退出码 $LASTEXITCODE"
        }

        $exe = Join-Path $out "$Config\$t.exe"
        if (-not (Test-Path $exe)) { throw "$t 编译未产出 $exe" }
        Write-Host "    OK -> $exe  ($((Get-Item $exe).LastWriteTime))" -ForegroundColor Green
    }
    catch {
        $failed += "$t : $($_.Exception.Message)"
        Write-Host "    FAILED -> $($_.Exception.Message)" -ForegroundColor Red
    }
    finally { Pop-Location }
}

if ($failed.Count -gt 0) {
    Write-Host "`n构建失败：" -ForegroundColor Red
    $failed | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}
Write-Host "`n全部构建成功。" -ForegroundColor Green
exit 0
