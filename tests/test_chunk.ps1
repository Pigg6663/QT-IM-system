# ============================================================
#  第 7 项：分片传输测试
#    - 大文件分片上传 + 整文件 MD5 校验
#    - 上传断点续传（.part 残留 -> 握手返回断点 -> 从断点继续）
#    - 分片偏移错乱被丢弃 -> 校验失败
#    - 客户端声明的 MD5 不符 -> 校验失败、不留下坏文件
#    - 服务端流式推送下载 + 下载断点续传
#    - 分享改为服务端侧拷贝，接收方网盘里能看到文件
#  用法：先启动 Server.exe（监听 127.0.0.1:5000），再运行本脚本
#
#  协议约定（见 protocol.h）：
#    UPLOAD_FILE_REQUEST          caData=[0:32]文件名 [32:40]总长 [40:56]原始MD5   caMsg=目录
#    UPLOAD_FILE_RESPOND          caData=[0:8]断点 [8]ok
#    UPLOAD_FILE_CHUNK_REQUEST    caData=[0:8]偏移  caMsg=数据
#    UPLOAD_FILE_FINISH_REQUEST   caData=[0:32]MD5 十六进制
#    UPLOAD_FILE_FINISH_RESPOND   caData=[0]ok
#    DOWNLOAD_FILE_REQUEST        caData=[0:32]文件名 [32:40]本地已有大小  caMsg=目录
#    DOWNLOAD_FILE_RESPOND        caData=[0]ok [1:9]起始偏移 [9:17]总长
#    DOWNLOAD_FILE_CHUNK_RESPOND  caData=[0:8]偏移  caMsg=数据
#    DOWNLOAD_FILE_FINISH_RESPOND caData=[0]ok [1:17]整文件原始MD5
# ============================================================
param(
    [string]$ServerIp = "127.0.0.1",
    [int]$Port = 5000,
    [string]$ServerWorkDir = "D:\QTdm\build-console\Server",
    [int]$ChunkSize = 262144
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
$UPLOAD_FILE_REQUEST          = 31
$UPLOAD_FILE_RESPOND          = 32
$DOWNLOAD_FILE_REQUEST        = 33
$DOWNLOAD_FILE_RESPOND        = 34
$SHARE_FILE_REQUEST           = 35
$SHARE_FILE_RESPOND           = 36
$UPLOAD_FILE_CHUNK_REQUEST    = 41
$UPLOAD_FILE_FINISH_REQUEST   = 42
$UPLOAD_FILE_FINISH_RESPOND   = 43
$DOWNLOAD_FILE_CHUNK_RESPOND  = 44
$DOWNLOAD_FILE_FINISH_RESPOND = 45

# ---------------- 基础工具 ----------------

# 注意：PowerShell 函数返回数组时会把数组"展开"成多个输出对象，
# 不加一元逗号的话调用方拿到的是 Object[] 而不是 byte[]；
# 随后 Set-Str 的参数类型 [byte[]] 会做一次拷贝转换，就地修改全部丢失
# （表现为服务端收到的 caData 全是 0，登录被判成"未登录"）。
function New-Data {
    return ,(New-Object byte[] 64)
}

function Set-Str {
    param([byte[]]$Data, [int]$Offset, [string]$Value)
    $b = [System.Text.Encoding]::UTF8.GetBytes($Value)
    [Array]::Copy($b, 0, $Data, $Offset, [Math]::Min($b.Length, 32))
}

function Set-I64 {
    param([byte[]]$Data, [int]$Offset, [long]$Value)
    [Array]::Copy([BitConverter]::GetBytes([int64]$Value), 0, $Data, $Offset, 8)
}

function Set-Bool {
    param([byte[]]$Data, [int]$Offset, [bool]$Value)
    if ($Value) { $Data[$Offset] = 1 } else { $Data[$Offset] = 0 }
}

function Get-I64 {
    param([byte[]]$Data, [int]$Offset)
    return [BitConverter]::ToInt64($Data, $Offset)
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

function Try-Read-Pdu {
    param($Client, [int]$TimeoutMs = 1500)
    try { return Read-Pdu -Client $Client -TimeoutMs $TimeoutMs } catch { return $null }
}

function Test-Result {
    param([string]$Name, [bool]$Ok, [string]$Detail = "")
    if ($Ok) { $script:pass++; Write-Host ("  [PASS] {0} {1}" -f $Name, $Detail) -ForegroundColor Green }
    else     { $script:fail++; Write-Host ("  [FAIL] {0} {1}" -f $Name, $Detail) -ForegroundColor Red }
}

function Get-Md5Raw {
    param([string]$Path)
    $md5 = [System.Security.Cryptography.MD5]::Create()
    $fs = [System.IO.File]::OpenRead($Path)
    try { return $md5.ComputeHash($fs) } finally { $fs.Close(); $md5.Dispose() }
}

function Get-Md5Hex {
    param([string]$Path)
    return ([BitConverter]::ToString((Get-Md5Raw $Path)) -replace '-', '').ToLower()
}

function Get-Md5HexOfBytes {
    param([byte[]]$Bytes)
    $md5 = [System.Security.Cryptography.MD5]::Create()
    try { return ([BitConverter]::ToString($md5.ComputeHash($Bytes)) -replace '-', '').ToLower() }
    finally { $md5.Dispose() }
}

function Reg-Or-Login {
    param([string]$User, [string]$Pwd, [bool]$Login = $true)
    $c = Connect-Server
    $type = if ($Login) { $LOGIN_REQUEST } else { $REGIST_REQUEST }
    $d = New-Data
    Set-Str $d 0 $User
    Set-Str $d 32 $Pwd
    $pdu = New-PduRaw -Type $type -Data $d
    $c.GetStream().Write($pdu, 0, $pdu.Length)
    $r = Read-Pdu -Client $c
    return [pscustomobject]@{ Client = $c; Result = $r }
}

# 发上传握手，返回 (ok, resume)
function Send-UploadInit {
    param($Client, [string]$Dir, [string]$FileName, [long]$Total, [byte[]]$Md5Raw)
    $d = New-Data
    Set-Str $d 0 $FileName
    Set-I64 $d 32 $Total
    [Array]::Copy($Md5Raw, 0, $d, 40, 16)
    $msg = [System.Text.Encoding]::UTF8.GetBytes($Dir) + [byte]0
    $pdu = New-PduRaw -Type $UPLOAD_FILE_REQUEST -Data $d -Msg $msg
    $Client.GetStream().Write($pdu, 0, $pdu.Length)
    $r = Read-Pdu -Client $Client
    if ($r.Type -ne $UPLOAD_FILE_RESPOND) { throw "期望 UPLOAD_FILE_RESPOND，实收 $($r.Type)" }
    return [pscustomobject]@{ Ok = [bool]$r.Data[8]; Resume = (Get-I64 $r.Data 0) }
}

# 从 StartAt 起推分片（只推到 EndAt 为止，便于构造"传一半断掉"）
function Send-UploadChunks {
    param($Client, [byte[]]$Bytes, [long]$StartAt, [long]$EndAt)
    $s = $Client.GetStream()
    for ($off = $StartAt; $off -lt $EndAt; $off += $ChunkSize) {
        $n = [Math]::Min($ChunkSize, $EndAt - $off)
        $block = New-Object byte[] $n
        [Array]::Copy($Bytes, $off, $block, 0, $n)
        $d = New-Data
        Set-I64 $d 0 $off
        $pdu = New-PduRaw -Type $UPLOAD_FILE_CHUNK_REQUEST -Data $d -Msg $block
        $s.Write($pdu, 0, $pdu.Length)
    }
}

function Send-UploadFinish {
    param($Client, [string]$Md5Hex)
    $d = New-Data
    Set-Str $d 0 $Md5Hex
    $pdu = New-PduRaw -Type $UPLOAD_FILE_FINISH_REQUEST -Data $d
    $Client.GetStream().Write($pdu, 0, $pdu.Length)
    $r = Read-Pdu -Client $Client
    if ($r.Type -ne $UPLOAD_FILE_FINISH_RESPOND) { throw "期望 UPLOAD_FILE_FINISH_RESPOND，实收 $($r.Type)" }
    return [bool]$r.Data[0]
}

# 发下载握手，返回 (ok, start, size)
function Send-DownloadInit {
    param($Client, [string]$Dir, [string]$FileName, [long]$Resume)
    $d = New-Data
    Set-Str $d 0 $FileName
    Set-I64 $d 32 $Resume
    $msg = [System.Text.Encoding]::UTF8.GetBytes($Dir) + [byte]0
    $pdu = New-PduRaw -Type $DOWNLOAD_FILE_REQUEST -Data $d -Msg $msg
    $Client.GetStream().Write($pdu, 0, $pdu.Length)
    $r = Read-Pdu -Client $Client
    if ($r.Type -ne $DOWNLOAD_FILE_RESPOND) { throw "期望 DOWNLOAD_FILE_RESPOND，实收 $($r.Type)" }
    return [pscustomobject]@{
        Ok = [bool]$r.Data[0]; Start = (Get-I64 $r.Data 1); Size = (Get-I64 $r.Data 9)
    }
}

# 收完所有分片直到 FINISH，返回拼好的字节与 MD5
function Receive-Download {
    param($Client, [int]$TimeoutMs = 15000)
    $list = New-Object System.Collections.Generic.List[byte]
    while ($true) {
        $r = Read-Pdu -Client $Client -TimeoutMs $TimeoutMs
        if ($r.Type -eq $DOWNLOAD_FILE_CHUNK_RESPOND) {
            $list.AddRange([byte[]]$r.Body)
        }
        elseif ($r.Type -eq $DOWNLOAD_FILE_FINISH_RESPOND) {
            $md5 = New-Object byte[] 16
            [Array]::Copy($r.Data, 1, $md5, 0, 16)
            return [pscustomobject]@{
                Bytes = $list.ToArray(); Ok = [bool]$r.Data[0]
                Md5Hex = ([BitConverter]::ToString($md5) -replace '-', '').ToLower()
            }
        }
        else { throw "下载过程中收到意外消息类型 $($r.Type)" }
    }
}

function Send-Share {
    param($Client, [string]$Dir, [string]$FileName, [string]$Target)
    $d = New-Data
    Set-Str $d 0 $FileName
    Set-Str $d 32 $Target
    $msg = [System.Text.Encoding]::UTF8.GetBytes($Dir) + [byte]0
    $pdu = New-PduRaw -Type $SHARE_FILE_REQUEST -Data $d -Msg $msg
    $Client.GetStream().Write($pdu, 0, $pdu.Length)
    $r = Read-Pdu -Client $Client
    if ($r.Type -ne $SHARE_FILE_RESPOND) { throw "期望 SHARE_FILE_RESPOND，实收 $($r.Type)" }
    return [bool]$r.Data[0]
}

# ---------------- 准备测试数据 ----------------

$suffix = [DateTime]::Now.ToString("HHmmss")
$rand   = Get-Random -Maximum 9999
$userA  = "chkA_${suffix}_$rand"
$userB  = "chkB_${suffix}_$rand"
$pwd    = "pwd123"

$dirA = "./userdata/$userA"
$dirB = "./userdata/$userB"
$diskA = Join-Path (Join-Path $ServerWorkDir "userdata") $userA
$diskB = Join-Path (Join-Path $ServerWorkDir "userdata") $userB

$tmpDir = Join-Path $env:TEMP "im_chunk_$suffix"
New-Item -ItemType Directory -Path $tmpDir -Force | Out-Null

# 3MB 随机文件 —— 正好跨多个分片
$bigName = "big_$suffix.bin"
$bigPath = Join-Path $tmpDir $bigName
$bigBytes = New-Object byte[] (3 * 1024 * 1024 + 12345)
$rng = New-Object System.Random(20260101)
$rng.NextBytes($bigBytes)
[System.IO.File]::WriteAllBytes($bigPath, $bigBytes)
$bigMd5 = Get-Md5Hex $bigPath

# 小文件：用于错乱偏移 / MD5 不符 / 断点续传
$smallName = "small_$suffix.bin"
$smallPath = Join-Path $tmpDir $smallName
$smallBytes = New-Object byte[] (700 * 1024)
$rng.NextBytes($smallBytes)
[System.IO.File]::WriteAllBytes($smallPath, $smallBytes)
$smallMd5 = Get-Md5Hex $smallPath

Write-Host "`n=== 分片传输测试 ($ServerIp`:$Port) ===" -ForegroundColor Cyan
Write-Host "A=$userA  B=$userB" -ForegroundColor DarkGray
Write-Host "大文件 $bigName $($bigBytes.Length) 字节 MD5=$bigMd5" -ForegroundColor DarkGray
Write-Host "小文件 $smallName $($smallBytes.Length) 字节 MD5=$smallMd5`n" -ForegroundColor DarkGray

# ---------- 准备账号 ----------
$regA = Reg-Or-Login -User $userA -Pwd $pwd -Login $false
$regA.Client.Close()
$regB = Reg-Or-Login -User $userB -Pwd $pwd -Login $false
$regB.Client.Close()

$sessA = Reg-Or-Login -User $userA -Pwd $pwd
Test-Result "A 登录成功" ($sessA.Result.Type -eq $LOGIN_RESPOND -and $sessA.Result.ResultBool)
$aConn = $sessA.Client

# ---------- 1. 大文件分片上传 ----------
Write-Host "[1] 3MB 文件分片上传（12+ 个分片）" -ForegroundColor Yellow
$init = Send-UploadInit -Client $aConn -Dir $dirA -FileName $bigName -Total $bigBytes.Length -Md5Raw (Get-Md5Raw $bigPath)
Test-Result "握手 ok 且断点为 0" ($init.Ok -and $init.Resume -eq 0) "(ok=$($init.Ok) resume=$($init.Resume))"

Send-UploadChunks -Client $aConn -Bytes $bigBytes -StartAt 0 -EndAt $bigBytes.Length
$finOk = Send-UploadFinish -Client $aConn -Md5Hex $bigMd5
Test-Result "上传校验通过（长度 + MD5）" $finOk

$bigOnDisk = Join-Path $diskA $bigName
$partOnDisk = "$bigOnDisk.part"
Test-Result "服务端网盘出现文件" (Test-Path $bigOnDisk)
Test-Result ".part 临时文件已转正（不存在）" (-not (Test-Path $partOnDisk))
if (Test-Path $bigOnDisk) {
    Test-Result "落盘内容与源文件一致（MD5）" ((Get-Md5Hex $bigOnDisk) -eq $bigMd5)
}

# ---------- 2. 上传断点续传 ----------
Write-Host "`n[2] 传一半断开，重新握手应从断点继续" -ForegroundColor Yellow
$half = 400 * 1024
$init2 = Send-UploadInit -Client $aConn -Dir $dirA -FileName $smallName -Total $smallBytes.Length -Md5Raw (Get-Md5Raw $smallPath)
Test-Result "首次握手断点为 0" ($init2.Ok -and $init2.Resume -eq 0)
Send-UploadChunks -Client $aConn -Bytes $smallBytes -StartAt 0 -EndAt $half
Start-Sleep -Milliseconds 700     # 等服务端把缓冲落盘
$smallPart = Join-Path $diskA "$smallName.part"
$partBytes = -1
if (Test-Path $smallPart) { $partBytes = (Get-Item $smallPart).Length }
Test-Result "服务端 .part 已写入前半段" ($partBytes -eq $half) "(part=$partBytes)"

$init3 = Send-UploadInit -Client $aConn -Dir $dirA -FileName $smallName -Total $smallBytes.Length -Md5Raw (Get-Md5Raw $smallPath)
Test-Result "再次握手返回断点" ($init3.Ok -and $init3.Resume -eq $half) "(resume=$($init3.Resume))"

Send-UploadChunks -Client $aConn -Bytes $smallBytes -StartAt $init3.Resume -EndAt $smallBytes.Length
$finOk2 = Send-UploadFinish -Client $aConn -Md5Hex $smallMd5
Test-Result "续传后校验通过" $finOk2
$smallOnDisk = Join-Path $diskA $smallName
Test-Result "续传文件内容正确" ((Test-Path $smallOnDisk) -and ((Get-Md5Hex $smallOnDisk) -eq $smallMd5))

# ---------- 3. 分片偏移错乱应被丢弃 ----------
Write-Host "`n[3] 分片偏移与期望不符 -> 丢弃，最终校验失败" -ForegroundColor Yellow
$badName = "bad_$suffix.bin"
$badPath = Join-Path $tmpDir $badName
[System.IO.File]::WriteAllBytes($badPath, $smallBytes)
$initBad = Send-UploadInit -Client $aConn -Dir $dirA -FileName $badName -Total $smallBytes.Length -Md5Raw (Get-Md5Raw $badPath)
Test-Result "握手 ok" $initBad.Ok

# 故意把第一片写成偏移 999999
$dBad = New-Data
Set-I64 $dBad 0 999999
$block0 = New-Object byte[] $ChunkSize
[Array]::Copy($smallBytes, 0, $block0, 0, $ChunkSize)
$pduBad = New-PduRaw -Type $UPLOAD_FILE_CHUNK_REQUEST -Data $dBad -Msg $block0
$aConn.GetStream().Write($pduBad, 0, $pduBad.Length)
Start-Sleep -Milliseconds 500

$badPart = Join-Path $diskA "$badName.part"
$badLen = 0
if (Test-Path $badPart) { $badLen = (Get-Item $badPart).Length }
Test-Result "错位分片没有被写入" ($badLen -eq 0) "(part=$badLen)"

$finBad = Send-UploadFinish -Client $aConn -Md5Hex (Get-Md5Hex $badPath)
Test-Result "缺片 -> 上传校验失败" (-not $finBad)
Test-Result "失败后不留下正式文件" (-not (Test-Path (Join-Path $diskA $badName)))
Test-Result "失败后清理 .part" (-not (Test-Path $badPart))

# ---------- 4. 客户端声明的 MD5 不符 ----------
Write-Host "`n[4] 客户端声明 MD5 与内容不符 -> 校验失败" -ForegroundColor Yellow
$liarName = "liar_$suffix.bin"
$initLiar = Send-UploadInit -Client $aConn -Dir $dirA -FileName $liarName -Total $smallBytes.Length -Md5Raw (Get-Md5Raw $smallPath)
Test-Result "握手 ok" $initLiar.Ok
Send-UploadChunks -Client $aConn -Bytes $smallBytes -StartAt 0 -EndAt $smallBytes.Length
$finLiar = Send-UploadFinish -Client $aConn -Md5Hex ("0" * 32)
Test-Result "MD5 不符 -> 上传失败" (-not $finLiar)
Test-Result "不留下正式文件" (-not (Test-Path (Join-Path $diskA $liarName)))
Test-Result "清理 .part" (-not (Test-Path (Join-Path $diskA "$liarName.part")))

# ---------- 5. 下载（服务端主动流式推送） ----------
Write-Host "`n[5] 下载 3MB 文件，逐片接收并核对 MD5" -ForegroundColor Yellow
$dl = Send-DownloadInit -Client $aConn -Dir $dirA -FileName $bigName -Resume 0
Test-Result "下载握手 ok，起始 0" ($dl.Ok -and $dl.Start -eq 0) "(ok=$($dl.Ok) start=$($dl.Start))"
Test-Result "服务端报告的总长正确" ($dl.Size -eq $bigBytes.Length) "(size=$($dl.Size))"

$recv = Receive-Download -Client $aConn
Test-Result "下载完成标志 ok" $recv.Ok
Test-Result "收到字节数与源文件一致" ($recv.Bytes.Length -eq $bigBytes.Length) "(recv=$($recv.Bytes.Length))"
Test-Result "重建内容 MD5 一致" ((Get-Md5HexOfBytes $recv.Bytes) -eq $bigMd5)
Test-Result "FINISH 里的整文件 MD5 一致" ($recv.Md5Hex -eq $bigMd5) "(finish=$($recv.Md5Hex))"

# ---------- 6. 下载断点续传 ----------
Write-Host "`n[6] 从 100KB 处续传下载，只应收到后半段" -ForegroundColor Yellow
$resumeAt = 100 * 1024
$dl2 = Send-DownloadInit -Client $aConn -Dir $dirA -FileName $bigName -Resume $resumeAt
Test-Result "服务端接受提议的断点" ($dl2.Ok -and $dl2.Start -eq $resumeAt) "(start=$($dl2.Start))"
$recv2 = Receive-Download -Client $aConn
$expectTail = New-Object byte[] ($bigBytes.Length - $resumeAt)
[Array]::Copy($bigBytes, $resumeAt, $expectTail, 0, $expectTail.Length)
Test-Result "续传收到后半段长度" ($recv2.Bytes.Length -eq $expectTail.Length) "(recv=$($recv2.Bytes.Length))"
Test-Result "续传后半段内容一致" ((Get-Md5HexOfBytes $recv2.Bytes) -eq (Get-Md5HexOfBytes $expectTail))
Test-Result "续传 FINISH 仍报整文件 MD5" ($recv2.Md5Hex -eq $bigMd5)

# ---------- 7. 分享 = 服务端拷贝 + 通知 ----------
Write-Host "`n[7] A 把文件分享给 B -> 服务端拷进 B 的网盘并通知 B" -ForegroundColor Yellow
$shareOk = Send-Share -Client $aConn -Dir $dirA -FileName $smallName -Target $userB
Test-Result "分享应答 ok" $shareOk
$sharedOnDisk = Join-Path $diskB $smallName
Test-Result "B 的网盘里出现该文件" (Test-Path $sharedOnDisk)
if (Test-Path $sharedOnDisk) {
    Test-Result "拷贝内容一致" ((Get-Md5Hex $sharedOnDisk) -eq $smallMd5)
}

$sessB = Reg-Or-Login -User $userB -Pwd $pwd
Test-Result "B 登录成功" ($sessB.Result.Type -eq $LOGIN_RESPOND -and $sessB.Result.ResultBool)
$notice = Try-Read-Pdu -Client $sessB.Client -TimeoutMs 3000
# 分享时 B 并不在线，通知会先落 message 表、由登录后的补发通道送到，
# 即 OFFLINE_MSG_RESEND；若 B 在线则直接收到 CHAT_RESEND。两种都算通过。
Test-Result "B 收到分享通知（在线直投或离线补发）" `
    ($null -ne $notice -and ($notice.Type -eq $CHAT_RESEND -or $notice.Type -eq $OFFLINE_MSG_RESEND)) `
    "(type=$(if ($notice) { $notice.Type } else { 'null' }))"
if ($null -ne $notice) {
    $txt = [System.Text.Encoding]::UTF8.GetString($notice.Body)
    Test-Result "通知里写明分享者与文件名" ($txt -like "*$userA*" -and $txt -like "*$smallName*") "($txt)"
}

# 分享给不存在的用户应失败
$shareBad = Send-Share -Client $aConn -Dir $dirA -FileName $smallName -Target "nobody_$suffix"
Test-Result "分享给不存在的用户 -> 失败" (-not $shareBad)

$aConn.Close()
$sessB.Client.Close()
Start-Sleep -Milliseconds 300

Remove-Item $tmpDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "`n=== 结果: $script:pass 通过 / $script:fail 失败 ===" -ForegroundColor Cyan
if ($script:fail -gt 0) { exit 1 } else { exit 0 }
