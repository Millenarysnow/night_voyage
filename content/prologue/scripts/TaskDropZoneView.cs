#nullable enable

using Godot;
using Godot.Collections;
using System;

/// <summary>
/// 白板子任务的可复用投放区。任务标题与高亮样式来自场景，
/// 线索挂接请求通过语义事件交由白板控制器和 C++ 状态机处理。
/// </summary>
public partial class TaskDropZoneView : PanelContainer
{
	/// <summary>由白板拥有者订阅，业务状态仍交给 C++ 状态机确认。</summary>
	public event Action<string, string>? ClueDropped;

	[Export] private StyleBox? HighlightStyle { get; set; }

	private string _taskId = "";
	private StyleBox? _normalStyle;
	public Rect2 BoardDropBounds { get; private set; }

	/// <summary>把当前任务 ID、标题和完成情况写入场景已有节点。</summary>
	public void Configure(string taskId, string title, bool done)
	{
		_taskId = taskId;
		RichTextLabel label = GetNode<RichTextLabel>("Layout/Title");
		label.Clear();
		if (done) label.PushStrikethrough();
		label.AddText(title);
		if (done) label.Pop();
		_normalStyle ??= GetThemeStylebox("panel");
	}

	/// <summary>恢复场景资源配置的最小尺寸，供白板布局计算任务标题和卡片高度。</summary>
	public void ResetZoneSize()
	{
		ResetSize();
	}

	/// <summary>保存任务标题与其挂接卡片覆盖的世界坐标范围供手势命中测试。</summary>
	public void SetBoardDropBounds(Rect2 bounds)
	{
		BoardDropBounds = bounds;
	}

	/// <summary>显示拖动目标反馈；颜色、边框和圆角均由任务场景资源定义。</summary>
	public void SetDropHighlighted(bool highlighted)
	{
		if (highlighted && HighlightStyle != null) AddThemeStyleboxOverride("panel", HighlightStyle);
		else if (_normalStyle != null) AddThemeStyleboxOverride("panel", _normalStyle);
	}

	/// <summary>Godot 拖放预检只验证数据形状，不在 UI 层判断线索是否匹配任务。</summary>
	public override bool _CanDropData(Vector2 atPosition, Variant data)
	{
		return data.VariantType == Variant.Type.Dictionary && data.AsGodotDictionary().ContainsKey("clue_id");
	}

	/// <summary>把线索 ID 与场景配置的任务 ID 转发给状态机入口。</summary>
	public override void _DropData(Vector2 atPosition, Variant data)
	{
		Dictionary payload = data.AsGodotDictionary();
		ClueDropped?.Invoke(PrologueMain.Get(payload, "clue_id", "").AsString(), _taskId);
	}
}
