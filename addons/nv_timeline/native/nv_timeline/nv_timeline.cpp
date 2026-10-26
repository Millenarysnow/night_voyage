#include "nv_timeline.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace godot {

// Godot 的 String 同时提供 String 和 StringName 的 operator+，直接把 UTF-8
// 字面量参与拼接会触发二义性。这个精确匹配的重载统一把中文诊断字面量转为
// String，调用点保持可读，同时避免在每一处手写 String::utf8。
String operator+(const String &p_left, const char *p_right) {
    return p_left.operator+(String::utf8(p_right));
}

namespace {

// 所有 JSON 字段检查都通过这个小工具完成，避免某个对象漏掉字段白名单。
bool has_only_keys(const Dictionary &p_object, const std::vector<String> &p_allowed,
        const String &p_path, PackedStringArray &r_errors) {
    Array keys = p_object.keys();
    for (int64_t i = 0; i < keys.size(); ++i) {
        const String key = keys[i];
        bool known = false;
        for (const String &allowed : p_allowed) {
            if (key == allowed) {
                known = true;
                break;
            }
        }
        if (!known) {
            r_errors.push_back(p_path + "." + key + "：不支持的字段");
        }
    }
    return r_errors.is_empty();
}

bool require_field(const Dictionary &p_object, const String &p_key, Variant::Type p_type,
        const String &p_path, PackedStringArray &r_errors, bool p_allow_null = false) {
    if (!p_object.has(p_key)) {
        r_errors.push_back(p_path + "：缺少字段 " + p_key);
        return false;
    }
    const Variant value = p_object[p_key];
    if (p_allow_null && value.get_type() == Variant::NIL) {
        return true;
    }
    if (value.get_type() != p_type) {
        r_errors.push_back(p_path + "." + p_key + "：类型不正确");
        return false;
    }
    return true;
}

bool optional_type(const Dictionary &p_object, const String &p_key, Variant::Type p_type,
        const String &p_path, PackedStringArray &r_errors, bool p_allow_null = false) {
    if (!p_object.has(p_key)) {
        return true;
    }
    const Variant value = p_object[p_key];
    if (p_allow_null && value.get_type() == Variant::NIL) {
        return true;
    }
    if (value.get_type() != p_type) {
        r_errors.push_back(p_path + "." + p_key + "：类型不正确");
        return false;
    }
    return true;
}

bool is_non_negative_int(const Variant &p_value) {
    return p_value.get_type() == Variant::INT && int64_t(p_value) >= 0 &&
            int64_t(p_value) <= std::numeric_limits<int32_t>::max();
}

bool is_positive_int(const Variant &p_value) {
    return is_non_negative_int(p_value) && int64_t(p_value) > 0;
}

bool is_hex_color(const String &p_value) {
    if (p_value.length() != 7 || !p_value.begins_with("#")) {
        return false;
    }
    for (int i = 1; i < 7; ++i) {
        const char32_t c = p_value[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

void add_id_if_unique(int p_id, const String &p_path, std::vector<int> &r_ids,
        PackedStringArray &r_errors) {
    if (!is_positive_int(p_id)) {
        r_errors.push_back(p_path + ".id：必须是正的 32 位整数");
        return;
    }
    if (std::find(r_ids.begin(), r_ids.end(), p_id) != r_ids.end()) {
        r_errors.push_back(p_path + ".id：与文档中已有对象重复");
        return;
    }
    r_ids.push_back(p_id);
}

String string_or_default(const Dictionary &p_object, const String &p_key, const String &p_default) {
    if (!p_object.has(p_key) || p_object[p_key].get_type() != Variant::STRING) {
        return p_default;
    }
    return String(p_object[p_key]);
}

// Godot 4.8 的 JSON 解析结果在部分构建中会把没有小数点的数字也表示为
// FLOAT。编排协议仍然要求 id、time、duration 使用整数，因此只对协议明确
// 的整数键做“整值且在 int32 范围内”的安全归一化；rate 等真正的 number 字段
// 保持 FLOAT，避免把 JSON 语义偷偷改变。
void normalize_integer_field(Dictionary &p_object, const String &p_key) {
    if (!p_object.has(p_key) || p_object[p_key].get_type() != Variant::FLOAT) {
        return;
    }
    const double value = double(p_object[p_key]);
    if (std::isfinite(value) && std::floor(value) == value &&
            value >= 0.0 && value <= static_cast<double>(std::numeric_limits<int32_t>::max())) {
        p_object[p_key] = static_cast<int64_t>(value);
    }
}

void normalize_document_integer_fields(Dictionary &p_document) {
    normalize_integer_field(p_document, "id_counter");
    Array clocks = p_document.get("clocks", Array());
    for (int64_t i = 0; i < clocks.size(); ++i) {
        if (clocks[i].get_type() == Variant::DICTIONARY) {
            Dictionary clock = clocks[i];
            normalize_integer_field(clock, "id");
            clocks[i] = clock;
        }
    }
    Array timelines = p_document.get("timelines", Array());
    for (int64_t i = 0; i < timelines.size(); ++i) {
        if (timelines[i].get_type() != Variant::DICTIONARY) {
            continue;
        }
        Dictionary timeline = timelines[i];
        normalize_integer_field(timeline, "id");
        normalize_integer_field(timeline, "clock_id");
        normalize_integer_field(timeline, "duration");
        Array tracks = timeline.get("tracks", Array());
        for (int64_t t = 0; t < tracks.size(); ++t) {
            if (tracks[t].get_type() != Variant::DICTIONARY) {
                continue;
            }
            Dictionary track = tracks[t];
            normalize_integer_field(track, "id");
            Array clips = track.get("clips", Array());
            for (int64_t c = 0; c < clips.size(); ++c) {
                if (clips[c].get_type() != Variant::DICTIONARY) {
                    continue;
                }
                Dictionary clip = clips[c];
                normalize_integer_field(clip, "id");
                normalize_integer_field(clip, "start");
                normalize_integer_field(clip, "duration");
                clips[c] = clip;
            }
            Array marks = track.get("marks", Array());
            for (int64_t m = 0; m < marks.size(); ++m) {
                if (marks[m].get_type() != Variant::DICTIONARY) {
                    continue;
                }
                Dictionary mark = marks[m];
                normalize_integer_field(mark, "id");
                normalize_integer_field(mark, "time");
                marks[m] = mark;
            }
            tracks[t] = track;
        }
        Array listeners = timeline.get("listeners", Array());
        for (int64_t l = 0; l < listeners.size(); ++l) {
            if (listeners[l].get_type() == Variant::DICTIONARY) {
                Dictionary listener = listeners[l];
                normalize_integer_field(listener, "id");
                listeners[l] = listener;
            }
        }
        timeline["tracks"] = tracks;
        timeline["listeners"] = listeners;
        timelines[i] = timeline;
    }
    p_document["clocks"] = clocks;
    p_document["timelines"] = timelines;
}

} // namespace

// ------------------------------ NVTimelineDocument ------------------------------

NVTimelineDocument::NVTimelineDocument() {
    create_empty();
}

void NVTimelineDocument::_bind_methods() {
    ClassDB::bind_method(D_METHOD("create_empty", "name"), &NVTimelineDocument::create_empty,
            DEFVAL("新时间轴文档"));
    ClassDB::bind_method(D_METHOD("load_text", "text"), &NVTimelineDocument::load_text);
    ClassDB::bind_method(D_METHOD("load_file", "path"), &NVTimelineDocument::load_file);
    ClassDB::bind_method(D_METHOD("save_file", "path", "pretty"), &NVTimelineDocument::save_file,
            DEFVAL(true));
    ClassDB::bind_method(D_METHOD("set_data", "data"), &NVTimelineDocument::set_data);
    ClassDB::bind_method(D_METHOD("get_data"), &NVTimelineDocument::get_data);
    ClassDB::bind_method(D_METHOD("get_validation_errors"), &NVTimelineDocument::get_validation_errors);
    ClassDB::bind_method(D_METHOD("is_valid"), &NVTimelineDocument::is_valid);
    ClassDB::bind_method(D_METHOD("allocate_id"), &NVTimelineDocument::allocate_id);
}

void NVTimelineDocument::create_empty(const String &p_name) {
    Dictionary new_data;
    new_data["name"] = p_name;
    new_data["id_counter"] = 0;
    new_data["clocks"] = Array();
    new_data["timelines"] = Array();
    data = new_data;
    validation_errors.clear();
}

bool NVTimelineDocument::load_text(const String &p_text) {
    Ref<JSON> parser;
    parser.instantiate();
    const Error parse_error = parser->parse(p_text, false);
    if (parse_error != OK) {
        validation_errors.clear();
        validation_errors.push_back("JSON 语法错误（第 " + String::num_int64(parser->get_error_line()) +
                " 行）：" + parser->get_error_message());
        return false;
    }
    const Variant parsed = parser->get_data();
    if (parsed.get_type() != Variant::DICTIONARY) {
        validation_errors.clear();
        validation_errors.push_back("JSON 根节点必须是对象");
        return false;
    }
    return set_data(Dictionary(parsed));
}

bool NVTimelineDocument::load_file(const String &p_path) {
    if (!FileAccess::file_exists(p_path)) {
        validation_errors.clear();
        validation_errors.push_back("找不到时间轴文件：" + p_path);
        return false;
    }
    return load_text(FileAccess::get_file_as_string(p_path));
}

bool NVTimelineDocument::save_file(const String &p_path, bool p_pretty) const {
    if (!is_valid()) {
        return false;
    }
    Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
    if (file.is_null()) {
        return false;
    }
    const String indent = p_pretty ? "  " : "";
    file->store_string(JSON::stringify(data, indent, true, false) + "\n");
    return true;
}

bool NVTimelineDocument::set_data(const Dictionary &p_data) {
    Dictionary normalized = p_data.duplicate(true);
    normalize_document_integer_fields(normalized);
    if (!validate_internal(normalized)) {
        return false;
    }
    data = normalized;
    validation_errors.clear();
    return true;
}

int NVTimelineDocument::allocate_id() {
    if (!is_valid() || !data.has("id_counter") || data["id_counter"].get_type() != Variant::INT) {
        validation_errors.clear();
        validation_errors.push_back("无法分配 ID：当前文档无效");
        return 0;
    }
    const int64_t current = int64_t(data["id_counter"]);
    // 设计要求按有符号 int 溢出结果拒绝分配；显式判断最大值，避免 C++ 有符号
    // 溢出属于未定义行为，同时保留与 C# unchecked 递增相同的拒绝语义。
    if (current < 0 || current >= std::numeric_limits<int32_t>::max()) {
        validation_errors.clear();
        validation_errors.push_back("无法分配 ID：id_counter 已达到 32 位整数上限");
        return 0;
    }
    const int next = static_cast<int>(current + 1);
    data["id_counter"] = next;
    return next;
}

bool NVTimelineDocument::validate_internal(const Dictionary &p_data) {
    validation_errors.clear();
    std::vector<int> ids;
    has_only_keys(p_data, {"name", "id_counter", "clocks", "timelines"}, "root", validation_errors);
    require_field(p_data, "id_counter", Variant::INT, "root", validation_errors);
    require_field(p_data, "clocks", Variant::ARRAY, "root", validation_errors);
    require_field(p_data, "timelines", Variant::ARRAY, "root", validation_errors);
    optional_type(p_data, "name", Variant::STRING, "root", validation_errors);
    if (p_data.has("id_counter") && p_data["id_counter"].get_type() == Variant::INT &&
            (int64_t(p_data["id_counter"]) < 0 || int64_t(p_data["id_counter"]) > std::numeric_limits<int32_t>::max())) {
        validation_errors.push_back("root.id_counter：必须是非负的 32 位整数");
    }

    std::vector<int> clock_ids;
    if (p_data.has("clocks") && p_data["clocks"].get_type() == Variant::ARRAY) {
        const Array clocks_array = p_data["clocks"];
        for (int64_t i = 0; i < clocks_array.size(); ++i) {
            const String path = "root.clocks[" + String::num_int64(i) + "]";
            if (clocks_array[i].get_type() != Variant::DICTIONARY) {
                validation_errors.push_back(path + "：必须是对象");
                continue;
            }
            const Dictionary clock = clocks_array[i];
            has_only_keys(clock, {"id", "name", "mode", "rate"}, path, validation_errors);
            require_field(clock, "id", Variant::INT, path, validation_errors);
            require_field(clock, "mode", Variant::STRING, path, validation_errors);
            optional_type(clock, "name", Variant::STRING, path, validation_errors);
            if (clock.has("rate") && clock["rate"].get_type() != Variant::FLOAT &&
                    clock["rate"].get_type() != Variant::INT) {
                validation_errors.push_back(path + ".rate：必须是 number 类型");
            }
            if (clock.has("id") && clock["id"].get_type() == Variant::INT) {
                const int clock_id = int(clock["id"]);
                add_id_if_unique(clock_id, path, ids, validation_errors);
                if (std::find(clock_ids.begin(), clock_ids.end(), clock_id) != clock_ids.end()) {
                    validation_errors.push_back(path + ".id：时钟 ID 重复");
                } else if (is_positive_int(clock_id)) {
                    clock_ids.push_back(clock_id);
                }
            }
            if (clock.has("mode") && clock["mode"].get_type() == Variant::STRING) {
                const String mode = clock["mode"];
                if (mode != "manual" && mode != "realtime") {
                    validation_errors.push_back(path + ".mode：只能是 manual 或 realtime");
                }
                if (mode == "realtime" && (!clock.has("rate") ||
                        (clock["rate"].get_type() != Variant::FLOAT && clock["rate"].get_type() != Variant::INT) ||
                        double(clock["rate"]) <= 0.0)) {
                    validation_errors.push_back(path + ".rate：realtime 时必须是大于 0 的数");
                }
            }
        }
    }

    if (p_data.has("timelines") && p_data["timelines"].get_type() == Variant::ARRAY) {
        const Array timelines_array = p_data["timelines"];
        for (int64_t i = 0; i < timelines_array.size(); ++i) {
            const String path = "root.timelines[" + String::num_int64(i) + "]";
            if (timelines_array[i].get_type() != Variant::DICTIONARY) {
                validation_errors.push_back(path + "：必须是对象");
                continue;
            }
            const Dictionary timeline = timelines_array[i];
            has_only_keys(timeline, {"id", "name", "clock_id", "duration", "tracks", "listeners"}, path, validation_errors);
            require_field(timeline, "id", Variant::INT, path, validation_errors);
            require_field(timeline, "clock_id", Variant::INT, path, validation_errors);
            require_field(timeline, "tracks", Variant::ARRAY, path, validation_errors);
            require_field(timeline, "listeners", Variant::ARRAY, path, validation_errors);
            optional_type(timeline, "name", Variant::STRING, path, validation_errors);
            optional_type(timeline, "duration", Variant::INT, path, validation_errors, true);
            if (timeline.has("id") && timeline["id"].get_type() == Variant::INT) {
                add_id_if_unique(int(timeline["id"]), path, ids, validation_errors);
            }
            if (timeline.has("clock_id") && timeline["clock_id"].get_type() == Variant::INT &&
                    std::find(clock_ids.begin(), clock_ids.end(), int(timeline["clock_id"])) == clock_ids.end()) {
                validation_errors.push_back(path + ".clock_id：没有引用到已定义的时钟");
            }
            int timeline_duration = -1;
            if (timeline.has("duration") && timeline["duration"].get_type() == Variant::INT) {
                timeline_duration = int(timeline["duration"]);
                if (!is_positive_int(timeline_duration)) {
                    validation_errors.push_back(path + ".duration：有限时长必须是正整数");
                }
            }

            if (timeline.has("tracks") && timeline["tracks"].get_type() == Variant::ARRAY) {
                const Array tracks = timeline["tracks"];
                for (int64_t t = 0; t < tracks.size(); ++t) {
                    const String track_path = path + ".tracks[" + String::num_int64(t) + "]";
                    if (tracks[t].get_type() != Variant::DICTIONARY) {
                        validation_errors.push_back(track_path + "：必须是对象");
                        continue;
                    }
                    const Dictionary track = tracks[t];
                    has_only_keys(track, {"id", "name", "enabled", "clips", "marks"}, track_path, validation_errors);
                    require_field(track, "id", Variant::INT, track_path, validation_errors);
                    require_field(track, "marks", Variant::ARRAY, track_path, validation_errors);
                    optional_type(track, "name", Variant::STRING, track_path, validation_errors);
                    optional_type(track, "enabled", Variant::BOOL, track_path, validation_errors);
                    optional_type(track, "clips", Variant::ARRAY, track_path, validation_errors);
                    if (track.has("id") && track["id"].get_type() == Variant::INT) {
                        add_id_if_unique(int(track["id"]), track_path, ids, validation_errors);
                    }
                    if (track.has("clips") && track["clips"].get_type() == Variant::ARRAY) {
                        const Array clips = track["clips"];
                        for (int64_t c = 0; c < clips.size(); ++c) {
                            const String clip_path = track_path + ".clips[" + String::num_int64(c) + "]";
                            if (clips[c].get_type() != Variant::DICTIONARY) {
                                validation_errors.push_back(clip_path + "：必须是对象");
                                continue;
                            }
                            const Dictionary clip = clips[c];
                            has_only_keys(clip, {"id", "name", "start", "duration", "color"}, clip_path, validation_errors);
                            require_field(clip, "id", Variant::INT, clip_path, validation_errors);
                            require_field(clip, "name", Variant::STRING, clip_path, validation_errors);
                            require_field(clip, "start", Variant::INT, clip_path, validation_errors);
                            require_field(clip, "duration", Variant::INT, clip_path, validation_errors);
                            optional_type(clip, "color", Variant::STRING, clip_path, validation_errors);
                            if (clip.has("id") && clip["id"].get_type() == Variant::INT) {
                                add_id_if_unique(int(clip["id"]), clip_path, ids, validation_errors);
                            }
                            if (clip.has("name") && clip["name"].get_type() == Variant::STRING &&
                                    String(clip["name"]).strip_edges().is_empty()) {
                                validation_errors.push_back(clip_path + ".name：不能为空");
                            }
                            if (clip.has("start") && clip.has("duration") &&
                                    clip["start"].get_type() == Variant::INT && clip["duration"].get_type() == Variant::INT) {
                                const int64_t start = int64_t(clip["start"]);
                                const int64_t clip_duration = int64_t(clip["duration"]);
                                if (start < 0 || clip_duration <= 0) {
                                    validation_errors.push_back(clip_path + "：start 必须非负，duration 必须为正");
                                }
                                if (timeline_duration > 0 && start + clip_duration > timeline_duration) {
                                    validation_errors.push_back(clip_path + "：片段末端超过时间轴 duration");
                                }
                            }
                            if (clip.has("color") && clip["color"].get_type() == Variant::STRING &&
                                    !is_hex_color(String(clip["color"]))) {
                                validation_errors.push_back(clip_path + ".color：必须是 #RRGGBB");
                            }
                        }
                    }
                    if (track.has("marks") && track["marks"].get_type() == Variant::ARRAY) {
                        const Array marks = track["marks"];
                        for (int64_t m = 0; m < marks.size(); ++m) {
                            const String mark_path = track_path + ".marks[" + String::num_int64(m) + "]";
                            if (marks[m].get_type() != Variant::DICTIONARY) {
                                validation_errors.push_back(mark_path + "：必须是对象");
                                continue;
                            }
                            const Dictionary mark = marks[m];
                            has_only_keys(mark, {"id", "name", "time", "kind", "label"}, mark_path, validation_errors);
                            require_field(mark, "id", Variant::INT, mark_path, validation_errors);
                            require_field(mark, "time", Variant::INT, mark_path, validation_errors);
                            require_field(mark, "kind", Variant::STRING, mark_path, validation_errors);
                            require_field(mark, "label", Variant::STRING, mark_path, validation_errors);
                            optional_type(mark, "name", Variant::STRING, mark_path, validation_errors);
                            if (mark.has("id") && mark["id"].get_type() == Variant::INT) {
                                add_id_if_unique(int(mark["id"]), mark_path, ids, validation_errors);
                            }
                            if (mark.has("time") && mark["time"].get_type() == Variant::INT) {
                                const int mark_time = int(mark["time"]);
                                if (!is_non_negative_int(mark_time) || (timeline_duration > 0 && mark_time > timeline_duration)) {
                                    validation_errors.push_back(mark_path + ".time：超出有效时间范围");
                                }
                            }
                            if (mark.has("kind") && mark["kind"].get_type() == Variant::STRING &&
                                    String(mark["kind"]) != "output" && String(mark["kind"]) != "blocking") {
                                validation_errors.push_back(mark_path + ".kind：只能是 output 或 blocking");
                            }
                            if (mark.has("label") && mark["label"].get_type() == Variant::STRING) {
                                const String label = mark["label"];
                                if (label.is_empty() || label != label.strip_edges()) {
                                    validation_errors.push_back(mark_path + ".label：必须是非空且无首尾空白的字符串");
                                }
                            }
                        }
                    }
                }
            }

            if (timeline.has("listeners") && timeline["listeners"].get_type() == Variant::ARRAY) {
                const Array listeners = timeline["listeners"];
                for (int64_t l = 0; l < listeners.size(); ++l) {
                    const String listener_path = path + ".listeners[" + String::num_int64(l) + "]";
                    if (listeners[l].get_type() != Variant::DICTIONARY) {
                        validation_errors.push_back(listener_path + "：必须是对象");
                        continue;
                    }
                    const Dictionary listener = listeners[l];
                    has_only_keys(listener, {"id", "name", "label", "enabled"}, listener_path, validation_errors);
                    require_field(listener, "id", Variant::INT, listener_path, validation_errors);
                    require_field(listener, "label", Variant::STRING, listener_path, validation_errors);
                    optional_type(listener, "name", Variant::STRING, listener_path, validation_errors);
                    optional_type(listener, "enabled", Variant::BOOL, listener_path, validation_errors);
                    if (listener.has("id") && listener["id"].get_type() == Variant::INT) {
                        add_id_if_unique(int(listener["id"]), listener_path, ids, validation_errors);
                    }
                    if (listener.has("label") && listener["label"].get_type() == Variant::STRING) {
                        const String label = listener["label"];
                        if (label.is_empty() || label != label.strip_edges()) {
                            validation_errors.push_back(listener_path + ".label：必须是非空且无首尾空白的字符串");
                        }
                    }
                }
            }
        }
    }

    if (p_data.has("id_counter") && p_data["id_counter"].get_type() == Variant::INT) {
        int max_id = 0;
        for (const int id : ids) {
            max_id = std::max(max_id, id);
        }
        if (int64_t(p_data["id_counter"]) < max_id) {
            validation_errors.push_back("root.id_counter：不能小于文档中已存在的最大 ID");
        }
    }
    return validation_errors.is_empty();
}

// ------------------------------ NVTimelineClock ------------------------------

void NVTimelineClock::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_id"), &NVTimelineClock::get_id);
    ClassDB::bind_method(D_METHOD("get_clock_name"), &NVTimelineClock::get_clock_name);
    ClassDB::bind_method(D_METHOD("get_mode"), &NVTimelineClock::get_mode);
    ClassDB::bind_method(D_METHOD("get_rate"), &NVTimelineClock::get_rate);
    ClassDB::bind_method(D_METHOD("get_position"), &NVTimelineClock::get_position);
    ClassDB::bind_method(D_METHOD("is_paused"), &NVTimelineClock::is_paused);
    ClassDB::bind_method(D_METHOD("set_paused", "paused"), &NVTimelineClock::set_paused);
    ClassDB::bind_method(D_METHOD("reset"), &NVTimelineClock::reset);
    ClassDB::bind_method(D_METHOD("advance_units", "units"), &NVTimelineClock::advance_units);
    ClassDB::bind_method(D_METHOD("advance_seconds", "seconds"), &NVTimelineClock::advance_seconds);
    BIND_ENUM_CONSTANT(MODE_MANUAL);
    BIND_ENUM_CONSTANT(MODE_REALTIME);
}

void NVTimelineClock::configure(int p_id, const String &p_name, ClockMode p_mode, double p_rate) {
    id = p_id;
    name = p_name;
    mode = p_mode;
    rate = p_rate > 0.0 ? p_rate : 1.0;
    reset();
}

void NVTimelineClock::reset() {
    position_units = 0;
    remainder_seconds = 0.0;
    paused = false;
}

int NVTimelineClock::advance_units(int p_units) {
    if (paused || p_units <= 0) {
        return 0;
    }
    position_units += p_units;
    return p_units;
}

int NVTimelineClock::advance_seconds(double p_delta_seconds) {
    if (paused || p_delta_seconds <= 0.0 || !std::isfinite(p_delta_seconds)) {
        return 0;
    }
    remainder_seconds += p_delta_seconds * rate;
    const int units = static_cast<int>(std::floor(remainder_seconds / 0.1));
    if (units > 0) {
        remainder_seconds -= static_cast<double>(units) * 0.1;
        position_units += units;
    }
    return units;
}

// ------------------------------ NVTimelineEventManager ------------------------------

void NVTimelineEventManager::_bind_methods() {
    ClassDB::bind_method(D_METHOD("register_binding", "label", "handler", "requires_timeline_input"),
            &NVTimelineEventManager::register_binding, DEFVAL(false));
    ClassDB::bind_method(D_METHOD("unregister_binding", "binding_id"), &NVTimelineEventManager::unregister_binding);
    ClassDB::bind_method(D_METHOD("set_binding_enabled", "binding_id", "enabled"), &NVTimelineEventManager::set_binding_enabled);
    ClassDB::bind_method(D_METHOD("publish_label", "label", "source_timeline_id", "source_run_id", "input_id", "time_units"),
            &NVTimelineEventManager::publish_label, DEFVAL(0), DEFVAL(0), DEFVAL(0), DEFVAL(0));
    ClassDB::bind_method(D_METHOD("has_pending_messages"), &NVTimelineEventManager::has_pending_messages);
    ClassDB::bind_method(D_METHOD("take_next_message"), &NVTimelineEventManager::take_next_message);
    ClassDB::bind_method(D_METHOD("clear_queue"), &NVTimelineEventManager::clear_queue);
    ClassDB::bind_method(D_METHOD("clear_bindings"), &NVTimelineEventManager::clear_bindings);
    ClassDB::bind_method(D_METHOD("get_max_messages_per_round"), &NVTimelineEventManager::get_max_messages_per_round);
    ClassDB::bind_method(D_METHOD("set_max_messages_per_round", "limit"), &NVTimelineEventManager::set_max_messages_per_round);
    ClassDB::bind_method(D_METHOD("get_diagnostics"), &NVTimelineEventManager::get_diagnostics);
}

Dictionary NVTimelineEventManager::message_to_dictionary(const QueuedMessage &p_message) {
    Dictionary result;
    result["label"] = p_message.label;
    result["source_timeline_id"] = p_message.source_timeline_id;
    result["source_run_id"] = p_message.source_run_id;
    result["input_id"] = p_message.input_id;
    result["time_units"] = p_message.time_units;
    result["accepted_timeline_ids"] = Array();
    result["accepted_input_ids"] = Array();
    return result;
}

int NVTimelineEventManager::register_binding(const String &p_label, const Callable &p_handler,
        bool p_requires_timeline_input) {
    if (p_label.is_empty() || !p_handler.is_valid()) {
        diagnostics.push_back("拒绝注册标签绑定：标签为空或 Callable 无效");
        return 0;
    }
    Binding binding;
    binding.id = next_binding_id++;
    binding.label = p_label;
    binding.handler = p_handler;
    binding.requires_timeline_input = p_requires_timeline_input;
    bindings.push_back(binding);
    return binding.id;
}

bool NVTimelineEventManager::unregister_binding(int p_binding_id) {
    for (auto it = bindings.begin(); it != bindings.end(); ++it) {
        if (it->id == p_binding_id) {
            bindings.erase(it);
            return true;
        }
    }
    return false;
}

void NVTimelineEventManager::set_binding_enabled(int p_binding_id, bool p_enabled) {
    for (Binding &binding : bindings) {
        if (binding.id == p_binding_id) {
            binding.enabled = p_enabled;
            return;
        }
    }
}

void NVTimelineEventManager::publish_label(const String &p_label, int p_source_timeline_id,
        int p_source_run_id, int p_input_id, int p_time_units) {
    if (p_label.is_empty()) {
        diagnostics.push_back("忽略空标签消息");
        return;
    }
    QueuedMessage message;
    message.label = p_label;
    message.source_timeline_id = p_source_timeline_id;
    message.source_run_id = p_source_run_id;
    message.input_id = p_input_id;
    message.time_units = p_time_units;
    queue.push_back(message);
}

Dictionary NVTimelineEventManager::take_next_message() {
    if (queue.empty()) {
        return Dictionary();
    }
    const QueuedMessage message = queue.front();
    queue.erase(queue.begin());
    return message_to_dictionary(message);
}

void NVTimelineEventManager::invoke_bindings(const Dictionary &p_message) {
    const String label = p_message.get("label", String());
    const Array accepted = p_message.get("accepted_timeline_ids", Array());
    for (const Binding &binding : bindings) {
        if (!binding.enabled || binding.label != label) {
            continue;
        }
        if (binding.requires_timeline_input && accepted.is_empty()) {
            continue;
        }
        // Callable 的参数只有一个 Dictionary，保证 C++、GDScript 和 C# 使用同一
        // 个上下文边界；回调中新发标签只会追加到当前队列，而不会递归调用自己。
        binding.handler.call(p_message);
    }
}

void NVTimelineEventManager::clear_queue() {
    queue.clear();
}

void NVTimelineEventManager::clear_bindings() {
    bindings.clear();
    next_binding_id = 1;
}

void NVTimelineEventManager::set_max_messages_per_round(int p_limit) {
    max_messages_per_round = std::clamp(p_limit, 1, 65536);
}

// ------------------------------ NVTimelineRuntime ------------------------------

void NVTimelineRuntime::_bind_methods() {
    ClassDB::bind_method(D_METHOD("configure", "definition", "output_callback"), &NVTimelineRuntime::configure);
    ClassDB::bind_method(D_METHOD("get_id"), &NVTimelineRuntime::get_id);
    ClassDB::bind_method(D_METHOD("get_clock_id"), &NVTimelineRuntime::get_clock_id);
    ClassDB::bind_method(D_METHOD("get_run_id"), &NVTimelineRuntime::get_run_id);
    ClassDB::bind_method(D_METHOD("get_position"), &NVTimelineRuntime::get_position);
    ClassDB::bind_method(D_METHOD("get_duration"), &NVTimelineRuntime::get_duration);
    ClassDB::bind_method(D_METHOD("get_state"), &NVTimelineRuntime::get_state);
    ClassDB::bind_method(D_METHOD("get_state_name"), &NVTimelineRuntime::get_state_name);
    ClassDB::bind_method(D_METHOD("is_active"), &NVTimelineRuntime::is_active);
    ClassDB::bind_method(D_METHOD("is_waiting"), &NVTimelineRuntime::is_waiting);
    ClassDB::bind_method(D_METHOD("get_waiting_mark_id"), &NVTimelineRuntime::get_waiting_mark_id);
    ClassDB::bind_method(D_METHOD("get_definition"), &NVTimelineRuntime::get_definition);
    ClassDB::bind_method(D_METHOD("start"), &NVTimelineRuntime::start);
    ClassDB::bind_method(D_METHOD("pause"), &NVTimelineRuntime::pause);
    ClassDB::bind_method(D_METHOD("resume"), &NVTimelineRuntime::resume);
    ClassDB::bind_method(D_METHOD("stop"), &NVTimelineRuntime::stop);
    ClassDB::bind_method(D_METHOD("receive_label", "label"), &NVTimelineRuntime::receive_label);
    ClassDB::bind_method(D_METHOD("advance_units", "units"), &NVTimelineRuntime::advance_units);
    BIND_ENUM_CONSTANT(STATE_STOPPED);
    BIND_ENUM_CONSTANT(STATE_RUNNING);
    BIND_ENUM_CONSTANT(STATE_PAUSED);
    BIND_ENUM_CONSTANT(STATE_WAITING);
    BIND_ENUM_CONSTANT(STATE_COMPLETED);
}

bool NVTimelineRuntime::configure(const Dictionary &p_definition, const Callable &p_output_callback) {
    if (!p_definition.has("id") || !p_definition.has("clock_id") ||
            p_definition["id"].get_type() != Variant::INT || p_definition["clock_id"].get_type() != Variant::INT) {
        return false;
    }
    definition = p_definition.duplicate(true);
    id = int(p_definition["id"]);
    clock_id = int(p_definition["clock_id"]);
    duration = -1;
    if (p_definition.has("duration") && p_definition["duration"].get_type() == Variant::INT) {
        duration = int(p_definition["duration"]);
    }
    output_callback = p_output_callback;
    marks.clear();
    received_labels.clear();
    next_mark_index = 0;
    waiting_mark_id = 0;
    position_units = 0;
    state = STATE_STOPPED;
    pause_requested = false;

    const Array tracks = p_definition.get("tracks", Array());
    for (int64_t track_index = 0; track_index < tracks.size(); ++track_index) {
        if (tracks[track_index].get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary track = tracks[track_index];
        if (track.has("enabled") && track["enabled"].get_type() == Variant::BOOL && !bool(track["enabled"])) {
            continue;
        }
        const Array track_marks = track.get("marks", Array());
        for (int64_t mark_index = 0; mark_index < track_marks.size(); ++mark_index) {
            if (track_marks[mark_index].get_type() != Variant::DICTIONARY) {
                continue;
            }
            const Dictionary mark = track_marks[mark_index];
            RuntimeMark runtime_mark;
            runtime_mark.id = int(mark.get("id", 0));
            runtime_mark.time = int(mark.get("time", 0));
            runtime_mark.blocking = String(mark.get("kind", String())) == "blocking";
            runtime_mark.label = String(mark.get("label", String()));
            runtime_mark.track_order = int(track_index);
            runtime_mark.mark_order = int(mark_index);
            marks.push_back(runtime_mark);
        }
    }
    std::stable_sort(marks.begin(), marks.end(), [](const RuntimeMark &a, const RuntimeMark &b) {
        if (a.time != b.time) {
            return a.time < b.time;
        }
        if (a.track_order != b.track_order) {
            return a.track_order < b.track_order;
        }
        return a.mark_order < b.mark_order;
    });
    return true;
}

bool NVTimelineRuntime::has_received(const String &p_label) const {
    return std::find(received_labels.begin(), received_labels.end(), p_label) != received_labels.end();
}

String NVTimelineRuntime::get_state_name() const {
    switch (state) {
        case STATE_RUNNING: return "running";
        case STATE_PAUSED: return "paused";
        case STATE_WAITING: return "waiting";
        case STATE_COMPLETED: return "completed";
        default: return "stopped";
    }
}

void NVTimelineRuntime::start() {
    ++run_id;
    if (run_id <= 0) {
        run_id = 1;
    }
    position_units = 0;
    next_mark_index = 0;
    waiting_mark_id = 0;
    received_labels.clear();
    pause_requested = false;
    state = STATE_RUNNING;
    evaluate_until(0);
}

void NVTimelineRuntime::pause() {
    if (is_active()) {
        pause_requested = true;
        state = STATE_PAUSED;
    }
}

void NVTimelineRuntime::resume() {
    if (!is_active()) {
        return;
    }
    pause_requested = false;
    state = waiting_mark_id != 0 ? STATE_WAITING : STATE_RUNNING;
    if (state == STATE_RUNNING) {
        evaluate_until(position_units);
    }
}

void NVTimelineRuntime::stop() {
    state = STATE_STOPPED;
    position_units = 0;
    next_mark_index = 0;
    waiting_mark_id = 0;
    received_labels.clear();
    pause_requested = false;
}

Array NVTimelineRuntime::receive_label(const String &p_label) {
    Array accepted_input_ids;
    if (!is_active() || p_label.is_empty()) {
        return accepted_input_ids;
    }
    if (!has_received(p_label)) {
        received_labels.push_back(p_label);
    }

    // 阻塞输入没有独立的接收对象数组，它们由 marks 表达；遍历启用轨道可以
    // 同时满足多个期待同名标签，符合“同一标签可供多个输入使用”的首版规则。
    const Array tracks = definition.get("tracks", Array());
    for (int64_t track_index = 0; track_index < tracks.size(); ++track_index) {
        if (tracks[track_index].get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary track = tracks[track_index];
        if (track.has("enabled") && track["enabled"].get_type() == Variant::BOOL && !bool(track["enabled"])) {
            continue;
        }
        const Array track_marks = track.get("marks", Array());
        for (int64_t mark_index = 0; mark_index < track_marks.size(); ++mark_index) {
            if (track_marks[mark_index].get_type() != Variant::DICTIONARY) {
                continue;
            }
            const Dictionary mark = track_marks[mark_index];
            if (String(mark.get("kind", String())) != "blocking" ||
                    String(mark.get("label", String())) != p_label) {
                continue;
            }
            const int mark_id = int(mark.get("id", 0));
            // 已经被游标越过的阻塞标记不再是当前运行的有效入口；否则一条迟到
            // 标签会被错误地报告为新输入，进而重复触发业务绑定。
            const auto mark_it = std::find_if(marks.begin(), marks.end(), [mark_id](const RuntimeMark &p_runtime_mark) {
                return p_runtime_mark.id == mark_id;
            });
            if (mark_it != marks.end() && mark_it < marks.begin() + next_mark_index) {
                continue;
            }
            accepted_input_ids.push_back(mark_id);
            if (waiting_mark_id == mark_id) {
                waiting_mark_id = 0;
                ++next_mark_index;
            }
        }
    }
    // listeners 属于时间轴而不是轨道；必须在轨道循环之外处理，否则多轨道会
    // 把同一全程入口重复投递多次，进而重复创建业务事件实例。
    const Array listeners = definition.get("listeners", Array());
    for (int64_t listener_index = 0; listener_index < listeners.size(); ++listener_index) {
        if (listeners[listener_index].get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary listener = listeners[listener_index];
        const bool enabled = !listener.has("enabled") || bool(listener["enabled"]);
        if (enabled && String(listener.get("label", String())) == p_label) {
            accepted_input_ids.push_back(int(listener.get("id", 0)));
        }
    }
    if (waiting_mark_id == 0 && state == STATE_WAITING) {
        state = pause_requested ? STATE_PAUSED : STATE_RUNNING;
        if (state == STATE_RUNNING) {
            evaluate_until(position_units);
        }
    }
    return accepted_input_ids;
}

int NVTimelineRuntime::advance_units(int p_units) {
    if (p_units <= 0 || state != STATE_RUNNING || waiting_mark_id != 0) {
        return 0;
    }
    const int target = duration > 0 ? std::min(position_units + p_units, duration) : position_units + p_units;
    const int old_position = position_units;
    evaluate_until(target);
    return position_units - old_position;
}

void NVTimelineRuntime::evaluate_until(int p_target_units) {
    if (state != STATE_RUNNING || waiting_mark_id != 0) {
        return;
    }
    while (next_mark_index < static_cast<int>(marks.size())) {
        const RuntimeMark &mark = marks[next_mark_index];
        if (mark.time > p_target_units) {
            break;
        }
        if (mark.blocking && !has_received(mark.label)) {
            position_units = mark.time;
            waiting_mark_id = mark.id;
            state = STATE_WAITING;
            return;
        }
        position_units = std::max(position_units, mark.time);
        ++next_mark_index;
        if (!mark.blocking && output_callback.is_valid()) {
            output_callback.call(mark.label, id, run_id, mark.time);
        }
    }
    position_units = std::max(position_units, p_target_units);
    if (duration > 0 && position_units >= duration && next_mark_index >= static_cast<int>(marks.size())) {
        state = STATE_COMPLETED;
    }
}

// ------------------------------ NVTimelineService ------------------------------

NVTimelineService::NVTimelineService() {
    event_manager.instantiate();
    document.instantiate();
    // PROCESS_MODE_ALWAYS 让桌面时钟在游戏暂停时继续运行；用户仍可以通过
    // pause_timeline 或 set_paused 明确暂停某条时间轴，不依赖场景树暂停状态。
    set_process_mode(Node::PROCESS_MODE_ALWAYS);
    set_process(true);
}

void NVTimelineService::_bind_methods() {
    // 运行实例通过 Callable 回调到 Service；即使该方法主要供 C++ 内部使用，
    // 也必须登记到 ClassDB，Godot 才能从 Callable 正确解析方法名。
    ClassDB::bind_method(D_METHOD("_on_runtime_output", "label", "timeline_id", "run_id", "time_units"),
            &NVTimelineService::on_runtime_output);
    ClassDB::bind_method(D_METHOD("load_document", "data"), &NVTimelineService::load_document);
    ClassDB::bind_method(D_METHOD("load_json_file", "path"), &NVTimelineService::load_json_file);
    ClassDB::bind_method(D_METHOD("save_json_file", "path", "pretty"), &NVTimelineService::save_json_file,
            DEFVAL(true));
    ClassDB::bind_method(D_METHOD("get_document_data"), &NVTimelineService::get_document_data);
    ClassDB::bind_method(D_METHOD("get_document"), &NVTimelineService::get_document);
    ClassDB::bind_method(D_METHOD("get_event_manager"), &NVTimelineService::get_event_manager);
    ClassDB::bind_method(D_METHOD("get_diagnostics"), &NVTimelineService::get_diagnostics);
    ClassDB::bind_method(D_METHOD("clear_diagnostics"), &NVTimelineService::clear_diagnostics);
    ClassDB::bind_method(D_METHOD("get_clock_ids"), &NVTimelineService::get_clock_ids);
    ClassDB::bind_method(D_METHOD("get_timeline_ids"), &NVTimelineService::get_timeline_ids);
    ClassDB::bind_method(D_METHOD("get_clock", "clock_id"), &NVTimelineService::get_clock);
    ClassDB::bind_method(D_METHOD("get_timeline", "timeline_id"), &NVTimelineService::get_timeline);
    ClassDB::bind_method(D_METHOD("start_timeline", "timeline_id"), &NVTimelineService::start_timeline);
    ClassDB::bind_method(D_METHOD("pause_timeline", "timeline_id"), &NVTimelineService::pause_timeline);
    ClassDB::bind_method(D_METHOD("resume_timeline", "timeline_id"), &NVTimelineService::resume_timeline);
    ClassDB::bind_method(D_METHOD("stop_timeline", "timeline_id"), &NVTimelineService::stop_timeline);
    ClassDB::bind_method(D_METHOD("advance_clock", "clock_id", "units"), &NVTimelineService::advance_clock);
    ClassDB::bind_method(D_METHOD("publish_label", "label", "source_timeline_id", "source_run_id", "input_id", "time_units"),
            &NVTimelineService::publish_label, DEFVAL(0), DEFVAL(0), DEFVAL(0), DEFVAL(0));
    ADD_SIGNAL(MethodInfo("timeline_started", PropertyInfo(Variant::INT, "timeline_id"),
            PropertyInfo(Variant::INT, "run_id")));
    ADD_SIGNAL(MethodInfo("timeline_paused", PropertyInfo(Variant::INT, "timeline_id")));
    ADD_SIGNAL(MethodInfo("timeline_completed", PropertyInfo(Variant::INT, "timeline_id"),
            PropertyInfo(Variant::INT, "run_id")));
    ADD_SIGNAL(MethodInfo("label_emitted", PropertyInfo(Variant::STRING, "label"),
            PropertyInfo(Variant::INT, "timeline_id"), PropertyInfo(Variant::INT, "time_units")));
    ADD_SIGNAL(MethodInfo("diagnostic", PropertyInfo(Variant::STRING, "message")));
}

void NVTimelineService::_process(double p_delta) {
    for (const Ref<NVTimelineClock> &clock : clocks) {
        if (clock.is_null() || clock->get_mode() != NVTimelineClock::MODE_REALTIME) {
            continue;
        }
        const int units = clock->advance_seconds(p_delta);
        if (units <= 0) {
            continue;
        }
        for (const Ref<NVTimelineRuntime> &timeline : timelines) {
            if (timeline.is_valid() && timeline->get_clock_id() == clock->get_id()) {
                const NVTimelineRuntime::RuntimeState before_state = timeline->get_state();
                timeline->advance_units(units);
                emit_completed_if_needed(timeline, before_state);
            }
        }
    }
    dispatch_messages();
}

void NVTimelineService::dispatch_messages() {
    // Callable 回调可能再次发布标签；只追加到当前队列尾部，待当前轮次继续，
    // 不能从回调内部递归进入派发函数，否则两个绑定互相发布时会耗尽调用栈。
    if (dispatching_messages) {
        return;
    }
    dispatching_messages = true;
    int dispatched = 0;
    const int limit = event_manager->get_max_messages_per_round();
    while (event_manager->has_pending_messages() && dispatched < limit) {
        Dictionary message = event_manager->take_next_message();
        const String label = message.get("label", String());
        Array accepted_timeline_ids;
        Array accepted_input_ids;
        for (const Ref<NVTimelineRuntime> &timeline : timelines) {
            if (timeline.is_null() || !timeline->is_active()) {
                continue;
            }
            const NVTimelineRuntime::RuntimeState before_state = timeline->get_state();
            const Array accepted = timeline->receive_label(label);
            emit_completed_if_needed(timeline, before_state);
            if (accepted.is_empty()) {
                continue;
            }
            accepted_timeline_ids.push_back(timeline->get_id());
            for (int64_t i = 0; i < accepted.size(); ++i) {
                accepted_input_ids.push_back(accepted[i]);
            }
        }
        message["accepted_timeline_ids"] = accepted_timeline_ids;
        message["accepted_input_ids"] = accepted_input_ids;
        event_manager->invoke_bindings(message);
        ++dispatched;
    }
    if (event_manager->has_pending_messages()) {
        add_diagnostic("单轮标签派发达到上限，可能存在循环触发；剩余消息将在下一帧继续");
    }
    dispatching_messages = false;
}

void NVTimelineService::on_runtime_output(const String &p_label, int p_timeline_id,
        int p_run_id, int p_time_units) {
    event_manager->publish_label(p_label, p_timeline_id, p_run_id, 0, p_time_units);
    emit_signal("label_emitted", p_label, p_timeline_id, p_time_units);
}

void NVTimelineService::add_diagnostic(const String &p_message) {
    diagnostics.push_back(p_message);
    emit_signal("diagnostic", p_message);
}

void NVTimelineService::emit_completed_if_needed(const Ref<NVTimelineRuntime> &p_timeline,
        NVTimelineRuntime::RuntimeState p_before_state) {
    if (p_timeline.is_valid() && p_before_state != NVTimelineRuntime::STATE_COMPLETED &&
            p_timeline->get_state() == NVTimelineRuntime::STATE_COMPLETED) {
        emit_signal("timeline_completed", p_timeline->get_id(), p_timeline->get_run_id());
    }
}

Ref<NVTimelineClock> NVTimelineService::find_clock(int p_clock_id) const {
    for (const Ref<NVTimelineClock> &clock : clocks) {
        if (clock.is_valid() && clock->get_id() == p_clock_id) {
            return clock;
        }
    }
    return Ref<NVTimelineClock>();
}

Ref<NVTimelineRuntime> NVTimelineService::find_timeline(int p_timeline_id) const {
    for (const Ref<NVTimelineRuntime> &timeline : timelines) {
        if (timeline.is_valid() && timeline->get_id() == p_timeline_id) {
            return timeline;
        }
    }
    return Ref<NVTimelineRuntime>();
}

bool NVTimelineService::build_from_document(const Dictionary &p_data) {
    clocks.clear();
    timelines.clear();
    event_manager->clear_queue();
    const Array clock_array = p_data.get("clocks", Array());
    for (int64_t i = 0; i < clock_array.size(); ++i) {
        const Dictionary clock_data = clock_array[i];
        Ref<NVTimelineClock> clock;
        clock.instantiate();
        const String mode = String(clock_data.get("mode", "manual"));
        clock->configure(int(clock_data.get("id", 0)), string_or_default(clock_data, "name",
                String::num_int64(int(clock_data.get("id", 0)))),
                mode == "realtime" ? NVTimelineClock::MODE_REALTIME : NVTimelineClock::MODE_MANUAL,
                double(clock_data.get("rate", 1.0)));
        clocks.push_back(clock);
    }
    const Callable output_callback(this, "_on_runtime_output");
    const Array timeline_array = p_data.get("timelines", Array());
    for (int64_t i = 0; i < timeline_array.size(); ++i) {
        Ref<NVTimelineRuntime> timeline;
        timeline.instantiate();
        if (!timeline->configure(Dictionary(timeline_array[i]), output_callback)) {
            add_diagnostic("时间轴运行实例构造失败：索引 " + String::num_int64(i));
            return false;
        }
        timelines.push_back(timeline);
    }
    return true;
}

bool NVTimelineService::load_document(const Dictionary &p_data) {
    Ref<NVTimelineDocument> candidate;
    candidate.instantiate();
    if (!candidate->set_data(p_data)) {
        diagnostics = candidate->get_validation_errors();
        for (int64_t i = 0; i < diagnostics.size(); ++i) {
            emit_signal("diagnostic", diagnostics[i]);
        }
        return false;
    }
    if (!build_from_document(candidate->get_data())) {
        return false;
    }
    document = candidate;
    diagnostics.clear();
    return true;
}

bool NVTimelineService::load_json_file(const String &p_path) {
    Ref<NVTimelineDocument> candidate;
    candidate.instantiate();
    if (!candidate->load_file(p_path)) {
        diagnostics = candidate->get_validation_errors();
        return false;
    }
    return load_document(candidate->get_data());
}

bool NVTimelineService::save_json_file(const String &p_path, bool p_pretty) const {
    return document.is_valid() && document->save_file(p_path, p_pretty);
}

Dictionary NVTimelineService::get_document_data() const {
    return document.is_valid() ? document->get_data() : Dictionary();
}

void NVTimelineService::clear_diagnostics() {
    diagnostics.clear();
    event_manager->clear_queue();
}

Array NVTimelineService::get_clock_ids() const {
    Array result;
    for (const Ref<NVTimelineClock> &clock : clocks) {
        if (clock.is_valid()) {
            result.push_back(clock->get_id());
        }
    }
    return result;
}

Array NVTimelineService::get_timeline_ids() const {
    Array result;
    for (const Ref<NVTimelineRuntime> &timeline : timelines) {
        if (timeline.is_valid()) {
            result.push_back(timeline->get_id());
        }
    }
    return result;
}

bool NVTimelineService::start_timeline(int p_timeline_id) {
    const Ref<NVTimelineRuntime> timeline = find_timeline(p_timeline_id);
    if (timeline.is_null()) {
        add_diagnostic("启动失败：找不到时间轴 " + String::num_int64(p_timeline_id));
        return false;
    }
    timeline->start();
    emit_signal("timeline_started", p_timeline_id, timeline->get_run_id());
    return true;
}

bool NVTimelineService::pause_timeline(int p_timeline_id) {
    const Ref<NVTimelineRuntime> timeline = find_timeline(p_timeline_id);
    if (timeline.is_null() || !timeline->is_active()) {
        return false;
    }
    timeline->pause();
    emit_signal("timeline_paused", p_timeline_id);
    return true;
}

bool NVTimelineService::resume_timeline(int p_timeline_id) {
    const Ref<NVTimelineRuntime> timeline = find_timeline(p_timeline_id);
    if (timeline.is_null() || !timeline->is_active()) {
        return false;
    }
    timeline->resume();
    return true;
}

bool NVTimelineService::stop_timeline(int p_timeline_id) {
    const Ref<NVTimelineRuntime> timeline = find_timeline(p_timeline_id);
    if (timeline.is_null()) {
        return false;
    }
    timeline->stop();
    return true;
}

int NVTimelineService::advance_clock(int p_clock_id, int p_units) {
    const Ref<NVTimelineClock> clock = find_clock(p_clock_id);
    if (clock.is_null() || p_units <= 0) {
        return 0;
    }
    const int accepted_units = clock->advance_units(p_units);
    for (const Ref<NVTimelineRuntime> &timeline : timelines) {
        if (timeline.is_valid() && timeline->get_clock_id() == p_clock_id) {
            const NVTimelineRuntime::RuntimeState before_state = timeline->get_state();
            timeline->advance_units(accepted_units);
            emit_completed_if_needed(timeline, before_state);
        }
    }
    dispatch_messages();
    return accepted_units;
}

void NVTimelineService::publish_label(const String &p_label, int p_source_timeline_id,
        int p_source_run_id, int p_input_id, int p_time_units) {
    event_manager->publish_label(p_label, p_source_timeline_id, p_source_run_id, p_input_id, p_time_units);
    dispatch_messages();
}

} // namespace godot
