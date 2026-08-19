document.addEventListener('DOMContentLoaded', () => {
  initSeleneSimulator();
});

function initSeleneSimulator() {
  const urlInput = document.getElementById('selene-url-field');
  const pageBody = document.getElementById('selene-page-content');
  const statusEl = document.getElementById('selene-status-text');
  const refreshBtn = document.getElementById('selene-refresh-btn');
  const presetChips = document.querySelectorAll('.selene-preset-chip');

  if (!urlInput || !pageBody) return;

  const mockPages = {
    'https://nyxos.cc': `
<div style="font-weight: bold; font-size: 1.05rem; color: #fff; margin-bottom: 0.4rem;">NyxOS — 64-Bit Operating System from Scratch</div>
<p style="color: #94a3b8; margin-bottom: 0.8rem;">A preemptive kernel, a windowed desktop with the Selene web browser, a real TCP/IP stack, EXT2 filesystem, and an in-OS C toolchain that self-hosts (TinyCC + xbm) plus the native N language.</p>
<div style="color: #c084fc; margin-bottom: 0.4rem;">[ ARCHITECTURE ]</div>
<p style="color: #cbd5e1;">x86_64 Long Mode · 4-Level Paging · 57 Syscalls · Ring-3 Isolation · 55 KATs Crypto CI</p>
<div style="margin-top: 0.8rem; border-top: 1px solid #1e293b; padding-top: 0.4rem; color: #64748b; font-size: 0.75rem;">
  HTTP/1.1 200 OK | Content-Length: 1420 bytes | Connection: close
</div>
    `,
    'http://kernel.org/release': `
<div style="font-weight: bold; font-size: 1.05rem; color: #c084fc; margin-bottom: 0.4rem;">Low-Level Kernel Registry</div>
<p style="color: #94a3b8;">Status: Operational. Architecture: x86_64 Long Mode. Page size: 4096 bytes.</p>
<ul style="padding-left: 1.2rem; color: #e2e8f0; margin-top: 0.4rem;">
  <li>Interrupt controller: I/O APIC Vector 32 (1000 Hz)</li>
  <li>Network: Realtek RTL8139 PCI Fast Ethernet</li>
  <li>Audio: Sound Blaster 16 DMA IRQ 5</li>
</ul>
    `,
    'http://metal.nyx': `
<div style="font-weight: bold; font-size: 1.05rem; color: #c084fc; margin-bottom: 0.4rem;">[ NYX METAL DISPATCH ]</div>
<p style="color: #e2e8f0;">"No libraries. Just the metal."</p>
<p style="color: #94a3b8; margin-top: 0.4rem;">NyxOS boots via Multiboot2, constructs its own identity paging, activates 64-bit Long Mode, and hands execution to the higher-half kernel.</p>
    `
  };

  function loadUrl(targetUrl) {
    const cleanUrl = targetUrl.trim();
    if (statusEl) {
      statusEl.textContent = `Resolving DNS for ${cleanUrl}...`;
    }
    pageBody.style.opacity = '0.4';

    setTimeout(() => {
      if (statusEl) {
        statusEl.textContent = `TCP 3-Way Handshake -> Connected. Fetching HTTP/1.1...`;
      }
      setTimeout(() => {
        if (statusEl) {
          statusEl.textContent = `HTTP/1.1 200 OK (Chunked Transfer Decoded)`;
        }
        pageBody.innerHTML = mockPages[cleanUrl] || `
          <div style="font-weight: bold; color: #fff;">${escapeHtml(cleanUrl)}</div>
          <p style="color: #94a3b8; margin-top: 0.5rem;">Raw HTTP content rendered by Selene HTML tokenizer.</p>
          <p style="color: #34d399; margin-top: 0.5rem;">Payload received: 200 OK.</p>
        `;
        pageBody.style.opacity = '1';
      }, 350);
    }, 250);
  }

  urlInput.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') {
      loadUrl(urlInput.value);
    }
  });

  if (refreshBtn) {
    refreshBtn.addEventListener('click', () => {
      loadUrl(urlInput.value);
    });
  }

  presetChips.forEach(chip => {
    chip.addEventListener('click', () => {
      const url = chip.getAttribute('data-url');
      urlInput.value = url;
      loadUrl(url);
    });
  });

  // Initial load
  loadUrl('https://nyxos.cc');

  function escapeHtml(str) {
    return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }
}
