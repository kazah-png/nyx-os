document.addEventListener('DOMContentLoaded', () => {
  initCardSpotlight();
});

function initCardSpotlight() {
  const cards = document.querySelectorAll(
    '.pipeline-stage, .app-editorial-card, .spec-block, .manual-step-box, .reg-display-card, .layer-bar, .code-playground-box, .qemu-config-card'
  );

  cards.forEach(card => {
    card.addEventListener('mousemove', (e) => {
      const rect = card.getBoundingClientRect();
      const x = e.clientX - rect.left;
      const y = e.clientY - rect.top;
      card.style.setProperty('--mouse-x', `${x}px`);
      card.style.setProperty('--mouse-y', `${y}px`);
    });
  });
}
