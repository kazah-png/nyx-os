/* NyxOS docs — interactions: starfield, scroll-spy, reveal, terminal typing */
(function () {
  "use strict";
  var reduce = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  /* ---------- starfield ---------- */
  (function starfield() {
    var c = document.getElementById("starfield");
    if (!c) return;
    var ctx = c.getContext("2d");
    var stars = [], w, h, dpr = Math.min(window.devicePixelRatio || 1, 2);
    function resize() {
      var cw = document.documentElement.clientWidth, ch = document.documentElement.clientHeight;
      w = c.width = cw * dpr; h = c.height = ch * dpr;   // buffer; CSS inset:0 sizes the element
      var n = Math.min(160, Math.floor((cw * ch) / 12000));
      stars = [];
      for (var i = 0; i < n; i++) {
        stars.push({
          x: Math.random() * w, y: Math.random() * h,
          r: (Math.random() * 1.3 + 0.2) * dpr,
          a: Math.random() * 0.6 + 0.15,
          tw: Math.random() * 0.02 + 0.004,
          hue: Math.random() < 0.15 ? "124,92,255" : (Math.random() < 0.2 ? "0,255,157" : "230,232,240")
        });
      }
    }
    var t = 0;
    function draw() {
      ctx.clearRect(0, 0, w, h);
      for (var i = 0; i < stars.length; i++) {
        var s = stars[i];
        var a = s.a + Math.sin(t * s.tw * 60 + i) * 0.12;
        ctx.beginPath();
        ctx.arc(s.x, s.y, s.r, 0, 6.283);
        ctx.fillStyle = "rgba(" + s.hue + "," + Math.max(0, a).toFixed(2) + ")";
        ctx.fill();
      }
      t += 1;
      if (!reduce) requestAnimationFrame(draw);
    }
    resize();
    addEventListener("resize", resize, { passive: true });
    if (reduce) draw(); else requestAnimationFrame(draw);
  })();

  /* ---------- nav: scrolled state + mobile toggle ---------- */
  var nav = document.getElementById("nav");
  var onScroll = function () {
    nav.classList.toggle("scrolled", window.scrollY > 12);
    if (window.scrollY < 260) {
      [].slice.call(document.querySelectorAll(".nav-links a.active")).forEach(function (x) { x.classList.remove("active"); });
    }
  };
  onScroll();
  addEventListener("scroll", onScroll, { passive: true });

  var toggle = document.getElementById("navToggle");
  var links = document.getElementById("navLinks");
  if (toggle) {
    toggle.addEventListener("click", function () { links.classList.toggle("open"); });
    links.addEventListener("click", function (e) {
      if (e.target.tagName === "A") links.classList.remove("open");
    });
  }

  /* ---------- scroll-spy ---------- */
  var sections = [].slice.call(document.querySelectorAll("main section[id]"));
  var navMap = {};
  [].slice.call(document.querySelectorAll(".nav-links a")).forEach(function (a) {
    navMap[a.getAttribute("href").slice(1)] = a;
  });
  if ("IntersectionObserver" in window) {
    var spy = new IntersectionObserver(function (entries) {
      entries.forEach(function (en) {
        var a = navMap[en.target.id];
        if (!a) return;
        if (en.isIntersecting) {
          [].slice.call(document.querySelectorAll(".nav-links a")).forEach(function (x) { x.classList.remove("active"); });
          a.classList.add("active");
        }
      });
    }, { rootMargin: "-45% 0px -50% 0px" });
    sections.forEach(function (s) { spy.observe(s); });
  }

  /* ---------- reveal on scroll ---------- */
  var reveals = [].slice.call(document.querySelectorAll(".reveal"));
  if (reduce || !("IntersectionObserver" in window)) {
    reveals.forEach(function (r) { r.classList.add("in"); });
  } else {
    var ro = new IntersectionObserver(function (entries, obs) {
      entries.forEach(function (en) {
        if (en.isIntersecting) { en.target.classList.add("in"); obs.unobserve(en.target); }
      });
    }, { threshold: 0.12 });
    reveals.forEach(function (r) { ro.observe(r); });
  }

  /* ---------- hero terminal typing ---------- */
  (function terminal() {
    var el = document.getElementById("typed");
    if (!el) return;
    var PROMPT = '<span class="p">nyx:root$</span> ';
    var seq = [
      { type: "cmd", text: "nyxfetch" },
      { type: "out", lines: [
        '',
        '       <span class="m">.:::o:o#:.</span>           <span class="v">nyx@nyxos</span>',
        '    <span class="m">.:oo.. :o.</span>              <span class="p">-----------------</span>',
        '  <span class="m">:oo:.oo.o:</span>                <span class="k">OS:</span>         NyxOS x86_64',
        ' <span class="m">.#o:.   :.</span>                 <span class="k">Host:</span>       QEMU Standard PC',
        ' <span class="m">#:::....:</span>                  <span class="k">Kernel:</span>     NyxOS 5.8.72 (Full Suite)',
        '<span class="m">o#::. . o.</span>                  <span class="k">Uptime:</span>     00:00:11',
        '<span class="m">o#.o:   :o</span>                  <span class="k">Resolution:</span> 1024 x 768',
        '<span class="m">o###o   o#</span>                  <span class="k">CPU:</span>        QEMU Virtual CPU version 2.5+ (1)',
        '<span class="m">:#oo::  .oo.</span>                <span class="k">Memory:</span>     255 / 255 MiB (0%)',
        ' <span class="m">o#o:o..  :o:.</span>              <span class="k">Processes:</span>  5',
        '  <span class="m">o#ooo::.:::#::        .:.</span> <span class="k">Disk:</span>       16M EXT2 at /mnt',
        '  <span class="m">.:o#oo::.: ..:oo::.o:#o.</span>  <span class="k">Network:</span>    10.0.2.15 (DHCP)',
        '     <span class="m">:o#####:#::o:.::o:</span>     <span class="k">Shell:</span>      NyxOS Terminal',
        '        <span class="m">.::oo####::.</span>        <span class="k">Time:</span>       2026-07-16 18:00:12',
        ''
      ]},
      { type: "cmd", text: "poll() demo" },
      { type: "out", lines: [
        'pipe readable &rarr; <span class="v">"from the pipe"</span>',
        'socket readable &rarr; <span class="v">"from the socket"</span>',
        'poll &rarr; 0 (timeout)',
        '<span class="p">OK</span>',
        ''
      ]},
      { type: "cmd", text: "echo TCPNCOK | nc 127.0.0.1 7" },
      { type: "out", lines: [
        '<span class="k">TCPNCOK</span>',
        ''
      ]},
      { type: "cmd", text: "exec /alarmdemo.elf" },
      { type: "out", lines: [
        'previous alarm had ~5s left',
        '<span class="v">SIGALRM delivered!</span>',
        'alarm()/SIGALRM works!',
        ''
      ]}
    ];

    var html = "";
    var si = 0;
    function step() {
      if (si >= seq.length) { setTimeout(function(){ si = 0; html=""; el.innerHTML=""; step(); }, 6000); return; }
      var item = seq[si++];
      if (item.type === "cmd") {
        html += PROMPT;
        el.innerHTML = html;
        typeChars(item.text, 0);
      } else {
        printLines(item.lines, 0);
      }
    }
    function typeChars(text, i) {
      if (i >= text.length) { html += "\n"; el.innerHTML = html; setTimeout(step, 320); return; }
      el.innerHTML = html + text.slice(0, i + 1);
      setTimeout(function () { typeChars(text, i + 1); }, 34 + Math.random() * 34);
    }
    function printLines(lines, i) {
      if (i >= lines.length) { setTimeout(step, 420); return; }
      html += lines[i] + "\n";
      el.innerHTML = html;
      setTimeout(function () { printLines(lines, i + 1); }, 90);
    }
    if (reduce) {
      // Render final state without animation
      seq.forEach(function (item) {
        if (item.type === "cmd") html += PROMPT + item.text + "\n";
        else html += item.lines.join("\n") + "\n";
      });
      el.innerHTML = html;
    } else {
      setTimeout(step, 500);
    }
  })();

  /* ---------- year in footer (if any) ---------- */
})();
