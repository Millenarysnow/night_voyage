#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

/**
 * 序章的权威状态与操作校验器。
 *
 * 这个类只保存剧情进度和可验证的内容配置，不负责创建界面，也不依赖
 * CEF 的具体实现。网页或 Godot 原生界面都只能通过 dispatch_action() 提交
 * 玩家操作，避免把剧情状态分散到多个显示层中。
 */
class NVPrologueState : public Node {
	GDCLASS(NVPrologueState, Node)

	public:
		/** 序章阶段名称，字符串会直接出现在调试快照和 IPC 消息中。 */
		enum Stage {
			STAGE_BOOT = 0,
			STAGE_CHAT = 1,
			STAGE_SEARCH = 2,
			STAGE_ARTICLE = 3,
			STAGE_FORUM = 4,
			STAGE_LOCATION = 5,
			STAGE_DONE = 6,
		};

	private:
		/** 当前序章阶段。所有阶段推进都集中在 dispatch_action()。 */
		Stage stage = STAGE_BOOT;
		/** 每次状态变化递增，供 CEF 丢弃旧页面快照。 */
		int64_t revision = 0;
		/** 当前序章使用的宠物配置，由上层内容数据注入。 */
		String pet_species = "cat";
		String pet_name = "年糕";
		String appearance_clue = "";
		String expected_location = "";
		String article_id = "lost_pet";
		String forum_id = "pet_sighting";
		/** 已经由玩家明确收下的线索类型，例如 text / image。 */
		Array collected_clues;

		static void _bind_methods();

		/** 阶段转成稳定的协议字符串，避免界面依赖整数枚举值。 */
		String stage_name() const;
		/** 生成统一成功或失败结果，保证所有调用方得到相同字段。 */
		Dictionary make_result(bool success, const String &code, const String &message) const;
		/** 判断字符串是否包含目标文本；空目标永远不匹配。 */
		bool contains_target(const String &value, const String &target) const;
		/** 判断指定线索是否已经收集，避免重复操作推进状态。 */
		bool has_clue(const String &clue_type) const;
		/** 所有要求的线索都收齐后，才允许进入论坛阶段。 */
		void update_after_clue();

	public:
		NVPrologueState() = default;
		~NVPrologueState() override = default;

		/** 清空运行时进度，但保留当前注入的宠物内容配置。 */
		void reset();
		/** 注入可复用的宠物序章内容，剧情逻辑不依赖具体是猫还是狗。 */
		void configure_pet(const String &species, const String &name,
				const String &appearance, const String &location,
				const String &article, const String &forum);
		/** 返回当前内容配置，供页面渲染和验收工具读取。 */
		Dictionary get_pet_profile() const;
		/** 返回可序列化的权威进度快照。 */
		Dictionary get_snapshot() const;
		/** 校验并执行一个序章操作，返回统一的结果快照。 */
		Dictionary dispatch_action(const String &action, const Dictionary &payload = Dictionary());
};

} // namespace godot

