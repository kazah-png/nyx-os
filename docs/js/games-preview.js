/**
 * NyxOS — Five Games Arcade Canvas Interactive Previews
 * Animated micro-simulations for DOOM, Tetris, Snake, Pong, and Minesweeper
 */

document.addEventListener('DOMContentLoaded', () => {
  initArcadeGames();
});

function initArcadeGames() {
  initPongCanvas();
  initSnakeCanvas();
  initTetrisCanvas();
}

/* ==========================================================================
   Pong AI Animation Canvas
   ========================================================================== */
function initPongCanvas() {
  const canvas = document.getElementById('canvas-pong');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');

  let ballX = 120, ballY = 80;
  let vx = 2.5, vy = 1.8;
  let p1Y = 60, p2Y = 60;

  function loop() {
    ctx.fillStyle = '#05070a';
    ctx.fillRect(0, 0, canvas.width, canvas.height);

    // Center dotted line
    ctx.strokeStyle = '#1e293b';
    ctx.setLineDash([4, 4]);
    ctx.beginPath();
    ctx.moveTo(canvas.width / 2, 0);
    ctx.lineTo(canvas.width / 2, canvas.height);
    ctx.stroke();
    ctx.setLineDash([]);

    // Ball movement
    ballX += vx;
    ballY += vy;

    if (ballY <= 4 || ballY >= canvas.height - 4) vy = -vy;

    // AI Paddles
    p1Y += (ballY - (p1Y + 15)) * 0.12;
    p2Y += (ballY - (p2Y + 15)) * 0.12;

    if (ballX <= 14 && ballY >= p1Y && ballY <= p1Y + 30) vx = Math.abs(vx);
    if (ballX >= canvas.width - 14 && ballY >= p2Y && ballY <= p2Y + 30) vx = -Math.abs(vx);

    if (ballX < 0 || ballX > canvas.width) {
      ballX = canvas.width / 2;
      ballY = canvas.height / 2;
    }

    // Draw Paddles
    ctx.fillStyle = '#34d399';
    ctx.fillRect(6, p1Y, 5, 30);
    ctx.fillRect(canvas.width - 11, p2Y, 5, 30);

    // Draw Ball
    ctx.fillStyle = '#ffffff';
    ctx.fillRect(ballX - 3, ballY - 3, 6, 6);

    requestAnimationFrame(loop);
  }
  loop();
}

/* ==========================================================================
   Snake Micro-Animation Canvas
   ========================================================================== */
function initSnakeCanvas() {
  const canvas = document.getElementById('canvas-snake');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');

  let snake = [{x: 8, y: 7}, {x: 7, y: 7}, {x: 6, y: 7}];
  let food = {x: 12, y: 7};
  let dx = 1, dy = 0;
  let lastTick = 0;

  function tick(timestamp) {
    if (timestamp - lastTick > 120) {
      lastTick = timestamp;

      const head = { x: snake[0].x + dx, y: snake[0].y + dy };

      // Simple AI toward food
      if (head.x === food.x && head.y === food.y) {
        snake.unshift(head);
        food = {
          x: Math.floor(Math.random() * 18) + 1,
          y: Math.floor(Math.random() * 12) + 1
        };
      } else {
        snake.unshift(head);
        snake.pop();
      }

      if (head.x < 1 || head.x > 22) dx = -dx;
      if (head.y < 1 || head.y > 14) dy = -dy;

      // Draw Grid
      ctx.fillStyle = '#05070a';
      ctx.fillRect(0, 0, canvas.width, canvas.height);

      // Draw Food
      ctx.fillStyle = '#f87171';
      ctx.fillRect(food.x * 10, food.y * 10, 8, 8);

      // Draw Snake
      ctx.fillStyle = '#34d399';
      snake.forEach((seg, i) => {
        ctx.fillStyle = i === 0 ? '#10b981' : '#34d399';
        ctx.fillRect(seg.x * 10, seg.y * 10, 8, 8);
      });
    }
    requestAnimationFrame(tick);
  }
  requestAnimationFrame(tick);
}

/* ==========================================================================
   Tetris Micro-Animation Canvas
   ========================================================================== */
function initTetrisCanvas() {
  const canvas = document.getElementById('canvas-tetris');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');

  let pieceY = 10;
  let lastTime = 0;

  function render(time) {
    if (time - lastTime > 60) {
      lastTime = time;
      pieceY = (pieceY + 2) % (canvas.height - 24);

      ctx.fillStyle = '#05070a';
      ctx.fillRect(0, 0, canvas.width, canvas.height);

      // Stack at bottom
      ctx.fillStyle = '#3b82f6';
      ctx.fillRect(40, canvas.height - 18, 16, 16);
      ctx.fillRect(56, canvas.height - 18, 16, 16);
      ctx.fillRect(72, canvas.height - 18, 16, 16);
      ctx.fillRect(88, canvas.height - 18, 16, 16);

      // Falling T-Piece
      ctx.fillStyle = '#a855f7';
      ctx.fillRect(60, pieceY, 14, 14);
      ctx.fillRect(46, pieceY + 14, 14, 14);
      ctx.fillRect(60, pieceY + 14, 14, 14);
      ctx.fillRect(74, pieceY + 14, 14, 14);
    }
    requestAnimationFrame(render);
  }
  requestAnimationFrame(render);
}
