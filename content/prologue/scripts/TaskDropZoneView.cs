#nullable enable

using Godot;
using Godot.Collections;
using System;

/// <summary>白板任务投放模板只接收线索 ID，并向外发出任务挂接请求。</summary>
public partial class TaskDropZoneView : PanelContainer
{
	/// <summary>由白板拥有者订阅，业务状态仍交给 C++ 状态机确认。</summary>
	public event Action<string, string>? ClueDropped;

	private string _taskId = "";

	/// <summary>把当前任务 ID、标题和完成情况写入场景已有节点。</summary>
	public void Configure(string taskId, string title, bool done)
	{
		_taskId = taskId;
		GetNode<Label>("Layout/Title").Text = done ? "已完成　" + title : title;
		GetNode<Label>("Layout/Title").Modulate = done ? new Color("8dd9a8") : new Color("f0c66d");
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
