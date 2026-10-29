#include "nv_prologue_state.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot {

void NVPrologueState::_bind_methods() {
	// 状态变化信号只携带完整快照，界面无需自己拼接多份状态字段。
	ADD_SIGNAL(MethodInfo("state_changed", PropertyInfo(Variant::DICTIONARY, "snapshot")));
	ClassDB::bind_method(D_METHOD("reset"), &NVPrologueState::reset);
	ClassDB::bind_method(D_METHOD("configure_pet", "species", "name", "appearance", "location", "article", "forum"),
			&NVPrologueState::configure_pet);
	ClassDB::bind_method(D_METHOD("get_pet_profile"), &NVPrologueState::get_pet_profile);
	ClassDB::bind_method(D_METHOD("get_snapshot"), &NVPrologueState::get_snapshot);
	ClassDB::bind_method(D_METHOD("dispatch_action", "action", "payload"), &NVPrologueState::dispatch_action,
			Dictionary());
}

String NVPrologueState::stage_name() const {
	switch (stage) {
		case STAGE_CHAT:
			return "chat";
		case STAGE_SEARCH:
			return "search";
		case STAGE_ARTICLE:
			return "article";
		case STAGE_FORUM:
			return "forum";
		case STAGE_LOCATION:
			return "location";
		case STAGE_DONE:
			return "done";
		case STAGE_BOOT:
		default:
			return "boot";
	}
}

Dictionary NVPrologueState::make_result(bool success, const String &code, const String &message) const {
	Dictionary result;
	result["ok"] = success;
	result["code"] = code;
	result["message"] = message;
	result["snapshot"] = get_snapshot();
	return result;
}

bool NVPrologueState::contains_target(const String &value, const String &target) const {
	return !target.is_empty() && value.findn(target) >= 0;
}

bool NVPrologueState::has_clue(const String &clue_type) const {
	for (int i = 0; i < collected_clues.size(); ++i) {
		if (String(collected_clues[i]) == clue_type) {
			return true;
		}
	}
	return false;
}

void NVPrologueState::update_after_clue() {
	// 序章要求一条文字线索和一张图片线索都由玩家主动收下，
	// 只打开页面或看见图片都不会改变流程状态。
	if (stage == STAGE_ARTICLE && has_clue("text") && has_clue("image")) {
		stage = STAGE_FORUM;
	}
}

void NVPrologueState::reset() {
	stage = STAGE_BOOT;
	revision = 0;
	collected_clues.clear();
	emit_signal("state_changed", get_snapshot());
}

void NVPrologueState::configure_pet(const String &species, const String &name,
		const String &appearance, const String &location,
		const String &article, const String &forum) {
	// 内容配置和运行时进度分离，策划更换宠物时不需要复制一套状态机。
	pet_species = species;
	pet_name = name;
	appearance_clue = appearance;
	expected_location = location;
	article_id = article.is_empty() ? "lost_pet" : article;
	forum_id = forum.is_empty() ? "pet_sighting" : forum;
	reset();
}

Dictionary NVPrologueState::get_pet_profile() const {
	Dictionary profile;
	profile["species"] = pet_species;
	profile["name"] = pet_name;
	profile["appearance"] = appearance_clue;
	profile["location"] = expected_location;
	profile["article"] = article_id;
	profile["forum"] = forum_id;
	return profile;
}

Dictionary NVPrologueState::get_snapshot() const {
	Dictionary snapshot;
	snapshot["stage"] = stage_name();
	snapshot["stage_id"] = static_cast<int>(stage);
	snapshot["revision"] = revision;
	snapshot["pet"] = get_pet_profile();
	snapshot["clues"] = collected_clues;
	snapshot["has_text_clue"] = has_clue("text");
	snapshot["has_image_clue"] = has_clue("image");
	snapshot["complete"] = stage == STAGE_DONE;
	return snapshot;
}

Dictionary NVPrologueState::dispatch_action(const String &action, const Dictionary &payload) {
	// 所有非法操作都返回明确错误而不改变状态；网页端不能通过伪造阶段
	// 或重复请求直接跳过序章。
	bool success = false;
	String code = "invalid_action";
	String message = "当前操作不能推进序章。";

	if (action == "begin") {
		if (stage == STAGE_BOOT) {
			stage = STAGE_CHAT;
			success = true;
			code = "started";
			message = "序章开始。";
		} else {
			success = true;
			code = "already_started";
			message = "序章已经开始。";
		}
	} else if (action == "search") {
		const String query = String(payload.get("query", ""));
		if (stage == STAGE_CHAT || stage == STAGE_SEARCH) {
			stage = STAGE_SEARCH;
			if (contains_target(query, pet_name) || contains_target(query, pet_species)) {
				success = true;
				code = "search_hit";
				message = "找到了相关页面。";
			} else {
				code = "search_miss";
				message = "没有找到相关页面。";
			}
		} else {
			code = "search_unavailable";
			message = "现在还不能进行搜索。";
		}
	} else if (action == "open_article") {
		const String id = String(payload.get("id", ""));
		if (stage == STAGE_SEARCH && id == article_id) {
			stage = STAGE_ARTICLE;
			success = true;
			code = "article_opened";
			message = "寻宠启事已打开。";
		} else {
			code = "article_unavailable";
			message = "这不是当前序章要找的启事。";
		}
	} else if (action == "collect_clue") {
		const String clue_type = String(payload.get("kind", ""));
		if (stage == STAGE_ARTICLE && (clue_type == "text" || clue_type == "image")) {
			if (!has_clue(clue_type)) {
				collected_clues.push_back(clue_type);
				revision += 1;
			}
			update_after_clue();
			success = true;
			code = "clue_collected";
			message = "线索已收进白板。";
		} else {
			code = "clue_unavailable";
			message = "当前页面没有可收集的线索。";
		}
	} else if (action == "open_forum") {
		const String id = String(payload.get("id", ""));
		if (stage == STAGE_FORUM && id == forum_id) {
			stage = STAGE_LOCATION;
			success = true;
			code = "forum_opened";
			message = "目击信息已打开。";
		} else {
			code = "forum_unavailable";
			message = "现在还不能打开这条目击信息。";
		}
	} else if (action == "submit_location") {
		const String location = String(payload.get("location", ""));
		if (stage == STAGE_LOCATION && contains_target(location, expected_location)) {
			stage = STAGE_DONE;
			success = true;
			code = "location_confirmed";
			message = "位置确认，序章完成。";
		} else {
			code = "location_mismatch";
			message = "位置还不够准确。";
		}
	} else if (action == "snapshot") {
		success = true;
		code = "snapshot";
		message = "已返回当前进度。";
	} else {
		code = "unknown_action";
		message = "未知的序章操作。";
	}

	if (success && action != "snapshot" && action != "collect_clue") {
		revision += 1;
	}
	if (success && action == "collect_clue" && has_clue("text") && has_clue("image")) {
		// 收齐第二条线索后，阶段变化也需要一个新的修订号。
		revision += 1;
	}
	if (success) {
		emit_signal("state_changed", get_snapshot());
	}
	return make_result(success, code, message);
}

} // namespace godot

