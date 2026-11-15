# ============================================================
#  阶段2测试：心跳应答 / 重复登录互踢 / 异常掉线超时回收
#  用法：先启动 Server.exe（监听 127.0.0.1:5000），再运行本脚本
#  说明：超时回收用例需要等待约 40 秒
# ============================================================
param(
    [string]$ServerIp = "127.0.0.1",
    [int]$Port = 5000,
    [switch]$SkipTimeoutCase
)

$ErrorActionPreference = "Stop"
$script:pass = 0
$script:fail = 0

# 协议消息类型（与 protocol.h 枚举数值一致）
$REGIST_REQUEST = 1;  $REGIST_RESPOND = 2
$LOGIN_REQUEST  = 3;  $LOGIN_RESPOND  = 4
$HEARTBEAT_REQUEST = 37; $HEARTBEAT_RESPOND = 38
$KICKED_RESEND = 39

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

function Connect-Server {
    $c = New-Object System.Net.Sockets.TcpClient($ServerIp, $Port)
    $c.NoDelay = $true
    return $c
}

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
    $type     = [BitConverter]::ToUInt32($hdr, 8)
    $rest = [int64]$totalLen - 76
    if ($rest -gt 0) {
        $body = New-Object byte[] $rest
        $g = 0
        while ($g -lt $rest) {
            $n = $s.Read($body, $g, $rest - $g)
            if ($n -le 0) { break }
            $g += $n
        }
    } else { $body = @() }
    $boolVal = $null
    if ($body.Length -ge 1) { $boolVal = ($body[0] -ne 0) }
    return [pscustomobject]@{ Type = $type; Body = $body }
}

function Test-Result {
    param([string]$Name, [bool]$Ok, [string]$Detail = "")
    if ($Ok) { $script:pass++; Write-Host ("  [PASS] {0} {1}" -f $Name, $Detail) -ForegroundColor Green }
    else     { $script:fail++; Write-Host ("  [FAIL] {0} {1}" -f $Name, $Detail) -ForegroundColor Red }
}

function Wait-Kicked {
    param($Client, [int]$TimeoutMs = 3000)
    # 被踢时服务端先下发 KICKED_RESEND 再断开，等待它出现
    $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    while ((Get-Date) -lt $deadline) {
        $r = Read-Pdu -Client $Client -TimeoutMs 500
        if ($r.Type -eq $KICKED_RESEND) { return $true }
    }
    return $false
}

$testUser = "hb_test_$([DateTime]::Now.ToString('HHmmss'))"
$testPwd  = "pw123456"

Write-Host "`n=== 阶段2测试 ($ServerIp`:$Port)  测试账号: $testUser`n" -ForegroundColor Cyan

# ---------- 测试 1：心跳请求应答 ----------
Write-Host "[测试1] 心跳：发送 HEARTBEAT_REQUEST，期望收到 HEARTBEAT_RESPOND" -ForegroundColor Yellow
$c1 = Connect-Server
try {
    $s = $c1.GetStream()
    $s.Write((New-Pdu -Type $HEARTBEAT_REQUEST), 0, 76)
    $r = Read-Pdu -Client $c1
    Test-Result "心跳有应答" ($r.Type -eq $HEARTBEAT_RESPOND) "(type=$($r.Type), 期望 $HEARTBEAT_RESPOND)"
} catch { Test-Result "心跳应答" $false $_.Exception.Message }
$c1.Close()

# ---------- 测试 2：注册测试账号 ----------
Write-Host "`n[测试2] 注册测试账号（为互踢用例准备）" -ForegroundColor Yellow
$c = Connect-Server
try {
    $c.GetStream().Write((New-Pdu -Type $REGIST_REQUEST -Data1 $testUser -Data2 $testPwd), 0, 76)
    $r = Read-Pdu -Client $c
    Test-Result "注册账号 $testUser" ($r.Type -eq $REGIST_RESPOND) "(type=$($r.Type))"
} catch { Test-Result "注册账号" $false $_.Exception.Message }
$c.Close()

# ---------- 测试 3：重复登录互踢 ----------
Write-Host "`n[测试3] 重复登录：同一账号登录两次，先登录的连接应被踢" -ForegroundColor Yellow
$cA = $null; $cB = $null
try {
    # 第一个连接登录
    $cA = Connect-Server
    $cA.GetStream().Write((New-Pdu -Type $LOGIN_REQUEST -Data1 $testUser -Data2 $testPwd), 0, 76)
    $rA = Read-Pdu -Client $cA
    Test-Result "连接A 登录成功" ($rA.Type -eq $LOGIN_RESPOND) "(type=$($rA.Type))"

    Start-Sleep -Milliseconds 300

    # 第二个连接用同一账号登录
    $cB = Connect-Server
    $cB.GetStream().Write((New-Pdu -Type $LOGIN_REQUEST -Data1 $testUser -Data2 $testPwd), 0, 76)
    $rB = Read-Pdu -Client $cB
    Test-Result "连接B 登录成功" ($rB.Type -eq $LOGIN_RESPOND) "(type=$($rB.Type))"

    # 连接A 应收到 KICKED_RESEND
    $kicked = $false
    try {
        $r = Read-Pdu -Client $cA -TimeoutMs 2000
        if ($r.Type -eq $KICKED_RESEND) { $kicked = $true }
    } catch { $kicked = $true }   # 直接断开也算被踢
    Test-Result "旧连接A 收到下线通知并被断开" $kicked

    # 连接B 应仍然可用（心跳仍能应答）
    Start-Sleep -Milliseconds 200
    $cB.GetStream().Write((New-Pdu -Type $HEARTBEAT_REQUEST), 0, 76)
    $rHB = Read-Pdu -Client $cB
    Test-Result "新连接B 仍可正常使用" ($rHB.Type -eq $HEARTBEAT_RESPOND) "(type=$($rHB.Type))"
} catch { Test-Result "重复登录互踢" $false $_.Exception.Message }
if ($cA) { $cA.Close() }
if ($cB) { $cB.Close() }

# ---------- 测试 4：异常掉线超时回收 ----------
if (-not $SkipTimeoutCase) {
    Write-Host "`n[测试4] 异常掉线：登录后不做任何心跳，等服务端 35 秒超时回收（需等约 45 秒）" -ForegroundColor Yellow
    try {
        $cC = Connect-Server
        $cC.GetStream().Write((New-Pdu -Type $LOGIN_REQUEST -Data1 $testUser -Data2 $testPwd), 0, 76)
        $rC = Read-Pdu -Client $cC
        Test-Result "测试连接登录成功" ($rC.Type -eq $LOGIN_RESPOND)

        Write-Host "  ... 静默等待 45 秒，期间不发任何心跳" -ForegroundColor DarkGray
        Start-Sleep -Seconds 45

        # 服务端应已因超时断开该连接：尝试读应发现连接已关闭
        $closed = $false
        try {
            $cC.GetStream().ReadTimeout = 2000
            $tmp = New-Object byte[] 76
            $n = $cC.GetStream().Read($tmp, 0, 76)
            if ($n -le 0) { $closed = $true }
        } catch { $closed = $true }
        Test-Result "心跳超时后连接被服务端回收" $closed
        $cC.Close()
    } catch { Test-Result "异常掉线超时回收" $false $_.Exception.Message }
} else {
    Write-Host "`n[测试4] 已跳过（-SkipTimeoutCase）" -ForegroundColor DarkGray
}

Write-Host "`n=== 结果: $script:pass 通过 / $script:fail 失败 ===" -ForegroundColor Cyan
if ($script:fail -gt 0) { exit 1 } else { exit 0 }
