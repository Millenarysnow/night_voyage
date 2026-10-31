#nullable enable

using Godot;
using Godot.Collections;
using System;

/// <summary>
/// 序章 C++ 状态机的离线集成验收。
///
/// 这个测试验证网页/聊天提交的动作顺序、白板状态持久化和白板 PackedScene 装配。
/// CEF 页面换版或 Godot 控件调整时，剧情规则与白板数据契约仍有稳定回归保护。
/// </summary>
public partial class PrologueStateIntegrationTest : Node
{
	private Node? _state;
	private BoardView? _boardView;
	private Control? _clueLayer;
	private bool _failed;
	private int _frameCount;

	/// <summary>在场景启动时装配白板场景并执行完整的找猫成功路径和失败分支。</summary>
	public override void _Ready()
	{
		if (!ClassDB.CanInstantiate("NVPrologueState"))
		{
			Fail("NVPrologueState 没有注册。");
			return;
		}
		_state = ClassDB.Instantiate("NVPrologueState").AsGodotObject() as Node;
		if (_state == null)
		{
			Fail("无法实例化 NVPrologueState。");
			return;
		}
		AddChild(_state);
		Variant parsed = Json.ParseString(FileAccess.GetFileAsString("res://content/prologue_cat.json"));
		Expect(parsed.VariantType == Variant.Type.Dictionary, "序章 JSON 可读取。");
		_state.Call("configure_content", parsed.AsGodotDictionary());

		Dictionary initialSnapshot = _state.Call("get_snapshot").AsGodotDictionary();
		PackedScene? boardScene = GD.Load<PackedScene>("res://content/prologue/scenes/board_window_content.tscn");
		if (boardScene?.Instantiate() is BoardView boardView)
		{
			_boardView = boardView;
			_boardView.SetAnchorsAndOffsetsPreset(Control.LayoutPreset.TopLeft);
			_boardView.Size = new Vector2(920.0f, 580.0f);
			AddChild(_boardView);
			_boardView.Configure(initialSnapshot);
			_clueLayer = _boardView.GetNode<Control>("BoardCanvas/BoardWorld/ClueLayer");
			Expect(_boardView.GetNodeOrNull<Button>("Toolbar/Buttons/FitAll") != null,
				"白板场景加载出工具栏和自由画布控件。");
		}
		else
		{
			Fail("无法实例化白板场景。");
		}
		long initialRevision = Get(initialSnapshot, "revision", 0).AsInt64();
		Dictionary initialBoardView = initialSnapshot["board_view"].AsGodotDictionary();
		Expect(Get(initialBoardView, "auto_fit", false).AsBool(), "新序章的白板默认进入全览模式。");
		Dictionary viewResult = Act("set_board_view", new Dictionary
		{
			["pan_x"] = -125.0,
			["pan_y"] = 48.0,
			["zoom"] = 1.35,
			["rotation"] = 15.0,
			["auto_fit"] = false,
		});
		Dictionary savedView = viewResult["snapshot"].AsGodotDictionary()["board_view"].AsGodotDictionary();
		Expect(Get(savedView, "pan_x", 0.0).AsDouble() == -125.0 &&
			Get(savedView, "zoom", 0.0).AsDouble() == 1.35 &&
			!Get(savedView, "auto_fit", true).AsBool(), "白板平移、缩放、旋转和全览状态写入快照。");
		long viewRevision = Get(viewResult, "revision", 0).AsInt64();
		Dictionary invalidView = Act("set_board_view", new Dictionary
		{
			["pan_x"] = 0.0,
			["pan_y"] = 0.0,
			["zoom"] = 0.0,
			["rotation"] = 0.0,
			["auto_fit"] = false,
		});
		Expect(Get(invalidView, "code", "").AsString() == "invalid_board_view" &&
			Get(_state.Call("get_snapshot").AsGodotDictionary(), "revision", 0).AsInt64() == viewRevision,
			"无效白板缩放会被拒绝且不推进修订号。");
		initialRevision = Get(_state.Call("get_snapshot").AsGodotDictionary(), "revision", 0).AsInt64();

		Dictionary rejected = Act("collect_clue", new Dictionary
		{
			["page"] = "article",
			["kind"] = "text",
			["text"] = "这张启事还没有打开。",
		});
		Expect(Get(rejected, "code", "").AsString() == "clue_unavailable", "页面尚未打开时不能收录线索。");
		Expect(Get(_state.Call("get_snapshot").AsGodotDictionary(), "revision", 0).AsInt64() == initialRevision,
			"失败的收录操作不改变状态修订号。");

		Dictionary result = Act("begin", new Dictionary());
		Expect(Get(result, "code", "").AsString() == "started", "开场对白操作成功。");
		Expect(result["say"].AsGodotArray().Count == 5, "开场对白包含五句原型台词。");
		long startedRevision = Get(result["snapshot"].AsGodotDictionary(), "revision", 0).AsInt64();
		rejected = Act("open_article", new Dictionary { ["id"] = "lostcat" });
		Expect(Get(rejected, "code", "").AsString() == "article_unavailable", "没有搜索启事时不能直接打开页面。");
		Expect(Get(_state.Call("get_snapshot").AsGodotDictionary(), "revision", 0).AsInt64() == startedRevision,
			"失败的页面打开操作不推进修订号。");

		result = Act("search", new Dictionary { ["scope"] = "web", ["query"] = "猫" });
		Expect(Get(result, "code", "").AsString() == "search_completed", "搜索大词可以完成但不命中启事。");
		Expect(result["snapshot"].AsGodotDictionary()["search_results"].AsGodotArray().Count == 3, "大词搜索只显示无关结果。");
		Expect(result["say"].AsGodotArray().Count == 1, "第一次搜猫会触发林越的纠正对白。");
		result = Act("search", new Dictionary { ["scope"] = "web", ["query"] = "猫" });
		Expect(result["say"].AsGodotArray().Count == 0, "重复搜猫不会重复触发纠正对白。");

		Act("search", new Dictionary { ["scope"] = "web", ["query"] = "年糕" });
		result = Act("open_article", new Dictionary { ["id"] = "lostcat" });
		Expect(Get(result, "code", "").AsString() == "article_opened", "搜年糕后可以打开寻猫启事。");
		rejected = Act("open_forum", new Dictionary { ["id"] = "pet_sighting" });
		Expect(Get(rejected, "code", "").AsString() == "forum_unavailable", "没有在论坛搜索到帖子时不能直接打开目击帖。");

		Act("collect_clue", new Dictionary { ["page"] = "article", ["kind"] = "text", ["text"] = "特征：半岁小橘猫，四爪白，脖子上系一根旧红绳，绳上一个小铜铃铛。" });
		Act("collect_clue", new Dictionary { ["page"] = "article", ["kind"] = "image" });
		Act("search", new Dictionary { ["scope"] = "forum", ["query"] = "猫" });
		Act("open_forum", new Dictionary { ["id"] = "pet_sighting" });
		Act("collect_clue", new Dictionary { ["page"] = "forum_post", ["kind"] = "text", ["text"] = "今天下午两点多，我路过南山路口，老粮站门口那个花坛上蹲着这么一只。" });

		Dictionary snapshot = _state.Call("get_snapshot").AsGodotDictionary();
		_boardView?.Configure(snapshot);
		Expect(_clueLayer?.GetChildCount() == 3, "三张收录线索都生成独立场景卡片。");
		string articleTextId = FindClue(snapshot, "article", "text");
		string articleImageId = FindClue(snapshot, "article", "image");
		string forumTextId = FindClue(snapshot, "forum_post", "text");
		result = Act("update_clue", new Dictionary
		{
			["clue_id"] = forumTextId,
			["x"] = 620.0,
			["y"] = 410.0,
			["scale"] = 1.25,
			["rotation"] = 30.0,
		});
		long looseLayoutRevision = Get(result, "revision", 0).AsInt64();
		Dictionary invalidScale = Act("update_clue", new Dictionary
		{
			["clue_id"] = forumTextId,
			["scale"] = 20.0,
		});
		Expect(Get(invalidScale, "code", "").AsString() == "invalid_clue_layout" &&
			Get(_state.Call("get_snapshot").AsGodotDictionary(), "revision", 0).AsInt64() == looseLayoutRevision,
			"超出场景控制范围的线索缩放会被拒绝。");
		result = Act("update_clue", new Dictionary
		{
			["clue_id"] = articleImageId,
			["x"] = 486.0,
			["y"] = 224.0,
			["scale"] = 1.4,
			["rotation"] = -15.0,
			["note"] = "照片里的花色和启事吻合",
		});
		Dictionary editedImage = FindClueById(result["snapshot"].AsGodotDictionary(), articleImageId);
		Expect(Get(result, "code", "").AsString() == "clue_updated" &&
			Get(editedImage, "x", 0.0).AsDouble() == 486.0 &&
			Get(editedImage, "scale", 0.0).AsDouble() == 1.4 &&
			Get(editedImage, "note", "").AsString() == "照片里的花色和启事吻合",
			"线索卡坐标、缩放、旋转和批注都保存在权威快照。");
		long clueRevision = Get(result, "revision", 0).AsInt64();
		Dictionary invalidClue = Act("update_clue", new Dictionary
		{
			["clue_id"] = articleImageId,
			["x"] = 10.0,
		});
		Expect(Get(invalidClue, "code", "").AsString() == "invalid_clue_layout" &&
			Get(_state.Call("get_snapshot").AsGodotDictionary(), "revision", 0).AsInt64() == clueRevision,
			"缺少 Y 坐标的卡片布局会被拒绝且不推进修订号。");
		invalidClue = Act("update_clue", new Dictionary
		{
			["clue_id"] = articleImageId,
			["x"] = 900.0,
			["y"] = 800.0,
			["scale"] = -2.0,
		});
		Expect(!Get(invalidClue, "ok", true).AsBool() &&
			Get(invalidClue, "revision", 0).AsInt64() == clueRevision &&
			Get(FindClueById(invalidClue["snapshot"].AsGodotDictionary(), articleImageId), "x", 0.0).AsDouble() == 486.0,
			"合法坐标和非法缩放混合提交时整次拒绝，不产生部分更新。");
		Dictionary exposedSnapshot = _state.Call("get_snapshot").AsGodotDictionary();
		FindClueById(exposedSnapshot, articleImageId)["note"] = "外部不应写入的内容";
		exposedSnapshot["board_view"].AsGodotDictionary()["pan_x"] = 99999.0;
		Expect(Get(FindClueById(_state.Call("get_snapshot").AsGodotDictionary(), articleImageId), "note", "").AsString() ==
			"照片里的花色和启事吻合" &&
			Get(_state.Call("get_snapshot").AsGodotDictionary()["board_view"].AsGodotDictionary(), "pan_x", 0.0).AsDouble() == -125.0,
			"修改返回快照中的线索或镜头字典不会改动 C++ 权威状态。");
		result = Act("remove_clue", new Dictionary { ["clue_id"] = articleImageId });
		_boardView?.Configure(result["snapshot"].AsGodotDictionary());
		Expect(Get(result, "code", "").AsString() == "clue_removed" &&
			!ContainsClueId(result["snapshot"].AsGodotDictionary(), articleImageId) &&
			_clueLayer?.GetChildCount() == 2, "移除线索后快照和场景都不再包含对应卡片。");

		result = Act("submit_location", new Dictionary { ["location"] = "老粮站" });
		Expect(Get(result, "code", "").AsString() == "quest_not_ready", "白板任务未准备好时提前报地址不推进流程。");
		Act("attach_clue", new Dictionary { ["clue_id"] = articleTextId, ["task_id"] = "cat_look" });
		result = Act("attach_clue", new Dictionary { ["clue_id"] = forumTextId, ["task_id"] = "cat_look" });
		Expect(Get(result, "code", "").AsString() == "clue_attached", "错误的白板挂接不会完成位置子任务。");
		Dictionary attachedClue = FindClueById(result["snapshot"].AsGodotDictionary(), forumTextId);
		Expect(Get(attachedClue, "x", 0.0).VariantType is not (Variant.Type.Float or Variant.Type.Int) &&
			Get(attachedClue, "scale", 0.0).AsDouble() == 1.25 &&
			Get(attachedClue, "rotation", 0.0).AsDouble() == 30.0,
			"挂接卡片后清除自由坐标并保留其变换属性。");
		result = Act("attach_clue", new Dictionary { ["clue_id"] = forumTextId, ["task_id"] = "cat_place" });
		Expect(Get(result, "code", "").AsString() == "quest_ready", "两条白板任务都对上后进入可报告阶段。");
		result = Act("detach_clue", new Dictionary
		{
			["clue_id"] = forumTextId,
			["x"] = 512.0,
			["y"] = 348.0,
			["scale"] = 1.25,
			["rotation"] = 30.0,
			["note"] = "目击地点仍要核对",
		});
		Dictionary detachedClue = FindClueById(result["snapshot"].AsGodotDictionary(), forumTextId);
		Expect(Get(detachedClue, "task_id", "not-empty").AsString().Length == 0 &&
			Get(detachedClue, "y", 0.0).AsDouble() == 348.0 &&
			Get(detachedClue, "rotation", 0.0).AsDouble() == 30.0,
			"拖回自由区时同时保存卡片位置和变换。");
		Expect(Get(_state.Call("get_snapshot").AsGodotDictionary(), "quest_ready", false).AsBool(),
			"任务已经对上后把卡片拖回自由区不会撤销已完成子任务。");
		long detachRevision = Get(result, "revision", 0).AsInt64();
		result = Act("detach_clue", new Dictionary
		{
			["clue_id"] = forumTextId,
			["x"] = 560.0,
			["y"] = 390.0,
		});
		Expect(Get(result, "revision", 0).AsInt64() > detachRevision &&
			Get(FindClueById(result["snapshot"].AsGodotDictionary(), forumTextId), "x", 0.0).AsDouble() == 560.0,
			"重复落在自由区时坐标变化仍会提交状态修订。");

		result = Act("chat", new Dictionary { ["text"] = "我知道了" });
		Expect(Get(result, "code", "").AsString() == "fallback", "白板准备好后普通聊天仍不会被误判为地点。");
		result = Act("chat", new Dictionary { ["text"] = "老粮站" });
		Expect(Get(result, "code", "").AsString() == "rough_location", "只报粮站会被要求说细。");
		result = Act("submit_location", new Dictionary { ["location"] = "河边" });
		Expect(Get(result, "code", "").AsString() == "wrong_location", "报错地点会得到纠正。");
		result = Act("chat", new Dictionary { ["text"] = "老粮站后头的花坛" });
		Expect(Get(result, "code", "").AsString() == "location_confirmed", "报到花坛后林越出发找猫。");
		result = Act("submit_location", new Dictionary { ["location"] = "花坛" });
		Expect(Get(result, "code", "").AsString() == "recovery_in_progress", "找回途中重复报告位置不会重复启动流程。");
		result = Act("finish_recovery", new Dictionary());
		Expect(Get(result, "code", "").AsString() == "recovery_complete", "交接完成后序章结束。");
		Expect(Get(result["snapshot"].AsGodotDictionary(), "complete", false).AsBool(), "完成快照标记 complete。");
		Dictionary completed = result["snapshot"].AsGodotDictionary();
		Expect(Get(FindClueById(completed, articleTextId), "task_id", "missing").AsString().Length == 0 &&
			Get(FindClueById(completed, forumTextId), "x", 0.0).AsDouble() == 560.0,
			"任务完成只拆回挂接卡片，原本自由摆放的卡片保留位置。");
		_boardView?.Configure(completed);
		bool allTasksHidden = true;
		if (_boardView != null)
			foreach (Control task in _boardView.GetNode<Control>("BoardCanvas/BoardWorld/TaskLayer").GetChildren())
				if (task.Visible) allTasksHidden = false;
		Expect(allTasksHidden && _clueLayer?.GetChildCount() == 2,
			"完成后场景收起子任务并保留所有剩余线索卡。");
		long completedRevision = Get(_state.Call("get_snapshot").AsGodotDictionary(), "revision", 0).AsInt64();
		_state.Call("reset");
		long resetRevision = Get(_state.Call("get_snapshot").AsGodotDictionary(), "revision", 0).AsInt64();
		Expect(resetRevision > completedRevision, "重开序章时修订号仍保持单调递增。");

		GD.Print(_failed ? "[FAIL] 序章状态机集成验收失败。" : "[PASS] 序章状态机集成验收通过。");
	}

	/// <summary>执行一项动作并确保状态机始终返回可序列化结果。</summary>
	private Dictionary Act(string action, Dictionary payload)
	{
		if (_state == null) return new Dictionary();
		Variant variant = _state.Call("dispatch_action", action, payload);
		Expect(variant.VariantType == Variant.Type.Dictionary, action + " 返回 Dictionary。");
		return variant.AsGodotDictionary();
	}

	/// <summary>在快照中按页面与类型找到线索卡 ID，模拟白板拖放使用的稳定句柄。</summary>
	private static string FindClue(Dictionary snapshot, string page, string kind)
	{
		foreach (Variant value in snapshot["clues"].AsGodotArray())
		{
			Dictionary clue = value.AsGodotDictionary();
			if (Get(clue, "page", "").AsString() == page &&
					Get(clue, "kind", "").AsString() == kind)
			{
				return Get(clue, "id", "").AsString();
			}
		}
		return "";
	}

	/// <summary>在状态快照中按稳定 ID 取得完整线索记录，验证用户编辑数据的持久化结果。</summary>
	private static Dictionary FindClueById(Dictionary snapshot, string clueId)
	{
		foreach (Variant value in snapshot["clues"].AsGodotArray())
		{
			Dictionary clue = value.AsGodotDictionary();
			if (Get(clue, "id", "").AsString() == clueId) return clue;
		}
		return new Dictionary();
	}

	/// <summary>确认移除动作已经从当前线索数组中清除指定稳定 ID。</summary>
	private static bool ContainsClueId(Dictionary snapshot, string clueId)
	{
		foreach (Variant value in snapshot["clues"].AsGodotArray())
		{
			Dictionary clue = value.AsGodotDictionary();
			if (Get(clue, "id", "").AsString() == clueId) return true;
		}
		return false;
	}

	/// <summary>输出单项断言，继续执行剩余动作以一次性收集完整诊断。</summary>
	private void Expect(bool condition, string message)
	{
		if (condition)
		{
			GD.Print("[PASS] " + message);
		}
		else
		{
			Fail(message);
		}
	}

	/// <summary>记录失败原因，最终由场景统一决定退出码。</summary>
	private void Fail(string message)
	{
		_failed = true;
		GD.PushError("[FAIL] " + message);
	}

	/// <summary>读取无类型 Dictionary 字段，避免在测试中引入业务层快捷扩展。</summary>
	private static Variant Get(Dictionary dictionary, string key, Variant fallback)
	{
		return dictionary.ContainsKey(key) ? dictionary[key] : fallback;
	}

	/// <summary>防止集成测试因场景生命周期异常而无限运行。</summary>
	public override void _Process(double delta)
	{
		_frameCount++;
		if (_frameCount > 30)
		{
			GetTree().Quit(_failed ? 1 : 0);
		}
	}
}
