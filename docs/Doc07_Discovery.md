# Doc07 模块详细设计：设备别名发现服务

> 版本：V2.0（2026-10-21 优化：补全状态机、心跳包表、冲突策略、静态映射、测试要点）

## 1. 职责

- 周期性 UDP 组播心跳上报本机（别名、IP、端口、版本、状态）
- 接收并解析其他设备心跳，维护别名映射表 `alias -> ip:port`
- 别名冲突检测与告警
- 为控制端提供别名解析：`resolve(alias) -> ip:port`
- 为 IT 视图提供在线设备列表

## 2. 状态机

```
Stopped ──start()──▶ Running ──stop()──▶ Stopped
                       │  心跳发送线程 + 接收处理
                       └─ 静态映射模式下：不组播、仅查表
```

## 3. 心跳包设计

见 [Doc05 §2](./Doc05_Protocol.md)。明文组播，不含敏感信息；magic 过滤无关报文。

## 4. 配置项（契约见 Doc17）

| key | 默认 | 说明 |
| --- | --- | --- |
| `discovery.multicast_enabled` | true | 关闭后进入静态映射模式 |
| `discovery.multicast_addr` | 239.255.88.99 | 组播地址，端口与 ws_port 独立 |
| `discovery.heartbeat_interval_s` | 3 | 心跳周期 |
| `discovery.offline_timeout_s` | 15 | 离线判定（≥3 倍心跳周期） |
| `discovery.static_map` | 空 | `alias=ip:port;...`，跨 VLAN 场景 |

## 5. 输出事件（EventBus）

| 事件 | 载荷 | 说明 |
| --- | --- | --- |
| `Evt_DeviceOnline` | DeviceInfo | 新设备上线 / 离线后恢复 |
| `Evt_DeviceOffline` | alias | 超时无心跳 |
| `Evt_AliasConflict` | alias、ip | 收到与本机别名相同但 IP 不同 / 表内别名 IP 跳变 |

## 6. 别名冲突策略

1. 启动后 2 个心跳周期内收到同别名异 IP 心跳 → 判定冲突，弹窗要求改名（首次启动引导页即预检）
2. 运行中映射表某别名 IP 跳变且旧条目未超时 → 记 warn 日志 + `Evt_AliasConflict`，UI 角标提醒；不自动改写路由，以最新心跳为准
3. 冲突期间两台设备互不可见对方服务的确定性由用户改名保证（与向日葵设备名逻辑一致）

## 7. 静态映射模式（跨 VLAN / 禁组播网络）

- `discovery.multicast_enabled=false` 时：停止组播收发，`set_static_map()` 加载配置表
- 在线状态不可知（无心跳），列表展示"已配置"状态；resolve 直接查表
- 兼容策略：表内条目 + 组播结果合并去重（组播优先）

## 8. 线程模型与边界

- Qt 适配器（QUdpSocket）在主线程事件循环收发，解析后经 EventBus 投递，Core 无 Qt
- 映射表读写加互斥锁；表容量上限 256 台，超出告警（内网规模异常）
- 组播被防火墙拦截：无事件、无崩溃；设置页提供"发现自检"按钮（发送探测 + 3s 内无回报则提示检查防火墙，见 Doc15）

## 9. 测试要点

1. 设备上线 → `Evt_DeviceOnline`；拔网线 / 杀进程 → `offline_timeout_s` 后离线
2. 两台同别名设备：冲突事件触发、UI 提示正确
3. 静态映射模式下组播关闭、resolve 命中
4. 心跳包畸形（长度截断、magic 错误）不崩溃、被丢弃
