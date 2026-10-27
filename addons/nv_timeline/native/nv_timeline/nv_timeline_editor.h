#pragma once

// 编辑器窗口和时间轴画布只依赖 Godot 的通用 Control 类型；静态编排数据仍
// 统一交由 NVTimelineDocument 校验，避免编辑器产生运行时专属的第二套格式。
#include "nv_timeline.h"

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <vector>

namespace godot {

class Button;
class CheckBox;
class ColorPickerButton;
class ConfirmationDialog;
class FileDialog;
class HBoxContainer;
class HSlider;
class ItemList;
class Label;
class LineEdit;
class MarginContainer;
class OptionButton;
class PopupMenu;
class ScrollContainer;
class SpinBox;
class VBoxContainer;

/**
 * 时间轴轨道的原生绘制与鼠标交互层。
 *
 * 该控件只保留绘制快照和拖动预览，不持有文档所有权；拖放结束时通过信号把
 * 新时间和值域交回 NVTimelineEditorWindow，由文档类完成严格校验和保存。
 */
class NVTimelineCanvas : public Control {
    GDCLASS(NVTimelineCanvas, Control)

    friend class NVTimelineEditorWindow;

    struct VisualItem {
        int id = 0;
        int track_index = 0;
        int lane_index = 0;
        int start = 0;
        int duration = 0;
        bool is_mark = false;
        bool blocking = false;
        bool enabled_track = true;
        String name;
        String label;
        Color color = Color(0.30, 0.55, 0.78, 1.0);
    };

    std::vector<VisualItem> items;
    std::vector<int> track_ids;
    std::vector<float> track_row_tops;
    std::vector<float> track_row_heights;
    int track_count = 0;
    int timeline_duration = 0;
    int visible_end_units = 600;
    int selected_id = 0;
    int playhead_units = 0;
    int dragging_id = 0;
    int drag_origin_units = 0;
    int drag_original_start = 0;
    int drag_origin_duration = 0;
    int preview_start = 0;
    int preview_duration = 0;
    int resize_edge = 0;
    float zoom = 1.0f;
    float pixels_per_unit = 9.0f;
    bool dragging = false;
    bool scrubbing_playhead = false;

    static constexpr float RULER_HEIGHT = 34.0f;
    static constexpr float TRACK_HEIGHT = 58.0f;

    static void _bind_methods();
    int snap_to_units(float p_x) const;
    bool find_item_at(const Vector2 &p_position, VisualItem &r_item, int &r_edge) const;
    void update_minimum_size();
    void draw_item(const VisualItem &p_item, int p_stack_index);

public:
    NVTimelineCanvas();

    // godot-cpp 的虚函数桥接会在注册模板中访问这两个重载，因此必须保持公开。
    void _draw() override;
    void _gui_input(const Ref<InputEvent> &p_event) override;

    /// 用当前时间轴静态数据重建绘制快照，并同步缩放、选中项和本地播放头。
    void set_timeline_data(const Dictionary &p_timeline, float p_zoom,
            int p_selected_id, int p_playhead_units);
    void set_selected_id(int p_id);
    void set_playhead_units(int p_units);
    void set_zoom(float p_zoom);
    /// 返回按画布快照计算出的轨道行高，供左侧轨道表头保持垂直对齐。
    float get_track_row_height(int p_track_index) const;
};

/**
 * NV Timeline 的独立可视化编辑窗口。
 *
 * 所有编辑操作都先复制候选 Dictionary，再交给 NVTimelineDocument 做严格白名单
 * 校验；只有完整通过校验后才替换当前数据，因此非法中间编辑不会破坏已打开文档。
 */
class NVTimelineEditorWindow : public Window {
    GDCLASS(NVTimelineEditorWindow, Window)

    enum ObjectType {
        OBJECT_DOCUMENT,
        OBJECT_CLOCK,
        OBJECT_TIMELINE,
        OBJECT_TRACK,
        OBJECT_CLIP,
        OBJECT_MARK,
        OBJECT_LISTENER,
    };

    enum PendingAction {
        ACTION_NONE,
        ACTION_NEW_DOCUMENT,
        ACTION_OPEN_DOCUMENT,
        ACTION_CLOSE_WINDOW,
    };

    struct ObjectLocation {
        bool valid = false;
        ObjectType type = OBJECT_DOCUMENT;
        int clock_index = -1;
        int timeline_index = -1;
        int track_index = -1;
        int item_index = -1;
        Dictionary object;
    };

    Ref<NVTimelineDocument> document;
    Dictionary saved_data;
    std::vector<Dictionary> undo_history;
    std::vector<Dictionary> redo_history;
    String current_path;
    String status_message;
    int current_timeline_id = 0;
    int current_clock_id = 0;
    int selected_object_id = 0;
    int playhead_units = 0;
    float zoom = 1.0f;
    bool dirty = false;
    bool refreshing = false;
    PendingAction pending_action = ACTION_NONE;

    MarginContainer *margin_root = nullptr;
    VBoxContainer *root = nullptr;
    HBoxContainer *toolbar = nullptr;
    HBoxContainer *main_row = nullptr;
    VBoxContainer *left_panel = nullptr;
    VBoxContainer *center_panel = nullptr;
    VBoxContainer *inspector_panel = nullptr;
    VBoxContainer *track_headers = nullptr;
    ScrollContainer *body_scroll = nullptr;
    ScrollContainer *time_scroll = nullptr;
    NVTimelineCanvas *canvas = nullptr;
    Label *document_label = nullptr;
    Label *status_label = nullptr;
    OptionButton *clock_picker = nullptr;
    ItemList *listener_list = nullptr;
    HSlider *zoom_slider = nullptr;
    Label *zoom_label = nullptr;
    Label *selection_label = nullptr;
    Label *id_value = nullptr;
    LineEdit *name_edit = nullptr;
    CheckBox *enabled_check = nullptr;
    OptionButton *mode_picker = nullptr;
    SpinBox *rate_spin = nullptr;
    OptionButton *timeline_clock_picker = nullptr;
    CheckBox *unlimited_check = nullptr;
    SpinBox *duration_spin = nullptr;
    SpinBox *start_spin = nullptr;
    SpinBox *time_spin = nullptr;
    OptionButton *mark_kind_picker = nullptr;
    LineEdit *label_edit = nullptr;
    ColorPickerButton *color_picker = nullptr;
    Button *apply_button = nullptr;
    Button *remove_selected_button = nullptr;
    Button *undo_button = nullptr;
    Button *redo_button = nullptr;
    FileDialog *file_dialog = nullptr;
    ConfirmationDialog *dirty_dialog = nullptr;
    PopupMenu *track_context_menu = nullptr;
    bool file_dialog_saving = false;
    int context_track_id = 0;

    static void _bind_methods();
    void build_ui();
    void configure_dialogs();
    void refresh_all();
    void refresh_navigation();
    void refresh_track_headers();
    void refresh_canvas();
    void refresh_inspector();
    void update_window_title();
    void set_status(const String &p_message);
    void request_action(PendingAction p_action);
    void perform_pending_action();
    void show_file_dialog(bool p_saving);
    void add_default_clock_and_timeline(const String &p_name);
    void mark_dirty_and_refresh(const String &p_message);
    bool commit_document_data(const Dictionary &p_candidate, const String &p_message);
    bool replace_object(const ObjectLocation &p_location, const Dictionary &p_object);
    bool add_object_to_array(const String &p_array_key, const Dictionary &p_object);
    ObjectLocation find_object(int p_id) const;
    ObjectLocation get_selected_object() const;
    int find_current_timeline_index(const Dictionary &p_data) const;
    int find_current_track_index(const Dictionary &p_timeline) const;
    void select_object(int p_id);
    void populate_property_row(HBoxContainer *p_row, const String &p_title, Control *p_control);
    HBoxContainer *create_property_row(const String &p_title, Control *p_control);
    void set_property_row_visibility(Control *p_control, bool p_visible);
    void rebuild_clock_options(OptionButton *p_picker, int p_selected_id);

    void _on_new_pressed();
    void _on_open_pressed();
    void _on_save_pressed();
    void _on_save_as_pressed();
    void _on_undo_pressed();
    void _on_redo_pressed();
    void _on_file_selected(const String &p_path);
    void _on_dirty_confirmed();
    void _on_close_requested();
    void _on_clock_selected(int p_index);
    void _on_listener_selected(int p_index);
    void _on_add_clock_pressed();
    void _on_remove_clock_pressed();
    void _on_add_track_pressed();
    void _on_remove_track_pressed();
    void _on_move_track_up_pressed();
    void _on_move_track_down_pressed();
    void _on_add_clip_pressed();
    void _on_add_output_pressed();
    void _on_add_blocking_pressed();
    void _on_add_listener_pressed();
    void _on_remove_listener_pressed();
    void _on_remove_selected_visual_object_pressed();
    void _on_apply_properties_pressed();
    void _on_zoom_changed(double p_value);
    void _on_track_header_pressed(int p_track_id);
    void _on_track_header_gui_input(const Ref<InputEvent> &p_event, int p_track_id);
    void _on_track_context_id_pressed(int p_menu_id);
    void _on_canvas_track_context_requested(int p_track_id);
    void _on_track_enabled_toggled(bool p_enabled, int p_track_id);
    void _on_canvas_object_selected(int p_object_id);
    void _on_canvas_object_moved(int p_object_id, int p_start, int p_duration);
    void _on_canvas_playhead_changed(int p_units);

protected:
    void _notification(int p_what);

public:
    NVTimelineEditorWindow();

    /// 从插件入口调用，显示或聚焦非模态的独立编辑窗口。
    void show_editor();
    /// 打开并严格校验一个 .timeline.json 文件；失败时保留当前文档。
    bool open_document(const String &p_path);
    /// 将当前已校验文档写入目标文件；路径可以是 res:// 或绝对文件系统路径。
    bool save_document(const String &p_path);
    /// 创建带一个手动时钟和一条空时间轴的新文档。
    void create_new_document(const String &p_name = String::utf8("新时间轴文档"));
    Dictionary get_document_data() const;
    bool is_dirty() const { return dirty; }
    String get_current_path() const { return current_path; }
};

} // namespace godot
