document.addEventListener('DOMContentLoaded', () => {
  initQemuBuilder();
});

function initQemuBuilder() {
  const ramSelect = document.getElementById('qemu-ram');
  const cpuSelect = document.getElementById('qemu-cpu');
  const netSelect = document.getElementById('qemu-net');
  const audioSelect = document.getElementById('qemu-audio');
  const cmdOutput = document.getElementById('qemu-generated-cmd');
  const copyBtn = document.getElementById('qemu-copy-btn');

  if (!cmdOutput) return;

  function updateCommand() {
    const ram = ramSelect ? ramSelect.value : '256M';
    const cpu = cpuSelect ? cpuSelect.value : '2';
    const net = netSelect ? netSelect.value : 'rtl8139';
    const audio = audioSelect ? audioSelect.value : 'sb16';

    let netFlag = '-nic user,model=rtl8139';
    if (net === 'tap') {
      netFlag = '-netdev tap,id=n0,ifname=tap0,script=no -device rtl8139,netdev=n0';
    } else if (net === 'none') {
      netFlag = '-nic none';
    }

    let audioFlag = '-audiodev dsound,id=snd0 -device sb16,audiodev=snd0';
    if (audio === 'none') {
      audioFlag = '';
    }

    const command = `qemu-system-x86_64 -cdrom NyxOS.iso -m ${ram} -smp ${cpu} ${netFlag} ${audioFlag} -vga std -display sdl`.replace(/\s+/g, ' ').trim();
    
    cmdOutput.textContent = command;
  }

  [ramSelect, cpuSelect, netSelect, audioSelect].forEach(sel => {
    if (sel) sel.addEventListener('change', updateCommand);
  });

  if (copyBtn) {
    copyBtn.addEventListener('click', () => {
      const text = cmdOutput.textContent;
      if (navigator.clipboard) {
        navigator.clipboard.writeText(text).then(() => {
          if (typeof window.showToast === 'function') {
            window.showToast('QEMU command copied to clipboard', '[QEMU]');
          }
          copyBtn.textContent = '[COPIED]';
          setTimeout(() => { copyBtn.textContent = 'Copy Command'; }, 2000);
        });
      }
    });
  }

  updateCommand();
}
