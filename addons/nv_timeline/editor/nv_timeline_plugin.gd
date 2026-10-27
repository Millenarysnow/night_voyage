@tool
extends EditorPlugin

## 时间轴编辑器的菜单入口。
##
## 所有文档编辑、JSON 校验与时间轴绘制均由原生 C++ 类完成；此脚本只处理
## Godot EditorPlugin 生命周期和原生独立窗口的创建。
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
		_timeline_window.call("show_editor")
		return

	var instance := ClassDB.instantiate("NVTimelineEditorWindow")
	if instance == null:
		push_error("NV Timeline 原生编辑窗口未注册，请确认 GDExtension 已成功加载。")
		return
	_timeline_window = instance as Window
	if not is_instance_valid(_timeline_window):
		instance.free()
		push_error("NV Timeline 原生编辑窗口类型不是 Window。")
		return
	add_child(_timeline_window)
	_timeline_window.call("show_editor")

func _close_timeline_window() -> void:
	if is_instance_valid(_timeline_window):
		_timeline_window.queue_free()
	_timeline_window = null
