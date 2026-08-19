document.addEventListener('DOMContentLoaded', () => {
  initSyscallInspector();
});

function initSyscallInspector() {
  const syscallChips = document.querySelectorAll('.syscall-chip');
  const regRax = document.getElementById('reg-rax');
  const regRdi = document.getElementById('reg-rdi');
  const regRsi = document.getElementById('reg-rsi');
  const regRdx = document.getElementById('reg-rdx');
  const regR10 = document.getElementById('reg-r10');
  const regR8  = document.getElementById('reg-r8');
  const syscallDesc = document.getElementById('syscall-detail-desc');
  const syscallNameEl = document.getElementById('syscall-active-name');

  if (!syscallChips.length || !regRax) return;

  const syscallData = {
    'sys_write': {
      name: 'sys_write (Syscall #1)',
      rax: '0x0000000000000001',
      rdi: '0x0000000000000001 (fd = STDOUT)',
      rsi: '0x0000000040001080 (const char *buf)',
      rdx: '0x000000000000002A (size_t count = 42)',
      r10: '0x0000000000000000 (unused)',
      r8:  '0x0000000000000000 (unused)',
      desc: 'Writes up to count bytes from buffer to open file descriptor. Checks user-space memory pointers before committing to VFS/framebuffer driver.'
    },
    'sys_read': {
      name: 'sys_read (Syscall #0)',
      rax: '0x0000000000000000',
      rdi: '0x0000000000000000 (fd = STDIN)',
      rsi: '0x0000000040002000 (char *buf)',
      rdx: '0x0000000000000400 (count = 1024)',
      r10: '0x0000000000000000 (unused)',
      r8:  '0x0000000000000000 (unused)',
      desc: 'Reads up to count bytes from file descriptor into buffer. Preemptible blocked I/O wakes waiting thread upon keyboard/disk IRQ.'
    },
    'sys_fork': {
      name: 'sys_fork (Syscall #57)',
      rax: '0x0000000000000039',
      rdi: '0x0000000000000000 (flags = 0)',
      rsi: '0x0000000000000000 (none)',
      rdx: '0x0000000000000000 (none)',
      r10: '0x0000000000000000 (none)',
      r8:  '0x0000000000000000 (none)',
      desc: 'Duplicates current process context. Allocates new PML4 page table with Copy-On-Write (COW) page sharing and isolated file descriptor table.'
    },
    'sys_poll': {
      name: 'sys_poll (Syscall #7)',
      rax: '0x0000000000000007',
      rdi: '0x0000000040003200 (struct pollfd *fds)',
      rsi: '0x0000000000000004 (nfds = 4)',
      rdx: '0x00000000000003E8 (timeout_ms = 1000)',
      r10: '0x0000000000000000 (sigmask = NULL)',
      r8:  '0x0000000000000000 (none)',
      desc: 'I/O multiplexing: waits for events on multiple socket/pipe descriptors simultaneously with microsecond APIC timer precision.'
    },
    'sys_socket': {
      name: 'sys_socket (Syscall #41)',
      rax: '0x0000000000000029',
      rdi: '0x0000000000000002 (AF_INET = 2)',
      rsi: '0x0000000000000001 (SOCK_STREAM = 1)',
      rdx: '0x0000000000000006 (IPPROTO_TCP = 6)',
      r10: '0x0000000000000000 (none)',
      r8:  '0x0000000000000000 (none)',
      desc: 'Allocates TCP/IP socket endpoint in kernel network subsystem. Returns new file descriptor bound to RTL8139 TCP state machine.'
    }
  };

  syscallChips.forEach(chip => {
    chip.addEventListener('click', () => {
      syscallChips.forEach(c => c.classList.remove('active'));
      chip.classList.add('active');

      const id = chip.getAttribute('data-syscall') || 'sys_write';
      const data = syscallData[id] || syscallData['sys_write'];

      if (syscallNameEl) syscallNameEl.textContent = data.name;
      if (regRax) regRax.textContent = data.rax;
      if (regRdi) regRdi.textContent = data.rdi;
      if (regRsi) regRsi.textContent = data.rsi;
      if (regRdx) regRdx.textContent = data.rdx;
      if (regR10) regR10.textContent = data.r10;
      if (regR8)  regR8.textContent  = data.r8;
      if (syscallDesc) syscallDesc.textContent = data.desc;

      // Pulse registers
      const regCards = document.querySelectorAll('.reg-display-card');
      regCards.forEach(rc => {
        rc.classList.add('pulse');
        setTimeout(() => rc.classList.remove('pulse'), 400);
      });
    });
  });
}
