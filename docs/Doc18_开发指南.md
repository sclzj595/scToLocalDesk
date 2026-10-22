# Doc18 开发指南——基建完毕后的完整实施路线

> 定位：从「地基已打好」到「可出售的企业内网远程协助软件 V1.0」的全流程开发指南。
> 与 Doc02（PRD）、Doc03（架构）、Doc14（测试计划）、Doc15（部署指南）互补引用，不重复其内容。
> 工作纪律与构建规范见 §9，各阶段任务切分以「一个模块一笔 commit」为单元。

---

## 1. 现状快照（2026-10-23 基建收口时点）

### 1.1 已有资产

| 层 | 资产 | 状态 |
|---|---|---|
| foundation | EventBus（快照分发）/ ThreadPool / IniConfig（宽字符修复）/ BoundedQueue / Buffer | 稳定 |
| foundation 日志 | ilogger 契约 → SpdlogLogger（vendored 1.17，flush_on(Warn)）/ FileLogger 兜底（5MB×5 滚动） | 稳定 |
| foundation 头库封装 | hashx（xxHash）/ jsonx（nlohmann）/ enumx（magic_enum）/ perf_counter（PerfCounter+ScopedPerf） | 稳定，third_party 注册中心存在即启用 |
| platform | SingleInstanceGuard（QLocalServer）/ QtEventBridge / TrayController / UdpDiscoveryAdapter / SendInput 注入器 | 稳定 |
| core 协议 | TLV 帧头 / AUTH_CHALLENGE(salt) / FILE_META / ScreenFrame | 骨架完整 |
| implement | GDI 采集 / RAW 编码 / WS 传输（proactor 发送 + RX 零拷贝）/ OpenSSL 加密 | 可用；MJPG/DXGI 为预留 |
| service | RemoteSessionService / FileService / ScreenStreamService（含 PerfCounter 统计）/ AliasService / InputService / AppConfig | 接线完整，等真实客户端流量 |
| UI | QML 版 + QWidget 版双实现，向日葵模式（连接名+9 位码+确认+信任记忆），7 组设置页（未接线） | 视觉收口，数据全部为演示占位 |

### 1.3 已知欠账（按阶段排入下文）

- UI 全部演示数据：设备列表 ListElement 写死、peerIp "192.168.1.45"、本机名假数据
- 设置页 7 组 ↔ AppConfig 未接线；TrustStore 信任缓存仅内存态
- 连接码未由机器身份派生（UI 假码）
- 远程画面：RAW 直通带宽不可商用；控制端无解码渲染
- 无单元测试框架；无崩溃捕获

---

## 2. 总路线图

```
P1 数据闭环（让 UI 活起来）────────── 纯接线，无新协议，最快见效
P2 远程画面闭环（核心卖点）────────── 编码升级 + 解码渲染 + 键鼠回传
P3 文件传输闭环 ──────────────────── UI 接 FileService 真信号
P4 稳定性加固（长期运行保障）──────── 测试框架 + 崩溃捕获 + 压测
P5 交付工程（可卖）────────────────── 构建固化 + 安装包 + i18n
P6 Linux 远程编译（Qt 6.2.4）──────── 跨平台适配
```

依赖关系：P1→P2→P3 严格顺序；P4 可与 P3 并行穿插；P5 依赖 P2/P3 完成；P6 独立可随时插入。

---

## 3. P1 数据闭环（让 UI 活起来）

> 目标：杀掉一切写死的演示数据。做完后双端 UI 显示真实本机身份、真实发现列表、持久化信任缓存、可保存设置。

### 3.1 IdService——连接码派生（新增 `src/service/id_service.hpp/.cpp`）

- 9 位连接码 = xxh3_64(机器指纹串, salt) 折叠为 3×3 位十进制；指纹串 = hostname + 首块非虚拟网卡 MAC + 安装目录（新增机器时避免码碰撞由 hash 空间保证，冲突概率 ≈ 1/10^9 域内）
- 连接名 = `username@hostname`（GetUserNameA / GetComputerNameA，宽字符 API 收敛到 platform）
- 自检：启动日志打印 `self_code=xxx xxx xxx`（复用 third_party 自检日志格式）
- commit：`ADD: IdService 连接码派生与连接名组装`

### 3.2 TrustStore——信任缓存落盘（新增 `src/service/trust_store.hpp/.cpp`）

- 文件：exe 旁 `trust.json`（jsonx 读写；字段 `[{code, name, trusted_at}]`）
- 接口：`is_trusted(code)` / `trust(code, name)` / `untrust(code)` / `clear()`；变更即原子落盘（临时文件 + rename，防断电半写）
- MainWindow（双端）的 `trustCache` 内存数组全部替换为 TrustStore 调用；确认弹窗勾选记忆 → `trust()`
- 设置页「清空信任设备缓存」按钮接 `clear()`
- 验收：信任某设备 → 重启 → 免确认直连；trust.json 中文设备名不乱码（jsonx ensure_ascii=false 已保证）
- commit：`ADD: TrustStore 信任缓存落盘`

### 3.3 设置页接线（双端）

- 7 组字段 ↔ AppConfig 逐项绑定：初始化读、变更写、`save()` 落盘（IniConfig 宽字符已稳）
- 需要即时生效的项接服务 setter：fps/画质→ScreenStreamService、心跳→UdpDiscoveryAdapter、日志级别→logger->set_level
- 主题开关已有 applyTheme 联动，把选择持久化进 `[appearance]`
- 验收：改端口 → 重启 → 生效；改日志级别 → 立即影响输出
- commit：`MOD: 设置页双端接线 AppConfig 落盘`

### 3.4 设备列表真数据（双端）

- QtEventBridge 已挂 AliasService 心跳信号——QML 侧 ListModel 由 bridge 驱动（QVariantList 注入或 QAbstractListModel，取后者防整表刷新抖动）；Widgets 侧 QListWidget 增量刷新
- 设备卡字段映射：name（对端连接名）/ code（对端连接码，来自发现心跳扩展字段——若 Doc07 心跳无此字段，本阶段在心跳包 TLV 加 `code` 体，向后兼容：旧包无体显示 `---`）
- 点击连接逻辑不变（trusted || TrustStore.is_trusted → 直连，否则确认弹窗，peerIp 用真实发现地址替换假数据）
- 验收：两台真机（或本机起双实例改端口）互见互连
- commit：`MOD: 设备列表接 AliasService 真数据（心跳扩展连接码）`

---

## 4. P2 远程画面闭环（核心卖点）

> 目标：被控端画面在控制端流畅渲染并可键鼠操作。这是产品验收的生死线。

### 4.1 MJPG 编码器（新增 `src/implement/encoder/mjpg_encoder.hpp/.cpp`）

- **third_party 吸收 stb_image_write.h（header-only，符合注册中心纪律）**：RGB→JPEG 压缩，质量参数复用 AppConfig.jpeg_quality
- IEncoder 接口不变；RawEncoder 保留为调试兜底（cfg 可切）
- 带宽估算验收：1080p@15fps JPEG q75 ≈ 3~6 MB/s（RAW 同场景 ≈ 90 MB/s，差 20 倍）
- PerfCounter 已在采集循环埋点——编码耗时进 `avg/max_ms`，JPEG 后必须回到帧预算内
- commit：`ADD: MJPG 编码器（stb_image_write vendor）`

### 4.2 控制端解码渲染

- 解码：**stb_image.h 同批 vendor**（JPEG→RGB，header-only）
- QML 版：QQuickPaintedItem 子类（platform/qt_adapter/video_surface）注册进 ScLocaldesk 单例命名空间；RemoteViewPage 放 surface 元素；帧到达 bridge 信号 → surface `update()` 重绘
- Widgets 版：QWidget paintEvent + QImage blit
- 性能红线：15fps 下 UI 不掉帧——QML 大帧建议走 QSGSimpleTextureNode（若 QQuickPaintedItem 实测卡顿再升级，先简后繁）
- commit：`ADD: 控制端视频面（stb_image vendor + QML/Widgets 双渲染）`

### 4.3 会话流程真实化

- REQUEST_CONNECT → AUTH_CHALLENGE/PROOF（密码/确认/无人值守三路已备）→ 对端 ScreenFrame 流
- 断开/超时/拒绝三种路径的 UI 状态回退（连接中→失败提示→回设备列表）
- commit：`MOD: 会话流程端到端接线`

### 4.4 键鼠回传

- 远程画面上 MouseArea/Keys 事件 → 归一化坐标（0~65535，IInputInjector 契约）→ SESSION_INPUT 消息 → 对端 InputService 注入
- 注意：全屏捕获模式需要控制端画面等比缩放，坐标反算按显示区/画面比例
- 验收：控制端操作对端记事本可打字、可点按钮
- commit：`ADD: 键鼠透传链路`

### 4.5 画质自适应（可选，P4 压测后再做）

- 用 ScreenStreamService 的 PerfCounter 统计：持续超预算 → 降 fps/降质量；空闲 → 回升
- 简单滞回即可，不做 PID

---

## 5. P3 文件传输闭环

- FileTransferPage 双端接 FileService 真信号（进度/失败/完成——信号已在 service 层就绪）
- 发送入口：按钮选文件 + 拖拽入区（QML DropArea / Widgets dragEnter）
- 接收：确认开关（AppConfig.confirm_before_transfer 已挂）+ 保存目录落盘
- 大文件验收：≥1GB 单文件不涨内存（分片流式已备）、校验值对上（接线点：FILE_CHUNK checksum → hashx::xxh3_64，此为 xxHash 的首个业务用途）
- commit：`MOD: 文件传输 UI 接 FileService`；`ADD: 分片 xxH3 校验接线`

---

## 6. P4 稳定性加固（可与 P3 穿插）

### 6.1 单元测试框架

- **third_party 吸收 doctest（header-only，单头）**；tests/ 建 protocol / bounded_queue / config / trust_store / id_service 用例
- CMake 加 `sc_localdesk_tests` target（SCLD_HAS_WEBSOCKET 桩模式复用）
- commit：`ADD: doctest 单元测试框架 + 核心用例`

### 6.2 崩溃捕获（platform/win32）

- SetUnhandledExceptionFilter → MiniDump 写 exe 旁 `dumps/`（挂 Windbg/WinDbg Preview 分析流程进 Doc15）
- 日志联动：崩溃前最后一条 WARN/ERROR（spdlog flush_on(Warn) 已保证在缓冲外）
- commit：`ADD: MiniDump 崩溃捕获`

### 6.3 长稳压测

- 双机连续推流 8h：内存曲线（无增长）、句柄数（无泄漏）、PerfCounter avg/max 无恶化
- 断网重连 100 次循环：会话状态机不卡死
- 结果记入 Doc14 测试计划执行记录

---

## 7. P5 交付工程（可卖）

### 7.1 构建脚本固化（最高优先）

- 把临时构建脚本（MSVC 环境注入 + Ninja）固化进仓库 `scripts/build_windows.ps1`，参数化 `-Clean`；新人 clone 后一条命令出双 exe
- windeployqt 步骤一并入脚本（QML 版必带 `--qmldir src\ui\qml`）
- commit：`ADD: Windows 构建脚本固化入仓`

### 7.2 安装包

- 便携 zip（exe + config.ini + README）与 Inno Setup 安装器二选一先做 zip（企业内网常走绿色部署）
- 单实例锁/文件日志/崩溃目录均 exe 旁相对路径，天然便携
- commit：`ADD: 便携包打包脚本`

### 7.3 i18n 与无障碍（P5 收尾）

- 双端全量 `qsTr()/tr()` 包裹扫描；zh-CN 为源语言，en.ts 后补
- QML Accessible.name 关键控件补齐
- commit：`MOD: i18n 包裹与无障碍属性`

---

## 8. P6 Linux 远程编译（Qt 6.2.4）

- CMake 基线已是 6.2，GlassBlurEffect 已按 hasGlassBlur 分档退化——预期主要工作是：
  1. SendInput 注入器换平台实现（Linux 无键鼠注入骨架，V1.0 可先禁用该入口）
  2. GDI 采集换 X11/Pipewire（V1.0 可先只做控制端）
  3. MiniDump 换 core dump 说明
- VS 远程构建配置或 SSH+CMake 工具链二选一，步骤沉淀进 Doc15

---

## 9. 工作纪律（固化）

1. **提交节奏**：一个小模块完成即 commit + push；前缀 `ADD:`（新增）/ `MOD:`（修改）/ `FIX:`（修缺陷），标题一行主题 + 正文要点
2. **构建**：MSVC 14.29 手动注入环境（INCLUDE/LIB/PATH 三件套）+ Ninja；构建前先 `taskkill` 两个 exe（LNK1104 占用）；命令模板：
   ```powershell
   $vs='E:\IDE_FOR_ALL\visual studio 2019 professional\VC\Tools\MSVC\14.29.30133'
   $sdk='C:\Program Files (x86)\Windows Kits\10'
   $env:PATH="$vs\bin\Hostx64\x64;$sdk\bin\10.0.19041.0\x64;F:\Apps\QT\Tools\Ninja;F:\Apps\QT\6.5.3\msvc2019_64\bin;$env:PATH"
   $env:INCLUDE="$vs\include;$sdk\Include\10.0.19041.0\ucrt;$sdk\Include\10.0.19041.0\um;$sdk\Include\10.0.19041.0\shared;$sdk\Include\10.0.19041.0\cppwinrt"
   $env:LIB="$vs\lib\x64;$sdk\Lib\10.0.19041.0\ucrt\x64;$sdk\Lib\10.0.19041.0\um\x64"
   F:\Toolchain\Cmake\bin\cmake.exe --build g:\GitLocal\scToLocalDesk\build-msvc
   ```
3. **third_party 落位后必须 reconfigure**：`cmake -S . -B build-msvc`（EXISTS 探测是 configure 期行为，已踩坑入档）
4. **冒烟**：改完必跑双 exe 存活验证 + 日志尾部检查（spdlog 版格式 `[scld] [error]`）
5. **网络**：GitHub TLS 抖动时走 `http://127.0.0.1:7890` 代理（临时环境变量，不写 git 配置）；下载大文件加 `--ssl-no-revoke`
6. **QSS/QML 修改后**：QML 版重编译即生效（qrc 重嵌）；windeployqt 仅 DLL/模块变化时需要重跑

---

## 10. 风险清单

| 风险 | 影响 | 缓解 | 状态 |
|---|---|---|---|
| RAW 带宽不可商用 | P2 阻塞 | MJPG（§4.1） | 排期 P2 |
| QML 大帧渲染卡顿 | P2 体验 | 先 QQuickPaintedItem，卡再 TextureNode | §4.2 预案 |
| 心跳包无连接码字段 | P1 设备列表 | TLV 加可选体，旧包 `---` 降级 | §3.4 |
| 端口占用静默失败 | 客户现场首装体验 | 日志已告警，需 UI 弹窗提示（P1 设置接线时顺带） | 待办 |
| 中文安装路径 | 已修 IniConfig | P5 便携包回归测试覆盖中文路径 | 回归项 |
| 8899 TIME_WAIT 重启失败 | 高频重启调试期 | 日志已见；可加 SO_REUSEADDR 或换端口配置 | 待办（P1） |

---

## 11. 建议启动顺序（下一轮开工清单）

1. P1.1 IdService（半天量级，解锁连接码真值）
2. P1.2 TrustStore（半天，解锁免确认直连闭环）
3. P1.3 设置页接线（一天，解锁 config.ini 生命周期）
4. P1.4 设备列表真数据（一~两天，含心跳扩展）
5. P2.1 MJPG（stb vendor + 编码器）

每步完成即 commit + push，按 §9 纪律执行。
