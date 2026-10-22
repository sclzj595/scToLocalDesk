# Doc12 基础设施设计：日志 spdlog、事件总线、线程池、buffer、配置

> 版本：V2.0（2026-10-21 优化：逐组件补全行为约定与配置契约）

## 1. Logger（ILogger / SpdlogLogger）

- ILogger 抽象（Doc06 §6），SpdlogLogger 实现：spdlog rotating sink，控制台 + 滚动文件双输出
- 滚动策略：按大小切割（默认 10 MB），保留 N 份（默认 7），日志目录同 exe `logs/`
- 等级：trace / debug / info / warn / error / critical，`log.level` 可配置（Doc17）
- 结构化约定：每条关键日志带 `[session_id=xxx][alias=xxx][msg_type=0xxx][err=xxx]` 字段，会话路径全链路可追溯
- qDebug/qWarning 重定向接入 spdlog（qInstallMessageHandler），统一日志出口
- 性能红线：日志调用不阻塞业务线程，spdlog 异步模式 + 满队列丢弃 debug 级

## 2. EventBus

- 线程安全发布订阅，支持跨线程投递：发布方任意线程，订阅方回调在注册时声明的投递线程（UI / 业务）执行
- 所有事件继承统一 `Event` 基类：`event_id、session_id、device_alias、timestamp`，便于日志追踪
- 语义：值拷贝投递（小对象），订阅回调必须快速返回，重活转线程池
- 典型事件清单：`Evt_DeviceOnline/Offline/AliasConflict`（Doc07）、`Evt_TransportState`（Doc08）、`Evt_ConnectRequest/SessionStart/SessionClosed`（Doc09）、`Evt_StreamLag`（Doc10）、`Evt_FileProgress/Done/Failed`（Doc11）、`Evt_UnattendedLogin`（Doc16）

## 3. ThreadPool

- 固定线程池（默认 4 线程 = CPU 核数下限），`submit(fn) -> std::future`
- 用途：文件 IO、CRC / 加密、PBKDF2 口令派生（Doc16，计算密集需专用长任务队列）
- 红线：禁止在池线程直接操作 UI；禁止池内再 submit 并同步等待自身结果（死锁）

## 4. Buffer

- 动态二进制缓冲区：`append / consume / readable_bytes`，几何扩容 + 复用，替代零散 vector
- 用途：TLV 报文组装 / 解析（Doc05）、文件分片缓冲、RawTcp 半包粘包处理（Doc08）
- 红线：跨线程 Buffer 不共享所有权，移交即转移

## 5. ConfigParser（IConfigRepository 实现）

- 读取 ini（key-value），启动加载 → 严格校验 → 默认值兜底 → 常驻内存；保存时原子写（临时文件 + rename，防掉电截断）
- **全量 key 契约、类型、默认值、校验规则唯一基准见 [Doc17](./Doc17_Settings_Page.md)**，本模块只实现机制不做业务校验
- 别名、端口、画质、自启开关、无人值守哈希等全部经此持久化
- 版本升级迁移：读到未知 key 忽略；缺失 key 补默认值并回写

## 6. 依赖关系

```
foundation(无Qt): logger / event_bus / thread_pool / buffer / config_parser
      ▲                     ▲
core/service ──────────────┘（只依赖抽象，不依赖 spdlog 头）
```

## 7. 测试要点

1. EventBus 多线程压测：发布 100 万事件无崩溃、无丢失（订阅在位时）
2. ConfigParser 畸形 ini / 缺失段 / 非法值：默认值兜底不崩溃
3. 滚动日志：写满切割、保留份数、中文路径正常
