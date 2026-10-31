#nullable enable

using Godot;

/// <summary>聊天消息模板的展示绑定；行结构、头像和气泡样式均由 chat_message.tscn 编辑。</summary>
public partial class ChatMessageView : MarginContainer
{
	/// <summary>只绑定内容和方向，换行、气泡与整行高度由场景容器按实际字体排版。</summary>
	public void Configure(string speaker, string message, bool fromPlayer)
	{
		Control incoming = GetNode<Control>("IncomingRow");
		Control outgoing = GetNode<Control>("OutgoingRow");
		incoming.Visible = !fromPlayer;
		outgoing.Visible = fromPlayer;

		Control row = fromPlayer ? outgoing : incoming;
		row.GetNode<Label>("Bubble/BubbleContent/Speaker").Text = speaker;
		row.GetNode<Label>("Bubble/BubbleContent/Message").Text = message;
		if (fromPlayer) row.GetNode<Label>("Avatar").Text = speaker.Length > 0 ? speaker[..1] : "我";
	}
}
