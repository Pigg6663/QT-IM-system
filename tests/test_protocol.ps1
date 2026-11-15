# ============================================================
#  协议层测试：手工构造 PDU 字节流，验证服务端拆包状态机
#  覆盖场景：粘包 / 半包 / 非法包长
#  用法：先启动 Server.exe（监听 127.0.0.1:5000），再运行本脚本
# ============================================================
param(
    [string]$ServerIp = "127.0.0.1",
    [int]$Port = 5000
)

$ErrorActionPreference = "Stop"
$script:pass = 0
$script:fail = 0

function New-TestPdu {
    # Data64 同时接受字符串（UTF-8 编码后放入）和 64 字节数组（直接拷贝）
    param([uint32]$Type, [byte[]]$Msg = @(), [object]$Data64 = "")
    $len = 76 + $Msg.Length
    $buf = New-Object System.Collections.Generic.List[byte]
    $buf.AddRange([BitConverter]::GetBytes([uint32]$len))        # uiTotalLen
    $buf.AddRange([BitConverter]::GetBytes([uint32]$Msg.Length)) # uiMsgLen
    $buf.AddRange([BitConverter]::GetBytes([uint32]$Type))       # uiType
    $data = New-Object byte[] 64
    if ($Data64 -is [byte[]]) {
        [Array]::Copy($Data64, 0, $data, 0, [Math]::Min($Data64.Length, 64))
    } elseif ($Data64.Length -gt 0) {
        $sb = [System.Text.Encoding]::UTF8.GetBytes([string]$Data64)
        [Array]::Copy($sb, 0, $data, 0, [Math]::Min($sb.Length, 64))
    }
    $buf.AddRange($data)                                         # caData[64]
    if ($Msg.Length -gt 0) { $buf.AddRange($Msg) }               # caMsg
    return $buf.ToArray()
}

# 从流中读出一个完整应答 PDU，返回 @{ Type; MsgLen }
function Read-Pdu {
    param($Stream, [int]$TimeoutMs = 3000)
    $stream.ReadTimeout = $TimeoutMs
    $hdr = New-Object byte[] 76
    $got = 0
    while ($got -lt 76) {
        $n = $Stream.Read($hdr, $got, 76 - $got)
        if ($n -le 0) { throw "连接被关闭（读到 $got/76 字节）" }
        $got += $n
    }
    $totalLen = [BitConverter]::ToUInt32($hdr, 0)
    $msgLen   = [BitConverter]::ToUInt32($hdr, 4)
    $type     = [BitConverter]::ToUInt32($hdr, 8)
    $rest = [int64]$totalLen - 76
    if ($rest -gt 0) {
        $body = New-Object byte[] $rest
        $g = 0
        while ($g -lt $rest) {
            $n = $Stream.Read($body, $g, $rest - $g)
            if ($n -le 0) { break }
            $g += $n
        }
    }
    return [pscustomobject]@{ TotalLen = $totalLen; MsgLen = $msgLen; Type = $type }
}

function Test-Result {
    param([string]$Name, [bool]$Ok, [string]$Detail = "")
    if ($Ok) { $script:pass++; Write-Host ("  [PASS] {0} {1}" -f $Name, $Detail) -ForegroundColor Green }
    else     { $script:fail++; Write-Host ("  [FAIL] {0} {1}" -f $Name, $Detail) -ForegroundColor Red }
}

$ONLINEUSER_REQUEST  = 7
$ONLINEUSER_RESPOND  = 8
$REGIST_REQUEST      = 1
$REGIST_RESPOND      = 2
$LOGIN_REQUEST       = 3
$LOGIN_RESPOND       = 4

# 服务端已加入登录态校验：除注册/登录/心跳外，未登录连接一律拒绝。
# 所以用于验证拆包的业务请求必须先登录。
$script:testUser = "proto_test_$([DateTime]::Now.ToString('HHmmss'))"
$script:testPwd  = "pwd123"

function New-AuthenticatedClient {
    param([string]$User, [string]$Pwd)

    # 构造 64 字节 caData：前 32 字节用户名，后 32 字节密码
    $data = New-Object byte[] 64
    $u = [System.Text.Encoding]::UTF8.GetBytes($User)
    $p = [System.Text.Encoding]::UTF8.GetBytes($Pwd)
    [Array]::Copy($u, 0, $data, 0,  [Math]::Min($u.Length, 32))
    [Array]::Copy($p, 0, $data, 32, [Math]::Min($p.Length, 32))

    # 先注册（已存在则忽略结果）
    $c0 = New-Object System.Net.Sockets.TcpClient($ServerIp, $Port)
    $s0 = $c0.GetStream()
    $reg = New-TestPdu -Type $REGIST_REQUEST -Data64 $data
    $s0.Write($reg, 0, $reg.Length)
    $null = Read-Pdu -Stream $s0
    $c0.Close()

    # 登录
    $c = New-Object System.Net.Sockets.TcpClient($ServerIp, $Port)
    $c.NoDelay = $true
    $s = $c.GetStream()
    $login = New-TestPdu -Type $LOGIN_REQUEST -Data64 $data
    $s.Write($login, 0, $login.Length)
    $resp = Read-Pdu -Stream $s
    if ($resp.Type -ne $LOGIN_RESPOND) {
        $c.Close()
        throw "登录失败: type=$($resp.Type)"
    }
    return $c
}

Write-Host "`n=== 协议层测试 ($ServerIp`:$Port) ===`n" -ForegroundColor Cyan
Write-Host "注意：业务请求需已登录（服务端有登录态校验）`n" -ForegroundColor DarkGray

# ---------- 测试 1：粘包（一次写两个 PDU）----------
Write-Host "[测试1] 粘包：2 个 PDU 一次性写入" -ForegroundColor Yellow
try {
    $c = New-AuthenticatedClient -User $script:testUser -Pwd $script:testPwd
    $s = $c.GetStream()
    $pdu = New-TestPdu -Type $ONLINEUSER_REQUEST
    $two = New-Object byte[] ($pdu.Length * 2)
    [Array]::Copy($pdu, 0, $two, 0, $pdu.Length)
    [Array]::Copy($pdu, 0, $two, $pdu.Length, $pdu.Length)
    $s.Write($two, 0, $two.Length)
    $r1 = Read-Pdu -Stream $s
    $r2 = Read-Pdu -Stream $s
    Test-Result "收到第 1 个应答" ($r1.Type -eq $ONLINEUSER_RESPOND) "(type=$($r1.Type))"
    Test-Result "收到第 2 个应答" ($r2.Type -eq $ONLINEUSER_RESPOND) "(type=$($r2.Type))"
    $c.Close()
} catch { Test-Result "粘包测试" $false $_.Exception.Message }

# ---------- 测试 2：半包（一个 PDU 分两次写）----------
Write-Host "`n[测试2] 半包：一个 PDU 拆成 40 + 36 字节两次写入，间隔 600ms" -ForegroundColor Yellow
try {
    $c = New-AuthenticatedClient -User $script:testUser -Pwd $script:testPwd
    $c.NoDelay = $true
    $s = $c.GetStream()
    $pdu = New-TestPdu -Type $ONLINEUSER_REQUEST
    $s.Write($pdu, 0, 40); $s.Flush()
    Start-Sleep -Milliseconds 600
    $s.Write($pdu, 40, 36); $s.Flush()
    $r = Read-Pdu -Stream $s
    Test-Result "半包拼接后正确解析" ($r.Type -eq $ONLINEUSER_RESPOND) "(type=$($r.Type), totalLen=$($r.TotalLen))"
    $c.Close()
} catch { Test-Result "半包测试" $false $_.Exception.Message }

# ---------- 测试 3：非法包长（uiTotalLen < 76）----------
Write-Host "`n[测试3] 非法包长：uiTotalLen=10 (< 76)，应被服务端断开" -ForegroundColor Yellow
try {
    $c = New-Object System.Net.Sockets.TcpClient($ServerIp, $Port)
    $s = $c.GetStream()
    $bad = New-Object System.Collections.Generic.List[byte]
    $bad.AddRange([BitConverter]::GetBytes([uint32]10))   # uiTotalLen = 10，非法
    $bad.AddRange([BitConverter]::GetBytes([uint32]0))
    $bad.AddRange([BitConverter]::GetBytes([uint32]$ONLINEUSER_REQUEST))
    $bad.AddRange((New-Object byte[] 64))
    $s.Write($bad.ToArray(), 0, 76)
    Start-Sleep -Milliseconds 800
    $closed = $false
    try {
        $s.ReadTimeout = 1000
        $tmp = New-Object byte[] 76
        $n = $s.Read($tmp, 0, 76)
        if ($n -eq 0) { $closed = $true }    # 对端关闭 -> 读到 0
    } catch { $closed = $true }              # 连接被重置/超时
    Test-Result "非法包长被断开（未崩溃）" $closed
    $c.Close()
} catch { Test-Result "非法包长测试" $false $_.Exception.Message }

# ---------- 测试 4：超大包长（> 8MB）----------
Write-Host "`n[测试4] 超大包长：uiTotalLen=100MB (> 8MB 上限)，应被断开" -ForegroundColor Yellow
try {
    $c = New-Object System.Net.Sockets.TcpClient($ServerIp, $Port)
    $s = $c.GetStream()
    $bad = New-Object System.Collections.Generic.List[byte]
    $bad.AddRange([BitConverter]::GetBytes([uint32](100 * 1024 * 1024)))
    $bad.AddRange([BitConverter]::GetBytes([uint32]0))
    $bad.AddRange([BitConverter]::GetBytes([uint32]$ONLINEUSER_REQUEST))
    $bad.AddRange((New-Object byte[] 64))
    $s.Write($bad.ToArray(), 0, 76)
    Start-Sleep -Milliseconds 800
    $closed = $false
    try {
        $s.ReadTimeout = 1000
        $tmp = New-Object byte[] 76
        $n = $s.Read($tmp, 0, 76)
        if ($n -eq 0) { $closed = $true }
    } catch { $closed = $true }
    Test-Result "超大包长被断开（未巨量 malloc）" $closed
    $c.Close()
} catch { Test-Result "超大包长测试" $false $_.Exception.Message }

# ---------- 测试 5：服务端仍存活 ----------
Write-Host "`n[测试5] 服务端存活检查：异常包之后新连接仍能正常应答" -ForegroundColor Yellow
try {
    # 同样需要先登录（业务接口有登录态校验）
    $c = New-AuthenticatedClient -User $script:testUser -Pwd $script:testPwd
    $s = $c.GetStream()
    $req = New-TestPdu -Type $ONLINEUSER_REQUEST
    $s.Write($req, 0, $req.Length)
    $r = Read-Pdu -Stream $s
    Test-Result "异常包之后服务端仍正常服务" ($r.Type -eq $ONLINEUSER_RESPOND) "(type=$($r.Type))"
    $c.Close()
} catch { Test-Result "服务端存活" $false $_.Exception.Message }

Write-Host "`n=== 结果: $script:pass 通过 / $script:fail 失败 ===" -ForegroundColor Cyan
if ($script:fail -gt 0) { exit 1 } else { exit 0 }
