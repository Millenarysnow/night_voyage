#nullable enable
using Godot;
using Godot.Collections;
using System;
using System.Threading.Tasks;

/// <summary>
/// 使用真实 BrowserView 场景和 CEF 验证：全网发现网站、站内筛选、原生导航、选区收录、
/// 图片解码、提前浏览以及找猫完成后新增收尾楼。测试不维护第二套网页加载逻辑。
/// </summary>
public partial class PrologueBrowserIntegrationTest : Control
{
	private Node _state = null!;
	private BrowserView _view = null!;
	private int _step;
	private long _handledRevision = -1;
	private bool _failed;
	private bool _completed;
	private double _elapsed;

	/// <summary>装配产品使用的浏览器场景，由组件管理独立网站文档的加载和 IPC。</summary>
	public override void _Ready()
	{
		_state = ClassDB.Instantiate("NVPrologueState").AsGodotObject() as Node ?? throw new InvalidOperationException("NVPrologueState 未注册。");
		AddChild(_state);
		_state.Call("configure_content", Json.ParseString(FileAccess.GetFileAsString("res://content/prologue_cat.json")).AsGodotDictionary());
		_state.Call("dispatch_action", "begin", new Dictionary());
		_view = GD.Load<PackedScene>("res://content/prologue/scenes/browser_window_content.tscn").Instantiate<BrowserView>();
		AddChild(_view);
		_view.ActionRequested += OnAction;
		_view.PageStateApplied += OnPageApplied;
		_view.ApplySnapshot(Snapshot());
		_view.Open();
	}

	/// <summary>按真实时间限制等待，避免帧率差异造成假超时。</summary>
	public override void _Process(double delta)
	{
		_elapsed += delta;
		if (!_completed && _elapsed > 75) { Expect(false, "CEF 超时，步骤 " + _step); Complete(); }
	}

	/// <summary>网页和工具栏动作共用 C++ 入口；图片预览后模拟玩家按下收录确认。</summary>
	private void OnAction(string action, Dictionary payload)
	{
		if (action == "preview_image")
		{
			string page = payload["page"].AsString();
			Dictionary source = page == "forum_post" ? Snapshot()["active_post"].AsGodotDictionary() : Snapshot()["content"].AsGodotDictionary()["article"].AsGodotDictionary();
			Expect(source["image"].AsString().StartsWith("res://content/assets/"), "灯箱使用当前页面的本地图片。");
			action = "collect_clue";
			payload = new Dictionary { ["page"] = page, ["kind"] = "image" };
		}
		Dictionary result = _state.Call("dispatch_action", action, payload).AsGodotDictionary();
		_view.ShowActionResult(result);
		if (_step == 22)
		{
			Expect(result["code"].AsString() == "thread_unavailable", "错误帖子地址被明确拒绝。");
			Expect(_view.GetNode<Label>("Status").Visible && _view.GetNode<Label>("Status").Text.Contains("帖子"), "失败原因以正常中文显示，保留当前网页。");
			VerifyDesktop();
			return;
		}
		Expect(result["ok"].AsBool(), action + " 通过 C++ 裁决。");
		_view.ApplySnapshot(result["snapshot"].AsGodotDictionary());
	}

	/// <summary>收到真实 DOM 与图片解码回执后推进输入；重复或过期回执不会重复执行步骤。</summary>
	private async void OnPageApplied(Dictionary message)
	{
		long revision = message["revision"].AsInt64();
		if (_completed || revision <= _handledRevision || revision != Snapshot()["revision"].AsInt64()) return;
		_handledRevision = revision;
		string page = message["page"].AsString();
		Expect(_view.GetNode<LineEdit>("Toolbar/Controls/Address").Text == message["url"].AsString(), "原生地址栏与网页路由一致。");
		try
		{
			switch (_step++)
			{
				case 0:
					Expect(page == "home" && message["has_global_search"].AsBool() && !message["has_forum_search"].AsBool(), "从独立夜航搜索首页开始。");
					Search("web", "暗涌"); break;
				case 1:
					Expect(Snapshot()["search_results"].AsGodotArray().Count == 1, "全网搜暗涌只得到网站入口。");
					Eval("document.querySelector('[data-action=\"open_forum\"]').click()"); break;
				case 2:
					Expect(page == "forum" && message["post_count"].AsInt32() == 7 && message["clue_count"].AsInt32() == 0, "无需启事收录即可进入七帖论坛首页。");
					Expect(message["has_forum_search"].AsBool() && !message["has_global_search"].AsBool(), "论坛是独立文档，只提供站内搜索。");
					await Capture("forum-home.png"); Search("forum", "猫"); break;
				case 3:
					Expect(page == "forum" && message["post_count"].AsInt32() == 1, "站内搜猫得到目击帖。");
					Eval("document.querySelector('[data-category=\"都市怪谈\"]').click()"); break;
				case 4:
					Expect(message["post_count"].AsInt32() == 0 && message["empty_message"].AsString().Contains("没有找到相关帖子"), "不匹配分类显示自然空结果。");
					Search("forum", ""); break;
				case 5:
					Expect(message["post_count"].AsInt32() == 4, "清空关键词恢复怪谈分类列表。");
					Eval("document.getElementById('forumQuery').value='猫'; document.querySelector('[data-category=\"全部\"]').click()"); break;
				case 6:
					Eval("document.querySelector('[data-action=\"open_thread\"]').click()"); break;
				case 7:
					Expect(page == "forum_post" && message["forum_image_width"].AsInt32() > 0, "帖子成功显示本地猫照片。");
					PressNavigation("Back"); break;
				case 8:
					Expect(page == "forum" && message["forum_query"].AsString() == "猫", "原生后退恢复站内查询。");
					PressNavigation("Forward"); break;
				case 9:
					Expect(page == "forum_post", "原生前进恢复当前帖子。");
					await Capture("forum-cat-post.png"); CollectText("threadBody"); break;
				case 10:
					Expect(message["clue_count"].AsInt32() == 1, "帖子文字通过真实 DOM 选区收录。");
					Eval("document.getElementById('threadPhoto').click()"); break;
				case 11:
					Expect(message["clue_count"].AsInt32() == 2, "帖子照片通过预览确认后收录。");
					PressNavigation("Home"); break;
				case 12:
					Expect(page == "home", "主页跨文档返回夜航搜索。"); Search("web", "猫"); break;
				case 13:
					Expect(Snapshot()["search_results"].AsGodotArray().Count == 3, "全网搜猫仍只有无关网页，不泄露已读猫帖。");
					Eval("document.querySelector('[data-action=\"open_result\"]').click()"); break;
				case 14:
					Expect(page == "web_result", "无关搜索结果能正常打开。");
					Eval("document.querySelector('[data-page=\"search_results\"]').click()"); break;
				case 15:
					Search("web", "年糕"); break;
				case 16:
					Eval("document.querySelector('[data-action=\"open_article\"]').click()"); break;
				case 17:
					Expect(page == "article" && message["article_image_width"].AsInt32() > 0, "启事加载自己的独立网页和照片。");
					CollectText("articleBody"); break;
				case 18:
					Expect(message["article_text_collected"].AsBool(), "启事文字收录成功。");
					Eval("document.getElementById('articlePhoto').click()"); break;
				case 19:
					Expect(message["article_image_collected"].AsBool() && message["clue_count"].AsInt32() == 4, "两个站点的文字与图片分别收录成四张卡。");
					FinishRecovery(); break;
				case 20:
					Expect(Snapshot()["complete"].AsBool(), "提前逛论坛后仍可完成找猫。");
					SubmitAddress("anyong.bbs/thread/pet_sighting"); break;
				case 21:
					Expect(page == "forum_post" && message["floor_count"].AsInt32() == 5, "完成后重访帖子新增失主领走的收尾楼。");
					SubmitAddress("https://anyong.bbs/thread/missing"); break;
			}
		}
		catch (Exception exception) { Expect(false, "CEF 验收异常：" + exception); Complete(); }
	}

	/// <summary>提交真实搜索表单，不直接调用网页内部渲染函数。</summary>
	private void Search(string prefix, string query) => Eval($"document.getElementById('{prefix}Query').value={Json.Stringify(query)}; document.getElementById('{prefix}Search').requestSubmit()");
	/// <summary>创建正文选区，点击产品已有的浮动收录按钮。</summary>
	private void CollectText(string id) => Eval($"(()=>{{const node=document.getElementById('{id}');const range=document.createRange();range.selectNodeContents(node);const selection=window.getSelection();selection.removeAllRanges();selection.addRange(range);document.dispatchEvent(new MouseEvent('mouseup',{{bubbles:true}}));document.getElementById('collectSelection').click();}})()");
	/// <summary>验证真实场景按钮已经连接的原生导航信号。</summary>
	private void PressNavigation(string name) => _view.GetNode<Button>("Toolbar/Controls/" + name).EmitSignal(Button.SignalName.Pressed);
	/// <summary>提交原生地址栏，覆盖无协议域名和帖子路径解析。</summary>
	private void SubmitAddress(string address)
	{
		LineEdit input = _view.GetNode<LineEdit>("Toolbar/Controls/Address");
		input.Text = address;
		input.EmitSignal(LineEdit.SignalName.TextSubmitted, address);
	}
	/// <summary>通过白板与聊天语义动作完成任务，不直接覆写剧情状态。</summary>
	private void FinishRecovery()
	{
		foreach (Variant item in Snapshot()["clues"].AsGodotArray())
		{
			Dictionary clue = item.AsGodotDictionary();
			if (clue["kind"].AsString() != "text") continue;
			_state.Call("dispatch_action", "attach_clue", new Dictionary { ["clue_id"] = clue["id"], ["task_id"] = clue["page"].AsString() == "article" ? "cat_look" : "cat_place" });
		}
		Dictionary report = _state.Call("dispatch_action", "chat", new Dictionary { ["text"] = "老粮站的花坛" }).AsGodotDictionary();
		Expect(report["code"].AsString() == "location_confirmed", "真实摘录能完成白板任务并报告位置。");
		Dictionary result = _state.Call("dispatch_action", "finish_recovery", new Dictionary()).AsGodotDictionary();
		_view.ApplySnapshot(result["snapshot"].AsGodotDictionary());
	}
	/// <summary>可选导出包含原生工具栏和真实 CEF 内容的渲染验收图。</summary>
	private async Task Capture(string filename, BrowserView? browser = null)
	{
		if (!System.Array.Exists(OS.GetCmdlineUserArgs(), argument => argument == "--capture-browser")) return;
		// DOM 就绪不等于纹理已发布；等待实际纹理包含页面细节，不能把纯色首帧当作成功截图。
		bool painted = false;
		for (int attempt = 0; attempt < 60 && !painted; attempt++)
		{
			await ToSignal(GetTree().CreateTimer(0.05), SceneTreeTimer.SignalName.Timeout);
			if ((browser ?? _view).CefSurface!.Get("texture").AsGodotObject() is not Texture2D texture) continue;
			Image image = texture.GetImage();
			if (image.GetWidth() < 16 || image.GetHeight() < 16) continue;
			Color background = image.GetPixel(4, 4);
			for (int y = 1; y < 10 && !painted; y++)
				for (int x = 1; x < 14 && !painted; x++)
					painted = !image.GetPixel(image.GetWidth() * x / 14, image.GetHeight() * y / 10).IsEqualApprox(background);
		}
		Expect(painted, "CEF 已发布包含可见网页内容的纹理。");
		await ToSignal(RenderingServer.Singleton, RenderingServer.SignalName.FramePostDraw);
		Expect(GetViewport().GetTexture().GetImage().SavePng("res://artifacts/" + filename) == Error.Ok, "保存浏览器验收图：" + filename);
	}

	/// <summary>在实际 CRT 桌面中装配浏览器，验证主协调器确实使用新组件和独立网页。</summary>
	private async void VerifyDesktop()
	{
		try
		{
			_view.Hide();
			PrologueMain main = GD.Load<PackedScene>("res://main.tscn").Instantiate<PrologueMain>();
			AddChild(main);
			main.GetNode<Control>("LockScreen").Hide();
			main.Call("DispatchAction", "begin", new Dictionary());
			main.Call("DispatchAction", "search", new Dictionary { ["query"] = "暗涌" });
			main.Call("DispatchAction", "open_forum", new Dictionary { ["id"] = "anyong" });
			Panel frame = main.GetNode<Panel>("PrologueDesktop/CRTViewport/ContentViewport/Content/DesktopContent/WindowLayer/browser");
			BrowserView browser = frame.GetNode<BrowserView>("Body/BrowserContent");
			TaskCompletionSource<Dictionary> ready = new();
			browser.PageStateApplied += message => ready.TrySetResult(message);
			main.Call("ShowWindow", "browser");
			Dictionary applied = await ready.Task;
			Expect(applied["page"].AsString() == "forum", "主桌面的真实浏览器也成功加载独立论坛首页。");
			Expect(browser.GetNode<LineEdit>("Toolbar/Controls/Address").Size.X > 300, "真实窗口中的原生地址栏保持可用宽度。");
			Expect(browser.GetNode<Control>("BrowserHost").Position.Y >= browser.GetNode<Control>("Toolbar").Size.Y, "CEF 正文与原生导航栏不重叠。");
			await Capture("forum-desktop.png", browser);
		}
		catch (Exception exception) { Expect(false, "真实桌面验收异常：" + exception); }
		Complete();
	}
	/// <summary>读取权威快照并通过扩展的脚本执行接口驱动 DOM。</summary>
	private Dictionary Snapshot() => _state.Call("get_snapshot").AsGodotDictionary();
	private void Eval(string script) => _view.CefSurface!.Call("eval", script);
	/// <summary>记录断言并通过进程退出码报告整条流程结果。</summary>
	private void Expect(bool condition, string message)
	{
		if (condition) GD.Print("[PASS] " + message);
		else { _failed = true; GD.PushError("[FAIL] " + message); }
	}
	/// <summary>只结束一次验收，失败通过非零退出码报告。</summary>
	private void Complete()
	{
		if (_completed) return;
		_completed = true;
		GD.Print(_failed ? "[FAIL] 序章 CEF IPC 集成验收失败。" : "[PASS] 序章 CEF IPC 集成验收通过。");
		GetTree().Quit(_failed ? 1 : 0);
	}
}
