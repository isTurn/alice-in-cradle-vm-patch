# Alice in Cradle 虚拟机检测去除 —— 完整记忆文件

> 用途：记录整个「去除 Alice in Cradle 试玩版虚拟机/模拟环境检测」任务的全过程，包括背景、原理、工具、步骤、数据、踩坑与发布信息。**可在任意设备/任意 AI 会话中复用**，按本文件即可复现与继续维护。
>
> 最后更新：2026-09-03

---

## 1. 任务概述

- **目标**：让 Alice in Cradle 试玩版（Unity 游戏）能在虚拟机 / 云游戏 / 安卓模拟器等环境下启动。
- **根因**：游戏自 **v0.29** 起加入了「虚拟机/模拟环境检测」（见游戏自带 `README/readme_v030.txt` 第 7 节「仮想マシンでの動作について」：出于安全原因禁止在虚拟机上启动）。
- **方案**：对游戏托管程序集 `unsafeAssem.dll` 做 IL 级补丁，使环境检测结果失效。**不改 exe**，不影响正版完整性校验。
- **现状**：已补丁完成并验证可正常启动。工具与流程已发布到 GitHub（public）。

---

## 2. 游戏环境检测机制

- 检测代码位于 `unsafeAssem.dll` → 类型 `XX.IN` → 方法 `Awake()`（游戏启动时最先执行的核心初始化）。
- 检测逻辑（反编译后形态，命中任一即 `flag = false` → 加载 `SceneCannotLaunch_InvalidEnviroment` 场景拦截）：

```csharp
string gpu = SystemInfo.graphicsDeviceName.ToLower();
bool flag2 = (Directory.Exists("E:\\Android\\data") && Directory.Exists("Z:\\"))  // 新版；旧版为 E:\DCIM
          || File.Exists("C:\\Windows\\wfm.exe")
          || File.Exists("Z:\\opt\\apps\\winaddons.exe")
          || File.Exists("Z:\\opt\\apps\\GPUInfo.exe");
if (gpu.Contains("virgl") || gpu.Contains("turnip") || gpu.Contains("vortex") || gpu.Contains("gamefusion") || flag2)
{
    flag = false;   // ← 命中检测 → 拦截
}
if (!flag) { SceneManager.LoadScene("SceneCannotLaunch_InvalidEnviroment"); ... }
```

- 检测特征：`virgl`（QEMU 虚拟 GPU）、`turnip`（安卓模拟器 Vulkan 驱动）、`vortex`、`gamefusion`（安卓模拟器）、模拟器/云游戏特征路径。

---

## 3. 补丁原理

- **只改 `unsafeAssem.dll`**，把环境检测分支里「置 `flag = false`」的两条 IL 指令替换为 `nop`：

```
IL_0293: ldc.i4.0      →  nop
IL_0294: stloc.0       →  nop
```

- 1:1 字节替换（`nop` 也是 1 字节），**不改变代码布局/偏移/相对跳转**，方法体积不变。
- 效果：检测逻辑仍在执行但结果不再生效，游戏正常进入初始化（`REG.initReg()` 等）。
- **刻意保留**（不修改）：
  - exe 的 SHA1 / ProductVersion 完整性校验分支（`text != "2022.3.62f2 (7670c08855a9)"` → flag=false）——补丁只动 DLL 不动 exe，正版校验照常通过。
  - `checkLocalFileAvailable()`（检查 `mti_shader.dat`）数据文件检查。

---

## 4. 工具清单与位置

| 工具/文件 | 作用 | 位置 |
|---|---|---|
| `PatchTool.exe` | 补丁工具（独立单文件，含 .NET 运行时，免安装） | `Work\AliceCradle_VMPatch\` 及 GitHub 仓库 |
| `patch.bat` | 一键批处理（拖拽 DLL 到其上即可） | 同上 |
| `patch.ps1` | 一键 PowerShell 脚本（底层实现） | 同上 |
| `src\Program.cs` + `src\PatchTool.csproj` | PatchTool 源码（dnlib 4.5.0） | 同上 |
| `README.md` | 使用说明 | 同上 |
| `unsafeAssem.dll.orig` | 原版备份（与补丁版同目录，可还原） | 游戏 `AliceInCradle_Data\Managed\` |

- 一键脚本流程：备份 `.orig`（仅首次）→ PatchTool 打补丁到临时文件 → 校验 → 覆盖回游戏目录。
- GitHub 仓库（public）：**https://github.com/isTurn/alice-in-cradle-vm-patch**
- 本地克隆副本：`_aic_gh_repo\`（游戏目录内，可用 `git pull` 同步）。

---

## 5. 完整操作流程（从零复现）

### 5.1 一次性准备
1. 需要 .NET 8 SDK（构建 PatchTool）与 Python（可选，仅分析用）。
2. 获取游戏新版 `unsafeAssem.dll`（游戏更新后它是原版）。

### 5.2 构建 PatchTool（若手上没有 exe）
```powershell
cd src
dotnet publish -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -o .\out
# 产物 .\out\PatchTool.exe，与 patch.ps1 放同一目录
```
（或直接从 GitHub 仓库下载现成 `PatchTool.exe`。）

### 5.3 一键打补丁
- 方式 A：把新版 `unsafeAssem.dll` 拖到 `patch.bat` 上
- 方式 B：`patch.bat "完整路径\unsafeAssem.dll"`
- 方式 C：`powershell -ExecutionPolicy Bypass -File patch.ps1 -DllPath "完整路径"`

脚本自动：备份原版 → 打补丁 → 应用。

### 5.4 验证
- 反编译确认：`unsafeAssem.dll` 的 `XX.IN.Awake` 中检测分支不再含 `flag = false`（变为空 if），版本校验分支保留。
- 运行游戏：能正常进入标题/游戏画面、无 `SceneCannotLaunch_InvalidEnviroment`。

### 5.5 还原
把 `unsafeAssem.dll.orig` 复制回 `unsafeAssem.dll` 覆盖即可。

---

## 6. 游戏更新后重新打补丁

1. 正常更新/替换游戏（新版 `unsafeAssem.dll` 覆盖旧补丁，正常现象）。
2. 对新的 `unsafeAssem.dll` 重新运行 `patch.bat`（同 5.3）。
3. **工具用模式识别定位，不依赖固定偏移**：查找方法内存在 `"virgl"` 特征串 + `ldc.i4.0; stloc.0; ldloc.0` 指令模式。只要新版还保留这套检测结构即可直接复用。
4. 若提示「`'virgl' detection string not found` / could not locate pattern」= 新版检测逻辑结构已变化，**勿强行使用**，需重新分析。

---

## 7. 关键数据速查（2026-09-03 当前状态）

> 注：此前的旧版(08-30) 目录与新版(09-01) 嵌套目录已被整理合并，**当前游戏目录 = 新版 + 新版配套补丁**。

| 项目 | 值 |
|---|---|
| 游戏根目录 | `D:\Download\Alice in Cradle Trial Version` |
| Unity 版本 | 2022.3.62f2 (7670c08855a9) |
| 已部署 `unsafeAssem.dll`（补丁版） | 2,661,376 B · SHA1 `84751f2b36bf7ebda51b68308c26277f1309fdf6` |
| 原版备份 `unsafeAssem.dll.orig` | 2,668,032 B · SHA1 `7b9438a061150bb04ad6366b70f9a5a716c638f0` |
| `AliceInCradle.exe` | 12,957,184 B · SHA1 `413de6874473898b...` |
| `Assembly-CSharp.dll`（新版） | 5,596,672 B · SHA1 `ed4a9e09d7189455...` |
| `spine-unity.dll`（新版） | 352,768 B · SHA1 `0a7dc582732c96e3...` |
| 检测点 | `XX.IN.Awake` 的 `IL_0293/0294`（新旧两版相同） |
| exe 完整性校验值 | SHA1 `41-3D-E6-87-44-73-89-8B-BC-75-DB-30-0D-6D-D4-C4-CB-9F-FE-24` |
| 版本校验串 | `2022.3.62f2 (7670c08855a9)` |

**历史版本参考**（已合并进当前，供回溯）：
- 旧版(08-30) 原版：2,665,984 B · SHA1 `48fbcaf9...`；其补丁版 2,659,328 B · SHA1 `aa90ad38...`
- 新版(09-01) 原版：即上方 `.orig`（2,668,032 B · `7b9438a0...`）

---

## 8. 踩坑记录（重要经验）

1. **dnlib 内存映射导致文件占用**：`AssemblyDef.Load(src)` 会内存映射源文件，**源和目标不能是同一路径**。打补丁须输出到临时文件再覆盖（一键脚本已处理）。
2. **PowerShell 5.1 编码**：`.ps1` 含中文必须存为 **UTF-8 with BOM**，否则按系统 ANSI(GBK) 解析会报「字符串缺终止符/花括号不匹配」。重新保存：`[IO.File]::WriteAllText(path, txt, (New-Object Text.UTF8Encoding $true))`。`.bat` 提示文字用英文避免乱码。
3. **PowerShell 传空字符串参数会被吞**：`& exe src "" dump` 中 `""` 不会传给程序，需用占位符（如 `"x"`）。
4. **模式识别优于硬编码偏移**：定位检测点用指令模式（`ldc.i4.0;stloc.0;ldloc.0` + `"virgl"` 特征串），避免游戏更新后偏移变化导致失效。版本校验分支的 flag=false 后跟 `br` 而非 `ldloc.0`，可据此区分。
5. **幂等处理**：对已补丁文件再次运行 → 工具返回 exit code 2，脚本识别并提示「已打过补丁」，不报错。
6. **UnityPlayer.dll 中的 VMWare/VirtualBox/Parallels 字符串是 Unity 引擎自带 GPU 厂商表**，非游戏 VM 检测，勿误判。
7. **大文件传 GitHub**：68MB 的 exe 不能走 Contents API/push_files（content 参数是文本），需用**本地 git + gh 凭据推送**（GitHub 单文件硬限制 100MB，68MB 可推，仅有 >50MB 提示）。
8. **gh 改仓库可见性**：需加 `--accept-visibility-change-consequences` 标志。
9. **dnlib 重写会改变整个 PE 布局**：字节级 diff 大量不同是正常现象（重写元数据/段布局），以反编译逻辑验证为准。

---

## 9. GitHub 发布信息

- 仓库：**https://github.com/isTurn/alice-in-cradle-vm-patch**（**public**）
- 账号：`isTurn`（gh CLI 已认证，token scopes 含 repo）
- 内容：`PatchTool.exe`(68.7MB) + `README.md` + `patch.bat` + `patch.ps1` + `src\`（源码）
- 本地 git 凭据走 gh：`credential.https://github.com.helper = gh auth git-credential`
- 本地克隆：`D:\Download\Alice in Cradle Trial Version\_aic_gh_repo`

---

## 10. 目录结构（当前，2026-09-03）

```
D:\Download\Alice in Cradle Trial Version\   （游戏根 = 新版 + 配套补丁）
├── AliceInCradle.exe / UnityPlayer.dll / UnityCrashHandler64.exe
├── AliceInCradle_Data\
│   ├── Managed\unsafeAssem.dll        （补丁版，已生效）
│   ├── Managed\unsafeAssem.dll.orig   （原版备份）
│   └── ...（新版数据）
├── MonoBleedingEdge\
├── Work\                               （工作区）
│   ├── AliceCradle_VMPatch\            （补丁工具包：PatchTool.exe / patch.bat / patch.ps1 / README / src）
│   ├── _aic_decomp*                    （反编译源码，供分析，可删）
│   └── _aic_patch\                     （补丁工程 + 中间产物）
└── _aic_gh_repo\                       （GitHub 仓库本地克隆）
```

---

## 11. 后续维护建议

- 游戏更新 → 对新 `unsafeAssem.dll` 跑 `patch.bat`（见第 6 节）。
- 工具若报检测逻辑变化 → 用 `PatchTool.exe <dll> <out> dump` 转储 `XX.IN.Awake` 的 IL，重新定位 `flag=false` 位置并更新工具模式。
- 本地 `_aic_gh_repo` 可用 `git pull` 同步 GitHub 最新内容。
- 反编译临时目录（`Work\_aic_decomp*`）无需保留，可按需删除。
