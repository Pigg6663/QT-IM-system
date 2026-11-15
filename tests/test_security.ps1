# ============================================================
#  阶段3测试：SQL 注入 / 密码加盐哈希 / 网盘路径越权 / 旧数据升级
#  用法：先启动 Server.exe（监听 127.0.0.1:5000），再运行本脚本
#  依赖：本机 MySQL 客户端（用于校验库内数据与目录副作用）
# ============================================================
param(
    [string]$ServerIp = "127.0.0.1",
    [int]$Port = 5000,
    [string]$MySqlExe = "D:\mySql\MySQL Server 5.7\bin\mysql.exe",
    [string]$MySqlUser = "root",
    [string]$MySqlPwd = "123456",
    [string]$MySqlDb = "mydb2501",
    [string]$ServerRoot = "D:\QTdm\build-console\Server"
)

$ErrorActionPreference = "Stop"
$script:pass = 0
$script:fail = 0

# 协议消息类型（与 protocol.h 枚举数值一致）
$REGIST_REQUEST = 1;  $REGIST_RESPOND = 2
$LOGIN_REQUEST  = 3;  $LOGIN_RESPOND  = 4
$MKDIR_REQUEST  = 21; $MKDIR_RESPOND  = 22
$FLUSH_FILE_REQUEST = 23; $FLUSH_FILE_RESPOND = 24

function New-Pdu {
    param([uint32]$Type, [string]$Data1 = "", [string]$Data2 = "", [byte[]]$Msg = @())
    $len = 76 + $Msg.Length
    $buf = New-Object System.Collections.Generic.List[byte]
    $buf.AddRange([BitConverter]::GetBytes([uint32]$len))
    $buf.AddRange([BitConverter]::GetBytes([uint32]$Msg.Length))
    $buf.AddRange([BitConverter]::GetBytes([uint32]$Type))
    $data = New-Object byte[] 64
    if ($Data1.Length -gt 0) {
        $b = [System.Text.Encoding]::UTF8.GetBytes($Data1)
        [Array]::Copy($b, $data, [Math]::Min($b.Length, 32))
    }
    if ($Data2.Length -gt 0) {
        $b = [System.Text.Encoding]::UTF8.GetBytes($Data2)
        [Array]::Copy($b, 0, $data, 32, [Math]::Min($b.Length, 32))
    }
    $buf.AddRange($data)
    if ($Msg.Length -gt 0) { $buf.AddRange($Msg) }
    return $buf.ToArray()
}

function New-PduWithPath {
    param([uint32]$Type, [string]$Data1 = "", [string]$Data2 = "", [string]$Path = "")
    $pathBytes = [System.Text.Encoding]::UTF8.GetBytes($Path)
    $msg = New-Object byte[] ($pathBytes.Length + 1)
    [Array]::Copy($pathBytes, $msg, $pathBytes.Length)
    return New-Pdu -Type $Type -Data1 $Data1 -Data2 $Data2 -Msg $msg
}

function Connect-Server {
    $c = New-Object System.Net.Sockets.TcpClient($ServerIp, $Port)
    $c.NoDelay = $true
    return $c
}

# 读一个完整 PDU，并把头部 caData 与 bool 结果解析出来
function Read-Pdu {
    param($Client, [int]$TimeoutMs = 3000)
    $s = $Client.GetStream()
    $s.ReadTimeout = $TimeoutMs
    $hdr = New-Object byte[] 76
    $got = 0
    while ($got -lt 76) {
        $n = $s.Read($hdr, $got, 76 - $got)
        if ($n -le 0) { throw "连接关闭(读到 $got/76)" }
        $got += $n
    }
    $totalLen = [BitConverter]::ToUInt32($hdr, 0)
    $msgLen   = [BitConverter]::ToUInt32($hdr, 4)
    $type     = [BitConverter]::ToUInt32($hdr, 8)
    # caData 位于 [12, 76)，前 4 字节就是 bool/int 结果
    $resultInt  = [BitConverter]::ToInt32($hdr, 12)
    $resultBool = ($hdr[12] -ne 0)
    $body = @()
    $rest = [int64]$totalLen - 76
    if ($rest -gt 0) {
        $body = New-Object byte[] $rest
        $g = 0
        while ($g -lt $rest) {
            $n = $s.Read($body, $g, $rest - $g)
            if ($n -le 0) { break }
            $g += $n
        }
    }
    return [pscustomobject]@{
        Type = $type; MsgLen = $msgLen; Body = $body
        ResultBool = $resultBool; ResultInt = $resultInt
    }
}

function Test-Result {
    param([string]$Name, [bool]$Ok, [string]$Detail = "")
    if ($Ok) { $script:pass++; Write-Host ("  [PASS] {0} {1}" -f $Name, $Detail) -ForegroundColor Green }
    else     { $script:fail++; Write-Host ("  [FAIL] {0} {1}" -f $Name, $Detail) -ForegroundColor Red }
}

function Sql {
    param([string]$Query)
    # mysql 会把 "Using a password on the command line" 写到 stderr，
    # 而本脚本开启了 ErrorActionPreference=Stop，必须显式容忍这条警告
    $old = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $raw = & $MySqlExe "-u$MySqlUser" "-p$MySqlPwd" $MySqlDb -N -B -e $Query 2>&1
    } finally {
        $ErrorActionPreference = $old
    }
    # 关键：单行结果时 PowerShell 会把它折叠成 string，$x[0] 会取到首字符。
    # 显式重建为 object[]，保证下标语义稳定。
    $lines = New-Object System.Collections.Generic.List[string]
    foreach ($l in $raw) {
        $s = [string]$l
        if ($s -notmatch "Using a password") { $lines.Add($s) }
    }
    return ,$lines.ToArray()
}

$suffix   = [DateTime]::Now.ToString("HHmmss")
$testUser = "sec_$suffix"
$testPwd  = "P@ssw0rd_$suffix"

Write-Host "`n=== 阶段3安全测试 ($ServerIp`:$Port)  测试账号: $testUser`n" -ForegroundColor Cyan

# ---------- 测试 1：SQL 注入登录 ----------
Write-Host "[测试1] SQL 注入：用户名写 ' or '1'='1，登录必须失败" -ForegroundColor Yellow
try {
    $c = Connect-Server
    $c.GetStream().Write((New-Pdu -Type $LOGIN_REQUEST -Data1 "' or '1'='1" -Data2 "anything"), 0, 76)
    $r = Read-Pdu -Client $c
    $c.Close()
    Test-Result "注入登录被拒绝" (($r.Type -eq $LOGIN_RESPOND) -and ($r.ResultBool -eq $false)) `
        "(type=$($r.Type), result=$($r.ResultBool))"
} catch { Test-Result "SQL 注入测试" $false $_.Exception.Message }

# ---------- 测试 2：注册 + 校验加盐哈希 ----------
Write-Host "`n[测试2] 密码哈希：注册后库内 pwd=64位hex、salt=32位hex" -ForegroundColor Yellow
$c = Connect-Server
try {
    $c.GetStream().Write((New-Pdu -Type $REGIST_REQUEST -Data1 $testUser -Data2 $testPwd), 0, 76)
    $r = Read-Pdu -Client $c
    Test-Result "注册成功" (($r.Type -eq $REGIST_RESPOND) -and $r.ResultBool) "(type=$($r.Type), result=$($r.ResultBool))"
} catch { Test-Result "注册" $false $_.Exception.Message }
$c.Close()

$row    = Sql "select pwd, salt from user_info where name='$testUser'"
$parts  = ([string]$row[0]) -split '\t'
$pwdDb  = $parts[0]
$saltDb = $parts[1]
Test-Result "pwd 为 64 位 hex"  ($pwdDb  -match '^[0-9a-f]{64}$') "(len=$($pwdDb.Length))"
Test-Result "salt 为 32 位 hex" ($saltDb -match '^[0-9a-f]{32}$') "(len=$($saltDb.Length))"
Test-Result "库内不含明文口令"  ($pwdDb -ne $testPwd -and $saltDb -ne $testPwd)

# ---------- 测试 3：错误口令被拒绝 ----------
Write-Host "`n[测试3] 登录：错误口令必须失败" -ForegroundColor Yellow
try {
    $c = Connect-Server
    $c.GetStream().Write((New-Pdu -Type $LOGIN_REQUEST -Data1 $testUser -Data2 "wrong_pwd"), 0, 76)
    $r = Read-Pdu -Client $c
    Test-Result "错误口令被拒绝" (($r.Type -eq $LOGIN_RESPOND) -and ($r.ResultBool -eq $false)) `
        "(result=$($r.ResultBool))"
} catch { Test-Result "错误口令" $false $_.Exception.Message }

# ---------- 测试 4：正确口令登录 ----------
Write-Host "`n[测试4] 登录：正确口令应成功" -ForegroundColor Yellow
try {
    $c.GetStream().Write((New-Pdu -Type $LOGIN_REQUEST -Data1 $testUser -Data2 $testPwd), 0, 76)
    $r = Read-Pdu -Client $c
    Test-Result "正确口令登录成功" (($r.Type -eq $LOGIN_RESPOND) -and $r.ResultBool) "(result=$($r.ResultBool))"
} catch { Test-Result "正确口令登录" $false $_.Exception.Message }

# 以下越权用例复用这条已登录连接（路径校验依赖 m_loginName）
$userRoot = Join-Path $ServerRoot "userdata\$testUser"
if (-not (Test-Path $userRoot)) { New-Item -ItemType Directory -Force -Path $userRoot | Out-Null }

# ---------- 测试 5：越权列目录 ----------
Write-Host "`n[测试5] 路径越权：列目录 ./userdata/<user>/../../ 必须被拒绝" -ForegroundColor Yellow
try {
    $evilPath = "./userdata/$testUser/../../"
    $pkt = New-PduWithPath -Type $FLUSH_FILE_REQUEST -Path $evilPath
    $c.GetStream().Write($pkt, 0, $pkt.Length)
    $r = Read-Pdu -Client $c
    # 正常列目录会返回 >=36 字节的 FileInfo 列表；拒绝时 caData 中 bool=false
    $denied = (($r.Type -eq $FLUSH_FILE_RESPOND) -and ($r.ResultBool -eq $false))
    Test-Result "越权列目录被拒绝" $denied "(type=$($r.Type), result=$($r.ResultBool), bodyLen=$($r.Body.Length))"
} catch { Test-Result "越权列目录" $false $_.Exception.Message }

# ---------- 测试 6：越权 mkdir（并验证确实没有创建目录）----------
Write-Host "`n[测试6] 路径越权：mkdir 到用户根之外必须被拒绝且不落盘" -ForegroundColor Yellow
try {
    $outsideDir = Join-Path $ServerRoot "userdata\evil_outside_$suffix"
    if (Test-Path $outsideDir) { Remove-Item $outsideDir -Recurse -Force }
    $evilPath = "./userdata/$testUser/../../evil_outside_$suffix"
    $pkt = New-PduWithPath -Type $MKDIR_REQUEST -Data1 "evil" -Path $evilPath
    $c.GetStream().Write($pkt, 0, $pkt.Length)
    $r = Read-Pdu -Client $c
    Test-Result "越权 mkdir 被拒绝" (($r.Type -eq $MKDIR_RESPOND) -and ($r.ResultBool -eq $false)) `
        "(result=$($r.ResultBool))"
    Test-Result "越权目录未落盘" (-not (Test-Path $outsideDir))
} catch { Test-Result "越权 mkdir" $false $_.Exception.Message }

# ---------- 测试 7：合法路径仍可用（防误杀）----------
Write-Host "`n[测试7] 回归：合法路径 mkdir 在用户目录内应成功" -ForegroundColor Yellow
try {
    $okPath = "./userdata/$testUser"
    $pkt = New-PduWithPath -Type $MKDIR_REQUEST -Data1 "normal_dir" -Path $okPath
    $c.GetStream().Write($pkt, 0, $pkt.Length)
    $r = Read-Pdu -Client $c
    Test-Result "合法 mkdir 成功" (($r.Type -eq $MKDIR_RESPOND) -and $r.ResultBool) "(result=$($r.ResultBool))"
    Test-Result "目录确实创建" (Test-Path (Join-Path $userRoot "normal_dir"))
} catch { Test-Result "合法 mkdir" $false $_.Exception.Message }

if ($c) { $c.Close() }

# ---------- 测试 8：历史明文口令自动升级 ----------
Write-Host "`n[测试8] 兼容旧数据：明文口令账号登录后应自动升级为加盐哈希" -ForegroundColor Yellow
$legacyUser = "legacy_${suffix}_$((Get-Random -Maximum 99999))"
$legacyPwd  = "old123"
Sql "delete from user_info where name='$legacyUser'" | Out-Null
Sql "insert into user_info(name, pwd, salt) values('$legacyUser', '$legacyPwd', '')" | Out-Null
$cl = Connect-Server
try {
    $cl.GetStream().Write((New-Pdu -Type $LOGIN_REQUEST -Data1 $legacyUser -Data2 $legacyPwd), 0, 76)
    $r = Read-Pdu -Client $cl
    Test-Result "旧明文账号可登录" (($r.Type -eq $LOGIN_RESPOND) -and $r.ResultBool) "(result=$($r.ResultBool))"
} catch { Test-Result "旧账号登录" $false $_.Exception.Message }
$cl.Close()
Start-Sleep -Milliseconds 300
$legacyRow  = Sql "select pwd, salt from user_info where name='$legacyUser'"
$lParts     = ([string]$legacyRow[0]) -split '\t'
$lPwd       = $lParts[0]
$lSalt      = $lParts[1]
Test-Result "登录后自动升级为哈希" (($lPwd -match '^[0-9a-f]{64}$') -and ($lSalt -match '^[0-9a-f]{32}$')) "(pwdLen=$($lPwd.Length), saltLen=$($lSalt.Length))"

Write-Host "`n=== 结果: $script:pass 通过 / $script:fail 失败 ===" -ForegroundColor Cyan
if ($script:fail -gt 0) { exit 1 } else { exit 0 }
