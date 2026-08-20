document.addEventListener('DOMContentLoaded', () => {
  init3DScene();
});

function init3DScene() {
  const container = document.getElementById('webgl-canvas-container');
  if (!container) return;
  if (typeof THREE === 'undefined') return;   // degrade to the CSS ambient background, no console error
  if (window.innerWidth < 900) return;         // skip WebGL on phones/tablets (hidden via CSS anyway) — saves battery

  const scene = new THREE.Scene();
  scene.fog = new THREE.FogExp2(0x030305, 0.030);

  const camera = new THREE.PerspectiveCamera(45, window.innerWidth / window.innerHeight, 0.1, 1000);
  camera.position.set(0, 0, 10.5);

  const renderer = new THREE.WebGLRenderer({
    antialias: true,
    alpha: true,
    powerPreference: 'high-performance',
    precision: 'highp'
  });
  renderer.setSize(window.innerWidth, window.innerHeight);
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 1.5));
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.4;
  container.innerHTML = '';
  container.appendChild(renderer.domElement);

  const ambientLight = new THREE.AmbientLight(0x281045, 2.4);
  scene.add(ambientLight);

  const sunLight = new THREE.DirectionalLight(0xffffff, 3.0);
  sunLight.position.set(8, 11, 8.5);
  scene.add(sunLight);

  const purplePoint = new THREE.PointLight(0xa855f7, 6.5, 45);
  purplePoint.position.set(-5, -0.5, 5.5);
  scene.add(purplePoint);

  const lavenderRim = new THREE.PointLight(0xe879f9, 4.5, 32);
  lavenderRim.position.set(5.5, -2.5, 4.5);
  scene.add(lavenderRim);

  const deepFill = new THREE.DirectionalLight(0x7c3aed, 1.6);
  deepFill.position.set(-8, 5, -4);
  scene.add(deepFill);

  const worldGroup = new THREE.Group();
  scene.add(worldGroup);

  const starCount = 420;
  const starGeo = new THREE.BufferGeometry();
  const starPos = new Float32Array(starCount * 3);
  const starColors = new Float32Array(starCount * 3);

  const colorPalette = [
    new THREE.Color(0xf5d0fe),
    new THREE.Color(0xc084fc),
    new THREE.Color(0xa855f7),
    new THREE.Color(0xe879f9),
    new THREE.Color(0xffffff),
    new THREE.Color(0x7c3aed)
  ];

  for (let i = 0; i < starCount; i++) {
    starPos[i * 3] = (Math.random() - 0.5) * 38;
    starPos[i * 3 + 1] = (Math.random() - 0.5) * 38;
    starPos[i * 3 + 2] = (Math.random() - 0.5) * 24;

    const col = colorPalette[Math.floor(Math.random() * colorPalette.length)];
    starColors[i * 3] = col.r;
    starColors[i * 3 + 1] = col.g;
    starColors[i * 3 + 2] = col.b;
  }

  starGeo.setAttribute('position', new THREE.BufferAttribute(starPos, 3));
  starGeo.setAttribute('color', new THREE.BufferAttribute(starColors, 3));

  const starMat = new THREE.PointsMaterial({
    size: 0.16,
    vertexColors: true,
    transparent: true,
    opacity: 0.85,
    blending: THREE.AdditiveBlending
  });

  const starField = new THREE.Points(starGeo, starMat);
  worldGroup.add(starField);

  let moonObject = null;
  const cloudObjects = [];
  let ringPrimary = null;
  let ringSecondary = null;

  if (typeof THREE.GLTFLoader !== 'undefined') {
    const loader = new THREE.GLTFLoader();
    loader.load(
      'assets/moon.glb',
      (gltf) => {
        const model = gltf.scene;
        model.scale.set(2.4, 2.4, 2.4);
        model.position.set(2.6, 0.3, 0);

        model.traverse((child) => {
          if (child.isMesh && child.material) {
            child.material.roughness = 0.82;
            child.material.metalness = 0.08;
          }
        });

        const glowGeo = new THREE.SphereGeometry(2.52, 36, 36);
        const glowMat = new THREE.MeshBasicMaterial({
          color: 0xa855f7,
          transparent: true,
          opacity: 0.16,
          side: THREE.BackSide,
          blending: THREE.AdditiveBlending
        });
        const glowMesh = new THREE.Mesh(glowGeo, glowMat);
        model.add(glowMesh);

        worldGroup.add(model);
        moonObject = model;
        createSculptedPurpleClouds();
      },
      undefined,
      () => {
        createPhotorealisticMoon();
        createSculptedPurpleClouds();
      }
    );
  } else {
    createPhotorealisticMoon();
    createSculptedPurpleClouds();
  }

  function createPhotorealisticMoon() {
    const moonGeo = new THREE.SphereGeometry(1.95, 72, 72);

    const moonCanvas = document.createElement('canvas');
    moonCanvas.width = 2048;
    moonCanvas.height = 2048;
    const ctx = moonCanvas.getContext('2d');

    ctx.fillStyle = '#b8c0cc';
    ctx.fillRect(0, 0, 2048, 2048);

    const maria = [
      { x: 680, y: 800, rx: 340, ry: 240, rot: 0.2 },
      { x: 1180, y: 640, rx: 400, ry: 280, rot: -0.15 },
      { x: 880, y: 1280, rx: 300, ry: 200, rot: 0.4 },
      { x: 1500, y: 1060, rx: 260, ry: 220, rot: 0.1 },
      { x: 440, y: 1100, rx: 200, ry: 160, rot: -0.3 },
      { x: 1680, y: 720, rx: 180, ry: 150, rot: 0.3 }
    ];

    maria.forEach(m => {
      ctx.save();
      ctx.translate(m.x, m.y);
      ctx.rotate(m.rot);
      const grad = ctx.createRadialGradient(0, 0, 20, 0, 0, m.rx);
      grad.addColorStop(0, '#353a47');
      grad.addColorStop(0.65, '#4a5263');
      grad.addColorStop(0.88, '#7b8596');
      grad.addColorStop(1, 'transparent');
      ctx.fillStyle = grad;
      ctx.beginPath();
      ctx.ellipse(0, 0, m.rx, m.ry, 0, 0, Math.PI * 2);
      ctx.fill();
      ctx.restore();
    });

    for (let i = 0; i < 90; i++) {
      const cx = Math.random() * 2048;
      const cy = Math.random() * 2048;
      const r = Math.random() * 45 + 8;

      if (r > 28) {
        ctx.strokeStyle = 'rgba(255, 255, 255, 0.32)';
        ctx.lineWidth = 1.6;
        for (let ray = 0; ray < 14; ray++) {
          const angle = (ray / 14) * Math.PI * 2 + (Math.random() - 0.5) * 0.25;
          const rayLen = r * (2.8 + Math.random() * 3.6);
          ctx.beginPath();
          ctx.moveTo(cx, cy);
          ctx.lineTo(cx + Math.cos(angle) * rayLen, cy + Math.sin(angle) * rayLen);
          ctx.stroke();
        }
      }

      const cGrad = ctx.createRadialGradient(cx, cy, r * 0.15, cx, cy, r);
      cGrad.addColorStop(0, '#222630');
      cGrad.addColorStop(0.65, '#3b4150');
      cGrad.addColorStop(0.85, '#ffffff');
      cGrad.addColorStop(1, '#828c9e');
      ctx.fillStyle = cGrad;
      ctx.beginPath();
      ctx.arc(cx, cy, r, 0, Math.PI * 2);
      ctx.fill();
    }

    const moonTexture = new THREE.CanvasTexture(moonCanvas);
    moonTexture.wrapS = THREE.RepeatWrapping;
    moonTexture.wrapT = THREE.RepeatWrapping;

    const bumpCanvas = document.createElement('canvas');
    bumpCanvas.width = 1024;
    bumpCanvas.height = 1024;
    const bCtx = bumpCanvas.getContext('2d');
    bCtx.fillStyle = '#808080';
    bCtx.fillRect(0, 0, 1024, 1024);

    for (let i = 0; i < 70; i++) {
      const bx = Math.random() * 1024;
      const by = Math.random() * 1024;
      const br = Math.random() * 40 + 8;
      const bGrad = bCtx.createRadialGradient(bx, by, br * 0.1, bx, by, br);
      bGrad.addColorStop(0, '#1a1a1a');
      bGrad.addColorStop(0.7, '#0a0a0a');
      bGrad.addColorStop(0.85, '#ffffff');
      bGrad.addColorStop(1, '#808080');
      bCtx.fillStyle = bGrad;
      bCtx.beginPath();
      bCtx.arc(bx, by, br, 0, Math.PI * 2);
      bCtx.fill();
    }

    const bumpTexture = new THREE.CanvasTexture(bumpCanvas);
    bumpTexture.wrapS = THREE.RepeatWrapping;
    bumpTexture.wrapT = THREE.RepeatWrapping;

    const moonMat = new THREE.MeshStandardMaterial({
      map: moonTexture,
      bumpMap: bumpTexture,
      bumpScale: 0.065,
      roughness: 0.80,
      metalness: 0.10
    });

    moonObject = new THREE.Mesh(moonGeo, moonMat);
    moonObject.position.set(2.6, 0.3, 0);

    const glowGeo = new THREE.SphereGeometry(2.06, 40, 40);
    const glowMat = new THREE.MeshBasicMaterial({
      color: 0xa855f7,
      transparent: true,
      opacity: 0.18,
      side: THREE.BackSide,
      blending: THREE.AdditiveBlending
    });
    const glowMesh = new THREE.Mesh(glowGeo, glowMat);
    moonObject.add(glowMesh);

    const haloGeo = new THREE.SphereGeometry(2.22, 40, 40);
    const haloMat = new THREE.MeshBasicMaterial({
      color: 0xc084fc,
      transparent: true,
      opacity: 0.10,
      side: THREE.BackSide,
      blending: THREE.AdditiveBlending
    });
    const haloMesh = new THREE.Mesh(haloGeo, haloMat);
    moonObject.add(haloMesh);

    const ringGeo = new THREE.RingGeometry(2.45, 2.58, 72);
    const ringMat = new THREE.MeshBasicMaterial({
      color: 0xc084fc,
      side: THREE.DoubleSide,
      transparent: true,
      opacity: 0.28,
      blending: THREE.AdditiveBlending
    });
    ringPrimary = new THREE.Mesh(ringGeo, ringMat);
    ringPrimary.rotation.x = Math.PI / 2.3;
    ringPrimary.rotation.y = Math.PI / 6;
    moonObject.add(ringPrimary);

    const ringGeo2 = new THREE.RingGeometry(2.72, 2.78, 72);
    const ringMat2 = new THREE.MeshBasicMaterial({
      color: 0xe879f9,
      side: THREE.DoubleSide,
      transparent: true,
      opacity: 0.16,
      blending: THREE.AdditiveBlending
    });
    ringSecondary = new THREE.Mesh(ringGeo2, ringMat2);
    ringSecondary.rotation.x = Math.PI / 2.8;
    ringSecondary.rotation.y = -Math.PI / 8;
    moonObject.add(ringSecondary);

    worldGroup.add(moonObject);
  }

  function createSculptedPurpleClouds() {
    const sharedSphereGeo = new THREE.SphereGeometry(1, 18, 18);

    function buildFastCloudCluster(x, y, z, scale, colorHex, opacity = 0.92, driftMult = 1.0) {
      const cluster = new THREE.Group();

      const cloudMat = new THREE.MeshStandardMaterial({
        color: colorHex,
        roughness: 0.78,
        metalness: 0.05,
        transparent: true,
        opacity: opacity,
        flatShading: false
      });

      const puffOffsets = [
        { x: 0, y: 0, z: 0, r: 0.65 },
        { x: 0.48, y: -0.06, z: 0.14, r: 0.48 },
        { x: -0.48, y: -0.08, z: 0.12, r: 0.46 },
        { x: 0.24, y: 0.28, z: -0.08, r: 0.40 },
        { x: -0.22, y: 0.26, z: 0.12, r: 0.38 },
        { x: 0.72, y: -0.14, z: 0.20, r: 0.34 },
        { x: -0.72, y: -0.16, z: 0.18, r: 0.32 },
        { x: 0, y: 0.35, z: 0.05, r: 0.32 }
      ];

      puffOffsets.forEach(p => {
        const puff = new THREE.Mesh(sharedSphereGeo, cloudMat);
        puff.position.set(p.x, p.y, p.z);
        puff.scale.set(p.r, p.r, p.r);
        cluster.add(puff);
      });

      cluster.position.set(x, y, z);
      cluster.scale.set(scale, scale, scale);
      worldGroup.add(cluster);

      cloudObjects.push({
        group: cluster,
        baseX: x,
        baseY: y,
        baseZ: z,
        driftSpeed: (0.16 + Math.random() * 0.18) * driftMult,
        rotSpeed: (Math.random() - 0.5) * 0.05
      });
    }

    buildFastCloudCluster(1.4, 0.9, 2.2, 1.40, 0xc084fc, 0.96, 1.2);
    buildFastCloudCluster(2.5, -1.3, 2.3, 1.50, 0xd8b4fe, 0.96, 1.1);
    buildFastCloudCluster(0.4, -0.4, 2.9, 1.05, 0xe9d5ff, 0.90, 1.3);

    buildFastCloudCluster(3.9, -0.5, 1.4, 1.35, 0xa855f7, 0.92, 0.9);
    buildFastCloudCluster(4.6, 1.2, 0.7, 1.20, 0x9333ea, 0.88, 0.85);
    buildFastCloudCluster(1.8, 2.2, 0.3, 1.30, 0x7c3aed, 0.84, 0.95);

    buildFastCloudCluster(-1.9, 0.8, 1.5, 1.15, 0xa855f7, 0.80, 0.7);
    buildFastCloudCluster(-3.2, -1.2, 0.4, 1.40, 0x581c87, 0.75, 0.6);
    buildFastCloudCluster(-0.8, -2.2, 1.8, 1.25, 0x6b21a8, 0.78, 0.8);
  }

  window.addEventListener('pointerdown', () => {
    purplePoint.intensity = 11.0;
    lavenderRim.intensity = 7.5;
    setTimeout(() => {
      purplePoint.intensity = 6.5;
      lavenderRim.intensity = 4.5;
    }, 350);
  });

  let mouseX = 0;
  let mouseY = 0;
  let targetX = 0;
  let targetY = 0;
  let scrollProgress = 0;

  window.addEventListener('mousemove', (e) => {
    mouseX = (e.clientX / window.innerWidth - 0.5) * 2;
    mouseY = (e.clientY / window.innerHeight - 0.5) * 2;
  }, { passive: true });

  window.addEventListener('scroll', () => {
    const maxScroll = document.documentElement.scrollHeight - window.innerHeight;
    scrollProgress = maxScroll > 0 ? window.scrollY / maxScroll : 0;
  }, { passive: true });

  window.addEventListener('resize', () => {
    camera.aspect = window.innerWidth / window.innerHeight;
    camera.updateProjectionMatrix();
    renderer.setSize(window.innerWidth, window.innerHeight);
  });

  const clock = new THREE.Clock();

  function renderFrame() {
    requestAnimationFrame(renderFrame);
    const delta = clock.getDelta();
    const elapsedTime = clock.getElapsedTime();

    targetX += (mouseX - targetX) * 0.055;
    targetY += (mouseY - targetY) * 0.055;

    const isMobile = window.innerWidth < 900;
    const baseMoonX = isMobile ? 0 : 2.6;
    const baseMoonY = isMobile ? 1.4 : 0.3;

    if (moonObject) {
      moonObject.rotation.y = elapsedTime * 0.08 + scrollProgress * 4.2;
      moonObject.rotation.x = targetY * 0.14 + scrollProgress * 0.40;

      moonObject.position.x = baseMoonX + targetX * 0.35 - scrollProgress * 2.2;
      moonObject.position.y = baseMoonY + Math.sin(elapsedTime * 0.65) * 0.09 - scrollProgress * 2.6;
      moonObject.position.z = -scrollProgress * 1.5;

      if (ringPrimary) ringPrimary.rotation.z = elapsedTime * 0.05;
      if (ringSecondary) ringSecondary.rotation.z = -elapsedTime * 0.035;
    }

    for (let i = 0; i < cloudObjects.length; i++) {
      const c = cloudObjects[i];
      c.group.position.x = c.baseX + Math.sin(elapsedTime * c.driftSpeed + i * 1.4) * 0.20 + targetX * 0.38;
      c.group.position.y = c.baseY + Math.cos(elapsedTime * c.driftSpeed + i * 1.1) * 0.14 - scrollProgress * 3.4;
      c.group.rotation.y += c.rotSpeed * delta;
    }

    starField.rotation.y = elapsedTime * 0.020 + scrollProgress * 0.40;
    starField.position.y = -scrollProgress * 3.8;

    worldGroup.rotation.y = targetX * 0.065;
    worldGroup.rotation.x = -targetY * 0.065;

    renderer.render(scene, camera);
  }

  renderFrame();
}
