# Armory VST

Armory Metal Whoosh Generator 的 Windows x64 VST3 版本。此分支将网页版的三模块金属 FM Drone / Whoosh 概念带入 DAW：保留核心声源、设计、混音、随机、预设与同步逻辑；不包含网页端的独立音频导出功能。

## 1.0.0 功能

- VST3 乐器：立体声输出、接收 MIDI，并在 REAPER 中作为 `VST3i: Armory VST` 使用。
- 三个可独立启用的 A / B / C 金属 FM 层：各层具有独立的金属比、双速音高摆动、时长、输出、高低通、混响和立体声宽度，全部参数均可由宿主自动化。
- 单一钢琴窗配合左侧 A / B / C 拨杆：每层分别记忆锁定音高。左键短按完成一次 ADSR，长按持续至松开；右键锁定或取消多个琴键。收到 MIDI 时，有锁定音高的层演奏锁定集合，否则跟随原始 MIDI 音高。
- 抽屉式迷你钢琴窗：可展开或收起；Zoom 滑杆、加减按钮与鼠标滚轮用于在完整音域中快速定位和精细操作。
- Sword、Dagger、Greatsword、Katana、Chain 五种分层预设；随机化只影响当前层，Sync 仅由 Core 控制，副层跟随其速度和时长。
- 参数值可直接输入、垂直拖拽调整、双击复位；锁定音符和全部参数随宿主工程保存与恢复。

## 本机构建

本项目默认使用 `D:/ArmoryVSTTools/JUCE`。工作区路径含中文时，JUCE 9 的 `juceaide` 可能在资源生成阶段异常退出；请从项目根目录运行下列脚本。脚本会通过 ASCII 路径入口构建，源码仍保留在工作区。

```powershell
.\scripts\build-debug.ps1
```

Debug VST3 输出目录：

`D:\ArmoryVSTTools\build\ArmoryVST\ArmoryVST_artefacts\Debug\VST3\`

生成可安装的优化版本：

```powershell
.\scripts\build-release.ps1
```

Release VST3 输出目录：

`D:\ArmoryVSTTools\build\ArmoryVST-Release\ArmoryVST_artefacts\Release\VST3\`

## 验证与宿主

`pluginval` 严格级别 5 已通过；Release 版本也已在 REAPER 7.79 专用工程中成功创建 VST3i 实例。`tests\reaper\verify-armory-vst.lua` 仅面向 `D:\Reaper Project\Armory_VST_Test\Armory_VST_Test.rpp`，不会修改其它 REAPER 工程。

无需启动 REAPER 的第一层验证可使用 JUCE AudioPluginHost：

```powershell
.\scripts\open-virtual-host.ps1
```

在 AudioPluginHost 的 VST3 扫描路径中加入脚本显示的 Release 目录，再将 **Armory VST** 放入图形并连接 MIDI 与音频输出。

## 分支说明

- `main`：网页版 Armory Metal Whoosh Generator。
- `vst`：原生 JUCE / VST3 产品源码与本说明文档。

## 授权

本项目保留原始 MIT 许可证。JUCE 9 使用 AGPLv3 或商业授权；分发闭源或商业版本前，必须选择并满足相应授权条件。
