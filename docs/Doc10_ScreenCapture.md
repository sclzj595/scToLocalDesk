# Doc10 模块详细设计：屏幕采集 + 图像编码子系统

> 版本：V2.0（2026-10-21 优化：补全采集器选型对比、编码管线、丢帧策略、参数配置）

## 1. 接口与实现

- IScreenCapturer 两个实现：

| 实现 | 适用 | 特点 |
| --- | --- | --- |
| DXGI DesktopDuplication | Win10 1809+（主力） | 高性能、抓帧快、GPU 拷贝一次 |
| Windows Graphics Capture (WGC) | Win11 / DXGI 受限环境 | 兼容新版系统、偶发黄色边框 |

- IEncoder 两个实现：

| 实现 | 依赖 | 定位 |
| --- | --- | --- |
| MJpegEncoder | 无（stb / libjpeg-turbo） | V1 默认，CPU 占用低、画质可调 |
| H264Encoder | FFmpeg（可选编译） | 高压缩比，弱网 / 低带宽内网 |

- 采集器自动降级：capture=auto 时按 DXGI → WGC 顺序探测，失败自动切换并 warn 日志

## 2. 生产管线（被控端）

```
采集线程: IScreenCapturer::grab()          10~30 fps（可配置）
             │ 仅保留最新帧（覆盖式）
             ▼
编码线程: IEncoder::encode(frame)
             ▼
打包 SCREEN_FRAME（TLV，Doc05 §1）
             ▼
Transport 发送队列（水位丢帧，Doc08 §4）
```

- 帧率控制：固定间隔驱动；画面无变化（DXGI DirtyRect 为空）跳过编码，降低 CPU 与带宽
- 画质参数：`session.fps`（10/15/24/30）、`session.quality`（low/medium/high）、`session.jpeg_quality`（1~100，custom 时生效）

## 3. 丢帧策略（红线）

1. 队列满只发最新帧，旧帧直接丢弃，不阻塞采集线程
2. 丢帧计数进结构化日志（session_id、dropped_frames），连续丢弃 > 100 帧上抛 `Evt_StreamLag`，控制端 UI 提示"网络较卡，已自动降低画质"
3. 降级动作：自动降 fps 一档，用户可在设置页改回（Doc17）

## 4. 控制端解码渲染

- MJPG：QImage 解码 + QML Image / 自绘 Item；解码放线程池，主线程只渲染
- 自适应缩放：画面等比缩放窗口，全屏模式保持比例；键鼠坐标按缩放比例 + 偏移反算后发送（Doc02 §5.2）

## 5. 异常与边界

1. 采集失败（如锁屏、UAC 安全桌面）：推最后一帧 + 提示遮罩"对方屏幕暂时不可见"；恢复后自动续推
2. 分辨率变化（插拔显示器）：重建采集器，通知控制端重设画布
3. 编码器 open 失败：自动降级 MJPG；仍失败则关闭会话并上抛错误

## 6. 配置项（契约见 Doc17）

| key | 默认 | 说明 |
| --- | --- | --- |
| `session.encoder` | mjpeg | mjpeg / h264（需 FFmpeg 编译） |
| `session.capture` | auto | dxgi / wgc / auto |
| `session.fps` | 15 | 10 / 15 / 24 / 30 |
| `session.quality` | medium | low / medium / high / custom |
| `session.jpeg_quality` | 80 | custom 时生效 |

## 7. 测试要点

1. 30 fps 高动态画面（播放视频）下 CPU / 内存基线达标，无队列堆积
2. 锁屏 → 解锁恢复推流；分辨率切换重建
3. h264 未编译时选择 h264：启动回退 MJPG + 日志
4. 帧时间戳单调性校验（防乱序）
