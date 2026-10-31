#nullable enable

using Godot;
using Godot.Collections;

/// <summary>白板线索卡把显示数据与拖放句柄绑定到可编辑的 clue_card.tscn。</summary>
public partial class ClueCardView : PanelContainer
{
	private Dictionary _clue = new();

	/// <summary>接收状态机快照中的线索字段并刷新卡片文字。</summary>
	public void Configure(Dictionary clue)
	{
		_clue = clue;
		GetNode<Label>("Content/Source").Text = PrologueMain.Get(_clue, "source", "线索").AsString();
		string text = PrologueMain.Get(_clue, "text", "").AsString();
		GetNode<Label>("Content/Text").Text = text.Length == 0 ? "照片线索" : text;
	}

	/// <summary>拖动预览由单独的场景模板实例化，拖放数据只携带稳定线索 ID。</summary>
	public override Variant _GetDragData(Vector2 atPosition)
	{
		PackedScene? previewScene = GD.Load<PackedScene>("res://content/prologue/scenes/clue_drag_preview.tscn");
		if (previewScene?.Instantiate() is Label preview)
		{
			preview.Text = PrologueMain.Get(_clue, "source", "线索").AsString();
			SetDragPreview(preview);
		}
		return new Dictionary { ["clue_id"] = PrologueMain.Get(_clue, "id", "").AsString() };
	}
}
