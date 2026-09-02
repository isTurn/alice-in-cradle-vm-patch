param(
    [Parameter(Mandatory = $true)][string]$DllPath
)
$ErrorActionPreference = 'Stop'

$tool = Join-Path $PSScriptRoot 'PatchTool.exe'

if (-not (Test-Path $DllPath)) {
    Write-Host "[错误] 找不到文件: $DllPath" -ForegroundColor Red
    exit 1
}
if (-not (Test-Path $tool)) {
    Write-Host "[错误] 找不到 PatchTool.exe（它必须与本脚本放在同一目录）" -ForegroundColor Red
    exit 1
}

$full = (Resolve-Path $DllPath).Path
$dir  = Split-Path $full -Parent
$orig = Join-Path $dir 'unsafeAssem.dll.orig'
$tmp  = Join-Path $env:TEMP ("aic_patched_" + [System.IO.Path]::GetRandomFileName() + ".dll")

Write-Host "== Alice in Cradle 虚拟机检测去除工具 ==" -ForegroundColor Cyan
Write-Host "目标文件: $full"

# 1) 备份原版
if (-not (Test-Path $orig)) {
    Copy-Item $full $orig -Force
    Write-Host "[1/3] 已备份原版 -> $orig" -ForegroundColor Green
}
else {
    Write-Host "[1/3] 备份已存在（$orig），跳过备份" -ForegroundColor Yellow
}

# 2) 打补丁（输出到临时文件，避免文件占用）
Write-Host "[2/3] 正在打补丁 ..."
& $tool $full $tmp
if ($LASTEXITCODE -eq 2) {
    Write-Host "[提示] 该文件看起来已打过补丁（或检测逻辑已变化），无需重复操作。" -ForegroundColor Yellow
    Remove-Item $tmp -Force -ErrorAction SilentlyContinue
    exit 0
}
if ($LASTEXITCODE -ne 0) {
    Write-Host "[错误] 打补丁失败。可能游戏更新后检测逻辑已变化，请保留以上输出联系处理。" -ForegroundColor Red
    Remove-Item $tmp -Force -ErrorAction SilentlyContinue
    exit 1
}
if (-not (Test-Path $tmp)) {
    Write-Host "[错误] 未生成补丁产物，已中止。" -ForegroundColor Red
    exit 1
}

# 3) 应用并清理
Copy-Item $tmp $full -Force
Remove-Item $tmp -Force
Write-Host "[3/3] 补丁已应用 -> $full" -ForegroundColor Green
Write-Host ""
Write-Host "完成！虚拟机/模拟环境检测已去除。" -ForegroundColor Green
Write-Host "原版备份: $orig" -ForegroundColor Green
Write-Host "如需还原，将备份复制回 unsafeAssem.dll 即可。" -ForegroundColor DarkGray
