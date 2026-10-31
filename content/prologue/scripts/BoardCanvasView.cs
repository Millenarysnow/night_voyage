#nullable enable

using Godot;
using System;

/// <summary>
/// 白板画布只管理镜头变换和空白处手势；任务与线索由 BoardView 投影到场景里。
/// 所有画布尺寸、缩放上限和框选外观都由 board_window_content.tscn 配置。
/// </summary>
public partial class BoardCanvasView : Control
{
	/// <summary>画布视角变化后通知工具栏刷新缩放率和角度。</summary>
	public event Action<float, float>? ViewChanged;
	/// <summary>镜头手势完成后将整块白板的视角写回权威状态快照。</summary>
	public event Action<Vector2, float, float, bool>? ViewStateCommitted;
	/// <summary>点击画布空白时让控制器清空卡片选中态并提交未结束的批注。</summary>
	public event Action? BackgroundPressed;

	[Export] private float MinimumZoom { get; set; } = 0.15f;
	[Export] private float MaximumZoom { get; set; } = 4.0f;
	[Export] private float MaximumFitZoom { get; set; } = 1.15f;
	[Export] private float WheelZoomStep { get; set; } = 1.15f;
	[Export] private float FitPadding { get; set; } = 26.0f;

	private Control? _world;
	private Panel? _marquee;
	private Vector2 _pan;
	private Vector2 _lastPointer;
	private Vector2 _marqueeStart;
	private float _zoom = 1.0f;
	private float _rotationDegrees;
	private bool _autoFit = true;
	private bool _hasContent;
	private Rect2 _contentBounds;
	private PointerMode _pointerMode;
	private MouseButton _activePointerButton;
	public float Zoom => _zoom;
	public float ViewRotationDegrees => _rotationDegrees;

	private enum PointerMode
	{
		None,
		Pan,
		Marquee,
	}

	/// <summary>等所有场景子节点就绪后缓存世界层和框选节点，并监听尺寸变化。</summary>
	public override void _Ready()
	{
		_world = GetNode<Control>("BoardWorld");
		_marquee = GetNode<Panel>("Marquee");
		_world.MouseFilter = MouseFilterEnum.Ignore;
		_world.PivotOffset = Vector2.Zero;
		_marquee.Visible = false;
		Resized += OnCanvasResized;
		UpdateWorldTransform();
		EmitViewChanged();
	}

	/// <summary>按场景层级把屏幕局部坐标反算成板子世界坐标。</summary>
	public Vector2 ScreenToWorld(Vector2 screenPosition)
	{
		Vector2 relative = screenPosition - WorldOriginOnCanvas();
		return Rotate(relative, -Mathf.DegToRad(_rotationDegrees)) / _zoom;
	}

	/// <summary>把视口的全局鼠标坐标转换为本白板画布的局部屏幕坐标。</summary>
	public Vector2 ToCanvasLocal(Vector2 viewportPosition)
	{
		return GetGlobalTransformWithCanvas().AffineInverse() * viewportPosition;
	}

	/// <summary>读取并恢复 C++ 快照中已经保存的镜头平移、缩放、角度和自动全览状态。</summary>
	public void SetView(Vector2 pan, float zoom, float rotationDegrees, bool autoFit)
	{
		_pan = pan;
		_zoom = Mathf.Clamp(zoom, MinimumZoom, MaximumZoom);
		_rotationDegrees = WrapDegrees(rotationDegrees);
		_autoFit = autoFit;
		// 自动全览的镜头由当前内容边界推导；快照中的初始平移不能覆盖计算结果。
		if (_autoFit && _hasContent) FitContent(false);
		else UpdateWorldTransform();
	}

	/// <summary>将板子世界坐标映射为画布内部的屏幕局部坐标。</summary>
	public Vector2 WorldToScreen(Vector2 worldPosition)
	{
		return WorldOriginOnCanvas() + Rotate(worldPosition * _zoom, Mathf.DegToRad(_rotationDegrees));
	}

	/// <summary>传入本次快照的总内容边界，玩家未手动调整视角时自动重新全览。</summary>
	public void SetContentBounds(Rect2 bounds, bool hasContent)
	{
		_contentBounds = bounds;
		_hasContent = hasContent;
		if (_autoFit && Size.X > 0.0f && Size.Y > 0.0f)
		{
			FitContent(false);
		}
		else
		{
			UpdateWorldTransform();
		}
	}

	/// <summary>用指定世界矩形重算镜头，使矩形完整落入畫布可视区。</summary>
	public void FitContent(bool persistView = true)
	{
		_autoFit = true;
		if (!_hasContent || Size.X <= 0.0f || Size.Y <= 0.0f)
		{
			_zoom = 1.0f;
			_rotationDegrees = 0.0f;
			_pan = Vector2.Zero;
			UpdateWorldTransform();
			if (persistView) CommitView();
			return;
		}

		Rect2 rotatedBounds = GetRotatedBounds(_contentBounds, _rotationDegrees);
		Vector2 available = new(
			Mathf.Max(1.0f, Size.X - FitPadding * 2.0f),
			Mathf.Max(1.0f, Size.Y - FitPadding * 2.0f));
		float scaleX = available.X / Mathf.Max(1.0f, rotatedBounds.Size.X);
		float scaleY = available.Y / Mathf.Max(1.0f, rotatedBounds.Size.Y);
		_zoom = Mathf.Clamp(Mathf.Min(Mathf.Min(scaleX, scaleY), MaximumFitZoom), MinimumZoom, MaximumZoom);
		Vector2 rotatedCenter = Rotate(_contentBounds.GetCenter() * _zoom, Mathf.DegToRad(_rotationDegrees));
		_pan = -rotatedCenter;
		UpdateWorldTransform();
		if (persistView) CommitView();
	}

	/// <summary>把指定世界点移到画布中心，并可选地同时调整缩放比例。</summary>
	public void FocusWorldPoint(Vector2 worldPoint, float? zoom = null)
	{
		MarkManualView();
		if (zoom.HasValue) _zoom = Mathf.Clamp(zoom.Value, MinimumZoom, MaximumZoom);
		_pan = -Rotate(worldPoint * _zoom, Mathf.DegToRad(_rotationDegrees));
		UpdateWorldTransform();
		CommitView();
	}

	/// <summary>编辑卡片时固定当前视角，防止卡片移远或变大后全览自动拉动镜头。</summary>
	public void PreserveCurrentView()
	{
		if (!_autoFit) return;
		MarkManualView();
		CommitView();
	}

	/// <summary>将鼠标下的世界位置固定在屏幕原处，围绕该点修改缩放。</summary>
	public void ZoomAt(float factor, Vector2 screenPoint)
	{
		Vector2 anchor = ScreenToWorld(screenPoint);
		float nextZoom = Mathf.Clamp(_zoom * factor, MinimumZoom, MaximumZoom);
		if (Mathf.IsEqualApprox(nextZoom, _zoom)) return;
		MarkManualView();
		_zoom = nextZoom;
		_pan = screenPoint - Size * 0.5f - Rotate(anchor * _zoom, Mathf.DegToRad(_rotationDegrees));
		UpdateWorldTransform();
		CommitView();
	}

	/// <summary>围绕鼠标锚点旋转整张白板，避免旋转时鼠标下的内容跳位。</summary>
	public void RotateAt(float deltaDegrees, Vector2 screenPoint)
	{
		Vector2 anchor = ScreenToWorld(screenPoint);
		MarkManualView();
		_rotationDegrees = WrapDegrees(_rotationDegrees + deltaDegrees);
		_pan = screenPoint - Size * 0.5f - Rotate(anchor * _zoom, Mathf.DegToRad(_rotationDegrees));
		UpdateWorldTransform();
		CommitView();
	}

	/// <summary>只把板面角度归零，保留当前鼠标锚点和缩放。</summary>
	public void ResetRotation()
	{
		RotateAt(-_rotationDegrees, Size * 0.5f);
	}

	/// <summary>重置到 1:1、零角度和左上方留白的初始视角。</summary>
	public void ResetView()
	{
		MarkManualView();
		_zoom = 1.0f;
		_rotationDegrees = 0.0f;
		_pan = new Vector2(FitPadding, FitPadding) - Size * 0.5f;
		UpdateWorldTransform();
		CommitView();
	}

	/// <summary>把当前屏幕框选区域转换到世界坐标后进行全览。</summary>
	public void FitScreenRect(Rect2 screenRect)
	{
		if (screenRect.Size.X <= 0.0f || screenRect.Size.Y <= 0.0f) return;
		// 框选针对当前屏幕矩形取景，保留整板旋转，不改写全览所需的真实内容边界。
		Vector2 center = ScreenToWorld(screenRect.GetCenter());
		float zoom = _zoom * Mathf.Min(Size.X / screenRect.Size.X, Size.Y / screenRect.Size.Y);
		FocusWorldPoint(center, zoom);
	}

	/// <summary>记录当前手势坐标，在鼠标释放前持续更新平移或取景框。</summary>
	public override void _GuiInput(InputEvent @event)
	{
		if (@event is InputEventMouseButton button)
		{
			if (button.ButtonIndex is MouseButton.Left or MouseButton.Middle)
			{
				if (button.Pressed)
				{
					// 正常 GUI 路由下 _Input 已先开启手势；只有它没有收到事件时才由此兜底，避免重复触发。
					if (_pointerMode != PointerMode.None) return;
					BeginPointerGesture(button, button.Position);
					AcceptEvent();
					return;
				}

			}
			return;
		}
	}

	/// <summary>手势在全局输入阶段持续跟踪，使平移和框选跨过卡片时仍可完成。</summary>
	public override void _Input(InputEvent @event)
	{
		// CRT 插件将事件直接推入内容子视口时，控件 GUI 阶段可能不会发出 _GuiInput。
		// 因此在全局输入阶段按当前悬停控件开启手势，确保真实桌面和普通白板场景使用同一条路径。
		if (@event is InputEventMouseButton { Pressed: true } press &&
			press.ButtonIndex is MouseButton.Left or MouseButton.Middle && _pointerMode == PointerMode.None)
		{
			Control? hovered = GetViewport().GuiGetHoveredControl();
			if (IsCanvasBackground(hovered))
			{
				BeginPointerGesture(press, ToCanvasLocal(press.Position));
				GetViewport().SetInputAsHandled();
				return;
			}
		}
		// 正文可选字控件会截住 GUI 冒泡，因此滚轮在输入阶段统一处理。
		// 只接受本画布当前悬停的控件，防止白板盖在浏览器下面时抢走滚轮。
		if (@event is InputEventMouseButton { Pressed: true } wheel &&
			wheel.ButtonIndex is MouseButton.WheelUp or MouseButton.WheelDown)
		{
			Control? hovered = GetViewport().GuiGetHoveredControl();
			bool overNote = false;
			// TextEdit 的内部滚动条也是独立控件，需要连同其祖先一起识别。
			for (Node? node = hovered; node != null && node != this; node = node.GetParent())
				if (node is TextEdit) { overNote = true; break; }
			if (IsVisibleInTree() && hovered != null && (hovered == this || IsAncestorOf(hovered)) &&
				!overNote)
			{
				Vector2 pointer = ToCanvasLocal(wheel.Position);
				if (wheel.AltPressed || wheel.ShiftPressed)
					RotateAt(wheel.ButtonIndex == MouseButton.WheelUp ? -4.0f : 4.0f, pointer);
				else ZoomAt(wheel.ButtonIndex == MouseButton.WheelUp ? WheelZoomStep : 1.0f / WheelZoomStep, pointer);
				GetViewport().SetInputAsHandled();
				return;
			}
		}
		if (_pointerMode == PointerMode.None) return;
		if (@event is InputEventMouseMotion motion)
		{
			Vector2 pointer = ToCanvasLocal(motion.Position);
			if (_pointerMode == PointerMode.Pan)
			{
				_pan += pointer - _lastPointer;
				UpdateWorldTransform();
			}
			else
			{
				UpdateMarquee(pointer);
			}
			_lastPointer = pointer;
			GetViewport().SetInputAsHandled();
			return;
		}

		if (@event is InputEventMouseButton { Pressed: false } release && release.ButtonIndex == _activePointerButton)
		{
			PointerMode releasedMode = _pointerMode;
			Vector2 pointer = ToCanvasLocal(release.Position);
			if (releasedMode == PointerMode.Marquee)
			{
				Rect2 selection = Rect2FromPoints(_marqueeStart, pointer);
				_marquee!.Visible = false;
				if (selection.Size.X >= 26.0f && selection.Size.Y >= 26.0f) FitScreenRect(selection);
			}
			_pointerMode = PointerMode.None;
			if (releasedMode == PointerMode.Pan) CommitView();
			// GUI 仍需要这次释放事件解除画布自己的鼠标捕获。
		}
	}

	/// <summary>统一初始化空白处的平移、框选和双击全览手势，避免 GUI 与 CRT 路由产生两份状态。</summary>
	private void BeginPointerGesture(InputEventMouseButton button, Vector2 pointer)
	{
		GrabFocus();
		BackgroundPressed?.Invoke();
		if (button.ButtonIndex == MouseButton.Left && button.DoubleClick && !button.CtrlPressed)
		{
			FitContent();
			return;
		}

		_lastPointer = pointer;
		_activePointerButton = button.ButtonIndex;
		if (button.ButtonIndex == MouseButton.Left && button.CtrlPressed)
		{
			_pointerMode = PointerMode.Marquee;
			_marqueeStart = pointer;
			UpdateMarquee(pointer);
			_marquee!.Visible = true;
		}
		else
		{
			_pointerMode = PointerMode.Pan;
			MarkManualView();
		}
	}

	/// <summary>只有画布空白或其背景节点才可开始平移，卡片、任务区和文本编辑控件保留各自交互。</summary>
	private bool IsCanvasBackground(Control? hovered)
	{
		if (!IsVisibleInTree() || hovered == null) return false;
		if (hovered == this) return true;
		for (Node? node = hovered; node != null && node != this; node = node.GetParent())
		{
			if (node is ClueCardView or TaskDropZoneView or TextEdit or LineEdit or Button)
				return false;
		}
		return IsAncestorOf(hovered);
	}

	/// <summary>画布尺寸变化后保持世界原点位于画布中心，并在自动模式下重做全览。</summary>
	private void OnCanvasResized()
	{
		UpdateWorldTransform();
		if (_autoFit) FitContent(false);
	}

	/// <summary>把世界层锚在视口中心，再应用玩家当前平移、缩放和旋转。</summary>
	private void UpdateWorldTransform()
	{
		if (_world == null) return;
		_world.Size = Size;
		_world.Position = Size * 0.5f + _pan;
		_world.Scale = Vector2.One * _zoom;
		_world.Rotation = Mathf.DegToRad(_rotationDegrees);
		EmitViewChanged();
	}

	/// <summary>根据当前鼠标端点更新场景中预先放置的框选矩形。</summary>
	private void UpdateMarquee(Vector2 pointer)
	{
		if (_marquee == null) return;
		Rect2 rect = Rect2FromPoints(_marqueeStart, pointer);
		_marquee.Position = rect.Position;
		_marquee.Size = rect.Size;
	}

	/// <summary>将视口中心和拖动偏移合并为世界层原点在画布中的位置。</summary>
	private Vector2 WorldOriginOnCanvas() => Size * 0.5f + _pan;

	/// <summary>用户主动拖动或调整视角后，窗口尺寸变化不再擅自触发全览。</summary>
	private void MarkManualView()
	{
		_autoFit = false;
	}

	/// <summary>向工具栏投影当前镜头数值。</summary>
	private void EmitViewChanged() => ViewChanged?.Invoke(_zoom, _rotationDegrees);

	/// <summary>将当前可视状态转换为原点偏移并写回 C++ 白板状态。</summary>
	private void CommitView() => ViewStateCommitted?.Invoke(_pan, _zoom, _rotationDegrees, _autoFit);

	/// <summary>取得矩形四角旋转后的轴对齐包围盒，供全览缩放计算使用。</summary>
	private static Rect2 GetRotatedBounds(Rect2 bounds, float degrees)
	{
		float radians = Mathf.DegToRad(degrees);
		Vector2[] points =
		{
			Rotate(bounds.Position, radians),
			Rotate(new Vector2(bounds.End.X, bounds.Position.Y), radians),
			Rotate(bounds.End, radians),
			Rotate(new Vector2(bounds.Position.X, bounds.End.Y), radians),
		};
		Vector2 min = points[0];
		Vector2 max = points[0];
		foreach (Vector2 point in points)
		{
			min = min.Min(point);
			max = max.Max(point);
		}
		return new Rect2(min, max - min);
	}

	/// <summary>統一用向量旋转执行屏幕到世界以及世界到屏幕的逆运算。</summary>
	private static Vector2 Rotate(Vector2 value, float radians)
	{
		float cosine = Mathf.Cos(radians);
		float sine = Mathf.Sin(radians);
		return new Vector2(value.X * cosine - value.Y * sine, value.X * sine + value.Y * cosine);
	}

	/// <summary>将旋转角限制在负一百八十到正一百八十度之间。</summary>
	private static float WrapDegrees(float degrees)
	{
		return Mathf.PosMod(degrees + 180.0f, 360.0f) - 180.0f;
	}

	/// <summary>以两点生成宽高非负的矩形，适用于任意拖动方向的框选。</summary>
	private static Rect2 Rect2FromPoints(Vector2 first, Vector2 second)
	{
		Vector2 min = first.Min(second);
		Vector2 max = first.Max(second);
		return new Rect2(min, max - min);
	}
}
