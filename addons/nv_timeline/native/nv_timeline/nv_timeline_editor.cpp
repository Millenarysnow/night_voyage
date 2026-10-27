#include "nv_timeline_editor.h"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/check_box.hpp>
#include <godot_cpp/classes/color_picker_button.hpp>
#include <godot_cpp/classes/confirmation_dialog.hpp>
#include <godot_cpp/classes/file_dialog.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/h_slider.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/item_list.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/margin_container.hpp>
#include <godot_cpp/classes/option_button.hpp>
#include <godot_cpp/classes/popup_menu.hpp>
#include <godot_cpp/classes/scroll_container.hpp>
#include <godot_cpp/classes/spin_box.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/rect2.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace godot {

namespace {

// godot-cpp 的 const char* 构造函数按 Latin-1 解码；插件源码统一为 UTF-8，
// 所有包含非 ASCII 字符的界面文本必须显式通过该函数转换。
String utf8(const char *p_text) {
    return String::utf8(p_text);
}

// 配置字段缺省时只在编辑器显示层提供可读回退值，不把派生文本写回 JSON。
String display_name(const Dictionary &p_object, const String &p_fallback) {
    const String value = p_object.get("name", p_fallback);
    return value.is_empty() ? p_fallback : value;
}

// 在指定播放头之后寻找当前轨道的第一个空闲区间。片段可以首尾相接，
// 但不会因为连续点击“新增片段”而互相覆盖；有限时间轴的末端也作为硬边界。
bool find_free_clip_slot(const Array &p_clips, int p_requested_start, int p_requested_duration,
        int p_timeline_duration, int &r_start, int &r_duration) {
    std::vector<std::pair<int64_t, int64_t>> intervals;
    for (int64_t i = 0; i < p_clips.size(); ++i) {
        if (p_clips[i].get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary clip = p_clips[i];
        const int64_t start = int64_t(clip.get("start", 0));
        const int64_t duration = int64_t(clip.get("duration", 0));
        if (start >= 0 && duration > 0) {
            intervals.emplace_back(start, start + duration);
        }
    }
    std::sort(intervals.begin(), intervals.end(), [](const auto &p_left, const auto &p_right) {
        return p_left.first < p_right.first;
    });

    const int64_t requested_start = std::max<int64_t>(0, p_requested_start);
    const int64_t requested_duration = std::max<int64_t>(1, p_requested_duration);
    int64_t cursor = requested_start;
    for (const auto &interval : intervals) {
        if (interval.second <= cursor) {
            continue;
        }
        if (interval.first > cursor) {
            // 先使用当前片段前面的空档；空档不足默认长度时，缩短新片段填满空档。
            const int64_t gap = interval.first - cursor;
            const int64_t duration = std::min(requested_duration, gap);
            if (duration > 0 && (p_timeline_duration <= 0 || cursor + duration <= p_timeline_duration)) {
                if (cursor > std::numeric_limits<int32_t>::max() ||
                        duration > std::numeric_limits<int32_t>::max()) {
                    return false;
                }
                r_start = static_cast<int>(cursor);
                r_duration = static_cast<int>(duration);
                return true;
            }
        }
        cursor = std::max(cursor, interval.second);
    }

    if (p_timeline_duration > 0) {
        if (cursor >= p_timeline_duration) {
            return false;
        }
        const int64_t available = p_timeline_duration - cursor;
        const int64_t duration = std::min(requested_duration, available);
        if (duration <= 0) {
            return false;
        }
        r_start = static_cast<int>(cursor);
        r_duration = static_cast<int>(duration);
        return true;
    }

    if (cursor > std::numeric_limits<int32_t>::max() ||
            requested_duration > std::numeric_limits<int32_t>::max() - cursor) {
        return false;
    }
    r_start = static_cast<int>(cursor);
    r_duration = static_cast<int>(requested_duration);
    return true;
}

// 插件 JSON 规范使用 #RRGGBB；对旧文档或外部手工修改的错误颜色提供稳定回退。
Color clip_color(const Dictionary &p_clip) {
    const String value = p_clip.get("color", "#568AC8");
    return Color::html_is_valid(value) ? Color::html(value) : Color(0.30, 0.55, 0.78, 1.0);
}

// 将字符串拼接为多行错误提示，避免只显示首条校验问题而掩盖后续修复项。
String join_errors(const PackedStringArray &p_errors) {
    String result;
    for (int64_t i = 0; i < p_errors.size(); ++i) {
        if (i > 0) {
            result += "\n";
        }
        result += p_errors[i];
    }
    return result;
}

} // namespace

// ------------------------------ NVTimelineCanvas ------------------------------

NVTimelineCanvas::NVTimelineCanvas() {
    set_mouse_filter(Control::MOUSE_FILTER_STOP);
    set_focus_mode(Control::FOCUS_ALL);
}

void NVTimelineCanvas::_bind_methods() {
    ADD_SIGNAL(MethodInfo("object_selected", PropertyInfo(Variant::INT, "object_id")));
    ADD_SIGNAL(MethodInfo("object_moved", PropertyInfo(Variant::INT, "object_id"),
            PropertyInfo(Variant::INT, "start"), PropertyInfo(Variant::INT, "duration")));
    ADD_SIGNAL(MethodInfo("playhead_changed", PropertyInfo(Variant::INT, "time_units")));
    ADD_SIGNAL(MethodInfo("track_context_requested", PropertyInfo(Variant::INT, "track_id")));
}

void NVTimelineCanvas::set_timeline_data(const Dictionary &p_timeline, float p_zoom,
        int p_selected_id, int p_playhead_units) {
    items.clear();
    track_ids.clear();
    track_row_tops.clear();
    track_row_heights.clear();
    selected_id = p_selected_id;
    playhead_units = std::max(0, p_playhead_units);
    timeline_duration = 0;
    track_count = 0;

    const Variant duration_value = p_timeline.get("duration", Variant());
    if (duration_value.get_type() == Variant::INT) {
        timeline_duration = int(duration_value);
    }

    const Array tracks = p_timeline.get("tracks", Array());
    track_count = static_cast<int>(tracks.size());
    int maximum_end = std::max(600, timeline_duration);
    for (int64_t track_index = 0; track_index < tracks.size(); ++track_index) {
        if (tracks[track_index].get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary track = tracks[track_index];
        track_ids.push_back(int(track.get("id", 0)));
        const bool enabled = !track.has("enabled") || bool(track["enabled"]);
        const Array clips = track.get("clips", Array());
        for (int64_t i = 0; i < clips.size(); ++i) {
            if (clips[i].get_type() != Variant::DICTIONARY) {
                continue;
            }
            const Dictionary clip = clips[i];
            VisualItem item;
            item.id = int(clip.get("id", 0));
            item.track_index = static_cast<int>(track_index);
            item.start = int(clip.get("start", 0));
            item.duration = int(clip.get("duration", 1));
            item.enabled_track = enabled;
            item.name = display_name(clip, utf8("视觉事件段"));
            item.color = clip_color(clip);
            items.push_back(item);
            maximum_end = std::max(maximum_end, item.start + item.duration);
        }

        const Array marks = track.get("marks", Array());
        for (int64_t i = 0; i < marks.size(); ++i) {
            if (marks[i].get_type() != Variant::DICTIONARY) {
                continue;
            }
            const Dictionary mark = marks[i];
            VisualItem item;
            item.id = int(mark.get("id", 0));
            item.track_index = static_cast<int>(track_index);
            item.start = int(mark.get("time", 0));
            item.is_mark = true;
            item.blocking = String(mark.get("kind", "output")) == "blocking";
            item.enabled_track = enabled;
            item.name = display_name(mark, String(mark.get("label", utf8("标签标记"))));
            item.label = String(mark.get("label", String()));
            item.color = item.blocking ? Color(0.92, 0.61, 0.25, 1.0) : Color(0.38, 0.78, 0.63, 1.0);
            items.push_back(item);
            maximum_end = std::max(maximum_end, item.start + 100);
        }
    }

    // 文档校验器保证同一轨道的片段不重叠，因此正常编辑时每条轨道只有一层；
    // 这里仍保留防御性分层计算，让外部直接调用画布 API 时也不会相互遮挡。
    std::vector<std::vector<int>> clips_by_track(static_cast<size_t>(track_count));
    for (size_t item_index = 0; item_index < items.size(); ++item_index) {
        if (!items[item_index].is_mark && items[item_index].track_index >= 0 &&
                items[item_index].track_index < track_count) {
            clips_by_track[static_cast<size_t>(items[item_index].track_index)].push_back(
                    static_cast<int>(item_index));
        }
    }
    std::vector<int> lane_counts(static_cast<size_t>(track_count), 0);
    std::vector<int> maximum_mark_stacks(static_cast<size_t>(track_count), 0);
    std::vector<std::vector<std::pair<int, int>>> mark_stack_counts(static_cast<size_t>(track_count));
    for (const VisualItem &item : items) {
        if (!item.is_mark || item.track_index < 0 || item.track_index >= track_count) {
            continue;
        }
        std::vector<std::pair<int, int>> &time_counts = mark_stack_counts[static_cast<size_t>(item.track_index)];
        auto stack = std::find_if(time_counts.begin(), time_counts.end(), [&item](const auto &p_entry) {
            return p_entry.first == item.start;
        });
        if (stack == time_counts.end()) {
            time_counts.emplace_back(item.start, 1);
            maximum_mark_stacks[static_cast<size_t>(item.track_index)] = std::max(
                    maximum_mark_stacks[static_cast<size_t>(item.track_index)], 1);
        } else {
            ++stack->second;
            maximum_mark_stacks[static_cast<size_t>(item.track_index)] = std::max(
                    maximum_mark_stacks[static_cast<size_t>(item.track_index)], stack->second);
        }
    }

    for (int track_index = 0; track_index < track_count; ++track_index) {
        std::vector<int> &clip_indices = clips_by_track[static_cast<size_t>(track_index)];
        std::stable_sort(clip_indices.begin(), clip_indices.end(), [this](int p_left, int p_right) {
            return items[static_cast<size_t>(p_left)].start < items[static_cast<size_t>(p_right)].start;
        });
        std::vector<int> lane_ends;
        for (const int item_index : clip_indices) {
            VisualItem &item = items[static_cast<size_t>(item_index)];
            int lane_index = 0;
            while (lane_index < static_cast<int>(lane_ends.size()) && lane_ends[lane_index] > item.start) {
                ++lane_index;
            }
            if (lane_index == static_cast<int>(lane_ends.size())) {
                lane_ends.push_back(0);
            }
            item.lane_index = lane_index;
            lane_ends[static_cast<size_t>(lane_index)] = item.start + item.duration;
        }
        lane_counts[static_cast<size_t>(track_index)] = static_cast<int>(lane_ends.size());
    }

    // 基础行高保留给普通片段与时间标记；外部快照若包含异常重叠数据，
    // 按实际可见子行数扩展该轨道，避免绘制层相互遮挡。
    float next_row_top = RULER_HEIGHT;
    for (int track_index = 0; track_index < track_count; ++track_index) {
        const float clip_height = 9.0f + static_cast<float>(lane_counts[static_cast<size_t>(track_index)]) * 23.0f;
        const float mark_height = 20.0f + static_cast<float>(maximum_mark_stacks[static_cast<size_t>(track_index)]) * 12.0f;
        const float required_height = std::max(clip_height, mark_height);
        const float row_height = std::max(TRACK_HEIGHT, required_height);
        track_row_tops.push_back(next_row_top);
        track_row_heights.push_back(row_height);
        next_row_top += row_height;
    }

    visible_end_units = timeline_duration > 0 ? timeline_duration : maximum_end;
    visible_end_units = std::max(100, visible_end_units);
    set_zoom(p_zoom);
    update_minimum_size();
    queue_redraw();
}

void NVTimelineCanvas::set_selected_id(int p_id) {
    selected_id = p_id;
    queue_redraw();
}

void NVTimelineCanvas::set_playhead_units(int p_units) {
    playhead_units = std::max(0, p_units);
    queue_redraw();
}

void NVTimelineCanvas::set_zoom(float p_zoom) {
    zoom = std::clamp(p_zoom, 0.5f, 3.0f);
    pixels_per_unit = 9.0f * zoom;
    update_minimum_size();
    queue_redraw();
}

int NVTimelineCanvas::snap_to_units(float p_x) const {
    return std::max(0, static_cast<int>(std::lround(p_x / pixels_per_unit)));
}

float NVTimelineCanvas::get_track_row_height(int p_track_index) const {
    if (p_track_index < 0 || p_track_index >= static_cast<int>(track_row_heights.size())) {
        return TRACK_HEIGHT;
    }
    return track_row_heights[static_cast<size_t>(p_track_index)];
}

bool NVTimelineCanvas::find_item_at(const Vector2 &p_position, VisualItem &r_item, int &r_edge) const {
    if (p_position.y < RULER_HEIGHT) {
        return false;
    }
    int track_index = -1;
    for (int row = 0; row < static_cast<int>(track_row_tops.size()); ++row) {
        const float row_top = track_row_tops[static_cast<size_t>(row)];
        const float row_bottom = row_top + track_row_heights[static_cast<size_t>(row)];
        if (p_position.y >= row_top && p_position.y < row_bottom) {
            track_index = row;
            break;
        }
    }
    if (track_index < 0 || track_index >= track_count) {
        return false;
    }
    const float x = p_position.x;

    // 標記優先命中，否則與標記相交的片段會遮住窄小的標記拖動區域。
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
        if (it->track_index != track_index || !it->is_mark) {
            continue;
        }
        const float item_x = static_cast<float>(it->start) * pixels_per_unit;
        if (std::abs(x - item_x) <= 8.0f) {
            r_item = *it;
            r_edge = 0;
            return true;
        }
    }

    for (auto it = items.rbegin(); it != items.rend(); ++it) {
        if (it->track_index != track_index || it->is_mark) {
            continue;
        }
        const float left = static_cast<float>(it->start) * pixels_per_unit;
        const float right = static_cast<float>(it->start + it->duration) * pixels_per_unit;
        const float row_top = track_row_tops[static_cast<size_t>(track_index)];
        const float row_height = track_row_heights[static_cast<size_t>(track_index)];
        // 命中区域必须和 draw_item 使用同一套几何计算，否则片段虽然已经
        // 绘制为整行高度，鼠标却只能点到上半截，边缘缩放也会显得失灵。
        const float top = row_top + 4.0f + static_cast<float>(it->lane_index) * 23.0f;
        const float height = std::max(20.0f, row_height - 8.0f -
                static_cast<float>(it->lane_index) * 23.0f);
        if (x >= left && x <= right && p_position.y >= top && p_position.y <= top + height) {
            r_item = *it;
            // 片段两端提供略宽于视觉边线的拖动热区，窄片段仍优先按左端处理，
            // 避免左右热区在只有几个像素宽时互相覆盖。
            r_edge = x - left <= 8.0f ? -1 : (right - x <= 8.0f ? 1 : 0);
            return true;
        }
    }
    return false;
}

void NVTimelineCanvas::update_minimum_size() {
    const float width = std::max(720.0f, static_cast<float>(visible_end_units) * pixels_per_unit + 40.0f);
    float height = RULER_HEIGHT + TRACK_HEIGHT;
    if (!track_row_heights.empty()) {
        height = RULER_HEIGHT;
        for (const float row_height : track_row_heights) {
            height += row_height;
        }
    }
    set_custom_minimum_size(Vector2(width, height));
}

void NVTimelineCanvas::draw_item(const VisualItem &p_item, int p_stack_index) {
    const float row_top = p_item.track_index >= 0 && p_item.track_index < static_cast<int>(track_row_tops.size()) ?
            track_row_tops[static_cast<size_t>(p_item.track_index)] : RULER_HEIGHT;
    const float row_height = p_item.track_index >= 0 && p_item.track_index < static_cast<int>(track_row_heights.size()) ?
            track_row_heights[static_cast<size_t>(p_item.track_index)] : TRACK_HEIGHT;
    const float alpha = p_item.enabled_track ? 1.0f : 0.34f;
    const bool previewed = dragging && p_item.id == dragging_id;
    const int start = previewed ? preview_start : p_item.start;
    const int duration = previewed ? preview_duration : p_item.duration;
    const float x = static_cast<float>(start) * pixels_per_unit;

    if (p_item.is_mark) {
        const Color mark_color(p_item.color.r, p_item.color.g, p_item.color.b, alpha);
        const float label_y = row_top + 15.0f + static_cast<float>(p_stack_index) * 12.0f;
        draw_line(Vector2(x, row_top + 4.0f), Vector2(x, row_top + row_height - 4.0f), mark_color, 2.0f);
        draw_circle(Vector2(x, row_top + 7.0f), 4.0f, mark_color);
        String caption = p_item.name;
        if (!p_item.label.is_empty()) {
            caption += "  " + p_item.label;
        }
        if (get_theme_default_font().is_valid()) {
            draw_string(get_theme_default_font(), Vector2(x + 7.0f, label_y), caption,
                    HORIZONTAL_ALIGNMENT_LEFT, 260.0f, 12, Color(0.90f, 0.92f, 0.94f, alpha));
        }
        return;
    }

    const float width = std::max(pixels_per_unit, static_cast<float>(duration) * pixels_per_unit);
    // 同一轨道的片段已经禁止重叠，因此正常情况下片段占据整行并在上下留出
    // 少量边距；异常外部快照仍按 lane_index 向下偏移，避免绘制对象完全重合。
    const float lane_top = row_top + 4.0f + static_cast<float>(p_item.lane_index) * 23.0f;
    const float lane_height = std::max(20.0f, row_height - 8.0f -
            static_cast<float>(p_item.lane_index) * 23.0f);
    const Rect2 block(Vector2(x, lane_top), Vector2(width, lane_height));
    const Color fill(p_item.color.r, p_item.color.g, p_item.color.b, alpha * 0.72f);
    draw_rect(block, fill, true);
    draw_rect(block, p_item.id == selected_id ? Color(0.96f, 0.96f, 0.98f, alpha) :
            Color(0.72f, 0.78f, 0.84f, alpha), false, p_item.id == selected_id ? 2.0f : 1.0f);
    // 选中片段时强调左右边界，让用户能看出这两个位置可以拖动改变时长。
    if (p_item.id == selected_id) {
        const Color handle_color(0.98f, 0.88f, 0.48f, alpha);
        draw_line(Vector2(x + 2.0f, lane_top + 2.0f),
                Vector2(x + 2.0f, lane_top + lane_height - 2.0f), handle_color, 3.0f);
        draw_line(Vector2(x + width - 2.0f, lane_top + 2.0f),
                Vector2(x + width - 2.0f, lane_top + lane_height - 2.0f), handle_color, 3.0f);
    }
    if (get_theme_default_font().is_valid()) {
        const float text_y = lane_top + std::min(lane_height - 4.0f, lane_height * 0.5f + 4.0f);
        draw_string(get_theme_default_font(), Vector2(x + 5.0f, text_y), p_item.name,
                HORIZONTAL_ALIGNMENT_LEFT, std::max(20.0f, width - 8.0f), 10,
                Color(0.97f, 0.98f, 0.99f, alpha));
    }
}

void NVTimelineCanvas::_draw() {
    const Vector2 size = get_size();
    draw_rect(Rect2(Vector2(), size), Color(0.105f, 0.115f, 0.13f, 1.0f), true);
    draw_rect(Rect2(Vector2(), Vector2(size.x, RULER_HEIGHT)), Color(0.16f, 0.17f, 0.19f, 1.0f), true);

    for (int row = 0; row < track_count; ++row) {
        const float y = track_row_tops[static_cast<size_t>(row)];
        const float row_height = track_row_heights[static_cast<size_t>(row)];
        draw_rect(Rect2(Vector2(0.0f, y), Vector2(size.x, row_height)),
                row % 2 == 0 ? Color(0.125f, 0.135f, 0.15f, 1.0f) : Color(0.14f, 0.15f, 0.165f, 1.0f), true);
        draw_line(Vector2(0.0f, y + row_height), Vector2(size.x, y + row_height),
                Color(0.25f, 0.26f, 0.28f, 1.0f), 1.0f);
    }

    const int grid_end = std::min(visible_end_units, static_cast<int>(size.x / pixels_per_unit) + 2);
    for (int unit = 0; unit <= grid_end; ++unit) {
        const float x = static_cast<float>(unit) * pixels_per_unit;
        const bool whole_second = unit % 10 == 0;
        const Color grid_color = whole_second ? Color(0.31f, 0.33f, 0.36f, 1.0f) :
                Color(0.21f, 0.23f, 0.25f, 1.0f);
        draw_line(Vector2(x, RULER_HEIGHT), Vector2(x, size.y), grid_color, whole_second ? 1.0f : 0.5f);
        if (whole_second && get_theme_default_font().is_valid()) {
            const String seconds = String::num_real(static_cast<double>(unit) / 10.0, 1) + " s";
            draw_string(get_theme_default_font(), Vector2(x + 4.0f, 23.0f), seconds,
                    HORIZONTAL_ALIGNMENT_LEFT, 64.0f, 11, Color(0.82f, 0.84f, 0.87f, 1.0f));
        }
    }

    // 有限时长之外仍保留少量可滚动空间，方便观察边界；用半透明灰层明确区分
    // “时间轴有效区间”和画布的剩余背景，不把这部分误认为可编排时间。
    if (timeline_duration > 0) {
        const float boundary_x = static_cast<float>(timeline_duration) * pixels_per_unit;
        if (boundary_x < size.x) {
            draw_rect(Rect2(Vector2(boundary_x, 0.0f), Vector2(size.x - boundary_x, size.y)),
                    Color(0.42f, 0.43f, 0.45f, 0.46f), true);
            draw_line(Vector2(boundary_x, 0.0f), Vector2(boundary_x, size.y),
                    Color(0.76f, 0.78f, 0.80f, 0.90f), 2.0f);
        }
    }

    std::vector<std::pair<int, int>> mark_stacks;
    for (const VisualItem &item : items) {
        int stack_index = 0;
        if (item.is_mark) {
            for (const auto &entry : mark_stacks) {
                if (entry.first == item.track_index && entry.second == item.start) {
                    ++stack_index;
                }
            }
            mark_stacks.emplace_back(item.track_index, item.start);
        }
        draw_item(item, stack_index);
    }

    // 播放头仅属于编辑器会话状态，用于定位和添加对象，不写回编排 JSON。
    const float playhead_x = static_cast<float>(playhead_units) * pixels_per_unit;
    draw_line(Vector2(playhead_x, 0.0f), Vector2(playhead_x, size.y), Color(0.96f, 0.35f, 0.31f, 0.95f), 2.0f);
    draw_circle(Vector2(playhead_x, 1.0f), 5.0f, Color(0.96f, 0.35f, 0.31f, 0.95f));
}

void NVTimelineCanvas::_gui_input(const Ref<InputEvent> &p_event) {
    const Ref<InputEventMouseButton> mouse_button = p_event;
    if (mouse_button.is_valid() && mouse_button->get_button_index() == MOUSE_BUTTON_RIGHT &&
            mouse_button->is_pressed()) {
        // 画布轨道区域也提供右键菜单入口。表头按钮和画布共享同一个
        // track_context_requested 信号，用户不必精确点中左侧文字。
        const Vector2 position = mouse_button->get_position();
        for (int row = 0; row < static_cast<int>(track_row_tops.size()); ++row) {
            const float row_top = track_row_tops[static_cast<size_t>(row)];
            const float row_bottom = row_top + track_row_heights[static_cast<size_t>(row)];
            if (position.y >= row_top && position.y < row_bottom &&
                    row >= 0 && row < static_cast<int>(track_ids.size()) && track_ids[static_cast<size_t>(row)] != 0) {
                emit_signal("track_context_requested", track_ids[static_cast<size_t>(row)]);
                accept_event();
                return;
            }
        }
    }

    if (mouse_button.is_valid() && mouse_button->get_button_index() == MOUSE_BUTTON_LEFT) {
        const Vector2 position = mouse_button->get_position();
        if (mouse_button->is_pressed()) {
            // 播放头只接受标尺区域的鼠标操作。轨道空白区域保持纯粹的编排
            // 画布，不会因为误点而改变当前时间。
            if (position.y < RULER_HEIGHT) {
                scrubbing_playhead = true;
                playhead_units = snap_to_units(position.x);
                if (timeline_duration > 0) {
                    playhead_units = std::min(playhead_units, timeline_duration);
                }
                emit_signal("playhead_changed", playhead_units);
                queue_redraw();
                accept_event();
                return;
            }

            VisualItem item;
            int edge = 0;
            if (find_item_at(position, item, edge)) {
                selected_id = item.id;
                dragging_id = item.id;
                drag_origin_units = snap_to_units(position.x);
                drag_original_start = item.start;
                drag_origin_duration = item.duration;
                preview_start = item.start;
                preview_duration = item.duration;
                resize_edge = edge;
                dragging = true;
                emit_signal("object_selected", selected_id);
            }
            queue_redraw();
            accept_event();
            return;
        }

        if (scrubbing_playhead) {
            scrubbing_playhead = false;
            accept_event();
            return;
        }

        if (dragging) {
            dragging = false;
            if (preview_start != drag_original_start || preview_duration != drag_origin_duration) {
                emit_signal("object_moved", dragging_id, preview_start, preview_duration);
            }
            dragging_id = 0;
            queue_redraw();
            accept_event();
            return;
        }
    }

    const Ref<InputEventMouseMotion> mouse_motion = p_event;
    if (mouse_motion.is_valid() && scrubbing_playhead) {
        // 按住标尺拖动时持续更新播放头，而不是只响应按下瞬间。
        playhead_units = snap_to_units(mouse_motion->get_position().x);
        if (timeline_duration > 0) {
            playhead_units = std::min(playhead_units, timeline_duration);
        }
        emit_signal("playhead_changed", playhead_units);
        queue_redraw();
        accept_event();
        return;
    }

    if (mouse_motion.is_valid() && dragging) {
        const int delta = snap_to_units(mouse_motion->get_position().x) - drag_origin_units;
        const auto it = std::find_if(items.begin(), items.end(), [this](const VisualItem &p_item) {
            return p_item.id == dragging_id;
        });
        if (it == items.end()) {
            dragging = false;
            return;
        }

        if (it->is_mark || resize_edge == 0) {
            preview_start = std::max(0, it->start + delta);
        } else if (resize_edge < 0) {
            const int original_end = it->start + it->duration;
            preview_start = std::max(0, std::min(original_end - 1, it->start + delta));
            preview_duration = original_end - preview_start;
        } else {
            preview_duration = std::max(1, drag_origin_duration + delta);
        }

        if (timeline_duration > 0) {
            if (it->is_mark) {
                preview_start = std::min(preview_start, timeline_duration);
            } else {
                preview_start = std::min(preview_start, timeline_duration - 1);
                preview_duration = std::max(1, std::min(preview_duration, timeline_duration - preview_start));
            }
        }

        // 拖动预览阶段也遵守“同一轨道片段不能重叠”的规则，避免用户松开鼠标
        // 前短暂看到一个最终无法提交的重叠状态。只检查当前片段同轨道的其他片段，
        // 标签标记不占用片段空间。
        if (!it->is_mark) {
            int previous_end = 0;
            int next_start = timeline_duration > 0 ? timeline_duration : std::numeric_limits<int>::max();
            const int original_end = it->start + it->duration;
            for (const VisualItem &other : items) {
                if (other.is_mark || other.id == dragging_id || other.track_index != it->track_index) {
                    continue;
                }
                const int other_end = other.start + other.duration;
                if (other_end <= it->start) {
                    previous_end = std::max(previous_end, other_end);
                } else if (other.start >= original_end) {
                    next_start = std::min(next_start, other.start);
                }
            }

            if (resize_edge == 0) {
                const int maximum_start = next_start == std::numeric_limits<int>::max() ?
                        std::numeric_limits<int>::max() - preview_duration : next_start - preview_duration;
                preview_start = std::clamp(preview_start, previous_end, maximum_start);
            } else if (resize_edge < 0) {
                preview_start = std::clamp(preview_start, previous_end, original_end - 1);
                preview_duration = original_end - preview_start;
            } else {
                const int maximum_duration = std::max(1, next_start - it->start);
                preview_duration = std::clamp(preview_duration, 1, maximum_duration);
            }
        }
        queue_redraw();
        accept_event();
    }
}

// ---------------------------- NVTimelineEditorWindow ---------------------------

void NVTimelineEditorWindow::_bind_methods() {
    // 这些方法供插件入口和控件信号调用；GDScript 只负责启动窗口，不复制数据编辑逻辑。
    ClassDB::bind_method(D_METHOD("show_editor"), &NVTimelineEditorWindow::show_editor);
    ClassDB::bind_method(D_METHOD("open_document", "path"), &NVTimelineEditorWindow::open_document);
    ClassDB::bind_method(D_METHOD("save_document", "path"), &NVTimelineEditorWindow::save_document);
    ClassDB::bind_method(D_METHOD("create_new_document", "name"), &NVTimelineEditorWindow::create_new_document,
            DEFVAL(utf8("新时间轴文档")));
    ClassDB::bind_method(D_METHOD("get_document_data"), &NVTimelineEditorWindow::get_document_data);
    ClassDB::bind_method(D_METHOD("is_dirty"), &NVTimelineEditorWindow::is_dirty);
    ClassDB::bind_method(D_METHOD("get_current_path"), &NVTimelineEditorWindow::get_current_path);

    ClassDB::bind_method(D_METHOD("_on_new_pressed"), &NVTimelineEditorWindow::_on_new_pressed);
    ClassDB::bind_method(D_METHOD("_on_open_pressed"), &NVTimelineEditorWindow::_on_open_pressed);
    ClassDB::bind_method(D_METHOD("_on_save_pressed"), &NVTimelineEditorWindow::_on_save_pressed);
    ClassDB::bind_method(D_METHOD("_on_save_as_pressed"), &NVTimelineEditorWindow::_on_save_as_pressed);
    ClassDB::bind_method(D_METHOD("_on_undo_pressed"), &NVTimelineEditorWindow::_on_undo_pressed);
    ClassDB::bind_method(D_METHOD("_on_redo_pressed"), &NVTimelineEditorWindow::_on_redo_pressed);
    ClassDB::bind_method(D_METHOD("_on_file_selected", "path"), &NVTimelineEditorWindow::_on_file_selected);
    ClassDB::bind_method(D_METHOD("_on_dirty_confirmed"), &NVTimelineEditorWindow::_on_dirty_confirmed);
    ClassDB::bind_method(D_METHOD("_on_close_requested"), &NVTimelineEditorWindow::_on_close_requested);
    ClassDB::bind_method(D_METHOD("_on_clock_selected", "index"), &NVTimelineEditorWindow::_on_clock_selected);
    ClassDB::bind_method(D_METHOD("_on_listener_selected", "index"), &NVTimelineEditorWindow::_on_listener_selected);
    ClassDB::bind_method(D_METHOD("_on_add_clock_pressed"), &NVTimelineEditorWindow::_on_add_clock_pressed);
    ClassDB::bind_method(D_METHOD("_on_remove_clock_pressed"), &NVTimelineEditorWindow::_on_remove_clock_pressed);
    ClassDB::bind_method(D_METHOD("_on_add_track_pressed"), &NVTimelineEditorWindow::_on_add_track_pressed);
    ClassDB::bind_method(D_METHOD("_on_remove_track_pressed"), &NVTimelineEditorWindow::_on_remove_track_pressed);
    ClassDB::bind_method(D_METHOD("_on_move_track_up_pressed"), &NVTimelineEditorWindow::_on_move_track_up_pressed);
    ClassDB::bind_method(D_METHOD("_on_move_track_down_pressed"), &NVTimelineEditorWindow::_on_move_track_down_pressed);
    ClassDB::bind_method(D_METHOD("_on_add_clip_pressed"), &NVTimelineEditorWindow::_on_add_clip_pressed);
    ClassDB::bind_method(D_METHOD("_on_add_output_pressed"), &NVTimelineEditorWindow::_on_add_output_pressed);
    ClassDB::bind_method(D_METHOD("_on_add_blocking_pressed"), &NVTimelineEditorWindow::_on_add_blocking_pressed);
    ClassDB::bind_method(D_METHOD("_on_add_listener_pressed"), &NVTimelineEditorWindow::_on_add_listener_pressed);
    ClassDB::bind_method(D_METHOD("_on_remove_listener_pressed"), &NVTimelineEditorWindow::_on_remove_listener_pressed);
    ClassDB::bind_method(D_METHOD("_on_remove_selected_visual_object_pressed"),
            &NVTimelineEditorWindow::_on_remove_selected_visual_object_pressed);
    ClassDB::bind_method(D_METHOD("_on_apply_properties_pressed"), &NVTimelineEditorWindow::_on_apply_properties_pressed);
    ClassDB::bind_method(D_METHOD("_on_zoom_changed", "value"), &NVTimelineEditorWindow::_on_zoom_changed);
    ClassDB::bind_method(D_METHOD("_on_track_header_pressed", "track_id"), &NVTimelineEditorWindow::_on_track_header_pressed);
    ClassDB::bind_method(D_METHOD("_on_track_header_gui_input", "event", "track_id"),
            &NVTimelineEditorWindow::_on_track_header_gui_input);
    ClassDB::bind_method(D_METHOD("_on_track_context_id_pressed", "menu_id"),
            &NVTimelineEditorWindow::_on_track_context_id_pressed);
    ClassDB::bind_method(D_METHOD("_on_track_enabled_toggled", "enabled", "track_id"),
            &NVTimelineEditorWindow::_on_track_enabled_toggled);
    ClassDB::bind_method(D_METHOD("_on_canvas_object_selected", "object_id"),
            &NVTimelineEditorWindow::_on_canvas_object_selected);
    ClassDB::bind_method(D_METHOD("_on_canvas_object_moved", "object_id", "start", "duration"),
            &NVTimelineEditorWindow::_on_canvas_object_moved);
    ClassDB::bind_method(D_METHOD("_on_canvas_playhead_changed", "time_units"),
            &NVTimelineEditorWindow::_on_canvas_playhead_changed);
    ClassDB::bind_method(D_METHOD("_on_canvas_track_context_requested", "track_id"),
            &NVTimelineEditorWindow::_on_canvas_track_context_requested);
}

NVTimelineEditorWindow::NVTimelineEditorWindow() {
    document.instantiate();
    build_ui();
    configure_dialogs();
    create_new_document();
    connect("close_requested", Callable(this, "_on_close_requested"));
}

void NVTimelineEditorWindow::build_ui() {
    set_title("NV Timeline");
    set_size(Vector2i(1420, 820));
    set_min_size(Vector2i(1040, 620));
    set_initial_position(Window::WINDOW_INITIAL_POSITION_CENTER_MAIN_WINDOW_SCREEN);

    margin_root = memnew(MarginContainer);
    margin_root->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
    add_child(margin_root);

    root = memnew(VBoxContainer);
    margin_root->add_child(root);

    toolbar = memnew(HBoxContainer);
    root->add_child(toolbar);
    auto add_toolbar_button = [this](const String &p_text, const String &p_method) {
        Button *button = memnew(Button);
        button->set_text(p_text);
        button->connect("pressed", Callable(this, p_method));
        toolbar->add_child(button);
        return button;
    };
    add_toolbar_button(utf8("新建"), "_on_new_pressed");
    add_toolbar_button(utf8("打开"), "_on_open_pressed");
    add_toolbar_button(utf8("保存"), "_on_save_pressed");
    add_toolbar_button(utf8("另存为"), "_on_save_as_pressed");
    undo_button = add_toolbar_button(utf8("撤销"), "_on_undo_pressed");
    undo_button->set_tooltip_text(utf8("撤销最近一次时间轴编辑"));
    redo_button = add_toolbar_button(utf8("重做"), "_on_redo_pressed");
    redo_button->set_tooltip_text(utf8("重做最近撤销的时间轴编辑"));
    document_label = memnew(Label);
    document_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    document_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
    toolbar->add_child(document_label);

    main_row = memnew(HBoxContainer);
    main_row->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    root->add_child(main_row);

    left_panel = memnew(VBoxContainer);
    left_panel->set_custom_minimum_size(Vector2(232.0f, 0.0f));
    main_row->add_child(left_panel);

    Button *document_properties = memnew(Button);
    document_properties->set_text(utf8("文档属性"));
    document_properties->connect("pressed", Callable(this, "_on_canvas_object_selected").bind(0));
    left_panel->add_child(document_properties);

    Label *clock_title = memnew(Label);
    clock_title->set_text(utf8("逻辑时钟"));
    left_panel->add_child(clock_title);
    HBoxContainer *clock_row = memnew(HBoxContainer);
    left_panel->add_child(clock_row);
    clock_picker = memnew(OptionButton);
    clock_picker->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    clock_picker->connect("item_selected", Callable(this, "_on_clock_selected"));
    clock_row->add_child(clock_picker);
    Button *add_clock = memnew(Button);
    add_clock->set_text("+");
    add_clock->set_tooltip_text(utf8("新增逻辑时钟"));
    add_clock->connect("pressed", Callable(this, "_on_add_clock_pressed"));
    clock_row->add_child(add_clock);
    Button *remove_clock = memnew(Button);
    remove_clock->set_text(utf8("−"));
    remove_clock->set_tooltip_text(utf8("删除所选逻辑时钟"));
    remove_clock->connect("pressed", Callable(this, "_on_remove_clock_pressed"));
    clock_row->add_child(remove_clock);

    Label *listener_title = memnew(Label);
    listener_title->set_text(utf8("全程输入"));
    left_panel->add_child(listener_title);
    listener_list = memnew(ItemList);
    listener_list->set_custom_minimum_size(Vector2(0.0f, 150.0f));
    listener_list->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    listener_list->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    listener_list->connect("item_selected", Callable(this, "_on_listener_selected"));
    left_panel->add_child(listener_list);
    HBoxContainer *listener_buttons = memnew(HBoxContainer);
    left_panel->add_child(listener_buttons);
    Button *add_listener = memnew(Button);
    add_listener->set_text(utf8("新增输入"));
    add_listener->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    add_listener->connect("pressed", Callable(this, "_on_add_listener_pressed"));
    listener_buttons->add_child(add_listener);
    Button *remove_listener = memnew(Button);
    remove_listener->set_text(utf8("删除"));
    remove_listener->connect("pressed", Callable(this, "_on_remove_listener_pressed"));
    listener_buttons->add_child(remove_listener);

    center_panel = memnew(VBoxContainer);
    center_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    center_panel->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    main_row->add_child(center_panel);

    HBoxContainer *edit_toolbar = memnew(HBoxContainer);
    center_panel->add_child(edit_toolbar);
    auto add_edit_button = [this, edit_toolbar](const String &p_text, const String &p_method, const String &p_tip) {
        Button *button = memnew(Button);
        button->set_text(p_text);
        button->set_tooltip_text(p_tip);
        button->connect("pressed", Callable(this, p_method));
        edit_toolbar->add_child(button);
        return button;
    };
    add_edit_button(utf8("新增轨道"), "_on_add_track_pressed", utf8("在当前时间轴末尾新增一条轨道"));
    add_edit_button(utf8("删除轨道"), "_on_remove_track_pressed", utf8("删除所选轨道及其片段和标记"));
    add_edit_button(utf8("上移"), "_on_move_track_up_pressed", utf8("将所选轨道上移一行"));
    add_edit_button(utf8("下移"), "_on_move_track_down_pressed", utf8("将所选轨道下移一行"));
    add_edit_button(utf8("新增片段"), "_on_add_clip_pressed", utf8("在所选轨道的播放头位置新增纯视觉片段"));
    add_edit_button(utf8("输出标记"), "_on_add_output_pressed", utf8("在播放头位置新增输出标签标记"));
    add_edit_button(utf8("阻塞输入"), "_on_add_blocking_pressed", utf8("在播放头位置新增阻塞输入标记"));
    zoom_label = memnew(Label);
    zoom_label->set_text(utf8("缩放"));
    edit_toolbar->add_child(zoom_label);
    zoom_slider = memnew(HSlider);
    zoom_slider->set_custom_minimum_size(Vector2(100.0f, 0.0f));
    zoom_slider->set_min(0.5);
    zoom_slider->set_max(3.0);
    zoom_slider->set_step(0.25);
    zoom_slider->set_value(zoom);
    zoom_slider->connect("value_changed", Callable(this, "_on_zoom_changed"));
    edit_toolbar->add_child(zoom_slider);

    body_scroll = memnew(ScrollContainer);
    body_scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    body_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    body_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
    body_scroll->set_vertical_scroll_mode(ScrollContainer::SCROLL_MODE_AUTO);
    center_panel->add_child(body_scroll);
    HBoxContainer *timeline_body = memnew(HBoxContainer);
    // ScrollContainer 不会自动把唯一子节点拉伸到视口大小；若保持默认尺寸标志，
    // 时间轴主体只会按左侧表头的最小宽度布局，内部时间滚动区会被压缩到 0 像素。
    // 显式扩展主体后，左侧轨道表头与右侧画布才能共同占满可用区域。
    timeline_body->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    timeline_body->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    body_scroll->add_child(timeline_body);
    track_headers = memnew(VBoxContainer);
    track_headers->set_custom_minimum_size(Vector2(188.0f, 0.0f));
    // 表头行与画布行使用同一高度；去掉 VBox 默认间距，避免滚动后逐行错位。
    track_headers->add_theme_constant_override("separation", 0);
    timeline_body->add_child(track_headers);
    time_scroll = memnew(ScrollContainer);
    time_scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    time_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    time_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_AUTO);
    time_scroll->set_vertical_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
    timeline_body->add_child(time_scroll);
    canvas = memnew(NVTimelineCanvas);
    canvas->connect("object_selected", Callable(this, "_on_canvas_object_selected"));
    canvas->connect("object_moved", Callable(this, "_on_canvas_object_moved"));
    canvas->connect("playhead_changed", Callable(this, "_on_canvas_playhead_changed"));
    canvas->connect("track_context_requested", Callable(this, "_on_canvas_track_context_requested"));
    time_scroll->add_child(canvas);

    inspector_panel = memnew(VBoxContainer);
    inspector_panel->set_custom_minimum_size(Vector2(272.0f, 0.0f));
    main_row->add_child(inspector_panel);
    Label *inspector_title = memnew(Label);
    inspector_title->set_text(utf8("属性"));
    inspector_panel->add_child(inspector_title);
    selection_label = memnew(Label);
    selection_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
    inspector_panel->add_child(selection_label);
    id_value = memnew(Label);
    // ID 只用于 JSON 引用和程序内部定位，不作为用户可见的编辑属性。
    id_value->set_visible(false);
    inspector_panel->add_child(id_value);

    name_edit = memnew(LineEdit);
    name_edit->set_placeholder(utf8("名称"));
    HBoxContainer *name_row = create_property_row(utf8("名称"), name_edit);
    inspector_panel->add_child(name_row);
    enabled_check = memnew(CheckBox);
    enabled_check->set_text(utf8("启用"));
    HBoxContainer *enabled_row = create_property_row(utf8("状态"), enabled_check);
    inspector_panel->add_child(enabled_row);
    mode_picker = memnew(OptionButton);
    mode_picker->add_item(utf8("手动"));
    mode_picker->set_item_id(0, NVTimelineClock::MODE_MANUAL);
    mode_picker->add_item(utf8("实时"));
    mode_picker->set_item_id(1, NVTimelineClock::MODE_REALTIME);
    HBoxContainer *mode_row = create_property_row(utf8("模式"), mode_picker);
    inspector_panel->add_child(mode_row);
    rate_spin = memnew(SpinBox);
    rate_spin->set_min(0.01);
    rate_spin->set_max(100.0);
    rate_spin->set_step(0.05);
    HBoxContainer *rate_row = create_property_row(utf8("实时倍率"), rate_spin);
    inspector_panel->add_child(rate_row);
    timeline_clock_picker = memnew(OptionButton);
    HBoxContainer *clock_ref_row = create_property_row(utf8("绑定时钟"), timeline_clock_picker);
    inspector_panel->add_child(clock_ref_row);
    unlimited_check = memnew(CheckBox);
    unlimited_check->set_text(utf8("不限时长"));
    HBoxContainer *unlimited_row = create_property_row(utf8("时长"), unlimited_check);
    inspector_panel->add_child(unlimited_row);
    duration_spin = memnew(SpinBox);
    duration_spin->set_min(1);
    duration_spin->set_max(std::numeric_limits<int32_t>::max());
    duration_spin->set_step(1);
    HBoxContainer *duration_row = create_property_row(utf8("长度（0.1 秒）"), duration_spin);
    inspector_panel->add_child(duration_row);
    start_spin = memnew(SpinBox);
    start_spin->set_min(0);
    start_spin->set_max(std::numeric_limits<int32_t>::max());
    start_spin->set_step(1);
    HBoxContainer *start_row = create_property_row(utf8("起点（0.1 秒）"), start_spin);
    inspector_panel->add_child(start_row);
    time_spin = memnew(SpinBox);
    time_spin->set_min(0);
    time_spin->set_max(std::numeric_limits<int32_t>::max());
    time_spin->set_step(1);
    HBoxContainer *time_row = create_property_row(utf8("时间（0.1 秒）"), time_spin);
    inspector_panel->add_child(time_row);
    mark_kind_picker = memnew(OptionButton);
    mark_kind_picker->add_item(utf8("输出标签"));
    mark_kind_picker->set_item_id(0, 0);
    mark_kind_picker->add_item(utf8("阻塞输入"));
    mark_kind_picker->set_item_id(1, 1);
    HBoxContainer *kind_row = create_property_row(utf8("标记类型"), mark_kind_picker);
    inspector_panel->add_child(kind_row);
    label_edit = memnew(LineEdit);
    label_edit->set_placeholder(utf8("区分大小写的标签"));
    HBoxContainer *label_row = create_property_row(utf8("标签"), label_edit);
    inspector_panel->add_child(label_row);
    color_picker = memnew(ColorPickerButton);
    color_picker->set_edit_alpha(false);
    HBoxContainer *color_row = create_property_row(utf8("显示颜色"), color_picker);
    inspector_panel->add_child(color_row);
    apply_button = memnew(Button);
    apply_button->set_text(utf8("应用属性"));
    apply_button->connect("pressed", Callable(this, "_on_apply_properties_pressed"));
    inspector_panel->add_child(apply_button);
    remove_selected_button = memnew(Button);
    remove_selected_button->set_text(utf8("删除所选对象"));
    remove_selected_button->connect("pressed", Callable(this, "_on_remove_selected_visual_object_pressed"));
    inspector_panel->add_child(remove_selected_button);

    Label *binding_notice = memnew(Label);
    binding_notice->set_text(utf8("标签处理绑定由程序管理；此窗口仅编辑 JSON 标签，不修改处理代码。"));
    binding_notice->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
    inspector_panel->add_child(binding_notice);

    status_label = memnew(Label);
    status_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
    root->add_child(status_label);

    // 初始属性面板由 refresh_inspector 按选中对象决定显示哪些字段。
    const std::array<Control *, 12> property_controls = {name_edit, enabled_check, mode_picker, rate_spin,
            timeline_clock_picker, unlimited_check, duration_spin, start_spin, time_spin,
            mark_kind_picker, label_edit, color_picker};
    for (Control *control : property_controls) {
        set_property_row_visibility(control, false);
    }
    apply_button->set_visible(false);
    remove_selected_button->set_visible(false);
}

HBoxContainer *NVTimelineEditorWindow::create_property_row(const String &p_title, Control *p_control) {
    HBoxContainer *row = memnew(HBoxContainer);
    Label *label = memnew(Label);
    label->set_text(p_title);
    label->set_custom_minimum_size(Vector2(104.0f, 0.0f));
    row->add_child(label);
    p_control->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    row->add_child(p_control);
    return row;
}

void NVTimelineEditorWindow::populate_property_row(HBoxContainer *p_row, const String &p_title, Control *p_control) {
    if (p_row == nullptr || p_control == nullptr) {
        return;
    }
    Label *label = Object::cast_to<Label>(p_row->get_child(0));
    if (label != nullptr) {
        label->set_text(p_title);
    }
    p_control->set_h_size_flags(Control::SIZE_EXPAND_FILL);
}

void NVTimelineEditorWindow::set_property_row_visibility(Control *p_control, bool p_visible) {
    if (p_control == nullptr) {
        return;
    }
    Control *row = Object::cast_to<Control>(p_control->get_parent());
    if (row != nullptr) {
        row->set_visible(p_visible);
    }
}

void NVTimelineEditorWindow::configure_dialogs() {
    file_dialog = memnew(FileDialog);
    file_dialog->set_access(FileDialog::ACCESS_FILESYSTEM);
    file_dialog->set_use_native_dialog(false);
    file_dialog->add_filter("*.timeline.json ; NV Timeline JSON");
    file_dialog->add_filter("*.json ; JSON");
    file_dialog->connect("file_selected", Callable(this, "_on_file_selected"));
    add_child(file_dialog);

    dirty_dialog = memnew(ConfirmationDialog);
    dirty_dialog->set_title(utf8("未保存的时间轴修改"));
    dirty_dialog->set_text(utf8("当前文档有未保存修改。确认将放弃修改；取消后可以继续编辑或先保存。"));
    dirty_dialog->get_ok_button()->set_text(utf8("放弃修改"));
    dirty_dialog->connect("confirmed", Callable(this, "_on_dirty_confirmed"));
    add_child(dirty_dialog);

    // 轨道右键菜单只保存临时的轨道 ID，具体新增动作仍走现有文档编辑函数，
    // 因此不会为右键操作引入第二套 JSON 修改路径。
    track_context_menu = memnew(PopupMenu);
    track_context_menu->add_item(utf8("新增片段"), 0);
    track_context_menu->add_item(utf8("新增输出标记"), 1);
    track_context_menu->add_item(utf8("新增阻塞输入"), 2);
    track_context_menu->connect("id_pressed", Callable(this, "_on_track_context_id_pressed"));
    add_child(track_context_menu);
}

void NVTimelineEditorWindow::refresh_all() {
    if (document.is_null()) {
        return;
    }
    refreshing = true;
    refresh_navigation();
    refresh_canvas();
    refresh_track_headers();
    refresh_inspector();
    undo_button->set_disabled(undo_history.empty());
    redo_button->set_disabled(redo_history.empty());
    update_window_title();
    refreshing = false;
}

void NVTimelineEditorWindow::refresh_navigation() {
    const Dictionary data = document->get_data();
    const Array clocks = data.get("clocks", Array());
    const Array timelines = data.get("timelines", Array());

    bool clock_exists = false;
    clock_picker->clear();
    for (int64_t i = 0; i < clocks.size(); ++i) {
        const Dictionary clock = clocks[i];
        const int id = int(clock.get("id", 0));
        clock_picker->add_item(display_name(clock, utf8("时钟 ") + String::num_int64(id)), id);
        if (id == current_clock_id) {
            clock_picker->select(static_cast<int>(i));
            clock_exists = true;
        }
    }
    if (!clock_exists && !clocks.is_empty()) {
        current_clock_id = int(Dictionary(clocks[0]).get("id", 0));
        clock_picker->select(0);
    }
    if (clocks.is_empty()) {
        current_clock_id = 0;
    }

    // 文件只允许一条时间轴；名称和时长通过画布左上角的时间轴按钮进入属性面板。
    if (timelines.size() == 1 && timelines[0].get_type() == Variant::DICTIONARY) {
        const Dictionary timeline = timelines[0];
        current_timeline_id = int(timeline.get("id", 0));
        current_clock_id = int(timeline.get("clock_id", 0));
    } else {
        current_timeline_id = 0;
    }

    listener_list->clear();
    const int timeline_index = find_current_timeline_index(data);
    if (timeline_index >= 0) {
        const Dictionary timeline = timelines[timeline_index];
        const Array listeners = timeline.get("listeners", Array());
        for (int64_t i = 0; i < listeners.size(); ++i) {
            const Dictionary listener = listeners[i];
            const int id = int(listener.get("id", 0));
            const bool enabled = !listener.has("enabled") || bool(listener["enabled"]);
            const String state = enabled ? "" : utf8("（禁用）");
            listener_list->add_item(state + display_name(listener, String(listener.get("label", utf8("输入")))) +
                    utf8("   ·   ") + String(listener.get("label", String())));
            listener_list->set_item_metadata(static_cast<int>(i), id);
            if (id == selected_object_id) {
                listener_list->select(static_cast<int>(i));
            }
        }
    }

    const String name = String(data.get("name", utf8("新时间轴文档")));
    document_label->set_text(name);
}

void NVTimelineEditorWindow::refresh_track_headers() {
    while (track_headers->get_child_count() > 0) {
        Node *child = track_headers->get_child(0);
        track_headers->remove_child(child);
        child->queue_free();
    }

    // 画布标尺左侧显示当前时间轴名称，并允许直接点击它选中时间轴对象。
    // 这样可以从画布入口进入右侧的时长编辑属性；按钮采用明显的填充样式，
    // 避免用户把它误认为只是表头文字。
    Button *ruler_spacer = memnew(Button);
    ruler_spacer->set_text(utf8("时间轴："));
    ruler_spacer->set_flat(false);
    ruler_spacer->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
    ruler_spacer->add_theme_font_size_override("font_size", 15);
    ruler_spacer->add_theme_color_override("font_color", Color(0.90f, 0.93f, 0.98f, 1.0f));
    ruler_spacer->add_theme_color_override("font_hover_color", Color(1.0f, 0.93f, 0.60f, 1.0f));
    ruler_spacer->set_tooltip_text(utf8("选择当前时间轴并编辑总时长"));
    ruler_spacer->set_custom_minimum_size(Vector2(188.0f, NVTimelineCanvas::RULER_HEIGHT));
    ruler_spacer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    ruler_spacer->connect("pressed", Callable(this, "_on_canvas_object_selected").bind(current_timeline_id));
    track_headers->add_child(ruler_spacer);

    const Dictionary data = document->get_data();
    const int timeline_index = find_current_timeline_index(data);
    if (timeline_index < 0) {
        return;
    }
    const Array timelines = data.get("timelines", Array());
    const Dictionary timeline = timelines[timeline_index];
    ruler_spacer->set_text(utf8("时间轴：") + display_name(timeline, utf8("未命名时间轴")));
    const Array tracks = timeline.get("tracks", Array());
    for (int64_t i = 0; i < tracks.size(); ++i) {
        const Dictionary track = tracks[i];
        const int id = int(track.get("id", 0));
        HBoxContainer *row = memnew(HBoxContainer);
        row->set_custom_minimum_size(Vector2(188.0f, canvas->get_track_row_height(static_cast<int>(i))));
        CheckBox *enabled = memnew(CheckBox);
        enabled->set_tooltip_text(utf8("轨道是否参与运行时标签输出和阻塞处理"));
        enabled->set_pressed(!track.has("enabled") || bool(track["enabled"]));
        enabled->connect("toggled", Callable(this, "_on_track_enabled_toggled").bind(id));
        row->add_child(enabled);
        Button *select = memnew(Button);
        select->set_text(display_name(track, utf8("轨道 ") + String::num_int64(id)));
        select->set_toggle_mode(true);
        select->set_pressed(id == selected_object_id);
        select->set_h_size_flags(Control::SIZE_EXPAND_FILL);
        select->connect("pressed", Callable(this, "_on_track_header_pressed").bind(id));
        // 表头单独接收 GUI 输入，右键时弹出针对当前轨道的新增菜单；左键
        // 仍由 pressed 信号走原有选择逻辑。
        select->connect("gui_input", Callable(this, "_on_track_header_gui_input").bind(id));
        row->add_child(select);
        track_headers->add_child(row);
    }
}

void NVTimelineEditorWindow::refresh_canvas() {
    const Dictionary data = document->get_data();
    const int timeline_index = find_current_timeline_index(data);
    if (timeline_index < 0) {
        canvas->set_timeline_data(Dictionary(), zoom, selected_object_id, playhead_units);
        return;
    }
    const Array timelines = data.get("timelines", Array());
    canvas->set_timeline_data(Dictionary(timelines[timeline_index]), zoom,
            selected_object_id, playhead_units);
}

void NVTimelineEditorWindow::refresh_inspector() {
    const std::array<Control *, 12> property_controls = {name_edit, enabled_check, mode_picker, rate_spin,
            timeline_clock_picker, unlimited_check, duration_spin, start_spin, time_spin,
            mark_kind_picker, label_edit, color_picker};
    for (Control *control : property_controls) {
        set_property_row_visibility(control, false);
    }
    apply_button->set_visible(false);
    remove_selected_button->set_visible(false);
    id_value->set_text("");

    ObjectLocation location = get_selected_object();
    if (!location.valid) {
        selection_label->set_text(utf8("未选择对象"));
        return;
    }

    const Dictionary object = location.object;
    String type_name;
    switch (location.type) {
        case OBJECT_DOCUMENT: type_name = utf8("时间轴文档"); break;
        case OBJECT_CLOCK: type_name = utf8("逻辑时钟"); break;
        case OBJECT_TIMELINE: type_name = utf8("时间轴"); break;
        case OBJECT_TRACK: type_name = utf8("轨道"); break;
        case OBJECT_CLIP: type_name = utf8("视觉事件段"); break;
        case OBJECT_MARK: type_name = utf8("标签标记"); break;
        case OBJECT_LISTENER: type_name = utf8("全程输入"); break;
    }
    selection_label->set_text(type_name);
    name_edit->set_text(String(object.get("name", location.type == OBJECT_DOCUMENT ?
            utf8("新时间轴文档") : String(object.get("label", String())))));
    set_property_row_visibility(name_edit, true);

    const bool enabled = !object.has("enabled") || bool(object["enabled"]);
    enabled_check->set_pressed(enabled);
    switch (location.type) {
        case OBJECT_DOCUMENT:
            break;
        case OBJECT_CLOCK: {
            const int mode_id = String(object.get("mode", "manual")) == "realtime" ?
                    NVTimelineClock::MODE_REALTIME : NVTimelineClock::MODE_MANUAL;
            mode_picker->select(mode_id == NVTimelineClock::MODE_REALTIME ? 1 : 0);
            rate_spin->set_value(double(object.get("rate", 1.0)));
            set_property_row_visibility(mode_picker, true);
            set_property_row_visibility(rate_spin, mode_id == NVTimelineClock::MODE_REALTIME);
        } break;
        case OBJECT_TIMELINE: {
            rebuild_clock_options(timeline_clock_picker, int(object.get("clock_id", 0)));
            const Variant duration = object.get("duration", Variant());
            const bool unlimited = duration.get_type() == Variant::NIL;
            unlimited_check->set_pressed(unlimited);
            duration_spin->set_value(unlimited ? 1 : int(duration));
            duration_spin->set_editable(!unlimited);
            set_property_row_visibility(timeline_clock_picker, true);
            set_property_row_visibility(unlimited_check, true);
            set_property_row_visibility(duration_spin, true);
        } break;
        case OBJECT_TRACK:
            set_property_row_visibility(enabled_check, true);
            break;
        case OBJECT_CLIP:
            remove_selected_button->set_text(utf8("删除片段"));
            remove_selected_button->set_visible(true);
            start_spin->set_value(int(object.get("start", 0)));
            duration_spin->set_value(int(object.get("duration", 1)));
            color_picker->set_pick_color(clip_color(object));
            set_property_row_visibility(start_spin, true);
            set_property_row_visibility(duration_spin, true);
            set_property_row_visibility(color_picker, true);
            break;
        case OBJECT_MARK: {
            remove_selected_button->set_text(utf8("删除标记"));
            remove_selected_button->set_visible(true);
            time_spin->set_value(int(object.get("time", 0)));
            const bool blocking = String(object.get("kind", "output")) == "blocking";
            mark_kind_picker->select(blocking ? 1 : 0);
            label_edit->set_text(String(object.get("label", String())));
            set_property_row_visibility(time_spin, true);
            set_property_row_visibility(mark_kind_picker, true);
            set_property_row_visibility(label_edit, true);
        } break;
        case OBJECT_LISTENER:
            label_edit->set_text(String(object.get("label", String())));
            set_property_row_visibility(label_edit, true);
            set_property_row_visibility(enabled_check, true);
            break;
    }
    apply_button->set_visible(true);
}

void NVTimelineEditorWindow::update_window_title() {
    const Dictionary data = document->get_data();
    String title = String(data.get("name", "NV Timeline"));
    if (!current_path.is_empty()) {
        title += utf8(" — ") + current_path.get_file();
    }
    if (dirty) {
        title += " *";
    }
    set_title(utf8("NV Timeline · ") + title);
    document_label->set_text(title);
}

void NVTimelineEditorWindow::set_status(const String &p_message) {
    status_message = p_message;
    const double seconds = static_cast<double>(playhead_units) / 10.0;
    status_label->set_text(status_message + utf8("    播放头：") + String::num_real(seconds, 1) + utf8(" 秒"));
}

void NVTimelineEditorWindow::rebuild_clock_options(OptionButton *p_picker, int p_selected_id) {
    p_picker->clear();
    const Array clocks = document->get_data().get("clocks", Array());
    int selected_index = -1;
    for (int64_t i = 0; i < clocks.size(); ++i) {
        const Dictionary clock = clocks[i];
        const int id = int(clock.get("id", 0));
        p_picker->add_item(display_name(clock, utf8("时钟 ") + String::num_int64(id)), id);
        if (id == p_selected_id) {
            selected_index = static_cast<int>(i);
        }
    }
    if (selected_index >= 0) {
        p_picker->select(selected_index);
    } else if (!clocks.is_empty()) {
        p_picker->select(0);
    }
}

int NVTimelineEditorWindow::find_current_timeline_index(const Dictionary &p_data) const {
    const Array timelines = p_data.get("timelines", Array());
    for (int64_t i = 0; i < timelines.size(); ++i) {
        if (timelines[i].get_type() == Variant::DICTIONARY &&
                int(Dictionary(timelines[i]).get("id", 0)) == current_timeline_id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

NVTimelineEditorWindow::ObjectLocation NVTimelineEditorWindow::find_object(int p_id) const {
    ObjectLocation result;
    if (document.is_null()) {
        return result;
    }
    const Dictionary data = document->get_data();
    if (p_id == 0) {
        result.valid = true;
        result.type = OBJECT_DOCUMENT;
        result.object = data;
        return result;
    }

    const Array clocks = data.get("clocks", Array());
    for (int64_t i = 0; i < clocks.size(); ++i) {
        const Dictionary object = clocks[i];
        if (int(object.get("id", 0)) == p_id) {
            result.valid = true;
            result.type = OBJECT_CLOCK;
            result.clock_index = static_cast<int>(i);
            result.object = object;
            return result;
        }
    }

    const Array timelines = data.get("timelines", Array());
    for (int64_t timeline_index = 0; timeline_index < timelines.size(); ++timeline_index) {
        const Dictionary timeline = timelines[timeline_index];
        if (int(timeline.get("id", 0)) == p_id) {
            result.valid = true;
            result.type = OBJECT_TIMELINE;
            result.timeline_index = static_cast<int>(timeline_index);
            result.object = timeline;
            return result;
        }
        const Array tracks = timeline.get("tracks", Array());
        for (int64_t track_index = 0; track_index < tracks.size(); ++track_index) {
            const Dictionary track = tracks[track_index];
            if (int(track.get("id", 0)) == p_id) {
                result.valid = true;
                result.type = OBJECT_TRACK;
                result.timeline_index = static_cast<int>(timeline_index);
                result.track_index = static_cast<int>(track_index);
                result.object = track;
                return result;
            }
            const Array clips = track.get("clips", Array());
            for (int64_t item_index = 0; item_index < clips.size(); ++item_index) {
                const Dictionary clip = clips[item_index];
                if (int(clip.get("id", 0)) == p_id) {
                    result.valid = true;
                    result.type = OBJECT_CLIP;
                    result.timeline_index = static_cast<int>(timeline_index);
                    result.track_index = static_cast<int>(track_index);
                    result.item_index = static_cast<int>(item_index);
                    result.object = clip;
                    return result;
                }
            }
            const Array marks = track.get("marks", Array());
            for (int64_t item_index = 0; item_index < marks.size(); ++item_index) {
                const Dictionary mark = marks[item_index];
                if (int(mark.get("id", 0)) == p_id) {
                    result.valid = true;
                    result.type = OBJECT_MARK;
                    result.timeline_index = static_cast<int>(timeline_index);
                    result.track_index = static_cast<int>(track_index);
                    result.item_index = static_cast<int>(item_index);
                    result.object = mark;
                    return result;
                }
            }
        }
        const Array listeners = timeline.get("listeners", Array());
        for (int64_t item_index = 0; item_index < listeners.size(); ++item_index) {
            const Dictionary listener = listeners[item_index];
            if (int(listener.get("id", 0)) == p_id) {
                result.valid = true;
                result.type = OBJECT_LISTENER;
                result.timeline_index = static_cast<int>(timeline_index);
                result.item_index = static_cast<int>(item_index);
                result.object = listener;
                return result;
            }
        }
    }
    return result;
}

NVTimelineEditorWindow::ObjectLocation NVTimelineEditorWindow::get_selected_object() const {
    return find_object(selected_object_id);
}

int NVTimelineEditorWindow::find_current_track_index(const Dictionary &p_timeline) const {
    const ObjectLocation location = find_object(selected_object_id);
    if (location.valid && (location.type == OBJECT_TRACK || location.type == OBJECT_CLIP ||
            location.type == OBJECT_MARK) && location.timeline_index == find_current_timeline_index(document->get_data())) {
        return location.track_index;
    }
    const Array tracks = p_timeline.get("tracks", Array());
    return tracks.is_empty() ? -1 : 0;
}

void NVTimelineEditorWindow::select_object(int p_id) {
    selected_object_id = p_id;
    const ObjectLocation location = find_object(p_id);
    if (location.valid && location.type == OBJECT_TIMELINE) {
        current_timeline_id = int(location.object.get("id", 0));
        current_clock_id = int(location.object.get("clock_id", 0));
        playhead_units = 0;
        refresh_all();
        return;
    }
    refresh_track_headers();
    refresh_canvas();
    refresh_inspector();
}

bool NVTimelineEditorWindow::commit_document_data(const Dictionary &p_candidate, const String &p_message) {
    const Dictionary previous = document->get_data().duplicate(true);
    if (previous == p_candidate) {
        set_status(utf8("没有需要提交的属性变更"));
        return true;
    }
    if (!document->set_data(p_candidate.duplicate(true))) {
        set_status(utf8("未应用修改：\n") + join_errors(document->get_validation_errors()));
        return false;
    }
    // 每个成功编辑只记录一次前态；限制历史长度避免大文档反复深拷贝占用内存。
    undo_history.push_back(previous);
    if (undo_history.size() > 100) {
        undo_history.erase(undo_history.begin());
    }
    redo_history.clear();
    dirty = document->get_data() != saved_data;
    refresh_all();
    set_status(p_message);
    return true;
}

bool NVTimelineEditorWindow::replace_object(const ObjectLocation &p_location, const Dictionary &p_object) {
    if (!p_location.valid) {
        return false;
    }
    Dictionary data = document->get_data().duplicate(true);
    if (p_location.type == OBJECT_DOCUMENT) {
        data["name"] = p_object.get("name", String(utf8("新时间轴文档")));
        return commit_document_data(data, utf8("文档属性已修改"));
    }

    if (p_location.type == OBJECT_CLOCK) {
        Array clocks = data.get("clocks", Array());
        clocks.set(p_location.clock_index, p_object);
        data["clocks"] = clocks;
        return commit_document_data(data, utf8("时钟属性已修改"));
    }

    Array timelines = data.get("timelines", Array());
    Dictionary timeline = timelines[p_location.timeline_index];
    if (p_location.type == OBJECT_TIMELINE) {
        timelines.set(p_location.timeline_index, p_object);
        data["timelines"] = timelines;
        return commit_document_data(data, utf8("时间轴属性已修改"));
    }

    if (p_location.type == OBJECT_LISTENER) {
        Array listeners = timeline.get("listeners", Array());
        listeners.set(p_location.item_index, p_object);
        timeline["listeners"] = listeners;
    } else {
        Array tracks = timeline.get("tracks", Array());
        Dictionary track = tracks[p_location.track_index];
        if (p_location.type == OBJECT_TRACK) {
            tracks.set(p_location.track_index, p_object);
        } else {
            const String child_key = p_location.type == OBJECT_CLIP ? "clips" : "marks";
            Array children = track.get(child_key, Array());
            children.set(p_location.item_index, p_object);
            track[child_key] = children;
            tracks.set(p_location.track_index, track);
        }
        timeline["tracks"] = tracks;
    }
    timelines.set(p_location.timeline_index, timeline);
    data["timelines"] = timelines;
    return commit_document_data(data, utf8("对象属性已修改"));
}

bool NVTimelineEditorWindow::add_object_to_array(const String &p_array_key, const Dictionary &p_object) {
    Dictionary data = document->get_data().duplicate(true);
    Array values = data.get(p_array_key, Array());
    values.push_back(p_object);
    data[p_array_key] = values;
    return commit_document_data(data, utf8("已新增对象"));
}

void NVTimelineEditorWindow::mark_dirty_and_refresh(const String &p_message) {
    dirty = true;
    refresh_all();
    set_status(p_message);
}

void NVTimelineEditorWindow::add_default_clock_and_timeline(const String &p_name) {
    undo_history.clear();
    redo_history.clear();
    document->create_empty(p_name);

    // 新文档中的配置 ID 也必须经过文档分配器生成，不能由编辑器固定写入 1、2。
    // 单时间轴约束要求时钟和时间轴在同一次候选数据提交中建立，不能先提交一个
    // 暂时没有时间轴的中间文档。
    const int clock_id = document->allocate_id();
    const int timeline_id = document->allocate_id();
    if (clock_id == 0 || timeline_id == 0) {
        set_status(join_errors(document->get_validation_errors()));
        return;
    }
    Dictionary clock;
    clock["id"] = clock_id;
    clock["name"] = utf8("手动时钟");
    clock["mode"] = "manual";
    Dictionary timeline;
    timeline["id"] = timeline_id;
    timeline["name"] = utf8("主时间轴");
    timeline["clock_id"] = clock_id;
    timeline["tracks"] = Array();
    timeline["listeners"] = Array();
    Dictionary data = document->get_data();
    Array clocks;
    clocks.push_back(clock);
    data["clocks"] = clocks;
    Array timelines;
    timelines.push_back(timeline);
    data["timelines"] = timelines;
    if (!document->set_data(data)) {
        set_status(utf8("创建默认时间轴失败：") + join_errors(document->get_validation_errors()));
        return;
    }

    current_path = "";
    dirty = false;
    current_clock_id = clock_id;
    current_timeline_id = timeline_id;
    selected_object_id = timeline_id;
    playhead_units = 0;
    saved_data = document->get_data().duplicate(true);
    refresh_all();
    set_status(utf8("新文档已创建"));
}

void NVTimelineEditorWindow::create_new_document(const String &p_name) {
    add_default_clock_and_timeline(p_name);
}

bool NVTimelineEditorWindow::open_document(const String &p_path) {
    Ref<NVTimelineDocument> candidate;
    candidate.instantiate();
    if (!candidate->load_file(p_path)) {
        set_status(utf8("无法打开文档：\n") + join_errors(candidate->get_validation_errors()));
        return false;
    }

    document = candidate;
    current_path = p_path;
    saved_data = document->get_data().duplicate(true);
    undo_history.clear();
    redo_history.clear();
    dirty = false;
    playhead_units = 0;
    const Array timelines = document->get_data().get("timelines", Array());
    if (!timelines.is_empty()) {
        const Dictionary timeline = timelines[0];
        current_timeline_id = int(timeline.get("id", 0));
        current_clock_id = int(timeline.get("clock_id", 0));
        selected_object_id = current_timeline_id;
    } else {
        current_timeline_id = 0;
        current_clock_id = 0;
        selected_object_id = 0;
    }
    refresh_all();
    set_status(utf8("已打开并校验时间轴文档"));
    return true;
}

bool NVTimelineEditorWindow::save_document(const String &p_path) {
    if (document.is_null() || !document->is_valid()) {
        set_status(utf8("无法保存：当前文档尚未通过校验"));
        return false;
    }
    if (!document->save_file(p_path, true)) {
        set_status(utf8("保存失败：") + p_path);
        return false;
    }
    current_path = p_path;
    saved_data = document->get_data().duplicate(true);
    dirty = false;
    refresh_all();
    set_status(utf8("已保存：") + p_path);
    return true;
}

Dictionary NVTimelineEditorWindow::get_document_data() const {
    return document.is_valid() ? document->get_data() : Dictionary();
}

void NVTimelineEditorWindow::show_editor() {
    if (is_visible()) {
        grab_focus();
        return;
    }
    popup_centered_clamped(Vector2i(1420, 820), 0.92f);
}

void NVTimelineEditorWindow::request_action(PendingAction p_action) {
    pending_action = p_action;
    if (dirty) {
        dirty_dialog->popup_centered();
        return;
    }
    perform_pending_action();
}

void NVTimelineEditorWindow::perform_pending_action() {
    const PendingAction action = pending_action;
    pending_action = ACTION_NONE;
    if (action == ACTION_NEW_DOCUMENT) {
        create_new_document();
    } else if (action == ACTION_OPEN_DOCUMENT) {
        show_file_dialog(false);
    } else if (action == ACTION_CLOSE_WINDOW) {
        hide();
    }
}

void NVTimelineEditorWindow::show_file_dialog(bool p_saving) {
    file_dialog_saving = p_saving;
    file_dialog->set_file_mode(p_saving ? FileDialog::FILE_MODE_SAVE_FILE : FileDialog::FILE_MODE_OPEN_FILE);
    file_dialog->set_title(p_saving ? utf8("保存时间轴文档") : utf8("打开时间轴文档"));
    file_dialog->set_current_dir("res://");
    if (!current_path.is_empty()) {
        file_dialog->set_current_file(current_path.get_file());
    } else if (p_saving) {
        file_dialog->set_current_file("new_timeline.timeline.json");
    }
    file_dialog->popup_file_dialog();
}

void NVTimelineEditorWindow::_on_new_pressed() {
    request_action(ACTION_NEW_DOCUMENT);
}

void NVTimelineEditorWindow::_on_open_pressed() {
    request_action(ACTION_OPEN_DOCUMENT);
}

void NVTimelineEditorWindow::_on_save_pressed() {
    if (current_path.is_empty()) {
        show_file_dialog(true);
    } else {
        save_document(current_path);
    }
}

void NVTimelineEditorWindow::_on_save_as_pressed() {
    show_file_dialog(true);
}

void NVTimelineEditorWindow::_on_undo_pressed() {
    if (undo_history.empty()) {
        set_status(utf8("没有可撤销的编辑操作"));
        return;
    }
    const Dictionary current = document->get_data().duplicate(true);
    const Dictionary previous = undo_history.back();
    undo_history.pop_back();
    if (!document->set_data(previous)) {
        // 快照来自曾经通过同一文档校验器的数据；如果后续规范变化导致失败，
        // 仍将快照放回历史栈，避免一次失败操作丢失可恢复内容。
        undo_history.push_back(previous);
        set_status(utf8("撤销失败：\n") + join_errors(document->get_validation_errors()));
        return;
    }
    redo_history.push_back(current);
    dirty = document->get_data() != saved_data;
    if (!find_object(selected_object_id).valid) {
        selected_object_id = find_object(current_timeline_id).valid ? current_timeline_id : 0;
    }
    refresh_all();
    set_status(utf8("已撤销"));
}

void NVTimelineEditorWindow::_on_redo_pressed() {
    if (redo_history.empty()) {
        set_status(utf8("没有可重做的编辑操作"));
        return;
    }
    const Dictionary current = document->get_data().duplicate(true);
    const Dictionary next = redo_history.back();
    redo_history.pop_back();
    if (!document->set_data(next)) {
        redo_history.push_back(next);
        set_status(utf8("重做失败：\n") + join_errors(document->get_validation_errors()));
        return;
    }
    undo_history.push_back(current);
    dirty = document->get_data() != saved_data;
    if (!find_object(selected_object_id).valid) {
        selected_object_id = find_object(current_timeline_id).valid ? current_timeline_id : 0;
    }
    refresh_all();
    set_status(utf8("已重做"));
}

void NVTimelineEditorWindow::_on_file_selected(const String &p_path) {
    if (file_dialog_saving) {
        save_document(p_path);
    } else {
        open_document(p_path);
    }
}

void NVTimelineEditorWindow::_on_dirty_confirmed() {
    dirty = false;
    perform_pending_action();
}

void NVTimelineEditorWindow::_on_close_requested() {
    request_action(ACTION_CLOSE_WINDOW);
}

void NVTimelineEditorWindow::_notification(int p_what) {
    // 此窗口不拥有运行时服务；离开树时只由 Godot 回收编辑控件和文件对话框。
    if (p_what == NOTIFICATION_PREDELETE) {
        pending_action = ACTION_NONE;
    }
}

void NVTimelineEditorWindow::_on_clock_selected(int p_index) {
    if (refreshing || p_index < 0 || p_index >= clock_picker->get_item_count()) {
        return;
    }
    current_clock_id = clock_picker->get_item_id(p_index);
    select_object(current_clock_id);
}

void NVTimelineEditorWindow::_on_listener_selected(int p_index) {
    if (refreshing || p_index < 0 || p_index >= listener_list->get_item_count()) {
        return;
    }
    select_object(int(listener_list->get_item_metadata(p_index)));
}

void NVTimelineEditorWindow::_on_add_clock_pressed() {
    const int id = document->allocate_id();
    if (id == 0) {
        set_status(join_errors(document->get_validation_errors()));
        return;
    }
    Dictionary clock;
    clock["id"] = id;
    clock["name"] = utf8("新时钟");
    clock["mode"] = "manual";
    if (add_object_to_array("clocks", clock)) {
        current_clock_id = id;
        select_object(id);
    }
}

void NVTimelineEditorWindow::_on_remove_clock_pressed() {
    const ObjectLocation location = find_object(current_clock_id);
    if (!location.valid || location.type != OBJECT_CLOCK) {
        set_status(utf8("请先选择要删除的时钟"));
        return;
    }
    const Array timelines = document->get_data().get("timelines", Array());
    for (int64_t i = 0; i < timelines.size(); ++i) {
        if (int(Dictionary(timelines[i]).get("clock_id", 0)) == current_clock_id) {
            set_status(utf8("该时钟仍被时间轴引用，请先为这些时间轴更换时钟"));
            return;
        }
    }
    Dictionary data = document->get_data().duplicate(true);
    Array clocks = data.get("clocks", Array());
    clocks.remove_at(location.clock_index);
    data["clocks"] = clocks;
    if (commit_document_data(data, utf8("时钟已删除"))) {
        current_clock_id = clocks.is_empty() ? 0 : int(Dictionary(clocks[0]).get("id", 0));
        selected_object_id = 0;
        refresh_all();
    }
}

void NVTimelineEditorWindow::_on_add_track_pressed() {
    const int timeline_index = find_current_timeline_index(document->get_data());
    if (timeline_index < 0) {
        set_status(utf8("请先创建或选择一条时间轴"));
        return;
    }
    const int id = document->allocate_id();
    if (id == 0) {
        set_status(join_errors(document->get_validation_errors()));
        return;
    }
    Dictionary track;
    track["id"] = id;
    track["name"] = utf8("新轨道");
    track["enabled"] = true;
    track["clips"] = Array();
    track["marks"] = Array();

    Dictionary data = document->get_data().duplicate(true);
    Array timelines = data.get("timelines", Array());
    Dictionary timeline = timelines[timeline_index];
    Array tracks = timeline.get("tracks", Array());
    tracks.push_back(track);
    timeline["tracks"] = tracks;
    timelines.set(timeline_index, timeline);
    data["timelines"] = timelines;
    if (commit_document_data(data, utf8("轨道已新增"))) {
        selected_object_id = id;
        refresh_all();
    }
}

void NVTimelineEditorWindow::_on_remove_track_pressed() {
    const ObjectLocation location = find_object(selected_object_id);
    if (!location.valid || location.type != OBJECT_TRACK || location.timeline_index !=
            find_current_timeline_index(document->get_data())) {
        set_status(utf8("请先在轨道列表中选择要删除的轨道"));
        return;
    }
    Dictionary data = document->get_data().duplicate(true);
    Array timelines = data.get("timelines", Array());
    Dictionary timeline = timelines[location.timeline_index];
    Array tracks = timeline.get("tracks", Array());
    tracks.remove_at(location.track_index);
    timeline["tracks"] = tracks;
    timelines.set(location.timeline_index, timeline);
    data["timelines"] = timelines;
    if (commit_document_data(data, utf8("轨道及其视觉对象已删除"))) {
        selected_object_id = current_timeline_id;
        refresh_all();
    }
}

void NVTimelineEditorWindow::_on_move_track_up_pressed() {
    const ObjectLocation location = find_object(selected_object_id);
    if (!location.valid || location.type != OBJECT_TRACK || location.track_index <= 0 ||
            location.timeline_index != find_current_timeline_index(document->get_data())) {
        set_status(utf8("所选轨道已经位于顶部"));
        return;
    }
    Dictionary data = document->get_data().duplicate(true);
    Array timelines = data.get("timelines", Array());
    Dictionary timeline = timelines[location.timeline_index];
    Array tracks = timeline.get("tracks", Array());
    const Variant moving = tracks[location.track_index];
    tracks.remove_at(location.track_index);
    tracks.insert(location.track_index - 1, moving);
    timeline["tracks"] = tracks;
    timelines.set(location.timeline_index, timeline);
    data["timelines"] = timelines;
    commit_document_data(data, utf8("轨道顺序已更新"));
}

void NVTimelineEditorWindow::_on_move_track_down_pressed() {
    const ObjectLocation location = find_object(selected_object_id);
    if (!location.valid || location.type != OBJECT_TRACK || location.timeline_index !=
            find_current_timeline_index(document->get_data())) {
        set_status(utf8("请先选择一条轨道"));
        return;
    }
    Dictionary data = document->get_data().duplicate(true);
    Array timelines = data.get("timelines", Array());
    Dictionary timeline = timelines[location.timeline_index];
    Array tracks = timeline.get("tracks", Array());
    if (location.track_index + 1 >= tracks.size()) {
        set_status(utf8("所选轨道已经位于底部"));
        return;
    }
    const Variant moving = tracks[location.track_index];
    tracks.remove_at(location.track_index);
    tracks.insert(location.track_index + 1, moving);
    timeline["tracks"] = tracks;
    timelines.set(location.timeline_index, timeline);
    data["timelines"] = timelines;
    commit_document_data(data, utf8("轨道顺序已更新"));
}

void NVTimelineEditorWindow::_on_add_clip_pressed() {
    int timeline_index = find_current_timeline_index(document->get_data());
    if (timeline_index < 0) {
        set_status(utf8("请先创建或选择一条时间轴"));
        return;
    }
    Array timelines = document->get_data().get("timelines", Array());
    Dictionary timeline = timelines[timeline_index];
    Array tracks = timeline.get("tracks", Array());
    if (tracks.is_empty()) {
        _on_add_track_pressed();
        timeline_index = find_current_timeline_index(document->get_data());
        timelines = document->get_data().get("timelines", Array());
        timeline = timelines[timeline_index];
        tracks = timeline.get("tracks", Array());
    }
    const int track_index = find_current_track_index(timeline);
    if (track_index < 0 || track_index >= tracks.size()) {
        set_status(utf8("请先新增一条轨道"));
        return;
    }
    Dictionary data = document->get_data().duplicate(true);
    timelines = data.get("timelines", Array());
    timeline = timelines[timeline_index];
    tracks = timeline.get("tracks", Array());
    Dictionary track = tracks[track_index];
    Array clips = track.get("clips", Array());

    const int timeline_duration = timeline.has("duration") &&
            timeline["duration"].get_type() == Variant::INT ? int(timeline["duration"]) : -1;
    int clip_start = 0;
    int clip_duration = 0;
    if (!find_free_clip_slot(clips, playhead_units, 50, timeline_duration, clip_start, clip_duration)) {
        set_status(utf8("当前轨道从播放头起没有可用的空闲区间"));
        return;
    }

    const int id = document->allocate_id();
    if (id == 0) {
        set_status(join_errors(document->get_validation_errors()));
        return;
    }
    // allocate_id 会推进文档内部的 id_counter；重新复制一次最新数据，避免提交
    // 候选对象时使用分配 ID 之前的旧计数器而被文档校验器拒绝。
    data = document->get_data().duplicate(true);
    timelines = data.get("timelines", Array());
    timeline = timelines[timeline_index];
    tracks = timeline.get("tracks", Array());
    track = tracks[track_index];
    clips = track.get("clips", Array());
    Dictionary clip;
    clip["id"] = id;
    clip["name"] = utf8("新视觉片段");
    clip["start"] = clip_start;
    clip["duration"] = clip_duration;
    clip["color"] = "#568AC8";
    clips.push_back(clip);
    std::vector<Dictionary> sorted_clips;
    for (int64_t i = 0; i < clips.size(); ++i) {
        sorted_clips.push_back(Dictionary(clips[i]));
    }
    std::stable_sort(sorted_clips.begin(), sorted_clips.end(), [](const Dictionary &a, const Dictionary &b) {
        return int(a.get("start", 0)) < int(b.get("start", 0));
    });
    clips.clear();
    for (const Dictionary &item : sorted_clips) {
        clips.push_back(item);
    }
    track["clips"] = clips;
    tracks.set(track_index, track);
    timeline["tracks"] = tracks;
    timelines.set(timeline_index, timeline);
    data["timelines"] = timelines;
    const String add_message = clip_start == std::max(0, playhead_units) ?
            utf8("视觉片段已新增；片段边界不会触发业务回调") :
            utf8("视觉片段已新增，并放置到轨道的下一个空闲区间");
    if (commit_document_data(data, add_message)) {
        selected_object_id = id;
        refresh_all();
    }
}

void NVTimelineEditorWindow::_on_add_output_pressed() {
    auto add_mark = [this](const String &p_kind, const String &p_label, const String &p_name) {
        int timeline_index = find_current_timeline_index(document->get_data());
        if (timeline_index < 0) {
            set_status(utf8("请先创建或选择一条时间轴"));
            return;
        }
        Array timelines = document->get_data().get("timelines", Array());
        Dictionary timeline = timelines[timeline_index];
        Array tracks = timeline.get("tracks", Array());
        if (tracks.is_empty()) {
            _on_add_track_pressed();
            timeline_index = find_current_timeline_index(document->get_data());
            timelines = document->get_data().get("timelines", Array());
            timeline = timelines[timeline_index];
            tracks = timeline.get("tracks", Array());
        }
        const int track_index = find_current_track_index(timeline);
        if (track_index < 0 || track_index >= tracks.size()) {
            set_status(utf8("请先新增一条轨道"));
            return;
        }
        const int id = document->allocate_id();
        if (id == 0) {
            set_status(join_errors(document->get_validation_errors()));
            return;
        }
        Dictionary mark;
        mark["id"] = id;
        mark["name"] = p_name;
        mark["time"] = playhead_units;
        mark["kind"] = p_kind;
        mark["label"] = p_label;

        Dictionary data = document->get_data().duplicate(true);
        timelines = data.get("timelines", Array());
        timeline = timelines[timeline_index];
        tracks = timeline.get("tracks", Array());
        Dictionary track = tracks[track_index];
        Array marks = track.get("marks", Array());
        marks.push_back(mark);
        std::vector<Dictionary> sorted_marks;
        for (int64_t i = 0; i < marks.size(); ++i) {
            sorted_marks.push_back(Dictionary(marks[i]));
        }
        std::stable_sort(sorted_marks.begin(), sorted_marks.end(), [](const Dictionary &a, const Dictionary &b) {
            return int(a.get("time", 0)) < int(b.get("time", 0));
        });
        marks.clear();
        for (const Dictionary &item : sorted_marks) {
            marks.push_back(item);
        }
        track["marks"] = marks;
        tracks.set(track_index, track);
        timeline["tracks"] = tracks;
        timelines.set(timeline_index, timeline);
        data["timelines"] = timelines;
        if (commit_document_data(data, p_kind == "blocking" ? utf8("阻塞输入标记已新增") : utf8("输出标签标记已新增"))) {
            selected_object_id = id;
            refresh_all();
        }
    };
    add_mark("output", "new_output_label", utf8("新输出标签"));
}

void NVTimelineEditorWindow::_on_add_blocking_pressed() {
    // 与输出标记共享相同的数据结构编辑流程，二者仅由 JSON 中的 kind 区分。
    const int timeline_index = find_current_timeline_index(document->get_data());
    if (timeline_index < 0) {
        set_status(utf8("请先创建或选择一条时间轴"));
        return;
    }
    Array timelines = document->get_data().get("timelines", Array());
    Dictionary timeline = timelines[timeline_index];
    Array tracks = timeline.get("tracks", Array());
    if (tracks.is_empty()) {
        _on_add_track_pressed();
        timelines = document->get_data().get("timelines", Array());
        timeline = timelines[find_current_timeline_index(document->get_data())];
        tracks = timeline.get("tracks", Array());
    }
    const int track_index = find_current_track_index(timeline);
    if (track_index < 0 || track_index >= tracks.size()) {
        set_status(utf8("请先新增一条轨道"));
        return;
    }
    const int id = document->allocate_id();
    if (id == 0) {
        set_status(join_errors(document->get_validation_errors()));
        return;
    }
    Dictionary mark;
    mark["id"] = id;
    mark["name"] = utf8("新的阻塞输入");
    mark["time"] = playhead_units;
    mark["kind"] = "blocking";
    mark["label"] = "new_blocking_label";
    Dictionary data = document->get_data().duplicate(true);
    timelines = data.get("timelines", Array());
    timeline = timelines[find_current_timeline_index(data)];
    tracks = timeline.get("tracks", Array());
    Dictionary track = tracks[track_index];
    Array marks = track.get("marks", Array());
    marks.push_back(mark);
    std::vector<Dictionary> sorted_marks;
    for (int64_t i = 0; i < marks.size(); ++i) {
        sorted_marks.push_back(Dictionary(marks[i]));
    }
    std::stable_sort(sorted_marks.begin(), sorted_marks.end(), [](const Dictionary &a, const Dictionary &b) {
        return int(a.get("time", 0)) < int(b.get("time", 0));
    });
    marks.clear();
    for (const Dictionary &item : sorted_marks) {
        marks.push_back(item);
    }
    track["marks"] = marks;
    tracks.set(track_index, track);
    timeline["tracks"] = tracks;
    timelines.set(find_current_timeline_index(data), timeline);
    data["timelines"] = timelines;
    if (commit_document_data(data, utf8("阻塞输入标记已新增"))) {
        selected_object_id = id;
        refresh_all();
    }
}

void NVTimelineEditorWindow::_on_add_listener_pressed() {
    const int timeline_index = find_current_timeline_index(document->get_data());
    if (timeline_index < 0) {
        set_status(utf8("请先创建或选择一条时间轴"));
        return;
    }
    const int id = document->allocate_id();
    if (id == 0) {
        set_status(join_errors(document->get_validation_errors()));
        return;
    }
    Dictionary listener;
    listener["id"] = id;
    listener["name"] = utf8("新全程输入");
    listener["label"] = "new_listener_label";
    listener["enabled"] = true;

    Dictionary data = document->get_data().duplicate(true);
    Array timelines = data.get("timelines", Array());
    Dictionary timeline = timelines[timeline_index];
    Array listeners = timeline.get("listeners", Array());
    listeners.push_back(listener);
    timeline["listeners"] = listeners;
    timelines.set(timeline_index, timeline);
    data["timelines"] = timelines;
    if (commit_document_data(data, utf8("全程输入已新增"))) {
        selected_object_id = id;
        refresh_all();
    }
}

void NVTimelineEditorWindow::_on_remove_listener_pressed() {
    const ObjectLocation location = find_object(selected_object_id);
    if (!location.valid || location.type != OBJECT_LISTENER || location.timeline_index !=
            find_current_timeline_index(document->get_data())) {
        set_status(utf8("请先选择要删除的全程输入"));
        return;
    }
    Dictionary data = document->get_data().duplicate(true);
    Array timelines = data.get("timelines", Array());
    Dictionary timeline = timelines[location.timeline_index];
    Array listeners = timeline.get("listeners", Array());
    listeners.remove_at(location.item_index);
    timeline["listeners"] = listeners;
    timelines.set(location.timeline_index, timeline);
    data["timelines"] = timelines;
    if (commit_document_data(data, utf8("全程输入已删除"))) {
        selected_object_id = current_timeline_id;
        refresh_all();
    }
}

void NVTimelineEditorWindow::_on_remove_selected_visual_object_pressed() {
    // 该命令只删除时间轴画布上的片段或标记；轨道、时钟和时间轴仍通过各自的
    // 结构操作按钮删除，避免误把层级对象当成普通可视元素处理。
    const ObjectLocation location = find_object(selected_object_id);
    if (!location.valid || (location.type != OBJECT_CLIP && location.type != OBJECT_MARK)) {
        set_status(utf8("请先选择一个片段或标签标记"));
        return;
    }

    Dictionary data = document->get_data().duplicate(true);
    Array timelines = data.get("timelines", Array());
    Dictionary timeline = timelines[location.timeline_index];
    Array tracks = timeline.get("tracks", Array());
    Dictionary track = tracks[location.track_index];
    const String key = location.type == OBJECT_CLIP ? "clips" : "marks";
    Array children = track.get(key, Array());
    children.remove_at(location.item_index);
    track[key] = children;
    tracks.set(location.track_index, track);
    timeline["tracks"] = tracks;
    timelines.set(location.timeline_index, timeline);
    data["timelines"] = timelines;

    if (commit_document_data(data, location.type == OBJECT_CLIP ? utf8("视觉片段已删除") : utf8("标签标记已删除"))) {
        selected_object_id = int(track.get("id", 0));
        refresh_all();
    }
}

void NVTimelineEditorWindow::_on_apply_properties_pressed() {
    const ObjectLocation location = get_selected_object();
    if (!location.valid) {
        return;
    }
    Dictionary updated = location.object.duplicate(true);
    updated["name"] = name_edit->get_text().strip_edges();

    switch (location.type) {
        case OBJECT_DOCUMENT:
            break;
        case OBJECT_CLOCK: {
            const int selected_mode = mode_picker->get_selected_id();
            const bool realtime = selected_mode == NVTimelineClock::MODE_REALTIME;
            updated["mode"] = realtime ? "realtime" : "manual";
            if (realtime) {
                updated["rate"] = rate_spin->get_value();
            } else {
                updated.erase("rate");
            }
        } break;
        case OBJECT_TIMELINE: {
            if (timeline_clock_picker->get_selected() < 0) {
                set_status(utf8("时间轴必须绑定一个有效时钟"));
                return;
            }
            updated["clock_id"] = timeline_clock_picker->get_selected_id();
            if (unlimited_check->is_pressed()) {
                updated["duration"] = Variant();
            } else {
                updated["duration"] = static_cast<int>(duration_spin->get_value());
            }
        } break;
        case OBJECT_TRACK:
        case OBJECT_LISTENER:
            updated["enabled"] = enabled_check->is_pressed();
            if (location.type == OBJECT_LISTENER) {
                updated["label"] = label_edit->get_text().strip_edges();
            }
            break;
        case OBJECT_CLIP:
            updated["start"] = static_cast<int>(start_spin->get_value());
            updated["duration"] = static_cast<int>(duration_spin->get_value());
            updated["color"] = color_picker->get_pick_color().to_html(false);
            break;
        case OBJECT_MARK: {
            updated["time"] = static_cast<int>(time_spin->get_value());
            updated["kind"] = mark_kind_picker->get_selected_id() == 1 ? "blocking" : "output";
            updated["label"] = label_edit->get_text().strip_edges();
        } break;
    }

    // 片段和标记改动会同时替换对象并维护推荐的时间升序，然后仅提交一次。
    // stable_sort 保留同一时刻对象在用户编排中的原有顺序。
    if (location.type == OBJECT_CLIP || location.type == OBJECT_MARK) {
        Dictionary data = document->get_data().duplicate(true);
        Array timelines = data.get("timelines", Array());
        Dictionary timeline = timelines[location.timeline_index];
        Array tracks = timeline.get("tracks", Array());
        Dictionary track = tracks[location.track_index];
        const String key = location.type == OBJECT_CLIP ? "clips" : "marks";
        Array children = track.get(key, Array());
        children.set(location.item_index, updated);
        std::vector<Dictionary> sorted;
        for (int64_t i = 0; i < children.size(); ++i) {
            sorted.push_back(Dictionary(children[i]));
        }
        std::stable_sort(sorted.begin(), sorted.end(), [location](const Dictionary &a, const Dictionary &b) {
            const String field = location.type == OBJECT_CLIP ? "start" : "time";
            return int(a.get(field, 0)) < int(b.get(field, 0));
        });
        children.clear();
        for (const Dictionary &item : sorted) {
            children.push_back(item);
        }
        track[key] = children;
        tracks.set(location.track_index, track);
        timeline["tracks"] = tracks;
        timelines.set(location.timeline_index, timeline);
        data["timelines"] = timelines;
        if (!commit_document_data(data, utf8("对象属性已应用"))) {
            // 文档校验失败时恢复输入框，避免面板暂时显示没有写入 JSON 的非法区间。
            refresh_all();
        }
        return;
    }

    if (!replace_object(location, updated)) {
        return;
    }
}

void NVTimelineEditorWindow::_on_zoom_changed(double p_value) {
    if (refreshing) {
        return;
    }
    zoom = static_cast<float>(p_value);
    zoom_label->set_text(utf8("缩放 ") + String::num_real(zoom, 2) + utf8("×"));
    refresh_canvas();
}

void NVTimelineEditorWindow::_on_track_header_pressed(int p_track_id) {
    if (!refreshing) {
        select_object(p_track_id);
    }
}

void NVTimelineEditorWindow::_on_track_header_gui_input(const Ref<InputEvent> &p_event, int p_track_id) {
    if (refreshing || track_context_menu == nullptr || !is_visible()) {
        return;
    }
    const Ref<InputEventMouseButton> mouse_button = p_event;
    if (mouse_button.is_valid() && mouse_button->get_button_index() == MOUSE_BUTTON_RIGHT &&
            mouse_button->is_pressed()) {
        context_track_id = p_track_id;
        // 右键菜单动作复用现有的新增函数；先选中轨道，确保片段和标记会
        // 写入用户实际右击的轨道，而不是沿用此前的选择对象。
        select_object(context_track_id);
        // Window 自身继承 Viewport；这里必须读取插件窗口局部坐标。使用
        // get_viewport() 会退回 Godot 主编辑器视口，导致弹出菜单偏到主窗口。
        const Vector2 mouse_position = get_mouse_position();
        track_context_menu->popup(Rect2i(Vector2i(mouse_position), Vector2i(1, 1)));
        get_viewport()->set_input_as_handled();
    }
}

void NVTimelineEditorWindow::_on_track_context_id_pressed(int p_menu_id) {
    if (context_track_id == 0) {
        return;
    }
    // 菜单可能在弹出后才收到选择信号，再次选中轨道可以抵御窗口刷新或
    // 外部脚本改变选中对象的情况。
    select_object(context_track_id);
    switch (p_menu_id) {
        case 0:
            _on_add_clip_pressed();
            break;
        case 1:
            _on_add_output_pressed();
            break;
        case 2:
            _on_add_blocking_pressed();
            break;
        default:
            break;
    }
}

void NVTimelineEditorWindow::_on_canvas_track_context_requested(int p_track_id) {
    if (refreshing || track_context_menu == nullptr || !is_visible() || p_track_id == 0) {
        return;
    }
    context_track_id = p_track_id;
    select_object(context_track_id);
    // 画布右键入口与表头入口使用同一套插件窗口局部坐标，保证菜单不会
    // 因为画布位于滚动容器中而被当成主编辑器坐标。
    const Vector2 mouse_position = get_mouse_position();
    track_context_menu->popup(Rect2i(Vector2i(mouse_position), Vector2i(1, 1)));
    get_viewport()->set_input_as_handled();
}

void NVTimelineEditorWindow::_on_track_enabled_toggled(bool p_enabled, int p_track_id) {
    if (refreshing) {
        return;
    }
    const ObjectLocation location = find_object(p_track_id);
    if (!location.valid || location.type != OBJECT_TRACK) {
        return;
    }
    Dictionary track = location.object.duplicate(true);
    track["enabled"] = p_enabled;
    replace_object(location, track);
}

void NVTimelineEditorWindow::_on_canvas_object_selected(int p_object_id) {
    select_object(p_object_id);
}

void NVTimelineEditorWindow::_on_canvas_object_moved(int p_object_id, int p_start, int p_duration) {
    const ObjectLocation location = find_object(p_object_id);
    if (!location.valid || (location.type != OBJECT_CLIP && location.type != OBJECT_MARK)) {
        return;
    }
    Dictionary data = document->get_data().duplicate(true);
    Array timelines = data.get("timelines", Array());
    Dictionary timeline = timelines[location.timeline_index];
    Array tracks = timeline.get("tracks", Array());
    Dictionary track = tracks[location.track_index];
    const String key = location.type == OBJECT_CLIP ? "clips" : "marks";
    Array children = track.get(key, Array());
    Dictionary moved = children[location.item_index];
    if (location.type == OBJECT_CLIP) {
        moved["start"] = p_start;
        moved["duration"] = p_duration;
    } else {
        moved["time"] = p_start;
    }
    children.set(location.item_index, moved);

    // 轨道数组只排序显示对象；片段之间的重叠由文档校验器拒绝，片段与标签
    // 标记仍可位于同一时间位置，因为二者没有业务绑定关系。
    std::vector<Dictionary> sorted;
    for (int64_t i = 0; i < children.size(); ++i) {
        sorted.push_back(Dictionary(children[i]));
    }
    std::stable_sort(sorted.begin(), sorted.end(), [location](const Dictionary &a, const Dictionary &b) {
        const String field = location.type == OBJECT_CLIP ? "start" : "time";
        return int(a.get(field, 0)) < int(b.get(field, 0));
    });
    children.clear();
    for (const Dictionary &item : sorted) {
        children.push_back(item);
    }
    track[key] = children;
    tracks.set(location.track_index, track);
    timeline["tracks"] = tracks;
    timelines.set(location.timeline_index, timeline);
    data["timelines"] = timelines;
    selected_object_id = p_object_id;
    if (!commit_document_data(data, location.type == OBJECT_CLIP ? utf8("片段位置已更新") : utf8("标签标记位置已更新"))) {
        // 拖动到同轨道已有片段的区间时，保留原对象位置并同步恢复画布选择状态。
        refresh_all();
    }
}

void NVTimelineEditorWindow::_on_canvas_playhead_changed(int p_units) {
    playhead_units = std::max(0, p_units);
    const ObjectLocation location = find_object(current_timeline_id);
    if (location.valid && location.object.get("duration", Variant()).get_type() == Variant::INT) {
        playhead_units = std::min(playhead_units, int(location.object["duration"]));
    }
    refresh_canvas();
    set_status(utf8("时间位置已定位"));
}

} // namespace godot
