#nullable enable

using Godot;
using Godot.Collections;
using System;

/// <summary>
/// 可复用浏览器外壳：场景保存导航控件和 CEF 插槽，本类只连接输入与状态投影。
/// 站点地址、历史、搜索和剧情规则全部由 C++ 决定；切换站点时加载各自独立的本地网页。
/// </summary>
public partial class BrowserView : Control
{
	private const string ExtensionPath = "res://addons/godot_cef/godot_cef.gdextension";
	private Control _host = null!;
	private LineEdit _address = null!;
	private Button _back = null!;
	private Button _forward = null!;
	private Label _error = null!;
	private Label _status = null!;
	private Control? _surface;
	private Dictionary _snapshot = new();
	private string _resource = "";
	private bool _documentReady;

	/// <summary>向协调器提交语义动作，网页与浏览器外壳不直接修改权威状态。</summary>
	public event Action<string, Dictionary>? ActionRequested;
	/// <summary>转发真实网页的绘制回执，供集成验收检查 DOM、图片和跨站导航。</summary>
	public event Action<Dictionary>? PageStateApplied;
	/// <summary>暴露扩展自身的脚本执行接口，供集成测试驱动真实 DOM。</summary>
	public Control? CefSurface => _surface;

	/// <summary>所有可见节点都预置在 tscn 中；运行时只连接它们已有的信号。</summary>
	public override void _Ready()
	{
		_host = GetNode<Control>("BrowserHost");
		_address = GetNode<LineEdit>("Toolbar/Controls/Address");
		_back = GetNode<Button>("Toolbar/Controls/Back");
		_forward = GetNode<Button>("Toolbar/Controls/Forward");
		_error = GetNode<Label>("BrowserError");
		_status = GetNode<Label>("Status");
		_back.Pressed += () => Request("browser_back", new Dictionary());
		_forward.Pressed += () => Request("browser_forward", new Dictionary());
		GetNode<Button>("Toolbar/Controls/Home").Pressed += () => Request("browser_navigate", new Dictionary { ["page"] = "home" });
		_address.TextSubmitted += text =>
		{
			_address.ReleaseFocus();
			Request("browser_address", new Dictionary { ["address"] = text });
		};
	}

	/// <summary>首次打开才创建插件渲染表面；再次打开窗口继续使用当前网页和运行进度。</summary>
	public void Open()
	{
		if (_surface != null) return;
		if (_snapshot.Count == 0) throw new InvalidOperationException("打开浏览器前必须先注入 C++ 快照。");
		if (!ClassDB.CanInstantiate("CefTexture"))
			GDExtensionManager.LoadExtension(ProjectSettings.GlobalizePath(ExtensionPath));
		if (!ClassDB.CanInstantiate("CefTexture"))
		{
			ShowFailure("CEF 网页扩展无法加载，请检查 addons/godot_cef/bin 中的本机运行库。");
			return;
		}
		_surface = ClassDB.Instantiate("CefTexture").AsGodotObject() as Control
			?? throw new InvalidOperationException("CEF 扩展未能创建渲染控件。");
		_resource = _snapshot["browser_resource"].AsString();
		_surface.Set("enable_accelerated_osr", false);
		_surface.Set("url", _resource);
		_surface.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
		_surface.Connect("ipc_message", Callable.From<string>(OnPageMessage));
		_surface.Connect("load_error", Callable.From<string, int, string>(OnLoadError));
		_host.AddChild(_surface);
	}

	/// <summary>更新地址栏和导航按钮；网页切换期间只保存最新快照，等待目标文档报到后发送。</summary>
	public void ApplySnapshot(Dictionary snapshot)
	{
		_snapshot = snapshot;
		if (!IsNodeReady() || snapshot.Count == 0) return;
		if (!_address.HasFocus()) _address.Text = snapshot["browser_url"].AsString();
		_back.Disabled = !snapshot["can_go_back"].AsBool();
		_forward.Disabled = !snapshot["can_go_forward"].AsBool();
		if (_surface == null) return;
		string resource = snapshot["browser_resource"].AsString();
		if (_resource != resource)
		{
			_resource = resource;
			_documentReady = false;
			_surface.Set("url", resource);
			return;
		}
		SendSnapshot();
	}

	/// <summary>失败作为操作结果显示，保持当前网页；不把未知地址静默替换为其他页面。</summary>
	public void ShowActionResult(Dictionary result)
	{
		_status.Visible = !result["ok"].AsBool();
		_status.Text = _status.Visible ? result["message"].AsString() : "";
	}

	/// <summary>仅接受当前独立网页的消息，避免已离开文档的延迟消息覆盖新站点。</summary>
	private void OnPageMessage(string raw)
	{
		Variant parsed = Json.ParseString(raw);
		if (parsed.VariantType != Variant.Type.Dictionary) return;
		Dictionary message = parsed.AsGodotDictionary();
		if (!message.ContainsKey("resource") || message["resource"].AsString() != _resource) return;
		string type = message["type"].AsString();
		if (type == "browser:ready")
		{
			_documentReady = true;
			_error.Hide();
			// CEF 的 ready 回调在消息处理中触发，推迟到 Godot 下一轮安全地回传状态。
			Callable.From(SendSnapshot).CallDeferred();
		}
		else if (type == "action") Request(message["action"].AsString(), message["payload"].AsGodotDictionary());
		else if (type == "browser:state_applied") PageStateApplied?.Invoke(message);
		else if (type == "browser:error") ShowFailure("网页显示失败：" + message["message"].AsString());
	}

	/// <summary>用统一协议发送权威快照，网页按修订号丢弃过期状态。</summary>
	private void SendSnapshot()
	{
		if (_surface == null || !_documentReady) return;
		_surface.Call("send_ipc_message", Json.Stringify(new Dictionary
		{
			["version"] = 1, ["type"] = "state", ["page"] = "prologue",
			["revision"] = _snapshot["revision"], ["snapshot"] = _snapshot,
		}));
	}

	/// <summary>网页意图和原生导航共用同一提交入口。</summary>
	private void Request(string action, Dictionary payload)
	{
		_status.Hide();
		ActionRequested?.Invoke(action, payload);
	}

	/// <summary>保留扩展提供的错误码，资源缺失时明确报错，不加载替代站点。</summary>
	private void OnLoadError(string url, int code, string description) => ShowFailure($"网页加载失败（{code}）：{description}\n{url}");

	/// <summary>错误面板也来自场景资源，脚本只填入诊断文字。</summary>
	private void ShowFailure(string message)
	{
		_error.Text = message;
		_error.Show();
		GD.PushError(message);
	}
}
