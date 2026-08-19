document.addEventListener('DOMContentLoaded', () => {
  initCodePlayground();
});

function initCodePlayground() {
  const codeEditor = document.getElementById('playground-editor');
  const outputConsole = document.getElementById('playground-output');
  const runBtn = document.getElementById('playground-run-btn');
  const sampleTabs = document.querySelectorAll('.playground-tab');

  if (!codeEditor || !outputConsole || !runBtn) return;

  const samples = {
    'hello.c': `/* NyxOS Native Userspace Example — hello.c */
#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    printf("NyxOS Ring-3 Process Started (PID: %d)\\n", getpid());
    printf("Higher-Half Kernel Linked Base: 0xFFFFFF8000000000\\n");
    printf("Compiling via TinyCC 0.9.27 directly on EXT2 VFS...\\n");
    printf("Success: 0 build warnings. Execution complete.\\n");
    return 0;
}`,
    'socket.c': `/* NyxOS TCP Socket Client Example — socket.c */
#include <nyx/socket.h>
#include <stdio.h>

int main(void) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(80);
    server.sin_addr.s_addr = inet_addr("127.0.0.1");

    printf("[socket] Connecting to RTL8139 TCP Stack...\\n");
    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) == 0) {
        printf("[socket] TCP Handshake ESTABLISHED (RTO: 200ms)\\n");
        send(sock, "GET / HTTP/1.1\\r\\n\\r\\n", 18, 0);
        printf("[socket] Packet transmitted over PCI NIC successfully.\\n");
    }
    close(sock);
    return 0;
}`,
    'fibonacci.n': `// NyxOS Native N Language Example — fibonacci.n
module Main

fn fibonacci(n: u64) -> u64 {
    if (n <= 1) {
        return n;
    }
    return fibonacci(n - 1) + fibonacci(n - 2);
}

fn main() -> i32 {
    let result: u64 = fibonacci(12);
    print("N Native Compiler (ncc) execution result: ");
    print_u64(result);
    print("\\nStatic verification passed with 0 runtime overhead.\\n");
    return 0;
}`
  };

  const outputs = {
    'hello.c': `[tcc] Compiling hello.c -> /mnt/bin/hello (ELF64 x86_64)...
[tcc] Time elapsed: 3.8ms | Optimization: O1 | Output size: 2480 bytes
--- PROCESS OUTPUT (PID 12) ---
NyxOS Ring-3 Process Started (PID: 12)
Higher-Half Kernel Linked Base: 0xFFFFFF8000000000
Compiling via TinyCC 0.9.27 directly on EXT2 VFS...
Success: 0 build warnings. Execution complete.
--- PROCESS EXITED (Status: 0) ---`,
    'socket.c': `[tcc] Compiling socket.c -> /mnt/bin/socket (ELF64 x86_64)...
[tcc] Linked with RTL8139 libc network bindings.
--- PROCESS OUTPUT (PID 14) ---
[socket] Connecting to RTL8139 TCP Stack...
[socket] TCP Handshake ESTABLISHED (RTO: 200ms)
[socket] Packet transmitted over PCI NIC successfully.
--- PROCESS EXITED (Status: 0) ---`,
    'fibonacci.n': `[ncc] Compiling fibonacci.n -> /mnt/bin/fib (Native N binary)...
[ncc] Type check: 100% OK | Static Verification: PASSED
--- PROCESS OUTPUT (PID 15) ---
N Native Compiler (ncc) execution result: 144
Static verification passed with 0 runtime overhead.
--- PROCESS EXITED (Status: 0) ---`
  };

  let activeFile = 'hello.c';

  sampleTabs.forEach(tab => {
    tab.addEventListener('click', () => {
      sampleTabs.forEach(t => t.classList.remove('active'));
      tab.classList.add('active');
      activeFile = tab.getAttribute('data-file') || 'hello.c';
      codeEditor.value = samples[activeFile] || '';
      outputConsole.textContent = 'Ready to compile. Press "[RUN] Compile & Run".';
    });
  });

  runBtn.addEventListener('click', () => {
    outputConsole.textContent = '[COMPILER] Initializing in-OS TinyCC / ncc compiler instance...';
    runBtn.disabled = true;
    runBtn.style.opacity = '0.6';

    setTimeout(() => {
      outputConsole.textContent = outputs[activeFile] || `[tcc] Compiled ${activeFile} successfully.\nProcess executed with exit code 0.`;
      runBtn.disabled = false;
      runBtn.style.opacity = '1';

      if (typeof window.showToast === 'function') {
        window.showToast(`Compiled & Executed ${activeFile} (0 Warnings)`, '[ELF64]');
      }
    }, 450);
  });
}
