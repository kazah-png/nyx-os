document.addEventListener('DOMContentLoaded', () => {
  initAudioChime();
});

function initAudioChime() {
  const audioBtn = document.getElementById('btn-play-chime');
  if (!audioBtn) return;

  let audioCtx = null;

  audioBtn.addEventListener('click', () => {
    try {
      if (!audioCtx) {
        const AudioContext = window.AudioContext || window.webkitAudioContext;
        audioCtx = new AudioContext();
      }

      if (audioCtx.state === 'suspended') {
        audioCtx.resume();
      }

      playNyxBootChord(audioCtx);

      if (typeof window.showToast === 'function') {
        window.showToast('SB16 Audio DMA: 44.1kHz Stereo Boot Chime Playing', '[SB16]');
      }
    } catch (err) {
      console.warn('Web Audio API not supported or blocked:', err);
    }
  });

  function playNyxBootChord(ctx) {
    const now = ctx.currentTime;
    
    // Notes: C4, G4, C5, E5, G5 (Classic Retro Hardware Chime)
    const frequencies = [261.63, 392.00, 523.25, 659.25, 783.99];

    frequencies.forEach((freq, index) => {
      const osc = ctx.createOscillator();
      const gain = ctx.createGain();

      // Sound Blaster 16 + PC Speaker mixture (Triangle + Square)
      osc.type = index % 2 === 0 ? 'triangle' : 'square';
      osc.frequency.setValueAtTime(freq, now + index * 0.08);

      // Envelope (Attack, Decay, Sustain, Release)
      gain.gain.setValueAtTime(0.001, now + index * 0.08);
      gain.gain.exponentialRampToValueAtTime(0.08, now + index * 0.08 + 0.04);
      gain.gain.exponentialRampToValueAtTime(0.0001, now + index * 0.08 + 0.9);

      osc.connect(gain);
      gain.connect(ctx.destination);

      osc.start(now + index * 0.08);
      osc.stop(now + index * 0.08 + 0.95);
    });
  }
}
