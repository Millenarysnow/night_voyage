@tool
extends EditorPlugin

## 时间轴编辑器的最小入口。
##
## 运行时核心完全由 C++ 提供；这里仅负责在 Godot 编辑器里创建一个独立
## Window，后续可以把 JSON 编辑控件放入窗口而不改变运行时数据边界。
var _timeline_window: Window

func _enter_tree() -> void:
	# 使用工具菜单作为稳定入口，不占用主编辑器 Dock，也符合设计稿的独立窗口要求。
	add_tool_menu_item("NV Timeline", Callable(self, "_open_timeline_window"))

func _exit_tree() -> void:
	remove_tool_menu_item("NV Timeline")
	if is_instance_valid(_timeline_window):
		_timeline_window.queue_free()
	_timeline_window = null

func _open_timeline_window() -> void:
	if is_instance_valid(_timeline_window):
		_timeline_window.grab_focus()
		return

	_timeline_window = Window.new()
	_timeline_window.title = "NV Timeline"
	_timeline_window.size = Vector2i(1100, 680)
	_timeline_window.min_size = Vector2i(720, 420)
	_timeline_window.close_requested.connect(_close_timeline_window)

	# 先放置一个明确的占位标签；JSON 读写和轨道控件将在 C++ 编辑器控件
	# 稳定后接入，避免把编辑器会话状态写进运行时文档。
	var notice := Label.new()
	notice.text = "NV Timeline\n请在项目中加载 .timeline.json 文档。"
	notice.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	notice.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	notice.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_timeline_window.add_child(notice)
	add_child(_timeline_window)
	_timeline_window.popup_centered()

func _close_timeline_window() -> void:
	if is_instance_valid(_timeline_window):
		_timeline_window.queue_free()
	_timeline_window = null
