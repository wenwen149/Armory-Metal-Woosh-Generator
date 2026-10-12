# Armory 本机试听设置

本项目使用 JUCE AudioPluginHost 进行快速试听时，插件图路由必须落到实际监听设备的立体声输出；图中的 `Audio Output` 仅是宿主出口，不代表 Windows 的默认播放设备。

## 当前工作站

- 已连接 MOTU 声卡：选择 `MOTU 1-2`。
- 未连接声卡、使用蓝牙音箱：选择 `HOCOHC`。该设备会被系统显示为耳机。
- 不要选择 `25G3ZM`：这是显卡送往显示器的音频输出，当前没有连接扬声器。
- 不要把 `High Definition Audio` 当作 JUCE 试听输出：它适合系统提示音和普通应用；在需要显式硬件路由的 JUCE 宿主中会导致没有可听输出。

设备选择属于本机运行环境，不能写入 VST 插件的默认行为或预设。每次人工试听前，在 AudioPluginHost 的 Audio Settings 中确认以上输出选择。
