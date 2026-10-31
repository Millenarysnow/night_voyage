#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

/**
 * 序章流程的权威状态与规则校验器。
 *
 * 该类不创建界面，也不依赖 CEF。所有剧情操作都由 Godot UI/CEF 转成
 * dispatch_action() 请求；流程进度、白板线索和任务判定只在此处保存。
 * 剧情文案和页面内容由项目 JSON 注入，因此策划调整原型时不需要复制规则代码。
 */
class NVPrologueState : public Node {
	GDCLASS(NVPrologueState, Node)

public:
	/** 阶段值只用于调试和快照展示，业务判断依赖里程碑而非线性阶段锁。 */
	enum Stage {
		STAGE_BOOT = 0,
		STAGE_CHAT = 1,
		STAGE_ARTICLE = 2,
		STAGE_FORUM = 3,
		STAGE_QUEST_READY = 4,
		STAGE_RETURNING = 5,
		STAGE_DONE = 6,
	};

private:
	/** 由内容 JSON 注入的宠物、网页、任务和对话配置。 */
	Dictionary content;
	/** 由玩家亲手收录的文字/图片线索；每项都保存来源和当前白板挂接位置。 */
	Array clues;
	/** 玩家调整后的白板镜头状态，供关闭窗口或重建界面后继续使用。 */
	Dictionary board_view;
	/** 最近一次搜索结果，作为打开页面的前置条件。 */
	Array current_results;
	/** 当前 CEF 页面标识，用于拒绝来自非当前页面的收录请求。 */
	String current_page = "home";
	/** 当前打开的一般网页结果 ID；只用于 CEF 正文页面的只读显示。 */
	String active_result_id;
	/** 最近搜索使用的站点与查询词，属于浏览器视图状态。 */
	String last_search_scope = "web";
	String last_search_query;
	/** 每次状态变化递增，用来丢弃异步 IPC 返回的旧快照。 */
	int64_t revision = 0;
	/** 线索 ID 在本次序章中单调分配，避免重复收录覆盖旧卡片。 */
	int64_t next_clue_id = 1;

	/** 以下布尔值记录可复用流程中的里程碑与一次性对白。 */
	bool started = false;
	bool broad_search_answered = false;
	bool article_result_available = false;
	bool forum_result_available = false;
	bool article_opened = false;
	bool forum_post_opened = false;
	bool notice_seen_said = false;
	bool notice_pic_said = false;
	bool task_tip_said = false;
	bool forum_prompt_said = false;
	bool found_said = false;
	bool appearance_task_done = false;
	bool location_task_done = false;
	bool quest_ready = false;
	bool returning = false;
	bool complete = false;

	static void _bind_methods();

	/** 把当前里程碑转换成稳定的调试字符串，不把界面路由误当成剧情进度。 */
	String stage_name() const;
	/** 读取内容字典的一个子字典，字段缺失时返回空字典。 */
	Dictionary get_section(const String &section_name) const;
	/** 从指定内容区读取字符串字段，并允许调用方提供兼容默认值。 */
	String get_content_text(const String &section_name, const String &field_name,
		const String &fallback = "") const;
	/** 从指定内容区读取数组字段，字段缺失或类型错误时返回空数组。 */
	Array get_content_array(const String &section_name, const String &field_name) const;
	/** 判断文本是否包含数组中任意一个有效关键词；空关键词不会误命中。 */
	bool contains_any(const String &value, const Array &targets) const;
	/** 判断两个字符串是否包含关系，忽略大小写并拒绝空目标。 */
	bool contains_target(const String &value, const String &target) const;
	/** 按任务 ID 查找内容 JSON 中定义的白板子任务。 */
	Dictionary find_task(const String &task_id) const;
	/** 按线索 ID 查找白板卡片；未找到时返回 -1。 */
	int find_clue_index(const String &clue_id) const;
	/** 验证卡片坐标、缩放、旋转和批注，整项合法后原子替换线索记录。 */
	bool apply_clue_layout(Dictionary &clue, const Dictionary &payload, bool &changed) const;
	/** 检查指定页面是否已有指定类型的玩家收录记录。 */
	bool has_page_clue(const String &page_id, const String &kind) const;
	/** 检查网页结果数组中是否含有指定结果 ID。 */
	bool results_contain_id(const String &result_id) const;
	/** 根据内容配置和线索当前挂接位置累积更新两个任务完成状态。 */
	void evaluate_quest_tasks();
	/** 生成包含操作结果、对白队列和权威快照的统一响应。 */
	Dictionary make_result(bool success, const String &code, const String &message,
		const Array &say = Array()) const;
	/** 创建搜索结果条目，页面数据仍由内容 JSON 提供。 */
	Dictionary make_search_result(const Dictionary &source, const String &kind) const;
	/** 判断搜索词是否为需要先扑空的泛化詞。 */
	bool is_broad_query(const String &query) const;
	/** 判断文本是否提及启事中已有的粗略地理范围。 */
	bool is_rough_location(const String &location) const;
	/** 依照一次性标记，把收录后的引导对白按原型顺序加入响应。 */
	void append_post_collection_dialogue(Array &say);
	/** 推进版本号并发送状态变更信号，确保快照与广播保持一致。 */
	void commit_state_change();

public:
	NVPrologueState() = default;
	~NVPrologueState() override = default;

	/** 注入可复用的剧情数据并从干净进度开始；不会把进度写回页面层。 */
	void configure_content(const Dictionary &p_content);
	/** 兼容早期原型接口：只覆盖宠物摘要和启事/论坛 ID。 */
	void configure_pet(const String &species, const String &name,
		const String &appearance, const String &location,
		const String &article, const String &forum);
	/** 清空运行进度但保留已经注入的内容配置。 */
	void reset();
	/** 返回给 Godot/CEF 使用的宠物摘要。 */
	Dictionary get_pet_profile() const;
	/** 返回页面、线索、任务和剧情里程碑的统一权威快照。 */
	Dictionary get_snapshot() const;
	/** 校验并执行一个剧情操作；失败操作不改变 revision 或游戏状态。 */
	Dictionary dispatch_action(const String &action, const Dictionary &payload = Dictionary());
};

} // namespace godot
