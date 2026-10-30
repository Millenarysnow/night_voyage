#nullable enable

using Godot;
using Godot.Collections;
using System;
using System.Collections.Generic;

/// <summary>
/// 序章游戏入口。
///
/// C++ 的 NVPrologueState 是唯一的剧情状态持有者，本类只负责把状态投影到
/// Godot 原生桌面、聊天、白板和 CEF 网页，并把玩家输入转换成状态机操作。
/// 这样后续更换原型内容时，界面不需要重新实现一套剧情判断。
/// </summary>
public partial class PrologueMain : Control
{
	private const string ContentPath = "res://content/prologue_cat.json";
	private const string BrowserPagePath = "res://content/prologue_browser.html";
	private const string BrowserExtensionPath = "res://addons/godot_cef/godot_cef.gdextension";
	private const string BackgroundPath = "res://content/assets/desktop_bg.png";

	private Node? _state;
	private Dictionary _snapshot = new();
	private Control? _browser;
	private Panel? _browserPanel;
	private Panel? _boardPanel;
	private Panel? _imageOverlay;
	private VBoxContainer? _chatMessages;
	private LineEdit? _chatInput;
	private VBoxContainer? _boardContent;
	private Label? _clockLabel;
	private Label? _statusLabel;
	private readonly Queue<(string Speaker, string Text)> _dialogueQueue = new();
	private bool _cefReady;
	private bool _fallbackBrowser;
	private bool _browserCreated;
	private bool _dialogueQueueRunning;
	private int _dialogueGeneration;

	/// <summary>
	/// 初始化顺序固定为：加载原生扩展、读取内容、创建状态机、创建桌面、开始序章。
	/// 状态机先于界面创建，避免界面首次绘制时读到半初始化的进度。
	/// </summary>
	public override void _Ready()
	{
		SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
		BuildDesktop();
		CreateState();
		DispatchAction("begin", new Dictionary());
	}

	/// <summary>
	/// 按帧检查当前状态。CEF 可能比 Godot 场景晚几帧完成本地网页加载，
	/// 因此在收到网页 ready 之前不发送快照，避免第一条消息被页面脚本错过。
	/// </summary>
	public override void _Process(double delta)
	{
		if (_cefReady && _browser != null && _snapshot.Count > 0)
		{
			// ready 标志只用于打开后第一次同步，发送完成后清除，防止每帧重复刷新网页。
			_cefReady = false;
			SendSnapshotToBrowser();
		}
	}

	/// <summary>
	/// 加载并实例化 C++ 状态类，所有内容字段从 JSON 资源注入。
	/// </summary>
	private void CreateState()
	{
		if (!ClassDB.CanInstantiate("NVPrologueState"))
		{
			string extensionPath = ProjectSettings.GlobalizePath("res://addons/night_voyageExt/bin/windows/night_voyageExt.windows.template_debug.x86_64.dll");
			if (GDExtensionManager.LoadExtension(extensionPath) == GDExtensionManager.LoadStatus.Failed)
			{
				ShowFatalError("无法加载序章 C++ 扩展。");
				return;
			}
		}

		GodotObject? stateObject = ClassDB.Instantiate("NVPrologueState").AsGodotObject();
		_state = stateObject as Node;
		if (_state == null)
		{
			ShowFatalError("无法创建序章状态机。");
			return;
		}
		AddChild(_state);

		Variant jsonVariant = Json.ParseString(FileAccess.GetFileAsString(ContentPath));
		if (jsonVariant.VariantType != Variant.Type.Dictionary)
		{
			ShowFatalError("序章内容文件不是有效的 JSON 对象。");
			return;
		}
		Dictionary content = jsonVariant.AsGodotDictionary();
		_state.Call("configure_content", content);
		_state.Connect("state_changed", Callable.From<Dictionary>(OnStateChanged));
		_snapshot = _state.Call("get_snapshot").AsGodotDictionary();
		RefreshAllViews();
	}

	/// <summary>
	/// 创建桌面背景、左侧应用栏、聊天窗和可切换白板。
	/// 所有面板均使用固定最小尺寸，窗口缩放时只改变桌面视图比例，避免文字挤压。
	/// </summary>
	private void BuildDesktop()
	{
		TextureRect background = new()
		{
			Texture = LoadTexture(BackgroundPath),
			ExpandMode = TextureRect.ExpandModeEnum.IgnoreSize,
			StretchMode = TextureRect.StretchModeEnum.KeepAspectCovered,
			MouseFilter = Control.MouseFilterEnum.Ignore,
		};
		background.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
		AddChild(background);
		MoveChild(background, 0);

		ColorRect tint = new()
		{
			Color = new Color("101722cc"),
			MouseFilter = Control.MouseFilterEnum.Ignore,
		};
		tint.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
		AddChild(tint);

		Panel sidebar = new()
		{
			Position = new Vector2(14, 22),
			Size = new Vector2(78, 610),
			MouseFilter = Control.MouseFilterEnum.Ignore,
		};
		sidebar.AddThemeStyleboxOverride("panel", MakePanelStyle(new Color("17212dcc"), new Color("344655"), 8));
		AddChild(sidebar);

		VBoxContainer apps = new()
		{
			Position = new Vector2(8, 12),
			Size = new Vector2(62, 590),
			Alignment = BoxContainer.AlignmentMode.Begin,
		};
		sidebar.AddChild(apps);
		apps.AddChild(MakeAppButton("暗线", "chat", OpenChat));
		apps.AddChild(MakeAppButton("浏览器", "browser", OpenBrowser));
		apps.AddChild(MakeAppButton("白板", "board", OpenBoard));
		apps.AddChild(MakeAppButton("重置", "reset", ResetPrologue));

		BuildChatWindow();
		BuildBoardWindow();
		BuildStatusBar();
	}

	/// <summary>
	/// 创建桌面应用按钮。按钮只负责打开对应窗口，剧情是否可做由 C++ 状态机判断。
	/// </summary>
	private Button MakeAppButton(string title, string actionName, Action action)
	{
		Button button = new()
		{
			Text = title,
			CustomMinimumSize = new Vector2(62, 68),
			TooltipText = title,
			FocusMode = Control.FocusModeEnum.None,
		};
		button.Pressed += action;
		button.SetMeta("app_action", actionName);
		return button;
	}

	/// <summary>
	/// 创建聊天窗和消息输入框。聊天消息来自状态机响应中的 say 数组，
	/// 不在 UI 侧复制任何剧情台词。
	/// </summary>
	private void BuildChatWindow()
	{
		Panel panel = new()
		{
			Position = new Vector2(108, 28),
			Size = new Vector2(500, 590),
			Name = "ChatWindow",
		};
		panel.AddThemeStyleboxOverride("panel", MakePanelStyle(new Color("111a25ee"), new Color("52697a"), 8));
		AddChild(panel);

		Label title = new()
		{
			Text = "暗线 · 林越",
			Position = new Vector2(20, 13),
			Size = new Vector2(300, 32),
		};
		title.AddThemeFontSizeOverride("font_size", 20);
		panel.AddChild(title);

		_statusLabel = new Label
		{
			Text = "在线",
			Position = new Vector2(380, 18),
			Size = new Vector2(90, 24),
			HorizontalAlignment = HorizontalAlignment.Right,
			Modulate = new Color("7ad5a0"),
		};
		panel.AddChild(_statusLabel);

		ScrollContainer scroll = new()
		{
			Position = new Vector2(14, 56),
			Size = new Vector2(472, 452),
			HorizontalScrollMode = ScrollContainer.ScrollMode.Disabled,
		};
		panel.AddChild(scroll);
		_chatMessages = new VBoxContainer
		{
			CustomMinimumSize = new Vector2(450, 440),
		};
		_chatMessages.AddThemeConstantOverride("separation", 10);
		scroll.AddChild(_chatMessages);

		_chatInput = new LineEdit
		{
			PlaceholderText = "跟林越说话…",
			Position = new Vector2(18, 522),
			Size = new Vector2(370, 42),
		};
		_chatInput.AddThemeFontSizeOverride("font_size", 16);
		panel.AddChild(_chatInput);
		_chatInput.TextSubmitted += OnChatSubmitted;

		Button send = new()
		{
			Text = "发送",
			Position = new Vector2(398, 522),
			Size = new Vector2(82, 42),
		};
		send.Pressed += SubmitChatInput;
		panel.AddChild(send);
	}

	/// <summary>
	/// 创建白板窗口。任务区域实现 Godot 原生拖放，卡片的正确性仍由 C++ 校验。
	/// </summary>
	private void BuildBoardWindow()
	{
		_boardPanel = new Panel
		{
			Position = new Vector2(138, 48),
			Size = new Vector2(1020, 600),
			Visible = false,
			Name = "BoardWindow",
		};
		_boardPanel.AddThemeStyleboxOverride("panel", MakePanelStyle(new Color("111a25f2"), new Color("b58a3c"), 8));
		AddChild(_boardPanel);

		Label title = new()
		{
			Text = "白板 · 你自己拼的那块板子",
			Position = new Vector2(24, 18),
			Size = new Vector2(600, 30),
		};
		title.AddThemeFontSizeOverride("font_size", 20);
		_boardPanel.AddChild(title);

		Button close = new()
		{
			Text = "关闭",
			Position = new Vector2(920, 14),
			Size = new Vector2(76, 32),
		};
		close.Pressed += () => _boardPanel.Visible = false;
		_boardPanel.AddChild(close);

		ScrollContainer boardScroll = new()
		{
			Position = new Vector2(22, 62),
			Size = new Vector2(976, 510),
			HorizontalScrollMode = ScrollContainer.ScrollMode.Disabled,
		};
		_boardPanel.AddChild(boardScroll);
		_boardContent = new VBoxContainer
		{
			SizeFlagsHorizontal = Control.SizeFlags.ExpandFill,
			SizeFlagsVertical = Control.SizeFlags.ExpandFill,
		};
		_boardContent.AddThemeConstantOverride("separation", 12);
		boardScroll.AddChild(_boardContent);
	}

	/// <summary>
	/// 创建底部状态栏，显示当前阶段和游戏内时间。
	/// </summary>
	private void BuildStatusBar()
	{
		Panel bar = new()
		{
			Position = new Vector2(0, 660),
			Size = new Vector2(1280, 60),
		};
		bar.AddThemeStyleboxOverride("panel", MakePanelStyle(new Color("0c131dcc"), new Color("293b48"), 0));
		AddChild(bar);
		_clockLabel = new Label
		{
			Text = "10 月 16 日 15:20",
			Position = new Vector2(970, 16),
			Size = new Vector2(280, 28),
			HorizontalAlignment = HorizontalAlignment.Right,
		};
		bar.AddChild(_clockLabel);
	}

	/// <summary>
	/// 创建 CEF 网页正文。CEF 不保存剧情状态，加载完成后只接收 C++ 快照。
	/// </summary>
	private void CreateBrowser()
	{
		if (_browserCreated)
		{
			return;
		}
		_browserCreated = true;
		_browserPanel = new Panel
		{
			Position = new Vector2(632, 28),
			Size = new Vector2(630, 600),
			Visible = false,
			Name = "BrowserWindow",
		};
		_browserPanel.AddThemeStyleboxOverride("panel", MakePanelStyle(new Color("111a25f5"), new Color("52697a"), 8));
		AddChild(_browserPanel);

		Label title = new()
		{
			Text = "浏览器 · 暗涌",
			Position = new Vector2(18, 12),
			Size = new Vector2(560, 30),
		};
		title.AddThemeFontSizeOverride("font_size", 18);
		_browserPanel.AddChild(title);

		PanelContainer host = new()
		{
			Position = new Vector2(10, 50),
			Size = new Vector2(610, 538),
		};
		host.AddThemeStyleboxOverride("panel", MakePanelStyle(new Color("101923"), new Color("3d5363"), 4));
		_browserPanel.AddChild(host);

		if (!ClassDB.CanInstantiate("CefTexture"))
		{
			GDExtensionManager.LoadExtension(ProjectSettings.GlobalizePath(BrowserExtensionPath));
		}
		if (ClassDB.CanInstantiate("CefTexture"))
		{
			GodotObject? browserObject = ClassDB.Instantiate("CefTexture").AsGodotObject();
			_browser = browserObject as Control;
		}
		if (_browser == null)
		{
			_fallbackBrowser = true;
			Label fallback = new()
			{
				Text = "CEF 网页扩展尚未加载。\n请确认 addons/godot_cef/bin 中有本机运行库。",
				HorizontalAlignment = HorizontalAlignment.Center,
				VerticalAlignment = VerticalAlignment.Center,
				Size = new Vector2(610, 538),
			};
			host.AddChild(fallback);
			return;
		}

		_browser.Set("enable_accelerated_osr", false);
		_browser.Set("url", BrowserPagePath);
		_browser.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
		_browser.Connect("load_finished", Callable.From<string, int>(OnBrowserLoadFinished));
		_browser.Connect("ipc_message", Callable.From<string>(OnBrowserIpcMessage));
		host.AddChild(_browser);
	}

	/// <summary>CEF 本地页面完成加载后，下一帧把完整状态推送给网页。</summary>
	private void OnBrowserLoadFinished(string url, int statusCode)
	{
		if (statusCode >= 200 && statusCode < 400)
		{
			_cefReady = true;
		}
	}

	/// <summary>
	/// 解析网页 IPC。网页只能提交 action，C# 不根据网页传来的阶段字段做任何信任判断。
	/// </summary>
	private void OnBrowserIpcMessage(string rawMessage)
	{
		if (rawMessage == "prologue:ready")
		{
			_cefReady = true;
			return;
		}
		Variant parsed = Json.ParseString(rawMessage);
		if (parsed.VariantType != Variant.Type.Dictionary)
		{
			return;
		}
		Dictionary message = parsed.AsGodotDictionary();
		if (Get(message, "type", "").AsString() != "action")
		{
			return;
		}
		string action = Get(message, "action", "").AsString();
		Dictionary payload = message.ContainsKey("payload") ? message["payload"].AsGodotDictionary() : new Dictionary();
		if (action == "preview_image")
		{
			ShowImagePreview(Get(payload, "page", "article").AsString());
			return;
		}
		DispatchAction(action, payload);
	}

	/// <summary>把 C++ 快照包装成网页协议消息；CEF 只接收单向状态投影。</summary>
	private void SendSnapshotToBrowser()
	{
		if (_browser == null || _fallbackBrowser)
		{
			return;
		}
		Dictionary message = new()
		{
			["version"] = 1,
			["type"] = "state",
			["page"] = "prologue",
			["revision"] = Get(_snapshot, "revision", 0),
			["snapshot"] = _snapshot,
		};
		_browser.Call("send_ipc_message", Json.Stringify(message));
	}

	/// <summary>状态变化信号的唯一处理入口，统一刷新聊天外的所有投影视图。</summary>
	private void OnStateChanged(Dictionary snapshot)
	{
		_snapshot = snapshot;
		RefreshAllViews();
		if (_browser != null && _browserPanel != null && _browserPanel.Visible)
		{
			SendSnapshotToBrowser();
		}
	}

	/// <summary>
	/// 执行一个状态机操作并把响应中的对白显示到聊天窗。
	/// </summary>
	private Dictionary DispatchAction(string action, Dictionary payload)
	{
		if (_state == null)
		{
			return new Dictionary();
		}
		Dictionary result = _state.Call("dispatch_action", action, payload).AsGodotDictionary();
		if ((action == "chat" || action == "submit_location") && payload.ContainsKey("text"))
		{
			AppendChat("我", payload["text"].AsString(), true);
		}
		if (result.ContainsKey("say"))
		{
			foreach (Variant line in result["say"].AsGodotArray())
			{
				_dialogueQueue.Enqueue(("林越", line.AsString()));
			}
			PumpDialogueQueue();
		}
		// 无论地点是由聊天动作内部转发，还是由原生 UI 直接提交，确认后都只启动一次找回计时。
		if (Get(result, "code", "").AsString() == "location_confirmed")
		{
			// 玩家已经把地址报准，林越先骑车去接猫，随后自动发回交接消息。
			CompleteRecoveryAfterDelay();
		}
		return result;
	}

	/// <summary>
	/// 把林越连续台词排入同一个队列，避免多次玩家操作造成回复插队。
	/// </summary>
	private void PumpDialogueQueue()
	{
		if (_dialogueQueueRunning || !IsInsideTree())
		{
			return;
		}
		_dialogueQueueRunning = true;
		PlayDialogueQueue();
	}

	/// <summary>
	/// 按消息顺序逐条显示对白，保持开场和找猫交接时的聊天节奏。
	/// </summary>
	private async void PlayDialogueQueue()
	{
		int generation = _dialogueGeneration;
		while (_dialogueQueue.Count > 0 && IsInsideTree() && generation == _dialogueGeneration)
		{
			(string speaker, string text) = _dialogueQueue.Dequeue();
			AppendChat(speaker, text, false);
			await ToSignal(GetTree().CreateTimer(0.45), SceneTreeTimer.SignalName.Timeout);
		}
		if (generation == _dialogueGeneration)
		{
			_dialogueQueueRunning = false;
		}
	}

	/// <summary>
	/// 模拟林越骑车去接年糕的短暂行程，自动提交交接完成操作并播放收尾台词。
	/// </summary>
	private async void CompleteRecoveryAfterDelay()
	{
		int generation = _dialogueGeneration;
		await ToSignal(GetTree().CreateTimer(1.4), SceneTreeTimer.SignalName.Timeout);
		if (IsInsideTree() && generation == _dialogueGeneration)
		{
			DispatchAction("finish_recovery", new Dictionary());
		}
	}

	/// <summary>刷新聊天、白板和窗口状态，保证状态机推进后画面立即同步。</summary>
	private void RefreshAllViews()
	{
		RebuildBoard();
		if (_clockLabel != null)
		{
			_clockLabel.Text = $"10 月 16 日 15:20 · {Get(_snapshot, "stage", "boot").AsString()}";
		}
		if (_statusLabel != null)
		{
			_statusLabel.Text = Get(_snapshot, "complete", false).AsBool() ? "已完成" : "在线";
		}
	}

	/// <summary>开关聊天窗；本序章默认打开聊天窗，便于直接看到林越的派活。</summary>
	private void OpenChat()
	{
		if (_boardPanel != null) _boardPanel.Visible = false;
		if (_browserPanel != null) _browserPanel.Visible = false;
	}

	/// <summary>打开 CEF 浏览器，并在已加载时立即同步最新状态。</summary>
	private void OpenBrowser()
	{
		if (_boardPanel != null) _boardPanel.Visible = false;
		CreateBrowser();
		if (_browserPanel != null)
		{
			_browserPanel.Visible = true;
			if (_browser != null) SendSnapshotToBrowser();
		}
	}

	/// <summary>打开白板，显示任务树和玩家已经收录的所有卡片。</summary>
	private void OpenBoard()
	{
		if (_browserPanel != null) _browserPanel.Visible = false;
		if (_boardPanel != null)
		{
			_boardPanel.Visible = true;
			RebuildBoard();
		}
	}

	/// <summary>重置本次序章，内容配置保留，方便验收时快速重走路径。</summary>
	private void ResetPrologue()
	{
		if (_state == null) return;
		// 使尚未结束的对白队列和接猫计时失效，避免重开后旧剧情插入新流程。
		_dialogueGeneration++;
		_dialogueQueue.Clear();
		_dialogueQueueRunning = false;
		_imageOverlay?.QueueFree();
		_imageOverlay = null;
		_state.Call("reset");
		if (_chatMessages != null)
		{
			foreach (Node child in _chatMessages.GetChildren()) child.QueueFree();
		}
		DispatchAction("begin", new Dictionary());
	}

	/// <summary>提交聊天输入；由 C++ 根据消息内容决定普通对白还是地点报告。</summary>
	private void SubmitChatInput()
	{
		if (_chatInput == null || _chatInput.Text.Trim().Length == 0) return;
		string text = _chatInput.Text.Trim();
		_chatInput.Clear();
		// 统一从 chat 入口提交，C++ 会根据当前内容判断是否是地点、粗略地点或普通闲聊。
		DispatchAction("chat", new Dictionary { ["text"] = text });
	}

	/// <summary>LineEdit 回车和发送按钮共用同一个提交函数。</summary>
	private void OnChatSubmitted(string text) => SubmitChatInput();

	/// <summary>把一条消息加入可滚动聊天区，并设置玩家/林越不同的对齐与颜色。</summary>
	private void AppendChat(string speaker, string message, bool fromPlayer)
	{
		if (_chatMessages == null) return;
		Panel bubble = new()
		{
			CustomMinimumSize = new Vector2(430, 72),
			SizeFlagsHorizontal = fromPlayer ? SizeFlags.ShrinkEnd : SizeFlags.ShrinkBegin,
		};
		bubble.AddThemeStyleboxOverride("panel", MakePanelStyle(fromPlayer ? new Color("243249") : new Color("202a39"), new Color("405369"), 7));
		VBoxContainer content = new() { CustomMinimumSize = new Vector2(400, 64) };
		Label name = new() { Text = speaker, Modulate = new Color("e3b866") };
		Label text = new() { Text = message, AutowrapMode = TextServer.AutowrapMode.WordSmart, CustomMinimumSize = new Vector2(400, 40) };
		content.AddChild(name);
		content.AddChild(text);
		bubble.AddChild(content);
		_chatMessages.AddChild(bubble);
		CallDeferred(MethodName.ScrollChatToBottom);
	}

	/// <summary>把聊天滚动条移到最新消息；通过延迟调用等待布局尺寸完成。</summary>
	private void ScrollChatToBottom()
	{
		if (_chatMessages?.GetParent() is ScrollContainer scroll)
		{
			scroll.SetDeferred("scroll_vertical", int.MaxValue);
		}
	}

	/// <summary>重新绘制白板任务与线索卡，拖放本身只提交 attach_clue。</summary>
	private void RebuildBoard()
	{
		if (_boardContent == null || !_snapshot.ContainsKey("quest")) return;
		foreach (Node child in _boardContent.GetChildren()) child.QueueFree();
		Dictionary quest = _snapshot["quest"].AsGodotDictionary();
		Label heading = new()
		{
			Text = $"今晚要办的事 · {Get(quest, "title", "找到年糕").AsString()}",
			Modulate = new Color("f0c66d"),
		};
		heading.AddThemeFontSizeOverride("font_size", 18);
		_boardContent.AddChild(heading);

		bool questComplete = Get(quest, "complete", false).AsBool();
		if (questComplete)
		{
			Label finished = new()
			{
				Text = "年糕已经平安回家 · 任务完成",
				Modulate = new Color("8dd9a8"),
			};
			finished.AddThemeFontSizeOverride("font_size", 17);
			_boardContent.AddChild(finished);
		}
		else if (quest.ContainsKey("tasks"))
		{
			foreach (Variant taskValue in quest["tasks"].AsGodotArray())
			{
				Dictionary task = taskValue.AsGodotDictionary();
				string taskId = Get(task, "id", "").AsString();
				TaskDropZone zone = new(taskId, Get(task, "title", "").AsString(),
						Get(task, "done", false).AsBool(), AttachClue);
				_boardContent.AddChild(zone);
				if (task.ContainsKey("clue_ids"))
				{
					foreach (Variant clueId in task["clue_ids"].AsGodotArray())
					{
						ClueCard? card = FindClue(clueId.AsString());
						if (card != null) zone.AddCard(card);
					}
				}
			}
		}

		// 玩家收录的卡片留在自由区，玩家拖入任务后才会挂接，支持之后再拖回来。
		FreeDropZone loose = new(DetachClue)
		{
			CustomMinimumSize = new Vector2(950, 210),
			SizeFlagsVertical = SizeFlags.ExpandFill,
		};
		_boardContent.AddChild(loose);
		if (_snapshot.ContainsKey("clues"))
		{
			foreach (Variant value in _snapshot["clues"].AsGodotArray())
			{
				Dictionary clue = value.AsGodotDictionary();
				if (!string.IsNullOrEmpty(Get(clue, "task_id", "").AsString())) continue;
				loose.AddCard(new ClueCard(clue));
			}
		}
		if (!questComplete && Get(_snapshot, "quest_ready", false).AsBool())
		{
			Label ready = new() { Text = "两条子任务已经对上，回到暗线向林越报告位置。", Modulate = new Color("8dd9a8") };
			_boardContent.AddChild(ready);
		}
	}

	/// <summary>从快照找到一张线索卡，供任务区重新挂接。</summary>
	private ClueCard? FindClue(string clueId)
	{
		if (!_snapshot.ContainsKey("clues")) return null;
		foreach (Variant value in _snapshot["clues"].AsGodotArray())
		{
			Dictionary clue = value.AsGodotDictionary();
			if (Get(clue, "id", "").AsString() == clueId) return new ClueCard(clue);
		}
		return null;
	}

	/// <summary>白板拖放落地时提交任务 ID，正确性由 C++ 判定。</summary>
	private void AttachClue(string clueId, string taskId)
	{
		DispatchAction("attach_clue", new Dictionary { ["clue_id"] = clueId, ["task_id"] = taskId });
	}

	/// <summary>白板卡片拖回自由区时解除任务挂接，但保留玩家已经完成的子任务。</summary>
	private void DetachClue(string clueId)
	{
		DispatchAction("detach_clue", new Dictionary { ["clue_id"] = clueId });
	}

	/// <summary>显示图片灯箱。图片点击和收录分成两个动作，符合白板收录规则。</summary>
	private void ShowImagePreview(string page)
	{
		_imageOverlay?.QueueFree();
		_imageOverlay = new Panel
		{
			Position = new Vector2(680, 70),
			Size = new Vector2(520, 510),
			MouseFilter = Control.MouseFilterEnum.Stop,
			ZIndex = 20,
		};
		_imageOverlay.AddThemeStyleboxOverride("panel", MakePanelStyle(new Color("111a25f8"), new Color("dca63f"), 8));
		AddChild(_imageOverlay);
		string imagePath = page == "forum_post" ? "res://content/assets/prop_cat.png" : "res://content/assets/prop_cat_home.png";
		TextureRect image = new()
		{
			Texture = LoadTexture(imagePath),
			Position = new Vector2(20, 20),
			Size = new Vector2(480, 390),
			ExpandMode = TextureRect.ExpandModeEnum.IgnoreSize,
			StretchMode = TextureRect.StretchModeEnum.KeepAspectCentered,
		};
		_imageOverlay.AddChild(image);
		Button collect = new() { Text = "收进白板", Position = new Vector2(250, 440), Size = new Vector2(120, 40) };
		collect.Pressed += () =>
		{
			DispatchAction("collect_clue", new Dictionary { ["page"] = page, ["kind"] = "image" });
			_imageOverlay?.QueueFree();
			_imageOverlay = null;
		};
		_imageOverlay.AddChild(collect);
		Button close = new() { Text = "关闭", Position = new Vector2(385, 440), Size = new Vector2(90, 40) };
		close.Pressed += () => { _imageOverlay?.QueueFree(); _imageOverlay = null; };
		_imageOverlay.AddChild(close);
	}

	/// <summary>
	/// 从 Godot 无类型 Dictionary 读取字段。Godot.Collections.Dictionary 没有
	/// .NET Dictionary 的泛型索引约束，因此所有跨 C++/C# 的数据都通过此方法取值。
	/// </summary>
	private static Variant Get(Dictionary dictionary, string key, Variant fallback)
	{
		return dictionary.ContainsKey(key) ? dictionary[key] : fallback;
	}

	/// <summary>
	/// 优先读取 Godot 已导入的纹理，导入尚未完成时退回直接读取 PNG 文件。
	/// 这样新复制进项目的原型素材在首次运行时也能立即显示。
	/// </summary>
	private static Texture2D? LoadTexture(string resourcePath)
	{
		// 资源目录可能还没有生成 .import 文件，直接读 PNG 字节可以避免首次运行白屏。
		byte[] bytes = FileAccess.GetFileAsBytes(resourcePath);
		Image image = new();
		if (image.LoadPngFromBuffer(bytes) != Error.Ok || image.IsEmpty())
		{
			return null;
		}
		return ImageTexture.CreateFromImage(image);
	}

	/// <summary>通用面板样式，统一沿用原型的深色玻璃与琥珀色边框。</summary>
	private static StyleBoxFlat MakePanelStyle(Color background, Color border, int radius)
	{
		StyleBoxFlat style = new()
		{
			BgColor = background,
			BorderColor = border,
			BorderWidthLeft = 1,
			BorderWidthTop = 1,
			BorderWidthRight = 1,
			BorderWidthBottom = 1,
			CornerRadiusTopLeft = radius,
			CornerRadiusTopRight = radius,
			CornerRadiusBottomLeft = radius,
			CornerRadiusBottomRight = radius,
			ContentMarginLeft = 12,
			ContentMarginRight = 12,
			ContentMarginTop = 8,
			ContentMarginBottom = 8,
		};
		return style;
	}

	/// <summary>CEF 不可用时给出明确的本地错误，避免静默白屏。</summary>
	private void ShowFatalError(string message)
	{
		Label error = new()
		{
			Text = message,
			Position = new Vector2(120, 80),
			Size = new Vector2(900, 120),
			Modulate = new Color("ffb6a8"),
		};
		error.AddThemeFontSizeOverride("font_size", 22);
		AddChild(error);
	}

	/// <summary>白板中的可拖动线索卡，拖动结果只携带不可信的 ID。</summary>
	private sealed partial class ClueCard : PanelContainer
	{
		private readonly Dictionary _clue;

		public ClueCard(Dictionary clue)
		{
			_clue = clue;
			CustomMinimumSize = new Vector2(270, 96);
			AddThemeStyleboxOverride("panel", MakePanelStyle(new Color("202c38"), new Color("b58a3c"), 5));
			VBoxContainer content = new()
			{
				MouseFilter = Control.MouseFilterEnum.Ignore,
			};
			Label source = new() { Text = PrologueMain.Get(clue, "source", "线索").AsString(), Modulate = new Color("e7bd67") };
			Label text = new() { Text = String.IsNullOrEmpty(PrologueMain.Get(clue, "text", "").AsString()) ? "照片线索" : PrologueMain.Get(clue, "text", "").AsString(), AutowrapMode = TextServer.AutowrapMode.WordSmart };
			source.MouseFilter = Control.MouseFilterEnum.Ignore;
			text.MouseFilter = Control.MouseFilterEnum.Ignore;
			content.AddChild(source);
			content.AddChild(text);
			AddChild(content);
		}

		public override Variant _GetDragData(Vector2 atPosition)
		{
			Label preview = new() { Text = PrologueMain.Get(_clue, "source", "线索").AsString(), Modulate = new Color("ffe4a2") };
			SetDragPreview(preview);
			return new Dictionary { ["clue_id"] = PrologueMain.Get(_clue, "id", "").AsString() };
		}
	}

	/// <summary>白板任务投放区，接受任意卡片后交给外层 C++ 判定是否答对。</summary>
	private sealed partial class TaskDropZone : PanelContainer
	{
		private readonly string _taskId;
		private readonly VBoxContainer _cards;
		private readonly Action<string, string> _attach;

		public TaskDropZone(string taskId, string title, bool done, Action<string, string> attach)
		{
			_taskId = taskId;
			_attach = attach;
			CustomMinimumSize = new Vector2(950, 112);
			AddThemeStyleboxOverride("panel", MakePanelStyle(new Color("17232e"),
					done ? new Color("4c9673") : new Color("795f31"), 5));
			VBoxContainer layout = new()
			{
				MouseFilter = Control.MouseFilterEnum.Ignore,
			};
			Label header = new()
			{
				Text = done ? "已完成　" + title : title,
				Modulate = done ? new Color("8dd9a8") : new Color("f0c66d"),
			};
			header.MouseFilter = Control.MouseFilterEnum.Ignore;
			header.AddThemeFontSizeOverride("font_size", 17);
			layout.AddChild(header);
			_cards = new VBoxContainer();
			_cards.AddThemeConstantOverride("separation", 4);
			_cards.MouseFilter = Control.MouseFilterEnum.Ignore;
			layout.AddChild(_cards);
			AddChild(layout);
		}

		public void AddCard(ClueCard card) => _cards.AddChild(card);

		public override bool _CanDropData(Vector2 atPosition, Variant data)
		{
			return data.VariantType == Variant.Type.Dictionary && data.AsGodotDictionary().ContainsKey("clue_id");
		}

		public override void _DropData(Vector2 atPosition, Variant data)
		{
			Dictionary dict = data.AsGodotDictionary();
			_attach(PrologueMain.Get(dict, "clue_id", "").AsString(), _taskId);
		}
	}

	/// <summary>白板自由区作为拖放目标，让已经挂接的卡片可以自由拆下并重新归类。</summary>
	private sealed partial class FreeDropZone : FlowContainer
	{
		private readonly Action<string> _detach;

		public FreeDropZone(Action<string> detach)
		{
			_detach = detach;
			AddThemeConstantOverride("h_separation", 12);
			AddThemeConstantOverride("v_separation", 12);
		}

		public void AddCard(ClueCard card) => AddChild(card);

		public override bool _CanDropData(Vector2 atPosition, Variant data)
		{
			return data.VariantType == Variant.Type.Dictionary && data.AsGodotDictionary().ContainsKey("clue_id");
		}

		public override void _DropData(Vector2 atPosition, Variant data)
		{
			Dictionary dict = data.AsGodotDictionary();
			_detach(PrologueMain.Get(dict, "clue_id", "").AsString());
		}
	}
}
