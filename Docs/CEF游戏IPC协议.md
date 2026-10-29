# CEF 游戏 IPC 协议

这份协议规定网页区域和 Godot 游戏逻辑之间的最小通信格式。网页只负责显示和收集操作，Godot 保存并校验序章进度。

## 消息方向

网页发送给 Godot 使用 `window.sendIpcMessage(JSON.stringify(message))`。Godot 通过 `CefTexture.send_ipc_message()` 回传 JSON 字符串。

两边都使用 UTF-8 JSON。未知字段必须忽略，缺少必填字段或类型不对时返回失败结果，不得改变游戏状态。

## 网页到 Godot：操作消息

```json
{
  "version": 1,
  "type": "action",
  "request_id": "r-0001",
  "page": "prologue",
  "action": "search",
  "payload": {
    "query": "年糕"
  }
}
```

字段含义：

- `version`：协议版本，当前为 `1`。
- `type`：固定为 `action`。
- `request_id`：网页为每次操作生成的非空字符串。Godot 原样带回，便于网页匹配结果。
- `page`：发起操作的页面标识。当前序章网页使用 `prologue`。
- `action`：操作名，例如 `begin`、`search`、`open_article`、`collect_clue`、`open_forum`、`submit_location`。
- `payload`：操作参数。所有剧情判断由 Godot 完成，网页不能通过 payload 指定阶段或直接写入进度。

## Godot 到网页：结果消息

```json
{
  "version": 1,
  "type": "result",
  "request_id": "r-0001",
  "page": "prologue",
  "ok": true,
  "code": "search_hit",
  "message": "找到了相关页面。",
  "revision": 3,
  "snapshot": {
    "stage": "search",
    "clues": [],
    "complete": false
  }
}
```

`ok=false` 时，`code` 和 `message` 描述可以显示给玩家的结果；失败请求不得推进 `revision`，也不得改变 `snapshot`。

`revision` 单调递增。网页只应用不小于当前修订号的快照，避免 CEF 异步消息返回顺序变化导致旧画面覆盖新状态。

## 序章操作

| 操作 | 作用 | 关键参数 |
| --- | --- | --- |
| `begin` | 开始序章聊天 | 无 |
| `search` | 提交网页搜索词 | `query` |
| `open_article` | 打开寻宠启事 | `id` |
| `collect_clue` | 玩家主动把线索收进白板 | `kind: text/image` |
| `open_forum` | 打开目击信息 | `id` |
| `submit_location` | 向林越报告位置 | `location` |
| `snapshot` | 请求当前状态，不改变剧情 | 无 |

序章状态机要求玩家先打开启事，再分别主动收下文字和图片，之后才能查看目击信息，最后提交准确位置。重复收集同类线索是幂等操作，不会重复生成卡片。

## 输入与坐标

CEF 页面内的 DOM 点击由 CEF 自己处理，语义操作才通过 IPC 发送到 Godot。Godot 原生浮层显示时，应由 Godot 先命中浮层并阻止同一次输入继续传给 CEF。

需要把网页选区显示为 Godot 原生浮层时，网页消息额外携带 `selection`：

```json
{
  "text": "被选中的文字",
  "source": "寻宠启事",
  "rect": { "x": 120, "y": 240, "width": 180, "height": 24 }
}
```

`rect` 是 CEF 页面 CSS 像素坐标，Godot 负责按 CefTexture 的显示尺寸、窗口缩放和 CRT 映射转换，网页不直接写入白板。

