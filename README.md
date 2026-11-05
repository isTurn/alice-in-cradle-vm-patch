# Alice in Cradle — 虚拟机 / 模拟器检测去除工具

> 原版基于 .NET 8 + dnlib，需要 PowerShell 和 .NET 运行时，在 Wine / box64（Winlator、Termux-Wine、Android 模拟器）里跑不起来。
> 现在整个工具已重写为**单个原生 exe，静态链接 C，只导入 KERNEL32.dll**，不需要 .NET、不需要 PowerShell、不需要任何运行时。

---

## 背景

Alice in Cradle 试玩版自 **v0.29** 起加入了「虚拟机 / 模拟环境检测」：游戏在 `unsafeAssem.dll` 的
`XX.IN.Awake()` 启动初始化里检查当前环境，一旦命中就加载 `SceneCannotLaunch_InvalidEnviroment`
拦截场景，导致游戏无法在虚拟机、云游戏、Android 模拟器（Winlator / Termux）等环境下启动。

检测依据（命中即拦截）：

- 显卡名（`SystemInfo.graphicsDeviceName`）包含 `virgl` / `turnip` / `vortex` / `gamefusion`
- 存在模拟器 / 云游戏特征路径：
  - `E:\Android\data` 且 `E:\DCIM`（旧版）或 `Z:\`（新版）
  - `C:\Windows\wfm.exe`
  - `Z:\opt\apps\winaddons.exe`
  - `Z:\opt\apps\GPUInfo.exe`

## 补丁原理

只修改 `unsafeAssem.dll` 一个文件。

工具在 `XX.IN::Awake()` 的方法体内部定位下面三条指令：

```
ldc.i4.0        ; flag = false     ← 环境检测命中时走这里
stloc.0
ldloc.0
```

然后把前两条替换为 `nop`（**1:1 字节替换**，不移动任何元数据、不改变 RVA、文件大小完全不变）：

```
nop
nop
ldloc.0
```

效果：检测逻辑仍然照常执行，但**结果不再生效**，游戏正常进入初始化流程。

刻意保留、不动的内容：

- exe 的 SHA1 / 版本完整性校验分支 **不修改**（补丁只动 DLL 不动 exe，正版校验照常通过）
- `checkLocalFileAvailable()` 数据文件检查不修改

### 为什么不会误伤别的方法

`ldc.i4.0 ; stloc.0` 在程序集里并不唯一（版本完整性校验分支也会置 flag=false），
但**后面紧接 `ldloc.0`** 的这个三指令组合在 `XX.IN::Awake()` 内唯一。
工具同时校验两个条件：

1. 方法体里确实存在 `ldstr "virgl"`（确认这是环境检测方法，防止游戏更新后误判）
2. `ldc.i4.0 ; stloc.0 ; ldloc.0` 三指令组合在该方法体内**恰好命中一次**（命中多次会直接报错退出，不猜）

## 文件说明

| 文件 | 作用 |
|---|---|
| `PatchAIC.exe` | 补丁工具本体。原生 x64，静态链接，仅依赖 `KERNEL32.dll` |
| `patch.bat` | 一键批处理（可直接把 DLL 或游戏目录拖到上面） |
| `src/PatchAIC.c` | 完整源码（单文件，手写 .NET metadata 解析，无第三方库） |
| `src/build.bat` | 构建脚本（自动定位 MSVC） |
| `unsafeAssem.dll` | 已打过补丁的成品 DLL（2,661,376 B） |
| `README.md` | 本文档 |

## 使用方法

### 方法 A — 拖拽（Windows）

把 `unsafeAssem.dll`（或整个游戏根目录）拖到 `patch.bat` 上，松手即可。

### 方法 B — 命令行

```bat
patch.bat "D:\Game\AliceInCradle_Data\Managed\unsafeAssem.dll"
patch.bat "D:\Game"
```

### 方法 C — 直接运行 exe（**Winlator / Wine 推荐**）

把 `PatchAIC.exe` 复制到游戏根目录，然后双击运行。不带任何参数时，工具会自己按顺序找：

1. exe 同目录下的 `unsafeAssem.dll`
2. `AliceInCradle_Data\Managed\unsafeAssem.dll`
3. `..\AliceInCradle_Data\Managed\unsafeAssem.dll`
4. `..\..\AliceInCradle_Data\Managed\unsafeAssem.dll`
5. `Data\Managed\unsafeAssem.dll`
6. `Managed\unsafeAssem.dll`
7. 以上都找不到时，从 exe 所在目录起**递归向下找 5 层**

因此把 `PatchAIC.exe` 丢在游戏根目录（和 `AliceInCradle.exe` 同级）直接运行是最省事的方式——
**不需要拖拽，不需要命令行参数**，这正是 Winlator 上原本做不到的操作。

如果全都没找到，工具会打印它搜索的起始目录，并提示手动输入完整路径。

### 其他命令行选项

```bat
PatchAIC.exe --check   [dll]    只检查状态，不做任何修改
PatchAIC.exe --restore [dll]    从 unsafeAssem.dll.orig 还原
PatchAIC.exe <in.dll> <out.dll> 补丁输出到新文件，不动原文件
PatchAIC.exe --help
```

### 退出码

| 码 | 含义 |
|---|---|
| `0` | 打补丁成功（或 `--restore` 成功） |
| `2` | 该文件已经打过补丁，无需重复操作 |
| `3` | 找不到文件 / 找不到特征码 / 游戏版本结构已变化 |
| `1` | 读写失败（文件被占用、权限不足等） |

对同一个文件重复运行是**安全且幂等**的：第二次运行会识别为「已打过补丁」并以 `2` 退出。

## 还原方法

```bat
PatchAIC.exe --restore
```

或者手工把同目录下的 `unsafeAssem.dll.orig` 复制回 `unsafeAssem.dll`。
备份只在第一次打补丁时创建，之后不会覆盖，所以 `.orig` 始终是最初的原始文件。

## 在 Winlator / Wine 上使用

Winlator 容器里没有 .NET 运行时，也没有能用的 PowerShell，所以原版 `patch.bat`
（依赖 `powershell -File patch.ps1` + dnlib 版 `PatchTool.exe`）在里面完全跑不通，
而且 Winlator 的文件管理器不支持「把文件拖到 .bat 上」这种 Windows 资源管理器专属操作。

本版本的做法：

1. 把 `PatchAIC.exe` 复制到游戏根目录（Winlator 里通常位于 `Z:\storage\emulated\0\...` 之类的路径）
2. 在 Winlator 的文件管理器里**双击** `PatchAIC.exe`
3. 看到 `PATCHED OK` 后回车退出，启动游戏

不需要参数、不需要拖拽、不需要 PowerShell、不需要 .NET。

> 已在 Winlator 实机验证通过（打补丁后 DLL 字节变更正确，游戏可正常启动）。

## 从源码构建

需要 Visual Studio 2022（或 Build Tools）的「使用 C++ 的桌面开发」工作负载。

```bat
cd src
build.bat
```

产物：`_out\PatchAIC.exe`（约 178 KB）

`build.bat` 会依次尝试：当前已初始化的开发者环境 → `vswhere.exe` → 常见安装路径（VS 2022/2019 × Community/Professional/Enterprise/BuildTools）。
也可以直接在「x64 Native Tools Command Prompt for VS 2022」里执行等价的 `cl` 命令：

```bat
cl /nologo /O2 /MT /GS- /W3 /D_CRT_SECURE_NO_WARNINGS ^
   /Fo:..\_out\ /Fe:..\_out\PatchAIC.exe PatchAIC.c ^
   /link /SUBSYSTEM:CONSOLE /MACHINE:X64
```

`/MT` 静态链接 CRT，这是产物能只依赖 `KERNEL32.dll`、能在 Wine 下直接跑的关键。

## 已验证版本

| 版本 | unsafeAssem.dll 大小 | 检测点 |
|---|---|---|
| 2026-08-30 版 | 2,665,984 B | IL_0293 / IL_0294 |
| 2026-09-01 版 | 2,668,032 B | IL_0293 / IL_0294 |
| 2026-09-05 版 | 2,672,128 B | IL_02B9 / IL_02BA |

工具采用**结构识别**（先定位类型 `XX.IN` → 方法 `Awake` → 校验 `"virgl"` 字面量 → 再在方法体内找三指令组合），
**不依赖固定文件偏移**，因此游戏小幅更新后通常可直接复用。

## 排错

| 输出 | 含义 / 处理 |
|---|---|
| `[!] unsafeAssem.dll was not found automatically` | 自动搜索失败。把 exe 放到游戏根目录，或按提示输入完整路径 |
| `[!] type XX.IN not found` / `method XX.IN::Awake not found` | 游戏代码结构已变，工具未适配该版本 |
| `[!] env-check pattern ... not found` | 文件可能已打过补丁，或检测逻辑被改写。先用 `--check` 确认 |
| `[=] ALREADY patched` | 已经是补丁状态，无需操作 |
| `[!] write failed` | 文件被游戏 / 杀软占用，或没有写权限 |

每次运行都会在同目录写一份 `PatchAIC.log`，内容与屏幕输出一致，方便存档排查。

## 注意

- 工具只应作用于 `unsafeAssem.dll`，不要对其他 DLL 或 exe 使用。
- 请仅在你拥有合法副本的前提下使用。
