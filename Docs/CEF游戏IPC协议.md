# CEF 游戏 IPC 协议

这份协议规定 CEF 网页、Godot 投影层和 C++ 序章状态机之间的最小通信格式。网页只负责呈现搜索结果、页面内容和玩家选区；序章进度、线索卡、任务判定和对白由 Godot 调用的 C++ 状态机保存。

## 消息方向

网页发送给 Godot 使用 `window.sendIpcMessage(JSON.stringify(message))`。Godot 通过 `CefTexture.send_ipc_message()` 回传 JSON 字符串。

两边都使用 UTF-8 JSON。未知字段应忽略；缺少必填字段或类型不对时，状态机返回失败结果，不改变进度修订号。

## 网页到 Godot：操作消息

网页操作统一使用以下外层结构：

```json
{
  "version": 1,
  "type": "action",
  "request_id": "web-1730000000000",
  "page": "prologue",
  "action": "search",
  "payload": {
    "scope": "web",
    "query": "年糕"
  }
}
```

- `version`：协议版本，当前为 `1`。
- `type`：固定为 `action`。
- `request_id`：网页为每次操作生成的字符串。当前 Godot 投影层不依赖它匹配响应，但保留该字段供后续异步请求追踪。
- `page`：发起操作的页面标识，序章网页固定为 `prologue`。
- `action`：语义操作名。网页不能通过它直接写入阶段或进度。
- `payload`：操作参数；所有剧情判断由 C++ 状态机完成。

当前序章网页会提交这些操作：

| 操作 | 作用 | 关键参数 |
| --- | --- | --- |
| `begin` | 开始序章聊天 | 无 |
| `search` | 提交搜索 | `scope: web/forum`、`query` |
| `open_article` | 打开寻猫启事 | `id` |
| `open_forum` | 打开论坛目击帖 | `id` |
| `open_result` | 打开泛搜中的普通网页 | `id` |
| `browser_navigate` | 返回搜索结果或首页 | `page: search_results/forum_results/home` |
| `preview_image` | 请求 Godot 显示图片灯箱 | `page: article/forum_post` |
| `collect_clue` | 将玩家选中的文字或图片收进白板 | `page`、`kind: text/image`、文字型还需 `text` |

Godot 原生白板还会调用状态机的 `attach_clue`、`detach_clue`、`submit_location`、`finish_recovery` 和 `snapshot`；这些不是 CEF 页面直接发送的消息，但仍属于同一套序章动作协议。

## Godot 到网页：状态快照

Godot 当前向网页发送完整状态快照，而不是逐项拼接页面字段：

```json
{
  "version": 1,
  "type": "state",
  "page": "prologue",
  "revision": 7,
  "snapshot": {
    "stage": "article",
    "page": "article",
    "search_results": [],
    "clues": [],
    "milestones": {
      "article_opened": true,
      "article_text_collected": false,
      "article_image_collected": false
    },
    "complete": false
  }
}
```

网页收到 `type: state` 后只根据 `snapshot.page` 重画当前页面。`snapshot.content` 是由项目内容 JSON 注入的只读页面投影，网页不得改写后回传为进度。

`revision` 在 C++ 状态发生变化时单调递增。失败操作、幂等的重复收录和 `snapshot` 请求不会推进修订号。接收方应保存已应用的修订号，丢弃更旧的快照，避免 CEF 异步消息乱序覆盖新画面。

## 网页到 Godot：状态应用回执

网页完成 DOM 重绘并等待本地图片解码后发送回执，供集成测试和诊断确认素材确实显示：

```json
{
  "type": "browser:state_applied",
  "page": "article",
  "clue_count": 1,
  "article_text_collected": true,
  "article_image_collected": false,
  "article_image_width": 380,
  "forum_image_width": 0
}
```

该回执不是剧情操作，不能改变游戏状态；生产界面可以忽略它。`article_image_width` 和 `forum_image_width` 为当前 DOM 图片的自然宽度，数值为零表示图片尚未解码或当前页面没有该图片。

## 规则边界

序章要求玩家先搜索并打开启事，主动收下启事文字和照片后才能在论坛搜索目击帖。白板任务由 Godot 原生拖放，C++ 根据线索文字关键词裁决子任务；完成“明确猫的样子”和“明确猫在哪儿”后，玩家还要在聊天中报告具体位置。只说“老粮站”或“南山路口”会追问范围，只有“花坛”等具体位置才会开始找回流程。

重复收集同一页面、同一类型、同一文字的线索是幂等操作，不会生成重复卡片。准确位置确认后进入 `returning` 状态；`finish_recovery` 完成交接并把已挂接的线索卡拆回白板自由区，主任务进入 `complete`。

## 输入与坐标

CEF 页面内的 DOM 点击由 CEF 自己处理，语义操作才通过 IPC 发送给 Godot。当前选区浮动按钮由网页在选区附近显示，按钮提交 `collect_clue`；若以后改为 Godot 原生浮层，网页可额外携带 CSS 坐标：

```json
{
  "text": "被选中的文字",
  "source": "寻猫启事",
  "rect": { "x": 120, "y": 240, "width": 180, "height": 24 }
}
```

`rect` 是 CEF 页面 CSS 像素坐标，Godot 负责按 CefTexture 显示尺寸、窗口缩放和 CRT 映射转换；网页不直接写入白板。
