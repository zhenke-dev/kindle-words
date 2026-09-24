#pragma once
/* 本文件由 tools/embed_html.py 自动生成，请勿手改 */
static const char INDEX_HTML[] PROGMEM = R"KWHTML(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, user-scalable=no">
<title>Kindle Words</title>
<style>
  * {
    margin: 0; padding: 0; box-sizing: border-box;
    -webkit-tap-highlight-color: transparent;  /* 禁点击高亮，防电子屏闪烁 */
  }
  html { font-size: 20px; }
  body {
    font-family: Georgia, "Songti SC", serif;
    background: #fff; color: #000;
    -webkit-text-size-adjust: 100%;
    touch-action: manipulation;
    user-select: none; -webkit-user-select: none;
  }
  button { -webkit-appearance: none; appearance: none; border-radius: 0; }
  button:active { opacity: 1; }               /* 按压不变色，避免多余重绘 */
  .view { display: none; min-height: 100vh; padding: 1rem 1.4rem 1rem; }
  .view.on { display: block; }

  /* 顶栏 */
  .bar {
    display: flex; justify-content: space-between; align-items: baseline;
    border-bottom: 2px solid #000; padding-bottom: .5rem;
    font-size: .95rem;
  }
  .bar .title { font-weight: bold; letter-spacing: .1em; }
  .bar button { background: none; border: none; font-size: .95rem; text-decoration: underline; padding: .4rem .3rem; }

  /* 首页 */
  .hero { text-align: center; margin-top: 4.5rem; }
  .hero h1 { font-size: 2rem; letter-spacing: .15em; margin-bottom: 3rem; }
  .stat { font-size: 1.05rem; line-height: 2.1; }
  .stat b { font-size: 1.5rem; }
  .big-btn {
    display: block; width: 100%; margin-top: 3.5rem; padding: 1.1rem 0;
    background: #000; color: #fff; border: none; font-size: 1.3rem;
    font-family: inherit; letter-spacing: .2em;
  }
  .big-btn.gray { background: #fff; color: #000; border: 2px solid #000; }

  /* 学习页：单词在上，释义居中自适应，操作区固定贴底 */
  .card-area {
    display: flex; flex-direction: column;
    height: calc(100vh - 4.6rem);
    text-align: center;
  }
  .word { font-size: 3rem; font-weight: bold; margin: 2.6rem 0 1.4rem; letter-spacing: .03em; }
  .divider { width: 4rem; border-top: 1px solid #000; margin: 0 auto; }
  .meaning {
    flex: 1; margin-top: 1.6rem; padding: 0 .5rem;
    font-size: 1.25rem; line-height: 2; text-align: center;
  }
  .meaning.hidden { visibility: hidden; }
  .bottom { padding-bottom: .4rem; }
  .reveal-btn {
    width: 100%; padding: 1.15rem 0; font-size: 1.15rem;
    background: #fff; color: #000; border: 2px solid #000; font-family: inherit;
  }
  .rate-grid { display: none; }
  .rate-grid.on { display: block; }
  .rate-row { display: flex; margin-bottom: .8rem; }
  .rate-row button + button { margin-left: .8rem; }  /* 不用 gap：Chromium<84 不支持 */
  .rate-row button {
    flex: 1; padding: .95rem 0 .75rem; font-size: 1.15rem; font-family: inherit;
    background: #fff; color: #000; border: 2px solid #000;
  }
  .rate-row button .ivl { display: block; font-size: .8rem; margin-top: .3rem; color: #333; }

  /* 设置页 */
  .sect { margin-top: 1.8rem; }
  .sect h2 { font-size: 1.1rem; border-bottom: 1px solid #000; padding-bottom: .4rem; margin-bottom: .9rem; }
  .row { display: flex; align-items: center; margin-bottom: .9rem; font-size: 1rem; flex-wrap: wrap; }
  .row label { flex: 1; }
  .row input {
    font-family: inherit; font-size: 1rem; padding: .4rem;
    border: 1px solid #000; background: #fff; color: #000; border-radius: 0;
  }
  .row input { width: 5.5rem; text-align: center; }
  .order-btn {
    width: 5.5rem; padding: .4rem 0; font-size: 1rem; font-family: inherit;
    background: #fff; color: #000; border: 1px solid #000; text-align: center;
  }
  .save-btn {
    display: block; width: 100%; padding: .7rem 0;
    background: #000; color: #fff; border: none;
    font-size: 1rem; font-family: inherit; text-align: center;
  }
  .msg { font-size: .9rem; }
  .row .msg { width: 100%; margin: .4rem 0 0 .1rem; }
</style>
</head>
<body>

<!-- 首页 -->
<div class="view on" id="v-home">
  <div class="bar"><span class="title">KINDLE WORDS</span><button id="b-to-set">设置</button></div>
  <div class="hero">
    <div class="stat">
      词书进度 <b id="s-learned">–</b> / <span id="s-total">–</span><br>
      今日新学 <b id="s-newdone">–</b> / <span id="s-newquota">–</span><br>
      待复习 <b id="s-due">–</b>
    </div>
    <button class="big-btn" id="b-start">开始学习</button>
  </div>
</div>

<!-- 学习页 -->
<div class="view" id="v-study">
  <div class="bar">
    <span>新 <b id="c-new">0</b> · 复习 <b id="c-due">0</b></span>
    <button id="b-quit">结束</button>
  </div>
  <div class="card-area">
    <div class="word" id="c-word"></div>
    <div class="divider"></div>
    <div class="meaning hidden" id="c-meaning"></div>
    <div class="bottom">
      <button class="reveal-btn" id="b-reveal">显示释义</button>
      <div class="rate-grid" id="c-rates">
        <div class="rate-row">
          <button data-r="1">重来<span class="ivl" id="ivl0"></span></button>
          <button data-r="2">困难<span class="ivl" id="ivl1"></span></button>
        </div>
        <div class="rate-row">
          <button data-r="3">良好<span class="ivl" id="ivl2"></span></button>
          <button data-r="4">简单<span class="ivl" id="ivl3"></span></button>
        </div>
      </div>
    </div>
  </div>
</div>

<!-- 完成页 -->
<div class="view" id="v-done">
  <div class="bar"><span class="title">KINDLE WORDS</span><button id="b-done-home">返回</button></div>
  <div class="hero">
    <h1>今日任务完成</h1>
    <div class="stat">
      今日新学 <b id="d-new">–</b> 词<br>
      今日复习 <b id="d-old">–</b> 词
    </div>
    <button class="big-btn gray" id="b-done-back">返回首页</button>
  </div>
</div>

<!-- 设置页 -->
<div class="view" id="v-set">
  <div class="bar">
    <span class="title">设置</span>
    <span><button id="b-save-all">保存</button><button id="b-set-home">返回</button></span>
  </div>

  <div class="sect">
    <h2>学习计划</h2>
    <div class="row"><label>每日新词数量</label><input id="in-npd" type="number" min="1" max="200"></div>
    <div class="row"><label>期望保留率 (0.7–0.99)</label><input id="in-dr" type="number" step="0.01" min="0.7" max="0.99"></div>
    <div class="row"><label>新词顺序</label><button class="order-btn" id="b-order">顺序</button></div>
  </div>

  <div class="sect">
    <h2>FSRS 参数</h2>
    <div class="row"><label>当前参数版本</label><span>v<b id="s-pver">–</b></span></div>
    <div class="row"><button class="save-btn" id="b-reset-w">恢复默认 FSRS 参数</button><span class="msg" id="m-rw"></span></div>
  </div>

  <div class="sect">
    <h2>重置</h2>
    <div class="row"><button class="save-btn" id="b-reset-data">清除全部学习数据</button><span class="msg" id="m-rd"></span></div>
  </div>
</div>

<script>
/* ES2015 保守语法：无箭头函数/模板串/可选链，适配 KPW6 无 JIT 浏览器 */
var DAY = Math.floor(Date.now() / 86400000);
var cur = null;      // 当前卡片
var busy = false;    // 仅作逻辑防抖，不改变按钮外观（避免电子屏多余重绘）
var order = 0;       // 新词顺序：0 顺序 / 1 随机

function $(id) { return document.getElementById(id); }

function show(name) {
  var views = document.querySelectorAll('.view');
  for (var i = 0; i < views.length; i++) views[i].className = 'view';
  $('v-' + name).className = 'view on';
  window.scrollTo(0, 0);
}

function api(path, body, cb) {
  var opt = { cache: 'no-store' };
  if (body !== null) {
    opt.method = 'POST';
    opt.headers = { 'Content-Type': 'application/json' };
    opt.body = JSON.stringify(body);
  }
  fetch(path, opt).then(function (r) { return r.json(); }).then(cb)
    .catch(function () { busy = false; });
}

function fmtIvl(d) {
  if (d >= 365) return (d / 365).toFixed(1) + ' 年';
  if (d >= 30) return Math.round(d / 30) + ' 个月';
  return d + ' 天';
}

/* ---------------- 首页 ---------------- */
function loadHome() {
  api('/api/stats', null, function (s) {
    $('s-learned').textContent = s.learned;
    $('s-total').textContent = s.total;
    $('s-newdone').textContent = s.new_done;
    $('s-newquota').textContent = s.new_per_day;
    $('s-due').textContent = s.due;
  });
}

/* ---------------- 学习 ---------------- */
function renderCard(c) {
  busy = false;
  if (c.done) {
    api('/api/stats', null, function (s) {
      $('d-new').textContent = s.new_done;
      $('d-old').textContent = s.old_today;
      show('done');
    });
    return;
  }
  cur = c;
  show('study');
  $('c-new').textContent = c['new'];
  $('c-due').textContent = c.due;
  $('c-word').textContent = c.word;
  $('c-meaning').textContent = c.meaning;
  $('c-meaning').className = 'meaning hidden';
  $('b-reveal').style.display = 'block';
  $('c-rates').className = 'rate-grid';
  for (var i = 0; i < 4; i++) $('ivl' + i).textContent = fmtIvl(c.ivl[i]);
  window.scrollTo(0, 0);
}

function nextCard() {
  api('/api/next?day=' + DAY, null, renderCard);
}

function rate(r) {
  if (busy || !cur) return;
  busy = true;
  api('/api/rate', { id: cur.id, rating: r, day: DAY, ts: Date.now() }, renderCard);
}

/* ---------------- 设置 ---------------- */
function loadSettings() {
  api('/api/stats', null, function (s) {
    $('in-npd').value = s.new_per_day;
    $('in-dr').value = s.dr;
    $('s-pver').textContent = s.params_version;
    order = s.order ? 1 : 0;
    $('b-order').textContent = order ? '随机' : '顺序';
  });
}

/* ---------------- 事件 ---------------- */
$('b-start').onclick = function () { nextCard(); };
$('b-quit').onclick = function () { loadHome(); show('home'); };
$('b-to-set').onclick = function () { loadSettings(); show('set'); };
$('b-set-home').onclick = function () { loadHome(); show('home'); };
$('b-done-home').onclick = function () { loadHome(); show('home'); };
$('b-done-back').onclick = function () { loadHome(); show('home'); };
$('b-reveal').onclick = function () {
  $('c-meaning').className = 'meaning';
  $('b-reveal').style.display = 'none';
  $('c-rates').className = 'rate-grid on';
};

$('b-order').onclick = function () {
  order = order ? 0 : 1;
  this.textContent = order ? '随机' : '顺序';
};

$('b-save-all').onclick = function () {
  var btn = this;
  api('/api/settings', {
    new_per_day: parseInt($('in-npd').value, 10),
    dr: parseFloat($('in-dr').value),
    order: order
  }, function () {
    btn.textContent = '已保存';
    setTimeout(function () { btn.textContent = '保存'; }, 2000);
  });
};

(function () {
  var btns = document.querySelectorAll('.rate-row button');
  for (var i = 0; i < btns.length; i++) {
    btns[i].onclick = function () { rate(parseInt(this.getAttribute('data-r'), 10)); };
  }
})();

/* Kindle 浏览器抑制 confirm() 弹窗，改用"再次点击确认"两段式 */
function arm(btn, label, fn) {
  btn.onclick = function () {
    if (btn.getAttribute('data-armed') === '1') {
      btn.setAttribute('data-armed', '0');
      btn.textContent = label;
      fn();
    } else {
      btn.setAttribute('data-armed', '1');
      btn.textContent = '再次点击确认';
      setTimeout(function () {
        btn.setAttribute('data-armed', '0');
        btn.textContent = label;
      }, 3000);
    }
  };
}

arm($('b-reset-w'), '恢复默认 FSRS 参数', function () {
  api('/api/reset_params', {}, function () {
    $('m-rw').textContent = '已恢复默认';
    loadSettings();
  });
});

arm($('b-reset-data'), '清除全部学习数据', function () {
  api('/api/reset_data', {}, function () {
    $('m-rd').textContent = '已清除（学习进度与复习记录已重置）';
    loadSettings();
  });
});

loadHome();
</script>
</body>
</html>
)KWHTML";
static const char ADMIN_HTML[] PROGMEM = R"KWHTML(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Kindle Words 管理</title>
<style>
  * { margin: 0; padding: 0; box-sizing: border-box; }
  body {
    font-family: Georgia, "Songti SC", "Microsoft YaHei", serif;
    background: #fafafa; color: #111; font-size: 16px;
  }
  .wrap { max-width: 720px; margin: 0 auto; padding: 24px 20px 60px; }
  header {
    display: flex; justify-content: space-between; align-items: baseline;
    border-bottom: 2px solid #111; padding-bottom: 10px; margin-bottom: 8px;
  }
  header h1 { font-size: 20px; letter-spacing: .1em; }
  header .ver { font-size: 14px; color: #555; }
  .sect { margin-top: 28px; background: #fff; border: 1px solid #ddd; padding: 18px 20px; }
  .sect h2 { font-size: 16px; margin-bottom: 14px; border-bottom: 1px solid #eee; padding-bottom: 8px; }
  .stat { font-size: 15px; line-height: 2; }
  .stat b { font-size: 20px; }
  textarea {
    width: 100%; height: 130px; font-family: ui-monospace, Consolas, monospace;
    font-size: 13px; padding: 10px; border: 1px solid #ccc; border-radius: 4px;
    resize: vertical;
  }
  .btn {
    display: inline-block; padding: 9px 22px; margin-top: 12px;
    background: #111; color: #fff; border: none; border-radius: 4px;
    font-size: 14px; cursor: pointer; text-decoration: none;
  }
  .btn.gray { background: #fff; color: #111; border: 1px solid #999; }
  .btn + .btn { margin-left: 10px; }
  .msg { font-size: 13px; color: #2a7d2a; margin-left: 10px; }
  .msg.err { color: #b00; }
  .hint { font-size: 13px; color: #666; line-height: 1.7; margin-top: 8px; }
  label.inline { font-size: 14px; margin-right: 8px; }
  input.num {
    width: 80px; padding: 5px 8px; font-size: 14px;
    border: 1px solid #ccc; border-radius: 4px; text-align: center;
  }
</style>
</head>
<body>
<div class="wrap">
  <header>
    <h1>KINDLE WORDS 管理</h1>
    <span class="ver">参数版本 v<b id="s-pver">–</b></span>
  </header>

  <div class="sect">
    <h2>学习数据</h2>
    <div class="stat">
      词书进度 <b id="s-learned">–</b> / <span id="s-total">–</span>
      ・ 今日新学 <b id="s-newdone">–</b> 词
      ・ 今日复习 <b id="s-old">–</b> 词<br>
      累计复习 <b id="s-reviews">–</b> 次
      ・ 期望保留率 <span id="s-dr">–</span>
    </div>
    <a class="btn" href="/api/export.csv" download="reviews.csv">下载复习记录 (CSV)</a>
    <div class="hint">CSV 列：ts, day, word_id, word, rating, elapsed_days, s_before, d_before, s_after, d_after, interval</div>
  </div>

  <div class="sect">
    <h2>FSRS 参数更新</h2>
    <textarea id="ta-w" spellcheck="false"></textarea>
    <div class="hint">21 个权重，逗号或空格分隔。在 PC 上完成参数优化后粘贴于此并更新。</div>
    <button class="btn" id="b-save-w">更新参数</button>
    <button class="btn gray" id="b-reset-w">恢复默认参数</button>
    <span class="msg" id="m-w"></span>
  </div>

  <div class="sect">
    <h2>学习计划</h2>
    <label class="inline">每日新词</label><input class="num" id="in-npd" type="number" min="1" max="200">
    <label class="inline" style="margin-left:16px">期望保留率</label><input class="num" id="in-dr" type="number" step="0.01" min="0.7" max="0.99">
    <label class="inline" style="margin-left:16px">新词顺序</label>
    <select id="sel-order" style="padding:5px 8px;font-size:14px">
      <option value="0">按词书顺序</option>
      <option value="1">随机</option>
    </select>
    <br>
    <button class="btn" id="b-save-set">保存计划</button>
    <span class="msg" id="m-set"></span>
  </div>
</div>

<script>
function $(id) { return document.getElementById(id); }

function api(path, body, cb, err) {
  var opt = { cache: 'no-store' };
  if (body !== null) {
    opt.method = 'POST';
    opt.headers = { 'Content-Type': 'application/json' };
    opt.body = JSON.stringify(body);
  }
  fetch(path, opt).then(function (r) { return r.json(); }).then(cb)
    .catch(function () { if (err) err(); });
}

function loadAll() {
  api('/api/stats', null, function (s) {
    $('s-learned').textContent = s.learned;
    $('s-total').textContent = s.total;
    $('s-newdone').textContent = s.new_done;
    $('s-old').textContent = s.old_today;
    $('s-reviews').textContent = s.total_reviews;
    $('s-dr').textContent = s.dr;
    $('s-pver').textContent = s.params_version;
    $('in-npd').value = s.new_per_day;
    $('in-dr').value = s.dr;
    $('sel-order').value = String(s.order ? 1 : 0);
  });
  api('/api/params', null, function (p) {
    $('ta-w').value = p.w.join(', ');
  });
}

$('b-save-w').onclick = function () {
  var m = $('m-w');
  var parts = $('ta-w').value.split(/[,\s]+/);
  var w = [];
  for (var i = 0; i < parts.length; i++) {
    var v = parseFloat(parts[i]);
    if (!isNaN(v)) w.push(v);
  }
  if (w.length !== 21) {
    m.textContent = '需要 21 个数值（当前 ' + w.length + ' 个）';
    m.className = 'msg err';
    return;
  }
  api('/api/params', { w: w }, function () {
    m.textContent = '已更新';
    m.className = 'msg';
    loadAll();
  });
};

$('b-reset-w').onclick = function () {
  if (!confirm('恢复为默认 FSRS 参数？')) return;
  api('/api/reset_params', {}, function () {
    $('m-w').textContent = '已恢复默认';
    $('m-w').className = 'msg';
    loadAll();
  });
};

$('b-save-set').onclick = function () {
  api('/api/settings', {
    new_per_day: parseInt($('in-npd').value, 10),
    dr: parseFloat($('in-dr').value),
    order: parseInt($('sel-order').value, 10)
  }, function () { $('m-set').textContent = '已保存'; });
};

loadAll();
</script>
</body>
</html>
)KWHTML";
