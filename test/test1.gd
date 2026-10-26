extends Node

## NV Timeline 运行时验收脚本。
##
## 把本脚本挂到 test/testmap1.tscn 的根节点后运行场景，即可在 Godot 输出
## 面板中看到每一项验收结果。脚本只使用插件公开的 Service/Document API，
## 不直接访问 C++ 内部字段，因而也能作为后续回归测试的基础。

## 是否在所有检查结束后自动关闭运行窗口；手动验收时保持 false 方便观察场景。
@export var quit_after_test: bool = false

var _service: NVTimelineService
var _emitted_labels: Array[String] = []
var _interrupt_contexts: Array[Dictionary] = []
var _failed: bool = false

func _ready() -> void:
	# Service 是普通 Node，可以直接挂在测试节点下；它会在后台处理 realtime
	# 时钟，同时允许本脚本通过 manual 时钟明确推进剧情时间。
	_service = NVTimelineService.new()
	_service.label_emitted.connect(_on_label_emitted)
	add_child(_service)

	_run_document_checks()
	_run_runtime_checks()

	if _failed:
		push_error("NV Timeline 验收失败，请根据上面的 [FAIL] 信息定位问题。")
	else:
		print("[PASS] NV Timeline 全部运行时验收通过。")

	if quit_after_test:
		# 延迟一帧，确保最后一条输出已经写入 Godot 日志。
		call_deferred("_quit_after_test")

func _run_document_checks() -> void:
	var sample_path := "res://addons/nv_timeline/examples/basic.timeline.json"
	_expect(_service.load_json_file(sample_path), "示例 JSON 可以加载")
	if _service.get_document_data().is_empty():
		return

	var loaded_data: Dictionary = _service.get_document_data()
	_expect(typeof(loaded_data.get("id_counter", null)) == TYPE_INT,
			"JSON 整数字段被归一化为 Godot int")
	_expect(loaded_data.get("id_counter", 0) == 11, "文档 id_counter 保持为 11")

	# 修改一份副本，确认未知字段不会被静默保存。
	var invalid_data: Dictionary = loaded_data.duplicate(true)
	invalid_data["unexpected_runtime_state"] = true
	var document := NVTimelineDocument.new()
	_expect(not document.set_data(invalid_data), "未知 JSON 字段会被拒绝")
	_expect(not document.get_validation_errors().is_empty(), "非法 JSON 会返回诊断信息")

func _run_runtime_checks() -> void:
	# 绑定一个只接收有效时间轴输入的 Callable，用来验证暂停期间 listener
	# 仍然有效，以及回调上下文包含接收时间轴 ID。
	var event_manager := _service.get_event_manager()
	var binding_id := event_manager.register_binding(
			"demo_interrupt", Callable(self, "_on_interrupt"), true)
	_expect(binding_id > 0, "可以注册 Callable 标签绑定")

	_expect(_service.start_timeline(2), "示例时间轴可以启动")
	var timeline := _service.get_timeline(2)
	_expect(timeline != null, "可以取得时间轴运行实例")
	if timeline == null:
		return
	_expect(timeline.get_position() == 0, "启动后游标位于 0")
	_expect(_emitted_labels == ["demo_started"], "启动时触发零时刻输出")

	# 暂停不应撤销 listeners；demo_interrupt 仍应被事件管理器接收。
	_expect(_service.pause_timeline(2), "时间轴可以暂停")
	_service.publish_label("demo_interrupt")
	_expect(_interrupt_contexts.size() == 1, "暂停期间全程输入仍然有效")
	if not _interrupt_contexts.is_empty():
		var context := _interrupt_contexts[0]
		_expect(context.get("accepted_timeline_ids", []).has(2),
				"Callable 上下文包含接收输入的时间轴 ID")
	_expect(_service.resume_timeline(2), "时间轴可以恢复")

	# 5 个单位等于 0.5 秒，应该触发另一条轨道上的并行输出。
	_service.advance_clock(1, 5)
	_expect(_emitted_labels == ["demo_started", "demo_fx_enter"],
			"跨轨道推进按稳定顺序触发输出")

	# 再推进 1 秒会抵达 time=10 的阻塞标记；更晚的输出不能越过阻塞点。
	_service.advance_clock(1, 10)
	_expect(timeline.is_waiting(), "未收到期待标签时进入 blocking 等待")
	_expect(timeline.get_position() == 10, "blocking 等待停在正确的逻辑时间")
	_expect(not _emitted_labels.has("demo_finished"), "blocking 后的输出没有提前触发")

	_service.publish_label("demo_confirmed")
	_expect(not timeline.is_waiting(), "收到期待标签后解除 blocking 等待")

	# 解除等待后推进到 time=30，依次触发并行表现结束和主流程结束。
	_service.advance_clock(1, 20)
	_expect(_emitted_labels == [
			"demo_started",
			"demo_fx_enter",
			"demo_fx_leave",
			"demo_finished",
		], "解除阻塞后的输出顺序正确")
	_expect(timeline.get_position() == 30, "业务输出结束时游标位于 time=30")
	_expect(timeline.get_state_name() == "running", "尚未到 duration 时仍保持运行")

	# 示例时间轴的有限 duration 是 40；最后再推进 1 秒，才应进入完成状态。
	_service.advance_clock(1, 10)
	_expect(timeline.get_state_name() == "completed", "有限时长时间轴最终完成")

func _on_label_emitted(label: String, _timeline_id: int, _time_units: int) -> void:
	# Service 的 label_emitted 是所有输出标记的统一观察点；输入标签不会从这里
	# 伪装成输出，因此可以直接用它核对编排顺序。
	_emitted_labels.append(label)
	print("[LABEL] ", label)

func _on_interrupt(context: Dictionary) -> void:
	# 事件管理器把运行时上下文作为 Dictionary 传入，业务回调不需要访问
	# 时间轴内部游标或直接修改 C++ 状态。
	_interrupt_contexts.append(context)
	print("[HANDLER] demo_interrupt accepted by ", context.get("accepted_timeline_ids", []))

func _expect(condition: bool, message: String) -> void:
	if condition:
		print("[PASS] ", message)
	else:
		_failed = true
		push_error("[FAIL] " + message)

func _quit_after_test() -> void:
	get_tree().quit(1 if _failed else 0)
