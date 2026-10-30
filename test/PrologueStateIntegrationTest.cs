#nullable enable

using Godot;
using Godot.Collections;
using System;

/// <summary>
/// 序章 C++ 状态机的离线集成验收。
///
/// 这个测试不创建桌面 UI，只验证网页/聊天可能提交的动作是否按原型顺序
/// 得到正确结果。这样 CEF 页面换版或 Godot 控件重做时，核心规则仍有稳定回归保护。
/// </summary>
public partial class PrologueStateIntegrationTest : Node
{
	private Node? _state;
	private bool _failed;
	private int _frameCount;

	/// <summary>在场景启动时执行完整的找猫成功路径和三个失败分支。</summary>
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
		long initialRevision = Get(initialSnapshot, "revision", 0).AsInt64();
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
		string articleTextId = FindClue(snapshot, "article", "text");
		string forumTextId = FindClue(snapshot, "forum_post", "text");
		result = Act("submit_location", new Dictionary { ["location"] = "老粮站" });
		Expect(Get(result, "code", "").AsString() == "quest_not_ready", "白板任务未准备好时提前报地址不推进流程。");
		Act("attach_clue", new Dictionary { ["clue_id"] = articleTextId, ["task_id"] = "cat_look" });
		result = Act("attach_clue", new Dictionary { ["clue_id"] = forumTextId, ["task_id"] = "cat_look" });
		Expect(Get(result, "code", "").AsString() == "clue_attached", "错误的白板挂接不会完成位置子任务。");
		result = Act("attach_clue", new Dictionary { ["clue_id"] = forumTextId, ["task_id"] = "cat_place" });
		Expect(Get(result, "code", "").AsString() == "quest_ready", "两条白板任务都对上后进入可报告阶段。");
		Act("detach_clue", new Dictionary { ["clue_id"] = forumTextId });
		Expect(Get(_state.Call("get_snapshot").AsGodotDictionary(), "quest_ready", false).AsBool(),
			"任务已经对上后把卡片拖回自由区不会撤销已完成子任务。");

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
