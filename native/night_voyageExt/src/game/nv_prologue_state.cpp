#include "nv_prologue_state.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot {

void NVPrologueState::_bind_methods() {
	// 只在一次完整状态提交后发信号，界面收到后读取同一份权威快照。
	ADD_SIGNAL(MethodInfo("state_changed", PropertyInfo(Variant::DICTIONARY, "snapshot")));
	ClassDB::bind_method(D_METHOD("configure_content", "content"), &NVPrologueState::configure_content);
	ClassDB::bind_method(D_METHOD("configure_pet", "species", "name", "appearance", "location", "article", "forum"),
		&NVPrologueState::configure_pet);
	ClassDB::bind_method(D_METHOD("reset"), &NVPrologueState::reset);
	ClassDB::bind_method(D_METHOD("get_pet_profile"), &NVPrologueState::get_pet_profile);
	ClassDB::bind_method(D_METHOD("get_snapshot"), &NVPrologueState::get_snapshot);
	ClassDB::bind_method(D_METHOD("dispatch_action", "action", "payload"),
		&NVPrologueState::dispatch_action, Dictionary());
}

String NVPrologueState::stage_name() const {
	// 阶段由已发生的里程碑推导，允许玩家先逛论坛再回去看启事。
	if (complete) {
		return "done";
	}
	if (returning) {
		return "returning";
	}
	if (quest_ready) {
		return "quest_ready";
	}
	if (forum_post_opened) {
		return "forum";
	}
	if (article_opened) {
		return "article";
	}
	return started ? "chat" : "boot";
}

Dictionary NVPrologueState::get_section(const String &section_name) const {
	const Variant value = content.get(section_name, Dictionary());
	if (value.get_type() != Variant::DICTIONARY) {
		return Dictionary();
	}
	return Dictionary(value);
}

String NVPrologueState::get_content_text(const String &section_name, const String &field_name,
		const String &fallback) const {
	const Dictionary section = get_section(section_name);
	const Variant value = section.get(field_name, fallback);
	return String(value);
}

Array NVPrologueState::get_content_array(const String &section_name, const String &field_name) const {
	const Dictionary section = get_section(section_name);
	const Variant value = section.get(field_name, Array());
	if (value.get_type() != Variant::ARRAY) {
		return Array();
	}
	return Array(value);
}

bool NVPrologueState::contains_target(const String &value, const String &target) const {
	return !target.strip_edges().is_empty() && value.findn(target) >= 0;
}

bool NVPrologueState::contains_any(const String &value, const Array &targets) const {
	for (int64_t i = 0; i < targets.size(); ++i) {
		if (contains_target(value, String(targets[i]))) {
			return true;
		}
	}
	return false;
}

Dictionary NVPrologueState::find_task(const String &task_id) const {
	const Dictionary quest = get_section("quest");
	const Variant value = quest.get("tasks", Array());
	if (value.get_type() != Variant::ARRAY) {
		return Dictionary();
	}
	const Array tasks = value;
	for (int64_t i = 0; i < tasks.size(); ++i) {
		if (tasks[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary task = tasks[i];
		if (String(task.get("id", "")) == task_id) {
			return task;
		}
	}
	return Dictionary();
}

int NVPrologueState::find_clue_index(const String &clue_id) const {
	for (int64_t i = 0; i < clues.size(); ++i) {
		if (clues[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary clue = clues[i];
		if (String(clue.get("id", "")) == clue_id) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

bool NVPrologueState::has_page_clue(const String &page_id, const String &kind) const {
	for (int64_t i = 0; i < clues.size(); ++i) {
		if (clues[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary clue = clues[i];
		if (String(clue.get("page", "")) == page_id && String(clue.get("kind", "")) == kind) {
			return true;
		}
	}
	return false;
}

bool NVPrologueState::results_contain_id(const String &result_id) const {
	for (int64_t i = 0; i < current_results.size(); ++i) {
		if (current_results[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary result = current_results[i];
		if (String(result.get("id", "")) == result_id) {
			return true;
		}
	}
	return false;
}

bool NVPrologueState::is_broad_query(const String &query) const {
	const Array broad_queries = get_content_array("search", "broad_queries");
	const String normalized_query = query.strip_edges();
	for (int64_t i = 0; i < broad_queries.size(); ++i) {
		if (normalized_query == String(broad_queries[i])) {
			return true;
		}
	}
	return false;
}

bool NVPrologueState::is_rough_location(const String &location) const {
	return contains_any(location, get_content_array("matching", "rough_location_keywords"));
}

Dictionary NVPrologueState::make_result(const bool success, const String &code, const String &message,
		const Array &say) const {
	Dictionary result;
	result["ok"] = success;
	result["code"] = code;
	result["message"] = message;
	result["revision"] = revision;
	result["say"] = say;
	result["snapshot"] = get_snapshot();
	return result;
}

Dictionary NVPrologueState::make_search_result(const Dictionary &source, const String &kind) const {
	Dictionary result;
	result["id"] = source.get("id", "");
	result["kind"] = kind;
	result["title"] = source.get("title", "");
	result["url"] = source.get("url", "");
	result["description"] = source.get("description", source.get("desc", ""));
	result["image"] = source.get("image", source.get("img", ""));
	result["image_caption"] = source.get("image_caption", source.get("imgCap", ""));
	return result;
}

void NVPrologueState::append_post_collection_dialogue(Array &say) {
	// 话术由内容数据管理；里程碑保证每段只说一次并保持原型规定的先后顺序。
	const Dictionary dialogue = get_section("dialogue");
	if (has_page_clue("article", "text") && !notice_pic_said) {
		notice_pic_said = true;
		const String line = String(dialogue.get("notice_pic", ""));
		if (!line.is_empty()) {
			say.push_back(line);
		}
	}
	if (has_page_clue("article", "text") && has_page_clue("article", "image") && !task_tip_said) {
		task_tip_said = true;
		forum_prompt_said = true;
		const String task_tip = String(dialogue.get("task_tip", ""));
		const String forum_prompt = String(dialogue.get("forum", ""));
		if (!task_tip.is_empty()) {
			say.push_back(task_tip);
		}
		if (!forum_prompt.is_empty()) {
			say.push_back(forum_prompt);
		}
	}
}

void NVPrologueState::evaluate_quest_tasks() {
	// 答对过的子任务保持完成，线索卡仍可在白板自由区与任务之间移动。
	const Dictionary quest = get_section("quest");
	const Variant tasks_value = quest.get("tasks", Array());
	if (tasks_value.get_type() != Variant::ARRAY) {
		return;
	}
	const Array tasks = tasks_value;
	for (int64_t task_index = 0; task_index < tasks.size(); ++task_index) {
		if (tasks[task_index].get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary task = tasks[task_index];
		const String task_id = String(task.get("id", ""));
		const Array keywords = task.get("keywords", Array());
		bool matched = false;
		for (int64_t clue_index = 0; clue_index < clues.size(); ++clue_index) {
			if (clues[clue_index].get_type() != Variant::DICTIONARY) {
				continue;
			}
			const Dictionary clue = clues[clue_index];
			if (String(clue.get("task_id", "")) != task_id || String(clue.get("kind", "")) != "text") {
				continue;
			}
			if (contains_any(String(clue.get("text", "")), keywords)) {
				matched = true;
				break;
			}
		}
		if (task_id == "cat_look") {
			appearance_task_done = appearance_task_done || matched;
		} else if (task_id == "cat_place") {
			location_task_done = location_task_done || matched;
		}
	}

	quest_ready = appearance_task_done && location_task_done;
}

void NVPrologueState::commit_state_change() {
	++revision;
	emit_signal("state_changed", get_snapshot());
}

void NVPrologueState::configure_content(const Dictionary &p_content) {
	content = p_content;
	reset();
}

void NVPrologueState::configure_pet(const String &species, const String &name,
		const String &appearance, const String &location,
		const String &article, const String &forum) {
	// 兼容早期的简化调用方式，正式序章使用 configure_content 注入完整内容。
	Dictionary pet = get_section("pet");
	pet["species"] = species;
	pet["name"] = name;
	content["pet"] = pet;
	Dictionary matching = get_section("matching");
	if (!appearance.is_empty()) {
		Array appearance_keywords;
		appearance_keywords.push_back(appearance);
		matching["appearance_keywords"] = appearance_keywords;
	}
	if (!location.is_empty()) {
		Array location_keywords;
		location_keywords.push_back(location);
		matching["location_keywords"] = location_keywords;
	}
	content["matching"] = matching;
	Dictionary article_data = get_section("article");
	Dictionary forum_data = get_section("forum");
	if (!article.is_empty()) {
		article_data["id"] = article;
	}
	if (!forum.is_empty()) {
		forum_data["id"] = forum;
	}
	content["article"] = article_data;
	content["forum"] = forum_data;
	reset();
}

void NVPrologueState::reset() {
	// 重开序章仅清除运行进度，内容配置由当前 JSON 资源继续复用。
	const int64_t previous_revision = revision;
	clues.clear();
	current_results.clear();
	current_page = "home";
	active_result_id = "";
	last_search_scope = "web";
	last_search_query = "";
	// 初次注入内容时从零开始；运行中重置仍视为一次状态变化，保持修订号单调递增。
	revision = previous_revision > 0 ? previous_revision + 1 : 0;
	next_clue_id = 1;
	started = false;
	broad_search_answered = false;
	article_result_available = false;
	forum_result_available = false;
	article_opened = false;
	forum_post_opened = false;
	notice_seen_said = false;
	notice_pic_said = false;
	task_tip_said = false;
	forum_prompt_said = false;
	found_said = false;
	appearance_task_done = false;
	location_task_done = false;
	quest_ready = false;
	returning = false;
	complete = false;
	emit_signal("state_changed", get_snapshot());
}

Dictionary NVPrologueState::get_pet_profile() const {
	Dictionary profile = get_section("pet");
	profile["article_id"] = get_content_text("article", "id", "lost_pet");
	profile["forum_id"] = get_content_text("forum", "id", "pet_sighting");
	return profile;
}

Dictionary NVPrologueState::get_snapshot() const {
	Dictionary snapshot;
	snapshot["stage"] = stage_name();
	snapshot["revision"] = revision;
	snapshot["started"] = started;
	snapshot["page"] = current_page;
	snapshot["active_result_id"] = active_result_id;
	snapshot["search_scope"] = last_search_scope;
	snapshot["search_query"] = last_search_query;
	snapshot["search_results"] = current_results;
	snapshot["pet"] = get_pet_profile();
	snapshot["clues"] = clues;
	snapshot["article_opened"] = article_opened;
	snapshot["forum_post_opened"] = forum_post_opened;
	snapshot["quest_ready"] = quest_ready;
	snapshot["complete"] = complete;

	Dictionary milestones;
	milestones["broad_search_answered"] = broad_search_answered;
	milestones["article_opened"] = article_opened;
	milestones["article_text_collected"] = has_page_clue("article", "text");
	milestones["article_image_collected"] = has_page_clue("article", "image");
	milestones["forum_post_opened"] = forum_post_opened;
	milestones["appearance_task_done"] = appearance_task_done;
	milestones["location_task_done"] = location_task_done;
	milestones["returning"] = returning;
	milestones["complete"] = complete;
	snapshot["milestones"] = milestones;

	Dictionary quest;
	const Dictionary quest_definition = get_section("quest");
	quest["id"] = quest_definition.get("id", "q_cat");
	quest["title"] = quest_definition.get("title", "");
	quest["ready"] = quest_ready;
	quest["complete"] = complete;
	Array task_snapshots;
	const Variant task_value = quest_definition.get("tasks", Array());
	if (task_value.get_type() == Variant::ARRAY) {
		const Array task_definitions = task_value;
		for (int64_t task_index = 0; task_index < task_definitions.size(); ++task_index) {
			if (task_definitions[task_index].get_type() != Variant::DICTIONARY) {
				continue;
			}
			const Dictionary task_definition = task_definitions[task_index];
			const String task_id = String(task_definition.get("id", ""));
			Dictionary task;
			task["id"] = task_id;
			task["title"] = task_definition.get("title", "");
			task["done"] = task_id == "cat_look" ? appearance_task_done :
					(task_id == "cat_place" ? location_task_done : false);
			Array attached_clues;
			for (int64_t clue_index = 0; clue_index < clues.size(); ++clue_index) {
				if (clues[clue_index].get_type() != Variant::DICTIONARY) {
					continue;
				}
				const Dictionary clue = clues[clue_index];
				if (String(clue.get("task_id", "")) == task_id) {
					attached_clues.push_back(clue.get("id", ""));
				}
			}
			task["clue_ids"] = attached_clues;
			task_snapshots.push_back(task);
		}
	}
	quest["tasks"] = task_snapshots;
	snapshot["quest"] = quest;
	// 页面只读这份内容投影，不允许通过页面字段反向改写游戏进度。
	snapshot["content"] = content;
	return snapshot;
}

Dictionary NVPrologueState::dispatch_action(const String &action, const Dictionary &payload) {
	bool success = false;
	bool changed = false;
	String code = "invalid_action";
	String message = "当前操作不能推进序章。";
	Array say;

	if (action == "begin") {
		if (!started) {
			started = true;
			changed = true;
			success = true;
			code = "started";
			message = "序章开始。";
			const Dictionary dialogue = get_section("dialogue");
			const Variant intro_value = dialogue.get("intro", Array());
			if (intro_value.get_type() == Variant::ARRAY) {
				say = intro_value;
			}
		} else {
			success = true;
			code = "already_started";
			message = "序章已经开始。";
		}
	} else if (action == "search") {
		if (!started) {
			code = "not_started";
			message = "序章尚未开始。";
		} else {
			const String query = String(payload.get("query", "")).strip_edges();
			const String scope = String(payload.get("scope", "web"));
			if (query.is_empty() || (scope != "web" && scope != "forum")) {
				code = "invalid_search";
				message = "搜索内容不能为空。";
			} else {
				last_search_scope = scope;
				last_search_query = query;
				current_results.clear();
				active_result_id = "";
				article_result_available = false;
				forum_result_available = false;
				const Dictionary dialogue = get_section("dialogue");
				if (scope == "web") {
					current_page = "search_results";
					if (is_broad_query(query)) {
						const Dictionary search_data = get_section("search");
						const Variant broad_value = search_data.get("broad_results", Array());
						if (broad_value.get_type() == Variant::ARRAY) {
							current_results = broad_value;
						}
						if (!broad_search_answered) {
							broad_search_answered = true;
							const String noise = String(dialogue.get("noise", ""));
							if (!noise.is_empty()) {
								say.push_back(noise);
							}
						}
					} else if (contains_any(query, get_content_array("matching", "article_search_keywords")) ||
							contains_target(query, get_content_text("pet", "name"))) {
						article_result_available = true;
						current_results.push_back(make_search_result(get_section("article"), "article"));
					}
			} else {
					current_page = "forum_results";
				// 原型要求先亲手收下启事文字和照片，之后才出现论坛里可搜索的目击帖。
				if (has_page_clue("article", "text") && has_page_clue("article", "image") &&
						contains_any(query, get_content_array("matching", "forum_search_keywords"))) {
						forum_result_available = true;
						current_results.push_back(make_search_result(get_section("forum"), "forum"));
					}
				}
			success = true;
			changed = true;
			code = "search_completed";
			message = current_results.is_empty() ? "沒有找到相關結果。" : "搜索完成。";
		}
		}
	} else if (action == "open_article") {
		const String id = String(payload.get("id", ""));
		if (started && article_result_available && id == get_content_text("article", "id") &&
				results_contain_id(id)) {
			current_page = "article";
			article_opened = true;
			success = true;
			changed = true;
			code = "article_opened";
			message = "寻猫启事已打开。";
			if (!notice_seen_said) {
				notice_seen_said = true;
				const String notice_line = String(get_section("dialogue").get("notice_seen", ""));
				if (!notice_line.is_empty()) {
					say.push_back(notice_line);
				}
			}
		} else {
			code = "article_unavailable";
			message = "搜索结果里没有这张启事。";
		}
	} else if (action == "open_forum") {
		const String id = String(payload.get("id", ""));
		if (started && forum_result_available && id == get_content_text("forum", "id") &&
				results_contain_id(id)) {
			current_page = "forum_post";
			forum_post_opened = true;
			success = true;
			changed = true;
			code = "forum_opened";
			message = "目击帖已打开。";
		} else {
			code = "forum_unavailable";
			message = "搜索结果里没有这条目击信息。";
		}
	} else if (action == "open_result") {
		const String id = String(payload.get("id", ""));
		bool is_general_result = false;
		for (int64_t i = 0; i < current_results.size(); ++i) {
			if (current_results[i].get_type() != Variant::DICTIONARY) {
				continue;
			}
			const Dictionary item = current_results[i];
			if (String(item.get("id", "")) == id && String(item.get("kind", "")) == "web") {
				is_general_result = true;
				break;
			}
		}
		if (started && is_general_result) {
			active_result_id = id;
			current_page = "web_result";
			success = true;
			changed = true;
			code = "result_opened";
			message = "网页已打开。";
		} else {
			code = "result_unavailable";
			message = "搜索结果里没有这个网页。";
		}
	} else if (action == "browser_navigate") {
		const String target_page = String(payload.get("page", "home"));
		if (!started || (target_page != "home" && target_page != "search_results" && target_page != "forum_results")) {
			code = "invalid_page";
			message = "不能返回这个浏览器页面。";
		} else {
			current_page = target_page;
			active_result_id = "";
			success = true;
			changed = true;
			code = "browser_navigated";
			message = "浏览器页面已切换。";
		}
	} else if (action == "collect_clue") {
		const String kind = String(payload.get("kind", ""));
		const String page_id = String(payload.get("page", ""));
		const String selected_text = String(payload.get("text", "")).strip_edges();
		const bool valid_page = (page_id == "article" && article_opened) ||
				(page_id == "forum_post" && forum_post_opened);
		if (!valid_page || current_page != page_id) {
			code = "clue_unavailable";
			message = "请先打开线索所在的页面。";
		} else if (kind != "text" && kind != "image") {
			code = "invalid_clue_kind";
			message = "无法收录这种内容。";
		} else if (kind == "text" && (selected_text.is_empty() || selected_text.length() > 600)) {
			code = "invalid_clue_text";
			message = "这段文字不能作为摘录。";
		} else {
			bool already_collected = false;
			for (int64_t i = 0; i < clues.size(); ++i) {
				if (clues[i].get_type() != Variant::DICTIONARY) {
					continue;
				}
				const Dictionary clue = clues[i];
				if (String(clue.get("page", "")) == page_id && String(clue.get("kind", "")) == kind &&
						String(clue.get("text", "")) == selected_text) {
					already_collected = true;
					break;
				}
			}
			if (already_collected) {
				success = true;
				code = "clue_already_collected";
				message = "这条线索已经收进白板。";
			} else {
				Dictionary clue;
				clue["id"] = "clue_" + String::num_int64(next_clue_id++);
				clue["kind"] = kind;
				clue["page"] = page_id;
				clue["text"] = kind == "text" ? selected_text : "";
				clue["task_id"] = "";
				if (page_id == "article") {
					const Dictionary article = get_section("article");
					clue["source"] = article.get("short_source", "寻猫启事");
					clue["image"] = kind == "image" ? article.get("image", "") : "";
				} else {
					const Dictionary forum = get_section("forum");
					clue["source"] = forum.get("short_source", "暗涌论坛");
					clue["image"] = kind == "image" ? forum.get("image", "") : "";
				}
				clues.push_back(clue);
				append_post_collection_dialogue(say);
				success = true;
				changed = true;
				code = "clue_collected";
				message = "线索已收进白板。";
			}
		}
	} else if (action == "attach_clue") {
		const String clue_id = String(payload.get("clue_id", ""));
		const String task_id = String(payload.get("task_id", ""));
		const int clue_index = find_clue_index(clue_id);
		if (clue_index < 0 || find_task(task_id).is_empty()) {
			code = "invalid_attachment";
			message = "这张卡片或任务当前不存在。";
		} else {
			Dictionary clue = clues[clue_index];
			if (String(clue.get("task_id", "")) != task_id) {
				clue["task_id"] = task_id;
				clues[clue_index] = clue;
				changed = true;
			}
			const bool was_ready = quest_ready;
			evaluate_quest_tasks();
			if (!was_ready && quest_ready && !say.is_empty()) {
				// 只有白板两条子任务都正确划掉后，林越才开口认猫。
			}
			if (!was_ready && quest_ready && !found_said) {
				found_said = true;
				const String found_line = String(get_section("dialogue").get("found", ""));
				if (!found_line.is_empty()) {
					say.push_back(found_line);
				}
				changed = true;
			}
			success = true;
			code = quest_ready ? "quest_ready" : "clue_attached";
			message = quest_ready ? "白板上的线索已经对上。" : "线索已放到任务下。";
		}
	} else if (action == "detach_clue") {
		const String clue_id = String(payload.get("clue_id", ""));
		const int clue_index = find_clue_index(clue_id);
		if (clue_index < 0) {
			code = "unknown_clue";
			message = "白板上没有这张卡片。";
		} else {
			Dictionary clue = clues[clue_index];
			if (!String(clue.get("task_id", "")).is_empty()) {
				clue["task_id"] = "";
				clues[clue_index] = clue;
				changed = true;
			}
			success = true;
			code = "clue_detached";
			message = "线索已放回白板自由区。";
		}
	} else if (action == "chat") {
		const String text = String(payload.get("text", "")).strip_edges();
		const Dictionary dialogue = get_section("dialogue");
		if (text.is_empty()) {
			code = "empty_chat";
			message = "消息不能为空。";
		} else if (complete) {
			success = true;
			code = "prologue_complete";
			message = "年糕已经送回家了。";
			say.push_back(dialogue.get("fallback", "我先把这件事办完。"));
		} else if (contains_any(text, get_content_array("matching", "location_keywords")) ||
				is_rough_location(text) || contains_any(text, get_content_array("matching", "location_question_keywords"))) {
			// 玩家提前报地址会收到原型中的“还得先把猫样子认准”回应，而不是泛化兜底。
			Dictionary location_payload;
			location_payload["location"] = text;
			return dispatch_action("submit_location", location_payload);
		} else {
			success = true;
			code = "fallback";
			message = "林越还没摸到这个话题。";
			const String fallback = String(dialogue.get("fallback", "这个方向我还没摸着边。"));
			if (!fallback.is_empty()) {
				say.push_back(fallback);
			}
		}
	} else if (action == "submit_location") {
		const String location = String(payload.get("location", "")).strip_edges();
		if (location.is_empty()) {
			code = "empty_location";
			message = "位置不能为空。";
		} else if (complete) {
			success = true;
			code = "prologue_complete";
			message = "年糕已经送回家了。";
		} else if (returning) {
			success = true;
			code = "recovery_in_progress";
			message = "林越正在去接年糕的路上。";
		} else if (!quest_ready) {
			success = true;
			code = "quest_not_ready";
			message = "林越还不确定猫的样子。";
			const String line = String(get_section("dialogue").get("not_yet", ""));
			if (!line.is_empty()) {
				say.push_back(line);
			}
		} else if (contains_any(location, get_content_array("matching", "location_keywords"))) {
			returning = true;
			changed = true;
			success = true;
			code = "location_confirmed";
			message = "位置确认，林越出发去接年糕。";
			const String line = String(get_section("dialogue").get("go", ""));
			if (!line.is_empty()) {
				say.push_back(line);
			}
		} else if (is_rough_location(location)) {
			success = true;
			code = "rough_location";
			message = "还需要一个更具体的位置。";
			const String line = String(get_section("dialogue").get("rough_place", ""));
			if (!line.is_empty()) {
				say.push_back(line);
			}
		} else {
			success = true;
			code = "wrong_location";
			message = "这个地点和目击帖对不上。";
			const String line = String(get_section("dialogue").get("wrong_place", ""));
			if (!line.is_empty()) {
				say.push_back(line);
			}
		}
	} else if (action == "finish_recovery") {
		if (returning && !complete) {
			returning = false;
			complete = true;
			changed = true;
			success = true;
			code = "recovery_complete";
			message = "年糕已经平安回家。";
			const Dictionary dialogue = get_section("dialogue");
			const Variant done_value = dialogue.get("done", Array());
			if (done_value.get_type() == Variant::ARRAY) {
				const Array done_lines = done_value;
				const String done_board = String(dialogue.get("done_board", ""));
				for (int64_t i = 0; i < done_lines.size(); ++i) {
					if (i == done_lines.size() - 1 && !done_board.is_empty() && !clues.is_empty()) {
						say.push_back(done_board);
					}
					say.push_back(done_lines[i]);
				}
			}
			// 原型在主任务完成时收起子任务，并把挂接在线索卡上的内容拆回白板自由区。
			for (int64_t clue_index = 0; clue_index < clues.size(); ++clue_index) {
				if (clues[clue_index].get_type() != Variant::DICTIONARY) {
					continue;
				}
				Dictionary clue = clues[clue_index];
				clue["task_id"] = "";
				clues[clue_index] = clue;
			}
		} else if (complete) {
			success = true;
			code = "already_complete";
			message = "年糕已经平安回家。";
		} else {
			code = "recovery_not_started";
			message = "林越还没有出发。";
		}
	} else if (action == "snapshot") {
		success = true;
		code = "snapshot";
		message = "已返回当前进度。";
	} else {
		code = "unknown_action";
		message = "未知的序章操作。";
	}

	if (changed) {
		commit_state_change();
	}
	return make_result(success, code, message, say);
}

} // namespace godot
