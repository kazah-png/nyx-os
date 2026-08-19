document.addEventListener('DOMContentLoaded', () => {
  initDesktopSimulator();
});

function initDesktopSimulator() {
  const sim = document.querySelector('.desktop-simulator');
  const floatingWin = document.querySelector('.sim-floating-window');
  const closeBtn = document.querySelector('.sim-floating-window .sim-dot.close');
  const maxBtn = document.querySelector('.sim-floating-window .sim-dot.max');
  const minBtn = document.querySelector('.sim-floating-window .sim-dot.min');

  if (!sim) return;

  // 3D Parallax Tilt Effect
  let isHovered = false;

  sim.addEventListener('mouseenter', () => { isHovered = true; });
  sim.addEventListener('mouseleave', () => {
    isHovered = false;
    sim.style.transform = 'perspective(1200px) rotateX(0deg) rotateY(0deg)';
  });

  sim.addEventListener('mousemove', (e) => {
    if (!isHovered) return;
    const rect = sim.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;
    
    const centerX = rect.width / 2;
    const centerY = rect.height / 2;
    
    const rotateX = ((y - centerY) / centerY) * -5;
    const rotateY = ((x - centerX) / centerX) * 6;

    sim.style.transform = `perspective(1200px) rotateX(${rotateX}deg) rotateY(${rotateY}deg)`;
  });

  // Floating Window Controls
  if (floatingWin) {
    if (closeBtn) {
      closeBtn.addEventListener('click', (e) => {
        e.stopPropagation();
        floatingWin.style.opacity = '0';
        floatingWin.style.pointerEvents = 'none';
        floatingWin.style.transform = 'scale(0.8)';
        setTimeout(() => {
          // Restore after 3 seconds for continuous showcase
          floatingWin.style.opacity = '1';
          floatingWin.style.pointerEvents = 'auto';
          floatingWin.style.transform = 'scale(1)';
        }, 3000);
      });
    }

    if (maxBtn) {
      let isMax = false;
      maxBtn.addEventListener('click', (e) => {
        e.stopPropagation();
        isMax = !isMax;
        if (isMax) {
          floatingWin.style.width = '92%';
          floatingWin.style.height = '88%';
          floatingWin.style.left = '4%';
          floatingWin.style.top = '6%';
        } else {
          floatingWin.style.width = '72%';
          floatingWin.style.height = '68%';
          floatingWin.style.left = '10%';
          floatingWin.style.top = '14%';
        }
      });
    }
  }
}
