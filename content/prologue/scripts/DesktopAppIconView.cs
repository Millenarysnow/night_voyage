#nullable enable

using Godot;

/// <summary>桌面图标只提供点击行为；字形、标题和颜色由组合场景的元数据配置。</summary>
public partial class DesktopAppIconView : Button
{
	/// <summary>读取场景元数据并投影到模板已有的字形、标题和色块节点。</summary>
	public override void _Ready()
	{
		string glyph = GetMeta("glyph_text", "").AsString();
		string caption = GetMeta("caption", "").AsString();
		Color iconColor = GetMeta("icon_color", Colors.White).AsColor();
		GetNode<Panel>("Glyph").Modulate = iconColor;
		GetNode<Label>("Glyph/GlyphText").Text = glyph;
		GetNode<Label>("Caption").Text = caption;
		TooltipText = caption;
	}
}
