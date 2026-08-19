document.addEventListener('DOMContentLoaded', () => {
  initNetworkSimulator();
});

function initNetworkSimulator() {
  const layerBars = document.querySelectorAll('.layer-bar');
  const logContainer = document.getElementById('net-sim-log');
  const btnSyn = document.getElementById('btn-packet-syn');
  const btnDns = document.getElementById('btn-packet-dns');
  const btnPing = document.getElementById('btn-packet-ping');
  const btnHttp = document.getElementById('btn-packet-http');

  if (!logContainer) return;

  function appendLog(text, type = 'ok') {
    const entry = document.createElement('div');
    entry.className = `net-log-entry ${type}`;
    const time = new Date().toISOString().substring(11, 19);
    entry.innerHTML = `[${time}] ${text}`;
    logContainer.appendChild(entry);
    logContainer.scrollTop = logContainer.scrollHeight;
  }

  function triggerPacketWave(direction = 'down-up') {
    layerBars.forEach((bar, index) => {
      setTimeout(() => {
        bar.classList.add('transmitting', 'active');
        setTimeout(() => bar.classList.remove('transmitting', 'active'), 600);
      }, index * 90);
    });
  }

  if (btnSyn) {
    btnSyn.addEventListener('click', () => {
      triggerPacketWave();
      appendLog('TCP OUT: [SYN] Seq=14920384 Win=65535 Len=0 MSS=1460', 'syn');
      setTimeout(() => {
        appendLog('TCP IN: [SYN, ACK] Seq=9928172 Ack=14920385 Win=65535', 'syn');
        appendLog('TCP OUT: [ACK] Ack=9928173 -> ESTABLISHED (RTO=200ms)', 'ok');
      }, 400);
    });
  }

  if (btnDns) {
    btnDns.addEventListener('click', () => {
      triggerPacketWave();
      appendLog('UDP OUT: DNS Query A nyxos.cc to 10.0.2.3:53 (id 0x4f21)', 'ok');
      setTimeout(() => {
        appendLog('UDP IN: DNS Response nyxos.cc -> 185.199.108.153 (TTL=300)', 'ok');
      }, 350);
    });
  }

  if (btnPing) {
    btnPing.addEventListener('click', () => {
      triggerPacketWave();
      appendLog('ICMP OUT: Echo Request (ping) -> 127.0.0.1 seq=1 id=0x10a', 'ok');
      setTimeout(() => {
        appendLog('ICMP IN: Echo Reply <- 127.0.0.1 seq=1 ttl=64 time=0.078ms', 'ok');
      }, 300);
    });
  }

  if (btnHttp) {
    btnHttp.addEventListener('click', () => {
      triggerPacketWave();
      appendLog('HTTP OUT: GET / HTTP/1.1 (Host: nyxos.cc, User-Agent: Selene/1.0)', 'http');
      setTimeout(() => {
        appendLog('HTTP IN: HTTP/1.1 200 OK (Transfer-Encoding: chunked, gzip)', 'ok');
        appendLog('SELENE: HTML tokenized & rendered to text viewport.', 'ok');
      }, 450);
    });
  }
}
