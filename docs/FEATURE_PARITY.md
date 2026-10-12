# Armory VST 功能复刻清单

唯一允许的产品差异：

- 原版 `Pitch` 频率参数替换为一个全音域钢琴窗；A/B/C 三层分别记忆锁定音高。
- 删除文件命名、渲染时长和 48/96 kHz 独立导出。

## 三层通道条

- [x] 独立启用 / 禁用
- [x] Sword / Dagger / Greatsword / Katana / Chain 预设
- [x] Shape、Time、Metal、Wobble、Speed、Air
- [x] Low / Mid / High EQ、HPF、LPF、Width、Verb、Gain
- [ ] 三条通道同时可见；结构已对应网页 v1.2，精确视觉待人工验收
- [x] 参数双击恢复原版默认值
- [x] 随机化（仅随机 Pitch 替代项、Metal、Wobble、Shape、Time、Speed、Air）
- [x] Core 的 Speed / Time 联动开关及关闭后恢复 B/C 原值
- [x] 每层 Hold / Shot 模式与试听触发器
- [x] Shape 包络小图实时更新

## 主控

- [x] Master Hold / Shot 模式与总触发器
- [x] 实时频谱显示
- [x] -1 dB 输出限制器

## 钢琴窗

- [x] 左键按住试听，松开停止
- [x] 右键锁定 / 再次右键取消
- [x] A/B/C 分层记忆多个锁定音高
- [x] MIDI 0–127 全音域横向钢琴窗
- [x] 八度快速定位与水平滚动
- [x] 显示当前层、锁定数量与清除当前层锁定操作
- [x] 加载预设时，以原版 Pitch 频率对应的最近 MIDI 音符更新当前层音高

## 声音链

- [x] FM harmonicity = Metal，modulation index = Metal × 3
- [x] 双 LFO Wobble（0.2 Hz / 3.1 Hz）
- [x] Shape / Time 包络映射
- [x] Speed 驱动动态带通与 Tremolo
- [x] Air 控制动态带通 Q 与湿度
- [x] Width 控制左右 Tremolo 相位差
- [x] EQ3 → HPF → LPF → Reverb → Gain
- [ ] 逐预设与网页原版 A/B 听感校准

## 验证顺序

1. JUCE AudioPluginHost 中扫描、实例化和人工试听。
2. pluginval 严格级别 10 稳定性回归。
3. 功能与听感定型后，最后才进入 REAPER 专用工程做兼容性验证。
