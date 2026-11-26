# ============================================================
#  第 9 项：慢操作下沉线程池测试
#    - 递归删除大目录期间，其它连接的心跳仍然能被及时响应
#    - 分享拷贝期间，接收方连接同样不受影响
#    - 删除途中发起方断线：服务端不崩、删除照样完成
#    - 删除不存在的目录：失败分支也能正常应答
#  用法：先启动 Server.exe（监听 127.0.0.1:5000，工作目录为本脚本的 -ServerWorkDir），
#        再运行本脚本。
# ============================================================
param(
    [string]$ServerIp = "127.0.0.1",
    [int]$Port = 5000,
    [string]$ServerWorkDir = "D:\QTdm\build-console\Server"
)

$ErrorActionPreference = "Stop"
$script:pass = 0
$script:fail = 0

# 协议消息类型（与 protocol.h 枚举数值一致）
$REGIST_REQUEST  = 1
$LOGIN_REQUEST   = 3
$LOGIN_RESPOND   = 4
$CHAT_RESEND     = 20
$OFFLINE_MSG_RESEND = 40
$DEL_DIR_REQUEST  = 25
$DEL_DIR_RESPOND  = 26
$SHARE_FILE_REQUEST  = 35
$SHARE_FILE_RESPOND  = 36
$HEARTBEAT_REQUEST  = 37
$HEARTBEAT_RESPOND  = 38

# ---------------- 基础工具 ----------------

# 注意：PowerShell 函数返回数组时会把数组"展开"成多个输出对象，
# 不加一元逗号的话调用方拿到的是 Object[] 而不是 byte[]；
# 随后 Set-Str 的参数类型 [byte[]] 会做一次拷贝转换，就地修改全部丢失
function New-Data {
    return ,(New-Object byte[] 64)
}

function Set-Str {
    param([byte[]]$Data, [int]$Offset, [string]$Value)
    $b = [System.Text.Encoding]::UTF8.GetBytes($Value)
    [Array]::Copy($b, 0, $Data, $Offset, [Math]::Min($b.Length, 32))
}

function New-PduRaw {
    param([uint32]$Type, [byte[]]$Data = @(), [byte[]]$Msg = @())
    $len = 76 + $Msg.Length
    $buf = New-Object System.Collections.Generic.List[byte]
    $buf.AddRange([BitConverter]::GetBytes([uint32]$len))
    $buf.AddRange([BitConverter]::GetBytes([uint32]$Msg.Length))
    $buf.AddRange([BitConverter]::GetBytes([uint32]$Type))
    $d = New-Object byte[] 64
    if ($Data.Length -gt 0) { [Array]::Copy($Data, $d, [Math]::Min(64, $Data.Length)) }
    $buf.AddRange($d)
    if ($Msg.Length -gt 0) { $buf.AddRange($Msg) }
    return ,$buf.ToArray()
}

function Connect-Server {
    $c = New-Object System.Net.Sockets.TcpClient($ServerIp, $Port)
    $c.NoDelay = $true
    return $c
}

function Read-Pdu {
    param($Client, [int]$TimeoutMs = 5000)
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
    $rest     = [int64]$totalLen - 76
    $body = New-Object byte[] 0
    if ($rest -gt 0) {
        $body = New-Object byte[] $rest
        $g = 0
        while ($g -lt $rest) {
            $n = $s.Read($body, $g, $rest - $g)
            if ($n -le 0) { throw "连接关闭(正文读到 $g/$rest)" }
            $g += $n
        }
    }
    $data = New-Object byte[] 64
    [Array]::Copy($hdr, 12, $data, 0, 64)
    return [pscustomobject]@{
        Type = $type; MsgLen = [int]$rest; Body = $body; Data = $data
        ResultBool = ($hdr[12] -ne 0)
    }
}

function Send-Pdu {
    param($Client, [byte[]]$Bytes)
    $Client.GetStream().Write($Bytes, 0, $Bytes.Length)
}

function Test-Result {
    param([string]$Name, [bool]$Ok, [string]$Detail = "")
    if ($Ok) { $script:pass++; Write-Host ("  [PASS] {0} {1}" -f $Name, $Detail) -ForegroundColor Green }
    else     { $script:fail++; Write-Host ("  [FAIL] {0} {1}" -f $Name, $Detail) -ForegroundColor Red }
}

function Login-Client {
    param([string]$User, [string]$Pwd)
    # 先注册（用户已存在时会失败，忽略即可），再登录
    $c1 = Connect-Server
    $d = New-Data
    Set-Str $d 0 $User
    Set-Str $d 32 $Pwd
    Send-Pdu $c1 (New-PduRaw -Type $REGIST_REQUEST -Data $d)
    Read-Pdu -Client $c1 | Out-Null
    $c1.Close()

    $c = Connect-Server
    $d = New-Data
    Set-Str $d 0 $User
    Set-Str $d 32 $Pwd
    Send-Pdu $c (New-PduRaw -Type $LOGIN_REQUEST -Data $d)
    $r = Read-Pdu -Client $c
    if ($r.Type -ne $LOGIN_RESPOND) { throw "登录未得到 LOGIN_RESPOND，实收 $($r.Type)" }
    return [pscustomobject]@{ Client = $c; Ok = [bool]$r.Data[0] }
}

# 心跳往返耗时（毫秒）
function Measure-Heartbeat {
    param($Client)
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    Send-Pdu $Client (New-PduRaw -Type $HEARTBEAT_REQUEST)
    $r = Read-Pdu -Client $Client -TimeoutMs 15000
    $sw.Stop()
    if ($r.Type -ne $HEARTBEAT_RESPOND) { throw "心跳未得到 HEARTBEAT_RESPOND，实收 $($r.Type)" }
    return [int]$sw.ElapsedMilliseconds
}

function Get-Md5Hex {
    param([string]$Path)
    $md5 = [System.Security.Cryptography.MD5]::Create()
    $fs = [System.IO.File]::OpenRead($Path)
    try { return ([BitConverter]::ToString($md5.ComputeHash($fs)) -replace '-', '').ToLower() }
    finally { $fs.Close(); $md5.Dispose() }
}

# 在用户网盘下造一棵多文件的目录树（直接写文件系统，比走协议快）
function New-BigTree {
    param([string]$BasePath, [int]$DirCount, [int]$FileCount)
    New-Item -ItemType Directory -Force -Path $BasePath | Out-Null
    $payload = [byte[]](1, 2, 3, 4, 5, 6, 7, 8)
    for ($i = 0; $i -lt $DirCount; $i++) {
        $sub = Join-Path $BasePath ("d{0}" -f $i)
        New-Item -ItemType Directory -Force -Path $sub | Out-Null
        for ($j = 0; $j -lt $FileCount; $j++) {
            [System.IO.File]::WriteAllBytes((Join-Path $sub ("f{0}.dat" -f $j)), $payload)
        }
    }
}

function Count-Files {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return 0 }
    return (Get-ChildItem -Path $Path -Recurse -File -ErrorAction SilentlyContinue | Measure-Object).Count
}

# ============================================================
$suffix     = [DateTime]::Now.ToString("HHmmss")
$userA      = "asyncA$suffix"
$userB      = "asyncB$suffix"
$pwd        = "123456"
$userDirA   = Join-Path $ServerWorkDir "userdata\$userA"
$userDirB   = Join-Path $ServerWorkDir "userdata\$userB"
$dirPathA   = "./userdata/$userA"      # 服务端看到的相对路径（协议里用这个）
$treeName   = "bigtree"
$bigTreeAbs = Join-Path $userDirA $treeName

Write-Host "=== 第 9 项：慢操作下沉线程池 ===" -ForegroundColor Cyan
Write-Host "用户: $userA / $userB   服务端工作目录: $ServerWorkDir"

$a = Login-Client -User $userA -Pwd $pwd
$b = Login-Client -User $userB -Pwd $pwd
try {
    if (-not $a.Ok) { throw "用户 $userA 登录失败" }
    if (-not $b.Ok) { throw "用户 $userB 登录失败" }

    # ---------------- [1] 递归删除不阻塞其它连接 ----------------
    Write-Host "`n[测试1] 递归删除 3000 个文件期间，另一条连接的心跳仍应及时应答" -ForegroundColor Yellow
    New-BigTree -BasePath $bigTreeAbs -DirCount 50 -FileCount 60
    Test-Result "目录树已就绪" ((Count-Files $bigTreeAbs) -eq 3000) "(files=$(Count-Files $bigTreeAbs))"

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $d = New-Data
    Set-Str $d 0 $treeName
    $msg = [System.Text.Encoding]::UTF8.GetBytes($dirPathA) + [byte]0
    Send-Pdu $a.Client (New-PduRaw -Type $DEL_DIR_REQUEST -Data $d -Msg $msg)

    # 删除任务还在后台跑，这条心跳应当立刻拿到应答
    $rtt = Measure-Heartbeat -Client $b.Client

    $r = Read-Pdu -Client $a.Client -TimeoutMs 120000
    $sw.Stop()
    $delMs = [int]$sw.ElapsedMilliseconds

    Test-Result "删除应答为成功" (($r.Type -eq $DEL_DIR_RESPOND) -and $r.ResultBool) "(type=$($r.Type) ok=$($r.ResultBool))"
    Test-Result "目录已删除" (-not (Test-Path $bigTreeAbs))
    Test-Result "删除期间心跳仍及时响应" ($rtt -lt 1000) "(心跳 ${rtt}ms / 删除 ${delMs}ms)"
    if ($delMs -le 1000) {
        Write-Host "  注意：本次删除只用了 ${delMs}ms，用例区分度有限（数据量大时差别才明显）" -ForegroundColor DarkGray
    }

    # ---------------- [2] 分享拷贝不阻塞连接 ----------------
    Write-Host "`n[测试2] 分享 16MB 文件期间，接收方连接不受影响" -ForegroundColor Yellow
    $shareName = "share_$suffix.bin"
    $shareAbs  = Join-Path $userDirA $shareName
    New-Item -ItemType Directory -Force -Path $userDirA | Out-Null
    $blob = New-Object byte[] (16 * 1024 * 1024)
    (New-Object System.Random(20261126)).NextBytes($blob)
    [System.IO.File]::WriteAllBytes($shareAbs, $blob)
    $shareMd5 = Get-Md5Hex $shareAbs

    $d = New-Data
    Set-Str $d 0 $shareName
    Set-Str $d 32 $userB
    $msg = [System.Text.Encoding]::UTF8.GetBytes($dirPathA) + [byte]0
    Send-Pdu $a.Client (New-PduRaw -Type $SHARE_FILE_REQUEST -Data $d -Msg $msg)

    # 拷贝在线程池里跑：这里给 B 发心跳，同时把可能先到的分享通知收下来
    $sw2 = [System.Diagnostics.Stopwatch]::StartNew()
    Send-Pdu $b.Client (New-PduRaw -Type $HEARTBEAT_REQUEST)
    $shareRtt = $null
    $notice   = $null
    for ($i = 0; $i -lt 6; $i++) {
        try { $rr = Read-Pdu -Client $b.Client -TimeoutMs 30000 } catch { break }
        if ($rr.Type -eq $HEARTBEAT_RESPOND) { $shareRtt = [int]$sw2.ElapsedMilliseconds }
        elseif ($rr.Type -eq $CHAT_RESEND -or $rr.Type -eq $OFFLINE_MSG_RESEND) { $notice = $rr }
        if (($null -ne $shareRtt) -and ($null -ne $notice)) { break }
    }

    $r = Read-Pdu -Client $a.Client -TimeoutMs 60000
    Test-Result "分享应答为成功" (($r.Type -eq $SHARE_FILE_RESPOND) -and $r.ResultBool) "(type=$($r.Type) ok=$($r.ResultBool))"
    Test-Result "接收方连接在拷贝期间有应答" ($null -ne $shareRtt) "(心跳 ${shareRtt}ms)"
    Test-Result "接收方收到分享通知" ($null -ne $notice) "(type=$($notice.Type))"

    $dstFile = Join-Path $userDirB $shareName
    $dstOk   = (Test-Path $dstFile) -and ((Get-Md5Hex $dstFile) -eq $shareMd5)
    Test-Result "接收方网盘出现同内容文件" $dstOk

    # ---------------- [3] 删除途中发起方断线 ----------------
    Write-Host "`n[测试3] 删除途中发起方断线：服务端不崩、删除仍然完成" -ForegroundColor Yellow
    $tree2Name = "tree2"
    $tree2Abs  = Join-Path $userDirA $tree2Name
    New-BigTree -BasePath $tree2Abs -DirCount 30 -FileCount 40

    $a2 = Login-Client -User $userA -Pwd $pwd
    $d = New-Data
    Set-Str $d 0 $tree2Name
    Send-Pdu $a2.Client (New-PduRaw -Type $DEL_DIR_REQUEST -Data $d -Msg $msg)
    Start-Sleep -Milliseconds 30
    $a2.Client.Close()          # 删除还没跑完就断开

    # 后台任务完成前，另一条连接必须一直被正常服务
    $rtt2 = Measure-Heartbeat -Client $b.Client
    Test-Result "断线后服务端仍正常应答" ($rtt2 -lt 3000) "(心跳 ${rtt2}ms)"

    # 轮询等删除完成（最多 30 秒）
    $gone = $false
    for ($i = 0; $i -lt 60; $i++) {
        if (-not (Test-Path $tree2Abs)) { $gone = $true; break }
        Start-Sleep -Milliseconds 500
    }
    Test-Result "发起方断线后删除依然完成" $gone

    $a3 = Login-Client -User $userA -Pwd $pwd
    Test-Result "重新登录成功（服务端状态正常）" $a3.Ok
    # 注意：同一账号再次登录会把旧会话踢掉（第 3 项），所以后面的用例必须
    # 改用新连接；继续用 $a.Client 只会读到"连接被对端关闭"

    # ---------------- [4] 删除不存在的目录 ----------------
    Write-Host "`n[测试4] 删除不存在的目录：异步路径的失败分支也要有应答" -ForegroundColor Yellow
    $d = New-Data
    Set-Str $d 0 "no_such_dir"
    Send-Pdu $a3.Client (New-PduRaw -Type $DEL_DIR_REQUEST -Data $d -Msg $msg)
    $r = Read-Pdu -Client $a3.Client -TimeoutMs 15000
    Test-Result "删除不存在目录得到失败应答" (($r.Type -eq $DEL_DIR_RESPOND) -and (-not $r.ResultBool)) `,
        "(type=$($r.Type) ok=$($r.ResultBool))"
    $a3.Client.Close()
}
finally {
    try { $a.Client.Close() } catch { }
    try { $b.Client.Close() } catch { }
}

Write-Host ""
Write-Host ("通过 {0} 项，失败 {1} 项" -f $script:pass, $script:fail) -ForegroundColor $(if ($script:fail -eq 0) { "Green" } else { "Red" })
if ($script:fail -gt 0) { exit 1 }
exit 0
