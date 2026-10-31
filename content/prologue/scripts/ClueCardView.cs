#nullable enable

using Godot;
using Godot.Collections;
using System;

/// <summary>
/// 白板线索卡的可复用交互组件。视觉结构保存在 clue_card.tscn，
/// 本脚本只处理卡片手势并通过语义事件交由 BoardView 与状态机保存。
/// </summary>
public partial class ClueCardView : Panel
{
	/// <summary>玩家选择卡片时通知白板控制器切换手柄和绘制层级。</summary>
	public event Action<string>? Selected;
	/// <summary>双击卡片题头时请求白板镜头聚焦此卡。</summary>
	public event Action<string>? FocusRequested;
	/// <summary>拖动卡片经过画布时报告指针位置以高亮任务投放区。</summary>
	public event Action<string, Vector2>? DragMoved;
	/// <summary>移动卡片松手时报告新位置和独立缩放、旋转值。</summary>
	public event Action<string, Vector2, Vector2, float, float>? DragFinished;
	/// <summary>单卡旋转或缩放操作完成后提交新的变换属性。</summary>
	public event Action<string, float, float>? TransformCommitted;
	/// <summary>任何卡片手势结束时允许控制器应用拖动期间暂存的外部状态刷新。</summary>
	public event Action? GestureEnded;
	/// <summary>指针超过点击阈值、真正开始编辑卡片时冻结白板的自动全览。</summary>
	public event Action? GestureStarted;
	/// <summary>批注编辑结束时把最终文本提交给白板控制器。</summary>
	public event Action<string, string>? NoteCommitted;
	/// <summary>玩家点击线索图片时请求打开现有灯箱。</summary>
	public event Action<string, string>? ImagePreviewRequested;
	/// <summary>玩家点击移除按钮后请求状态机删除线索。</summary>
	public event Action<string>? RemovalRequested;

	[Export] private float MinimumScale { get; set; } = 0.4f;
	[Export] private float MaximumScale { get; set; } = 2.8f;
	[Export] private float RotationStepDegrees { get; set; } = 15.0f;
	[Export] private StyleBox? SelectedStyle { get; set; }

	private TextEdit? _noteEdit;
	private TextureRect? _image;
	private RichTextLabel? _text;
	private RichTextLabel? _caption;
	private Control? _rotateHandle;
	private Control? _scaleHandle;
	private StyleBox? _normalStyle;
	private GestureMode _gesture;
	private Vector2 _dragAnchorOffset;
	private Vector2 _gestureCenter;
	private Vector2 _gestureStartPointer;
	private float _gestureStartAngle;
	private float _gestureStartRotation;
	private float _gestureStartScale;
	private float _gestureStartDistance;
	private bool _gestureMoved;
	private bool _previewOnRelease;
	private string _clueId = "";
	private string _imagePath = "";
	private float _cardScale = 1.0f;
	private float _cardRotation;
	private string _savedNote = "";
	private bool _signalsConnected;

	private enum GestureMode
	{
		None,
		Move,
		Rotate,
		Scale,
	}

	public string ClueId => _clueId;
	public float CardScale => _cardScale;
	public float CardRotation => _cardRotation;
	public bool IsGestureActive => _gesture != GestureMode.None;

	/// <summary>读取卡片模板内部节点，将快照中的线索字段投影到场景资源。</summary>
	public void Configure(Dictionary clue)
	{
		_clueId = PrologueMain.Get(clue, "id", "").AsString();
		_imagePath = PrologueMain.Get(clue, "image", "").AsString();
		bool isImage = PrologueMain.Get(clue, "kind", "text").AsString() == "image";

		GetNode<Label>("Layout/Header/Kind").Text = isImage ? "图片" : "摘录";
		GetNode<Label>("Layout/Header/Source").Text = PrologueMain.Get(clue, "source", "线索").AsString();
		_text = GetNode<RichTextLabel>("Layout/Text");
		string excerpt = PrologueMain.Get(clue, "text", "").AsString();
		if (_text.Text != excerpt) _text.Text = excerpt;
		_text.Visible = !isImage;
		_caption = GetNode<RichTextLabel>("Layout/Caption");

		_image = GetNode<TextureRect>("Layout/Image");
		_image.Visible = isImage;
		if (isImage)
		{
			_image.Texture = _imagePath.Length > 0 ? GD.Load<Texture2D>(_imagePath) : null;
			if (_image.Texture == null) GD.PushError("线索图片资源无法读取：" + _imagePath);
			string caption = PrologueMain.Get(clue, "caption", "").AsString();
			if (_caption.Text != caption) _caption.Text = caption;
			_caption.Visible = _caption.Text.Length > 0;
		}
		else
		{
			_image.Texture = null;
			_caption.Visible = false;
		}

		_cardScale = Mathf.Clamp((float)PrologueMain.Get(clue, "scale", 1.0).AsDouble(), MinimumScale, MaximumScale);
		_cardRotation = (float)PrologueMain.Get(clue, "rotation", 0.0).AsDouble();
		Scale = Vector2.One * _cardScale;
		Rotation = Mathf.DegToRad(_cardRotation);
		_noteEdit = GetNode<TextEdit>("Layout/Note");
		_savedNote = PrologueMain.Get(clue, "note", "").AsString();
		// 其他卡片或剧情变更时保留正在编辑的草稿与光标；离开焦点后再提交当前文本。
		if (!_noteEdit.HasFocus()) _noteEdit.Text = _savedNote;
		_rotateHandle = GetNode<Control>("RotateHandle");
		_scaleHandle = GetNode<Control>("ScaleHandle");
		if (!_signalsConnected)
		{
			_normalStyle = GetThemeStylebox("panel");
			_text.GuiInput += OnSelectableTextInput;
			_caption.GuiInput += OnSelectableTextInput;
			_noteEdit.FocusEntered += () => Selected?.Invoke(_clueId);
			_noteEdit.FocusExited += CommitNote;
			_rotateHandle.GuiInput += input => OnHandleInput(input, GestureMode.Rotate);
			_scaleHandle.GuiInput += input => OnHandleInput(input, GestureMode.Scale);
			GetNode<Button>("Layout/Header/Remove").Pressed += () => RemovalRequested?.Invoke(_clueId);
			_signalsConnected = true;
		}
	}

	/// <summary>加入场景并完成容器布局后恢复模板尺寸及卡片中心枢轴。</summary>
	public void ResetCardSize()
	{
		// Panel 不会像 PanelContainer 那样把每个手柄也拉伸到整张卡片。
		// 正文边距留在场景里，脚本只按正文实际最小高度调整数据驱动的卡片高度。
		Control layout = GetNode<Control>("Layout");
		Size = new Vector2(CustomMinimumSize.X,
			Mathf.Max(CustomMinimumSize.Y, layout.GetCombinedMinimumSize().Y + layout.OffsetTop - layout.OffsetBottom));
		PivotOffset = Size * 0.5f;
	}

	/// <summary>切换选中边框和变换手柄；选中样式在 clue_card.tscn 中编辑。</summary>
	public void SetSelected(bool selected)
	{
		if (_rotateHandle != null) _rotateHandle.Visible = selected;
		if (_scaleHandle != null) _scaleHandle.Visible = selected;
		if (selected && SelectedStyle != null) AddThemeStyleboxOverride("panel", SelectedStyle);
		else if (_normalStyle != null) AddThemeStyleboxOverride("panel", _normalStyle);
	}

	/// <summary>鼠标按下时辨认移动、旋转或缩放目标，并保存当前手势的基准值。</summary>
	public override void _GuiInput(InputEvent @event)
	{
		if (@event is not InputEventMouseButton { Pressed: true, ButtonIndex: MouseButton.Left } button)
		{
			return;
		}

		Vector2 screenPointer = GetGlobalTransformWithCanvas() * button.Position;
		if (IsPointerOver(GetNode<Button>("Layout/Header/Remove"), screenPointer)) return;
		if (IsPointerOver(_noteEdit, screenPointer)) return;
		if (IsPointerOver(_text, screenPointer) || IsPointerOver(_caption, screenPointer)) return;
		if (IsPointerOver(_image, screenPointer))
		{
			// 图片轻点放大、拖动则挪卡片，与网页原型的点击/拖动区分保持一致。
			BeginGesture(screenPointer, GestureMode.Move, true);
			AcceptEvent();
			return;
		}

		if (button.DoubleClick)
		{
			Selected?.Invoke(_clueId);
			FocusRequested?.Invoke(_clueId);
			AcceptEvent();
			return;
		}
		BeginGesture(screenPointer, GestureMode.Move);
		AcceptEvent();
	}

	/// <summary>手柄拥有独立输入节点，确保点中其可见区域就启动相应变换手势。</summary>
	private void OnHandleInput(InputEvent input, GestureMode mode)
	{
		if (input is not InputEventMouseButton { Pressed: true, ButtonIndex: MouseButton.Left } button) return;
		Control handle = mode == GestureMode.Rotate ? _rotateHandle! : _scaleHandle!;
		BeginGesture(handle.GetGlobalTransformWithCanvas() * button.Position, mode);
		handle.AcceptEvent();
	}

	/// <summary>选择正文仍然选中卡片，但把文字拖选和复制交给 RichTextLabel。</summary>
	private void OnSelectableTextInput(InputEvent input)
	{
		if (input is InputEventMouseButton { Pressed: true, ButtonIndex: MouseButton.Left })
			Selected?.Invoke(_clueId);
	}

	/// <summary>从视口指针初始化手势；坐标在单卡与整板变换间逐层转换。</summary>
	private void BeginGesture(Vector2 screenPointer, GestureMode mode, bool previewOnRelease = false)
	{
		GrabFocus();
		Selected?.Invoke(_clueId);
		Vector2 parentPointer = ParentWorldFromScreen(screenPointer);
		Vector2 localPointer = GetGlobalTransformWithCanvas().AffineInverse() * screenPointer;
		_gestureStartPointer = parentPointer;
		_gestureStartRotation = _cardRotation;
		_gestureStartScale = _cardScale;
		_gestureCenter = Position + PivotOffset;
		_gestureStartAngle = Mathf.Atan2(parentPointer.Y - _gestureCenter.Y, parentPointer.X - _gestureCenter.X);
		_gestureStartDistance = Mathf.Max(1.0f, parentPointer.DistanceTo(_gestureCenter));
		_dragAnchorOffset = PivotOffset + RotateVector((localPointer - PivotOffset) * _cardScale,
			Mathf.DegToRad(_cardRotation));
		_gestureMoved = false;
		_previewOnRelease = previewOnRelease;
		_gesture = mode;
	}

	/// <summary>全局捕获鼠标移动与松开，允许拖动跨越任务区并在画布边缘结束。</summary>
	public override void _Input(InputEvent @event)
	{
		if (_gesture == GestureMode.None) return;
		if (@event is InputEventMouseMotion motion)
		{
			Vector2 parentPointer = ParentWorldFromScreen(motion.Position);
			if (!_gestureMoved && parentPointer.DistanceTo(_gestureStartPointer) > 2.0f)
			{
				_gestureMoved = true;
				GestureStarted?.Invoke();
			}
			switch (_gesture)
			{
				case GestureMode.Move:
					Position = parentPointer - _dragAnchorOffset;
					DragMoved?.Invoke(_clueId, motion.Position);
					break;
				case GestureMode.Rotate:
					float currentAngle = Mathf.Atan2(parentPointer.Y - _gestureCenter.Y,
						parentPointer.X - _gestureCenter.X);
					float deltaRadians = Mathf.PosMod(currentAngle - _gestureStartAngle + Mathf.Pi,
						Mathf.Tau) - Mathf.Pi;
					float deltaDegrees = Mathf.RadToDeg(deltaRadians);
					float snappedRotation = Mathf.Round((_gestureStartRotation + deltaDegrees) / RotationStepDegrees) * RotationStepDegrees;
					SetTransform(snappedRotation, _cardScale);
					break;
				case GestureMode.Scale:
					float nextScale = _gestureStartScale * parentPointer.DistanceTo(_gestureCenter) / _gestureStartDistance;
					SetTransform(_cardRotation, Mathf.Clamp(nextScale, MinimumScale, MaximumScale));
					break;
			}
			GetViewport().SetInputAsHandled();
			return;
		}

		if (@event is InputEventMouseButton { Pressed: false, ButtonIndex: MouseButton.Left } release)
		{
			GestureMode finishedGesture = _gesture;
			_gesture = GestureMode.None;
			if (_gestureMoved && finishedGesture == GestureMode.Move)
			{
				DragFinished?.Invoke(_clueId, release.Position, Position, _cardScale, _cardRotation);
			}
			else if (_gestureMoved && finishedGesture is GestureMode.Rotate or GestureMode.Scale)
			{
				TransformCommitted?.Invoke(_clueId, _cardScale, _cardRotation);
			}
			else if (_previewOnRelease)
			{
				ImagePreviewRequested?.Invoke(_imagePath, _caption?.Text ?? "");
			}
			_previewOnRelease = false;
			// 释放事件必须继续送到 GUI，让 Godot 清除按下时建立的鼠标捕获。
			// 若在全局 _Input 提前拦住释放，下一次点击仍会被发给旧手柄。
			GestureEnded?.Invoke();
		}
	}

	/// <summary>批注离开焦点后才提交，避免每次按键重建白板并丢失输入焦点。</summary>
	public void CommitNote()
	{
		if (_noteEdit == null || _noteEdit.Text == _savedNote) return;
		_savedNote = _noteEdit.Text;
		NoteCommitted?.Invoke(_clueId, _savedNote);
	}

	/// <summary>卡片正文有选区时保留 Delete/Backspace 的文本编辑语义，避免误删整卡。</summary>
	public bool HasSelectedText() => (_text?.GetSelectedText().Length ?? 0) > 0 ||
		(_caption?.GetSelectedText().Length ?? 0) > 0;

	/// <summary>统一更新单卡缩放和旋转；场景枢轴位于卡片中心，变换不会带动卡片中心。</summary>
	private void SetTransform(float rotation, float scale)
	{
		_cardRotation = Mathf.PosMod(rotation + 180.0f, 360.0f) - 180.0f;
		_cardScale = scale;
		Rotation = Mathf.DegToRad(_cardRotation);
		Scale = Vector2.One * _cardScale;
	}

	/// <summary>把视口鼠标位置逆变换为父级世界层坐标，抵消整板缩放和平移。</summary>
	private Vector2 ParentWorldFromScreen(Vector2 screenPosition)
	{
		return GetParent<Control>().GetGlobalTransformWithCanvas().AffineInverse() * screenPosition;
	}

	/// <summary>使用控件全局逆变换测试鼠标位置，支持已旋转或缩放的卡片子控件。</summary>
	private static bool IsPointerOver(Control? control, Vector2 screenPosition)
	{
		if (control == null || !control.Visible) return false;
		Vector2 localPointer = control.GetGlobalTransformWithCanvas().AffineInverse() * screenPosition;
		return new Rect2(Vector2.Zero, control.Size).HasPoint(localPointer);
	}

	/// <summary>按卡片旋转角计算局部锚点在父级世界坐标中的偏移。</summary>
	private static Vector2 RotateVector(Vector2 value, float radians)
	{
		float cosine = Mathf.Cos(radians);
		float sine = Mathf.Sin(radians);
		return new Vector2(value.X * cosine - value.Y * sine, value.X * sine + value.Y * cosine);
	}
}
