<p align="right">
  <strong>English</strong> · [简体中文](README.zh_CN.md)
</p>

# 像素习惯打卡

一个运行在 [FoloToy AI Passport](https://github.com/FoloToy/ai-passport)
（ESP32-C3，240×320 屏幕，三个实体按键）上的离线像素风习惯打卡应用。
本仓库是我对上游项目的 fork，这里的工作重点是这个应用本身，而非上游基线。

- 四项日常习惯：早睡 · 锻炼 · 戒烟 · 阅读
- 单项月历视图，90 天滚动记录
- 误触后三秒撤销
- NVS 掉电持久化，深睡唤醒日期不丢
- 不联网、不注册、无配套 App

## 文档

| | |
| --- | --- |
| [应用说明](docs/apps/pixel-habit-tracker.zh_CN.md) | 按键、存储模型、功耗设计、源码结构 |
| [硬件指南](docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md) | 板卡事实与 BSP 接口约束 |
| [构建与测试](docs/development/engineering/build-and-test.zh_CN.md) | 验证、烧录与真机验收 |

## 构建

```bash
source $IDF_PATH/export.sh   # ESP-IDF 5.5.3
./tools/validate.sh --static     # 主机测试
./tools/validate.sh --firmware   # 编译 + 合并镜像校验
```

应用开发在 `feature/pixel-habit-tracker` 分支；`main` 保持上游基线不动。

---

Fork 自 [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport)。
MIT License。
