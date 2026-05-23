# 压缩指定目录下的 DeMic-x64.exe、..\..\cli.md、..\..\config.md 和 plugin\*.plugin 到 DeMic-x64.zip 中

# 如果执行报错 “无法加载文件 \Compress.ps1，因为在此系统上禁止运行脚本。”，可执行：
# Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned
# 为当前用户开启本地 PowerShell 脚本执行权限（从网络下载的脚本依然需要签名）。

# 1. 设置参数，默认为当前目录
param (
    [string]$TargetDir = "."
)

# 确保路径是绝对路径
$TargetDir = Resolve-Path $TargetDir

# 2. 定义文件名和路径
$ZipName = "DeMic-x64.zip"
$ZipPath = Join-Path $TargetDir $ZipName
$TempPath = Join-Path $env:TEMP "DeMic_Build_Temp_$(Get-Random)"
$InnerDir = Join-Path $TempPath "DeMic-x64"

# 3. 清理已存在的 ZIP 文件（如果存在）
if (Test-Path $ZipPath) {
    Remove-Item $ZipPath -Force
    Write-Host "已删除旧的 $ZipName" -ForegroundColor Cyan
}

try {
    Write-Host "正在准备临时目录结构..." -ForegroundColor Gray
    
    # 创建目录结构：Temp\DeMic-x64\plugin
    New-Item -ItemType Directory -Path (Join-Path $InnerDir "plugin") -Force | Out-Null

    # 4. 复制主程序
    $ExeFile = Join-Path $TargetDir "DeMic-x64.exe"
    if (Test-Path $ExeFile) {
        Copy-Item $ExeFile $InnerDir
    } else {
        Write-Error "找不到文件: $ExeFile"
        return
    }

    #5. 复制 cli.md 和 config.md
    $CliMDFile = Join-Path $TargetDir "..\..\cli.md"
    if (Test-Path $CliMDFile) {
        Copy-Item $CliMDFile $InnerDir
    } else {
        Write-Error "找不到文件: $CliMDFile"
        return
    }
    $ConfigMDFile = Join-Path $TargetDir "..\..\config.md"
    if (Test-Path $ConfigMDFile) {
        Copy-Item $ConfigMDFile $InnerDir
    } else {
        Write-Error "找不到文件: $ConfigMDFile"
        return
    }

    # 6. 复制插件文件
    $PluginSource = Join-Path $TargetDir "plugin\*.plugin"
    Copy-Item $PluginSource (Join-Path $InnerDir "plugin")

    # 7. 执行压缩
    Write-Host "正在生成 $ZipName..." -ForegroundColor Green
    Compress-Archive -Path "$InnerDir" -DestinationPath $ZipPath -Force

    Write-Host "完成！压缩包位置: $ZipPath" -ForegroundColor Green

} catch {
    Write-Error "打包过程中出现错误: $_"
} finally {
    # 8. 清理临时文件
    if (Test-Path $TempPath) {
        Remove-Item $TempPath -Recurse -Force
    }
}