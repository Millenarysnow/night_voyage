# Night Voyage 原生 UI 效果组件

同一个 GDExtension addon 提供毛玻璃、圆角表面、纯色/线性/径向渐变、外/内阴影、光晕、透明轮廓投影、CRT 位移与显示器信号效果、监控画面调色，以及圆角/Alpha 裁切和对应的精确命中。运行时代码使用 **C++17 + CanvasItem Shader**，没有新增 GDScript 或 C#，保留原有 `ExampleClass`。

当前绑定 Godot **4.8** API。本机验证版本：`4.8.beta.custom_build.679066724`，Windows x86_64 / GL Compatibility / RTX 4060 Ti。其他小版本需要匹配 godot-cpp API 后重新编译，不能直接声称兼容 4.6。

## 先看效果

打开 `examples/display_pipeline_demo.tscn`，运行当前场景（F6），或直接运行项目（F5）。它是一个独立的显示效果演示地图，场景资源中的 `pipeline_mode=true` 会自动创建 CRT 屏幕、监控画面、圆角裁切和 Alpha 裁切，不需要命令行参数。原来的 `examples/effects_demo.tscn` 仍保留毛玻璃、阴影、光晕与 Alpha 投影基础演示。

`--nv-pipeline-test` 只用于自动验收：它会在 GL Compatibility 实际渲染后端检查 GPU 像素，并向变形屏幕、圆角区域和 Alpha 环注入鼠标点击，验证命中范围与显示形状一致。正常查看效果不需要这个参数。

演示由原生 `NVEffectsDemo` 在运行时搭建，所以编辑器里的演示根场景是空的；**实际组件自身支持编辑器预览**。在编辑器中摆放时，使用下列原生节点，或 `components/` 下的四个可实例化场景。

| 最小组件 | 用途 | 组合方式 |
| --- | --- | --- |
| `NVSurface : Control` | 纯色/线性/径向渐变填充、圆角、边框、外阴影、内阴影、光晕 | 用样式关闭不需要的层，普通 UI 可直接作为子项 |
| `NVGlassPanel : NVSurface` | 背景模糊、染色、饱和度、亮度 | 包含同样的表面效果，子项的文字/图标保持清晰 |
| `NVAlphaShadow : Control` | 依据 `Texture2D` 的 Alpha 生成投影 | 可同时显示原图，也可只显示投影；彩色阴影可作为轮廓光晕 |
| `NVSurfaceStyle : Resource` | 多节点共享的样式 | 保存为 `.tres`，与节点及业务逻辑分离 |
| `NVScreenEffectViewport : NVEffectViewport` | 对整组 UI 做显示器变形、扫描线、噪点、暗角、反光、滚动亮带和闪烁 | 将游戏/桌面 UI 放在它的 `Content` 下；鼠标、触摸坐标会按画面映射到内容 |
| `NVFootageEffect : Control` | 单张纹理或 ViewportTexture 的调色和信号效果 | 接收监控画面的 Texture2D；不会处理其 Control 子项 |
| `NVClipViewport : NVEffectViewport` | 对一组 UI 做圆角或 Alpha 遮罩裁切 | 用同一个 `NVHitShape` 驱动显示裁切与鼠标命中 |
| `NVWarpProfile / NVAnalogProfile : Resource` | 共享位移场和模拟显示参数 | 分别配置径向/贴图位移，与调色、扫描线、噪点等参数；可存为 `.tres` |
| `NVHitShape : Resource` | 圆角矩形解析命中或 Alpha 遮罩命中 | 可在多个裁切容器之间共享形状；默认容差为 0 |

用 UE 的经验理解：`Control` 对应自定义 UI 控件，`.tscn` 对应可嵌套的 Widget 组合，`.tres` 对应共享的样式资产。底层效果合并到一次表面绘制中，无需给每个边框、阴影分别增加一个渲染节点。

## 接入自己的 UI

1. 在创建节点窗口搜索 `NVGlassPanel` / `NVSurface` / `NVAlphaShadow` / `NVScreenEffectViewport` / `NVFootageEffect` / `NVClipViewport`，或实例化 `components/*.tscn`。
2. 表面的 `Surface Style` 指向一个 `NVSurfaceStyle`。可以复用 `presets/` 下的 `.tres`，也可新建样式。未指定样式时，进入场景后会创建独立默认资源。
3. 在表面下面放 `MarginContainer`、`VBoxContainer`、`Label`、`Button` 等普通节点，配置锚点与边距。表面本身不是 `Container`，不会自动安排子节点，也不从内容推导最小尺寸；在外层 Container 中使用时设置 `custom_minimum_size` 和尺寸标志。
4. 共享样式会同步更新所有使用者。只改一个实例时，先在 Inspector 中把资源设为独立副本。
5. Alpha 投影的 `Texture` 需要带透明通道，并设置 Control 尺寸。图片会拉伸到整个 Control；保留比例可在外面放 `AspectRatioContainer`。`Show Source=false` 时只显示投影，便于自己叠放原图。
6. 完整画面需要 CRT 效果时，把主界面控件放到 `NVScreenEffectViewport/Content`，用 `Warp Profile` 与 `Analog Profile` 控制变形和显示质感。监控视频单独放入自己的 SubViewport，再把其 `ViewportTexture` 交给 `NVFootageEffect`。
7. 对子 UI 做裁切时，把按钮、文字等放到 `NVClipViewport/Content`，将 `Hit Shape` 资源设为圆角矩形或 Alpha 遮罩。Alpha 遮罩建议使用静态带 Alpha 的 `Texture2D`；内容纹理与形状资源分别决定画面和点击区域。

8. 需要渐变填充时，在 `NVSurfaceStyle` 中将 `Fill Mode` 设为 `Linear Gradient` 或 `Radial Gradient`，把 `Fill Gradient` 指向一个 Godot `Gradient` 资源。`Gradient` 可包含任意数量的颜色停止点，并可设置线性插值、常量插值或三次插值。渐变坐标均为表面内容区域的归一化坐标：线性模式使用 `gradient_start` 到 `gradient_end`，径向模式使用 `gradient_center` 和椭圆半径 `gradient_radius`。启用渐变后，停止点颜色（包括 Alpha）决定填充颜色和不透明度，`fill_color` 只在 `Solid` 模式下使用。

默认鼠标策略：表面为 `PASS`，Alpha 投影为 `IGNORE`，子节点继续使用原生输入、焦点与导航。阴影扩展的是绘制范围，**不会扩大布局或鼠标命中范围**。父容器若开启 `clip_contents`，外扩阴影/光晕也会被裁掉。圆角只作用于自身表面，**不会把整棵子 UI 按圆角裁剪**。

`NVHitShape` 的圆角命中使用与 `NVSurface` 同序的四角半径和圆角距离场，直接检查光标落点，不再用统一内缩的矩形近似。`hit_slop` 可用于圆角边缘的局部容差；默认 0 时，落在圆角曲线之外的点不会命中，也不会扩大 Control 的布局矩形。Alpha 形状则用 `alpha_threshold` 判断遮罩不透明度。CPU 命中缓存只在资源变化或调用 `refresh_alpha_cache()` 时重建，不会在鼠标移动中读取 GPU。

`NVWarpProfile` 的径向弯曲和 RG 位移贴图以“显示坐标到内容采样坐标”定义映射。CRT 节点把同一映射交给 Shader 和鼠标/触摸输入反算。位移贴图按最长边最多 512 像素生成 CPU/GPU 共用的采样图；Alpha 遮罩最多 1024 像素。这样限制命中缓存并让两端采样一致。贴图更新时自动重建；动态 ViewportTexture 通常没有可读的 CPU 图像，不适合作为需要精确输入反算的位移场或 Alpha 遮罩。

`NVAnalogProfile` 可同时供屏幕和监控画面使用：亮度/对比度/饱和度、扫描线、颗粒、暗角、镜面反光、滚动亮带和轻微闪烁都合成在一次 CanvasItem 绘制中。动态层直接使用 Shader `TIME`；关闭 `motion_enabled` 后噪点固定且闪烁/亮带停止。`NVFootageEffect` 的采样 UV 覆盖整块控件，素材会拉伸填满；需要保持长宽比时，在外部用 `AspectRatioContainer`。

`NVSurfaceStyle` 的渐变填充通过一张共享的 256×1 `GradientTexture1D` 采样，渐变坐标和颜色停止点在 CPU 侧只在资源变化时上传，绘制时与 SDF 圆角、边框、阴影和光晕合成在同一个片元 Shader 中。多个节点共享同一个样式资源时只保留一份渐变纹理；只改渐变方向、中心或半径不会重建纹理。

## 参数与单位

`NVSurfaceStyle`：

- `fill_mode`：`Solid`、`Linear Gradient` 或 `Radial Gradient`。渐变模式需要设置 `fill_gradient`。
- `fill_gradient`：Godot `Gradient` 资源，可包含多个颜色停止点；停止点 Alpha 控制填充不透明度。
- `gradient_start / gradient_end`：线性渐变的归一化起点和终点，允许超出 0–1 以制作延伸渐变。
- `gradient_center / gradient_radius`：径向渐变的归一化中心和 X/Y 半径；半径决定椭圆范围。
- `corner_radii`：左上、右上、右下、左下的半径，每个限制到短边的一半。
- `fill_color`：普通表面的 RGBA 填充。**在玻璃中，Alpha 表示染色强度**；整个玻璃淡出请改节点 `modulate.a`。
- `border_color / border_width`：向内描边，不改变布局。
- `shadow_color / offset / softness / spread`：外阴影；`spread` 正值扩张、负值收缩。
- `glow_color / softness / spread`：外光晕，零偏移。
- `inner_shadow_color / offset / softness`：仅在表面内部绘制的阴影。

以上长度为 Control 局部像素，跟随控件缩放。SDF 的 `softness` 是近似高斯标准差，采用距离场软覆盖近似，不是对整张圆角图做精确卷积，也不是 CSS `box-shadow` 的像素级复刻。颜色 Alpha 为零可关闭对应层；外阴影/光晕扣除形状内部，避免透明填充被自己的外阴影染黑。

`NVGlassPanel`：

- `blur_radius`：屏幕像素中的近似模糊尺度，0–64，使用引擎高斯 mip 链；与 CSS `backdrop-filter: blur()` 没有一比一映射。0 时读取清晰的第 0 级。
- `saturation / brightness`：背景采样的饱和度与亮度，0–2。
- `high_quality`：默认关闭，采样一次 mip；开启后使用五个错位采样，成本更高。
- `local_capture`：默认开启，在自身绘制前复制含模糊边距的背景，支持玻璃叠放。**屏幕复制和 mip 生成仍然是每帧 GPU 成本。**

多个玻璃需要共用同一背景、无需互相映入时：在它们之前放一个原生 `BackBufferCopy`，设为 `Viewport`，关闭这些面板的 `local_capture`。复制之后、这些玻璃之间避免夹入会重新复制屏幕的节点。这个模式不能同时表达前后玻璃叠映。

本机 4.8 beta 的局部复制路径直接使用 `rect`；组件通过抵消变换的捕获节点统一到屏幕像素坐标，避免局部黑块。已验证移动、旋转、缩放、窗口拉伸；**Camera2D / 动态 CanvasLayer 变换尚未完整验证**，复杂画布优先使用 Viewport 共享复制模式。`CanvasGroup` 会占用同一屏幕缓冲，不要把玻璃放在其内部。

`NVAlphaShadow`：

- `blur_radius`：局部像素的模糊标准差，0–96。
- `shadow_color / shadow_offset`：最终投影颜色和偏移，修改不会重做缓存。
- `cache_scale`：缓存相对逻辑尺寸的分辨率上限，默认 0.5，可选 0.125–1。大半径还会进一步降采样，保证固定卷积核不出现稀疏采样条带；因此实际比例可能低于该值。
- `max_cache_size`：缓存最长边上限，默认 1024，64–2048，包含四周的 3σ 留白。
- `invalidate_cache()`：显式通知动态纹理重建。普通纹理的 `changed` 信号会自动触发；动态 ViewportTexture 内容更新通常不发该信号。
- `cache_ready`：横、纵两步 GPU 缓存完成后发出。正常构建需要两个实际渲染帧，刚加载或修改期间可能短暂显示旧缓存。
- `get_cache_build_count() / is_cache_pending() / get_cache_size_pixels()`：诊断构建次数、等待状态和实际缓存尺寸。

本轮 Alpha 阴影针对 **Texture2D 透明轮廓**，没有提供捕获整棵 UI 子树的功能。固定 25 点卷积与降采样是性能优先的近似，极细纹理和大半径仍需按实际素材检查。适合图标、装饰与静态插画；每帧改变图片或半径会失去缓存收益。

## 性能设计

- 圆角、填充、边框、内外阴影、光晕由一个扩展四边形的 SDF Shader 合成，无贴图卷积，无额外离屏 Viewport；关闭效果跳过对应分支。
- 渐变填充只增加一次 256×1 颜色纹理采样，不创建离屏 Viewport；纯色模式不采样渐变纹理。颜色停止点由 Godot `Gradient` 资源管理，适合策划在 Inspector 中复用和调整。
- 玻璃复用 Godot GPU 屏幕 mip，默认一次采样。实际成本取决于捕获面积、面板数量、重叠和屏幕分辨率，**使用 Shader 本身不代表一定更快**。
- Alpha 投影用两个低分辨率 `SubViewport` 分别横向/纵向模糊，每步每像素 25 次 Alpha 读取。仅尺寸、纹理、半径、缓存设置变化时重建；同帧多次修改合并。完成后两个 Viewport 停止更新并断开帧回调，最终显示层读取一次缓存，可选再读取一次原图。
- CRT 与裁切组件各将子 UI 合成到一个透明 `SubViewport`，再用单次 Shader 绘制做显示变形或遮罩；不可见时由 `UPDATE_WHEN_PARENT_VISIBLE` 暂停离屏重绘。它们适合需要整组裁切/处理的内容，不应为了静态单图增加 Viewport。
- CRT 输入反算通过预缓存位移场的 CPU 双线性采样完成，不在输入事件期间读回 GPU。CPU 缓存上限同时约束了 GPU 使用的采样图，确保点击与显示使用同一张图。
- 监控画面效果单次采样底图并一次合成所有模拟层，不创建额外中间纹理。若启用动态颗粒、亮带或闪烁，着色器会持续变化；固定监控可关闭 `motion_enabled`。
- 运行时组件不执行 `_process` 轮询，不读回 GPU，不在 CPU 逐像素生成投影。演示的测试图生成和截图读回仅用于演示/验证。
- 两个 RGBA8 缓存的基础纹理开销约 `宽 × 高 × 4 × 2` 字节，不含后端附加分配；两张 256² 约 0.5 MiB，两张 1024² 约 8 MiB。
- 大半径会增大透明绘制留白和过度绘制。批量装饰优先用小半径、共享背景、较低缓存比例，并隐藏不可见节点。

## 构建与复用

这是自动加载的 GDExtension addon，无需 GDScript `EditorPlugin` 或独立插件开关。开发源码在 `native/night_voyageExt/`，运行时内容在 `addons/night_voyageExt/`。克隆后先初始化 godot-cpp：`git submodule update --init --recursive`。

在项目根目录执行，需配置 C++17 编译器、CMake、Ninja 和 Python：

```powershell
# Debug：关闭无关的 IDE 插件下载。
cmake -S . -B cmake-build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DGODOTCPP_TARGET=template_debug -DINSTALL_GODOT_ADDONS=OFF
cmake --build cmake-build-debug --target night_voyageExt --parallel 8

# Release 导出：使用独立构建目录。
cmake -S . -B cmake-build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DGODOTCPP_TARGET=template_release -DINSTALL_GODOT_ADDONS=OFF
cmake --build cmake-build-release --target night_voyageExt --parallel 8
```

产物自动复制到 `addons/night_voyageExt/bin/<平台>/`。当前 `reloadable=false`，更新 DLL 前关闭正在使用该扩展的编辑器/游戏进程。

搬到其他项目时，复制**整个 `addons/night_voyageExt/`，包括对应平台和构建类型的二进制**，保持目录名不变；Shader 使用固定 `res://addons/night_voyageExt/` 路径。继续开发 C++ 时还需要 `native/` 和构建配置。本仓库忽略编译产物，单独复制 Git 源文件后需先编译。`.gdextension` 的其他平台条目是路径约定，不代表已经生成或验证相应平台的库。

导出需匹配平台的 Release 二进制。将 `addons/night_voyageExt/shaders/*` 加入资源导出包含规则，确保 `.gdshader` 与 `.gdshaderinc` 都进入包；这些资源由 C++ 按路径加载，不能依赖场景引用自动发现。使用演示时也包含 `presets/*.tres`。`native/` 和构建目录有 `.gdignore`，不会作为游戏资源导入。

## 验证

在实际 GPU 后端运行，headless 的 dummy 渲染不能代替画面检查：

```powershell
godot --path . --rendering-method gl_compatibility res://addons/night_voyageExt/examples/effects_demo.tscn -- --nv-self-test
godot --path . --rendering-method gl_compatibility res://addons/night_voyageExt/examples/display_pipeline_demo.tscn -- --nv-capture
godot --path . --rendering-method gl_compatibility res://addons/night_voyageExt/examples/display_pipeline_demo.tscn -- --nv-pipeline-test
```

`--nv-self-test` 验证毛玻璃、表面、Alpha 投影与缓存生命周期；`--nv-pipeline-test` 验证 CRT/监控/裁切像素和变形、圆角、Alpha 命中。成功退出码为 0，截图在 Git 忽略的 `artifacts/`。未承诺跨设备帧率，未验证移动/Web 导出。
