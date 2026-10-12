# Armory VST 开发计划

本计划以网页原版 `index.html` 的可听功能为基准；导出和文件命名功能不迁移到 VST。

## 当前确认

- 日常验证固定在 JUCE AudioPluginHost；已启动当前 Release 宿主环境，Release VST3 也已通过 `pluginval` 严格级别 10。人工扫描、接线与试听仍待用户界面验收。
- A/B/C 独立锁定音高、共享 MIDI 0–127 钢琴窗、各层开关、五个预设、完整网页控制集、Hold / Shot 以及锁定和弦触发均已实现。
- 声音链已包含原版的 FM frequency-offset 调制、双 LFO Wobble、Shape/Time 包络、动态带通与 Tremolo、并联三段 EQ、HPF/LPF、卷积混响与 -1 dB 限制器。逐预设网页 A/B 听感校准仍是本阶段的主要验收项。

## 开发与验证顺序

声音与交互的日常迭代固定在 JUCE `AudioPluginHost` 虚拟宿主中完成，并直接与网页成品对照。每轮只要求：能构建、能在虚拟宿主扫描/实例化、并通过 `pluginval` 严格级别 10。

REAPER 不参与日常调音或功能迭代。仅当网页对照、虚拟宿主试听和功能验收完成后，才在专用工程中进行一次最终兼容性回归。

## 里程碑 1：声音引擎

1. 为每层加入 Shape 参数，映射为可控的 Attack / Decay / Sustain / Release 包络。
2. 为每层加入 Speed 参数，驱动律动与动态变化。
3. 为每层加入 Air 参数，增加可控的高频空气感与调制滤波质感。
4. 调整 FM 泛音结构、输出余量和保护，避免强叠层时削波。

验收：Sword、Dagger、Greatsword 三个音色在同一 MIDI 音符下具有明确不同的起音、密度和尾音。

## 里程碑 2：音色与混音控制

1. 加入 Low / Mid / High 三段 EQ，保持每层独立并可供宿主自动化。
2. 将页面整理为 SOURCE、MOTION、TONE、SPACE，避免将不相干的参数混在一起。
3. 重新校准五个原版预设的完整参数组合。

验收：每个预设会更新所有相关控制，且每层保留自己的不同设定。

## 里程碑 3：演奏工作流

1. 保持现有单钢琴窗与 A/B/C 独立记忆。
2. 增加当前层的“清除锁定”操作与锁定数量提示。
3. 验证鼠标试听、锁音、多音锁定、外部 MIDI 触发、锁定和弦的 Hold / Shot 触发和工程恢复。

验收：同一外部 MIDI 输入可以稳定触发三层各自的锁定和音色；多音 Shot 能一起释放，不出现粘音。

## 里程碑 4：回归与试听

1. 每次声音链变动均进行 Debug 构建与 pluginval 严格级别 10 验证。
2. 在 JUCE AudioPluginHost 扫描并实例化当前 Release VST3，完成网页原版与 VST 的人工 A/B 试听。
3. 人工验收优先检查：默认 Sword、五个预设差异、A/B/C 叠层、锁定钢琴窗、锁定和弦 Hold / Shot。
4. 仅在以上通过后，将 Release 安装到用户 VST3 目录，并在 `D:\Reaper Project\Armory_VST_Test\Armory_VST_Test.rpp` 做最终兼容性回归。

人工试听是最终调音依据；自动验证只确认稳定性和宿主兼容性。
