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
var _blocking_output_contexts: Array[Dictionary] = []
var _failed: bool = false

func _ready() -> void:
	# Service 是普通 Node，可以直接挂在测试节点下；它会在后台处理 realtime
	# 时钟，同时允许本脚本通过 manual 时钟明确推进剧情时间。
	_service = NVTimelineService.new()
	_service.label_emitted.connect(_on_label_emitted)
	add_child(_service)

	_run_document_checks()
	_run_runtime_checks()
	_run_extended_runtime_checks()

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

func _run_extended_runtime_checks() -> void:
	_expect(not _service.submit_request({"type": "label", "label": "internal_only"}),
			"公開請求入口只接受九種規範請求，不接受內部標籤消息類型")

	# 等待结果只适用于 output。先确认文档层会拒绝把它配置在 blocking 输入上。
	var waiting_data: Dictionary = _service.get_document_data()
	var timelines: Array = waiting_data["timelines"]
	var timeline_data: Dictionary = timelines[0]
	var tracks: Array = timeline_data["tracks"]
	var first_track: Dictionary = tracks[0]
	var marks: Array = first_track["marks"]
	var invalid_input_data: Dictionary = waiting_data.duplicate(true)
	var invalid_timeline: Dictionary = invalid_input_data["timelines"][0]
	var invalid_tracks: Array = invalid_timeline["tracks"]
	var invalid_first_track: Dictionary = invalid_tracks[0]
	var invalid_marks: Array = invalid_first_track["marks"]
	var invalid_input_wait_flag := false
	for mark_index in range(invalid_marks.size()):
		var invalid_mark: Dictionary = invalid_marks[mark_index]
		if invalid_mark.get("kind", "") == "blocking":
			invalid_mark["wait_for_result"] = true
			invalid_marks[mark_index] = invalid_mark
			invalid_input_wait_flag = true
	invalid_first_track["marks"] = invalid_marks
	invalid_tracks[0] = invalid_first_track
	invalid_timeline["tracks"] = invalid_tracks
	var invalid_timelines: Array = invalid_input_data["timelines"]
	invalid_timelines[0] = invalid_timeline
	invalid_input_data["timelines"] = invalid_timelines
	_expect(invalid_input_wait_flag, "測試資料包含一個阻塞輸入")
	_expect(not _service.load_document(invalid_input_data),
			"文檔拒絕給 blocking 輸入設定 wait_for_result")

	# 首個輸出等待業務結果；快進區間內再放置一個等待輸出，以驗證連續阻塞。
	var start_mark: Dictionary = marks[0]
	start_mark["wait_for_result"] = true
	marks[0] = start_mark
	marks.append({
		"id": 12,
		"name": "中途等待業務結果",
		"time": 15,
		"kind": "output",
		"label": "demo_gate_wait",
		"wait_for_result": true,
	})
	first_track["marks"] = marks
	tracks[0] = first_track
	timeline_data["tracks"] = tracks
	timelines[0] = timeline_data
	waiting_data["timelines"] = timelines
	waiting_data["id_counter"] = 13
	_expect(_service.load_document(waiting_data), "輸出標記可以設定 wait_for_result")
	_service.get_event_manager().register_binding(
			"demo_started", Callable(self, "_on_blocking_output"))
	_service.get_event_manager().register_binding(
			"demo_gate_wait", Callable(self, "_on_blocking_output"))
	_emitted_labels.clear()
	_blocking_output_contexts.clear()

	# 統一請求入口依 FIFO 執行啟動、暫停和恢復；阻塞輸出在整個過程中保留。
	_expect(_service.submit_request({"type": "start_timeline", "timeline_id": 2}),
			"統一請求可以啟動時間軸")
	var timeline := _service.get_timeline(2)
	_expect(timeline != null and timeline.is_waiting_for_output_result(),
			"輸出標記發出後等待業務返回結果")
	_expect(_blocking_output_contexts.size() == 1,
			"業務 Callable 收到阻塞輸出的上下文")
	if timeline == null or _blocking_output_contexts.is_empty():
		return

	var output_context: Dictionary = _blocking_output_contexts[0]
	_expect(output_context.get("wait_for_result", false), "回調上下文標出需要返回結果")
	_expect(_service.submit_request({"type": "pause_timeline", "timeline_id": 2}),
			"統一請求可以暫停時間軸")
	_expect(timeline.get_state_name() == "paused", "等待輸出結果時仍可暫停")
	_expect(_service.submit_request({"type": "resume_timeline", "timeline_id": 2}),
			"統一請求可以恢復時間軸")
	_expect(timeline.is_waiting_for_output_result(), "恢復後仍等待原輸出結果")

	# 0.1 秒為一個單位；先讓普通時鐘推進到輸入阻塞，再為已阻塞的時間軸提出快進。
	_expect(_service.submit_request({
			"type": "resolve_output_result",
			"timeline_id": 2,
			"run_id": output_context.get("source_run_id", 0),
			"mark_id": output_context.get("output_mark_id", 0),
			"success": true,
	}), "业务可以提交阻塞输出的成功结果")
	_expect(not timeline.is_waiting_for_output_result(), "成功结果解除输出等待")
	_expect(_service.submit_request({"type": "advance_clock", "clock_id": 1, "units": 15}),
			"统一请求可以推进时钟")
	_expect(timeline.is_waiting() and timeline.get_position() == 10,
			"普通时钟推进在输入阻塞处暂停")
	_expect(_service.submit_request({"type": "pause_timeline", "timeline_id": 2}),
			"等待中的時間軸可以額外進入暫停狀態")

	_expect(_service.submit_request({
			"type": "fast_forward", "timeline_id": 2, "target_units": 20,
	}), "等待输入且已暂停的时间轴可以记录快进目标")
	_expect(timeline.is_waiting() and timeline.get_position() == 10 and
			timeline.get_state_name() == "paused",
			"快进请求不会越过当前阻塞或解除暂停")
	_expect(_service.submit_request({
			"type": "publish_label", "label": "demo_confirmed", "target_timeline_id": 2,
	}), "统一标签请求可以指定接收时间轴")
	_expect(timeline.get_position() == 10 and timeline.get_state_name() == "paused",
			"暂停期间收到输入只解除阻塞，不会自动恢复时间轴")
	_expect(_service.submit_request({"type": "resume_timeline", "timeline_id": 2}),
			"解除输入阻塞后可以显式恢复时间轴")
	_expect(timeline.is_waiting_for_output_result() and timeline.get_position() == 15,
			"解除輸入阻塞後快進繼續，並按序停在下一個等待輸出")
	_expect(_blocking_output_contexts.size() == 2,
			"同一段快進可以依次觸發多個等待結果輸出")
	if _blocking_output_contexts.size() >= 2:
		var second_output_context: Dictionary = _blocking_output_contexts[1]
		_expect(_service.submit_request({
				"type": "resolve_output_result",
				"timeline_id": 2,
				"run_id": second_output_context.get("source_run_id", 0),
				"mark_id": second_output_context.get("output_mark_id", 0),
				"success": true,
		}), "第二個等待輸出可以獨立返回成功")
		_expect(timeline.get_position() == 20 and timeline.get_state_name() == "running",
				"第二個阻塞解除後繼續完成原快進目標")

	# 直接跳转越过区间标记；这些输出不应补发，之后的正常推进从目标之后开始。
	_expect(_service.submit_request({"type": "jump", "timeline_id": 2, "target_units": 30}),
			"统一请求可以直接跳转时间轴")
	_expect(timeline.get_position() == 30, "跳转直接设置目标时间")
	_expect(not _emitted_labels.has("demo_fx_leave") and not _emitted_labels.has("demo_finished"),
			"跳转不会触发经过位置的输出标记")
	_expect(_service.submit_request({"type": "jump", "timeline_id": 2, "target_units": 25}),
			"合法格式的向后跳转请求会进入统一队列")
	_expect(timeline.get_position() == 30, "向后跳转被运行时拒绝且不改变当前位置")
	var old_run_context: Dictionary = _blocking_output_contexts[0]
	_expect(_service.submit_request({"type": "stop_timeline", "timeline_id": 2}),
			"统一请求可以停止时间轴")
	_expect(timeline.get_state_name() == "stopped", "队列中的停止请求生效")

	# 重新运行会生成新 run_id；失败结果按约定停止本次时间轴。
	_blocking_output_contexts.clear()
	_expect(_service.submit_request({"type": "start_timeline", "timeline_id": 2}),
			"停止后可通过统一请求重新启动")
	_expect(_blocking_output_contexts.size() == 1, "新运行再次等待零时刻输出结果")
	if not _blocking_output_contexts.is_empty():
		output_context = _blocking_output_contexts[0]
		_expect(_service.submit_request({
				"type": "resolve_output_result",
				"timeline_id": 2,
				"run_id": old_run_context.get("source_run_id", 0),
				"mark_id": old_run_context.get("output_mark_id", 0),
				"success": true,
		}), "舊運行的遲到結果請求可入隊")
		_expect(timeline.is_waiting_for_output_result(),
				"舊運行的遲到結果不會解除新運行的等待")
		_expect(_service.submit_request({
				"type": "resolve_output_result",
				"timeline_id": 2,
				"run_id": output_context.get("source_run_id", 0),
				"mark_id": output_context.get("output_mark_id", 0),
				"success": false,
		}), "业务可以提交阻塞输出的失败结果")
		_expect(timeline.get_state_name() == "stopped", "失败结果停止当前时间轴运行")

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

func _on_blocking_output(context: Dictionary) -> void:
	# 插件不持有业务处理器或追踪异步耗时；业务代码只需保存这组运行/标记标识，
	# 完成后通过 submit_request 回传 success，旧运行的上下文会由运行时拒绝。
	_blocking_output_contexts.append(context)

func _expect(condition: bool, message: String) -> void:
	if condition:
		print("[PASS] ", message)
	else:
		_failed = true
		push_error("[FAIL] " + message)

func _quit_after_test() -> void:
	get_tree().quit(1 if _failed else 0)
