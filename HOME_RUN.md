# 在另一台 Windows 电脑运行（2026-10-14）

本分支保存 5a59 最新生产源码，不包含尚未完成的独立槽识别试验。中文操作错误弹窗和 TXT/NC 导出已经集成；真实模型完整槽加工仍在完善，不能视为全部功能验收完成。

## 拉取

```powershell
git clone --branch codex/home-run-20261014 https://github.com/overjjjj/gcodecnc.git
cd gcodecnc
```

已有仓库请先保存本地改动，再 fetch 并切换此分支，不要直接覆盖本地工作。

## 构建环境

当前验证环境为 Windows x64、Qt 5.14.2 MinGW 7.3 64-bit、OpenCascade（MSYS2 mingw64 发行布局）。Qt Creator 打开 untitled/untitled.pro 并选择相同的 64 位 Kit。不要使用 32 位 Kit 或 MSVC 库混搭。

OpenCascade 前缀目录须包含 include/opencascade、lib/libTKDESTEP.dll.a 和 bin 下的运行库。在构建环境设置 CNEXT_OCC_PREFIX 指向该目录。必须确认 qmake 输出启用 OpenCascade；没有该依赖的构建不能用于 STEP 模型加工。第三方 SDK/DLL 不包含在源码 Git 中，需在家中电脑配置相容版本。

以下是与现有验证环境一致的 PowerShell 示例；按本机安装位置修改三个目录：

```powershell
$qtBin = 'D:/qt5/5.14.2/mingw73_64/bin'
$compilerBin = 'D:/qt5/Tools/mingw730_64/bin'
$env:CNEXT_OCC_PREFIX = 'D:/msys64/mingw64'
$env:PATH = "$qtBin;$compilerBin;$env:PATH"
New-Item -ItemType Directory -Force build-home | Out-Null
Set-Location build-home
& "$qtBin/qmake.exe" ../untitled/untitled.pro -spec win32-g++ 'CONFIG+=debug'
if ($LASTEXITCODE -ne 0) { throw 'qmake failed' }
& "$compilerBin/mingw32-make.exe" -f Makefile.Debug -j4
if ($LASTEXITCODE -ne 0) { throw 'build failed' }
& "$qtBin/windeployqt.exe" --debug --compiler-runtime debug/CNEXT-CAM.exe
& ./debug/CNEXT-CAM.exe
```

项目 post-link 会从 CNEXT_OCC_PREFIX/bin 复制已列明的 OCC 及相关 DLL。若缺运行库，应补齐相容依赖，不能从不相容编译器目录随机复制。本次未上传新增的用户 STEP 测试模型，请通过你认可的私有渠道另行带回。项目中旧电脑的绝对模型路径在新电脑需要重新定位。

## 本次验证边界

此次 Git 上传保存源码快照。对应最近独立构建 EXE SHA256：D5153DE8DFFDC1BDBC7A58282CD4DE395BEC1F25887D6C7DDAAF7EA5609D0D81。其构建报告见 docs/handoff-20261014/ACCEPTANCE.md。该报告是编码线程的交付记录，尚待主线程完整独立审核，不是本次重新跑完全部回归的声明。

运行环境没有打入 Git；上述构建步骤尚未在用户家中电脑执行。操作弹窗保留原始错误的详情，遇到问题可复制反馈。用于软件验证，不代表实际机床试切完成。

参考方案存放在 docs/handoff-20261014；其中指向恢复源码及本地验收附件的相对链接仅在原始资料目录有效，恢复源码和验收二进制未附带。

