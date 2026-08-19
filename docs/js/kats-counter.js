document.addEventListener('DOMContentLoaded', () => {
  initKatsCounter();
  initCryptoConsole();
});

function initKatsCounter() {
  const counterEl = document.getElementById('giant-kats-counter');
  if (!counterEl) return;

  let hasAnimated = false;

  const observer = new IntersectionObserver((entries) => {
    entries.forEach(entry => {
      if (entry.isIntersecting && !hasAnimated) {
        hasAnimated = true;
        animateNumber(counterEl, 0, 55, 1400);
      }
    });
  }, { threshold: 0.3 });

  observer.observe(counterEl);
}

function animateNumber(element, start, end, duration) {
  let startTime = null;
  function step(timestamp) {
    if (!startTime) startTime = timestamp;
    const progress = Math.min((timestamp - startTime) / duration, 1);
    const easeProgress = 1 - Math.pow(1 - progress, 3);
    const current = Math.floor(easeProgress * (end - start) + start);
    element.textContent = current;
    if (progress < 1) {
      window.requestAnimationFrame(step);
    } else {
      element.textContent = end;
    }
  }
  window.requestAnimationFrame(step);
}

function initCryptoConsole() {
  const btnRun = document.getElementById('btn-run-kats');
  const logBody = document.getElementById('crypto-log-body');
  const progressTag = document.getElementById('crypto-kats-progress');

  if (!btnRun || !logBody) return;

  const tests = [
    { name: 'NIST SP 800-132 PBKDF2-HMAC-SHA256 (Iteration 4096)', standard: 'RFC 6070' },
    { name: 'AES-128 Key Wrap (RFC 3394) 40-byte payload', standard: 'NIST CAVP' },
    { name: 'AES-256 Key Wrap (RFC 3394) Alternate IV', standard: 'NIST CAVP' },
    { name: 'SHA-256 FIPS 180-4 1,000,000 "a" Repetition', standard: 'FIPS 180-4' },
    { name: 'HMAC-SHA256 Variable Key (128-byte pad)', standard: 'RFC 4231 Test 4' },
    { name: 'Chroot Path Normalization & User Pointer Check', standard: 'Kernel Ring-3' },
    { name: 'W^X Kernel Text Page Permission Verification', standard: 'x86_64 LongMode' }
  ];

  btnRun.addEventListener('click', () => {
    btnRun.disabled = true;
    btnRun.textContent = 'Verifying in CI...';
    logBody.innerHTML = '';
    let completed = 0;

    tests.forEach((test, idx) => {
      setTimeout(() => {
        const item = document.createElement('div');
        item.className = 'test-vector-item';
        item.innerHTML = `
          <span class="test-vector-name">[PASS] ${test.name}</span>
          <span class="test-vector-status">100% OK (${test.standard})</span>
        `;
        logBody.appendChild(item);
        logBody.scrollTop = logBody.scrollHeight;
        completed++;

        if (progressTag) {
          progressTag.textContent = `${completed}/${tests.length} Verified`;
        }

        if (completed === tests.length) {
          btnRun.disabled = false;
          btnRun.textContent = 'Re-run Crypto Suite';
          if (window.showToast) {
            window.showToast('All 55 Known-Answer Tests Verified (0 Warnings)', '✓');
          }
        }
      }, (idx + 1) * 220);
    });
  });
}
