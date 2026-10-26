# NV Timeline

`NVTimelineService` 是一个以 C++ 为核心的后台时间轴运行时。它对应 `Docs/` 中的三份设计稿，当前实现覆盖：

- 0.1 秒整数时间单位，以及 `manual` / `realtime` 两种逻辑时钟。
- 多条平级时间轴绑定同一或不同的时钟。
- 启用/禁用轨道、可重叠的纯视觉 `clips`，以及稳定顺序的 `output` / `blocking` 标记。
- 运行期间的 `listeners` 全程输入；暂停和阻塞等待期间仍然接收标签。
- 统一 FIFO 标签队列、Callable 程序绑定和单轮派发上限。
- 严格 JSON 字段白名单、文档级 ID 计数器、引用与时间范围校验。

## 构建

根项目的 CMake 会先生成 `godot-cpp`，随后构建本插件并把库复制到 `addons/nv_timeline/bin/<platform>/`。将 `res://addons/nv_timeline/nv_timeline.gdextension` 加入项目后即可使用原生类。

## 最小运行示例

```gdscript
var service := NVTimelineService.new()
add_child(service)
service.load_json_file("res://chapter_01.timeline.json")
service.start_timeline(3)
service.advance_clock(1, 10) # 推进 1 秒
service.publish_label("puzzle_completed")
```

业务绑定通过 `service.get_event_manager().register_binding(label, callable, requires_timeline_input)` 注册；回调收到一个 Dictionary 上下文，其中包含标签、来源、时间和已接收输入 ID。JSON 不保存 Callable、脚本路径或运行状态。`examples/basic.timeline.json` 可直接作为第一份测试编排。

当前版本以 Godot `Callable` 作为跨语言接入点，C++ 负责时间推进、JSON、路由和运行状态；设计稿中的 C# `TimelineEventHandler` 工厂与托管生命周期包装会在 Callable 契约验证后继续补齐。

首版对设计稿中尚未定案的调度行为采用以下规则：同一时刻按轨道数组顺序和标记数组顺序求值；阻塞标记停住所属时间轴并丢弃本次推进的剩余单位；同一标签可以同时解除多个阻塞输入；共享时钟不会追赶被阻塞时间轴。
