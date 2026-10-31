#nullable enable

using Godot;
using Godot.Collections;
using Array = Godot.Collections.Array;
using System;
using System.Collections.Generic;

/// <summary>
/// 把 C++ 序章快照呈现为可自由导航的任务树白板，并把玩家编辑动作交回状态机。
/// 任务、线索、工具栏和画布结构均由可编辑场景提供，布局参数通过场景导出属性配置。
/// </summary>
public partial class BoardView : Control
{
	private const string TaskScenePath = "res://content/prologue/scenes/task_drop_zone.tscn";
	private const string ClueScenePath = "res://content/prologue/scenes/clue_card.tscn";

	[Export] private float TreeWidth { get; set; } = 320.0f;
	[Export] private float TaskIndent { get; set; } = 22.0f;
	[Export] private float ClueIndent { get; set; } = 44.0f;
	[Export] private float FreeCardGap { get; set; } = 14.0f;
	[Export] private float TaskGap { get; set; } = 12.0f;
	[Export] private float AttachedCardGap { get; set; } = 10.0f;
	[Export] private float FreeCardTop { get; set; } = 22.0f;
	[Export] private float FreeCardOffset { get; set; } = 36.0f;
	[Export] private float TreeTrunkX { get; set; } = 7.0f;
	[Export] private float BranchInset { get; set; } = 6.0f;
	[Export] private float AttachedCardTopGap { get; set; } = 22.0f;
	[Export] private float DropPadding { get; set; } = 10.0f;

	/// <summary>玩家在白板上执行的状态动作，由序幕协调器提交给 C++。</summary>
	public event Action<string, Dictionary>? ActionRequested;
	/// <summary>玩家点击图片线索时通知序幕协调器打开统一图片灯箱。</summary>
	public event Action<string, string>? ImagePreviewRequested;

	private BoardCanvasView? _canvas;
	private BoardLinksView? _links;
	private Control? _world;
	private Control? _taskLayer;
	private Control? _clueLayer;
	private Label? _countLabel;
	private RichTextLabel? _questTitle;
	private Control? _treeTitle;
	private Label? _questStatus;
	private Label? _zoomLabel;
	private Label? _rotationLabel;
	private readonly System.Collections.Generic.Dictionary<string, ClueCardView> _cards = new();
	private readonly System.Collections.Generic.Dictionary<string, TaskDropZoneView> _tasks = new();
	private readonly System.Collections.Generic.Dictionary<string, Dictionary> _clueData = new();
	private readonly List<BoardLinksView.Segment> _segments = new();
	private string _snapshotSignature = "";
	private string _viewSignature = "";
	private string _selectedClueId = "";
	private Dictionary? _pendingSnapshot;
	private bool _measuredLayoutQueued;

	/// <summary>从场景读取工具栏、画布图层和复用模板容器。</summary>
	public override void _Ready()
	{
		_canvas = GetNode<BoardCanvasView>("BoardCanvas");
		_world = _canvas.GetNode<Control>("BoardWorld");
		_links = _world.GetNode<BoardLinksView>("Links");
		_taskLayer = _world.GetNode<Control>("TaskLayer");
		_clueLayer = _world.GetNode<Control>("ClueLayer");
		_questTitle = _world.GetNode<RichTextLabel>("TreeTitle/QuestTitle");
		_treeTitle = _world.GetNode<Control>("TreeTitle");
		_treeTitle.Resized += QueueMeasuredLayout;
		_treeTitle.MinimumSizeChanged += QueueMeasuredLayout;
		VisibilityChanged += QueueMeasuredLayout;
		_canvas.Resized += QueueMeasuredLayout;
		_questStatus = _world.GetNode<Label>("QuestStatus");
		_countLabel = GetNode<Label>("Toolbar/Buttons/Count");
		_zoomLabel = GetNode<Label>("Toolbar/Buttons/ZoomValue");
		_rotationLabel = GetNode<Label>("Toolbar/Buttons/RotationValue");
		_canvas.ViewChanged += UpdateCameraLabels;
		_canvas.ViewStateCommitted += SaveBoardView;
		_canvas.BackgroundPressed += () => SelectCard("");
		GetNode<Button>("Toolbar/Buttons/ZoomOut").Pressed += () => _canvas.ZoomAt(1.0f / 1.25f, _canvas.Size * 0.5f);
		GetNode<Button>("Toolbar/Buttons/ZoomIn").Pressed += () => _canvas.ZoomAt(1.25f, _canvas.Size * 0.5f);
		GetNode<Button>("Toolbar/Buttons/ResetZoom").Pressed += () => _canvas.ZoomAt(1.0f / _canvas.Zoom, _canvas.Size * 0.5f);
		GetNode<Button>("Toolbar/Buttons/RotateLeft").Pressed += () => _canvas.RotateAt(-15.0f, _canvas.Size * 0.5f);
		GetNode<Button>("Toolbar/Buttons/RotateRight").Pressed += () => _canvas.RotateAt(15.0f, _canvas.Size * 0.5f);
		GetNode<Button>("Toolbar/Buttons/ResetRotation").Pressed += _canvas.ResetRotation;
		GetNode<Button>("Toolbar/Buttons/FocusSelected").Pressed += FocusSelectedCard;
		GetNode<Button>("Toolbar/Buttons/FitAll").Pressed += () => _canvas.FitContent();
		GetNode<Button>("Toolbar/Buttons/ResetView").Pressed += _canvas.ResetView;
		GetNode<Button>("Toolbar/Buttons/Tidy").Pressed += TidyBoard;
	}

	/// <summary>同步最新权威快照；若白板数据未变则不拆场景，避免打断正在编辑的卡片。</summary>
	public void Configure(Dictionary snapshot)
	{
		if (!snapshot.ContainsKey("quest") || !snapshot.ContainsKey("clues"))
		{
			GD.PushError("白板快照缺少 quest 或 clues 字段。");
			return;
		}

		Dictionary quest = snapshot["quest"].AsGodotDictionary();
		Array clues = snapshot["clues"].AsGodotArray();
		if (snapshot.ContainsKey("board_view"))
		{
			Dictionary view = snapshot["board_view"].AsGodotDictionary();
			string viewSignature = Json.Stringify(view);
			if (viewSignature != _viewSignature)
			{
				_viewSignature = viewSignature;
				_canvas!.SetView(
					new Vector2(
						(float)PrologueMain.Get(view, "pan_x", 0.0).AsDouble(),
						(float)PrologueMain.Get(view, "pan_y", 0.0).AsDouble()),
					(float)PrologueMain.Get(view, "zoom", 1.0).AsDouble(),
					(float)PrologueMain.Get(view, "rotation", 0.0).AsDouble(),
					PrologueMain.Get(view, "auto_fit", true).AsBool());
			}
		}
		string nextSignature = BuildSignature(quest, clues);
		_countLabel!.Text = clues.Count + " 张线索";
		if (nextSignature == _snapshotSignature) return;
		// 手势中的卡片使用玩家的临时坐标；先暂存其他来源的刷新，松手后再应用。
		foreach (ClueCardView card in _cards.Values)
		{
			if (!card.IsGestureActive) continue;
			_pendingSnapshot = snapshot;
			return;
		}
		_pendingSnapshot = null;
		_snapshotSignature = nextSignature;
		_segments.Clear();

		bool questComplete = PrologueMain.Get(quest, "complete", false).AsBool();
		bool questReady = PrologueMain.Get(snapshot, "quest_ready", false).AsBool();
		string questTitle = PrologueMain.Get(quest, "title", "").AsString();
		// AddText 不会改写 RichTextLabel.Text 属性，同样的 Text 赋值不会清掉上次追加的内容。
		// 每次先清空富文本缓冲，避免状态刷新后重复堆叠任务标题。
		_questTitle!.Clear();
		_questTitle.AddText("今晚要办的事");
		_questTitle.AddText("\n");
		if (questComplete) _questTitle.PushStrikethrough();
		_questTitle.AddText(questTitle);
		if (questComplete) _questTitle.Pop();
		_questStatus!.Visible = questComplete || questReady;
		_questStatus.Text = questComplete
			? "任务完成"
			: "两条子任务已对上，回到暗线报告位置。";

		HashSet<string> taskIds = new();
		foreach (Variant taskValue in quest["tasks"].AsGodotArray())
		{
			Dictionary task = taskValue.AsGodotDictionary();
			string taskId = PrologueMain.Get(task, "id", "").AsString();
			taskIds.Add(taskId);
			if (!_tasks.TryGetValue(taskId, out TaskDropZoneView? zone))
			{
				zone = GD.Load<PackedScene>(TaskScenePath).Instantiate<TaskDropZoneView>();
				zone.Name = taskId;
				_taskLayer!.AddChild(zone);
				_tasks.Add(taskId, zone);
				zone.Resized += QueueMeasuredLayout;
				zone.MinimumSizeChanged += QueueMeasuredLayout;
			}
			zone.Configure(taskId, PrologueMain.Get(task, "title", "").AsString(),
				PrologueMain.Get(task, "done", false).AsBool());
			zone.Visible = !questComplete;
			zone.ResetZoneSize();
			zone.Size = new Vector2(TreeWidth - TaskIndent, zone.Size.Y);
		}
		foreach (string id in new List<string>(_tasks.Keys))
		{
			if (taskIds.Contains(id)) continue;
			_taskLayer!.RemoveChild(_tasks[id]);
			_tasks[id].QueueFree();
			_tasks.Remove(id);
		}

		HashSet<string> clueIds = new();
		foreach (Variant clueValue in clues)
		{
			Dictionary clue = clueValue.AsGodotDictionary();
			string clueId = PrologueMain.Get(clue, "id", "").AsString();
			clueIds.Add(clueId);
			if (!_cards.TryGetValue(clueId, out ClueCardView? card))
			{
				card = GD.Load<PackedScene>(ClueScenePath).Instantiate<ClueCardView>();
				card.Name = clueId;
				card.Selected += SelectCard;
				card.FocusRequested += id => { SelectCard(id); FocusSelectedCard(); };
				card.DragMoved += UpdateDropHighlight;
				card.DragFinished += FinishCardDrag;
				card.TransformCommitted += CommitCardTransform;
				card.GestureEnded += ApplyPendingSnapshot;
				card.GestureStarted += _canvas!.PreserveCurrentView;
				card.NoteCommitted += CommitCardNote;
				card.ImagePreviewRequested += (path, caption) => ImagePreviewRequested?.Invoke(path, caption);
				card.RemovalRequested += RemoveCard;
				_clueLayer!.AddChild(card);
				_cards.Add(clueId, card);
				card.Resized += QueueMeasuredLayout;
			}
			card.Configure(clue);
			card.ResetCardSize();
			_clueData[clueId] = clue;
			card.SetSelected(_selectedClueId == clueId);
			card.ZIndex = _selectedClueId == clueId ? 100 : 2;
		}
		foreach (string id in new List<string>(_cards.Keys))
		{
			if (clueIds.Contains(id)) continue;
			_clueData.Remove(id);
			_clueLayer!.RemoveChild(_cards[id]);
			_cards[id].QueueFree();
			_cards.Remove(id);
		}
		if (!_cards.ContainsKey(_selectedClueId)) _selectedClueId = "";

		PlaceBoardContents(quest, questComplete);
		QueueMeasuredLayout();
	}

	/// <summary>卡片松手后应用暂存刷新；手势动作生成了更新快照时暂存值已在 Configure 清空。</summary>
	private void ApplyPendingSnapshot()
	{
		if (_pendingSnapshot != null) Configure(_pendingSnapshot);
	}

	/// <summary>合并布局请求；隐藏控件尚未排版，等显示及实际尺寸变化后再量取连接端点。</summary>
	private void QueueMeasuredLayout()
	{
		if (_measuredLayoutQueued || !IsInsideTree() || !IsVisibleInTree() || _lastQuest.Count == 0) return;
		_measuredLayoutQueued = true;
		CallDeferred(MethodName.FinishMeasuredLayout);
	}

	/// <summary>按当前可见控件的真实尺寸刷新任务树；尺寸后续再次变化时通过信号重新请求。</summary>
	private void FinishMeasuredLayout()
	{
		_measuredLayoutQueued = false;
		if (!IsInsideTree() || !IsVisibleInTree()) return;
		foreach (ClueCardView card in _cards.Values)
			if (card.IsGestureActive) return;
		foreach (ClueCardView card in _cards.Values) card.ResetCardSize();
		PlaceBoardContents(_lastQuest, _lastQuestComplete);
	}

	private Dictionary _lastQuest = new();
	private bool _lastQuestComplete;

	/// <summary>按原型的主任务树坐标摆放子任务、挂接线索和右侧自由卡片。</summary>
	private void PlaceBoardContents(Dictionary quest, bool questComplete)
	{
		_lastQuest = quest;
		_lastQuestComplete = questComplete;
		if (_canvas == null || _links == null || _taskLayer == null || _clueLayer == null || _world == null) return;
		_segments.Clear();
		float rootBottom = _treeTitle!.GetRect().End.Y;
		float y = (_questStatus!.Visible ? _questStatus.GetRect().End.Y : rootBottom) + TaskGap;
		float minX = 0.0f;
		float minY = 0.0f;
		float maxX = TreeWidth;
		float maxY = y;
		bool hasWorldItems = true;

		Array taskValues = quest.ContainsKey("tasks") ? quest["tasks"].AsGodotArray() : new Array();
		if (!questComplete && taskValues.Count > 0)
		{
			float trunkTop = (rootBottom + y) * 0.5f;
			float trunkBottom = trunkTop;
			float rootCenterX = _treeTitle.GetRect().GetCenter().X;
			_segments.Add(new BoardLinksView.Segment(new Vector2(rootCenterX, rootBottom),
				new Vector2(rootCenterX, trunkTop), false));
			_segments.Add(new BoardLinksView.Segment(new Vector2(rootCenterX, trunkTop),
				new Vector2(TreeTrunkX, trunkTop), false));
			foreach (Variant taskValue in taskValues)
			{
				Dictionary task = taskValue.AsGodotDictionary();
				string taskId = PrologueMain.Get(task, "id", "").AsString();
				if (!_tasks.TryGetValue(taskId, out TaskDropZoneView? zone)) continue;
				zone.Position = new Vector2(TaskIndent, y);
				zone.ResetZoneSize();
				float titleBottom = y + zone.Size.Y;
				float cardY = titleBottom + AttachedCardTopGap;
				float zoneBottom = titleBottom;
				float zoneRight = TaskIndent + zone.Size.X;
				_segments.Add(new BoardLinksView.Segment(new Vector2(TreeTrunkX, y + zone.Size.Y * 0.5f),
					new Vector2(TaskIndent, y + zone.Size.Y * 0.5f), false));
				trunkBottom = y + zone.Size.Y * 0.5f;

				Array clueIds = task.ContainsKey("clue_ids") ? task["clue_ids"].AsGodotArray() : new Array();
				foreach (Variant clueIdValue in clueIds)
				{
					string clueId = clueIdValue.AsString();
					if (!_cards.TryGetValue(clueId, out ClueCardView? card)) continue;
					Vector2 cardBounds = GetCardRotatedSize(card);
					card.Position = new Vector2(
						ClueIndent + (cardBounds.X - card.Size.X) * 0.5f,
						cardY + (cardBounds.Y - card.Size.Y) * 0.5f);
					card.ZIndex = _selectedClueId == clueId ? 100 : 2;
					float occupiedHeight = GetCardOccupiedHeight(card);
					zoneRight = Mathf.Max(zoneRight, ClueIndent + cardBounds.X);
					_segments.Add(new BoardLinksView.Segment(new Vector2(TaskIndent + BranchInset, titleBottom),
						new Vector2(TaskIndent + BranchInset, cardY), true));
					_segments.Add(new BoardLinksView.Segment(new Vector2(TaskIndent + BranchInset, cardY),
						new Vector2(ClueIndent, cardY), true));
					cardY += occupiedHeight + AttachedCardGap;
					zoneBottom = Mathf.Max(zoneBottom, cardY - AttachedCardGap);
					maxX = Mathf.Max(maxX, ClueIndent + cardBounds.X);
				}
				// 相邻任务的外扩投放范围不能互相覆盖，避免边界处依赖字典遍历顺序归类。
				float verticalPadding = Mathf.Min(DropPadding, TaskGap * 0.5f);
				zone.SetBoardDropBounds(new Rect2(new Vector2(TaskIndent - DropPadding, y - verticalPadding),
					new Vector2(zoneRight - TaskIndent + DropPadding * 2.0f,
						zoneBottom - y + verticalPadding * 2.0f)));
				maxY = Mathf.Max(maxY, zoneBottom);
				y = zoneBottom + TaskGap;
			}
			if (trunkBottom > trunkTop)
			{
				_segments.Add(new BoardLinksView.Segment(new Vector2(TreeTrunkX, trunkTop), new Vector2(TreeTrunkX, trunkBottom), false));
			}
		}
		else if (questComplete)
		{
			maxY = _questStatus.GetRect().End.Y;
		}

		PlaceLooseCards(ref minX, ref minY, ref maxX, ref maxY);
		_links.SetSegments(_segments);
		Rect2 bounds = new(new Vector2(minX, minY), new Vector2(Mathf.Max(1.0f, maxX - minX), Mathf.Max(1.0f, maxY - minY)));
		_canvas.SetContentBounds(bounds, hasWorldItems);
	}

	/// <summary>将自由卡片按最短列自动排位；玩家保存过的坐标优先于自动排位。</summary>
	private void PlaceLooseCards(ref float minX, ref float minY, ref float maxX, ref float maxY)
	{
		if (_canvas == null) return;
		List<ClueCardView> loose = new();
		foreach ((string clueId, ClueCardView card) in _cards)
		{
			Dictionary data = _clueData[clueId];
			string taskId = PrologueMain.Get(data, "task_id", "").AsString();
			if (taskId.Length == 0 || _lastQuestComplete) loose.Add(card);
		}

		if (loose.Count == 0) return;
		float cardWidth = 0.0f;
		foreach (ClueCardView card in loose) cardWidth = Mathf.Max(cardWidth, GetCardRotatedSize(card).X);
		float startX = TreeWidth + FreeCardOffset;
		int columns = Mathf.Max(1, Mathf.FloorToInt((_canvas.Size.X - startX + FreeCardGap) /
			(cardWidth + FreeCardGap)));
		float[] bottoms = new float[columns];
		for (int index = 0; index < bottoms.Length; index++) bottoms[index] = FreeCardTop;

		foreach (ClueCardView card in loose)
		{
			Dictionary data = _clueData[card.ClueId];
			if (TryGetStoredPoint(data, out Vector2 storedPoint))
			{
				card.Position = storedPoint;
				Rect2 bounds = GetCardWorldBounds(card);
				int column = Mathf.Clamp(Mathf.RoundToInt((bounds.Position.X - startX) / Mathf.Max(1.0f, cardWidth + FreeCardGap)), 0, columns - 1);
				bottoms[column] = Mathf.Max(bottoms[column], bounds.End.Y + FreeCardGap);
			}
		}

		foreach (ClueCardView card in loose)
		{
			Dictionary data = _clueData[card.ClueId];
			if (TryGetStoredPoint(data, out _)) continue;
			int column = 0;
			for (int candidate = 1; candidate < columns; candidate++)
			{
				if (bottoms[candidate] < bottoms[column]) column = candidate;
			}
			Vector2 extents = GetCardRotatedSize(card);
			card.Position = new Vector2(
				startX + column * (cardWidth + FreeCardGap) + (extents.X - card.Size.X) * 0.5f,
				bottoms[column] + (extents.Y - card.Size.Y) * 0.5f);
			bottoms[column] += GetCardOccupiedHeight(card) + FreeCardGap;
		}

		foreach (ClueCardView card in loose)
		{
			Rect2 bounds = GetCardWorldBounds(card);
			minX = Mathf.Min(minX, bounds.Position.X);
			minY = Mathf.Min(minY, bounds.Position.Y);
			maxX = Mathf.Max(maxX, bounds.End.X);
			maxY = Mathf.Max(maxY, bounds.End.Y);
		}
	}

	/// <summary>拖卡结束时根据板面世界坐标选择任务区或保留自由位置。</summary>
	private void FinishCardDrag(string clueId, Vector2 screenPosition, Vector2 worldPosition,
		float cardScale, float cardRotation)
	{
		if (_canvas == null || !_cards.ContainsKey(clueId)) return;
		// 投放点定义为松手时的鼠标位置。拖住题头、图片或卡片边缘时，
		// 鼠标与卡片中心本来就不重合，使用中心会让玩家无法把卡片精确放到树枝上。
		Vector2 dropPoint = ScreenToBoardWorld(screenPosition);
		string targetTask = FindTaskAt(dropPoint);
		string currentTask = PrologueMain.Get(_clueData[clueId], "task_id", "").AsString();
		if (targetTask.Length > 0)
		{
			ClearDropHighlights();
			ActionRequested?.Invoke("attach_clue", new Dictionary
			{
				["clue_id"] = clueId,
				["task_id"] = targetTask,
				["scale"] = cardScale,
				["rotation"] = cardRotation,
			});
			// 同一任务内松手时数据指纹可以保持不变，仍需恢复任务树吸附布局。
			PlaceBoardContents(_lastQuest, _lastQuestComplete);
			return;
		}

		Dictionary payload = new()
		{
			["clue_id"] = clueId,
			["x"] = worldPosition.X,
			["y"] = worldPosition.Y,
			["scale"] = cardScale,
			["rotation"] = cardRotation,
		};
		ClearDropHighlights();
		ActionRequested?.Invoke(currentTask.Length > 0 ? "detach_clue" : "update_clue", payload);
	}

	/// <summary>清除上一次拖动留下的目标强调状态。</summary>
	private void ClearDropHighlights()
	{
		foreach (TaskDropZoneView zone in _tasks.Values) zone.SetDropHighlighted(false);
	}

	/// <summary>保存旋转或缩放手柄完成后的单卡变换，不重置玩家已经摆好的位置。</summary>
	private void CommitCardTransform(string clueId, float cardScale, float cardRotation)
	{
		if (!_cards.TryGetValue(clueId, out ClueCardView? card)) return;
		Dictionary payload = new()
		{
			["clue_id"] = clueId,
			["scale"] = cardScale,
			["rotation"] = cardRotation,
		};
		if (TryGetStoredPoint(_clueData[clueId], out Vector2 point))
		{
			payload["x"] = point.X;
			payload["y"] = point.Y;
		}
		else
		{
			payload["x"] = card.Position.X;
			payload["y"] = card.Position.Y;
		}
		ActionRequested?.Invoke("update_clue", payload);
	}

	/// <summary>将拖动卡片时的屏幕坐标反算后，与任务及已挂接卡片区域命中测试。</summary>
	private string FindTaskAt(Vector2 worldPoint)
	{
		foreach ((string taskId, TaskDropZoneView zone) in _tasks)
		{
			if (!zone.Visible) continue;
			if (zone.BoardDropBounds.HasPoint(worldPoint)) return taskId;
		}
		return "";
	}

	/// <summary>卡片移动经过任务区域时高亮唯一候选任务，表示可放置而非对错提示。</summary>
	private void UpdateDropHighlight(string clueId, Vector2 screenPosition)
	{
		if (_canvas == null) return;
		// 高亮与松手判定必须共用同一个鼠标世界坐标，否则视觉提示与实际结果会相反。
		string target = FindTaskAt(ScreenToBoardWorld(screenPosition));
		foreach ((string taskId, TaskDropZoneView zone) in _tasks)
		{
			zone.SetDropHighlighted(taskId == target);
		}
	}

	/// <summary>把视口屏幕坐标转换为 BoardWorld 坐标，统一处理窗口、整板缩放和旋转。</summary>
	private Vector2 ScreenToBoardWorld(Vector2 screenPosition)
	{
		return _canvas!.ScreenToWorld(_canvas.ToCanvasLocal(screenPosition));
	}

	/// <summary>提交卡片批注；C++ 状态更新后会把文字写回同一张场景卡。</summary>
	private void CommitCardNote(string clueId, string note)
	{
		if (!_clueData.ContainsKey(clueId)) return;
		ActionRequested?.Invoke("update_clue", new Dictionary { ["clue_id"] = clueId, ["note"] = note });
	}

	/// <summary>请求 C++ 删除一张玩家主动收录的线索卡。</summary>
	private void RemoveCard(string clueId)
	{
		ActionRequested?.Invoke("remove_clue", new Dictionary { ["clue_id"] = clueId });
	}

	/// <summary>选中卡片时更新场景手柄可见性并提升该卡片绘制层级。</summary>
	private void SelectCard(string clueId)
	{
		_selectedClueId = clueId;
		foreach ((string id, ClueCardView card) in _cards)
		{
			card.SetSelected(id == clueId);
			card.ZIndex = id == clueId ? 100 : 2;
		}
		// 与窗口遵循同样的原则：选中卡片的输入顺序也跟随它的前景绘制顺序。
		if (_cards.TryGetValue(clueId, out ClueCardView? selected))
			_clueLayer!.MoveChild(selected, _clueLayer.GetChildCount() - 1);
	}

	/// <summary>将当前选中的卡片移动到画布中心，便于快速检查较远处的摘录。</summary>
	private void FocusSelectedCard()
	{
		if (_canvas == null || !_cards.TryGetValue(_selectedClueId, out ClueCardView? card)) return;
		_canvas.FocusWorldPoint(card.Position + card.PivotOffset, Mathf.Max(_canvas.Zoom, 1.1f));
	}

	/// <summary>镜头变化后刷新场景中已有的缩放率和角度文字。</summary>
	private void UpdateCameraLabels(float zoom, float rotation)
	{
		if (_zoomLabel != null) _zoomLabel.Text = Mathf.RoundToInt(zoom * 100.0f) + "%";
		if (_rotationLabel != null) _rotationLabel.Text = Mathf.RoundToInt(rotation) + "°";
	}

	/// <summary>把白板镜头参数作为单独状态动作提交，不重建任务和线索场景。</summary>
	private void SaveBoardView(Vector2 pan, float zoom, float rotation, bool autoFit)
	{
		ActionRequested?.Invoke("set_board_view", new Dictionary
		{
			["pan_x"] = pan.X,
			["pan_y"] = pan.Y,
			["zoom"] = zoom,
			["rotation"] = rotation,
			["auto_fit"] = autoFit,
		});
	}

	/// <summary>取出保存的自由卡坐标；两坐标必须同时为数值才构成可用位置。</summary>
	private static bool TryGetStoredPoint(Dictionary clue, out Vector2 point)
	{
		Variant x = PrologueMain.Get(clue, "x", default);
		Variant y = PrologueMain.Get(clue, "y", default);
		if ((x.VariantType is Variant.Type.Float or Variant.Type.Int) &&
			(y.VariantType is Variant.Type.Float or Variant.Type.Int))
		{
			point = new Vector2((float)x.AsDouble(), (float)y.AsDouble());
			return true;
		}
		point = Vector2.Zero;
		return false;
	}

	/// <summary>按旋转后的卡片外接矩形高度计算后续列与任务间距。</summary>
	private static float GetCardOccupiedHeight(ClueCardView card) => GetCardRotatedSize(card).Y;

	/// <summary>返回以卡片中心枢轴计算的旋转后世界轴对齐矩形。</summary>
	private static Rect2 GetCardWorldBounds(ClueCardView card)
	{
		Vector2 extents = GetCardRotatedSize(card);
		Vector2 center = card.Position + card.PivotOffset;
		return new Rect2(center - extents * 0.5f, extents);
	}

	/// <summary>按卡片自己的缩放和旋转角换算其世界坐标轴对齐尺寸。</summary>
	private static Vector2 GetCardRotatedSize(ClueCardView card)
	{
		float width = card.Size.X * card.CardScale;
		float height = card.Size.Y * card.CardScale;
		float radians = Mathf.DegToRad(card.CardRotation);
		float cosine = Mathf.Abs(Mathf.Cos(radians));
		float sine = Mathf.Abs(Mathf.Sin(radians));
		return new Vector2(width * cosine + height * sine, height * cosine + width * sine);
	}

	/// <summary>生成只包含白板显示数据的指纹，忽略聊天等无关剧情修订。</summary>
	private static string BuildSignature(Dictionary quest, Array clues)
	{
		// 使用结构化序列化，避免玩家输入的分隔字符与手工拼接指纹发生碰撞。
		return Json.Stringify(new Dictionary { ["quest"] = quest, ["clues"] = clues });
	}

	/// <summary>整理前提交当前批注草稿，随后由 C++ 统一清除自由布局并重新全览。</summary>
	private void TidyBoard()
	{
		foreach (ClueCardView card in _cards.Values) card.CommitNote();
		ActionRequested?.Invoke("tidy_board", new Dictionary());
		PlaceBoardContents(_lastQuest, _lastQuestComplete);
		_canvas!.FitContent();
	}

	/// <summary>仅在白板持有焦点时响应快捷键；输入批注和选中文字时保持编辑器原有语义。</summary>
	public override void _UnhandledKeyInput(InputEvent input)
	{
		if (!IsVisibleInTree() || input is not InputEventKey { Pressed: true, Echo: false } key) return;
		Control? focus = GetViewport().GuiGetFocusOwner();
		if (focus == null || !IsAncestorOf(focus) || focus is TextEdit or LineEdit) return;
		if (key.CtrlPressed || key.AltPressed || key.MetaPressed) return;
		switch (key.Keycode)
		{
			case Key.F:
				_canvas!.FitContent();
				break;
			case Key.R:
				_canvas!.ResetRotation();
				break;
			case Key.Escape:
				SelectCard("");
				break;
			case Key.Delete:
			case Key.Backspace:
				foreach (ClueCardView card in _cards.Values)
					if (card.HasSelectedText()) return;
				if (_selectedClueId.Length > 0) RemoveCard(_selectedClueId);
				break;
			default:
				return;
		}
		GetViewport().SetInputAsHandled();
	}
}
