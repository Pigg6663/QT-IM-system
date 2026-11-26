# ============================================================
#  离线消息测试：对方不在线时落库、登录后补发、已读不重发、在线时直投
#  用法：先启动 Server.exe（监听 127.0.0.1:5000），再运行本脚本
#  依赖：本机 MySQL 客户端（用于校验 message 表落库与已读状态）
#
#  协议约定：
#    CHAT_REQUEST  caData = [发送者, 接收者]，caMsg = 消息内容（无应答包）
#    CHAT_RESEND   在线直投，caData/caMsg 同上
#    OFFLINE_MSG_RESEND  登录后补发，caData = [发送者, 接收者]，caMsg = 内容
#
#  投递语义：服务端"先查未读 -> 逐条下发 -> 再标记已读"，
#  若在中间崩溃会重复投递一次（宁可重复，不可丢失）。
# ============================================================
param(
    [string]$ServerIp = "127.0.0.1",
    [int]$Port = 5000,
    [string]$MySqlExe = "D:\mySql\MySQL Server 5.7\bin\mysql.exe",
    [string]$MySqlUser = "root",
    [string]$MySqlPwd = "123456",
    [string]$MySqlDb = "mydb2501"
)

$ErrorActionPreference = "Stop"
$script:pass = 0
$script:fail = 0

# 协议消息类型（与 protocol.h 枚举数值一致）
$REGIST_REQUEST = 1;  $REGIST_RESPOND = 2
$LOGIN_REQUEST  = 3;  $LOGIN_RESPOND  = 4
$CHAT_REQUEST   = 18; $CHAT_RESEND    = 20
$OFFLINE_MSG_RESEND = 40

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
    $totalLen   = [BitConverter]::ToUInt32($hdr, 0)
    $type       = [BitConverter]::ToUInt32($hdr, 8)
    $resultBool = ($hdr[12] -ne 0)
    $data1 = [System.Text.Encoding]::UTF8.GetString($hdr, 12, 32).TrimEnd([char]0)
    $data2 = [System.Text.Encoding]::UTF8.GetString($hdr, 44, 32).TrimEnd([char]0)
    $rest = [int64]$totalLen - 76
    $body = @()
    if ($rest -gt 0) {
        $body = New-Object byte[] $rest
        $g = 0
        while ($g -lt $rest) {
            $n = $s.Read($body, $g, $rest - $g)
            if ($n -le 0) { break }
            $g += $n
        }
    }
    $content = ""
    if ($rest -gt 0) {
        $content = [System.Text.Encoding]::UTF8.GetString($body).TrimEnd([char]0)
    }
    return [pscustomobject]@{
        Type = $type; MsgLen = $rest; Body = $body
        ResultBool = $resultBool; Data1 = $data1; Data2 = $data2; Content = $content
    }
}

function Try-Read-Pdu {
    param($Client, [int]$TimeoutMs = 1500)
    try { return Read-Pdu -Client $Client -TimeoutMs $TimeoutMs } catch { return $null }
}

function Test-Result {
    param([string]$Name, [bool]$Ok, [string]$Detail = "")
    if ($Ok) { $script:pass++; Write-Host ("  [PASS] {0} {1}" -f $Name, $Detail) -ForegroundColor Green }
    else     { $script:fail++; Write-Host ("  [FAIL] {0} {1}" -f $Name, $Detail) -ForegroundColor Red }
}

function Sql {
    param([string]$Query)
    $old = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $raw = & $MySqlExe "-u$MySqlUser" "-p$MySqlPwd" $MySqlDb -N -B -e $Query 2>&1
    } finally { $ErrorActionPreference = $old }
    $lines = New-Object System.Collections.Generic.List[string]
    foreach ($l in $raw) {
        $s = [string]$l
        if ($s -notmatch "Using a password") { $lines.Add($s) }
    }
    return ,$lines.ToArray()
}

function SqlScalar {
    param([string]$Query)
    $rows = Sql $Query
    if ($rows.Count -eq 0) { return "" }
    return [string]$rows[0]
}

function Reg-Or-Login {
    param([string]$User, [string]$Pwd, [bool]$Login = $true)
    $c = Connect-Server
    $type = if ($Login) { $LOGIN_REQUEST } else { $REGIST_REQUEST }
    $c.GetStream().Write((New-Pdu -Type $type -Data1 $User -Data2 $Pwd), 0, 76)
    $r = Read-Pdu -Client $c
    return [pscustomobject]@{ Client = $c; Result = $r }
}

$suffix = [DateTime]::Now.ToString("HHmmss")
$rand   = Get-Random -Maximum 9999
$userA  = "offA_${suffix}_$rand"
$userB  = "offB_${suffix}_$rand"
$pwd    = "pwd123"
$msg1   = "offline-hello-$suffix"
$msg2   = "online-hello-$suffix"

Write-Host "`n=== 离线消息测试 ($ServerIp`:$Port) ===" -ForegroundColor Cyan
Write-Host "测试账号: $userA -> $userB`n" -ForegroundColor DarkGray

# ---------- 准备账号 ----------
$regA = Reg-Or-Login -User $userA -Pwd $pwd -Login $false
$regA.Client.Close()
$regB = Reg-Or-Login -User $userB -Pwd $pwd -Login $false
$regB.Client.Close()

# ---------- 1. B 离线时发消息，应落库 ----------
Write-Host "[1] B 不在线，A 发消息 -> 应落库为未读" -ForegroundColor Yellow
$sessA = Reg-Or-Login -User $userA -Pwd $pwd
Test-Result "A 登录成功" ($sessA.Result.Type -eq $LOGIN_RESPOND -and $sessA.Result.ResultBool)

$msgBytes = [System.Text.Encoding]::UTF8.GetBytes($msg1)
$chatPdu = New-Pdu -Type $CHAT_REQUEST -Data1 $userA -Data2 $userB -Msg $msgBytes
$sessA.Client.GetStream().Write($chatPdu, 0, $chatPdu.Length)
Start-Sleep -Milliseconds 600

$cnt = [int](SqlScalar "select count(*) from message where receiver='$userB' and content='$msg1'")
Test-Result "消息已写入 message 表" ($cnt -eq 1) "(count=$cnt)"
$unread = [int](SqlScalar "select count(*) from message where receiver='$userB' and is_read=0")
Test-Result "is_read 仍为 0（未投递）" ($unread -ge 1) "(unread=$unread)"
Test-Result "B 仍未登录（online=0）" ([int](SqlScalar "select online from user_info where name='$userB'") -eq 0)

# ---------- 2. B 登录，应先收到 LOGIN_RESPOND 再收到补发 ----------
Write-Host "`n[2] B 登录 -> 应先收到登录应答，再收到离线补发" -ForegroundColor Yellow
$sessB = Connect-Server
$sessB.GetStream().Write((New-Pdu -Type $LOGIN_REQUEST -Data1 $userB -Data2 $pwd), 0, 76)
$loginResp = Read-Pdu -Client $sessB
Test-Result "第一条是 LOGIN_RESPOND" ($loginResp.Type -eq $LOGIN_RESPOND -and $loginResp.ResultBool) "(type=$($loginResp.Type))"

$offResp = Try-Read-Pdu -Client $sessB -TimeoutMs 3000
Test-Result "随后收到离线补发" ($null -ne $offResp -and $offResp.Type -eq $OFFLINE_MSG_RESEND) `
    "(type=$(if ($offResp) { $offResp.Type } else { 'null' }))"
Test-Result "补发的发送者是 A" ($null -ne $offResp -and $offResp.Data1 -eq $userA) `
    "(sender=$(if ($offResp) { $offResp.Data1 } else { '' }))"
Test-Result "补发内容是原始消息" ($null -ne $offResp -and $offResp.Content -eq $msg1) `
    "(content=$(if ($offResp) { $offResp.Content } else { '' }))"
Test-Result "补发只发一条" ($null -eq (Try-Read-Pdu -Client $sessB -TimeoutMs 1200))

$readFlag = [int](SqlScalar "select is_read from message where receiver='$userB' and content='$msg1'")
Test-Result "补发后 is_read 置 1" ($readFlag -eq 1) "(is_read=$readFlag)"

# ---------- 3. 再次登录不应重复补发 ----------
Write-Host "`n[3] B 重新登录 -> 已读消息不应重复补发" -ForegroundColor Yellow
$sessB.Close()
Start-Sleep -Milliseconds 400
$sessB2 = Connect-Server
$sessB2.GetStream().Write((New-Pdu -Type $LOGIN_REQUEST -Data1 $userB -Data2 $pwd), 0, 76)
$loginResp2 = Read-Pdu -Client $sessB2
Test-Result "重新登录成功" ($loginResp2.Type -eq $LOGIN_RESPOND -and $loginResp2.ResultBool)
Test-Result "没有重复补发" ($null -eq (Try-Read-Pdu -Client $sessB2 -TimeoutMs 1500))

# ---------- 4. 双方在线时直投，不落库 ----------
Write-Host "`n[4] 双方在线 -> 应直投 CHAT_RESEND，且不落库" -ForegroundColor Yellow
$msg2Bytes = [System.Text.Encoding]::UTF8.GetBytes($msg2)
$chatPdu2 = New-Pdu -Type $CHAT_REQUEST -Data1 $userA -Data2 $userB -Msg $msg2Bytes
$sessA.Client.GetStream().Write($chatPdu2, 0, $chatPdu2.Length)

$direct = Try-Read-Pdu -Client $sessB2 -TimeoutMs 3000
Test-Result "B 收到 CHAT_RESEND" ($null -ne $direct -and $direct.Type -eq $CHAT_RESEND) `
    "(type=$(if ($direct) { $direct.Type } else { 'null' }))"
Test-Result "直投内容正确" ($null -ne $direct -and $direct.Content -eq $msg2)
$cnt2 = [int](SqlScalar "select count(*) from message where receiver='$userB' and content='$msg2'")
Test-Result "在线投递不落库" ($cnt2 -eq 0) "(count=$cnt2)"

# ---------- 5. 非法/越权：冒名发送不应落库 ----------
Write-Host "`n[5] 冒名发送 -> 应被丢弃且不落库" -ForegroundColor Yellow
$fakeMsg = "fake-$suffix"
$fakeBytes = [System.Text.Encoding]::UTF8.GetBytes($fakeMsg)
# A 的会话里把发送者写成 userB，属于冒充
$fakePdu = New-Pdu -Type $CHAT_REQUEST -Data1 $userB -Data2 $userB -Msg $fakeBytes
$sessA.Client.GetStream().Write($fakePdu, 0, $fakePdu.Length)
Start-Sleep -Milliseconds 600
$cnt3 = [int](SqlScalar "select count(*) from message where content='$fakeMsg'")
Test-Result "冒名消息未落库" ($cnt3 -eq 0) "(count=$cnt3)"
Test-Result "B 也没收到冒名消息" ($null -eq (Try-Read-Pdu -Client $sessB2 -TimeoutMs 1200))

$sessA.Client.Close()
$sessB2.Close()

Write-Host "`n=== 结果: $script:pass 通过 / $script:fail 失败 ===" -ForegroundColor Cyan
if ($script:fail -gt 0) { exit 1 } else { exit 0 }
