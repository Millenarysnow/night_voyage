#nullable enable

using Godot;
using Godot.Collections;
using System;
using System.Threading.Tasks;

/// <summary>
/// 通过 Viewport.PushInput 走 Godot 的真实命中、焦点、GUI 和全局输入链，
/// 验证自由白板在整板变换、卡片变换和状态刷新之后仍能操作。
/// 可使用 -- --capture-board 输出一次实际渲染的白板图供布局检查。
/// </summary>
public partial class BoardInteractionIntegrationTest : Node
{
	private Node _state = null!;
	private BoardView _board = null!;
	private BoardCanvasView _canvas = null!;
	private Control _cards = null!;
	private Viewport _viewport = null!;
	private bool _failed;
	private Vector2 _lastPointer;

	/// <summary>创建真实白板场景和原生状态机，再等待容器布局就绪后逐项验收输入。</summary>
	public override async void _Ready()
	{
		try
		{
			_state = (Node)ClassDB.Instantiate("NVPrologueState").AsGodotObject();
			AddChild(_state);
			_state.Call("configure_content", Json.ParseString(FileAccess.GetFileAsString("res://content/prologue_cat.json")));
			_board = GD.Load<PackedScene>("res://content/prologue/scenes/board_window_content.tscn").Instantiate<BoardView>();
			_board.SetAnchorsAndOffsetsPreset(Control.LayoutPreset.TopLeft);
			_board.GrowHorizontal = Control.GrowDirection.End;
			_board.GrowVertical = Control.GrowDirection.End;
			_board.Size = new Vector2(920, 610);
			AddChild(_board);
			_board.Position = new Vector2(100, 50);
			_canvas = _board.GetNode<BoardCanvasView>("BoardCanvas");
			_cards = _canvas.GetNode<Control>("BoardWorld/ClueLayer");
			_viewport = GetViewport();
			_viewport.NotifyMouseEntered();
			_board.ActionRequested += (action, payload) =>
			{
				Dictionary result = Act(action, payload);
				Expect(result["ok"].AsBool(), "白板动作被状态机接受：" + action);
			};
			_state.Connect("state_changed", Callable.From<Dictionary>(snapshot => _board.Configure(snapshot)));
			_board.Configure(Snapshot());

			Act("begin", new Dictionary());
			Act("search", new Dictionary { ["scope"] = "web", ["query"] = "年糕" });
			Act("open_article", new Dictionary { ["id"] = "lostcat" });
			Act("collect_clue", new Dictionary { ["page"] = "article", ["kind"] = "text", ["text"] = "四爪白、红绳和铃铛" });
			Act("collect_clue", new Dictionary { ["page"] = "article", ["kind"] = "image" });
			await Frames();
			ClueCardView card = _cards.GetNode<ClueCardView>("clue_1");
			ClueCardView image = _cards.GetNode<ClueCardView>("clue_2");
			Vector2 fittedOrigin = _canvas.WorldToScreen(Vector2.Zero);
			Act("search", new Dictionary { ["scope"] = "web", ["query"] = "年糕" });
			Expect(_canvas.WorldToScreen(Vector2.Zero).IsEqualApprox(fittedOrigin), "无关剧情刷新保留自动全览的镜头。");
			float fittedZoom = _canvas.Zoom;
			Vector2 imageHeader = Point(image.GetNode<Control>("Layout/Header/Kind"));
			await Drag(imageHeader, imageHeader + new Vector2(65, 0));
			Expect(Mathf.IsEqualApprox(_canvas.Zoom, fittedZoom) &&
				_canvas.WorldToScreen(Vector2.Zero).IsEqualApprox(fittedOrigin),
				"从自动全览中拖动卡片时冻结镜头，不随新增内容边界跳动。");

			_canvas.ResetView();
			_canvas.ZoomAt(0.8f, _canvas.Size * 0.5f);
			_canvas.RotateAt(20.0f, _canvas.Size * 0.5f);
			await Frames();
			Vector2 beforeDrag = card.Position;
			Vector2 start = Point(card.GetNode<Control>("Layout/Header/Kind"));
			Vector2 delta = new(45, 28);
			Vector2 expectedDelta = _canvas.ScreenToWorld(_canvas.ToCanvasLocal(start + delta)) -
				_canvas.ScreenToWorld(_canvas.ToCanvasLocal(start));
			await Drag(start, start + delta);
			Expect(card.Position.DistanceTo(beforeDrag + expectedDelta) < 0.1f &&
				Math.Abs(Clue("clue_1")["x"].AsDouble() - card.Position.X) < 0.1,
				"整板缩放与旋转后拖动题头仍保留鼠标锚点，并把坐标写回 C++。");

			// 另一张线索加入时，正在写的批注保持同一实例、文本和焦点。
			TextEdit note = card.GetNode<TextEdit>("Layout/Note");
			note.GrabFocus();
			note.Text = "草稿：需要对照照片 | 保留光标";
			Act("open_article", new Dictionary { ["id"] = "lostcat" });
			Act("collect_clue", new Dictionary { ["page"] = "article", ["kind"] = "text", ["text"] = "它是孙女留下的猫。" });
			await Frames();
			Expect(_cards.GetChildCount() == 3 && _cards.GetNode<ClueCardView>("clue_1") == card && note.HasFocus() &&
				note.Text.Contains("保留光标"), "收录新线索不会销毁正在编辑的卡片或丢失批注草稿。");
			note.ReleaseFocus();
			await Frames();
			Expect(Clue("clue_1")["note"].AsString().Contains("保留光标"), "离开批注后保存最终文本。");

			_canvas.ResetView();
			await Click(Point(card.GetNode<Control>("Layout/Header/Kind")));
			Control rotate = card.GetNode<Control>("RotateHandle");
			Control scale = card.GetNode<Control>("ScaleHandle");
			Expect(rotate.Visible && rotate.Size.X < card.Size.X * 0.5f && scale.Size.Y < card.Size.Y * 0.5f,
				"选中后的两个手柄保持独立尺寸，不被容器拉伸或覆盖移除按钮。");
			Vector2 center = card.GetGlobalTransformWithCanvas() * card.PivotOffset;
			start = Point(rotate);
			await Drag(start, center + (start - center).Rotated(Mathf.DegToRad(31.0f)));
			Expect(Math.Abs(Clue("clue_1")["rotation"].AsDouble() - 30.0) < 0.1,
				"旋转手柄通过实际输入按 15 度刻度保存单卡角度。");
			center = card.GetGlobalTransformWithCanvas() * card.PivotOffset;
			start = Point(scale);
			await Drag(start, center + (start - center) * 1.25f);
			Expect(Math.Abs(Clue("clue_1")["scale"].AsDouble() - 1.25) < 0.02,
				"缩放手柄通过实际输入保存单卡比例。");

			// 批注与正文有自己的编辑语义；滚轮在正文上仍应缩放整张板子。
			RichTextLabel text = card.GetNode<RichTextLabel>("Layout/Text");
			Move(Point(text));
			float previousZoom = _canvas.Zoom;
			Wheel(Point(text));
			await Frames();
			Expect(_canvas.Zoom > previousZoom, "滚轮停在可选摘录正文上时仍能缩放白板。");
			text.GrabFocus();
			text.SelectAll();
			int previousCount = _cards.GetChildCount();
			_viewport.PushInput(new InputEventKey { Keycode = Key.Delete, Pressed = true }, true);
			_viewport.PushInput(new InputEventKey { Keycode = Key.Delete, Pressed = false }, true);
			await Frames();
			Expect(text.GetSelectedText().Length > 0 && _cards.GetChildCount() == previousCount,
				"摘录支持划选复制，存在文字选区时 Delete 不会误删卡片。");
			text.Deselect();

			// 放大已有照片只发预览事件，不会重新收录，也不依赖当前浏览器页面。
			_board.GetNode<Button>("Toolbar/Buttons/Tidy").EmitSignal(BaseButton.SignalName.Pressed);
			await Frames();
			int previewCount = 0;
			_board.ImagePreviewRequested += (path, caption) =>
			{
				if (path.EndsWith("prop_cat_home.png") && caption.Length > 0) previewCount++;
			};
			_canvas.ResetView();
			await Click(Point(image.GetNode<Control>("Layout/Image")));
			Expect(previewCount == 1 && _cards.GetChildCount() == previousCount,
				"白板图片点击只请求预览原图和说明，不会新增线索。");
			Vector2 imagePosition = image.Position;
			start = Point(image.GetNode<Control>("Layout/Image"));
			await Drag(start, start + new Vector2(25, 12));
			Expect(previewCount == 1 && image.Position.DistanceTo(imagePosition + new Vector2(25, 12)) < 0.1f,
				"拖动图片本身移动整张卡片，松手不会误开预览。");

			// 抓题头时把鼠标放进任务行，卡片中心因抓取偏移落在投放区外。
			TaskDropZoneView task = _canvas.GetNode<TaskDropZoneView>("BoardWorld/TaskLayer/cat_look");
			Vector2 targetCenter = task.BoardDropBounds.GetCenter();
			Vector2 cardCenterWorld = card.Position + card.PivotOffset;
			start = Point(card.GetNode<Control>("Layout/Header/Kind"));
			Vector2 destination = _canvas.GetGlobalTransformWithCanvas() * _canvas.WorldToScreen(targetCenter);
			Vector2 pointerWorld = _canvas.ScreenToWorld(_canvas.ToCanvasLocal(destination));
			Vector2 startWorld = _canvas.ScreenToWorld(_canvas.ToCanvasLocal(start));
			Vector2 expectedCenter = targetCenter + (cardCenterWorld - startWorld);
			Expect(task.BoardDropBounds.HasPoint(pointerWorld) && !task.BoardDropBounds.HasPoint(expectedCenter),
				"投放验收使用鼠标在任务区内、卡片中心在任务区外的场景。");
			await Drag(start, destination);
			Expect(Clue("clue_1")["task_id"].AsString() == "cat_look",
				"任务挂接根据鼠标释放点命中，并由 C++ 完成子任务判定。");

			_canvas.FitContent();
			await Frames();
			Vector2 blank = _canvas.GetGlobalTransformWithCanvas() * (_canvas.Size - new Vector2(14, 14));
			Vector2 originBeforePan = _canvas.WorldToScreen(Vector2.Zero);
			await Drag(blank, blank - new Vector2(35, 18));
			Expect((_canvas.WorldToScreen(Vector2.Zero) - originBeforePan).DistanceTo(new Vector2(-35, -18)) < 0.1f,
				"空白拖动平移画布并在松手后保留视角。");
			previousZoom = _canvas.Zoom;
			Vector2 marqueeStart = _canvas.GetGlobalTransformWithCanvas() * new Vector2(10, 10);
			Move(marqueeStart);
			Expect(_viewport.GuiGetHoveredControl() == _canvas, "框选从画布空白处开始。");
			await Drag(marqueeStart, marqueeStart + new Vector2(140, 100), true);
			Expect(_canvas.Zoom > previousZoom && !Snapshot()["board_view"].AsGodotDictionary()["auto_fit"].AsBool(),
				"Ctrl 框选放大所框区域，并保存为手动视角。");
			_canvas.FitContent();
			await Frames();
			Expect(_canvas.Zoom < 2.0f, "框选后全览仍使用真实内容边界。");

			_board.GetNode<Button>("Toolbar/Buttons/Tidy").EmitSignal(BaseButton.SignalName.Pressed);
			await Frames();
			Expect(Clue("clue_1")["scale"].AsDouble() == 1.0 &&
				Clue("clue_1")["rotation"].AsDouble() == 0.0 &&
				Clue("clue_1")["task_id"].AsString() == "cat_look" &&
				Clue("clue_1")["note"].AsString().Contains("保留光标"),
				"整理恢复卡片大小和角度，并保留任务归属、批注和剧情进度。");
			Expect(_board.GetNode<RichTextLabel>("BoardCanvas/BoardWorld/TreeTitle/QuestTitle").GetParsedText() ==
				"今晚要办的事\n找到年糕", "多次状态刷新不会重复追加任务标题。");

			if (System.Array.Exists(OS.GetCmdlineUserArgs(), argument => argument == "--capture-board"))
			{
				await ToSignal(RenderingServer.Singleton, RenderingServer.SignalName.FramePostDraw);
				Error saved = _viewport.GetTexture().GetImage().SavePng("res://artifacts/board-preview.png");
				Expect(saved == Error.Ok, "实际渲染的白板布局图已写出。");
			}
			if (System.Array.Exists(OS.GetCmdlineUserArgs(), argument => argument == "--capture-desktop"))
				await CaptureDesktop();
		}
		catch (Exception error)
		{
			_failed = true;
			GD.PushError("白板输入验收异常：" + error);
		}
		GD.Print(_failed ? "[FAIL] 白板输入集成验收失败。" : "[PASS] 白板输入集成验收通过。");
		GetTree().Quit(_failed ? 1 : 0);
	}

	/// <summary>额外加载真实序幕协调器，在其 CRT 桌面和窗口插槽中检查同一白板组件。</summary>
	private async Task CaptureDesktop()
	{
		_board.Hide();
		PrologueMain main = GD.Load<PackedScene>("res://main.tscn").Instantiate<PrologueMain>();
		AddChild(main);
		main.GetNode<Control>("LockScreen").Hide();
		Node? desktopState = null;
		foreach (Node child in main.GetChildren())
			if (child.GetClass() == "NVPrologueState") desktopState = child;
		if (desktopState == null) throw new InvalidOperationException("真实桌面没有创建原生状态机。");
		desktopState.Call("dispatch_action", "begin", new Dictionary());
		desktopState.Call("dispatch_action", "search", new Dictionary { ["scope"] = "web", ["query"] = "年糕" });
		desktopState.Call("dispatch_action", "open_article", new Dictionary { ["id"] = "lostcat" });
		desktopState.Call("dispatch_action", "collect_clue", new Dictionary { ["page"] = "article", ["kind"] = "text", ["text"] = "半岁小橘猫，四爪白，脖子上系旧红绳，绳上挂着小铜铃铛。" });
		desktopState.Call("dispatch_action", "collect_clue", new Dictionary { ["page"] = "article", ["kind"] = "image" });
		desktopState.Call("dispatch_action", "open_forum", new Dictionary { ["id"] = "anyong" });
		desktopState.Call("dispatch_action", "forum_search", new Dictionary { ["query"] = "猫" });
		desktopState.Call("dispatch_action", "open_thread", new Dictionary { ["id"] = "pet_sighting" });
		desktopState.Call("dispatch_action", "collect_clue", new Dictionary { ["page"] = "forum_post", ["kind"] = "text", ["text"] = "今天下午两点多，老粮站门口那个花坛上蹲着这么一只。" });
		desktopState.Call("dispatch_action", "collect_clue", new Dictionary { ["page"] = "forum_post", ["kind"] = "image" });
		desktopState.Call("dispatch_action", "attach_clue", new Dictionary { ["clue_id"] = "clue_1", ["task_id"] = "cat_look" });
		desktopState.Call("dispatch_action", "attach_clue", new Dictionary { ["clue_id"] = "clue_3", ["task_id"] = "cat_place" });
		Panel frame = main.GetNode<Panel>("PrologueDesktop/CRTViewport/ContentViewport/Content/DesktopContent/WindowLayer/board");
		frame.Show();
		await Frames();
		Control taskbar = frame.GetParent().GetParent().GetNode<Control>("Taskbar");
		Expect(frame.GetRect().End.Y <= taskbar.Position.Y, "白板窗口的完整内容位于任务栏上方。");
		await ToSignal(RenderingServer.Singleton, RenderingServer.SignalName.FramePostDraw);
		Error saved = _viewport.GetTexture().GetImage().SavePng("res://artifacts/board-desktop-preview.png");
		Expect(saved == Error.Ok, "白板在真实桌面窗口中的渲染图已写出。");
	}

	/// <summary>返回当前原生权威状态，避免测试把旧快照误当成更新结果。</summary>
	private Dictionary Snapshot() => _state.Call("get_snapshot").AsGodotDictionary();

	/// <summary>按稳定 ID 查找卡片数据；未找到时中止测试并保留明确错误。</summary>
	private Dictionary Clue(string id)
	{
		foreach (Variant value in Snapshot()["clues"].AsGodotArray())
		{
			Dictionary clue = value.AsGodotDictionary();
			if (clue["id"].AsString() == id) return clue;
		}
		throw new InvalidOperationException("没有找到验收卡片：" + id);
	}

	/// <summary>调用原生动作入口，交互与测试准备均使用同一套剧情校验。</summary>
	private Dictionary Act(string action, Dictionary payload) =>
		_state.Call("dispatch_action", action, payload).AsGodotDictionary();

	/// <summary>等待场景容器排序、延迟布局和镜头刷新完成。</summary>
	private async Task Frames()
	{
		for (int frame = 0; frame < 3; frame++) await ToSignal(GetTree(), SceneTree.SignalName.ProcessFrame);
	}

	/// <summary>获取控件中心的视口坐标，正确处理其自身和所有祖先的缩放、旋转。</summary>
	private static Vector2 Point(Control control) => control.GetGlobalTransformWithCanvas() * (control.Size * 0.5f);

	/// <summary>更新悬停控件；按下、拖动和松开都通过 Viewport 的真实输入分发。</summary>
	private void Move(Vector2 pointer, bool dragging = false)
	{
		_viewport.PushInput(new InputEventMouseMotion
		{
			Position = pointer,
			GlobalPosition = pointer,
			Relative = pointer - _lastPointer,
			ButtonMask = dragging ? MouseButtonMask.Left : 0,
		}, true);
		_lastPointer = pointer;
	}

	/// <summary>从控件中心发出一次真实点击，验证 GUI 命中和选中焦点。</summary>
	private async Task Click(Vector2 pointer)
	{
		Move(pointer);
		Button(pointer, true);
		Button(pointer, false);
		await Frames();
	}

	/// <summary>分三段拖动，覆盖跨控件捕获以及鼠标释放后的状态写回。</summary>
	private async Task Drag(Vector2 from, Vector2 to, bool control = false)
	{
		Move(from);
		Button(from, true, control);
		for (int step = 1; step <= 3; step++) Move(from.Lerp(to, step / 3.0f), true);
		Button(to, false, control);
		await Frames();
	}

	/// <summary>构造带正确按钮掩码的鼠标事件，Ctrl 字段用于画布框选。</summary>
	private void Button(Vector2 pointer, bool pressed, bool control = false)
	{
		_viewport.PushInput(new InputEventMouseButton
		{
			Position = pointer,
			GlobalPosition = pointer,
			ButtonIndex = MouseButton.Left,
			ButtonMask = pressed ? MouseButtonMask.Left : 0,
			Pressed = pressed,
			CtrlPressed = control,
		}, true);
	}

	/// <summary>从正文上发出滚轮输入，验证可选字控件不会吞掉白板镜头操作。</summary>
	private void Wheel(Vector2 pointer)
	{
		_viewport.PushInput(new InputEventMouseButton
		{
			Position = pointer,
			GlobalPosition = pointer,
			ButtonIndex = MouseButton.WheelUp,
			Pressed = true,
		}, true);
	}

	/// <summary>记录有实际行为意义的断言；最终退出码使测试可用于自动验收。</summary>
	private void Expect(bool condition, string message)
	{
		if (condition) GD.Print("[PASS] " + message);
		else
		{
			_failed = true;
			GD.PushError("[FAIL] " + message);
		}
	}
}
