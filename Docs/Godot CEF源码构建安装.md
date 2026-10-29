# Godot CEF 源码构建与本地安装

## 当前安装

- Godot CEF 上游仓库作为 Git 子模块固定在 `native/godot_cef`，版本为 v2.1.1。
- 项目内 Godot 插件位于 `addons/godot_cef`，提供 `CefTexture`、`CefTexture2D` 和 IPC 检查器等 API。
- 当前机器构建目标为 Windows x86_64 Release。CEF Rust crate 版本为 `154.5.0`，对应 CEF 运行时版本 `154.0.34`。
- 当前项目使用 Godot `4.8.beta.mono.custom_build`。本地编辑器路径为 `F:\Godot\godot\bin\godot.windows.editor.x86_64.mono.exe`，插件最低兼容版本为 Godot 4.6。
- 验收场景使用 GL Compatibility，因此 CEF 走软件渲染。切换到受支持的 Vulkan 渲染后，可再单独验证加速 OSR。

CEF/Chromium 运行库体积较大，`addons/godot_cef/bin/` 已加入 `.gitignore`。扩展描述、图标、检查器资源和源码子模块进入版本控制；每个平台的 DLL、CEF runtime 和 helper 需要在目标机器重新构建，不能只依赖 Git clone 后的文件。

## Windows x86_64 源码构建

构建脚本位于 `native/godot_cef/scripts/build`。它使用 Rust nightly、Node、pnpm、Visual Studio C++ 工具链以及与 Cargo.lock 匹配的 CEF runtime。仓库的 `mise.toml` 还声明了 Godot 4.6；本项目已有自编译 Godot 4.8 Mono，因此不要运行会自动安装所有工具的 `mise install`。关闭 mise 自动安装后，直接运行构建脚本即可避免安装第二个 Godot。

以下 PowerShell 示例假定 Rust nightly、Node 26 或更新版本、pnpm 和 Visual Studio C++ 构建工具已安装并且在 `PATH` 中。`CEF_PATH` 应指向 CEF runtime 解压后的目录；若目录尚未准备好，需先按 Godot CEF 上游 README 的说明下载与锁定版本匹配的 CEF runtime。

```powershell
$env:MISE_AUTO_INSTALL = "0"
$env:CEF_PATH = (Resolve-Path "artifacts\godot_cef\cef_windows_x86_64\154.0.34\cef_windows_x86_64").Path
$env:PATH = "$env:CEF_PATH;$env:PATH"

Push-Location native\godot_cef
try {
    pnpm --dir scripts/build install --frozen-lockfile --prod
    node scripts/build/main.ts bundle --release --target host --target-dir=target
} finally {
    Pop-Location
}
```

构建完成后，源代码仓库会在 `native/godot_cef/addons/godot_cef/bin/x86_64-pc-windows-msvc` 生成扩展 DLL、CEF runtime 文件和 helper。将这些运行文件复制到主项目插件目录：

```powershell
$source = "native\godot_cef\addons\godot_cef\bin\x86_64-pc-windows-msvc"
$destination = "addons\godot_cef\bin\x86_64-pc-windows-msvc"
New-Item -ItemType Directory -Force $destination | Out-Null
Get-ChildItem -LiteralPath $source -Force | Copy-Item -Destination $destination -Recurse -Force
```

构建不同平台时，应在该平台对应环境编译，并使用上游文档列出的目标三元组。当前项目只提交 Windows x86_64 的插件配置与资源，不包含可直接发布到其他平台的本机二进制。

## 本地验收

仓库本机的临时验收场景位于 `artifacts/godot_cef/visual_smoke`。它加载项目内 `res://cef_smoke.html`，等待页面发出 JavaScript IPC，并检查 CEF 纹理像素后保存截图。运行命令如下：

```powershell
$env:CEF_SMOKE_SCREENSHOT = "artifacts\godot_cef\visual_smoke\cef_smoke_result.png"
& "F:\Godot\godot\bin\godot.windows.editor.x86_64.mono.exe" `
    --path "artifacts\godot_cef\visual_smoke" `
    --rendering-method gl_compatibility
```

验收通过时，控制台会输出 `CEF_SMOKE_RESULT:PASS 960x640`，截图应显示离线网页的实际文字和样式；同时应看到 `HTTP=200` 和本地网页 JavaScript 消息。若仅有加载信号而纹理未绘制，验收脚本会失败。

该临时场景位于被忽略的 `artifacts/` 目录，只用于本机验证，不属于正式游戏流程。Godot 编辑器扫描项目时，`native/.gdignore` 会阻止它再次扫描子模块里的另一份 addon，避免扩展类重复注册。

## 已知边界

- 本次验证覆盖 Windows x86_64、Godot 4.8 Mono 和 GL Compatibility 软件渲染；尚未验证其他操作系统、导出模板或 Vulkan 加速 OSR。
- CEF runtime 使用 Chromium 内核，发布游戏时应连同 Godot CEF 的许可要求一并检查和分发其许可文件。
- `res://file.html` 被 CEF 规范化成带尾斜线的 URL 时，协议处理器会先尝试目录的 `index.html`，再尝试原文件名；有效但不存在的资源仍返回 HTTP 404，非法路径仍返回 HTTP 403。
