document.addEventListener('DOMContentLoaded', () => {
  initScrollReveals();
  initStickyHeader();
  initMobileDrawer();
  initSystemTicker();
  initCommandSearch();
  initToasts();
});
function initScrollReveals() {
  const revealElements = document.querySelectorAll('.reveal-item');
  if (!revealElements.length) return;

  const reveal = el => el.classList.add('is-revealed');

  // No IntersectionObserver → just show everything.
  if (!('IntersectionObserver' in window)) {
    revealElements.forEach(reveal);
    return;
  }

  let ioFired = false;
  const observer = new IntersectionObserver((entries, obs) => {
    ioFired = true;
    entries.forEach(entry => {
      if (entry.isIntersecting) {
        reveal(entry.target);
        obs.unobserve(entry.target);
      }
    });
  }, {
    root: null,
    threshold: 0.12,
    rootMargin: '0px 0px -40px 0px'
  });

  revealElements.forEach(el => observer.observe(el));

  // Safety net: if the observer never fires (a non-compositing/background tab can
  // throttle it), reveal everything so content is never permanently invisible.
  // On a normal browser the initial callback sets ioFired, so this is a no-op.
  window.addEventListener('load', () => setTimeout(() => {
    if (!ioFired) revealElements.forEach(reveal);
  }, 1500));
}

/* ==========================================================================
   Sticky Header & Scrollspy
   ========================================================================== */
function initStickyHeader() {
  const header = document.querySelector('.nav-header');
  const sections = document.querySelectorAll('section[id]');
  const navLinks = document.querySelectorAll('.nav-link, .mobile-nav-links a');

  if (!header) return;

  let ticking = false;

  window.addEventListener('scroll', () => {
    if (!ticking) {
      window.requestAnimationFrame(() => {
        const scrollY = window.scrollY;

        // Toggle sticky background
        if (scrollY > 40) {
          header.classList.add('is-scrolled');
        } else {
          header.classList.remove('is-scrolled');
        }

        // Active Section Scrollspy
        let currentSectionId = '';
        sections.forEach(section => {
          const sectionTop = section.offsetTop - 160;
          const sectionHeight = section.offsetHeight;
          if (scrollY >= sectionTop && scrollY < sectionTop + sectionHeight) {
            currentSectionId = section.getAttribute('id');
          }
        });

        if (currentSectionId) {
          navLinks.forEach(link => {
            const href = link.getAttribute('href');
            if (href === `#${currentSectionId}`) {
              link.classList.add('active');
            } else if (href.startsWith('#')) {
              link.classList.remove('active');
            }
          });
        }

        ticking = false;
      });
      ticking = true;
    }
  }, { passive: true });
}

/* ==========================================================================
   Mobile Drawer Navigation
   ========================================================================== */
function initMobileDrawer() {
  const toggle = document.querySelector('.nav-toggle');
  const drawer = document.querySelector('.mobile-drawer');
  const backdrop = document.querySelector('.mobile-drawer-backdrop');
  const drawerLinks = document.querySelectorAll('.mobile-nav-links a');

  if (!toggle || !drawer || !backdrop) return;

  const toggleMenu = (open) => {
    const isOpen = open !== undefined ? open : !drawer.classList.contains('is-open');
    toggle.classList.toggle('is-open', isOpen);
    drawer.classList.toggle('is-open', isOpen);
    backdrop.classList.toggle('is-open', isOpen);
    document.body.style.overflow = isOpen ? 'hidden' : '';
    toggle.setAttribute('aria-expanded', isOpen);
  };

  toggle.addEventListener('click', () => toggleMenu());
  backdrop.addEventListener('click', () => toggleMenu(false));

  drawerLinks.forEach(link => {
    link.addEventListener('click', () => toggleMenu(false));
  });

  window.addEventListener('keydown', (e) => {
    if (e.key === 'Escape' && drawer.classList.contains('is-open')) {
      toggleMenu(false);
    }
  });
}

/* ==========================================================================
   APIC Timer & Kernel Uptime Simulation
   ========================================================================== */
function initSystemTicker() {
  const tickEl = document.getElementById('kernel-tick-counter');
  const uptimeEl = document.getElementById('kernel-uptime-val');
  const heapEl = document.getElementById('kernel-heap-val');

  let ticks = 1048576;
  let seconds = 384;

  setInterval(() => {
    ticks += 1000;
    seconds += 1;

    if (tickEl) {
      tickEl.textContent = `${ticks.toLocaleString()} Hz`;
    }

    if (uptimeEl) {
      const mins = Math.floor(seconds / 60);
      const secs = seconds % 60;
      uptimeEl.textContent = `${mins}m ${secs < 10 ? '0' : ''}${secs}s`;
    }

    if (heapEl && Math.random() > 0.7) {
      const mb = (4.82 + (Math.random() * 0.08)).toFixed(2);
      heapEl.textContent = `${mb} / 16 MB`;
    }
  }, 1000);
}

/* ==========================================================================
   Searchable Command Matrix Filter
   ========================================================================== */
function initCommandSearch() {
  const searchInput = document.getElementById('cmd-search');
  const tableRows = document.querySelectorAll('.cmd-table tbody tr');

  if (!searchInput || !tableRows.length) return;

  searchInput.addEventListener('input', (e) => {
    const query = e.target.value.toLowerCase().trim();

    tableRows.forEach(row => {
      const text = row.textContent.toLowerCase();
      if (text.includes(query)) {
        row.style.display = '';
      } else {
        row.style.display = 'none';
      }
    });
  });
}

/* ==========================================================================
   Toast Notification & Clipboard Utility
   ========================================================================== */
function initToasts() {
  window.showToast = function(message, icon = '✓') {
    let container = document.querySelector('.toast-container');
    if (!container) {
      container = document.createElement('div');
      container.className = 'toast-container';
      document.body.appendChild(container);
    }

    const toast = document.createElement('div');
    toast.className = 'toast-message';
    toast.innerHTML = `<span class="toast-icon">${icon}</span><span>${message}</span>`;
    container.appendChild(toast);

    requestAnimationFrame(() => {
      toast.classList.add('is-visible');
    });

    setTimeout(() => {
      toast.classList.remove('is-visible');
      setTimeout(() => toast.remove(), 350);
    }, 3000);
  };

  // Global copy button handler
  document.addEventListener('click', (e) => {
    const copyBtn = e.target.closest('.copy-btn');
    if (copyBtn) {
      const textToCopy = copyBtn.getAttribute('data-copy') || copyBtn.innerText;
      if (navigator.clipboard) {
        navigator.clipboard.writeText(textToCopy).then(() => {
          copyBtn.classList.add('copied');
          const original = copyBtn.innerHTML;
          copyBtn.innerHTML = '✓ Copied';
          window.showToast(`Copied to clipboard: "${textToCopy.slice(0, 30)}..."`);
          setTimeout(() => {
            copyBtn.classList.remove('copied');
            copyBtn.innerHTML = original;
          }, 2000);
        });
      }
    }
  });
}

/* ==========================================================================
   Focus Shell button (replaces an inline onclick — keeps a strict CSP)
   ========================================================================== */
document.addEventListener('DOMContentLoaded', () => {
  const fs = document.getElementById('btn-focus-shell');
  const ti = document.getElementById('term-input');
  if (fs && ti) fs.addEventListener('click', () => ti.focus());
});
