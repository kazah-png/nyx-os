document.addEventListener('DOMContentLoaded', () => {
  initTerminal();
});

function initTerminal() {
  const termBody = document.getElementById('terminal-body');
  const termInput = document.getElementById('term-input');
  const quickPills = document.querySelectorAll('.cmd-pill');

  if (!termBody || !termInput) return;

  const cmdHistory = [];
  let historyIdx = -1;

  // Auto-run initial nyxfetch
  runInitialFetch();

  // Handle Command Input
  termInput.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') {
      const rawCmd = termInput.value.trim();
      if (rawCmd) {
        cmdHistory.push(rawCmd);
        historyIdx = cmdHistory.length;
        executeCommand(rawCmd);
      } else {
        appendEmptyPrompt();
      }
      termInput.value = '';
      scrollToBottom();
    } else if (e.key === 'ArrowUp') {
      e.preventDefault();
      if (historyIdx > 0) {
        historyIdx--;
        termInput.value = cmdHistory[historyIdx];
      }
    } else if (e.key === 'ArrowDown') {
      e.preventDefault();
      if (historyIdx < cmdHistory.length - 1) {
        historyIdx++;
        termInput.value = cmdHistory[historyIdx];
      } else {
        historyIdx = cmdHistory.length;
        termInput.value = '';
      }
    }
  });

  // Quick chips click
  quickPills.forEach(pill => {
    pill.addEventListener('click', () => {
      const cmd = pill.getAttribute('data-cmd') || pill.textContent.trim();
      termInput.value = cmd;
      termInput.focus();
      executeCommand(cmd);
      termInput.value = '';
      scrollToBottom();
    });
  });

  function scrollToBottom() {
    termBody.scrollTop = termBody.scrollHeight;
  }

  function appendEmptyPrompt() {
    const line = document.createElement('div');
    line.className = 'term-line prompt-line';
    line.innerHTML = `<span class="term-prompt-tag">nyx:root</span> <span class="term-prompt-cwd">/</span> <span class="term-prompt-char">#</span>`;
    termBody.appendChild(line);
  }

  function appendCommandLine(cmd) {
    const line = document.createElement('div');
    line.className = 'term-line prompt-line';
    line.innerHTML = `<span class="term-prompt-tag">nyx:root</span> <span class="term-prompt-cwd">/</span> <span class="term-prompt-char">#</span> <span class="term-cmd-text">${escapeHtml(cmd)}</span>`;
    termBody.appendChild(line);
  }

  function appendOutput(html) {
    const out = document.createElement('div');
    out.className = 'term-output-block';
    out.innerHTML = html;
    termBody.appendChild(out);
  }

  function executeCommand(input) {
    appendCommandLine(input);
    const parts = input.split(' ');
    const cmd = parts[0].toLowerCase();
    const args = parts.slice(1);

    switch (cmd) {
      case 'nyxfetch':
        appendOutput(getNyxfetchOutput());
        break;

      case 'help':
        appendOutput(`
          <div style="color: #c084fc; font-weight: bold; margin-bottom: 0.4rem;">NYXOS KERNEL SHELL — BUILT-IN COMMANDS</div>
          <div style="display: grid; grid-template-columns: 90px 1fr; gap: 0.25rem 0.8rem; font-size: 0.82rem;">
            <span style="color: #a855f7;">System:</span> <span>help clear nyxfetch uname date version reboot history free mem</span>
            <span style="color: #a855f7;">Files:</span> <span>ls cd pwd cat touch mkdir rm cp mv find tree diff hexdump</span>
            <span style="color: #a855f7;">Processes:</span> <span>ps exec spawn jobs wait kill nice pmap</span>
            <span style="color: #a855f7;">Toolchain:</span> <span>cc xbm ncc tcc (self-hosting C compiler & packages)</span>
            <span style="color: #a855f7;">Network:</span> <span>ifconfig dhcp ping setip tcptest tcpserve nc</span>
            <span style="color: #a855f7;">Apps:</span> <span>selene doom pong snake tetris mines gui</span>
          </div>
        `);
        break;

      case 'uname':
        if (args.includes('-a')) {
          appendOutput(`<div>NyxOS nyx-core 6.4.180 #1 SMP PREEMPT x86_64 LongMode GNU/Nyx</div>`);
        } else {
          appendOutput(`<div>NyxOS</div>`);
        }
        break;

      case 'cat':
        if (args[0] === '/etc/os-release' || args[0] === 'os-release') {
          appendOutput(`
NAME="NyxOS"
VERSION="6.4.180"
ID=nyxos
PRETTY_NAME="NyxOS 6.4.180 (x86_64 Long Mode)"
ARCH="x86_64"
HOME_URL="https://nyxos.cc"
TOOLCHAIN="TinyCC 0.9.27 + xbm + ncc"
FEATURES="Preemptive Kernel, Selene Browser, 5 Games, EXT2, TCP/IP"
          `);
        } else if (args[0] === '/etc/hostname') {
          appendOutput(`<div>nyx-core</div>`);
        } else if (!args[0]) {
          appendOutput(`<div style="color: #e06c75;">cat: missing file operand</div>`);
        } else {
          appendOutput(`<div>/* File: ${escapeHtml(args[0])} */<br>System file verified intact. Length: 1024 bytes.</div>`);
        }
        break;

      case 'ls':
        appendOutput(`
<div style="display: flex; gap: 1.2rem; flex-wrap: wrap;">
  <span style="color: #c084fc; font-weight: bold;">bin/</span>
  <span style="color: #c084fc; font-weight: bold;">boot/</span>
  <span style="color: #c084fc; font-weight: bold;">dev/</span>
  <span style="color: #c084fc; font-weight: bold;">etc/</span>
  <span style="color: #c084fc; font-weight: bold;">home/</span>
  <span style="color: #e879f9; font-weight: bold;">mnt/ (ext2)</span>
  <span style="color: #c084fc; font-weight: bold;">proc/</span>
  <span style="color: #c084fc; font-weight: bold;">usr/</span>
</div>
        `);
        break;

      case 'pwd':
        appendOutput(`<div>/</div>`);
        break;

      case 'date':
        appendOutput(`<div>${new Date().toUTCString()}</div>`);
        break;

      case 'version':
        appendOutput(`<div>NyxOS Kernel v6.4.180 (x86_64, higher-half, 57 syscalls)</div>`);
        break;

      case 'clear':
        termBody.innerHTML = '';
        break;

      case 'ps':
        appendOutput(`
<table style="width: 100%; border-collapse: collapse; font-size: 0.78rem;">
  <tr style="color: #64748b; text-align: left;"><th>PID</th><th>PPID</th><th>USER</th><th>STAT</th><th>COMMAND</th></tr>
  <tr><td>1</td><td>0</td><td>root</td><td style="color:#c084fc">S</td><td>/bin/init</td></tr>
  <tr><td>2</td><td>1</td><td>root</td><td style="color:#c084fc">S</td><td>[kworker/apic]</td></tr>
  <tr><td>3</td><td>1</td><td>root</td><td style="color:#c084fc">S</td><td>[ktcp_retransmit]</td></tr>
  <tr><td>4</td><td>1</td><td>nyx</td><td style="color:#e879f9">R</td><td>/bin/compositor (gui)</td></tr>
  <tr><td>5</td><td>4</td><td>nyx</td><td style="color:#c084fc">S</td><td>/bin/selene</td></tr>
  <tr><td>6</td><td>4</td><td>nyx</td><td style="color:#e879f9">R</td><td>/bin/sh (terminal)</td></tr>
</table>
        `);
        break;

      case 'mem':
      case 'free':
        appendOutput(`
              total        used        free      shared  buff/cache   available
Mem:        262144K      48216K     213928K          0K       8192K     205736K
Heap:        16384K       4940K      11444K  (Kernel Higher-Half Linked)
        `);
        break;

      case 'ping':
        const target = args[0] || '127.0.0.1';
        appendOutput(`
<div>PING ${escapeHtml(target)} (${escapeHtml(target)}): 56 data bytes</div>
<div>64 bytes from ${escapeHtml(target)}: icmp_seq=1 ttl=64 time=0.082 ms</div>
<div>64 bytes from ${escapeHtml(target)}: icmp_seq=2 ttl=64 time=0.076 ms</div>
<div style="color: #c084fc; margin-top: 0.25rem;">--- ${escapeHtml(target)} ping statistics ---<br>2 packets transmitted, 2 received, 0% packet loss, rtt avg = 0.079 ms</div>
        `);
        break;

      case 'ifconfig':
        appendOutput(`
rtl0: flags=4163&lt;UP,BROADCAST,RUNNING&gt; mtu 1500
      inet 10.0.2.15 netmask 255.255.255.0 gateway 10.0.2.2
      ether 52:54:00:12:34:56 (PCI RTL8139 Fast Ethernet)

lo: flags=73&lt;UP,LOOPBACK,RUNNING&gt; mtu 65536
      inet 127.0.0.1 netmask 255.0.0.0
        `);
        break;

      case 'selene':
        const url = args[0] || 'https://nyxos.cc';
        appendOutput(`
<div style="color: #c084fc;">[SELENE] Resolving DNS for ${escapeHtml(url)}... OK</div>
<div style="color: #a855f7;">[SELENE] TCP Handshake Connected to port 80</div>
<div style="color: #f59e0b;">[SELENE] HTTP/1.1 GET / -> 200 OK (Content-Type: text/html, chunked)</div>
<div style="color: #e879f9; margin-top: 0.35rem; padding: 0.45rem; background: rgba(168,85,247,0.08); border-left: 2px solid #c084fc;">
  <strong>NyxOS — 64-bit operating system from scratch</strong><br>
  Preemptive kernel, windowed desktop with Selene browser, 5 games, TCP/IP, EXT2.
</div>
        `);
        break;

      case 'cc':
      case 'tcc':
        appendOutput(`
<div style="color: #c084fc;">[cc] TinyCC 0.9.27 in-OS native compiler</div>
<div>Compiling target (${args.join(' ') || 'hello.c'})...</div>
<div>ELF64 binary generated in /mnt/bin/hello (0 warnings). Execution success!</div>
        `);
        break;

      case 'xbm':
        if (args[0] === 'list') {
          appendOutput(`
<div style="color: #c084fc; font-weight: bold;">Installed xbm packages:</div>
<div>- ncc (N language native compiler v1.2) [installed]</div>
<div>- tcc (TinyCC self-hosting C compiler) [installed]</div>
<div>- doom-engine (1993 classic port) [installed]</div>
<div>- net-tools (nc, ping, ifconfig, tcpserve) [installed]</div>
          `);
        } else {
          appendOutput(`<div>xbm: package '${escapeHtml(args[1] || 'pkg')}' synced with recipe index.</div>`);
        }
        break;

      case 'doom':
      case 'games':
      case 'tetris':
      case 'snake':
      case 'pong':
      case 'mines':
        appendOutput(`
<div style="color: #f59e0b;">[GAME] Launching ${cmd.toUpperCase()} on compositor window...</div>
<div style="color: #c084fc;">Resolution: 320x240 @ 30fps framebuffer tick. Controls: [Arrow Keys / Space].</div>
        `);
        break;

      case 'reboot':
        appendOutput(`<div style="color: #e06c75;">[KERNEL] ACPI reset signal dispatched... Resetting system.</div>`);
        setTimeout(() => {
          termBody.innerHTML = '';
          runInitialFetch();
        }, 800);
        break;

      default:
        appendOutput(`<div style="color: #e06c75;">nyx: command not found: ${escapeHtml(cmd)}. Type '<span style="color:#c084fc">help</span>' for available commands.</div>`);
        break;
    }
  }

  function getNyxfetchOutput() {
    return `
<div style="display: flex; gap: 1.5rem; flex-wrap: wrap; align-items: center;">
<pre class="term-ascii-logo">
      /\\
     /  \\       NYXOS
    / /\\ \\      x86_64
   / /  \\ \\     
  /_/    \\_\\    
</pre>
<div class="term-stat-table">
  <span class="term-stat-key">OS:</span> <span class="term-stat-val accent">NyxOS 6.4.180 (Long Mode)</span>
  <span class="term-stat-key">Kernel:</span> <span class="term-stat-val">Higher-Half PML4[511]</span>
  <span class="term-stat-key">Syscalls:</span> <span class="term-stat-val">57 POSIX-like</span>
  <span class="term-stat-key">Architecture:</span> <span class="term-stat-val">x86_64 · C + NASM</span>
  <span class="term-stat-key">Network:</span> <span class="term-stat-val">RTL8139 · TCP/IP · poll()</span>
  <span class="term-stat-key">VFS / FS:</span> <span class="term-stat-val">Ramdisk + EXT2 Read/Write</span>
  <span class="term-stat-key">Toolchain:</span> <span class="term-stat-val">TinyCC 0.9.27 + xbm + ncc</span>
  <span class="term-stat-key">Compositor:</span> <span class="term-stat-val">32 Windows · 4 Workspaces</span>
  <span class="term-stat-key">Security:</span> <span class="term-stat-val accent">NX · SMEP · SMAP · W^X</span>
  <span class="term-stat-key">Crypto KATs:</span> <span class="term-stat-val accent">55 Verified Tests</span>
</div>
</div>
    `;
  }

  function runInitialFetch() {
    termBody.innerHTML = '';
    const initialLine = document.createElement('div');
    initialLine.className = 'term-line prompt-line';
    initialLine.innerHTML = `<span class="term-prompt-tag">nyx:root</span> <span class="term-prompt-cwd">/</span> <span class="term-prompt-char">#</span> <span class="term-cmd-text">nyxfetch</span>`;
    termBody.appendChild(initialLine);

    setTimeout(() => {
      appendOutput(getNyxfetchOutput());
      appendEmptyPrompt();
      scrollToBottom();
    }, 400);
  }

  function escapeHtml(str) {
    return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }
}
