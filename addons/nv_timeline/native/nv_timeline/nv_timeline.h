#pragma once

// 该头文件只依赖 Godot 的稳定 GDExtension 包装类型；所有时间轴业务数据仍
// 以普通 C++ 结构保存，避免把编排 JSON 和 Godot 场景对象生命周期耦合起来。
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace godot {

/**
 * 时间轴编排文档的 JSON 读写和严格校验入口。
 *
 * 该类只保存静态编排数据，不保存运行游标、消息队列或事件处理实例，因而
 * 可以被编辑器和运行时分别使用，也可以在不启动游戏的情况下完成校验。
 */
class NVTimelineDocument : public RefCounted {
    GDCLASS(NVTimelineDocument, RefCounted)

    Dictionary data;
    PackedStringArray validation_errors;

    static void _bind_methods();
    bool validate_internal(const Dictionary &p_data);

public:
    NVTimelineDocument();

    /// 创建符合设计稿最小结构的空文档，方便编辑器新建文件。
    void create_empty(const String &p_name = String::utf8("新时间轴文档"));
    /// 从 JSON 文本加载并校验；失败时保留原文档并提供诊断。
    bool load_text(const String &p_text);
    /// 从 Godot 路径读取 UTF-8 JSON 文件并加载。
    bool load_file(const String &p_path);
    /// 将当前文档以可读 JSON 写回 Godot 路径。
    bool save_file(const String &p_path, bool p_pretty = true) const;
    /// 设置一份已经构造好的 Dictionary；只有完整通过校验时才会替换当前数据。
    bool set_data(const Dictionary &p_data);
    Dictionary get_data() const { return data; }
    PackedStringArray get_validation_errors() const { return validation_errors; }
    bool is_valid() const { return validation_errors.is_empty() && !data.is_empty(); }
    /// 按文档级规则分配 ID，溢出或文档无效时返回 0。
    int allocate_id();
};

/**
 * 逻辑时钟。时间单位固定为 0.1 秒，实时模式只在这里累计不足一个单位的
 * 小数部分；时间轴本身只接收已经取整的推进区间。
 */
class NVTimelineClock : public RefCounted {
    GDCLASS(NVTimelineClock, RefCounted)

public:
    enum ClockMode {
        MODE_MANUAL = 0,
        MODE_REALTIME = 1,
    };

private:
    int id = 0;
    String name;
    ClockMode mode = MODE_MANUAL;
    double rate = 1.0;
    int64_t position_units = 0;
    double remainder_seconds = 0.0;
    bool paused = false;

    static void _bind_methods();

public:
    NVTimelineClock() = default;

    void configure(int p_id, const String &p_name, ClockMode p_mode, double p_rate);
    int get_id() const { return id; }
    String get_clock_name() const { return name; }
    ClockMode get_mode() const { return mode; }
    double get_rate() const { return rate; }
    int64_t get_position() const { return position_units; }
    bool is_paused() const { return paused; }
    void set_paused(bool p_paused) { paused = p_paused; }
    void reset();
    /// 手动推进并返回实际接受的单位数；负数请求会被拒绝。
    int advance_units(int p_units);
    /// 实时推进，返回本次产生的完整 0.1 秒单位数。
    int advance_seconds(double p_delta_seconds);
};

/**
 * 统一标签消息管理器。
 *
 * 消息先进入队列，再由 NVTimelineService 在安全的派发轮次中交给时间轴和
 * 程序绑定。Callable 是 Godot/C# 的共同边界，核心不要求业务事件继承某个
 * C++ 类型；绑定的所有权由管理器保存到注销或清空为止。
 */
class NVTimelineEventManager : public RefCounted {
    GDCLASS(NVTimelineEventManager, RefCounted)

    struct Binding {
        int id = 0;
        String label;
        Callable handler;
        bool requires_timeline_input = false;
        bool enabled = true;
    };

    struct QueuedMessage {
        String label;
        int source_timeline_id = 0;
        int source_run_id = 0;
        int input_id = 0;
        int time_units = 0;
    };

    std::vector<Binding> bindings;
    std::vector<QueuedMessage> queue;
    int next_binding_id = 1;
    int max_messages_per_round = 1024;
    PackedStringArray diagnostics;

    static void _bind_methods();
    static Dictionary message_to_dictionary(const QueuedMessage &p_message);

public:
    NVTimelineEventManager() = default;

    int register_binding(const String &p_label, const Callable &p_handler,
            bool p_requires_timeline_input = false);
    bool unregister_binding(int p_binding_id);
    void set_binding_enabled(int p_binding_id, bool p_enabled);
    void publish_label(const String &p_label, int p_source_timeline_id = 0,
            int p_source_run_id = 0, int p_input_id = 0, int p_time_units = 0);
    bool has_pending_messages() const { return !queue.empty(); }
    Dictionary take_next_message();
    /// 对已经由 Service 路由过的消息执行代码绑定；回调中新发的消息只会进入队列尾部。
    void invoke_bindings(const Dictionary &p_message);
    void clear_queue();
    void clear_bindings();
    int get_max_messages_per_round() const { return max_messages_per_round; }
    void set_max_messages_per_round(int p_limit);
    PackedStringArray get_diagnostics() const { return diagnostics; }
};

/**
 * 一条时间轴定义的运行实例。
 *
 * 运行时将启用轨道中的 marks 展平成稳定序列：先按时间，再按轨道数组顺序，
 * 最后按 marks 数组顺序。阻塞标记保持游标不越过，因而一次大步推进不会跳过
 * 后续输出；未消耗的推进量按首版规则丢弃，下一次时钟推进再继续计算。
 */
class NVTimelineRuntime : public RefCounted {
    GDCLASS(NVTimelineRuntime, RefCounted)

public:
    enum RuntimeState {
        STATE_STOPPED = 0,
        STATE_RUNNING = 1,
        STATE_PAUSED = 2,
        STATE_WAITING = 3,
        STATE_COMPLETED = 4,
    };

private:
    struct RuntimeMark {
        int id = 0;
        int time = 0;
        bool blocking = false;
        String label;
        int track_order = 0;
        int mark_order = 0;
    };

    Dictionary definition;
    std::vector<RuntimeMark> marks;
    std::vector<String> received_labels;
    Callable output_callback;
    int id = 0;
    int clock_id = 0;
    int duration = -1;
    int position_units = 0;
    int next_mark_index = 0;
    int waiting_mark_id = 0;
    int run_id = 0;
    RuntimeState state = STATE_STOPPED;
    bool pause_requested = false;

    static void _bind_methods();
    bool has_received(const String &p_label) const;
    void evaluate_until(int p_target_units);

public:
    NVTimelineRuntime() = default;

    /// 从已校验的时间轴 Dictionary 构造运行实例；不会自动启动。
    bool configure(const Dictionary &p_definition, const Callable &p_output_callback);
    int get_id() const { return id; }
    int get_clock_id() const { return clock_id; }
    int get_run_id() const { return run_id; }
    int get_position() const { return position_units; }
    int get_duration() const { return duration; }
    RuntimeState get_state() const { return state; }
    bool is_active() const { return state == STATE_RUNNING || state == STATE_PAUSED || state == STATE_WAITING; }
    bool is_waiting() const { return waiting_mark_id != 0; }
    int get_waiting_mark_id() const { return waiting_mark_id; }
    Dictionary get_definition() const { return definition; }
    String get_state_name() const;

    void start();
    void pause();
    void resume();
    void stop();
    /// 收到标签后返回本实例接收的入口 ID 列表，并在必要时解除阻塞。
    Array receive_label(const String &p_label);
    /// 由绑定时钟调用，返回本次实际向前移动的单位数。
    int advance_units(int p_units);
};

/**
 * 时间轴系统节点。一个 Service 持有所有时钟、对等时间轴和唯一事件管理器，
 * 可作为普通场景节点或 Autoload 使用；节点自身始终处理 realtime 时钟，即使
 * 游戏场景暂停，也不会把后台时间轴误绑定到场景树的可暂停逻辑。
 */
class NVTimelineService : public Node {
    GDCLASS(NVTimelineService, Node)

    std::vector<Ref<NVTimelineClock>> clocks;
    std::vector<Ref<NVTimelineRuntime>> timelines;
    Ref<NVTimelineEventManager> event_manager;
    Ref<NVTimelineDocument> document;
    PackedStringArray diagnostics;
    bool dispatching_messages = false;

    static void _bind_methods();

public:
    // godot-cpp 的虚函数注册模板需要直接访问该入口，因此保持 public；
    // 外部业务仍应使用 Service 的公开推进 API 驱动时间轴。
    void _process(double p_delta) override;

private:
    void dispatch_messages();
    void on_runtime_output(const String &p_label, int p_timeline_id, int p_run_id, int p_time_units);
    void add_diagnostic(const String &p_message);
    void emit_completed_if_needed(const Ref<NVTimelineRuntime> &p_timeline,
            NVTimelineRuntime::RuntimeState p_before_state);
    Ref<NVTimelineClock> find_clock(int p_clock_id) const;
    Ref<NVTimelineRuntime> find_timeline(int p_timeline_id) const;
    bool build_from_document(const Dictionary &p_data);

public:
    NVTimelineService();
    ~NVTimelineService() override = default;

    bool load_document(const Dictionary &p_data);
    bool load_json_file(const String &p_path);
    bool save_json_file(const String &p_path, bool p_pretty = true) const;
    Dictionary get_document_data() const;
    Ref<NVTimelineDocument> get_document() const { return document; }
    Ref<NVTimelineEventManager> get_event_manager() const { return event_manager; }
    PackedStringArray get_diagnostics() const { return diagnostics; }
    void clear_diagnostics();

    Array get_clock_ids() const;
    Array get_timeline_ids() const;
    Ref<NVTimelineClock> get_clock(int p_clock_id) const { return find_clock(p_clock_id); }
    Ref<NVTimelineRuntime> get_timeline(int p_timeline_id) const { return find_timeline(p_timeline_id); }

    bool start_timeline(int p_timeline_id);
    bool pause_timeline(int p_timeline_id);
    bool resume_timeline(int p_timeline_id);
    bool stop_timeline(int p_timeline_id);
    /// 手动时钟推进；每条绑定该时钟的时间轴收到同一个推进区间。
    int advance_clock(int p_clock_id, int p_units);
    /// 外部系统和脚本统一从这里发标签，不直接改写时间轴游标。
    void publish_label(const String &p_label, int p_source_timeline_id = 0,
            int p_source_run_id = 0, int p_input_id = 0, int p_time_units = 0);
};

} // namespace godot

VARIANT_ENUM_CAST(godot::NVTimelineClock::ClockMode);
VARIANT_ENUM_CAST(godot::NVTimelineRuntime::RuntimeState);
