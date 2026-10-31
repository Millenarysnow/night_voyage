#nullable enable

using Godot;
using Godot.Collections;
using System;
using System.Collections.Generic;

/// <summary>
/// 序幕运行时协调器。
/// 所有可见界面和可重复控件均来自 PackedScene；本类只装配场景、连接输入，
/// 并把 C++ 状态机快照投影到已经存在的场景节点。
/// </summary>
public partial class PrologueMain : Control
{
	private const string ContentPath = "res://content/prologue_cat.json";
	private const string BrowserPagePath = "res://content/prologue_browser.html";
	private const string BrowserExtensionPath = "res://addons/godot_cef/godot_cef.gdextension";
	private const string NativeExtensionPath = "res://addons/night_voyageExt/bin/night_voyageExt.gdextension";
	private const string DesktopScenePath = "res://content/prologue/scenes/prologue_desktop.tscn";
	private const string LockScenePath = "res://content/prologue/scenes/lock_screen.tscn";
	private const string ChatWindowScenePath = "res://content/prologue/scenes/chat_window.tscn";
	private const string BrowserWindowScenePath = "res://content/prologue/scenes/browser_window.tscn";
	private const string BoardWindowScenePath = "res://content/prologue/scenes/board_window.tscn";
	private const string WindowTaskbarButtonScenePath = "res://content/prologue/scenes/window_taskbar_button.tscn";
	private const string ChatMessageScenePath = "res://content/prologue/scenes/chat_message.tscn";
	private const string ToastScenePath = "res://content/prologue/scenes/toast.tscn";
	private const string ImageLightboxScenePath = "res://content/prologue/scenes/image_lightbox.tscn";
	private const string FatalErrorScenePath = "res://content/prologue/scenes/error_message.tscn";
	private const int ModalZIndex = 3000;
	private const int MaximumWindowZIndex = 900;
	// 每个窗口预留足够容纳内部卡片选中层的区间，防止后方窗口的高 Z 子控件盖住前窗。
	private const int WindowZStride = 128;

	/// <summary>单个应用窗口及其场景化任务栏按钮的运行时句柄。</summary>
	private sealed class WindowRecord
	{
		public required string Id;
		public required string Title;
		public required Panel Frame;
		public required Control Body;
		public required Button TaskbarButton;
		public bool Minimized;
	}

	private Node? _state;
	private Dictionary _snapshot = new();
	private Control? _desktop;
	private Control? _desktopContent;
	private Control? _lockOverlay;
	private Panel? _imageOverlay;
	private LineEdit? _loginName;
	private Label? _loginError;
	private Label? _welcomeLabel;
	private Label? _playerLabel;
	private Label? _clockLabel;
	private Label? _statusLabel;
	private VBoxContainer? _chatMessages;
	private LineEdit? _chatInput;
	private Control? _browserHost;
	private Control? _browser;
	private BoardView? _boardView;
	private bool _browserCreated;
	private bool _fallbackBrowser;
	private bool _cefReady;
	private bool _unlocked;
	private string _playerName = "";
	private string _previewedImagePage = "article";
	private int _windowZ = 20;
	private Panel? _draggingWindow;
	private Vector2 _dragPointerOrigin;
	private Vector2 _dragWindowOrigin;
	private double _clockRefreshElapsed;
	private bool _dialogueQueueRunning;
	private int _dialogueGeneration;
	private readonly Queue<(string Speaker, string Text)> _dialogueQueue = new();
	private readonly System.Collections.Generic.Dictionary<string, WindowRecord> _windows = new();
	private readonly System.Collections.Generic.Dictionary<string, Tween> _windowTweens = new();

	/// <summary>加载项目 C++ 扩展后再解析含原生 CRT 类型的场景资源。</summary>
	public override void _Ready()
	{
		SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
		EnsureNativeExtension();
		BuildSceneComposition();
		CreateState();
		RefreshAllViews();
	}

	/// <summary>等待 CEF 页面报到后发送首份快照，并低频刷新任务栏时钟。</summary>
	public override void _Process(double delta)
	{
		if (_cefReady && _browserCreated && _snapshot.Count > 0)
		{
			_cefReady = false;
			SendSnapshotToBrowser();
		}
		_clockRefreshElapsed += delta;
		if (_clockRefreshElapsed >= 0.5)
		{
			_clockRefreshElapsed = 0.0;
			RefreshClock();
		}
	}

	/// <summary>统一处理窗口聚焦和标题栏拖动；窗口的尺寸与布局来自应用场景。</summary>
	public override void _Input(InputEvent @event)
	{
		// 根视口仍会先收到全局输入；模态层打开时不能顺便聚焦或拖动后面的应用窗口。
		// 淡出动画结束后遮罩会被 QueueFree；先确认托管引用仍对应有效节点，
		// 避免已释放的 Godot 对象在输入帧中抛异常并截断窗口拖动逻辑。
		bool lockOverlayVisible = _lockOverlay != null && GodotObject.IsInstanceValid(_lockOverlay) && _lockOverlay.IsVisibleInTree();
		bool imageOverlayVisible = _imageOverlay != null && GodotObject.IsInstanceValid(_imageOverlay) && _imageOverlay.IsVisibleInTree();
		if (lockOverlayVisible || imageOverlayVisible)
		{
			_draggingWindow = null;
			return;
		}
		if (@event is InputEventMouseMotion motion && _draggingWindow != null)
		{
			Vector2 next = _dragWindowOrigin + motion.Position - _dragPointerOrigin;
			_draggingWindow.Position = ClampWindowPosition(_draggingWindow, next);
			return;
		}

		if (@event is not InputEventMouseButton mouseButton || mouseButton.ButtonIndex != MouseButton.Left)
		{
			return;
		}

		if (!mouseButton.Pressed)
		{
			_draggingWindow = null;
			return;
		}

		WindowRecord? hit = FindTopWindowAt(mouseButton.Position);
		if (hit == null) return;

		FocusWindow(hit.Id);
		Control titleBar = hit.Frame.GetNode<Control>("TitleBar");
		bool clickedWindowAction = titleBar.GetNode<Control>("Minimize").GetGlobalRect().HasPoint(mouseButton.Position) ||
			titleBar.GetNode<Control>("Close").GetGlobalRect().HasPoint(mouseButton.Position);
		if (!clickedWindowAction && titleBar.GetGlobalRect().HasPoint(mouseButton.Position))
		{
			_draggingWindow = hit.Frame;
			_dragPointerOrigin = mouseButton.Position;
			_dragWindowOrigin = hit.Frame.Position;
		}
	}

	/// <summary>
	/// 实例化桌面、登录页和三个应用场景，再连接场景节点上的交互信号。
	/// 场景负责位置、尺寸、视觉层级和控件结构；这里仅负责场景组合关系。
	/// </summary>
	private void BuildSceneComposition()
	{
		PackedScene? desktopScene = GD.Load<PackedScene>(DesktopScenePath);
		if (desktopScene?.Instantiate() is not Control desktop)
		{
			ShowFatalError("无法加载序幕桌面场景。");
			return;
		}
		_desktop = desktop;
		_desktop.Name = "PrologueDesktop";
		_desktop.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
		AddChild(_desktop);

		_desktopContent = _desktop.GetNodeOrNull<Control>("CRTViewport/ContentViewport/Content/DesktopContent");
		if (_desktopContent == null)
		{
			ShowFatalError("桌面场景缺少 CRT 内容节点。");
			return;
		}

		ConnectDesktopIcons();
		InitializeTaskbarReferences();
		RegisterWindow("chat", "暗线 · 林越", "ENCRYPTED", "💬", ChatWindowScenePath);
		RegisterWindow("browser", "夜航浏览器", "SEARCH", "🌐", BrowserWindowScenePath);
		RegisterWindow("board", "白板", "BOARD", "🗒️", BoardWindowScenePath);
		InitializeChatReferences();
		InitializeBrowserReferences();
		InitializeLightbox();
		InitializeLockScreen();
	}

	/// <summary>桌面图标来自 desktop_app_icon.tscn，应用 ID 存在每个场景实例的导出属性中。</summary>
	private void ConnectDesktopIcons()
	{
		VBoxContainer? icons = _desktopContent?.GetNodeOrNull<VBoxContainer>("DesktopIcons");
		if (icons == null) return;

		foreach (Node child in icons.GetChildren())
		{
			if (child is DesktopAppIconView icon)
			{
				string appId = icon.Name.ToString();
				icon.Pressed += () => ShowWindow(appId);
			}
		}
	}

	/// <summary>从任务栏场景缓存窗口按钮容器、玩家名和时钟节点。</summary>
	private void InitializeTaskbarReferences()
	{
		if (_desktopContent == null) return;
		_playerLabel = _desktopContent.GetNodeOrNull<Label>("Taskbar/PlayerName");
		_clockLabel = _desktopContent.GetNodeOrNull<Label>("Taskbar/Clock");
	}

	/// <summary>从聊天窗场景缓存消息列表、输入框和联系人状态节点。</summary>
	private void InitializeChatReferences()
	{
		if (!_windows.TryGetValue("chat", out WindowRecord? chat)) return;
		_chatMessages = chat.Frame.GetNodeOrNull<VBoxContainer>("Body/ChatContent/ChatScroll/ChatMessages");
		_chatInput = chat.Frame.GetNodeOrNull<LineEdit>("Body/ChatContent/InputPanel/ChatInput");
		_statusLabel = chat.Frame.GetNodeOrNull<Label>("Body/ChatContent/Header/Status");
		if (_chatInput != null) _chatInput.TextSubmitted += OnChatSubmitted;
		Button? send = chat.Frame.GetNodeOrNull<Button>("Body/ChatContent/InputPanel/SendButton");
		if (send != null) send.Pressed += SubmitChatInput;
	}

	/// <summary>浏览器窗的占位面板存在于场景中，原生 CEF 表面只作为插件节点挂入该插槽。</summary>
	private void InitializeBrowserReferences()
	{
		if (_windows.TryGetValue("browser", out WindowRecord? browser))
		{
			_browserHost = browser.Frame.GetNodeOrNull<Control>("Body/BrowserContent/BrowserHost");
		}
	}

	/// <summary>预建灯箱场景并连接收录、关闭和点击遮罩事件。</summary>
	private void InitializeLightbox()
	{
		PackedScene? scene = GD.Load<PackedScene>(ImageLightboxScenePath);
		if (scene?.Instantiate() is not Panel overlay) return;

		_imageOverlay = overlay;
		_imageOverlay.Name = "ImageLightbox";
		_imageOverlay.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
		_imageOverlay.ZIndex = ModalZIndex;
		_imageOverlay.Visible = false;
		AddChild(_imageOverlay);
		_imageOverlay.GuiInput += OnLightboxGuiInput;
		_imageOverlay.GetNode<Button>("Card/Collect").Pressed += CollectPreviewedImage;
		_imageOverlay.GetNode<Button>("Card/Close").Pressed += CloseImageOverlay;
	}

	/// <summary>登录卡片、壁纸和遮罩都来自 lock_screen.tscn。</summary>
	private void InitializeLockScreen()
	{
		PackedScene? scene = GD.Load<PackedScene>(LockScenePath);
		if (scene?.Instantiate() is not Panel overlay)
		{
			ShowFatalError("无法加载登录场景。");
			return;
		}

		_lockOverlay = overlay;
		_lockOverlay.Name = "LockScreen";
		_lockOverlay.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
		_lockOverlay.ZIndex = ModalZIndex;
		AddChild(_lockOverlay);
		_loginName = _lockOverlay.GetNode<LineEdit>("LoginCard/LoginName");
		_loginError = _lockOverlay.GetNode<Label>("LoginCard/LoginError");
		_welcomeLabel = _lockOverlay.GetNode<Label>("Welcome");
		_loginName.TextSubmitted += _ => UnlockDesktop();
		_lockOverlay.GetNode<Button>("LoginCard/UnlockButton").Pressed += UnlockDesktop;
		_loginName.CallDeferred(Control.MethodName.GrabFocus);
	}

	/// <summary>加载应用窗口 PackedScene，并将对应任务栏按钮模板挂到桌面任务栏。</summary>
	private void RegisterWindow(string id, string title, string subtitle, string glyph, string scenePath)
	{
		if (_desktopContent == null) return;
		PackedScene? windowScene = GD.Load<PackedScene>(scenePath);
		if (windowScene?.Instantiate() is not Panel frame)
		{
			ShowFatalError("无法加载应用窗口场景：" + title);
			return;
		}

		frame.Name = id;
		frame.ZIndex = _windowZ;
		Node windowLayer = _desktopContent.GetNode("WindowLayer");
		windowLayer.AddChild(frame);
		frame.GetNode<Label>("TitleBar/Glyph").Text = glyph;
		frame.GetNode<Label>("TitleBar/Title").Text = title;
		frame.GetNode<Label>("TitleBar/Subtitle").Text = subtitle;
		frame.GetNode<Button>("TitleBar/Minimize").Pressed += () => MinimizeWindow(id);
		frame.GetNode<Button>("TitleBar/Close").Pressed += () => CloseWindow(id);

		PackedScene? taskbarScene = GD.Load<PackedScene>(WindowTaskbarButtonScenePath);
		if (taskbarScene?.Instantiate() is not Button taskbarButton)
		{
			ShowFatalError("无法加载窗口任务栏按钮场景。");
			return;
		}
		taskbarButton.Name = id + "TaskbarButton";
		taskbarButton.GetNode<Label>("Glyph").Text = glyph;
		taskbarButton.GetNode<Label>("Title").Text = title;
		taskbarButton.Visible = false;
		_desktopContent.GetNode("Taskbar/WindowButtons").AddChild(taskbarButton);
		taskbarButton.Pressed += () => ToggleWindow(id);

		_windows[id] = new WindowRecord
		{
			Id = id,
			Title = title,
			Frame = frame,
			Body = frame.GetNode<Control>("Body"),
			TaskbarButton = taskbarButton,
		};
		if (id == "board")
		{
			_boardView = frame.GetNodeOrNull<BoardView>("Body/BoardContentRoot");
			if (_boardView != null)
			{
				_boardView.ActionRequested += HandleBoardAction;
				_boardView.ImagePreviewRequested += ShowBoardImagePreview;
			}
		}
	}

	/// <summary>加载并实例化 C++ 状态机，再注入 JSON 内容并订阅唯一状态变化信号。</summary>
	private void CreateState()
	{
		EnsureNativeExtension();
		if (!ClassDB.ClassExists("NVPrologueState"))
		{
			ShowFatalError("无法加载序幕 C++ 状态扩展。");
			return;
		}

		_state = ClassDB.Instantiate("NVPrologueState").AsGodotObject() as Node;
		if (_state == null)
		{
			ShowFatalError("无法创建序幕状态机。");
			return;
		}
		AddChild(_state);

		Variant parsed = Json.ParseString(FileAccess.GetFileAsString(ContentPath));
		if (parsed.VariantType != Variant.Type.Dictionary)
		{
			ShowFatalError("序章内容文件不是有效的 JSON 对象。");
			return;
		}
		_state.Call("configure_content", parsed.AsGodotDictionary());
		_state.Connect("state_changed", Callable.From<Dictionary>(OnStateChanged));
		_snapshot = _state.Call("get_snapshot").AsGodotDictionary();
	}

	/// <summary>显式加载项目原生扩展，确保后续场景里的 CRT 和状态类型已注册。</summary>
	private void EnsureNativeExtension()
	{
		if (ClassDB.ClassExists("NVScreenEffectViewport") && ClassDB.ClassExists("NVPrologueState")) return;
		GDExtensionManager.LoadExtension(ProjectSettings.GlobalizePath(NativeExtensionPath));
	}

	/// <summary>登录成功后写入玩家名、开始状态机开场并显示暗线窗口。</summary>
	private void UnlockDesktop()
	{
		if (_unlocked) return;
		if (_loginName == null || _loginName.Text.Trim().Length == 0)
		{
			if (_loginError != null) _loginError.Visible = true;
			return;
		}

		_unlocked = true;
		_playerName = _loginName.Text.Trim();
		if (_playerLabel != null)
		{
			_playerLabel.Text = _playerName;
			_playerLabel.Visible = true;
		}
		if (_welcomeLabel != null)
		{
			_welcomeLabel.Text = "欢迎回来，" + _playerName;
			_welcomeLabel.Visible = true;
		}
		if (_loginError != null) _loginError.Visible = false;
		FadeOutLockScreen();

		Dictionary result = DispatchAction("begin", new Dictionary());
		ShowWindow("chat");
		string firstLine = result.ContainsKey("say") && result["say"].AsGodotArray().Count > 0
			? result["say"].AsGodotArray()[0].AsString()
			: "林越发来一条新消息。";
		ShowToast("林越 · 新消息", firstLine);
	}

	/// <summary>打开窗口、恢复其任务栏入口并在首次打开浏览器时创建 CEF 页面。</summary>
	private void ShowWindow(string id)
	{
		if (!_windows.TryGetValue(id, out WindowRecord? record)) return;
		bool wasVisible = record.Frame.Visible;
		record.Frame.Visible = true;
		record.Minimized = false;
		record.TaskbarButton.Visible = true;
		FocusWindow(id);

		if (!wasVisible)
		{
			if (_windowTweens.TryGetValue(id, out Tween? oldTween) && oldTween.IsRunning()) oldTween.Kill();
			record.Frame.Modulate = new Color(1, 1, 1, 0);
			record.Frame.Scale = new Vector2(0.96f, 0.96f);
			record.Frame.PivotOffset = record.Frame.Size * 0.5f;
			Tween tween = CreateTween().SetParallel(true);
			tween.TweenProperty(record.Frame, "modulate:a", 1.0f, 0.26f)
				.SetTrans(Tween.TransitionType.Cubic).SetEase(Tween.EaseType.Out);
			tween.TweenProperty(record.Frame, "scale", Vector2.One, 0.26f)
				.SetTrans(Tween.TransitionType.Cubic).SetEase(Tween.EaseType.Out);
			_windowTweens[id] = tween;
		}

		if (id == "browser") CreateBrowser();
		if (id == "board") _boardView?.Configure(_snapshot);
		if (id == "chat") CallDeferred(MethodName.FocusChatInput);
	}

	/// <summary>关闭窗口但保留场景实例、CEF 页面及应用内状态。</summary>
	private void CloseWindow(string id)
	{
		if (!_windows.TryGetValue(id, out WindowRecord? record)) return;
		StopWindowTween(id);
		record.Frame.Visible = false;
		record.Frame.Modulate = Colors.White;
		record.Frame.Scale = Vector2.One;
		record.Minimized = false;
		record.TaskbarButton.Visible = false;
	}

	/// <summary>隐藏窗口但保留任务栏入口，使按钮可恢复原场景实例。</summary>
	private void MinimizeWindow(string id)
	{
		if (!_windows.TryGetValue(id, out WindowRecord? record)) return;
		StopWindowTween(id);
		record.Frame.Visible = false;
		record.Frame.Modulate = Colors.White;
		record.Frame.Scale = Vector2.One;
		record.Minimized = true;
		record.TaskbarButton.Visible = true;
	}

	/// <summary>终止仍在运行的场景窗口动画，避免隐藏节点继续被 Tween 写入。</summary>
	private void StopWindowTween(string id)
	{
		if (_windowTweens.TryGetValue(id, out Tween? tween) && tween.IsRunning()) tween.Kill();
	}

	/// <summary>任务栏按钮在最小化与恢复窗口之间切换。</summary>
	private void ToggleWindow(string id)
	{
		if (!_windows.TryGetValue(id, out WindowRecord? record)) return;
		if (record.Frame.Visible) MinimizeWindow(id);
		else ShowWindow(id);
	}

	/// <summary>同时提升绘制层级与场景顺序，保证视觉前景和 Godot GUI 输入命中一致。</summary>
	private void FocusWindow(string id)
	{
		if (!_windows.TryGetValue(id, out WindowRecord? record)) return;
		if (_windowZ + WindowZStride > MaximumWindowZIndex)
		{
			_windowZ = 18;
			foreach (WindowRecord ordered in GetWindowsOrderedByZ()) ordered.Frame.ZIndex = _windowZ += WindowZStride;
		}
		_windowZ += WindowZStride;
		record.Frame.ZIndex = _windowZ;
		// Control 的命中按兄弟节点逆序搜索，不依据 ZIndex；只改 Z 会产生“前窗显示、后窗收输入”。
		Node layer = record.Frame.GetParent();
		layer.MoveChild(record.Frame, layer.GetChildCount() - 1);
		Control? focus = record.Frame.GetViewport().GuiGetFocusOwner();
		if (focus != null && focus != record.Frame && !record.Frame.IsAncestorOf(focus))
			focus.ReleaseFocus();
		foreach (WindowRecord other in _windows.Values)
		{
			other.TaskbarButton.Modulate = other.Id == id ? Colors.White : new Color("b4bec9");
		}
	}

	/// <summary>以窗口场景当前层级生成可排序序列，避免 LINQ 分配额外临时列表。</summary>
	private IEnumerable<WindowRecord> GetWindowsOrderedByZ()
	{
		List<WindowRecord> ordered = new(_windows.Values);
		ordered.Sort((left, right) => left.Frame.ZIndex.CompareTo(right.Frame.ZIndex));
		return ordered;
	}

	/// <summary>查找鼠标下可见且层级最高的应用窗口。</summary>
	private WindowRecord? FindTopWindowAt(Vector2 point)
	{
		WindowRecord? result = null;
		int highest = int.MinValue;
		foreach (WindowRecord record in _windows.Values)
		{
			if (record.Frame.Visible && record.Frame.GetGlobalRect().HasPoint(point) && record.Frame.ZIndex >= highest)
			{
				result = record;
				highest = record.Frame.ZIndex;
			}
		}
		return result;
	}

	/// <summary>从当前视口和任务栏场景读取窗口可拖动范围，避免把设计分辨率写进逻辑。</summary>
	private Vector2 ClampWindowPosition(Control window, Vector2 position)
	{
		Vector2 viewportSize = GetViewportRect().Size;
		float taskbarTop = viewportSize.Y;
		if (_desktopContent?.GetNodeOrNull<Control>("Taskbar") is Control taskbar)
		{
			taskbarTop = taskbar.GetGlobalRect().Position.Y;
		}

		return new Vector2(
			Mathf.Clamp(position.X, -window.Size.X + 90, viewportSize.X - 90),
			Mathf.Clamp(position.Y, 0, taskbarTop - 40));
	}

	/// <summary>创建 CEF 扩展原生控件并挂入浏览器场景预留的 BrowserHost 插槽。</summary>
	private void CreateBrowser()
	{
		if (_browserCreated || _browserHost == null) return;
		_browserCreated = true;
		if (!ClassDB.ClassExists("CefTexture"))
		{
			GDExtensionManager.LoadExtension(ProjectSettings.GlobalizePath(BrowserExtensionPath));
		}

		if (ClassDB.ClassExists("CefTexture"))
		{
			_browser = ClassDB.Instantiate("CefTexture").AsGodotObject() as Control;
		}
		if (_browser == null)
		{
			_fallbackBrowser = true;
			Control? content = _windows["browser"].Frame.GetNodeOrNull<Control>("Body/BrowserContent");
			Label? error = content?.GetNodeOrNull<Label>("BrowserError");
			if (error != null)
			{
				error.Text = "CEF 网页扩展尚未加载。\n请确认 addons/godot_cef/bin 中有本机运行库。";
				error.Visible = true;
			}
			return;
		}

		_browser.Set("enable_accelerated_osr", false);
		_browser.Set("url", BrowserPagePath);
		_browser.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
		_browser.Connect("load_finished", Callable.From<string, int>(OnBrowserLoadFinished));
		_browser.Connect("ipc_message", Callable.From<string>(OnBrowserIpcMessage));
		_browserHost.AddChild(_browser);
	}

	/// <summary>记录 CEF 本地网页加载成功，网页 ready 消息仍负责启动第一次状态同步。</summary>
	private void OnBrowserLoadFinished(string url, int statusCode)
	{
		if (statusCode >= 200 && statusCode < 400) _cefReady = true;
	}

	/// <summary>接收网页通过 IPC 发出的语义动作，不采信网页自行提交的进度状态。</summary>
	private void OnBrowserIpcMessage(string rawMessage)
	{
		if (rawMessage == "prologue:ready")
		{
			_cefReady = true;
			return;
		}

		Variant parsed = Json.ParseString(rawMessage);
		if (parsed.VariantType != Variant.Type.Dictionary) return;
		Dictionary message = parsed.AsGodotDictionary();
		if (Read(message, "type", "").AsString() != "action") return;

		string action = Read(message, "action", "").AsString();
		Dictionary payload = message.ContainsKey("payload")
			? message["payload"].AsGodotDictionary()
			: new Dictionary();
		if (action == "preview_image")
		{
			ShowImagePreview(Read(payload, "page", "article").AsString());
			return;
		}
		DispatchAction(action, payload);
	}

	/// <summary>把 C++ 权威快照序列化后回传给正在显示的网页。</summary>
	private void SendSnapshotToBrowser()
	{
		if (_browser == null || _fallbackBrowser) return;
		Dictionary message = new()
		{
			["version"] = 1,
			["type"] = "state",
			["page"] = "prologue",
			["revision"] = Read(_snapshot, "revision", 0),
			["snapshot"] = _snapshot,
		};
		_browser.Call("send_ipc_message", Json.Stringify(message));
	}

	/// <summary>状态机变化后刷新场景投影，并在浏览器已打开时发送新快照。</summary>
	private void OnStateChanged(Dictionary snapshot)
	{
		_snapshot = snapshot;
		RefreshAllViews();
		if (_browser != null && _windows.TryGetValue("browser", out WindowRecord? browser) && browser.Frame.Visible)
		{
			SendSnapshotToBrowser();
		}
	}

	/// <summary>向 C++ 状态机提交动作，回显玩家输入并将返回对白加入顺序队列。</summary>
	private Dictionary DispatchAction(string action, Dictionary payload)
	{
		if (_state == null) return new Dictionary();
		Dictionary result = _state.Call("dispatch_action", action, payload).AsGodotDictionary();
		if ((action == "chat" || action == "submit_location") && payload.ContainsKey("text"))
		{
			AppendChat(_playerName.Length > 0 ? _playerName : "我", payload["text"].AsString(), true);
		}
		if (result.ContainsKey("say"))
		{
			foreach (Variant line in result["say"].AsGodotArray()) _dialogueQueue.Enqueue(("林越", line.AsString()));
			PumpDialogueQueue();
		}
		if (Read(result, "code", "").AsString() == "location_confirmed") CompleteRecoveryAfterDelay();
		return result;
	}

	/// <summary>启动唯一对白队列，避免连续回复互相插队。</summary>
	private void PumpDialogueQueue()
	{
		if (_dialogueQueueRunning || !IsInsideTree()) return;
		_dialogueQueueRunning = true;
		PlayDialogueQueue();
	}

	/// <summary>按原型节奏逐条实例化 chat_message.tscn 并显示林越回复。</summary>
	private async void PlayDialogueQueue()
	{
		int generation = _dialogueGeneration;
		while (_dialogueQueue.Count > 0 && IsInsideTree() && generation == _dialogueGeneration)
		{
			(string speaker, string text) = _dialogueQueue.Dequeue();
			AppendChat(speaker, text, false);
			await ToSignal(GetTree().CreateTimer(0.45), SceneTreeTimer.SignalName.Timeout);
		}
		if (generation == _dialogueGeneration) _dialogueQueueRunning = false;
	}

	/// <summary>地点确认后等待原型行程时间，再请求 C++ 状态机完成交接。</summary>
	private async void CompleteRecoveryAfterDelay()
	{
		int generation = _dialogueGeneration;
		await ToSignal(GetTree().CreateTimer(1.4), SceneTreeTimer.SignalName.Timeout);
		if (IsInsideTree() && generation == _dialogueGeneration) DispatchAction("finish_recovery", new Dictionary());
	}

	/// <summary>把时钟、联系人在线状态及白板投影刷新到场景中的现有节点。</summary>
	private void RefreshAllViews()
	{
		RefreshClock();
		if (_statusLabel != null)
		{
			bool complete = Read(_snapshot, "complete", false).AsBool();
			_statusLabel.Text = complete ? "● 已完成" : "● 在线";
			_statusLabel.Modulate = complete ? new Color("8dd9a8") : new Color("54b183");
		}
		_boardView?.Configure(_snapshot);
	}

	/// <summary>当前找猫序幕固定在一晚内的十五点二十分。</summary>
	private void RefreshClock()
	{
		if (_clockLabel != null) _clockLabel.Text = "15:20\n10月16日 星期五";
	}

	/// <summary>打开场景化聊天窗口，并将键盘焦点交给其输入节点。</summary>
	private void OpenChat() => ShowWindow("chat");

	/// <summary>打开场景化 CEF 浏览器窗口。</summary>
	private void OpenBrowser()
	{
		ShowWindow("browser");
		if (_browser != null) SendSnapshotToBrowser();
	}

	/// <summary>打开场景化白板窗口并同步最新权威快照。</summary>
	private void OpenBoard() => ShowWindow("board");

	/// <summary>重置状态机、聊天展示和延迟对白，保留已经创建的 UI 场景。</summary>
	private void ResetPrologue()
	{
		if (_state == null) return;
		_dialogueGeneration++;
		_dialogueQueue.Clear();
		_dialogueQueueRunning = false;
		CloseImageOverlay();
		ClearChildren(_chatMessages);
		_state.Call("reset");
		if (_unlocked) DispatchAction("begin", new Dictionary());
	}

	/// <summary>提交聊天输入，地点判断与剧情反馈仍完全由 C++ 状态机负责。</summary>
	private void SubmitChatInput()
	{
		if (_chatInput == null || _chatInput.Text.Trim().Length == 0) return;
		string text = _chatInput.Text.Trim();
		_chatInput.Clear();
		DispatchAction("chat", new Dictionary { ["text"] = text });
	}

	/// <summary>LineEdit 的回车提交适配器。</summary>
	private void OnChatSubmitted(string text) => SubmitChatInput();

	/// <summary>将输入焦点交给已由聊天场景定义的输入框。</summary>
	private void FocusChatInput() => _chatInput?.GrabFocus();

	/// <summary>实例化聊天消息模板，把发言者和正文交给模板脚本绑定。</summary>
	private void AppendChat(string speaker, string message, bool fromPlayer)
	{
		if (_chatMessages == null) return;
		PackedScene? scene = GD.Load<PackedScene>(ChatMessageScenePath);
		if (scene?.Instantiate() is not ChatMessageView row)
		{
			ShowFatalError("无法加载聊天消息场景。");
			return;
		}
		row.Configure(speaker, message, fromPlayer);
		_chatMessages.AddChild(row);
		CallDeferred(MethodName.ScrollChatToBottom);
	}

	/// <summary>延迟到容器完成布局后把聊天列表滚动到最新消息。</summary>
	private void ScrollChatToBottom()
	{
		if (_chatMessages?.GetParent() is ScrollContainer scroll) scroll.SetDeferred("scroll_vertical", int.MaxValue);
	}

	/// <summary>把白板语义动作提交给剧情状态机，不在协调器中实现任务判定。</summary>
	private void HandleBoardAction(string action, Dictionary payload)
	{
		Dictionary result = DispatchAction(action, payload);
		if (!Read(result, "ok", false).AsBool())
			GD.PushError("白板操作被状态机拒绝：" + action + "，" + Read(result, "message", "状态机没有返回错误说明").AsString());
	}

	/// <summary>切换预置灯箱场景的图片纹理并显示模态层。</summary>
	private void ShowImagePreview(string page)
	{
		if (_imageOverlay == null) return;
		_previewedImagePage = page == "forum_post" ? "forum_post" : "article";
		// 浏览器图片继续通过内容快照取得资源，不将具体关卡的图片路径写进界面逻辑。
		Dictionary content = _snapshot["content"].AsGodotDictionary();
		Dictionary source = content[_previewedImagePage == "forum_post" ? "forum" : "article"].AsGodotDictionary();
		_imageOverlay.GetNode<TextureRect>("Card/Image").Texture = LoadTexture(source["image"].AsString());
		_imageOverlay.GetNode<Label>("Card/Caption").Text = Read(source, "image_caption", "").AsString();
		_imageOverlay.GetNode<Button>("Card/Collect").Visible = true;
		_imageOverlay.Visible = true;
		_imageOverlay.MoveToFront();
	}

	/// <summary>白板中已经收录的图片只打开原图，隐藏收录入口，避免再次提交网页收录动作。</summary>
	private void ShowBoardImagePreview(string imagePath, string caption)
	{
		if (_imageOverlay == null) return;
		_imageOverlay.GetNode<TextureRect>("Card/Image").Texture = LoadTexture(imagePath);
		_imageOverlay.GetNode<Label>("Card/Caption").Text = caption;
		_imageOverlay.GetNode<Button>("Card/Collect").Visible = false;
		_imageOverlay.Visible = true;
		_imageOverlay.MoveToFront();
	}

	/// <summary>确认收录当前预览照片后关闭场景化灯箱。</summary>
	private void CollectPreviewedImage()
	{
		if (_imageOverlay == null) return;
		DispatchAction("collect_clue", new Dictionary { ["page"] = _previewedImagePage, ["kind"] = "image" });
		CloseImageOverlay();
	}

	/// <summary>点击灯箱卡片外的暗色区域关闭预览。</summary>
	private void OnLightboxGuiInput(InputEvent @event)
	{
		if (@event is InputEventMouseButton { Pressed: true, ButtonIndex: MouseButton.Left } click &&
			_imageOverlay?.GetNode<Control>("Card").GetGlobalRect().HasPoint(click.Position) == false)
		{
			CloseImageOverlay();
		}
	}

	/// <summary>隐藏已预建的灯箱场景，不重建卡片、按钮或遮罩控件。</summary>
	private void CloseImageOverlay()
	{
		if (_imageOverlay != null) _imageOverlay.Visible = false;
	}

	/// <summary>实例化 toast.tscn 并绑定通知文字、点击和自动收起行为。</summary>
	private async void ShowToast(string title, string message)
	{
		if (_desktopContent == null) return;
		PackedScene? scene = GD.Load<PackedScene>(ToastScenePath);
		if (scene?.Instantiate() is not Panel toast) return;
		toast.GetNode<Label>("Content/Title").Text = title;
		toast.GetNode<Label>("Content/Message").Text = message;
		VBoxContainer? host = _desktopContent.GetNodeOrNull<VBoxContainer>("Toasts");
		if (host == null) return;
		host.AddChild(toast);
		toast.Modulate = new Color(1, 1, 1, 0);
		toast.GuiInput += OnToastGuiInput;
		CreateTween().TweenProperty(toast, "modulate:a", 1.0f, 0.35f)
			.SetTrans(Tween.TransitionType.Cubic).SetEase(Tween.EaseType.Out);
		await ToSignal(GetTree().CreateTimer(3.2), SceneTreeTimer.SignalName.Timeout);
		if (GodotObject.IsInstanceValid(toast)) toast.QueueFree();
	}

	/// <summary>点击通知打开暗线窗口；事件源通知仍由其场景节点负责命中。</summary>
	private void OnToastGuiInput(InputEvent @event)
	{
		if (@event is InputEventMouseButton { Pressed: true, ButtonIndex: MouseButton.Left }) OpenChat();
	}

	/// <summary>登录解锁时在欢迎文字淡出后释放锁屏场景实例。</summary>
	private void FadeOutLockScreen()
	{
		if (_lockOverlay == null) return;
		Control overlay = _lockOverlay;
		Tween tween = CreateTween();
		tween.TweenProperty(overlay, "modulate:a", 0.0f, 0.55f)
			.SetTrans(Tween.TransitionType.Cubic).SetEase(Tween.EaseType.Out);
		tween.TweenCallback(Callable.From(() =>
		{
			// 清空字段与释放节点保持同一回调，输入阶段不会再访问悬空引用。
			if (_lockOverlay == overlay) _lockOverlay = null;
			if (GodotObject.IsInstanceValid(overlay)) overlay.QueueFree();
		}));
	}

	/// <summary>清空容器中的运行时场景实例；每个新元素仍通过 PackedScene 创建。</summary>
	private static void ClearChildren(Node? container)
	{
		if (container == null) return;
		foreach (Node child in container.GetChildren()) child.QueueFree();
	}

	/// <summary>从 Godot Dictionary 读取字段并返回调用方提供的默认值。</summary>
	internal static Variant Read(Dictionary dictionary, string key, Variant fallback)
	{
		return dictionary.ContainsKey(key) ? dictionary[key] : fallback;
	}

	/// <summary>供场景绑定脚本复用的快照字段读取入口。</summary>
	internal static Variant Get(Dictionary dictionary, string key, Variant fallback) => Read(dictionary, key, fallback);

	/// <summary>读取 Godot 导入纹理，保留不同图片格式的资源管线；资源缺失时明确报错。</summary>
	private static Texture2D? LoadTexture(string path)
	{
		Texture2D? texture = GD.Load<Texture2D>(path);
		if (texture == null) GD.PushError("无法加载图片资源：" + path);
		return texture;
	}

	/// <summary>实例化错误提示场景并写入具体资源或初始化错误信息。</summary>
	private void ShowFatalError(string message)
	{
		PackedScene? scene = GD.Load<PackedScene>(FatalErrorScenePath);
		if (scene?.Instantiate() is not Label error) return;
		error.Text = message;
		AddChild(error);
	}
}
