#include "nv_prologue_state.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cmath>

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

Dictionary NVPrologueState::find_forum_post(const String &post_id) const {
	const Array posts = get_content_array("forum", "posts");
	for (int64_t i = 0; i < posts.size(); ++i) {
		const Dictionary post = posts[i];
		if (String(post["id"]) == post_id) {
			return post;
		}
	}
	return Dictionary();
}

Dictionary NVPrologueState::project_forum_post(const Dictionary &post) const {
	// 内容字段是独立投影，页面不能通过修改快照污染后续搜索和帖子正文。
	Dictionary projected = post.duplicate(true);
	Array visible_floors;
	const Array floors = post.get("floors", Array());
	for (int64_t i = 0; i < floors.size(); ++i) {
		const Dictionary floor = floors[i];
		if (!bool(floor.get("requires_complete", false)) || complete) {
			visible_floors.push_back(floor.duplicate(true));
		}
	}
	projected["floors"] = visible_floors;
	return projected;
}

bool NVPrologueState::forum_post_matches(const Dictionary &post, const String &query) const {
	if (query.is_empty()) {
		return true;
	}
	String text = String(post["title"]) + " " + String(post["author"]) + " " +
		String(post["body"]) + " " + String(post["category"]);
	const Dictionary projected = project_forum_post(post);
	const Array floors = projected["floors"];
	for (int64_t i = 0; i < floors.size(); ++i) {
		const Dictionary floor = floors[i];
		// 原型的跑题楼层仍可阅读，但不得把整栋楼带入不相关的搜索结果。
		if (!bool(floor.get("no_search", false))) {
			text += " " + String(floor["author"]) + " " + String(floor["text"]);
		}
	}
	// 站名不是帖子主题；搜“暗涌/论坛/bbs”只用于发现网站，不把版规等整楼捎上。
	text = text.to_lower();
	const Array excluded_terms = get_content_array("forum", "search_excluded_terms");
	for (int64_t i = 0; i < excluded_terms.size(); ++i) {
		text = text.replace(String(excluded_terms[i]).to_lower(), " ");
	}
	return text.contains(query.to_lower());
}

Array NVPrologueState::get_forum_results() const {
	Array results;
	const Array posts = get_content_array("forum", "posts");
	for (int64_t i = 0; i < posts.size(); ++i) {
		const Dictionary post = posts[i];
		if ((forum_category == U"全部" || String(post["category"]) == forum_category) &&
				forum_post_matches(post, forum_query)) {
			results.push_back(make_search_result(post, "forum_post"));
		}
	}
	return results;
}

Dictionary NVPrologueState::get_browser_route() const {
	Dictionary route;
	route["page"] = current_page;
	route["web_query"] = last_search_query;
	route["web_results"] = current_results.duplicate(true);
	route["result_id"] = active_result_id;
	route["post_id"] = active_post_id;
	route["forum_query"] = forum_query;
	route["forum_category"] = forum_category;
	return route;
}

void NVPrologueState::remember_browser_route() {
	const Dictionary route = get_browser_route();
	if (browser_history_index >= 0) {
		const Dictionary previous = browser_history[browser_history_index];
		// 查询结果由数据和进度产生；识别重复导航只比较路由语义字段。
		bool same_route = true;
		const char *keys[] = { "page", "web_query", "result_id", "post_id", "forum_query", "forum_category" };
		for (const char *key : keys) {
			if (String(previous[key]) != String(route[key])) {
				same_route = false;
				break;
			}
		}
		if (same_route) {
			browser_history[browser_history_index] = route;
			return;
		}
	}
	browser_history.resize(browser_history_index + 1);
	browser_history.push_back(route);
	browser_history_index = browser_history.size() - 1;
}

void NVPrologueState::restore_browser_route(const Dictionary &route) {
	current_page = route["page"];
	last_search_query = route["web_query"];
	current_results = Array(route["web_results"]).duplicate(true);
	active_result_id = route["result_id"];
	active_post_id = route["post_id"];
	forum_query = route["forum_query"];
	forum_category = route["forum_category"];
}

Dictionary NVPrologueState::get_browser_page() const {
	Dictionary page;
	const Dictionary search = get_section("search");
	const Dictionary forum = get_section("forum");
	const Dictionary article = get_section("article");
	if (current_page == "forum" || current_page == "forum_post") {
		const Dictionary post = find_forum_post(active_post_id);
		page["url"] = current_page == "forum" ? String(forum["url"]) : String(post["url"]);
		page["title"] = current_page == "forum" ? String(forum["title"]) : String(post["title"]) + " - " + String(forum["site"]);
		page["resource"] = forum["page_resource"];
	} else if (current_page == "article") {
		page["url"] = article["url"];
		page["title"] = article["title"];
		page["resource"] = article["page_resource"];
	} else {
		page["url"] = current_page == "home" ? String(search["url"]) : String(search["url"]) + "?q=" + last_search_query.uri_encode();
		page["title"] = current_page == "home" ? String(search["title"]) : last_search_query + String(" - ") + String(search["title"]);
		page["resource"] = search["page_resource"];
		if (current_page == "web_result") {
			for (int64_t i = 0; i < current_results.size(); ++i) {
				const Dictionary result = current_results[i];
				if (String(result["id"]) == active_result_id) {
					page["url"] = result["url"];
					page["title"] = result["title"];
					break;
				}
			}
		}
	}
	return page;
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
	// 帖子列表的表现数据来自内容文件；C++ 只生成统一的只读条目。
	result["author"] = source.get("author", "");
	result["category"] = source.get("category", "");
	result["time"] = source.get("time", "");
	result["pinned"] = source.get("pinned", false);
	result["replies"] = source.get("replies", 0);
	result["views"] = source.get("views", 0);
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
	// 规则层持有独立内容副本，防止调用方在配置后改写帖子或搜索条件。
	content = p_content.duplicate(true);
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
	if (!article.is_empty()) {
		article_data["id"] = article;
	}
	if (!forum.is_empty()) {
		pet["forum_post_id"] = forum;
	}
	content["pet"] = pet;
	content["article"] = article_data;
	reset();
}

void NVPrologueState::reset() {
	// 重开序章仅清除运行进度，内容配置由当前 JSON 资源继续复用。
	const int64_t previous_revision = revision;
	clues.clear();
	board_view.clear();
	board_view["pan_x"] = 0.0;
	board_view["pan_y"] = 0.0;
	board_view["zoom"] = 1.0;
	board_view["rotation"] = 0.0;
	board_view["auto_fit"] = true;
	current_results.clear();
	current_page = "home";
	active_result_id = "";
	last_search_query = "";
	forum_query = "";
	forum_category = U"全部";
	active_post_id = "";
	browser_history.clear();
	browser_history_index = -1;
	remember_browser_route();
	// 初次注入内容时从零开始；运行中重置仍视为一次状态变化，保持修订号单调递增。
	revision = previous_revision > 0 ? previous_revision + 1 : 0;
	next_clue_id = 1;
	started = false;
	broad_search_answered = false;
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
	profile["forum_id"] = get_content_text("pet", "forum_post_id");
	return profile;
}

Dictionary NVPrologueState::get_snapshot() const {
	Dictionary snapshot;
	snapshot["stage"] = stage_name();
	snapshot["revision"] = revision;
	snapshot["started"] = started;
	snapshot["page"] = current_page;
	snapshot["active_result_id"] = active_result_id;
	snapshot["search_scope"] = "web";
	snapshot["search_query"] = last_search_query;
	snapshot["search_results"] = current_results.duplicate(true);
	snapshot["forum_query"] = forum_query;
	snapshot["forum_category"] = forum_category;
	snapshot["forum_results"] = get_forum_results();
	snapshot["active_post_id"] = active_post_id;
	const Dictionary active_post = find_forum_post(active_post_id);
	snapshot["active_post"] = active_post.is_empty() ? Dictionary() : project_forum_post(active_post);
	const Dictionary browser_page = get_browser_page();
	snapshot["browser_url"] = browser_page["url"];
	snapshot["browser_title"] = browser_page["title"];
	snapshot["browser_resource"] = browser_page["resource"];
	snapshot["can_go_back"] = browser_history_index > 0;
	snapshot["can_go_forward"] = browser_history_index + 1 < browser_history.size();
	snapshot["pet"] = get_pet_profile();
	// 快照是只读投影的独立副本，界面持有旧快照或编辑字典均不能改到权威状态。
	snapshot["clues"] = clues.duplicate(true);
	snapshot["board_view"] = board_view.duplicate(true);
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
	snapshot["content"] = content.duplicate(true);
	return snapshot;
}

bool NVPrologueState::apply_clue_layout(Dictionary &clue, const Dictionary &payload, bool &changed) const {
	// Godot Dictionary 默认共享底层数据；先在独立副本中验证，整次请求合法后才替换。
	// 否则先收到合法坐标、后收到非法缩放时，会出现“返回失败却已经改了坐标”的半提交。
	Dictionary candidate = clue.duplicate(true);
	bool has_layout = false;
	// 坐标必须成对提交，避免卡片在一次状态变更后丢失一个轴的位置。
	const bool has_x = payload.has("x");
	const bool has_y = payload.has("y");
	if (has_x != has_y) {
		return false;
	}
	if (has_x) {
		const Variant x_value = payload.get("x", Variant());
		const Variant y_value = payload.get("y", Variant());
		if ((x_value.get_type() != Variant::FLOAT && x_value.get_type() != Variant::INT) ||
				(y_value.get_type() != Variant::FLOAT && y_value.get_type() != Variant::INT)) {
			return false;
		}
		const double x = x_value.get_type() == Variant::FLOAT ? static_cast<double>(x_value) : static_cast<int64_t>(x_value);
		const double y = y_value.get_type() == Variant::FLOAT ? static_cast<double>(y_value) : static_cast<int64_t>(y_value);
		if (!std::isfinite(x) || !std::isfinite(y)) {
			return false;
		}
		candidate["x"] = x;
		candidate["y"] = y;
		has_layout = true;
	}

	for (const String &key : { String("scale"), String("rotation") }) {
		if (!payload.has(key)) {
			continue;
		}
		const Variant value = payload.get(key, Variant());
		if (value.get_type() != Variant::FLOAT && value.get_type() != Variant::INT) {
			return false;
		}
		const double number = value.get_type() == Variant::FLOAT ? static_cast<double>(value) : static_cast<int64_t>(value);
		if (!std::isfinite(number) ||
				(key == "scale" && (number < 0.4 || number > 2.8)) ||
				(key == "rotation" && (number < -180.0 || number > 180.0))) {
			return false;
		}
		candidate[key] = number;
		has_layout = true;
	}
	if (payload.has("note")) {
		const Variant note_value = payload.get("note", Variant());
		if (note_value.get_type() != Variant::STRING) {
			return false;
		}
		candidate["note"] = String(note_value);
		has_layout = true;
	}
	if (!has_layout) return false;
	clue = candidate;
	changed = true;
	return true;
}

Dictionary NVPrologueState::dispatch_action(const String &action, const Dictionary &payload) {
	bool success = false;
	bool changed = false;
	bool navigated = false;
	String code = "invalid_action";
	String message = U"当前操作不能推进序章。";
	Array say;

	if (action == "begin") {
		if (!started) {
			started = true;
			changed = true;
			success = true;
			code = "started";
			message = U"序章开始。";
			const Dictionary dialogue = get_section("dialogue");
			const Variant intro_value = dialogue.get("intro", Array());
			if (intro_value.get_type() == Variant::ARRAY) {
				say = intro_value;
			}
		} else {
			success = true;
			code = "already_started";
			message = U"序章已经开始。";
		}
	} else if (action == "search") {
		if (!started) {
			code = "not_started";
			message = U"序章尚未开始。";
		} else {
			const String query = String(payload.get("query", "")).strip_edges();
			const String scope = String(payload.get("scope", "web"));
			if (query.is_empty() || scope != "web") {
				code = "invalid_search";
				message = U"全网搜索需要非空关键词；站内搜索请在论坛页面提交。";
			} else {
				last_search_query = query;
				current_results.clear();
				active_result_id = "";
				active_post_id = "";
				current_page = "search_results";
				const Dictionary dialogue = get_section("dialogue");
				if (is_broad_query(query)) {
					current_results = get_content_array("search", "broad_results").duplicate(true);
					if (!broad_search_answered) {
						broad_search_answered = true;
						const String noise = String(dialogue.get("noise", ""));
						if (!noise.is_empty()) say.push_back(noise);
					}
				} else if (contains_any(query, get_content_array("matching", "article_search_keywords")) ||
						contains_target(query, get_content_text("pet", "name"))) {
					current_results.push_back(make_search_result(get_section("article"), "article"));
				}
				// 网站入口参与全网搜索；forum_only 帖子只参与站内搜索，不以收录进度解锁。
				if (contains_any(query, get_content_array("forum", "search_keywords"))) {
					current_results.push_back(make_search_result(get_section("forum"), "forum_site"));
				}
				const Array posts = get_content_array("forum", "posts");
				for (int64_t i = 0; i < posts.size(); ++i) {
					const Dictionary post = posts[i];
					if (!bool(post.get("forum_only", false)) && forum_post_matches(post, query)) {
						current_results.push_back(make_search_result(post, "forum_post"));
					}
				}
				success = changed = navigated = true;
				code = "search_completed";
				message = current_results.is_empty() ? U"没有找到相关结果。" : U"搜索完成。";
			}
		}
	} else if (action == "forum_search") {
		const String category = String(payload.get("category", forum_category));
		if (!started || (current_page != "forum" && current_page != "forum_post")) {
			code = "forum_not_open";
			message = U"站内搜索需要由论坛页面发起。";
		} else if (!get_content_array("forum", "categories").has(category)) {
			code = "invalid_forum_category";
			message = U"论坛中不存在这个分类。";
		} else {
			forum_query = String(payload.get("query", forum_query)).strip_edges();
			forum_category = category;
			current_page = "forum";
			active_post_id = "";
			success = changed = navigated = true;
			code = "forum_searched";
			message = U"站内帖子列表已更新。";
		}
	} else if (action == "open_article") {
		const String id = String(payload.get("id", ""));
		if (started && id == get_content_text("article", "id") &&
				results_contain_id(id)) {
			current_page = "article";
			active_post_id = "";
			article_opened = true;
			success = true;
			changed = true;
			navigated = true;
			code = "article_opened";
			message = U"寻猫启事已打开。";
			if (!notice_seen_said) {
				notice_seen_said = true;
				const String notice_line = String(get_section("dialogue").get("notice_seen", ""));
				if (!notice_line.is_empty()) {
					say.push_back(notice_line);
				}
			}
		} else {
			code = "article_unavailable";
			message = U"搜索结果里没有这张启事。";
		}
	} else if (action == "open_forum") {
		const String id = String(payload.get("id", ""));
		if (started && id == get_content_text("forum", "id")) {
			current_page = "forum";
			active_result_id = "";
			forum_query = "";
			forum_category = U"全部";
			active_post_id = "";
			success = changed = navigated = true;
			code = "forum_opened";
			message = U"暗涌论坛首页已打开。";
		} else {
			code = "forum_unavailable";
			message = U"没有这个论坛站点。";
		}
	} else if (action == "open_thread") {
		const String id = String(payload.get("id", ""));
		if (started && !find_forum_post(id).is_empty()) {
			current_page = "forum_post";
			active_result_id = "";
			active_post_id = id;
			// 逛其他帖子不会冒充读过找猫目击帖，也不额外触发找猫对白。
			if (id == get_content_text("pet", "forum_post_id")) forum_post_opened = true;
			success = changed = navigated = true;
			code = "thread_opened";
			message = U"论坛帖子已打开。";
		} else {
			code = "thread_unavailable";
			message = U"论坛中不存在这个帖子。";
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
			active_post_id = "";
			success = true;
			changed = true;
			navigated = true;
			code = "result_opened";
			message = U"网页已打开。";
		} else {
			code = "result_unavailable";
			message = U"搜索结果里没有这个网页。";
		}
	} else if (action == "browser_navigate") {
		const String target_page = String(payload.get("page", "home"));
		if (!started || (target_page != "home" && target_page != "search_results" && target_page != "forum") ||
				(target_page == "search_results" && last_search_query.is_empty())) {
			code = "invalid_page";
			message = U"不能返回这个浏览器页面。";
		} else {
			current_page = target_page;
			active_result_id = "";
			active_post_id = "";
			success = changed = navigated = true;
			code = "browser_navigated";
			message = U"浏览器页面已切换。";
		}
	} else if (action == "browser_back" || action == "browser_forward") {
		const int64_t target = browser_history_index + (action == "browser_back" ? -1 : 1);
		if (!started || target < 0 || target >= browser_history.size()) {
			code = "history_unavailable";
			message = U"没有可切换的浏览历史。";
		} else {
			browser_history_index = target;
			restore_browser_route(browser_history[target]);
			success = changed = true;
			code = "browser_navigated";
			message = U"浏览历史已恢复。";
		}
	} else if (action == "browser_address") {
		String address = String(payload.get("address", "")).strip_edges();
		if (!address.is_empty() && address.find("://") < 0 && address.contains(".")) address = "https://" + address;
		const String forum_url = get_content_text("forum", "url");
		const String search_url = get_content_text("search", "url");
		Dictionary destination;
		if (address == forum_url || address + "/" == forum_url) {
			destination["id"] = get_content_text("forum", "id");
			return dispatch_action("open_forum", destination);
		} else if (address.begins_with(forum_url + String("thread/"))) {
			destination["id"] = address.substr(forum_url.length() + 7).trim_suffix("/");
			return dispatch_action("open_thread", destination);
		} else if (address == search_url || address + "/" == search_url) {
			destination["page"] = "home";
			return dispatch_action("browser_navigate", destination);
		} else if (address.begins_with(search_url + String("?q="))) {
			destination["query"] = address.substr(search_url.length() + 3).uri_decode();
			return dispatch_action("search", destination);
		} else if (!address.is_empty() && address.find("://") < 0) {
			// 地址栏中的普通词交给夜航搜索，域名则必须精确命中本地站点，绝不访问真实外网。
			destination["query"] = address;
			return dispatch_action("search", destination);
		} else if (started && address == get_content_text("article", "url")) {
			current_page = "article";
			active_post_id = "";
			article_opened = true;
			if (!notice_seen_said) {
				notice_seen_said = true;
				say.push_back(get_content_text("dialogue", "notice_seen"));
			}
			success = changed = navigated = true;
			code = "browser_navigated";
			message = U"寻猫启事已打开。";
		} else {
			// 普通网页也能在其结果仍存在时按显示地址重新打开，未知域名则明确报错。
			for (int64_t i = 0; i < current_results.size(); ++i) {
				const Dictionary item = current_results[i];
				if (String(item["kind"]) == "web" && String(item["url"]) == address) {
					destination["id"] = item["id"];
					return dispatch_action("open_result", destination);
				}
			}
			code = "unknown_address";
			message = U"找不到这个网址。";
		}
	} else if (action == "collect_clue") {
		const String kind = String(payload.get("kind", ""));
		const String page_id = String(payload.get("page", ""));
		const String selected_text = String(payload.get("text", "")).strip_edges();
		const bool valid_page = (page_id == "article" && article_opened) ||
				(page_id == "forum_post" && !find_forum_post(active_post_id).is_empty());
		if (!valid_page || current_page != page_id) {
			code = "clue_unavailable";
			message = U"请先打开线索所在的页面。";
		} else if (kind != "text" && kind != "image") {
			code = "invalid_clue_kind";
			message = U"无法收录这种内容。";
		} else if (kind == "text" && (selected_text.is_empty() || selected_text.length() > 600)) {
			code = "invalid_clue_text";
			message = U"这段文字不能作为摘录。";
		} else {
			bool already_collected = false;
			for (int64_t i = 0; i < clues.size(); ++i) {
				if (clues[i].get_type() != Variant::DICTIONARY) {
					continue;
				}
				const Dictionary clue = clues[i];
				if (String(clue.get("page", "")) == page_id && String(clue.get("kind", "")) == kind &&
						(page_id != "forum_post" || String(clue.get("post_id", "")) == active_post_id) &&
						String(clue.get("text", "")) == selected_text) {
					already_collected = true;
					break;
				}
			}
			if (already_collected) {
				success = true;
				code = "clue_already_collected";
				message = U"这条线索已经收进白板。";
			} else {
				Dictionary clue;
				clue["id"] = "clue_" + String::num_int64(next_clue_id++);
				clue["kind"] = kind;
				clue["page"] = page_id;
				clue["post_id"] = page_id == "forum_post" ? active_post_id : "";
				clue["text"] = kind == "text" ? selected_text : "";
				clue["task_id"] = "";
				clue["x"] = Variant();
				clue["y"] = Variant();
				clue["scale"] = 1.0;
				clue["rotation"] = 0.0;
				clue["note"] = "";
				if (page_id == "article") {
					const Dictionary article = get_section("article");
					clue["source"] = article.get("short_source", U"寻猫启事");
					clue["image"] = kind == "image" ? article.get("image", "") : "";
					clue["caption"] = kind == "image" ? article.get("image_caption", "") : "";
				} else {
					const Dictionary forum = find_forum_post(active_post_id);
					clue["source"] = forum.get("short_source", U"暗涌论坛");
					clue["image"] = kind == "image" ? forum.get("image", "") : "";
					clue["caption"] = kind == "image" ? forum.get("image_caption", "") : "";
				}
				clues.push_back(clue);
				append_post_collection_dialogue(say);
				success = true;
				changed = true;
				code = "clue_collected";
				message = U"线索已收进白板。";
			}
		}
	} else if (action == "attach_clue") {
		const String clue_id = String(payload.get("clue_id", ""));
		const String task_id = String(payload.get("task_id", ""));
		const int clue_index = find_clue_index(clue_id);
		if (clue_index < 0 || find_task(task_id).is_empty()) {
			code = "invalid_attachment";
			message = U"这张卡片或任务当前不存在。";
		} else {
			Dictionary clue = clues[clue_index];
			bool layout_changed = false;
			if ((payload.has("scale") || payload.has("rotation")) &&
					!apply_clue_layout(clue, payload, layout_changed)) {
				code = "invalid_clue_layout";
				message = U"线索卡片变换数据无效。";
				return make_result(false, code, message, say);
			}
			if (String(clue.get("task_id", "")) != task_id) {
				clue["task_id"] = task_id;
				clue["x"] = Variant();
				clue["y"] = Variant();
				changed = true;
			}
			if (layout_changed || changed) {
				clues[clue_index] = clue;
			}
			// 同一任务内重新松开也要求 UI 按任务树布局重新吸附卡片。
			changed = true;
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
			message = quest_ready ? U"白板上的线索已经对上。" : U"线索已放到任务下。";
		}
	} else if (action == "detach_clue") {
		const String clue_id = String(payload.get("clue_id", ""));
		const int clue_index = find_clue_index(clue_id);
		if (clue_index < 0) {
			code = "unknown_clue";
			message = U"白板上没有这张卡片。";
		} else {
			Dictionary clue = clues[clue_index];
			bool layout_changed = false;
			if ((payload.has("x") || payload.has("y") || payload.has("scale") ||
					payload.has("rotation") || payload.has("note")) &&
					!apply_clue_layout(clue, payload, layout_changed)) {
				code = "invalid_clue_layout";
				message = U"线索卡片布局数据无效。";
				return make_result(false, code, message, say);
			}
			if (!String(clue.get("task_id", "")).is_empty()) {
				clue["task_id"] = "";
				changed = true;
			}
			changed = changed || layout_changed;
			if (layout_changed || changed) {
				clues[clue_index] = clue;
			}
			success = true;
			code = "clue_detached";
			message = U"线索已放回白板自由区。";
		}
	} else if (action == "update_clue") {
		const String clue_id = String(payload.get("clue_id", ""));
		const int clue_index = find_clue_index(clue_id);
		if (clue_index < 0) {
			code = "unknown_clue";
			message = U"白板上没有这张卡片。";
		} else {
			Dictionary clue = clues[clue_index];
			if (!apply_clue_layout(clue, payload, changed)) {
				code = "invalid_clue_layout";
				message = U"线索卡片布局数据无效或没有可保存的字段。";
			} else {
				clues[clue_index] = clue;
				success = true;
				code = "clue_updated";
				message = U"白板线索卡已更新。";
			}
		}
	} else if (action == "remove_clue") {
		const String clue_id = String(payload.get("clue_id", ""));
		const int clue_index = find_clue_index(clue_id);
		if (clue_index < 0) {
			code = "unknown_clue";
			message = U"白板上没有这张卡片。";
		} else {
			clues.remove_at(clue_index);
			evaluate_quest_tasks();
			success = true;
			changed = true;
			code = "clue_removed";
			message = U"线索卡已从白板撕下。";
		}
	} else if (action == "tidy_board") {
		// 整理只重置摆放与单卡变换，保留线索内容、批注、任务归属和已经答对的里程碑。
		for (int64_t index = 0; index < clues.size(); ++index) {
			Dictionary clue = Dictionary(clues[index]).duplicate(true);
			clue["x"] = Variant();
			clue["y"] = Variant();
			clue["scale"] = 1.0;
			clue["rotation"] = 0.0;
			clues[index] = clue;
		}
		board_view["auto_fit"] = true;
		success = true;
		changed = true;
		code = "board_tidied";
		message = U"白板卡片已重新整理。";
	} else if (action == "set_board_view") {
		const Variant pan_x_value = payload.get("pan_x", Variant());
		const Variant pan_y_value = payload.get("pan_y", Variant());
		const Variant zoom_value = payload.get("zoom", Variant());
		const Variant rotation_value = payload.get("rotation", Variant());
		const Variant auto_fit_value = payload.get("auto_fit", Variant());
		const bool numeric_values =
				(pan_x_value.get_type() == Variant::FLOAT || pan_x_value.get_type() == Variant::INT) &&
				(pan_y_value.get_type() == Variant::FLOAT || pan_y_value.get_type() == Variant::INT) &&
				(zoom_value.get_type() == Variant::FLOAT || zoom_value.get_type() == Variant::INT) &&
				(rotation_value.get_type() == Variant::FLOAT || rotation_value.get_type() == Variant::INT);
		const bool auto_fit_valid = auto_fit_value.get_type() == Variant::BOOL;
		if (!numeric_values || !auto_fit_valid) {
			code = "invalid_board_view";
			message = U"白板镜头状态字段不完整。";
		} else {
			const double pan_x = pan_x_value.get_type() == Variant::FLOAT ? static_cast<double>(pan_x_value) : static_cast<int64_t>(pan_x_value);
			const double pan_y = pan_y_value.get_type() == Variant::FLOAT ? static_cast<double>(pan_y_value) : static_cast<int64_t>(pan_y_value);
			const double zoom = zoom_value.get_type() == Variant::FLOAT ? static_cast<double>(zoom_value) : static_cast<int64_t>(zoom_value);
			const double rotation = rotation_value.get_type() == Variant::FLOAT ? static_cast<double>(rotation_value) : static_cast<int64_t>(rotation_value);
			if (!std::isfinite(pan_x) || !std::isfinite(pan_y) || !std::isfinite(zoom) || zoom <= 0.0 || !std::isfinite(rotation)) {
				code = "invalid_board_view";
				message = U"白板镜头数值超出有效范围。";
			} else {
				board_view["pan_x"] = pan_x;
				board_view["pan_y"] = pan_y;
				board_view["zoom"] = zoom;
				board_view["rotation"] = rotation;
				board_view["auto_fit"] = bool(auto_fit_value);
				success = true;
				changed = true;
				code = "board_view_updated";
				message = U"白板视角已保存。";
			}
		}
	} else if (action == "chat") {
		const String text = String(payload.get("text", "")).strip_edges();
		const Dictionary dialogue = get_section("dialogue");
		if (text.is_empty()) {
			code = "empty_chat";
			message = U"消息不能为空。";
		} else if (complete) {
			success = true;
			code = "prologue_complete";
			message = U"年糕已经送回家了。";
			say.push_back(dialogue.get("fallback", U"我先把这件事办完。"));
		} else if (contains_any(text, get_content_array("matching", "location_keywords")) ||
				is_rough_location(text) || contains_any(text, get_content_array("matching", "location_question_keywords"))) {
			// 玩家提前报地址会收到原型中的“还得先把猫样子认准”回应，而不是泛化兜底。
			Dictionary location_payload;
			location_payload["location"] = text;
			return dispatch_action("submit_location", location_payload);
		} else {
			success = true;
			code = "fallback";
			message = U"林越还没摸到这个话题。";
			const String fallback = String(dialogue.get("fallback", U"这个方向我还没摸着边。"));
			if (!fallback.is_empty()) {
				say.push_back(fallback);
			}
		}
	} else if (action == "submit_location") {
		const String location = String(payload.get("location", "")).strip_edges();
		if (location.is_empty()) {
			code = "empty_location";
			message = U"位置不能为空。";
		} else if (complete) {
			success = true;
			code = "prologue_complete";
			message = U"年糕已经送回家了。";
		} else if (returning) {
			success = true;
			code = "recovery_in_progress";
			message = U"林越正在去接年糕的路上。";
		} else if (!quest_ready) {
			success = true;
			code = "quest_not_ready";
			message = U"林越还不确定猫的样子。";
			const String line = String(get_section("dialogue").get("not_yet", ""));
			if (!line.is_empty()) {
				say.push_back(line);
			}
		} else if (contains_any(location, get_content_array("matching", "location_keywords"))) {
			returning = true;
			changed = true;
			success = true;
			code = "location_confirmed";
			message = U"位置确认，林越出发去接年糕。";
			const String line = String(get_section("dialogue").get("go", ""));
			if (!line.is_empty()) {
				say.push_back(line);
			}
		} else if (is_rough_location(location)) {
			success = true;
			code = "rough_location";
			message = U"还需要一个更具体的位置。";
			const String line = String(get_section("dialogue").get("rough_place", ""));
			if (!line.is_empty()) {
				say.push_back(line);
			}
		} else {
			success = true;
			code = "wrong_location";
			message = U"这个地点和目击帖对不上。";
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
			message = U"年糕已经平安回家。";
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
				// 已在自由区摆好的卡片继续留在原位，只散开原先挂在子任务下的卡片。
				if (!String(clue.get("task_id", "")).is_empty()) {
					clue["task_id"] = "";
					clue["x"] = Variant();
					clue["y"] = Variant();
					clues[clue_index] = clue;
				}
			}
		} else if (complete) {
			success = true;
			code = "already_complete";
			message = U"年糕已经平安回家。";
		} else {
			code = "recovery_not_started";
			message = U"林越还没有出发。";
		}
	} else if (action == "snapshot") {
		success = true;
		code = "snapshot";
		message = U"已返回当前进度。";
	} else {
		code = "unknown_action";
		message = U"未知的序章操作。";
	}

	if (navigated) {
		remember_browser_route();
	}
	if (changed) {
		commit_state_change();
	}
	return make_result(success, code, message, say);
}

} // namespace godot
