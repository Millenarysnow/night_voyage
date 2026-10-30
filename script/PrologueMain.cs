#nullable enable

using Godot;
using Godot.Collections;
using System;
using System.Collections.Generic;
using System.Linq;

/// <summary>
/// 序幕的 Godot 外壳。
/// C++ 的 NVPrologueState 是唯一剧情状态持有者；本类负责桌面、窗口、CEF 页面、
/// 输入和显示效果投影。策划替换 JSON 或网页内容时，不需要在 UI 内复制剧情规则。
/// </summary>
public partial class PrologueMain : Control
{
	private const string ContentPath = "res://content/prologue_cat.json";
	private const string BrowserPagePath = "res://content/prologue_browser.html";
	private const string BrowserExtensionPath = "res://addons/godot_cef/godot_cef.gdextension";
	private const string NativeExtensionPath = "res://addons/night_voyageExt/bin/night_voyageExt.gdextension";
	private const string BackgroundPath = "res://content/assets/desktop_bg.png";
	private const string LinyueAvatarPath = "res://content/assets/avatar_linyue.png";
	private const string ScreenEffectScenePath = "res://addons/night_voyageExt/components/screen_effect_viewport.tscn";
	private const string GlassPanelScenePath = "res://addons/night_voyageExt/components/glass_panel.tscn";
	private const int TaskbarHeight = 46;
	private const int TaskbarZIndex = 1000;
	private const int ToastZIndex = 1200;
	private const int ModalZIndex = 3000;
	private const int MaximumWindowZIndex = 900;

	/// <summary>桌面遮罩复现网页的中心提亮、上下渐暗和边缘暗角。</summary>
	private const string DesktopShadeShaderCode = """
		shader_type canvas_item;
		void fragment() {
			// 桌面原型叠了径向暗角与纵向渐变；这里按同样的透明度顺序合成。
			vec2 radial_uv = (UV - vec2(0.5, 0.1)) / vec2(0.6, 0.45);
			float radial_edge = smoothstep(0.55, 1.2, length(radial_uv));
			float radial_alpha = mix(0.30, 0.92, radial_edge);
			float vertical_alpha = UV.y < 0.4
				? mix(0.55, 0.15, smoothstep(0.0, 0.4, UV.y))
				: mix(0.15, 0.80, smoothstep(0.4, 1.0, UV.y));
			float alpha = radial_alpha + vertical_alpha * (1.0 - radial_alpha);
			COLOR = vec4(vec3(0.018, 0.024, 0.038), alpha);
		}
		""";

	/// <summary>登录页在壁纸上加柔和径向暗化，保留网页原型中可见的背景细节。</summary>
	private const string LockShadeShaderCode = """
		shader_type canvas_item;
		void fragment() {
			// CSS 径向遮罩中心透明度约 0.42，四角加深至约 0.88。
			vec2 radial_uv = (UV - vec2(0.5, 0.2)) / vec2(0.72, 0.9);
			float edge = smoothstep(0.0, 1.1, length(radial_uv));
			float alpha = mix(0.42, 0.88, edge);
			COLOR = vec4(vec3(0.018, 0.024, 0.038), alpha);
		}
		""";

	/// <summary>锁屏处于弧面视口之外，因此单独复现覆盖网页全局的玻璃反光和扫描线。</summary>
	private const string LockGlassShaderCode = """
		shader_type canvas_item;
		void fragment() {
			// 锁屏不做鱼眼畸变，但网页的全局伪元素仍在它上方绘制微弱反光和扫描线。
			vec2 sky_uv = (UV - vec2(0.22, -0.02)) / vec2(0.62, 0.46);
			float sky = (1.0 - smoothstep(0.45, 1.0, length(sky_uv))) * 0.075;
			vec2 return_uv = (UV - vec2(0.84, 1.02)) / vec2(0.34, 0.26);
			float return_light = (1.0 - smoothstep(0.35, 1.0, length(return_uv))) * 0.05;
			float top_glint = (1.0 - smoothstep(0.0, 0.012, UV.y)) * 0.07;
			float scanline = 1.0 - step(1.0, mod(FRAGCOORD.y, 3.0));
			float breath = 0.5 + 0.5 * sin(TIME * 1.2083);
			float scan_alpha = scanline * mix(0.103, 0.127, breath);
			float reflection_alpha = sky + return_light + top_glint;
			float alpha = reflection_alpha + scan_alpha * (1.0 - reflection_alpha);
			vec3 reflection_color = vec3(0.72, 0.84, 1.0);
			vec3 color = reflection_color * (reflection_alpha / max(alpha, 0.0001));
			COLOR = vec4(color, alpha);
		}
		""";

	/// <summary>窗口管理器保存每个应用的独立窗口、任务栏按钮和最小化状态。</summary>
	private sealed class WindowRecord
	{
		public required string Id;
		public required Panel Frame;
		public required Control Body;
		public required Button TaskbarButton;
		public bool Minimized;
	}

	private Node? _state;
	private Dictionary _snapshot = new();
	private Control? _desktopRoot;
	private Panel? _taskbar;
	private HBoxContainer? _taskbarItems;
	private VBoxContainer? _toastContainer;
	private Label? _clockLabel;
	private Label? _playerLabel;
	private Label? _welcomeLabel;
	private string _playerName = "";
	private readonly System.Collections.Generic.Dictionary<string, WindowRecord> _windows = new();
	private readonly System.Collections.Generic.Dictionary<string, Tween> _windowTweens = new();
	private int _windowZ = 20;
	private Panel? _draggingWindow;
	private Vector2 _dragPointerOrigin;
	private Vector2 _dragWindowOrigin;

	private Panel? _browserWindow;
	private Control? _browserBody;
	private Control? _browser;
	private Panel? _imageOverlay;
	private VBoxContainer? _chatMessages;
	private LineEdit? _chatInput;
	private VBoxContainer? _boardContent;
	private Label? _statusLabel;
	private Panel? _lockOverlay;
	private LineEdit? _loginName;
	private Label? _loginError;
	private bool _unlocked;
	private bool _browserCreated;
	private bool _cefReady;
	private bool _fallbackBrowser;
	private bool _dialogueQueueRunning;
	private int _dialogueGeneration;
	private double _clockRefreshElapsed;
	private readonly Queue<(string Speaker, string Text)> _dialogueQueue = new();

	/// <summary>创建 CRT 桌面、应用窗口、状态机与登录遮罩；解锁后才开始剧情。</summary>
	public override void _Ready()
	{
		SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
		EnsureNativeExtension();
		BuildDesktop();
		BuildWindows();
		CreateState();
		BuildLockScreen();
	}

	/// <summary>CEF ready 后发送首份快照，并低频刷新游戏内时钟。</summary>
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

	/// <summary>处理窗口聚焦与标题栏拖动，其他鼠标输入继续交给 Godot 子控件。</summary>
	public override void _Input(InputEvent @event)
	{
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

	/// <summary>建立 CRT 效果视口；扩展未注册时退回普通桌面控件。</summary>
	private void BuildDesktop()
	{
		// CRT 曲面采样在边缘会露出透明像素；用网页桌面的底色填充这些弯曲边界。
		ColorRect backplate = new() { Name = "CRTBackplate", Color = new Color("06080c"), MouseFilter = Control.MouseFilterEnum.Ignore };
		backplate.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
		AddChild(backplate);
		MoveChild(backplate, 0);

		PackedScene? effectScene = GD.Load<PackedScene>(ScreenEffectScenePath);
		if (effectScene != null && effectScene.Instantiate() is Control effect)
		{
			effect.Name = "CRTDesktop";
			effect.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
			// 拉伸关闭后再设置原生像素尺寸，避免 SubViewportContainer 拒绝尺寸修改并让文字被低分辨率放大。
			effect.Set("stretch", false);
			AddChild(effect);
			if (effect.GetNodeOrNull<SubViewport>("ContentViewport") is SubViewport viewport)
			{
				viewport.Size = new Vector2I(1280, 720);
			}
			if (effect.Call("get_content_root").AsGodotObject() is Control contentRoot)
			{
				_desktopRoot = contentRoot;
			}
		}

		// 原生效果类在扩展尚未加载时可能无法实例化，普通 Control 仍可承载整套桌面。
		if (_desktopRoot == null)
		{
			Control fallbackRoot = new() { Name = "DesktopFallback" };
			fallbackRoot.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
			AddChild(fallbackRoot);
			_desktopRoot = fallbackRoot;
		}

		TextureRect wallpaper = new()
		{
			Name = "Wallpaper",
			Texture = LoadTexture(BackgroundPath),
			ExpandMode = TextureRect.ExpandModeEnum.IgnoreSize,
			StretchMode = TextureRect.StretchModeEnum.KeepAspectCovered,
			MouseFilter = Control.MouseFilterEnum.Ignore,
		};
		wallpaper.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
		_desktopRoot.AddChild(wallpaper);
		_desktopRoot.MoveChild(wallpaper, 0);

		ColorRect shade = new()
		{
			Name = "DesktopShade",
			Color = Colors.White,
			Material = MakeShadeMaterial(DesktopShadeShaderCode),
			MouseFilter = Control.MouseFilterEnum.Ignore,
		};
		shade.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
		_desktopRoot.AddChild(shade);
		_desktopRoot.MoveChild(shade, 1);

		BuildDesktopIcons();
		BuildTaskbar();
		_toastContainer = new VBoxContainer
		{
			Name = "Toasts",
			Position = new Vector2(964, 496),
			Size = new Vector2(300, 164),
			Alignment = BoxContainer.AlignmentMode.End,
			MouseFilter = Control.MouseFilterEnum.Ignore,
			ZIndex = ToastZIndex,
		};
		_toastContainer.AddThemeConstantOverride("separation", 8);
		_desktopRoot.AddChild(_toastContainer);
	}

	/// <summary>创建原型桌面左侧的应用图标，点击后打开独立窗口。</summary>
	private void BuildDesktopIcons()
	{
		if (_desktopRoot == null) return;
		VBoxContainer icons = new()
		{
			Name = "DesktopIcons",
			Position = new Vector2(18, 20),
			Size = new Vector2(96, 460),
			ZIndex = 5,
		};
		icons.AddThemeConstantOverride("separation", 6);
		_desktopRoot.AddChild(icons);
		icons.AddChild(MakeAppIcon("💬", "暗线", new Color("254b62"), OpenChat));
		icons.AddChild(MakeAppIcon("🌐", "浏览器", new Color("23584b"), OpenBrowser));
		icons.AddChild(MakeAppIcon("🗒️", "白板", new Color("4b3e24"), OpenBoard));
	}

	/// <summary>构造统一的桌面图标样式。</summary>
	private static Button MakeAppIcon(string glyph, string title, Color iconTint, Action action)
	{
		Button button = new()
		{
			Text = "",
			CustomMinimumSize = new Vector2(96, 86),
			TooltipText = title,
			FocusMode = Control.FocusModeEnum.None,
			MouseDefaultCursorShape = Control.CursorShape.PointingHand,
		};
		button.AddThemeStyleboxOverride("normal", MakePanelStyle(new Color("00000000"), new Color("00000000"), 12));
		button.AddThemeStyleboxOverride("hover", MakePanelStyle(new Color("7896c024"), new Color("96b4dc38"), 12));
		button.AddThemeStyleboxOverride("pressed", MakePanelStyle(new Color("7896c038"), new Color("96b4dc55"), 12));
		Panel glyphPanel = new() { Name = "Glyph", Position = new Vector2(23, 0), Size = new Vector2(50, 50), MouseFilter = Control.MouseFilterEnum.Ignore };
		StyleBoxFlat glyphStyle = MakePanelStyle(iconTint, new Color("ffffff1a"), 13);
		glyphStyle.ShadowColor = new Color("00000080");
		glyphStyle.ShadowSize = 10;
		glyphStyle.ShadowOffset = new Vector2(0, 5);
		glyphPanel.AddThemeStyleboxOverride("panel", glyphStyle);
		Label glyphLabel = new() { Text = glyph, Size = glyphPanel.Size, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center, MouseFilter = Control.MouseFilterEnum.Ignore };
		glyphLabel.AddThemeFontSizeOverride("font_size", 24);
		glyphPanel.AddChild(glyphLabel);
		button.AddChild(glyphPanel);
		Label titleLabel = new() { Text = title, Position = new Vector2(0, 56), Size = new Vector2(96, 20), HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center, MouseFilter = Control.MouseFilterEnum.Ignore };
		titleLabel.AddThemeFontSizeOverride("font_size", 12);
		titleLabel.AddThemeColorOverride("font_color", new Color("e3e8f0"));
		button.AddChild(titleLabel);
		button.Pressed += action;
		return button;
	}

	/// <summary>创建原型底部任务栏、已打开窗口列表、系统托盘与游戏内时钟。</summary>
	private void BuildTaskbar()
	{
		if (_desktopRoot == null) return;
		_taskbar = new Panel { Name = "Taskbar", Position = new Vector2(0, 720 - TaskbarHeight), Size = new Vector2(1280, TaskbarHeight), ZIndex = TaskbarZIndex };
		_taskbar.AddThemeStyleboxOverride("panel", MakePanelStyle(new Color("090f17e8"), new Color("344655"), 0));
		_desktopRoot.AddChild(_taskbar);

		Panel logo = new() { Position = new Vector2(14, 10), Size = new Vector2(26, 26) };
		logo.AddThemeStyleboxOverride("panel", MakePanelStyle(new Color("202b3d"), new Color("7a5f27"), 7));
		_taskbar.AddChild(logo);
		Label logoText = new() { Text = "夜", Position = Vector2.Zero, Size = logo.Size, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center };
		logo.AddChild(logoText);
		_taskbar.AddChild(new Label { Text = "夜航 OS", Position = new Vector2(48, 12), Size = new Vector2(82, 22), Modulate = new Color("949dae") });

		_taskbarItems = new HBoxContainer { Name = "TaskbarItems", Position = new Vector2(138, 6), Size = new Vector2(645, 34) };
		_taskbarItems.AddThemeConstantOverride("separation", 6);
		_taskbar.AddChild(_taskbarItems);
		_playerLabel = new Label { Name = "PlayerName", Position = new Vector2(790, 12), Size = new Vector2(100, 22), HorizontalAlignment = HorizontalAlignment.Center, Visible = false, Modulate = new Color("949dae"), ClipText = true };
		_playerLabel.AddThemeFontSizeOverride("font_size", 12);
		_taskbar.AddChild(_playerLabel);
		_taskbar.AddChild(new Label { Text = "🔋 62%   📶   🔒   🔊", Position = new Vector2(895, 12), Size = new Vector2(180, 22), HorizontalAlignment = HorizontalAlignment.Right, Modulate = new Color("8491a0") });
		_clockLabel = new Label { Name = "Clock", Position = new Vector2(1078, 6), Size = new Vector2(188, 34), HorizontalAlignment = HorizontalAlignment.Right, VerticalAlignment = VerticalAlignment.Center };
		_clockLabel.AddThemeFontSizeOverride("font_size", 13);
		_taskbar.AddChild(_clockLabel);
		RefreshClock();
	}

	/// <summary>创建可同时打开的聊天、白板与浏览器窗口。</summary>
	private void BuildWindows()
	{
		if (_desktopRoot == null) return;
		BuildChatWindow();
		BuildBoardWindow();
		BuildBrowserWindow();
	}

	/// <summary>创建可复用窗口框架，标题栏统一支持拖动、最小化和关闭。</summary>
	private WindowRecord CreateWindowFrame(string id, string title, string subtitle, string glyph, Vector2 position, Vector2 size)
	{
		if (_desktopRoot == null) throw new InvalidOperationException("桌面内容根节点尚未创建。");
		Panel frame = new() { Name = id + "Window", Position = position, Size = size, Visible = false, ZIndex = _windowZ };
		StyleBoxFlat frameStyle = MakePanelStyle(new Color("11192366"), new Color("52697a"), 10);
		frameStyle.ShadowColor = new Color("0000009c");
		frameStyle.ShadowSize = 20;
		frameStyle.ShadowOffset = new Vector2(0, 10);
		frame.AddThemeStyleboxOverride("panel", frameStyle);
		_desktopRoot.AddChild(frame);
		AddWindowGlass(frame, size);

		Panel titleBar = new() { Name = "TitleBar", Size = new Vector2(size.X, 40), MouseFilter = Control.MouseFilterEnum.Stop };
		titleBar.AddThemeStyleboxOverride("panel", MakePanelStyle(new Color("1d2734ee"), new Color("344655"), 8));
		frame.AddChild(titleBar);
		Label titleLabel = new() { Text = glyph + "  " + title, Position = new Vector2(14, 7), Size = new Vector2(Mathf.Max(160, size.X - 210), 26), VerticalAlignment = VerticalAlignment.Center };
		titleLabel.AddThemeFontSizeOverride("font_size", 14);
		titleBar.AddChild(titleLabel);
		titleBar.AddChild(new Label { Text = subtitle, Position = new Vector2(size.X - 180, 10), Size = new Vector2(82, 20), HorizontalAlignment = HorizontalAlignment.Right, Modulate = new Color("738292") });

		Button minimize = MakeWindowActionButton("Minimize", "–", "最小化", new Vector2(size.X - 80, 6));
		minimize.Pressed += () => MinimizeWindow(id);
		titleBar.AddChild(minimize);
		Button close = MakeWindowActionButton("Close", "×", "关闭", new Vector2(size.X - 43, 6));
		close.Pressed += () => CloseWindow(id);
		titleBar.AddChild(close);

		Control body = new() { Name = "Body", Position = new Vector2(0, 40), Size = new Vector2(size.X, size.Y - 40) };
		frame.AddChild(body);
		Button taskbarButton = new() { Text = glyph + "  " + title, CustomMinimumSize = new Vector2(145, 34), Visible = false, FocusMode = Control.FocusModeEnum.None };
		taskbarButton.AddThemeFontSizeOverride("font_size", 12);
		taskbarButton.AddThemeStyleboxOverride("normal", MakePanelStyle(new Color("151e2bcc"), new Color("00000000"), 7));
		taskbarButton.AddThemeStyleboxOverride("hover", MakePanelStyle(new Color("26384dcc"), new Color("65809a"), 7));
		taskbarButton.Pressed += () => ToggleWindow(id);
		_taskbarItems?.AddChild(taskbarButton);
		WindowRecord record = new() { Id = id, Frame = frame, Body = body, TaskbarButton = taskbarButton };
		_windows[id] = record;
		return record;
	}

	/// <summary>统一构造窗口标题栏操作按钮。</summary>
	private static Button MakeWindowActionButton(string nodeName, string text, string tooltip, Vector2 position)
	{
		Button button = new() { Name = nodeName, Text = text, TooltipText = tooltip, Position = position, Size = new Vector2(27, 27), FocusMode = Control.FocusModeEnum.None };
		button.AddThemeStyleboxOverride("normal", MakePanelStyle(new Color("00000000"), new Color("00000000"), 7));
		button.AddThemeStyleboxOverride("hover", MakePanelStyle(nodeName == "Close" ? new Color("e0605c") : new Color("ffffff14"), new Color("00000000"), 7));
		button.AddThemeStyleboxOverride("pressed", MakePanelStyle(nodeName == "Close" ? new Color("c94d49") : new Color("ffffff20"), new Color("00000000"), 7));
		button.AddThemeColorOverride("font_color", new Color("949dae"));
		button.AddThemeColorOverride("font_hover_color", Colors.White);
		return button;
	}

	/// <summary>
	/// 在通用窗口底层放置原生玻璃组件，内容控件保持清晰。
	/// 每个窗口克隆独立样式资源，避免修改一个窗口时影响其他应用。
	/// </summary>
	private static void AddWindowGlass(Panel frame, Vector2 size)
	{
		PackedScene? glassScene = GD.Load<PackedScene>(GlassPanelScenePath);
		if (glassScene?.Instantiate() is not Control glass)
		{
			return;
		}

		glass.Name = "WindowGlass";
		glass.Position = Vector2.Zero;
		glass.Size = size;
		glass.MouseFilter = Control.MouseFilterEnum.Ignore;
		GodotObject? styleObject = glass.Get("surface_style").AsGodotObject();
		if (styleObject is Resource sourceStyle && sourceStyle.Duplicate(true) is Resource style)
		{
			style.Set("corner_radii", new Vector4(10, 10, 10, 10));
			style.Set("fill_color", new Color("111923b3"));
			style.Set("border_color", new Color("9bb7cf38"));
			style.Set("shadow_color", new Color("00000070"));
			style.Set("shadow_offset", new Vector2(0, 8));
			style.Set("shadow_softness", 12.0);
			glass.Set("surface_style", style);
		}
		glass.Set("blur_radius", 10.0);
		glass.Set("saturation", 1.08);
		glass.Set("brightness", 1.0);
		frame.AddChild(glass);
		frame.MoveChild(glass, 0);
	}

	/// <summary>创建暗线窗口的头像、聊天消息列表和输入栏。</summary>
	private void BuildChatWindow()
	{
		WindowRecord record = CreateWindowFrame("chat", "暗线 · 林越", "ENCRYPTED", "💬", new Vector2(96, 34), new Vector2(470, 630));
		Control body = record.Body;
		Panel header = new() { Position = Vector2.Zero, Size = new Vector2(body.Size.X, 64) };
		header.AddThemeStyleboxOverride("panel", MakePanelStyle(new Color("141924"), new Color("2a3242"), 0));
		body.AddChild(header);
		header.AddChild(MakeAvatar(LinyueAvatarPath, new Vector2(16, 12), new Vector2(38, 38)));
		Label person = new() { Text = "林越 · 徒弟", Position = new Vector2(66, 10), Size = new Vector2(180, 22) };
		person.AddThemeFontSizeOverride("font_size", 15);
		header.AddChild(person);
		_statusLabel = new Label { Text = "● 在线", Position = new Vector2(66, 34), Size = new Vector2(120, 18), Modulate = new Color("54b183") };
		_statusLabel.AddThemeFontSizeOverride("font_size", 11);
		header.AddChild(_statusLabel);

		float inputTop = body.Size.Y - 76;
		ScrollContainer scroll = new() { Name = "ChatScroll", Position = new Vector2(0, 64), Size = new Vector2(body.Size.X, inputTop - 64), HorizontalScrollMode = ScrollContainer.ScrollMode.Disabled };
		scroll.AddThemeStyleboxOverride("panel", MakePanelStyle(new Color("12161e"), new Color("00000000"), 0));
		body.AddChild(scroll);
		_chatMessages = new VBoxContainer { Name = "ChatMessages", CustomMinimumSize = new Vector2(body.Size.X - 26, 0), SizeFlagsHorizontal = Control.SizeFlags.ExpandFill };
		_chatMessages.AddThemeConstantOverride("separation", 10);
		scroll.AddChild(_chatMessages);

		Panel inputPanel = new() { Position = new Vector2(0, inputTop), Size = new Vector2(body.Size.X, 76) };
		inputPanel.AddThemeStyleboxOverride("panel", MakePanelStyle(new Color("141924"), new Color("2a3242"), 0));
		body.AddChild(inputPanel);
		_chatInput = new LineEdit { PlaceholderText = "跟林越说话…", Position = new Vector2(12, 17), Size = new Vector2(body.Size.X - 114, 42), MaxLength = 240 };
		_chatInput.AddThemeFontSizeOverride("font_size", 14);
		ApplyInputStyle(_chatInput);
		inputPanel.AddChild(_chatInput);
		_chatInput.TextSubmitted += OnChatSubmitted;
		Button send = new() { Text = "发送", Position = new Vector2(body.Size.X - 94, 17), Size = new Vector2(82, 42), FocusMode = Control.FocusModeEnum.None };
		ApplyPrimaryButtonStyle(send);
		send.Pressed += SubmitChatInput;
		inputPanel.AddChild(send);
	}

	/// <summary>创建白板窗口，任务区和自由区继续使用原生拖放。</summary>
	private void BuildBoardWindow()
	{
		WindowRecord record = CreateWindowFrame("board", "白板", "BOARD", "🗒️", new Vector2(148, 86), new Vector2(920, 630));
		ScrollContainer scroll = new() { Position = new Vector2(12, 12), Size = new Vector2(record.Body.Size.X - 24, record.Body.Size.Y - 24), HorizontalScrollMode = ScrollContainer.ScrollMode.Disabled };
		record.Body.AddChild(scroll);
		_boardContent = new VBoxContainer { Name = "BoardContent", CustomMinimumSize = new Vector2(870, 520) };
		_boardContent.AddThemeConstantOverride("separation", 12);
		scroll.AddChild(_boardContent);
	}

	/// <summary>先创建浏览器窗口框架，CEF 控件在第一次打开时才初始化。</summary>
	private void BuildBrowserWindow()
	{
		WindowRecord record = CreateWindowFrame("browser", "夜航浏览器", "SEARCH", "🌐", new Vector2(122, 60), new Vector2(860, 630));
		_browserWindow = record.Frame;
		_browserBody = record.Body;
	}

	/// <summary>构造网页原型开机登录遮罩，使用独立顶层遮罩保持层级效果。</summary>
	private void BuildLockScreen()
	{
		_lockOverlay = new Panel { Name = "LockScreen", Size = new Vector2(1280, 720), ZIndex = ModalZIndex, MouseFilter = Control.MouseFilterEnum.Stop };
		_lockOverlay.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
		_lockOverlay.AddThemeStyleboxOverride("panel", MakePanelStyle(new Color("00000000"), new Color("00000000"), 0));
		AddChild(_lockOverlay);

		// 登录遮罩自己持有壁纸与渐变，不在桌面颜色上再叠一块近乎不透明的黑底。
		TextureRect wallpaper = new()
		{
			Name = "LockWallpaper",
			Texture = LoadTexture(BackgroundPath),
			ExpandMode = TextureRect.ExpandModeEnum.IgnoreSize,
			StretchMode = TextureRect.StretchModeEnum.KeepAspectCovered,
			MouseFilter = Control.MouseFilterEnum.Ignore,
		};
		wallpaper.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
		_lockOverlay.AddChild(wallpaper);
		ColorRect shade = new() { Name = "LockShade", Color = Colors.White, Material = MakeShadeMaterial(LockShadeShaderCode), MouseFilter = Control.MouseFilterEnum.Ignore };
		shade.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
		_lockOverlay.AddChild(shade);

		Label time = new() { Text = "15:20", Position = new Vector2(0, 170), Size = new Vector2(1280, 64), HorizontalAlignment = HorizontalAlignment.Center };
		time.AddThemeFontSizeOverride("font_size", 54);
		_lockOverlay.AddChild(time);
		_lockOverlay.AddChild(new Label { Text = "10月16日 星期五 · 夜航 OS 3.1", Position = new Vector2(0, 240), Size = new Vector2(1280, 24), HorizontalAlignment = HorizontalAlignment.Center, Modulate = new Color("949dae") });

		Panel card = new() { Position = new Vector2(490, 292), Size = new Vector2(300, 226) };
		card.AddThemeStyleboxOverride("panel", MakePanelStyle(new Color("0e1219d1"), new Color("2a3242"), 14));
		_lockOverlay.AddChild(card);
		Panel logo = new() { Position = new Vector2(124, 20), Size = new Vector2(52, 52) };
		logo.AddThemeStyleboxOverride("panel", MakePanelStyle(new Color("d9a441"), new Color("d9a441"), 14));
		card.AddChild(logo);
		Label logoText = new() { Text = "夜", Size = logo.Size, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center };
		logoText.AddThemeFontSizeOverride("font_size", 26);
		logoText.AddThemeColorOverride("font_color", new Color("171208"));
		logo.AddChild(logoText);
		_loginName = new LineEdit { Name = "LoginName", PlaceholderText = "输入你的名字", Position = new Vector2(26, 88), Size = new Vector2(248, 42), Alignment = HorizontalAlignment.Center, MaxLength = 12 };
		ApplyInputStyle(_loginName);
		card.AddChild(_loginName);
		Button unlock = new() { Name = "UnlockButton", Text = "解锁进入", Position = new Vector2(26, 142), Size = new Vector2(248, 42) };
		ApplyPrimaryButtonStyle(unlock);
		unlock.Pressed += UnlockDesktop;
		card.AddChild(unlock);
		_loginError = new Label { Text = "总得有个名字，这台电脑才认得你。", Position = new Vector2(16, 190), Size = new Vector2(268, 30), HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center, Modulate = new Color("e0605c"), Visible = false };
		_loginError.AddThemeFontSizeOverride("font_size", 12);
		card.AddChild(_loginError);
		_welcomeLabel = new Label { Name = "Welcome", Text = "", Position = new Vector2(0, 536), Size = new Vector2(1280, 28), HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center, Modulate = new Color("d6dce6"), Visible = false };
		_lockOverlay.AddChild(_welcomeLabel);
		ColorRect glass = new()
		{
			Name = "LockGlassOverlay",
			Color = Colors.White,
			Material = MakeShadeMaterial(LockGlassShaderCode),
			MouseFilter = Control.MouseFilterEnum.Ignore,
		};
		glass.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
		_lockOverlay.AddChild(glass);
		_loginName.TextSubmitted += _ => UnlockDesktop();
		_loginName.CallDeferred(Control.MethodName.GrabFocus);
	}

	/// <summary>加载原生 C++ 状态机并从 JSON 注入序幕内容。</summary>
	private void CreateState()
	{
		EnsureNativeExtension();
		GodotObject? stateObject = ClassDB.Instantiate("NVPrologueState").AsGodotObject();
		_state = stateObject as Node;
		if (_state == null)
		{
			ShowFatalError("无法创建序章状态机。");
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
		RefreshAllViews();
	}

	/// <summary>在加载自定义资源场景前注册 C++ 类型，确保 CRT 视口与剧情状态都可用。</summary>
	private void EnsureNativeExtension()
	{
		// 已由项目扩展扫描器注册时直接复用，避免重复加载不可热重载的动态库。
		if (ClassDB.ClassExists("NVScreenEffectViewport") && ClassDB.ClassExists("NVPrologueState")) return;
		// GDExtensionManager 接收描述文件；Godot 再依据当前平台选择对应动态库。
		string absolutePath = ProjectSettings.GlobalizePath(NativeExtensionPath);
		GDExtensionManager.LoadExtension(absolutePath);
	}

	/// <summary>登录成功后开始序幕，打开暗线窗口并显示与开场对白一致的通知。</summary>
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
		if (_lockOverlay != null)
		{
			Tween tween = CreateTween();
			tween.TweenProperty(_lockOverlay, "modulate:a", 0.0f, 0.55f).SetTrans(Tween.TransitionType.Cubic).SetEase(Tween.EaseType.Out);
			tween.TweenCallback(Callable.From(() => _lockOverlay?.QueueFree()));
		}
		Dictionary result = DispatchAction("begin", new Dictionary());
		ShowWindow("chat");
		string firstLine = result.ContainsKey("say") && result["say"].AsGodotArray().Count > 0
			? result["say"].AsGodotArray()[0].AsString() : "林越发来一条新消息。";
		ShowToast("林越 · 新消息", firstLine);
	}

	/// <summary>打开窗口并显示对应任务栏按钮；其它窗口仍留在桌面上。</summary>
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
			// 窗口打开时复现网页的淡入和轻微缩放，恢复窗口也沿用同一节奏。
			if (_windowTweens.TryGetValue(id, out Tween? oldTween) && oldTween.IsRunning()) oldTween.Kill();
			record.Frame.Modulate = new Color(1, 1, 1, 0);
			record.Frame.Scale = new Vector2(0.96f, 0.96f);
			record.Frame.PivotOffset = record.Frame.Size * 0.5f;
			Tween tween = CreateTween().SetParallel(true);
			tween.TweenProperty(record.Frame, "modulate:a", 1.0f, 0.26f).SetTrans(Tween.TransitionType.Cubic).SetEase(Tween.EaseType.Out);
			tween.TweenProperty(record.Frame, "scale", Vector2.One, 0.26f).SetTrans(Tween.TransitionType.Cubic).SetEase(Tween.EaseType.Out);
			_windowTweens[id] = tween;
		}
		if (id == "browser") CreateBrowser();
		if (id == "board") RebuildBoard();
		if (id == "chat") CallDeferred(MethodName.FocusChatInput);
	}

	/// <summary>关闭窗口但保留实例和应用状态。</summary>
	private void CloseWindow(string id)
	{
		if (!_windows.TryGetValue(id, out WindowRecord? record)) return;
		if (_windowTweens.TryGetValue(id, out Tween? tween) && tween.IsRunning()) tween.Kill();
		record.Frame.Visible = false;
		record.Frame.Modulate = Colors.White;
		record.Frame.Scale = Vector2.One;
		record.Minimized = false;
		record.TaskbarButton.Visible = false;
	}

	/// <summary>最小化窗口但在任务栏保留恢复入口。</summary>
	private void MinimizeWindow(string id)
	{
		if (!_windows.TryGetValue(id, out WindowRecord? record)) return;
		if (_windowTweens.TryGetValue(id, out Tween? tween) && tween.IsRunning()) tween.Kill();
		record.Frame.Visible = false;
		record.Frame.Modulate = Colors.White;
		record.Frame.Scale = Vector2.One;
		record.Minimized = true;
		record.TaskbarButton.Visible = true;
	}

	/// <summary>点击任务栏窗口项，在恢复和最小化之间切换。</summary>
	private void ToggleWindow(string id)
	{
		if (!_windows.TryGetValue(id, out WindowRecord? record)) return;
		if (record.Frame.Visible) MinimizeWindow(id);
		else ShowWindow(id);
	}

	/// <summary>把窗口提升到最前层，并标记当前任务栏项。</summary>
	private void FocusWindow(string id)
	{
		if (!_windows.TryGetValue(id, out WindowRecord? record)) return;
		if (_windowZ >= MaximumWindowZIndex)
		{
			// 长时间反复切换窗口时压缩层级，避免 ZIndex 超出 Godot 的有效范围。
			_windowZ = 18;
			foreach (WindowRecord ordered in _windows.Values.OrderBy(item => item.Frame.ZIndex))
			{
				ordered.Frame.ZIndex = _windowZ += 2;
			}
		}
		_windowZ += 2;
		record.Frame.ZIndex = _windowZ;
		foreach (WindowRecord other in _windows.Values)
		{
			bool active = other.Id == id;
			other.TaskbarButton.Modulate = active ? Colors.White : new Color("b4bec9");
			other.TaskbarButton.AddThemeStyleboxOverride("normal", MakePanelStyle(
				active ? new Color("26384dcc") : new Color("151e2bcc"),
				active ? new Color("65809a") : new Color("00000000"), 7));
		}
	}

	/// <summary>根据层级查找鼠标下的最顶层窗口。</summary>
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

	/// <summary>限制窗口可拖动范围，至少留下九十像素标题栏可供找回。</summary>
	private static Vector2 ClampWindowPosition(Control window, Vector2 position)
	{
		return new Vector2(Mathf.Clamp(position.X, -window.Size.X + 90, 1280 - 90), Mathf.Clamp(position.Y, 0, 720 - TaskbarHeight - 40));
	}

	/// <summary>首次打开时实例化 CEF 页面并连接 IPC。</summary>
	private void CreateBrowser()
	{
		if (_browserCreated || _browserBody == null) return;
		_browserCreated = true;
		Panel host = new() { Position = new Vector2(8, 8), Size = _browserBody.Size - new Vector2(16, 16) };
		host.AddThemeStyleboxOverride("panel", MakePanelStyle(new Color("0b1119"), new Color("344655"), 5));
		_browserBody.AddChild(host);
		if (!ClassDB.CanInstantiate("CefTexture")) GDExtensionManager.LoadExtension(ProjectSettings.GlobalizePath(BrowserExtensionPath));
		if (ClassDB.CanInstantiate("CefTexture")) _browser = ClassDB.Instantiate("CefTexture").AsGodotObject() as Control;
		if (_browser == null)
		{
			_fallbackBrowser = true;
			host.AddChild(new Label { Text = "CEF 网页扩展尚未加载。\n请确认 addons/godot_cef/bin 中有本机运行库。", HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center, Size = host.Size });
			return;
		}
		_browser.Set("enable_accelerated_osr", false);
		_browser.Set("url", BrowserPagePath);
		_browser.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
		_browser.Connect("load_finished", Callable.From<string, int>(OnBrowserLoadFinished));
		_browser.Connect("ipc_message", Callable.From<string>(OnBrowserIpcMessage));
		host.AddChild(_browser);
	}

	/// <summary>记录 CEF 页面成功加载状态。</summary>
	private void OnBrowserLoadFinished(string url, int statusCode)
	{
		if (statusCode >= 200 && statusCode < 400) _cefReady = true;
	}

	/// <summary>只接受网页提交的结构化动作，不采信网页侧状态字段。</summary>
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
		if (Get(message, "type", "").AsString() != "action") return;
		string action = Get(message, "action", "").AsString();
		Dictionary payload = message.ContainsKey("payload") ? message["payload"].AsGodotDictionary() : new Dictionary();
		if (action == "preview_image")
		{
			ShowImagePreview(Get(payload, "page", "article").AsString());
			return;
		}
		DispatchAction(action, payload);
	}

	/// <summary>向网页发送完整 C++ 快照，供页面渲染当前搜索结果或文章。</summary>
	private void SendSnapshotToBrowser()
	{
		if (_browser == null || _fallbackBrowser) return;
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

	/// <summary>状态变化后刷新本地投影；浏览器可见时立即同步新快照。</summary>
	private void OnStateChanged(Dictionary snapshot)
	{
		_snapshot = snapshot;
		RefreshAllViews();
		if (_browser != null && _browserWindow?.Visible == true) SendSnapshotToBrowser();
	}

	/// <summary>执行状态机动作、回显玩家输入并顺序播放状态机返回的对白。</summary>
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
		if (Get(result, "code", "").AsString() == "location_confirmed") CompleteRecoveryAfterDelay();
		return result;
	}

	/// <summary>确保连续对白使用同一队列播放，避免新回复插队。</summary>
	private void PumpDialogueQueue()
	{
		if (_dialogueQueueRunning || !IsInsideTree()) return;
		_dialogueQueueRunning = true;
		PlayDialogueQueue();
	}

	/// <summary>按原型节奏逐条显示林越消息。</summary>
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

	/// <summary>收到准确地点后等待短暂行程，再让状态机结算交接。</summary>
	private async void CompleteRecoveryAfterDelay()
	{
		int generation = _dialogueGeneration;
		await ToSignal(GetTree().CreateTimer(1.4), SceneTreeTimer.SignalName.Timeout);
		if (IsInsideTree() && generation == _dialogueGeneration) DispatchAction("finish_recovery", new Dictionary());
	}

	/// <summary>更新白板、完成状态和任务栏时钟。</summary>
	private void RefreshAllViews()
	{
		RefreshClock();
		if (_statusLabel != null)
		{
			bool complete = Get(_snapshot, "complete", false).AsBool();
			_statusLabel.Text = complete ? "● 已完成" : "● 在线";
			_statusLabel.Modulate = complete ? new Color("8dd9a8") : new Color("54b183");
		}
		if (_boardContent != null) RebuildBoard();
	}

	/// <summary>显示原型桌面的游戏内时间与日期；当前找猫序幕在一晚内固定为十五点二十分。</summary>
	private void RefreshClock()
	{
		if (_clockLabel == null) return;
		_clockLabel.Text = "15:20\n10月16日 星期五";
	}

	private void OpenChat() => ShowWindow("chat");
	private void OpenBrowser() { ShowWindow("browser"); if (_browser != null) SendSnapshotToBrowser(); }
	private void OpenBoard() => ShowWindow("board");

	/// <summary>重开序幕时使旧对白和延迟交接失效，避免旧回调污染新进度。</summary>
	private void ResetPrologue()
	{
		if (_state == null) return;
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
		if (_unlocked) DispatchAction("begin", new Dictionary());
	}

	/// <summary>提交聊天输入，地点识别和流程判断仍由 C++ 决定。</summary>
	private void SubmitChatInput()
	{
		if (_chatInput == null || _chatInput.Text.Trim().Length == 0) return;
		string text = _chatInput.Text.Trim();
		_chatInput.Clear();
		DispatchAction("chat", new Dictionary { ["text"] = text });
	}

	private void OnChatSubmitted(string text) => SubmitChatInput();
	private void FocusChatInput() => _chatInput?.GrabFocus();

	/// <summary>以头像、名字和正文气泡呈现一条消息。</summary>
	private void AppendChat(string speaker, string message, bool fromPlayer)
	{
		if (_chatMessages == null) return;
		// 依据中文字符数预留换行高度，避免原型中的长消息被固定高度气泡截断。
		int visualLines = message.Split('\n').Sum(line => Math.Max(1, (int)Math.Ceiling(line.Length / 22.0)));
		int bubbleHeight = Math.Max(62, 28 + visualLines * 23);
		HBoxContainer row = new() { CustomMinimumSize = new Vector2(444, Math.Max(64, bubbleHeight)), SizeFlagsHorizontal = Control.SizeFlags.ExpandFill, Alignment = fromPlayer ? BoxContainer.AlignmentMode.End : BoxContainer.AlignmentMode.Begin };
		Control avatar = fromPlayer ? MakePlayerAvatar(speaker) : MakeAvatar(LinyueAvatarPath, Vector2.Zero, new Vector2(34, 34));
		Panel bubble = new() { CustomMinimumSize = new Vector2(326, bubbleHeight), SizeFlagsHorizontal = Control.SizeFlags.ShrinkBegin };
		bubble.AddThemeStyleboxOverride("panel", MakePanelStyle(fromPlayer ? new Color("20283add") : new Color("1b2331dd"), new Color("33405a"), 10));
		VBoxContainer content = new() { Position = new Vector2(12, 6), Size = new Vector2(300, bubbleHeight - 12), MouseFilter = Control.MouseFilterEnum.Ignore };
		content.AddThemeConstantOverride("separation", 2);
		content.AddChild(new Label { Text = speaker, Modulate = new Color("d9a441"), MouseFilter = Control.MouseFilterEnum.Ignore });
		content.AddChild(new Label { Text = message, AutowrapMode = TextServer.AutowrapMode.WordSmart, CustomMinimumSize = new Vector2(296, visualLines * 22), MouseFilter = Control.MouseFilterEnum.Ignore });
		bubble.AddChild(content);
		if (fromPlayer) { row.AddChild(bubble); row.AddChild(avatar); }
		else { row.AddChild(avatar); row.AddChild(bubble); }
		_chatMessages.AddChild(row);
		CallDeferred(MethodName.ScrollChatToBottom);
	}

	/// <summary>创建头像纹理控件，缺素材时仍保留固定布局尺寸。</summary>
	private static TextureRect MakeAvatar(string path, Vector2 position, Vector2 size)
	{
		return new TextureRect { Texture = LoadTexture(path), Position = position, Size = size, CustomMinimumSize = size, ExpandMode = TextureRect.ExpandModeEnum.IgnoreSize, StretchMode = TextureRect.StretchModeEnum.KeepAspectCovered };
	}

	/// <summary>创建玩家消息使用的固定尺寸首字头像。</summary>
	private static Control MakePlayerAvatar(string playerName)
	{
		string initial = playerName.Length > 0 ? playerName[..1] : "我";
		return new Label { Text = initial, CustomMinimumSize = new Vector2(34, 34), Size = new Vector2(34, 34), HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center, Modulate = new Color("9fb4d0") };
	}

	/// <summary>延迟到容器完成布局后滚动至聊天列表底部。</summary>
	private void ScrollChatToBottom()
	{
		if (_chatMessages?.GetParent() is ScrollContainer scroll) scroll.SetDeferred("scroll_vertical", int.MaxValue);
	}

	/// <summary>从状态快照重建任务树和线索卡自由区。</summary>
	private void RebuildBoard()
	{
		if (_boardContent == null || !_snapshot.ContainsKey("quest")) return;
		foreach (Node child in _boardContent.GetChildren()) child.QueueFree();
		Dictionary quest = _snapshot["quest"].AsGodotDictionary();
		Label heading = new() { Text = "今晚要办的事 · " + Get(quest, "title", "找到年糕").AsString(), Modulate = new Color("f0c66d") };
		heading.AddThemeFontSizeOverride("font_size", 18);
		_boardContent.AddChild(heading);
		bool complete = Get(quest, "complete", false).AsBool();
		if (complete)
		{
			Label finished = new() { Text = "年糕已经平安回家 · 任务完成", Modulate = new Color("8dd9a8") };
			finished.AddThemeFontSizeOverride("font_size", 17);
			_boardContent.AddChild(finished);
		}
		else if (quest.ContainsKey("tasks"))
		{
			foreach (Variant taskValue in quest["tasks"].AsGodotArray())
			{
				Dictionary task = taskValue.AsGodotDictionary();
				string taskId = Get(task, "id", "").AsString();
				TaskDropZone zone = new(taskId, Get(task, "title", "").AsString(), Get(task, "done", false).AsBool(), AttachClue);
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
		FreeDropZone loose = new(DetachClue) { CustomMinimumSize = new Vector2(850, 205), SizeFlagsVertical = Control.SizeFlags.ExpandFill };
		_boardContent.AddChild(loose);
		if (_snapshot.ContainsKey("clues"))
		{
			foreach (Variant value in _snapshot["clues"].AsGodotArray())
			{
				Dictionary clue = value.AsGodotDictionary();
				if (Get(clue, "task_id", "").AsString().Length == 0) loose.AddCard(new ClueCard(clue));
			}
		}
		if (!complete && Get(_snapshot, "quest_ready", false).AsBool())
		{
			_boardContent.AddChild(new Label { Text = "两条子任务已经对上，回到暗线向林越报告位置。", Modulate = new Color("8dd9a8") });
		}
	}

	/// <summary>从快照中按线索 ID 创建卡片副本供任务区显示。</summary>
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

	private void AttachClue(string clueId, string taskId) => DispatchAction("attach_clue", new Dictionary { ["clue_id"] = clueId, ["task_id"] = taskId });
	private void DetachClue(string clueId) => DispatchAction("detach_clue", new Dictionary { ["clue_id"] = clueId });

	/// <summary>展示灯箱图片，只有点击收录按钮才会把图片线索交给状态机。</summary>
	private void ShowImagePreview(string page)
	{
		_imageOverlay?.QueueFree();
		_imageOverlay = new Panel { Name = "ImageLightbox", Size = new Vector2(1280, 720), ZIndex = ModalZIndex, MouseFilter = Control.MouseFilterEnum.Stop };
		_imageOverlay.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
		_imageOverlay.AddThemeStyleboxOverride("panel", MakePanelStyle(new Color("030509df"), new Color("030509"), 0));
		AddChild(_imageOverlay);
		Panel card = new() { Position = new Vector2(250, 70), Size = new Vector2(780, 580) };
		card.AddThemeStyleboxOverride("panel", MakePanelStyle(new Color("111923fc"), new Color("d9a441"), 10));
		_imageOverlay.AddChild(card);
		_imageOverlay.GuiInput += @event =>
		{
			if (@event is InputEventMouseButton { Pressed: true, ButtonIndex: MouseButton.Left } click &&
				!card.GetGlobalRect().HasPoint(click.Position)) CloseImageOverlay();
		};
		string path = page == "forum_post" ? "res://content/assets/prop_cat.png" : "res://content/assets/prop_cat_home.png";
		card.AddChild(new TextureRect { Texture = LoadTexture(path), Position = new Vector2(26, 24), Size = new Vector2(728, 440), ExpandMode = TextureRect.ExpandModeEnum.IgnoreSize, StretchMode = TextureRect.StretchModeEnum.KeepAspectCentered });
		Button collect = new() { Text = "📌 收进白板", Position = new Vector2(470, 500), Size = new Vector2(135, 42) };
		ApplyPrimaryButtonStyle(collect);
		collect.Pressed += () => { DispatchAction("collect_clue", new Dictionary { ["page"] = page, ["kind"] = "image" }); CloseImageOverlay(); };
		card.AddChild(collect);
		Button close = new() { Text = "关闭", Position = new Vector2(620, 500), Size = new Vector2(110, 42) };
		ApplySecondaryButtonStyle(close);
		close.Pressed += CloseImageOverlay;
		card.AddChild(close);
	}

	/// <summary>关闭灯箱并清空引用。</summary>
	private void CloseImageOverlay()
	{
		_imageOverlay?.QueueFree();
		_imageOverlay = null;
	}

	/// <summary>在桌面右下角显示开场消息通知，数秒后自动收起。</summary>
	private async void ShowToast(string title, string message)
	{
		if (_toastContainer == null) return;
		Panel toast = new() { CustomMinimumSize = new Vector2(290, 78), MouseFilter = Control.MouseFilterEnum.Stop };
		StyleBoxFlat toastStyle = MakePanelStyle(new Color("161b24f5"), new Color("2a3242"), 9);
		toastStyle.BorderWidthLeft = 3;
		toastStyle.BorderColor = new Color("d9a441");
		toast.AddThemeStyleboxOverride("panel", toastStyle);
		VBoxContainer content = new() { Position = new Vector2(12, 8), Size = new Vector2(264, 62), MouseFilter = Control.MouseFilterEnum.Ignore };
		content.AddChild(new Label { Text = title, Modulate = new Color("d9a441") });
		content.AddChild(new Label { Text = message, AutowrapMode = TextServer.AutowrapMode.WordSmart, CustomMinimumSize = new Vector2(260, 38) });
		toast.AddChild(content);
		toast.GuiInput += @event =>
		{
			if (@event is InputEventMouseButton { Pressed: true, ButtonIndex: MouseButton.Left }) ShowWindow("chat");
		};
		_toastContainer.AddChild(toast);
		toast.Modulate = new Color(1, 1, 1, 0);
		CreateTween().TweenProperty(toast, "modulate:a", 1.0f, 0.35f).SetTrans(Tween.TransitionType.Cubic).SetEase(Tween.EaseType.Out);
		await ToSignal(GetTree().CreateTimer(3.2), SceneTreeTimer.SignalName.Timeout);
		if (IsInstanceValid(toast)) toast.QueueFree();
	}

	/// <summary>从 Godot 无类型 Dictionary 读取字段并提供默认值。</summary>
	private static Variant Get(Dictionary dictionary, string key, Variant fallback) => dictionary.ContainsKey(key) ? dictionary[key] : fallback;

	/// <summary>直接读取 PNG 资源，避免首次导入前桌面素材暂时不可见。</summary>
	private static Texture2D? LoadTexture(string path)
	{
		byte[] bytes = FileAccess.GetFileAsBytes(path);
		Image image = new();
		if (image.LoadPngFromBuffer(bytes) != Error.Ok || image.IsEmpty()) return null;
		return ImageTexture.CreateFromImage(image);
	}

	/// <summary>集中定义可复用的深色面板样式。</summary>
	private static StyleBoxFlat MakePanelStyle(Color background, Color border, int radius)
	{
		return new StyleBoxFlat
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
	}

	/// <summary>用已编译着色器创建全屏色调遮罩，避免堆叠多层半透明控件。</summary>
	private static ShaderMaterial MakeShadeMaterial(string shaderCode)
	{
		return new ShaderMaterial { Shader = new Shader { Code = shaderCode } };
	}

	/// <summary>统一登录框与聊天输入框的深色底、细边和琥珀色焦点状态。</summary>
	private static void ApplyInputStyle(LineEdit input)
	{
		input.AddThemeStyleboxOverride("normal", MakePanelStyle(new Color("0a0d12"), new Color("2a3242"), 9));
		input.AddThemeStyleboxOverride("focus", MakePanelStyle(new Color("0a0d12"), new Color("7a5f27"), 9));
		input.AddThemeColorOverride("font_color", new Color("e9ecf2"));
		input.AddThemeColorOverride("font_placeholder_color", new Color("666f80"));
		input.AddThemeColorOverride("caret_color", new Color("d9a441"));
	}

	/// <summary>把主要确认动作统一为原型的琥珀底色和深色文字。</summary>
	private static void ApplyPrimaryButtonStyle(Button button)
	{
		button.AddThemeStyleboxOverride("normal", MakePanelStyle(new Color("d9a441"), new Color("d9a441"), 8));
		button.AddThemeStyleboxOverride("hover", MakePanelStyle(new Color("e6b357"), new Color("e6b357"), 8));
		button.AddThemeStyleboxOverride("pressed", MakePanelStyle(new Color("c58f32"), new Color("c58f32"), 8));
		button.AddThemeColorOverride("font_color", new Color("171208"));
		button.AddThemeColorOverride("font_hover_color", new Color("171208"));
		button.AddThemeColorOverride("font_pressed_color", new Color("171208"));
	}

	/// <summary>统一构造不抢视觉焦点的次要操作按钮。</summary>
	private static void ApplySecondaryButtonStyle(Button button)
	{
		button.AddThemeStyleboxOverride("normal", MakePanelStyle(new Color("161b24"), new Color("2a3242"), 8));
		button.AddThemeStyleboxOverride("hover", MakePanelStyle(new Color("222a39"), new Color("3d4859"), 8));
		button.AddThemeColorOverride("font_color", new Color("e9ecf2"));
		button.AddThemeColorOverride("font_hover_color", new Color("e9ecf2"));
	}

	/// <summary>初始化失败时显示明确错误，避免状态机缺失造成静默空白。</summary>
	private void ShowFatalError(string message)
	{
		Label error = new() { Text = message, Position = new Vector2(120, 80), Size = new Vector2(900, 120), Modulate = new Color("ffb6a8") };
		error.AddThemeFontSizeOverride("font_size", 22);
		AddChild(error);
	}

	/// <summary>线索卡只传稳定 ID，业务正确性由 C++ 状态机判断。</summary>
	private sealed partial class ClueCard : PanelContainer
	{
		private readonly Dictionary _clue;
		public ClueCard(Dictionary clue)
		{
			_clue = clue;
			CustomMinimumSize = new Vector2(270, 96);
			AddThemeStyleboxOverride("panel", MakePanelStyle(new Color("202c38"), new Color("b58a3c"), 5));
			VBoxContainer content = new() { MouseFilter = Control.MouseFilterEnum.Ignore };
			content.AddChild(new Label { Text = PrologueMain.Get(clue, "source", "线索").AsString(), Modulate = new Color("e7bd67") });
			string clueText = PrologueMain.Get(clue, "text", "").AsString();
			content.AddChild(new Label { Text = String.IsNullOrEmpty(clueText) ? "照片线索" : clueText, AutowrapMode = TextServer.AutowrapMode.WordSmart });
			AddChild(content);
		}
		public override Variant _GetDragData(Vector2 atPosition)
		{
			SetDragPreview(new Label { Text = PrologueMain.Get(_clue, "source", "线索").AsString(), Modulate = new Color("ffe4a2") });
			return new Dictionary { ["clue_id"] = PrologueMain.Get(_clue, "id", "").AsString() };
		}
	}

	/// <summary>任务投放区把线索 ID 和任务 ID 交给外层状态机入口。</summary>
	private sealed partial class TaskDropZone : PanelContainer
	{
		private readonly string _taskId;
		private readonly VBoxContainer _cards;
		private readonly Action<string, string> _attach;
		public TaskDropZone(string taskId, string title, bool done, Action<string, string> attach)
		{
			_taskId = taskId;
			_attach = attach;
			CustomMinimumSize = new Vector2(850, 112);
			AddThemeStyleboxOverride("panel", MakePanelStyle(new Color("17232e"), done ? new Color("4c9673") : new Color("795f31"), 5));
			VBoxContainer layout = new() { MouseFilter = Control.MouseFilterEnum.Ignore };
			layout.AddChild(new Label { Text = done ? "已完成　" + title : title, Modulate = done ? new Color("8dd9a8") : new Color("f0c66d") });
			_cards = new VBoxContainer { MouseFilter = Control.MouseFilterEnum.Ignore };
			layout.AddChild(_cards);
			AddChild(layout);
		}
		public void AddCard(ClueCard card) => _cards.AddChild(card);
		public override bool _CanDropData(Vector2 atPosition, Variant data) => data.VariantType == Variant.Type.Dictionary && data.AsGodotDictionary().ContainsKey("clue_id");
		public override void _DropData(Vector2 atPosition, Variant data) => _attach(PrologueMain.Get(data.AsGodotDictionary(), "clue_id", "").AsString(), _taskId);
	}

	/// <summary>白板自由区允许线索卡从任务下拖回。</summary>
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
		public override bool _CanDropData(Vector2 atPosition, Variant data) => data.VariantType == Variant.Type.Dictionary && data.AsGodotDictionary().ContainsKey("clue_id");
		public override void _DropData(Vector2 atPosition, Variant data) => _detach(PrologueMain.Get(data.AsGodotDictionary(), "clue_id", "").AsString());
	}
}
