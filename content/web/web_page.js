"use strict";

// 三个独立文档共用呈现与 IPC 适配器。这里不判定搜索命中、帖子可见性或剧情进度。
const resource = document.body.dataset.resource;
const site = document.body.dataset.site;
let snapshot = null;
let appliedRevision = -1;
let requestNumber = 0;
let selectedText = "";
let selectedPage = "";
let selectedPostId = "";
let forumSearchTimer = 0;
const byId = (id) => document.getElementById(id);
const selectionPop = byId("selectionPop");

/** 每个消息携带来源文档，Godot 据此拒绝已经离开页面的延迟动作。 */
function emit(message) {
  window.sendIpcMessage(JSON.stringify({ version: 1, resource, ...message }));
}

/** 网页只提交玩家意图，权威规则与历史始终由 C++ 执行。 */
function send(action, payload = {}) {
  emit({ type: "action", page: "prologue", request_id: `web-${++requestNumber}`, action, payload });
}

/** 数据中的文字先转义，再处理原型使用的粗体标记，避免正文被误解释为 HTML。 */
function escapeHtml(value) {
  return String(value).replace(/[&<>"']/g, (ch) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[ch]));
}
function formatText(value) {
  return escapeHtml(value).replace(/\*\*([^*]+)\*\*/g, "<strong>$1</strong>");
}

/** 输入过程中收到其他状态更新时，保留玩家尚未提交的输入内容与光标。 */
function projectInput(input, value) {
  if (document.activeElement !== input) input.value = value;
}

/** 全网结果根据 C++ 提供的类型进入相应网站，论坛入口与帖子入口严格分开。 */
function renderSearch() {
  const home = snapshot.page === "home";
  const general = snapshot.page === "web_result";
  byId("searchIntro").hidden = !home;
  byId("webSearch").hidden = general;
  byId("searchResults").hidden = home || general;
  byId("generalPage").hidden = !general;
  projectInput(byId("webQuery"), home ? "" : snapshot.search_query);
  if (general) {
    const result = snapshot.search_results.find((item) => item.id === snapshot.active_result_id);
    if (!result) throw new Error("普通网页未找到对应的搜索结果。");
    byId("generalTitle").textContent = result.title;
    byId("generalUrl").textContent = result.url;
    byId("generalBody").textContent = result.description;
    return;
  }
  if (home) return;
  byId("resultCount").textContent = `找到 ${snapshot.search_results.length} 条与「${snapshot.search_query}」相关的结果`;
  const actions = { article: "open_article", forum_site: "open_forum", forum_post: "open_thread", web: "open_result" };
  byId("resultList").innerHTML = snapshot.search_results.length
    ? snapshot.search_results.map((result) => {
      if (!actions[result.kind]) throw new Error(`不支持的搜索结果类型：${result.kind}`);
      return `<div class="result"><div class="result-url">${escapeHtml(result.url)}</div><button class="result-link" data-action="${actions[result.kind]}" data-id="${escapeHtml(result.id)}">${escapeHtml(result.title)}</button><p>${formatText(result.description)}</p></div>`;
    }).join("")
    : '<div class="empty">没有找到相关内容。换个词试试？</div>';
}

/** 论坛首页和站内筛选共用一份列表；查询与分类的命中结果直接使用 C++ 快照。 */
function renderForum() {
  const forum = snapshot.content.forum;
  const thread = snapshot.page === "forum_post";
  byId("forumName").textContent = forum.title;
  byId("forumSlogan").textContent = forum.slogan;
  document.querySelector(".forum-logo").textContent = forum.site.slice(0, 1);
  projectInput(byId("forumQuery"), snapshot.forum_query);
  byId("forumIndex").hidden = thread;
  byId("threadPage").hidden = !thread;
  byId("forumCategories").innerHTML = forum.categories.map((category) => `<button class="f-tab${category === snapshot.forum_category ? " active" : ""}" data-category="${escapeHtml(category)}">${escapeHtml(category)}</button>`).join("");
  if (thread) {
    renderThread(snapshot.active_post);
    return;
  }
  byId("postList").innerHTML = snapshot.forum_results.length
    ? snapshot.forum_results.map((post) => `<button class="post-row" data-action="open_thread" data-id="${escapeHtml(post.id)}"><span class="avatar">${escapeHtml(post.author.slice(0, 1))}</span><span class="p-main"><span class="p-title">${post.pinned ? '<span class="pin">置顶</span>' : ""}<span class="cat">${escapeHtml(post.category)}</span><span>${escapeHtml(post.title)}</span></span><span class="p-meta">${escapeHtml(post.author)} · ${escapeHtml(post.time)}</span></span><span class="p-stat">回复 ${post.replies}<br>浏览 ${post.views}</span></button>`).join("")
    : `<div class="empty">没有找到相关帖子${snapshot.forum_query ? `：${escapeHtml(snapshot.forum_query)}` : ""}</div>`;
}

/** 任何帖子都使用同一正文模板；图片、楼层和收尾内容均由数据投影提供。 */
function renderThread(post) {
  if (!post.id) throw new Error("论坛正文缺少有效帖子。");
  byId("threadTitle").textContent = post.title;
  byId("threadMeta").textContent = `${post.category} · ${post.author} · ${post.time}`;
  byId("threadAvatar").textContent = post.author.slice(0, 1);
  byId("threadAuthor").textContent = `${post.author} · 楼主`;
  byId("threadBody").innerHTML = formatText(post.body);
  byId("threadPhotoBlock").hidden = !post.image;
  if (post.image) byId("threadPhoto").src = post.image;
  else byId("threadPhoto").removeAttribute("src");
  byId("threadCaption").textContent = post.image_caption;
  byId("threadFloors").innerHTML = post.floors.map((floor, index) => `<section class="floor"><div class="avatar">${escapeHtml(floor.author.slice(0, 1))}</div><div class="floor-content"><p class="floor-author"><span>${escapeHtml(floor.author)}</span><small>${index + 1} 楼 · ${escapeHtml(floor.time)}</small></p><div class="floor-text" data-markable="forum_post">${formatText(floor.text)}</div></div></section>`).join("");
}

/** 启事站点只投影当前告示资源，照片的收录仍需先打开 Godot 原生灯箱。 */
function renderArticle() {
  const article = snapshot.content.article;
  byId("articleTitle").textContent = article.title;
  byId("articleSource").textContent = article.short_source;
  byId("articleBody").innerHTML = formatText(article.body);
  byId("articlePhoto").src = article.image;
  byId("articleCaption").textContent = article.image_caption;
}

/** 图片完成解码后回执；异步期间出现新快照时，旧绘制不能冒充新页面完成。 */
async function acknowledge(appliedSnapshot) {
  await Promise.all(Array.from(document.querySelectorAll("img.photo[src]")).map((photo) => photo.decode()));
  if (snapshot !== appliedSnapshot) return;
  emit({
    type: "browser:state_applied", revision: snapshot.revision, page: snapshot.page,
    url: snapshot.browser_url, title: document.title,
    clue_count: snapshot.clues.length,
    article_text_collected: snapshot.milestones.article_text_collected,
    article_image_collected: snapshot.milestones.article_image_collected,
    article_image_width: byId("articlePhoto")?.naturalWidth ?? 0,
    forum_image_width: byId("threadPhoto")?.naturalWidth ?? 0,
    empty_message: document.querySelector("#app .empty")?.textContent ?? "",
    post_count: snapshot.forum_results.length,
    floor_count: snapshot.active_post.floors?.length ?? 0,
    has_global_search: Boolean(byId("webSearch")),
    has_forum_search: Boolean(byId("forumSearch")),
    forum_query: snapshot.forum_query, forum_category: snapshot.forum_category,
  });
}

/** 显示真实错误并发回 Godot，不能吞掉异常后继续呈现错误的剧情页面。 */
function reportError(error) {
  const message = error instanceof Error ? error.message : String(error);
  const warning = document.createElement("p");
  warning.className = "empty";
  warning.textContent = `网页显示失败：${message}`;
  byId("app").replaceChildren(warning);
  emit({ type: "browser:error", message });
}

window.onIpcMessage = (raw) => {
  try {
    const message = typeof raw === "string" ? JSON.parse(raw) : raw;
    if (message.type !== "state") return;
    const incoming = message.snapshot;
    if (incoming.browser_resource !== resource || incoming.revision < appliedRevision) return;
    if (!Number.isFinite(incoming.revision)) throw new Error("状态快照缺少有效修订号。");
    // 同一修订号只回执，不重画，避免重开窗口或重复同步清除玩家正在划选的内容。
    if (snapshot && incoming.revision === appliedRevision) {
      acknowledge(snapshot).catch(reportError);
      return;
    }
    appliedRevision = incoming.revision;
    // 原生后退或前进改变当前正文时，撤销上一个页面尚未发出的即时筛选。
    if (snapshot && (incoming.page !== snapshot.page || incoming.active_post_id !== snapshot.active_post_id)) clearTimeout(forumSearchTimer);
    snapshot = incoming;
    if (selectionPop) selectionPop.hidden = true;
    document.title = snapshot.browser_title;
    if (site === "search") renderSearch();
    else if (site === "forum") renderForum();
    else if (site === "article") renderArticle();
    else throw new Error(`没有对应的站点模板：${site}`);
    acknowledge(snapshot).catch(reportError);
  } catch (error) { reportError(error); }
};

// 表单只表达当前页面内的搜索；全网搜索永远没有 forum 范围切换选项。
byId("webSearch")?.addEventListener("submit", (event) => {
  event.preventDefault();
  byId("webQuery").blur();
  const query = byId("webQuery").value.trim();
  if (query) send("search", { scope: "web", query });
  else send("browser_navigate", { page: "home" });
});
function searchForum() {
  clearTimeout(forumSearchTimer);
  send("forum_search", { query: byId("forumQuery").value, category: snapshot.forum_category });
}
byId("forumSearch")?.addEventListener("submit", (event) => { event.preventDefault(); searchForum(); });
// 原型的站内输入即时筛选；合并连续按键，避免每个键都触发 CEF 往返。
byId("forumQuery")?.addEventListener("input", () => {
  clearTimeout(forumSearchTimer);
  forumSearchTimer = setTimeout(searchForum, 180);
});
document.addEventListener("click", (event) => {
  const target = event.target.closest("button, img[data-preview]");
  if (!target || !snapshot) return;
  if (target.dataset.action) {
    clearTimeout(forumSearchTimer);
    send(target.dataset.action, { id: target.dataset.id });
  }
  else if (target.hasAttribute("data-forum-home")) {
    clearTimeout(forumSearchTimer);
    byId("forumQuery").blur();
    send("open_forum", { id: snapshot.content.forum.id });
  } else if (target.dataset.category) {
    clearTimeout(forumSearchTimer);
    send("forum_search", { query: byId("forumQuery").value, category: target.dataset.category });
  } else if (target.dataset.page) send("browser_navigate", { page: target.dataset.page });
  else if (target.dataset.preview) send("preview_image", { page: target.dataset.preview });
});

// 只接受同一可摘录区域的真实选区；正文和每一层回复都可由玩家亲手摘录。
document.addEventListener("mouseup", (event) => {
  if (!selectionPop || selectionPop.contains(event.target)) return;
  const selection = window.getSelection();
  const anchor = selection?.anchorNode;
  const element = anchor instanceof Element ? anchor : anchor?.parentElement;
  const source = element?.closest("[data-markable]");
  const text = selection?.toString().trim() ?? "";
  if (!source || !selection.focusNode || !source.contains(selection.focusNode) || !text || text.length > 600) {
    selectionPop.hidden = true;
    selectedText = "";
    return;
  }
  selectedText = text;
  selectedPage = source.dataset.markable;
  selectedPostId = snapshot.active_post_id;
  const rect = selection.getRangeAt(0).getBoundingClientRect();
  selectionPop.hidden = false;
  selectionPop.style.left = Math.max(8, Math.min(rect.left, window.innerWidth - selectionPop.offsetWidth - 8)) + "px";
  selectionPop.style.top = Math.min(rect.bottom + 8, window.innerHeight - selectionPop.offsetHeight - 8) + "px";
});
byId("collectSelection")?.addEventListener("mousedown", (event) => event.preventDefault());
byId("collectSelection")?.addEventListener("click", () => {
  if (!selectedText || selectedPage !== snapshot.page || selectedPostId !== snapshot.active_post_id) return;
  send("collect_clue", { page: selectedPage, kind: "text", text: selectedText });
  selectionPop.hidden = true;
  selectedText = "";
  window.getSelection().removeAllRanges();
});

// 所有监听器就绪后才报到，Godot 可以安全地向这个独立文档发送最新状态。
emit({ type: "browser:ready" });
