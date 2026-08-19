document.addEventListener('DOMContentLoaded', () => {
  initPagingVisualizer();
});

function initPagingVisualizer() {
  const vaddrInput = document.getElementById('paging-vaddr-input');
  const pml4Box = document.getElementById('mmu-pml4-idx');
  const pdptBox = document.getElementById('mmu-pdpt-idx');
  const pdBox   = document.getElementById('mmu-pd-idx');
  const ptBox   = document.getElementById('mmu-pt-idx');
  const offsetBox = document.getElementById('mmu-offset-idx');
  const paddrBox  = document.getElementById('mmu-paddr-out');
  const presetBtns = document.querySelectorAll('.mmu-preset-btn');

  if (!vaddrInput || !pml4Box) return;

  function calculatePaging(hexStr) {
    let cleanHex = hexStr.trim().replace(/^0x/i, '');
    if (!/^[0-9a-fA-F]+$/.test(cleanHex)) {
      cleanHex = 'FFFFFF8000100000';
    }

    try {
      // Pad to 16 hex digits (64-bit)
      cleanHex = cleanHex.padStart(16, '0');
      const bigVal = BigInt('0x' + cleanHex);

      // x86_64 4-Level Paging bitfields:
      // Offset: bits 0..11 (12 bits)
      // PT:     bits 12..20 (9 bits)
      // PD:     bits 21..29 (9 bits)
      // PDPT:   bits 30..38 (9 bits)
      // PML4:   bits 39..47 (9 bits)

      const offset = Number(bigVal & 0xFFFn);
      const pt   = Number((bigVal >> 12n) & 0x1FFn);
      const pd   = Number((bigVal >> 21n) & 0x1FFn);
      const pdpt = Number((bigVal >> 30n) & 0x1FFn);
      const pml4 = Number((bigVal >> 39n) & 0x1FFn);

      if (pml4Box) pml4Box.textContent = `PML4[${pml4}] (0x${pml4.toString(16).toUpperCase().padStart(3, '0')})`;
      if (pdptBox) pdptBox.textContent = `PDPT[${pdpt}] (0x${pdpt.toString(16).toUpperCase().padStart(3, '0')})`;
      if (pdBox)   pdBox.textContent   = `PD[${pd}] (0x${pd.toString(16).toUpperCase().padStart(3, '0')})`;
      if (ptBox)   ptBox.textContent   = `PT[${pt}] (0x${pt.toString(16).toUpperCase().padStart(3, '0')})`;
      if (offsetBox) offsetBox.textContent = `Offset: 0x${offset.toString(16).toUpperCase().padStart(3, '0')}`;

      // Mock Physical Address computation based on kernel identity/heap map
      const mockPaddr = (BigInt(pt * 4096) + BigInt(offset)).toString(16).toUpperCase().padStart(8, '0');
      if (paddrBox) paddrBox.textContent = `0x00000000${mockPaddr} [Present · R/W · NX=0]`;
    } catch (e) {
      console.warn('Invalid address computation:', e);
    }
  }

  vaddrInput.addEventListener('input', () => {
    calculatePaging(vaddrInput.value);
  });

  presetBtns.forEach(btn => {
    btn.addEventListener('click', () => {
      presetBtns.forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      const addr = btn.getAttribute('data-addr') || '0xFFFFFF8000100000';
      vaddrInput.value = addr;
      calculatePaging(addr);
    });
  });

  // Initial calculation
  calculatePaging('0xFFFFFF8000100000');
}
