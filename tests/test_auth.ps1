# ============================================================
#  Bug 回归测试：重复登录误置离线 / 服务端不校验操作身份
#  用法：先启动 Server.exe（监听 127.0.0.1:5000），再运行本脚本
#  依赖：本机 MySQL 客户端（用于校验 online 字段与好友关系）
#
#  测试顺序说明：
#   先测 Bug 2（身份校验），最后测 Bug 1（互踢掉线）。
#   因为好友列表按 online=1 过滤，若先互踢会让对方掉线，
#   导致"好友列表包含对方"的断言无辜失败。
#
#  协议行为记录（写测试时踩到，留作备忘）：
#   ADD_FRIEND_REQUEST 在对方在线时，服务端只把包改类型后转发给对方
#   （ADD_FRIEND_RESEND），**不给发起方回 RESPOND**；对方同意后，
#   AGREER 收到 ADD_FRIEND_AGREE_RESPOND，同时发起方会收到一条通知。
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
$ADD_FRIEND_REQUEST = 9; $ADD_FRIEND_RESPOND = 10; $ADD_FRIEND_RESEND = 11
$ADD_FRIEND_AGREE_REQUEST = 12; $ADD_FRIEND_AGREE_RESPOND = 13
$FLUSH_FRIEND_REQUEST = 14; $FLUSH_FRIEND_RESPOND = 15
$DEL_FRIEND_REQUEST = 16; $DEL_FRIEND_RESPOND = 17
$CHAT_REQUEST = 18; $CHAT_RESEND = 20
$FLUSH_FILE_REQUEST = 23; $FLUSH_FILE_RESPOND = 24
$HEARTBEAT_REQUEST = 37; $HEARTBEAT_RESPOND = 38

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
    $pb = [System.Text.Encoding]::UTF8.GetBytes($Path)
    $msg = New-Object byte[] ($pb.Length + 1)
    [Array]::Copy($pb, $msg, $pb.Length)
    return New-Pdu -Type $Type -Data1 $Data1 -Data2 $Data2 -Msg $msg
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
    $resultInt  = [BitConverter]::ToInt32($hdr, 12)
    $resultBool = ($hdr[12] -ne 0)
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
    return [pscustomobject]@{
        Type = $type; MsgLen = $rest; Body = $body
        ResultBool = $resultBool; ResultInt = $resultInt
    }
}

function Try-Read-Pdu {
    param($Client, [int]$TimeoutMs = 1500)
    try { return Read-Pdu -Client $Client -TimeoutMs $TimeoutMs }
    catch { return $null }
}

# 清空连接上所有待读数据。
# 前面的用例（通知类消息等）会留下未读应答，不清空会导致后续读错位。
function Clear-Pending {
    param($Client, [int]$SettleMs = 350)
    if ($null -eq $Client) { return }
    try {
        for ($round = 0; $round -lt 2; $round++) {
            while ($Client.Available -gt 0) {
                $buf = New-Object byte[] $Client.Available
                $Client.Receive($buf) | Out-Null
                Start-Sleep -Milliseconds 40
            }
            Start-Sleep -Milliseconds $SettleMs
        }
    } catch { }
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

function Reg-Or-Login {
    param([string]$User, [string]$Pwd, [bool]$Login = $true)
    $c = Connect-Server
    $type = if ($Login) { $LOGIN_REQUEST } else { $REGIST_REQUEST }
    $c.GetStream().Write((New-Pdu -Type $type -Data1 $User -Data2 $Pwd), 0, 76)
    $r = Read-Pdu -Client $c
    return [pscustomobject]@{ Client = $c; Result = $r }
}

function Get-Online {
    param([string]$User)
    $row = Sql "select online from user_info where name='$User'"
    if ($row.Count -eq 0) { return -1 }
    return [int]([string]$row[0])
}

$suffix = [DateTime]::Now.ToString("HHmmss")
$rand   = Get-Random -Maximum 9999
$userA  = "bugA_${suffix}_$rand"
$userB  = "bugB_${suffix}_$rand"
$pwd    = "pwd123"

Write-Host "`n=== Bug 回归测试 ($ServerIp`:$Port) ===" -ForegroundColor Cyan
Write-Host "测试账号: $userA / $userB`n" -ForegroundColor DarkGray

# ---- 2.1 未登录连接不得调用业务接口 ----
Write-Host "[Bug 2] 操作身份校验" -ForegroundColor Yellow
Write-Host "`n  [2.1] 未登录连接调用业务接口" -ForegroundColor Yellow
$anon = Connect-Server
$denied = $false
try {
    $pkt = New-PduWithPath -Type $FLUSH_FILE_REQUEST -Path "./userdata/$userB"
    $anon.GetStream().Write($pkt, 0, $pkt.Length)
    $r = Try-Read-Pdu -Client $anon -TimeoutMs 1500
    if ($null -eq $r) {
        $denied = $true                                   # 直接断开也算拒绝
    } else {
        $denied = ($r.Type -eq $FLUSH_FILE_RESPOND -and $r.ResultBool -eq $false -and $r.MsgLen -eq 0)
    }
} catch { $denied = $true }
Test-Result "未登录的 FLUSH_FILE 被拒绝" $denied
$anon.Close()

# ---- 准备 A、B 两个已登录会话 ----
$regA = Reg-Or-Login -User $userA -Pwd $pwd -Login $false
$regA.Client.Close()
$regB = Reg-Or-Login -User $userB -Pwd $pwd -Login $false
$regB.Client.Close()
$sessA = Reg-Or-Login -User $userA -Pwd $pwd
$sessB = Reg-Or-Login -User $userB -Pwd $pwd
Test-Result "A 登录" ($sessA.Result.ResultBool)
Test-Result "B 登录" ($sessB.Result.ResultBool)
Start-Sleep -Milliseconds 300
Clear-Pending $sessA.Client
Clear-Pending $sessB.Client

# ---- 2.2 冒名聊天 ----
Write-Host "`n  [2.2] 冒名聊天：A 以 B 的名义给 B 发消息" -ForegroundColor Yellow
$spoofed = $false
try {
    $chatPkt = New-Pdu -Type $CHAT_REQUEST -Data1 $userB -Data2 $userB `
        -Msg ([System.Text.Encoding]::UTF8.GetBytes("SPOOFED_MESSAGE"))
    $sessA.Client.GetStream().Write($chatPkt, 0, $chatPkt.Length)
    $rb = Try-Read-Pdu -Client $sessB.Client -TimeoutMs 1500
    if ($null -ne $rb -and $rb.Type -eq $CHAT_RESEND) { $spoofed = $true }
} catch { }
Test-Result "冒名消息未被转发给 B" (-not $spoofed)

# 反向验证：以自己名义聊天应正常送达
try {
    $okPkt = New-Pdu -Type $CHAT_REQUEST -Data1 $userA -Data2 $userB `
        -Msg ([System.Text.Encoding]::UTF8.GetBytes("LEGIT_MESSAGE"))
    $sessA.Client.GetStream().Write($okPkt, 0, $okPkt.Length)
    $rb2 = Try-Read-Pdu -Client $sessB.Client -TimeoutMs 2500
    Test-Result "合法聊天正常送达（未误杀）" ($null -ne $rb2 -and $rb2.Type -eq $CHAT_RESEND) `
        "(type=$(if ($rb2) { $rb2.Type } else { 'none' }))"
} catch { Test-Result "合法聊天" $false $_.Exception.Message }
Clear-Pending $sessB.Client

# ---- 2.3 冒名加好友 ----
Write-Host "`n  [2.3] 冒名加好友：A 以 B 的名义发起好友请求" -ForegroundColor Yellow
try {
    $afPkt = New-Pdu -Type $ADD_FRIEND_REQUEST -Data1 $userB -Data2 $userB
    $sessA.Client.GetStream().Write($afPkt, 0, $afPkt.Length)
    $ra = Read-Pdu -Client $sessA.Client
    Test-Result "冒名加好友被拒绝" (($ra.Type -eq $ADD_FRIEND_RESPOND) -and ($ra.ResultBool -eq $false) -and ($ra.ResultInt -ne 1)) `
        "(type=$($ra.Type), resultInt=$($ra.ResultInt))"
} catch { Test-Result "冒名加好友" $false $_.Exception.Message }
Clear-Pending $sessA.Client
Clear-Pending $sessB.Client

# ---- 2.4 冒名删好友 ----
Write-Host "`n  [2.4] 冒名删好友：A 以 B 的名义删好友关系" -ForegroundColor Yellow
$friendReady = $false
try {
    # 前置：正常流程建立 A-B 好友关系。
    # 注意：对方在线时 ADD_FRIEND_REQUEST 只转发、不回 RESPOND，所以这里等 B 的转发包。
    $af2 = New-Pdu -Type $ADD_FRIEND_REQUEST -Data1 $userA -Data2 $userB
    $sessA.Client.GetStream().Write($af2, 0, $af2.Length)

    $resendB = Try-Read-Pdu -Client $sessB.Client -TimeoutMs 2500
    if ($null -eq $resendB -or $resendB.Type -ne $ADD_FRIEND_RESEND) {
        Test-Result "前置：B 收到好友请求转发" $false `
            "(type=$(if ($resendB) { $resendB.Type } else { 'none' }))"
    } else {
        # caData 语义与真实客户端一致：[发起方 A, 同意方 B]。
        # 客户端收到 ADD_FRIEND_RESEND 后会原样沿用 caData（发起方 + 自己），
        # 所以这里也必须把 A 放在前 32 字节，否则测的就不是真实流程。
        $agree = New-Pdu -Type $ADD_FRIEND_AGREE_REQUEST -Data1 $userA -Data2 $userB
        $sessB.Client.GetStream().Write($agree, 0, $agree.Length)
        $rbAgree = Read-Pdu -Client $sessB.Client
        $friendReady = ($rbAgree.Type -eq $ADD_FRIEND_AGREE_RESPOND -and $rbAgree.ResultBool)
        Test-Result "前置：建立 A-B 好友关系" $friendReady "(result=$($rbAgree.ResultBool))"

        # 同意成功后，服务端还会给发起方 A 回一条 ADD_FRIEND_AGREE_RESPOND 通知。
        # 必须显式消费掉：它是真实行为（A 需要知道自己被同意了），但若留在 A 的
        # 接收缓冲区里，后面每个用例都会读到上一个用例的包，导致断言错位。
        $notifyA = Try-Read-Pdu -Client $sessA.Client -TimeoutMs 2000
        if ($null -ne $notifyA -and $notifyA.Type -eq $ADD_FRIEND_AGREE_RESPOND) {
            Write-Host "    (已消费发起方 A 的同意通知 type=$($notifyA.Type))" -ForegroundColor DarkGray
        } else {
            Write-Host "    (未收到发起方通知，type=$(if ($notifyA) { $notifyA.Type } else { 'none' }))" -ForegroundColor DarkYellow
        }
    }

    # 清掉残留（A 会收到一条 ADD_FRIEND_AGREE_RESPOND 通知）
    Clear-Pending $sessA.Client
    Clear-Pending $sessB.Client

    if ($friendReady) {
        # A 冒用 B 的名义删除好友关系
        $delPkt = New-Pdu -Type $DEL_FRIEND_REQUEST -Data1 $userB -Data2 $userA
        $sessA.Client.GetStream().Write($delPkt, 0, $delPkt.Length)
        $rd = Read-Pdu -Client $sessA.Client
        Test-Result "冒名删好友被拒绝" (($rd.Type -eq $DEL_FRIEND_RESPOND) -and ($rd.ResultBool -eq $false)) `
            "(type=$($rd.Type), result=$($rd.ResultBool))"

        # 好友关系应仍然存在
        $cnt = Sql "select count(*) from friend f join user_info u1 on f.user_id=u1.id join user_info u2 on f.friend_id=u2.id where (u1.name='$userA' and u2.name='$userB') or (u1.name='$userB' and u2.name='$userA')"
        $pairCnt = [int]([string]$cnt[0])
        Test-Result "好友关系未被删除" ($pairCnt -ge 1) "(records=$pairCnt)"
    }
} catch { Test-Result "冒名删好友" $false $_.Exception.Message }
Clear-Pending $sessA.Client

# ---- 2.5 冒名查看好友列表 ----
# 说明：FLUSH_FRIEND_RESPOND 的 caData 不承载 bool，拒绝与"空列表"在线上不可区分。
# 这是刻意的：拒绝时不返回任何好友名字，即不泄漏信息。
Write-Host "`n  [2.5] 冒名查好友列表：A 请求查看 B 的好友列表" -ForegroundColor Yellow
try {
    $ffPkt = New-Pdu -Type $FLUSH_FRIEND_REQUEST -Data1 $userB
    $sessA.Client.GetStream().Write($ffPkt, 0, $ffPkt.Length)
    $rf = Read-Pdu -Client $sessA.Client
    Test-Result "冒名查好友列表未泄漏任何名字" (($rf.Type -eq $FLUSH_FRIEND_RESPOND) -and ($rf.MsgLen -eq 0)) `
        "(type=$($rf.Type), caMsgLen=$($rf.MsgLen))"
} catch { Test-Result "冒名查好友" $false $_.Exception.Message }
Clear-Pending $sessA.Client

# ---- 2.6 以自己的身份查好友列表应成功（防误杀）----
Write-Host "`n  [2.6] 回归：A 查自己的好友列表，应返回 B" -ForegroundColor Yellow
try {
    $ffOk = New-Pdu -Type $FLUSH_FRIEND_REQUEST -Data1 $userA
    $sessA.Client.GetStream().Write($ffOk, 0, $ffOk.Length)
    $rfOk = Read-Pdu -Client $sessA.Client
    # 成功时返回 >=32 字节（每个好友名定长 32 字节）；B 此刻在线，必然在列表中
    $hasB = $false
    if ($rfOk.MsgLen -ge 32) {
        $text = [System.Text.Encoding]::UTF8.GetString($rfOk.Body)
        $hasB = $text.Contains($userB)
    }
    Test-Result "合法查好友列表未被误杀且包含 B" ($rfOk.Type -eq $FLUSH_FRIEND_RESPOND -and $rfOk.MsgLen -ge 32 -and $hasB) `
        "(caMsgLen=$($rfOk.MsgLen), 含B=$hasB)"
} catch { Test-Result "合法查好友" $false $_.Exception.Message }

$sessA.Client.Close()
$sessB.Client.Close()
Start-Sleep -Milliseconds 500

# ============================================================
#  Bug 1：重复登录踢人后，账号不应被错误置为离线
# ============================================================
Write-Host "`n[Bug 1] 重复登录互踢后 online 状态" -ForegroundColor Yellow
$userC = "bugC_${suffix}_$rand"
$regC = Reg-Or-Login -User $userC -Pwd $pwd -Login $false
$regC.Client.Close()
Test-Result "注册 $userC" ($regC.Result.Type -eq $REGIST_RESPOND -and $regC.Result.ResultBool)

# 第一次登录
$c1 = Reg-Or-Login -User $userC -Pwd $pwd
Test-Result "连接1 登录成功" ($c1.Result.Type -eq $LOGIN_RESPOND -and $c1.Result.ResultBool)
Start-Sleep -Milliseconds 300
Test-Result "连接1 登录后 online=1" ((Get-Online $userC) -eq 1) "(online=$(Get-Online $userC))"

# 第二次登录（触发互踢）
$c2 = Reg-Or-Login -User $userC -Pwd $pwd
Test-Result "连接2 登录成功（触发互踢）" ($c2.Result.Type -eq $LOGIN_RESPOND -and $c2.Result.ResultBool)

# 等旧连接的 disconnected 回调执行完毕，再检查 online
Start-Sleep -Milliseconds 1500
$onlineAfterKick = Get-Online $userC
Test-Result "互踢后 online 仍为 1（未被旧连接覆盖为 0）" ($onlineAfterKick -eq 1) "(online=$onlineAfterKick)"

# 新连接应仍可用
$hb = $null
try {
    $c2.Client.GetStream().Write((New-Pdu -Type $HEARTBEAT_REQUEST), 0, 76)
    $hb = Read-Pdu -Client $c2.Client
} catch { }
Test-Result "新连接仍可正常通信" ($null -ne $hb -and $hb.Type -eq $HEARTBEAT_RESPOND) `
    "(type=$(if ($hb) { $hb.Type } else { 'none' }))"

# 唯一连接断开后，online 应回到 0
$c2.Client.Close()
Start-Sleep -Milliseconds 1500
$onlineAfterClose = Get-Online $userC
Test-Result "唯一连接断开后 online 归 0" ($onlineAfterClose -eq 0) "(online=$onlineAfterClose)"

# ============================================================
#  空用户名 / 空口令必须被拒绝
#  历史缺陷：caData 全为 0 时服务端用 name='' 去查库，
#  只要库里存在一条 name='' 的脏记录就会被判成登录成功。
# ============================================================
Write-Host "`n[加固] 空用户名 / 空口令" -ForegroundColor Yellow

$emptyReg = Reg-Or-Login -User "" -Pwd $pwd -Login $false
Test-Result "空用户名注册被拒绝" ($emptyReg.Result.Type -eq $REGIST_RESPOND -and -not $emptyReg.Result.ResultBool)
$emptyReg.Client.Close()

$emptyLogin = Reg-Or-Login -User "" -Pwd $pwd
Test-Result "空用户名登录被拒绝" ($emptyLogin.Result.Type -eq $LOGIN_RESPOND -and -not $emptyLogin.Result.ResultBool)
$emptyLogin.Client.Close()

$emptyPwd = Reg-Or-Login -User "emptyPwd_$suffix" -Pwd "" -Login $false
Test-Result "空口令注册被拒绝" ($emptyPwd.Result.Type -eq $REGIST_RESPOND -and -not $emptyPwd.Result.ResultBool)
$emptyPwd.Client.Close()

Write-Host "`n=== 结果: $script:pass 通过 / $script:fail 失败 ===" -ForegroundColor Cyan
if ($script:fail -gt 0) { exit 1 } else { exit 0 }
