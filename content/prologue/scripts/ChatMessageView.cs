#nullable enable

using Godot;

/// <summary>聊天消息模板的展示绑定；行结构、头像和气泡样式均由 chat_message.tscn 编辑。</summary>
public partial class ChatMessageView : Control
{
	/// <summary>逐条消息高度所采用的场景行高参数。</summary>
	[Export] public int EstimatedCharactersPerLine { get; set; } = 22;

	/// <summary>将发言者与正文写入入站或玩家消息行。</summary>
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

		int lines = 0;
		foreach (string line in message.Split('\n'))
		{
			lines += Mathf.Max(1, Mathf.CeilToInt((float)line.Length / Mathf.Max(1, EstimatedCharactersPerLine)));
		}

		int height = Mathf.Max(64, 34 + lines * 23);
		CustomMinimumSize = new Vector2(444, height);
		row.CustomMinimumSize = new Vector2(444, height);
		PanelContainer bubble = row.GetNode<PanelContainer>("Bubble");
		bubble.CustomMinimumSize = new Vector2(326, height);
		row.GetNode<Label>("Bubble/BubbleContent/Message").CustomMinimumSize = new Vector2(296, lines * 22);
	}
}
