#nullable enable

using Godot;
using Godot.Collections;
using System;
using System.Threading.Tasks;

/// <summary>
/// 从 main.tscn 的真实 CRT 桌面进入，覆盖窗口叠放后的输入、隐藏再打开的白板布局，
/// 以及实际中文换行后的连续聊天消息高度；不绕过根视口到子视口的输入链。
/// </summary>
public partial class DesktopUiRegressionTest : Node
{
	private PrologueMain _main = null!;
	private Control _desktop = null!;
	private SubViewport _contentViewport = null!;
	private BoardCanvasView _canvas = null!;
	private bool _failed;
	private Vector2 _lastPointer;

	/// <summary>打开真实应用窗口，完成异步布局后依次验证三个回归问题。</summary>
	public override async void _Ready()
	{
		try
		{
			_main = GD.Load<PackedScene>("res://main.tscn").Instantiate<PrologueMain>();
			AddChild(_main);
			// 走真实解锁路径，验证登录遮罩释放后窗口标题栏仍能收到拖动输入。
			_main.GetNode<LineEdit>("LockScreen/LoginCard/LoginName").Text = "验收玩家";
			_main.Call("UnlockDesktop");
			await Delay(0.8);
			_desktop = _main.GetNode<Control>("PrologueDesktop/CRTViewport/ContentViewport/Content/DesktopContent");
			_contentViewport = _desktop.GetViewport() as SubViewport ?? throw new InvalidOperationException("桌面没有使用预期的子视口。");
			Panel chat = Window("chat");
			Panel board = Window("board");
			_canvas = board.GetNode<BoardCanvasView>("Body/BoardContentRoot/BoardCanvas");
			// 解锁流程已经打开暗线并启动对白，后续只等待真实队列完成。
			await Delay(2.5);
			ValidateChatRows();

			_main.Call("ShowWindow", "board");
			await Delay(0.4);
			ValidateTree("白板首次从隐藏状态打开");
			await Capture("desktop-board-regression.png");

			// 点击白板外露出的暗线标题栏，再在两窗口重叠的消息区拖动与滚轮。
			await Click(new Vector2(260, 52));
			Expect(chat.GetIndex() > board.GetIndex(), "暗线聚焦后节点顺序与视觉层级保持一致。");
			Vector2 chatPosition = chat.Position;
			await Drag(new Vector2(260, 52), new Vector2(292, 70));
			Expect(chat.Position.DistanceTo(chatPosition) > 10.0f, "暗线标题栏可以拖动窗口。");
			Vector2 origin = _canvas.WorldToScreen(Vector2.Zero);
			float zoom = _canvas.Zoom;
			Vector2 overlap = new(260, 480);
			Move(overlap);
			Expect(IsHoveredInside(chat), "重叠区域实际命中前方的暗线窗口。");
			await Drag(overlap, overlap + new Vector2(18, -35));
			Wheel(overlap);
			await Frames();
			Expect(_canvas.WorldToScreen(Vector2.Zero).IsEqualApprox(origin) && Mathf.IsEqualApprox(_canvas.Zoom, zoom),
				"在暗线上拖动和滚轮不会操作后面的白板。");

			_main.Call("ShowWindow", "board");
			_canvas.ResetView();
			await Frames();
			origin = _canvas.WorldToScreen(Vector2.Zero);
			Move(overlap);
			Expect(IsHoveredInside(board), "白板重新聚焦后命中顺序跟随切换。");
			await Drag(overlap, overlap + new Vector2(20, -25));
			Expect(_canvas.WorldToScreen(Vector2.Zero).DistanceTo(origin) > 10.0f, "重新聚焦的白板仍可正常平移。");

			// 浏览器使用同一窗口外壳和真实 BrowserHost，检查窗口路由而不启动额外 CEF 进程。
			Panel browser = Window("browser");
			browser.Show();
			_main.Call("FocusWindow", "browser");
			origin = _canvas.WorldToScreen(Vector2.Zero);
			Move(overlap);
			Expect(IsHoveredInside(browser), "浏览器窗口使用同样的前景输入顺序。");
			await Drag(overlap, overlap + new Vector2(-20, 30));
			Wheel(overlap);
			await Frames();
			Expect(_canvas.WorldToScreen(Vector2.Zero).IsEqualApprox(origin), "浏览器前景区域不会穿透到白板。");
			_main.Call("CloseWindow", "browser");

			// 先隐藏白板再收录/挂接，模拟玩家一直在网页和暗线推进，最后才回来看白板。
			_main.Call("CloseWindow", "board");
			_main.Call("DispatchAction", "search", new Dictionary { ["scope"] = "web", ["query"] = "年糕" });
			_main.Call("DispatchAction", "open_article", new Dictionary { ["id"] = "lostcat" });
			_main.Call("DispatchAction", "collect_clue", new Dictionary { ["page"] = "article", ["kind"] = "text", ["text"] = "四爪白、红绳和铃铛" });
			_main.Call("DispatchAction", "attach_clue", new Dictionary { ["clue_id"] = "clue_1", ["task_id"] = "cat_look" });
			await Frames();
			_main.Call("ShowWindow", "board");
			await Delay(0.4);
			ValidateTree("隐藏期间更新内容后再次打开白板");
			_canvas.FitContent();
			_canvas.RotateAt(25, _canvas.Size * 0.5f);
			await Frames();
			ValidateTree("缩放旋转白板后");

			_main.Call("CloseWindow", "board");
			_main.Call("ShowWindow", "chat");
			await Delay(0.4);
			ValidateChatRows();
			chat.Size = new Vector2(390, chat.Size.Y);
			await Delay(0.15);
			ValidateChatRows();
			chat.Size = new Vector2(620, chat.Size.Y);
			await Delay(0.15);
			ValidateChatRows();
			await Capture("desktop-chat-regression.png");
		}
		catch (Exception error)
		{
			_failed = true;
			GD.PushError("桌面界面回归异常：" + error);
		}
		GD.Print(_failed ? "[FAIL] 桌面界面回归失败。" : "[PASS] 桌面界面回归通过。");
		GetTree().Quit(_failed ? 1 : 0);
	}

	/// <summary>根据实际控件尺寸验证连线端点，确保布局完成后没有主任务悬空或支线脱节。</summary>
	private void ValidateTree(string phase)
	{
		Control world = _canvas.GetNode<Control>("BoardWorld");
		Control title = world.GetNode<Control>("TreeTitle");
		BoardLinksView links = world.GetNode<BoardLinksView>("Links");
		Vector2 rootAnchor = title.Position + new Vector2(title.Size.X * 0.5f, title.Size.Y);
		bool rootConnected = false;
		foreach (BoardLinksView.Segment line in links.Segments)
			if (line.From.DistanceTo(rootAnchor) < 0.1f || line.To.DistanceTo(rootAnchor) < 0.1f) rootConnected = true;
		Expect(rootConnected, phase + "：连线接在主任务实际下沿。");
		foreach (Control task in world.GetNode<Control>("TaskLayer").GetChildren())
		{
			if (!task.Visible) continue;
			Vector2 anchor = task.Position + new Vector2(0, task.Size.Y * 0.5f);
			bool connected = false;
			foreach (BoardLinksView.Segment line in links.Segments)
				if (line.To.DistanceTo(anchor) < 0.1f) connected = true;
			Expect(connected, phase + "：子任务 " + task.Name + " 连接实际中线。");
		}
	}

	/// <summary>用实际气泡、行和文字的矩形检查连续消息，覆盖中文长句及显式换行。</summary>
	private void ValidateChatRows()
	{
		VBoxContainer messages = Window("chat").GetNode<VBoxContainer>("Body/ChatContent/ChatScroll/ChatMessages");
		Expect(messages.GetChildCount() >= 5, "真实暗线已显示多条开场消息。");
		float previousBottom = float.NegativeInfinity;
		bool noOverlap = true;
		bool containsText = true;
		foreach (Control message in messages.GetChildren())
		{
			Control row = message.GetNode<Control>("IncomingRow");
			if (!row.Visible) row = message.GetNode<Control>("OutgoingRow");
			Control bubble = row.GetNode<Control>("Bubble");
			Control text = bubble.GetNode<Control>("BubbleContent/Message");
			Rect2 rowRect = row.GetGlobalRect();
			Rect2 bubbleRect = bubble.GetGlobalRect();
			Rect2 textRect = text.GetGlobalRect();
			if (rowRect.Position.Y < previousBottom - 0.1f) noOverlap = false;
			if (textRect.End.Y > bubbleRect.End.Y + 0.1f || bubbleRect.End.Y > message.GetGlobalRect().End.Y + 0.1f)
				containsText = false;
			previousBottom = Mathf.Max(rowRect.End.Y, bubbleRect.End.Y);
		}
		Expect(noOverlap, "连续聊天消息按照实际高度排列，不互相覆盖。");
		Expect(containsText, "气泡和消息行完整包含实际换行的文字。");
	}

	/// <summary>从真实桌面窗口层读取应用窗口，避免为测试创建替代外壳。</summary>
	private Panel Window(string id) => _desktop.GetNode<Panel>("WindowLayer/" + id);


	/// <summary>确认 CRT 内容视口当前悬停控件属于指定的前景窗口。</summary>
	private bool IsHoveredInside(Control frame)
	{
		Control? hovered = _contentViewport.GuiGetHoveredControl();
		return hovered != null && (hovered == frame || frame.IsAncestorOf(hovered));
	}

	/// <summary>从根视口发送鼠标事件，让真实 CRT 插件执行坐标映射及子视口路由。</summary>
	private void Move(Vector2 pointer, bool dragging = false)
	{
		GetViewport().PushInput(new InputEventMouseMotion
		{
			Position = pointer, GlobalPosition = pointer, Relative = pointer - _lastPointer,
			ButtonMask = dragging ? MouseButtonMask.Left : 0,
		}, true);
		_lastPointer = pointer;
	}

	/// <summary>发出匹配的按下与松开，保留 Godot 自己的鼠标捕获生命周期。</summary>
	private void Button(Vector2 pointer, bool pressed) => GetViewport().PushInput(new InputEventMouseButton
	{
		Position = pointer, GlobalPosition = pointer, ButtonIndex = MouseButton.Left,
		Pressed = pressed, ButtonMask = pressed ? MouseButtonMask.Left : 0,
	}, true);

	/// <summary>通过根视口完整执行一次点击并等待窗口排序生效。</summary>
	private async Task Click(Vector2 pointer)
	{
		Move(pointer);
		Button(pointer, true);
		Button(pointer, false);
		await Frames();
	}

	/// <summary>通过根视口跨多段拖动，确认前景窗口接收后不会触发后方手势。</summary>
	private async Task Drag(Vector2 from, Vector2 to)
	{
		Move(from);
		Button(from, true);
		for (int step = 1; step <= 3; step++) Move(from.Lerp(to, step / 3.0f), true);
		Button(to, false);
		await Frames();
	}

	/// <summary>检查覆盖窗口下的白板不会从全局输入阶段抢走滚轮。</summary>
	private void Wheel(Vector2 pointer)
	{
		Move(pointer);
		GetViewport().PushInput(new InputEventMouseButton
		{
			Position = pointer, GlobalPosition = pointer, ButtonIndex = MouseButton.WheelUp, Pressed = true,
		}, true);
	}

	/// <summary>等待容器排序与延迟布局完成。</summary>
	private async Task Frames()
	{
		for (int frame = 0; frame < 5; frame++) await ToSignal(GetTree(), SceneTree.SignalName.ProcessFrame);
	}

	/// <summary>等待原有的窗口过渡和逐条对白，不改动产品里的时间参数。</summary>
	private async Task Delay(double seconds)
	{
		await ToSignal(GetTree().CreateTimer(seconds), SceneTreeTimer.SignalName.Timeout);
		await Frames();
	}

	/// <summary>需要渲染验收时导出实际桌面图；无窗口回归不等待绘图信号。</summary>
	private async Task Capture(string filename)
	{
		if (!System.Array.Exists(OS.GetCmdlineUserArgs(), argument => argument == "--capture-desktop")) return;
		await ToSignal(RenderingServer.Singleton, RenderingServer.SignalName.FramePostDraw);
		Expect(GetViewport().GetTexture().GetImage().SavePng("res://artifacts/" + filename) == Error.Ok,
			"已输出桌面回归图：" + filename);
	}

	/// <summary>记录全部断言，最后通过退出码给出可自动执行的验收结果。</summary>
	private void Expect(bool condition, string message)
	{
		if (condition) GD.Print("[PASS] " + message);
		else { _failed = true; GD.PushError("[FAIL] " + message); }
	}
}
