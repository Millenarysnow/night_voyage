#nullable enable

using Godot;
using Godot.Collections;

/// <summary>
/// 通过 Godot Mono 运行时验证 NV Timeline 的 C# Callable 互操作。
/// 测试使用动态 GodotObject 调用 GDExtension API，避免假设自定义 C++ 类
/// 已经生成静态 C# 包装类型。
/// </summary>
public partial class NVTimelineCallableInteropTest : Node
{
	private const string ExtensionPath = "res://addons/nv_timeline/nv_timeline.gdextension";
	private const string SampleTimelinePath = "res://addons/nv_timeline/examples/basic.timeline.json";
	private const int SampleTimelineId = 2;

	private Node? _service;
	private bool _setupFinished;
	private bool _failed;
	private bool _submitAccepted;
	private bool _controlRequestAccepted;
	private int _framesWaited;
	private int _startedCallbackCount;
	private int _followupCallbackCount;
	private readonly System.Collections.Generic.List<string> _callbackOrder = new();

	/// <summary>
	/// 在场景启动时加载原生扩展、创建 Service、注册 C# Callable，并启动示例时间轴。
	/// 所有准备步骤都在主线程顺序完成，失败时记录原因并交由 _Process 统一退出。
	/// </summary>
	public override void _Ready()
	{
		// Godot 可能已经从项目文件扫描中自动注册扩展类；先检查类是否存在，
		// 只有确实没有注册时才显式加载，避免同一进程重复注册。
		string absoluteExtensionPath = ProjectSettings.GlobalizePath(ExtensionPath);
		if (!ClassDB.CanInstantiate("NVTimelineService"))
		{
			GDExtensionManager.LoadStatus loadStatus = GDExtensionManager.LoadExtension(absoluteExtensionPath);
			if (loadStatus != GDExtensionManager.LoadStatus.Ok &&
					loadStatus != GDExtensionManager.LoadStatus.AlreadyLoaded)
			{
				Fail($"无法加载 NV Timeline GDExtension，状态为 {loadStatus}");
				_setupFinished = true;
				return;
			}
		}
		GD.Print("[PASS] Godot Mono 已加载 NV Timeline GDExtension。");

		// 自定义 GDExtension 类没有项目内 C# 静态包装时，仍可经由 ClassDB 创建并使用。
		GodotObject? serviceObject = ClassDB.Instantiate("NVTimelineService").AsGodotObject();
		if (serviceObject is not Node serviceNode)
		{
			serviceObject?.Dispose();
			Fail("ClassDB 无法创建 NVTimelineService Node。");
			_setupFinished = true;
			return;
		}
		_service = serviceNode;
		AddChild(serviceNode);

		GodotObject? eventManager = serviceNode.Call("get_event_manager").AsGodotObject();
		if (eventManager is null)
		{
			Fail("NVTimelineService 未返回事件管理器对象。");
			_setupFinished = true;
			return;
		}

		// Callable.From 将托管委托封装成 Godot Callable；C++ 派发时应把 Dictionary
		// 上下文原样传回对应的 C# 方法。
		int startedBindingId = eventManager.Call(
				"register_binding",
				"demo_started",
				Callable.From<Dictionary>(OnTimelineStarted),
				false).AsInt32();
		int followupBindingId = eventManager.Call(
				"register_binding",
				"csharp_followup",
				Callable.From<Dictionary>(OnFollowupLabel),
				false).AsInt32();
		if (startedBindingId <= 0 || followupBindingId <= 0)
		{
			Fail("事件管理器没有接受 C# Callable 绑定。");
			_setupFinished = true;
			return;
		}
		GD.Print("[PASS] C# Callable.From<Dictionary> 绑定成功。");

		if (!serviceNode.Call("load_json_file", SampleTimelinePath).AsBool())
		{
			Fail("NV Timeline 无法加载基础示例 JSON。");
			_setupFinished = true;
			return;
		}

		// 通过公开的统一请求入口启动流程；启动输出会进入队列并调用上面注册的 C# 委托。
		var startRequest = new Dictionary
		{
			["type"] = "start_timeline",
			["timeline_id"] = SampleTimelineId,
		};
		_submitAccepted = serviceNode.Call("submit_request", startRequest).AsBool();
		if (!_submitAccepted)
		{
			Fail("NVTimelineService 拒绝了 C# 提交的启动请求。");
		}
		_setupFinished = true;
	}

	/// <summary>
	/// 等待主线程派发完成后检查回调次数与顺序；有限帧超时可避免错误场景挂住 CI。
	/// </summary>
	public override void _Process(double delta)
	{
		if (!_setupFinished)
		{
			return;
		}

		_framesWaited++;
		if (_startedCallbackCount > 0 && _followupCallbackCount > 0)
		{
			VerifyResults();
			GetTree().Quit(_failed ? 1 : 0);
			return;
		}

		if (_framesWaited >= 120)
		{
			Fail("等待 C# Callable 回调超时。");
			GetTree().Quit(1);
		}
	}

	/// <summary>
	/// 收到示例输出后检查上下文，并在回调内提交新标签。
	/// 这个请求应先入队，待当前回调返回后再由 Service 继续派发。
	/// </summary>
	private void OnTimelineStarted(Dictionary context)
	{
		_startedCallbackCount++;
		_callbackOrder.Add("started-enter");

		Expect(context.ContainsKey("label") && context["label"].AsString() == "demo_started",
				"输出回调收到正确的标签名。");
		Expect(context.ContainsKey("source_timeline_id") &&
				context["source_timeline_id"].AsInt32() == SampleTimelineId,
				"输出回调上下文包含来源时间轴 ID。");
		Expect(context.ContainsKey("time_units") && context["time_units"].AsInt32() == 0,
				"启动标记的回调时间为零。");

		var followupRequest = new Dictionary
		{
			["type"] = "publish_label",
			["label"] = "csharp_followup",
		};
		bool accepted = _service!.Call("submit_request", followupRequest).AsBool();
		Expect(accepted, "C# Callable 内可以提交后续标签请求。");
		_callbackOrder.Add("started-exit");
	}

	/// <summary>
	/// 接收由前一个 C# 回调排入队列的后续标签，再提交暂停控制请求，验证项目回调
	/// 可以通过统一入口安全地改变时间轴状态。
	/// </summary>
	private void OnFollowupLabel(Dictionary context)
	{
		_followupCallbackCount++;
		_callbackOrder.Add("followup-enter");
		Expect(context.ContainsKey("label") && context["label"].AsString() == "csharp_followup",
				"第二个 C# 回调收到后续标签。");
		Expect(context.ContainsKey("accepted_timeline_ids"), "标签回调上下文包含时间轴接收列表。");

		var pauseRequest = new Dictionary
		{
			["type"] = "pause_timeline",
			["timeline_id"] = SampleTimelineId,
		};
		_controlRequestAccepted = _service!.Call("submit_request", pauseRequest).AsBool();
		Expect(_controlRequestAccepted, "C# Callable 内可以提交时间轴暂停控制请求。");
		_callbackOrder.Add("followup-exit");
	}

	/// <summary>
	/// 汇总同步和异步两部分断言；只要其中一项失败，headless 进程就以非零状态退出。
	/// </summary>
	private void VerifyResults()
	{
		Expect(_submitAccepted, "C# 提交的时间轴启动请求被接受。");
		Expect(_controlRequestAccepted, "C# 回调提交的暂停控制请求被接受。");
		Expect(_startedCallbackCount == 1, "启动输出的 C# Callable 恰好触发一次。");
		Expect(_followupCallbackCount == 1, "回调内追加的标签 C# Callable 恰好触发一次。");
		Expect(_callbackOrder.Count == 4 &&
				_callbackOrder[0] == "started-enter" &&
				_callbackOrder[1] == "started-exit" &&
				_callbackOrder[2] == "followup-enter" &&
				_callbackOrder[3] == "followup-exit",
				"回调内请求在当前 Callable 返回后按队列顺序派发。");

		GodotObject? timeline = _service!.Call("get_timeline", SampleTimelineId).AsGodotObject();
		Expect(timeline is not null && timeline.Call("get_state_name").AsString() == "paused",
				"队列已执行 C# 回调提交的暂停请求。");

		if (!_failed)
		{
			GD.Print("[PASS] NV Timeline C# Callable 与请求队列互操作验收通过。");
		}
	}

	/// <summary>
	/// 输出单项断言并统一记录失败状态，避免某一项失败后漏掉剩余诊断。
	/// </summary>
	private void Expect(bool condition, string message)
	{
		if (condition)
		{
			GD.Print("[PASS] ", message);
			return;
		}

		Fail(message);
	}

	/// <summary>
	/// 以统一前缀报告失败，最终由场景以非零退出码结束。
	/// </summary>
	private void Fail(string message)
	{
		_failed = true;
		GD.PushError("[FAIL] " + message);
	}
}
