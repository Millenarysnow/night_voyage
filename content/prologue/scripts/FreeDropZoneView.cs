#nullable enable

using Godot;
using Godot.Collections;
using System;

/// <summary>白板线索自由区模板允许玩家把任务中的线索拖回未分类区域。</summary>
public partial class FreeDropZoneView : FlowContainer
{
	/// <summary>白板拥有者订阅后向 C++ 状态机提交解除挂接请求。</summary>
	public event Action<string>? ClueDropped;

	/// <summary>拖放预检只要求提供有效线索句柄。</summary>
	public override bool _CanDropData(Vector2 atPosition, Variant data)
	{
		return data.VariantType == Variant.Type.Dictionary && data.AsGodotDictionary().ContainsKey("clue_id");
	}

	/// <summary>将线索稳定 ID 发送给白板拥有者处理。</summary>
	public override void _DropData(Vector2 atPosition, Variant data)
	{
		Dictionary payload = data.AsGodotDictionary();
		ClueDropped?.Invoke(PrologueMain.Get(payload, "clue_id", "").AsString());
	}
}
