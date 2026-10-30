#nullable enable

using Godot;
using Godot.Collections;
using System;

/// <summary>
/// 离线验证序章 CEF 页面、Godot IPC 和 C++ 规则之间的完整往返。
///
/// 测试实际驱动网页搜索、启事文字选区、照片预览、论坛搜索与帖子选区，
/// 确认网页只负责呈现和提交操作，所有进度仍由 C++ 状态机裁决。
/// </summary>
public partial class PrologueBrowserIntegrationTest : Control
{
	private const string BrowserExtensionPath = "res://addons/godot_cef/godot_cef.gdextension";
	private const string NativeExtensionPath = "res://addons/night_voyageExt/bin/windows/night_voyageExt.windows.template_debug.x86_64.dll";
	private const string BrowserPagePath = "res://content/prologue_browser.html";
	private const string ContentPath = "res://content/prologue_cat.json";

	private Node? _state;
	private Control? _browser;
	private bool _failed;
	private bool _completed;
	private bool _waitingForState;
	private int _step;
	private int _frameCount;

	/// <summary>创建真实 CefTexture 与 C++ 状态机，并在网页 ready 后驱动原型交互。</summary>
	public override void _Ready()
	{
		SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
		if (!EnsureExtension("NVPrologueState", NativeExtensionPath))
		{
			Fail("NVPrologueState GDExtension 未加载。");
			return;
		}
		if (!EnsureExtension("CefTexture", BrowserExtensionPath))
		{
			Fail("Godot CEF GDExtension 未加载。");
			return;
		}

		_state = ClassDB.Instantiate("NVPrologueState").AsGodotObject() as Node;
		if (_state == null)
		{
			Fail("无法创建 NVPrologueState。");
			return;
		}
		AddChild(_state);
		Variant parsed = Json.ParseString(FileAccess.GetFileAsString(ContentPath));
		if (parsed.VariantType != Variant.Type.Dictionary)
		{
			Fail("序章内容 JSON 不可读取。");
			return;
		}
		_state.Call("configure_content", parsed.AsGodotDictionary());
		_state.Call("dispatch_action", "begin", new Dictionary());

		_browser = ClassDB.Instantiate("CefTexture").AsGodotObject() as Control;
		if (_browser == null)
		{
			Fail("无法创建 CefTexture。");
			return;
		}
		_browser.Set("enable_accelerated_osr", false);
		_browser.Set("url", BrowserPagePath);
		_browser.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
		_browser.Connect("load_finished", Callable.From<string, int>(OnLoadFinished));
		_browser.Connect("load_error", Callable.From<string, int, string>(OnLoadError));
		_browser.Connect("ipc_message", Callable.From<string>(OnBrowserMessage));
		AddChild(_browser);
	}

	/// <summary>用有限帧数等待整条 CEF 操作链，避免失败时测试进程一直挂住。</summary>
	public override void _Process(double delta)
	{
		if (_completed) return;
		_frameCount++;
		if (_frameCount > 1200)
		{
			Fail("等待 CEF 网页操作链超时，当前测试步骤为 " + _step);
			CompleteTest();
		}
	}

	/// <summary>检查扩展类注册状态，未注册时按资源描述显式加载一次。</summary>
	private bool EnsureExtension(string className, string extensionPath)
	{
		if (ClassDB.CanInstantiate(className)) return true;
		string absolutePath = ProjectSettings.GlobalizePath(extensionPath);
		GDExtensionManager.LoadStatus status = GDExtensionManager.LoadExtension(absolutePath);
		return (status == GDExtensionManager.LoadStatus.Ok ||
				status == GDExtensionManager.LoadStatus.AlreadyLoaded) && ClassDB.CanInstantiate(className);
	}

	/// <summary>CEF 网页完成加载用于日志诊断，IPC ready 会启动页面驱动的交互。</summary>
	private void OnLoadFinished(string url, int statusCode)
	{
		Expect(statusCode >= 200 && statusCode < 400, "CEF 本地序章网页加载成功。");
		GD.Print($"[INFO] CEF 页面加载完成，HTTP {statusCode}，{url}");
	}

	/// <summary>保留 CEF 的加载错误码与说明，让测试超时前能指出具体页面故障。</summary>
	private void OnLoadError(string url, int errorCode, string description)
	{
		Fail($"CEF 页面加载失败，错误码 {errorCode}，{description}");
	}

	/// <summary>按协议接收 ready、网页状态确认与玩家操作消息。</summary>
	private void OnBrowserMessage(string rawMessage)
	{
		if (rawMessage == "prologue:ready")
		{
			GD.Print("[PASS] 网页脚本已通过 CEF IPC 向 Godot 报到。");
			SendSnapshot();
			_waitingForState = true;
			return;
		}

		Variant parsed = Json.ParseString(rawMessage);
		if (parsed.VariantType != Variant.Type.Dictionary)
		{
			return;
		}
		Dictionary message = parsed.AsGodotDictionary();
		string type = Get(message, "type", "").AsString();
		if (type == "browser:state_applied")
		{
			HandleBrowserStateApplied(message);
			return;
		}
		if (type != "action")
		{
			return;
		}

		string action = Get(message, "action", "").AsString();
		Dictionary payload = Get(message, "payload", new Dictionary()).AsGodotDictionary();
		if (action == "search")
		{
			HandleSearch(payload);
		}
		else if (action == "open_article")
		{
			DispatchAndReturn(action, payload, "article_opened", 5);
		}
		else if (action == "open_forum")
		{
			DispatchAndReturn(action, payload, "forum_opened", 9);
		}
		else if (action == "open_result")
		{
			DispatchAndReturn(action, payload, "result_opened", 2);
		}
		else if (action == "browser_navigate")
		{
			DispatchAndReturn(action, payload, "browser_navigated", 3);
		}
		else if (action == "collect_clue")
		{
			HandleClueCollection(payload);
		}
		else if (action == "preview_image")
		{
			HandleImagePreview(payload);
		}
	}

	/// <summary>每次状态快照完成页面重绘且图片载入结束后，推进下一项网页交互。</summary>
	private void HandleBrowserStateApplied(Dictionary message)
	{
		if (!_waitingForState) return;
		_waitingForState = false;
		GD.Print("[PASS] 网页脚本已应用 Godot 返回的状态快照。");

		string page = Get(message, "page", "").AsString();
		switch (_step)
		{
			case 0:
				Eval("document.getElementById('query').value = '猫'; document.getElementById('searchForm').requestSubmit();");
				break;
			case 1:
				Expect(page == "search_results", "泛搜结果仍停留在搜索结果页面。");
				Eval("document.querySelector('[data-action=\"open_result\"]').click();");
				break;
			case 2:
				Expect(page == "web_result", "泛搜结果可以打开普通网页。");
				Eval("document.querySelector('[data-page=\"search_results\"]').click();");
				break;
			case 3:
				Expect(page == "search_results", "普通网页可以返回泛搜结果。");
				Eval("document.getElementById('query').value = '年糕'; document.getElementById('searchForm').requestSubmit();");
				break;
			case 4:
				Eval("document.querySelector('[data-action=\"open_article\"]').click();");
				break;
			case 5:
				Expect(page == "article", "年糕搜索结果打开了寻猫启事页面。");
				Expect(Get(message, "article_image_width", 0).AsInt32() > 0, "启事照片从项目内本地资源成功显示。");
				Eval("(()=>{const p=document.querySelector('.paper p'); const r=document.createRange(); r.selectNodeContents(p); const s=window.getSelection(); s.removeAllRanges(); s.addRange(r); document.dispatchEvent(new MouseEvent('mouseup',{bubbles:true})); setTimeout(()=>document.getElementById('collectSelection').click(),0);})();");
				break;
			case 6:
				Expect(Get(message, "article_text_collected", false).AsBool(), "玩家选中的启事文字已进入白板快照。");
				Eval("document.querySelector('.page img.photo').click();");
				break;
			case 7:
				Expect(Get(message, "article_image_collected", false).AsBool(), "玩家确认收录后启事照片已进入白板快照。");
				Eval("document.querySelector('[data-scope=\"forum\"]').click(); document.getElementById('query').value='猫'; document.getElementById('searchForm').requestSubmit();");
				break;
			case 8:
				Expect(page == "forum_results", "论坛搜索切换到了论坛结果页面。");
				Eval("document.querySelector('[data-action=\"open_forum\"]').click();");
				break;
			case 9:
				Expect(page == "forum_post", "论坛搜索结果打开了目击帖页面。");
				Expect(Get(message, "forum_image_width", 0).AsInt32() > 0, "论坛照片从项目内本地资源成功显示。");
				Eval("(()=>{const p=document.querySelector('.post .body'); const r=document.createRange(); r.selectNodeContents(p); const s=window.getSelection(); s.removeAllRanges(); s.addRange(r); document.dispatchEvent(new MouseEvent('mouseup',{bubbles:true})); setTimeout(()=>document.getElementById('collectSelection').click(),0);})();");
				break;
			case 10:
				Expect(Get(message, "clue_count", 0).AsInt32() == 3, "启事文字、启事照片和论坛目击文字均由玩家收录。");
				CompleteTest();
				break;
		}
	}

	/// <summary>根据测试当前阶段验证搜索词，再让 C++ 返回搜索结果快照。</summary>
	private void HandleSearch(Dictionary payload)
	{
		string query = Get(payload, "query", "").AsString();
		string scope = Get(payload, "scope", "").AsString();
		if (_step == 0)
		{
			Expect(query == "猫" && scope == "web", "网页提交了全网猫字搜索。");
			Dictionary result = Dispatch("search", payload, "search_completed");
			Dictionary snapshot = Get(result, "snapshot", new Dictionary()).AsGodotDictionary();
			Expect(Get(snapshot, "search_results", new Godot.Collections.Array()).AsGodotArray().Count == 3,
					"C++ 将猫字泛化搜索判成三个无关结果。");
			ReturnSnapshot(1);
		}
		else if (_step == 3)
		{
			Expect(query == "年糕" && scope == "web", "网页再次搜索了年糕。");
			DispatchAndReturn("search", payload, "search_completed", 4);
		}
		else if (_step == 7)
		{
			Expect(query == "猫" && scope == "forum", "收录启事文字和照片后搜索了论坛猫帖。");
			Dictionary result = Dispatch("search", payload, "search_completed");
			Dictionary snapshot = Get(result, "snapshot", new Dictionary()).AsGodotDictionary();
			Expect(Get(snapshot, "search_results", new Godot.Collections.Array()).AsGodotArray().Count == 1,
					"C++ 只返回一个论坛目击帖。");
			ReturnSnapshot(8);
		}
	}

	/// <summary>验证正文划选按钮发出的收录请求，并让 C++ 记录玩家的线索卡。</summary>
	private void HandleClueCollection(Dictionary payload)
	{
		string page = Get(payload, "page", "").AsString();
		Dictionary result = Dispatch("collect_clue", payload, "clue_collected");
		ReturnSnapshot(page == "article" ? 6 : 10);
	}

	/// <summary>照片点击先打开原生预览；测试再模拟玩家按下预览中的收录按钮。</summary>
	private void HandleImagePreview(Dictionary payload)
	{
		Expect(Get(payload, "page", "").AsString() == "article", "玩家点击启事照片后网页提交预览请求。");
		Dictionary result = Dispatch("collect_clue", new Dictionary
		{
			["page"] = "article",
			["kind"] = "image",
		}, "clue_collected");
		ReturnSnapshot(7);
	}

	/// <summary>执行操作并检查业务操作码，避免页面只收到无条件成功回执。</summary>
	private Dictionary Dispatch(string action, Dictionary payload, string expectedCode)
	{
		Dictionary result = _state!.Call("dispatch_action", action, payload).AsGodotDictionary();
		Expect(Get(result, "code", "").AsString() == expectedCode, action + " 操作通过 C++ 校验。");
		return result;
	}

	/// <summary>执行状态操作、保留当前 CEF 页面状态并等待网页确认重绘。</summary>
	private void DispatchAndReturn(string action, Dictionary payload, string expectedCode, int nextStep)
	{
		Dispatch(action, payload, expectedCode);
		ReturnSnapshot(nextStep);
	}

	/// <summary>把最新权威快照发回 CEF，修订号用于防止旧异步消息覆盖新画面。</summary>
	private void ReturnSnapshot(int nextStep)
	{
		_step = nextStep;
		_waitingForState = true;
		SendSnapshot();
	}

	/// <summary>在 CEF 主 Frame 执行测试构造的真实页面表单和文本选择交互。</summary>
	private void Eval(string script)
	{
		_browser!.Call("eval", script);
	}

	/// <summary>将权威进度快照包装成 Godot 到网页的 IPC 消息。</summary>
	private void SendSnapshot()
	{
		Dictionary snapshotMessage = new()
		{
			["version"] = 1,
			["type"] = "state",
			["page"] = "prologue",
			["snapshot"] = _state!.Call("get_snapshot").AsGodotDictionary(),
		};
		_browser!.Call("send_ipc_message", Json.Stringify(snapshotMessage));
	}

	/// <summary>每项断言立即写入结果，失败后仍继续运行以收集后续错误。</summary>
	private void Expect(bool condition, string message)
	{
		if (condition) GD.Print("[PASS] " + message);
		else Fail(message);
	}

	/// <summary>记录集成失败及原因，最终通过非零退出码通知自动验收流程。</summary>
	private void Fail(string message)
	{
		_failed = true;
		GD.PushError("[FAIL] " + message);
	}

	/// <summary>统一结束场景并输出可在日志中搜索的验收标记。</summary>
	private void CompleteTest()
	{
		if (_completed) return;
		_completed = true;
		GD.Print(_failed ? "[FAIL] 序章 CEF IPC 集成验收失败。" : "[PASS] 序章 CEF IPC 集成验收通过。");
		GetTree().Quit(_failed ? 1 : 0);
	}

	/// <summary>读取 Godot 无类型 Dictionary 字段，兼容缺字段时的安全默认值。</summary>
	private static Variant Get(Dictionary dictionary, string key, Variant fallback)
	{
		return dictionary.ContainsKey(key) ? dictionary[key] : fallback;
	}
}
