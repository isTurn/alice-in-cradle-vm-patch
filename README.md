# Alice in Cradle 虚拟机检测去除工具

## 背景

Alice in Cradle 试玩版自 **v0.29** 起加入了"虚拟机/模拟环境检测"：游戏在 `unsafeAssem.dll` 的
`XX.IN.Awake()` 启动初始化里检查当前环境，一旦命中就加载 `SceneCannotLaunch_InvalidEnviroment`
拦截场景，导致游戏无法在虚拟机、云游戏、Android 模拟器等环境下启动。

检测依据（命中即拦截）：
- 显卡名（`SystemInfo.graphicsDeviceName`）包含 `virgl` / `turnip` / `vortex` / `gamefusion`
- 存在模拟器/云游戏特征路径：
  - `E:\Android\data` 且 `E:\DCIM`（旧版）或 `Z:\`（新版）
  - `C:\Windows\wfm.exe`
  - `Z:\opt\apps\winaddons.exe`
  - `Z:\opt\apps\GPUInfo.exe`

## 补丁原理

仅修改 `unsafeAssem.dll` 一个文件。在 IL 层面把环境检测分支里"置 `flag = false`"的两条指令
（`ldc.i4.0 ; stloc.0`）替换为 `nop`（1:1 字节，不改变代码布局）。

效果：检测逻辑仍在执行，但**结果不再生效**，游戏正常进入初始化流程。

刻意保留的部分：
- exe 的 SHA1 / 版本完整性校验分支 **不修改**（补丁只动 DLL 不动 exe，正版校验照常通过）
- `checkLocalFileAvailable()` 数据文件检查不修改

## 文件说明

| 文件 | 作用 |
|---|---|
| `PatchTool.exe` | 补丁工具（独立单文件，无需安装 .NET；本仓库不直接存放该二进制） |
| `src/PatchTool.csproj` | 补丁工具源码工程 |
| `src/Program.cs` | 补丁工具源码 |
| `patch.bat` | 一键批处理（可直接拖拽 DLL 到其上） |
| `patch.ps1` | 一键 PowerShell 脚本（bat 的底层实现，也可单独用） |
| `README.md` | 本文档 |

## 从源码构建 PatchTool

仓库未直接存放 `PatchTool.exe`（二进制不适合版本管理）。按以下命令构建（需安装 .NET 8 SDK）：

```powershell
cd src
dotnet publish -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -o .\out
# 产物为 .\out\PatchTool.exe，放入本目录（与 patch.ps1 同级）即可
```

## 使用方法（游戏更新后重新打补丁）

1. 先按正常方式**更新/替换游戏**（新版的 `unsafeAssem.dll` 会覆盖旧补丁，属正常现象）。
2. 对游戏目录里的新 `unsafeAssem.dll` 执行补丁：
   - **方式 A**：把新 `unsafeAssem.dll` 直接拖到 `patch.bat` 上
   - **方式 B**：命令行运行
     `patch.bat "D:\游戏路径\AliceInCradle_Data\Managed\unsafeAssem.dll"`
   - **方式 C**：`powershell -ExecutionPolicy Bypass -File patch.ps1 -DllPath "完整路径"`
3. 脚本会自动完成：
   - 备份原版为 `unsafeAssem.dll.orig`（同目录，仅首次备份）
   - 打补丁到临时文件 → 校验 → 覆盖回去
4. 启动游戏确认可正常运行。

## 还原方法

把 `unsafeAssem.dll.orig` 复制回 `unsafeAssem.dll` 覆盖即可。

## 已验证版本

| 版本 | unsafeAssem.dll 大小 | 检测点 |
|---|---|---|
| 2026-08-30 版（旧） | 2,665,984 B | IL_0293/0294 |
| 2026-09-01 版（新） | 2,668,032 B | IL_0293/0294 |

工具采用**模式识别**（查找 `ldc.i4.0;stloc.0;ldloc.0` 且方法内存在 `"virgl"` 特征串）定位检测点，
**不依赖固定偏移**，因此游戏更新后通常可直接复用。

## 注意事项

- 若运行后提示 `ERROR: could not locate ...` / `'virgl' detection string not found`，
  说明新版游戏的检测逻辑结构已变化，**不要强行使用**——请保留工具输出，联系处理重新分析。
- 若提示"该文件看起来已打过补丁"，说明当前 `unsafeAssem.dll` 已是补丁状态，无需重复操作
  （对同一文件重复运行是安全的，脚本会跳过）。
- 工具只应作用于 `unsafeAssem.dll`；不要对其他 DLL 或 exe 使用。
- 请仅在你拥有合法副本的前提下使用。
