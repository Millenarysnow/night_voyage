#nullable enable

using Godot;
using System.Collections.Generic;

/// <summary>任务树的连接线绘制层；线宽、颜色和节点位置均由场景与 BoardView 管理。</summary>
public partial class BoardLinksView : Control
{
	/// <summary>一段连接线的端点及其层级语义。</summary>
	public readonly record struct Segment(Vector2 From, Vector2 To, bool IsSecondary);

	[Export] private Color MainLineColor { get; set; } = new("#d9a44199");
	[Export] private Color SecondaryLineColor { get; set; } = new("#d9a44166");
	[Export] private float MainLineWidth { get; set; } = 2.0f;
	[Export] private float SecondaryLineWidth { get; set; } = 1.5f;
	[Export] private float SecondaryDashLength { get; set; } = 4.0f;

	private readonly List<Segment> _segments = new();

	/// <summary>替换整组连接线并触发重绘，避免旧任务树的线段留在画布上。</summary>
	public void SetSegments(IEnumerable<Segment> segments)
	{
		_segments.Clear();
		_segments.AddRange(segments);
		QueueRedraw();
	}

	/// <summary>按连接线类型使用场景导出的颜色和粗细绘制任务树。</summary>
	public override void _Draw()
	{
		foreach (Segment segment in _segments)
		{
			Color color = segment.IsSecondary ? SecondaryLineColor : MainLineColor;
			float width = segment.IsSecondary ? SecondaryLineWidth : MainLineWidth;
			if (segment.IsSecondary)
				DrawDashedLine(segment.From, segment.To, color, width, SecondaryDashLength, true, true);
			else DrawLine(segment.From, segment.To, color, width, true);
		}
	}
}
