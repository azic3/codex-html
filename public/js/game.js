(function () {
  var canvas = document.getElementById("sword-game");
  var ctx = canvas.getContext("2d");
  var scoreEl = document.getElementById("score");
  var energyEl = document.getElementById("energy");
  var bestEl = document.getElementById("best-score");
  var startPanel = document.getElementById("start-panel");
  var gameOverPanel = document.getElementById("game-over-panel");
  var finalScore = document.getElementById("final-score");
  var resultText = document.getElementById("result-text");
  var startButton = document.getElementById("start-button");
  var restartButton = document.getElementById("restart-button");
  var pauseButton = document.getElementById("pause-button");
  var pausePanel = document.getElementById("pause-panel");
  var resumeButton = document.getElementById("resume-button");

  var width = canvas.width;
  var height = canvas.height;
  var running = false;
  var paused = false;
  var holding = false;
  var lastTime = 0;
  var spawnTimer = 0;
  var orbTimer = 0;
  var distance = 0;
  var score = 0;
  var bonusScore = 0;
  var energy = 0;
  var speed = 360;
  var best = Number(localStorage.getItem("xiaochen_sword_best") || 0);
  var obstacles = [];
  var orbs = [];
  var sparks = [];
  var cranes = [];
  var burstCraneSpawned = false;
  var craneSpawnTimer = 0;
  var swordTrail = [];

  var player = {
    x: 220,
    y: 330,
    vy: 0,
    radius: 24,
    tilt: 0
  };

  bestEl.textContent = best;

  function fitCanvas() {
    var rect = canvas.getBoundingClientRect();
    var ratio = window.devicePixelRatio || 1;
    canvas.width = Math.max(320, Math.floor(rect.width * ratio));
    canvas.height = Math.max(320, Math.floor(rect.height * ratio));
    ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
    width = rect.width;
    height = rect.height;
  }

  function rand(min, max) {
    return min + Math.random() * (max - min);
  }

  function resetGame() {
    player.y = height * 0.48;
    player.vy = 0;
    player.tilt = 0;
    obstacles = [];
    orbs = [];
    sparks = [];
    cranes = [];
    burstCraneSpawned = false;
    craneSpawnTimer = 0;
    swordTrail = [];
    spawnTimer = 0.8;
    orbTimer = 0.4;
    distance = 0;
    score = 0;
    bonusScore = 0;
    energy = 0;
    speed = Math.max(300, width * 0.32);
    scoreEl.textContent = "0";
    energyEl.textContent = "0";
  }

  function startGame() {
    fitCanvas();
    resetGame();
    running = true;
    paused = false;
    lastTime = performance.now();
    startPanel.hidden = true;
    gameOverPanel.hidden = true;
    pausePanel.hidden = true;
    pauseButton.classList.remove("is-paused");
    requestAnimationFrame(loop);
  }

  function endGame() {
    running = false;
    paused = false;
    pausePanel.hidden = true;
    pauseButton.classList.remove("is-paused");
    best = Math.max(best, score);
    localStorage.setItem("xiaochen_sword_best", String(best));
    bestEl.textContent = best;
    finalScore.textContent = score + " 分";
    resultText.textContent = energy >= 12 ? "灵气很足，下一局可以挑战更远距离。" : "保持在云海中线附近，遇到山峰提前抬升。";
    gameOverPanel.hidden = false;
  }

  function togglePause(forceState) {
    if (!running) {
      return;
    }
    paused = typeof forceState === "boolean" ? forceState : !paused;
    pausePanel.hidden = !paused;
    pauseButton.classList.toggle("is-paused", paused);
    if (!paused) {
      lastTime = performance.now();
      requestAnimationFrame(loop);
    }
  }

  function spawnObstacle() {
    var kindRoll = Math.random();
    var obstacle;

    if (kindRoll < 0.38) {
      var mountainHeight = rand(height * 0.22, height * 0.48);
      obstacle = {
        type: "mountain",
        x: width + 80,
        y: height - mountainHeight,
        w: rand(110, 190),
        h: mountainHeight,
        tree: Math.random() > 0.42,
        passed: false
      };
    } else if (kindRoll < 0.78) {
      obstacle = {
        type: "bird",
        x: width + 70,
        y: rand(height * 0.22, height * 0.62),
        w: 108,
        h: 58,
        phase: rand(0, Math.PI * 2),
        passed: false
      };
    } else {
      obstacle = {
        type: "rock",
        x: width + 70,
        y: rand(height * 0.18, height * 0.5),
        w: 44,
        h: 54,
        spin: rand(0, Math.PI * 2),
        passed: false
      };
    }

    obstacles.push(obstacle);
  }

  function spawnOrb() {
    var upperY = height * 0.24 + rand(-12, 12);
    var lowerY = height * 0.52 + rand(-12, 12);
    var row = Math.random() < 0.5 ? upperY : lowerY;
    orbs.push({
      x: width + 48,
      y: row,
      r: 14,
      taken: false,
      pulse: rand(0, Math.PI * 2)
    });
  }

  function spawnCrane(depth) {
    var poses = ["glide", "fly", "glide", "fly", "front"];
    var pose = poses[Math.floor(Math.random() * poses.length)];
    var baseSize;
    if (depth === "far") {
      baseSize = rand(0.55, 0.8);
    } else if (depth === "near") {
      baseSize = rand(1.6, 2.2);
    } else if (depth === "foreground") {
      baseSize = rand(2.8, 3.8);
      pose = "front";
    } else {
      baseSize = rand(0.9, 1.4);
    }
    cranes.push({
      x: width + rand(60, 280),
      y: rand(height * 0.14, height * 0.55),
      size: baseSize,
      phase: rand(0, Math.PI * 2),
      pose: pose,
      depth: depth,
      alpha: depth === "far" ? rand(0.3, 0.5) : depth === "foreground" ? 0.85 : rand(0.55, 0.8),
      speedMul: depth === "far" ? 0.55 : depth === "near" ? 0.95 : 0.75
    });
  }

  function update(dt) {
    distance += speed * dt;
    speed += dt * 7;
    score = Math.floor(distance / 18) + energy * 25 + bonusScore;
    scoreEl.textContent = score;
    energyEl.textContent = energy;

    var lift = holding ? -1180 : 820;
    player.vy += lift * dt;
    player.vy *= 0.988;
    player.y += player.vy * dt;
    player.tilt = Math.max(-0.45, Math.min(0.55, player.vy / 700));

    if (player.y < 112) {
      player.y = 112;
      player.vy = 80;
    }
    if (player.y > height - 64) {
      endGame();
      return;
    }

    spawnTimer -= dt;
    orbTimer -= dt;
    if (spawnTimer <= 0) {
      spawnObstacle();
      spawnTimer = rand(0.85, 1.4) * Math.max(0.66, 360 / speed);
    }
    if (orbTimer <= 0) {
      spawnOrb();
      orbTimer = rand(0.75, 1.25);
    }

    obstacles.forEach(function (item) {
      item.x -= speed * dt;
      if (item.type === "bird") {
        item.phase += dt * 7;
        item.y += Math.sin(item.phase) * 0.45;
      }
      if (item.type === "rock") {
        item.spin += dt * 3;
        item.y += dt * 34;
      }
      if (!item.passed && item.x + item.w < player.x) {
        item.passed = true;
        bonusScore += 10;
      }
    });

    orbs.forEach(function (orb) {
      orb.x -= speed * dt;
      orb.pulse += dt * 5;
    });

    sparks.forEach(function (spark) {
      spark.life -= dt;
      spark.x += spark.vx * dt;
      spark.y += spark.vy * dt;
    });

    craneSpawnTimer -= dt;
    if (craneSpawnTimer <= 0) {
      var depthRoll = Math.random();
      if (depthRoll < 0.5) {
        spawnCrane("mid");
      } else if (depthRoll < 0.78) {
        spawnCrane("far");
      } else {
        spawnCrane("near");
      }
      craneSpawnTimer = rand(2.5, 5.5);
    }

    if (!burstCraneSpawned && distance > 3200) {
      spawnCrane("foreground");
      burstCraneSpawned = true;
    }

    cranes.forEach(function (crane) {
      crane.x -= speed * crane.speedMul * dt;
      crane.phase += dt * 4;
    });

    if (running && Math.random() < 0.6) {
      swordTrail.push({
        x: player.x - 80,
        y: player.y + rand(-8, 8),
        life: rand(0.4, 0.9),
        size: rand(1.5, 4)
      });
    }
    swordTrail.forEach(function (t) {
      t.life -= dt;
      t.x -= speed * dt * 0.6;
    });

    obstacles = obstacles.filter(function (item) {
      return item.x > -180;
    });
    orbs = orbs.filter(function (orb) {
      return orb.x > -60 && !orb.taken;
    });
    sparks = sparks.filter(function (spark) {
      return spark.life > 0;
    });
    cranes = cranes.filter(function (crane) {
      return crane.x > -350;
    });
    swordTrail = swordTrail.filter(function (t) {
      return t.life > 0;
    });

    checkCollisions();
  }

  function circleRectHit(cx, cy, cr, rect) {
    var nearestX = Math.max(rect.x, Math.min(cx, rect.x + rect.w));
    var nearestY = Math.max(rect.y, Math.min(cy, rect.y + rect.h));
    var dx = cx - nearestX;
    var dy = cy - nearestY;
    return dx * dx + dy * dy < cr * cr;
  }

  function checkCollisions() {
    var hitRadius = player.radius * 0.82;

    for (var i = 0; i < obstacles.length; i += 1) {
      var item = obstacles[i];
      var rect;
      if (item.type === "mountain") {
        rect = { x: item.x + item.w * 0.18, y: item.y + 18, w: item.w * 0.64, h: item.h };
      } else {
        rect = { x: item.x + 8, y: item.y + 6, w: item.w - 16, h: item.h - 12 };
      }
      if (circleRectHit(player.x, player.y, hitRadius, rect)) {
        endGame();
        return;
      }
    }

    orbs.forEach(function (orb) {
      var dx = player.x - orb.x;
      var dy = player.y - orb.y;
      if (dx * dx + dy * dy < Math.pow(player.radius + orb.r, 2)) {
        orb.taken = true;
        energy += 1;
        for (var i = 0; i < 12; i += 1) {
          sparks.push({
            x: orb.x,
            y: orb.y,
            vx: rand(-110, 110),
            vy: rand(-110, 110),
            life: rand(0.25, 0.6),
            size: rand(2, 5)
          });
        }
      }
    });
  }

  function drawBackground(time) {
    var sky = ctx.createLinearGradient(0, 0, 0, height);
    sky.addColorStop(0, "#d4e0da");
    sky.addColorStop(0.38, "#e5e5db");
    sky.addColorStop(0.72, "#eee8d8");
    sky.addColorStop(1, "#e0d8c4");
    ctx.fillStyle = sky;
    ctx.fillRect(0, 0, width, height);

    ctx.save();
    ctx.globalAlpha = 0.028;
    for (var g = 0; g < 18; g += 1) {
      ctx.fillStyle = g % 3 === 0 ? "#d4cbb8" : "#e8e0d2";
      ctx.fillRect(rand(0, width), rand(0, height), rand(18, 62), rand(14, 38));
    }
    ctx.restore();

    drawMoon(time);

    drawMountainLayer(time * 0.008, height * 0.56, 0.2, "#8baaa5", 230);
    drawMountainLayer(time * 0.018, height * 0.7, 0.36, "#6b8f88", 180);
    drawCloudLayer(time * 0.012, height * 0.26, 0.45, 80, 22);
    drawFloatingIsland(width * 0.55 - (time * 0.018 % (width + 320)), height * 0.22, 110, 0.62, time);
    drawFloatingIsland(width * 0.93 - (time * 0.03 % (width + 380)), height * 0.34, 145, 0.78, time);
    drawCloudLayer(time * 0.032, height * 0.42, 0.62, 104, 32);
    drawMountainLayer(time * 0.042, height * 0.9, 0.52, "#4a7069", 150);
    drawCloudLayer(time * 0.055, height * 0.78, 0.4, 140, 46);
  }

  function drawMoon(time) {
    var mx = width * 0.17;
    var my = height * 0.16;
    var mr = Math.max(62, width * 0.058);

    ctx.save();
    ctx.shadowColor = "rgba(245, 235, 200, 0.45)";
    ctx.shadowBlur = 40;

    ctx.globalAlpha = 0.85;
    ctx.fillStyle = "#f7f0d5";
    ctx.beginPath();
    ctx.arc(mx, my, mr, 0, Math.PI * 2);
    ctx.fill();

    ctx.shadowBlur = 0;
    ctx.globalAlpha = 0.12;
    var marePositions = [
      [0.22, -0.15, 0.32], [-0.18, 0.08, 0.25], [0.08, 0.22, 0.22],
      [-0.28, -0.2, 0.18], [0.3, 0.1, 0.15], [-0.1, -0.28, 0.2],
      [0.15, -0.25, 0.14], [-0.22, 0.2, 0.16], [0.05, -0.05, 0.28],
      [-0.3, -0.05, 0.13], [0.2, 0.2, 0.12], [-0.15, -0.12, 0.13],
      [0.28, -0.2, 0.11], [-0.05, 0.15, 0.15]
    ];
    ctx.fillStyle = "#d4c9a0";
    for (var i = 0; i < marePositions.length; i += 1) {
      ctx.beginPath();
      ctx.arc(mx + marePositions[i][0] * mr, my + marePositions[i][1] * mr, mr * marePositions[i][2], 0, Math.PI * 2);
      ctx.fill();
    }

    ctx.globalAlpha = 0.06;
    ctx.fillStyle = "#c4b88a";
    ctx.beginPath();
    ctx.arc(mx + mr * 0.1, my + mr * 0.05, mr * 0.35, 0, Math.PI * 2);
    ctx.fill();

    ctx.restore();
  }

  function drawMountainLayer(offset, baseY, scale, color, step) {
    ctx.save();
    ctx.globalAlpha = scale;

    var grad = ctx.createLinearGradient(0, baseY - 160, 0, height);
    grad.addColorStop(0, "#8baaa0");
    grad.addColorStop(0.5, color);
    grad.addColorStop(1, "#2d4540");
    ctx.fillStyle = grad;
    ctx.beginPath();
    ctx.moveTo(0, height);
    for (var x = -step; x <= width + step; x += step) {
      var noiseVal = randLike(x, offset);
      var peakX = x - (offset % step);
      var peakY = baseY - noiseVal * height * 0.22 - 68;
      var halfStep = step * 0.48;
      var cpX = peakX + halfStep * 0.6;
      var cpY = peakY + randLike(x + step * 0.3, offset) * 42;
      ctx.quadraticCurveTo(cpX, cpY, peakX + halfStep, peakY);
      ctx.quadraticCurveTo(peakX + halfStep + step * 0.08, peakY + randLike(x + step * 0.6, offset) * 36, peakX + step, height);
    }
    ctx.closePath();
    ctx.fill();

    ctx.globalAlpha = scale * 0.28;
    ctx.strokeStyle = "#2a4540";
    ctx.lineWidth = 1.5;
    for (var i = -step; i <= width + step; i += step) {
      var px = i - (offset % step) + step * 0.48;
      var nv = randLike(i, offset);
      var py = baseY - nv * height * 0.22 - 68;
      for (var s = 0; s < 5; s += 1) {
        var sx = px + (s - 2) * step * 0.16;
        var sy = py + s * 18;
        ctx.globalAlpha = scale * (0.14 + s * 0.04);
        ctx.lineWidth = 1 + (s % 3) * 0.8;
        ctx.beginPath();
        ctx.moveTo(sx, sy);
        ctx.lineTo(sx - 6 + s, sy + 14 + (s % 2) * 8);
        ctx.stroke();
      }
    }

    ctx.globalAlpha = scale * 0.32;
    ctx.fillStyle = "#1a3530";
    for (var j = -step; j <= width + step; j += step * 0.7) {
      var mx = j - (offset % step) + step * 0.5 + Math.sin(j * 0.04) * 28;
      var mnv = randLike(j, offset + 100);
      var my = baseY - mnv * height * 0.22 - 72;
      ctx.beginPath();
      ctx.arc(mx, my - 4, 2 + Math.random() * 2.5, 0, Math.PI * 2);
      ctx.fill();
    }

    var mistGrad = ctx.createLinearGradient(0, baseY - 40, 0, baseY + 60);
    mistGrad.addColorStop(0, "rgba(240, 236, 225, 0)");
    mistGrad.addColorStop(0.55, "rgba(240, 236, 225, 0.2)");
    mistGrad.addColorStop(1, "rgba(240, 236, 225, 0)");
    ctx.globalAlpha = 0.55;
    ctx.fillStyle = mistGrad;
    ctx.fillRect(0, baseY - 40, width, 100);

    ctx.restore();
  }

  function randLike(x, offset) {
    return 0.55 + Math.abs(Math.sin(x * 0.021 + offset * 0.004)) * 0.9;
  }

  function drawCloudLayer(offset, y, alpha, gap, verticalScale) {
    ctx.save();
    ctx.globalAlpha = alpha;
    ctx.shadowColor = "rgba(180, 175, 160, 0.35)";
    ctx.shadowBlur = 18;

    var vs = verticalScale || 28;
    for (var x = -gap * 2; x < width + gap * 2; x += gap * 1.8) {
      var px = x - (offset % (gap * 1.8));
      ctx.fillStyle = "rgba(250, 247, 238, 0.58)";
      ctx.beginPath();
      ctx.ellipse(px, y, gap * 0.72, vs, 0, 0, Math.PI * 2);
      ctx.fill();
      ctx.fillStyle = "rgba(248, 244, 232, 0.45)";
      ctx.beginPath();
      ctx.ellipse(px + gap * 0.42, y + 10, gap * 0.56, vs * 0.78, 0, 0, Math.PI * 2);
      ctx.fill();
      ctx.fillStyle = "rgba(255, 252, 245, 0.38)";
      ctx.beginPath();
      ctx.ellipse(px - gap * 0.44, y + 12, gap * 0.44, vs * 0.68, 0, 0, Math.PI * 2);
      ctx.fill();

      ctx.globalAlpha = alpha * 0.35;
      ctx.strokeStyle = "rgba(195, 190, 175, 0.28)";
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(px - gap * 0.6, y + vs * 0.28);
      ctx.quadraticCurveTo(px, y - vs * 0.15, px + gap * 0.55, y + vs * 0.2);
      ctx.stroke();
      ctx.globalAlpha = alpha;
    }

    ctx.shadowBlur = 0;
    ctx.restore();
  }

  function drawPineTree(x, y, size, lean) {
    ctx.save();
    ctx.translate(x, y);
    ctx.rotate(lean || -0.22);
    ctx.strokeStyle = "#3a3020";
    ctx.lineWidth = Math.max(1.5, size * 0.28);
    ctx.lineCap = "round";
    ctx.beginPath();
    ctx.moveTo(0, 0);
    ctx.lineTo(size * 0.18, -size * 0.72);
    ctx.stroke();

    ctx.lineWidth = Math.max(1, size * 0.14);
    ctx.beginPath();
    ctx.moveTo(size * 0.15, -size * 0.55);
    ctx.quadraticCurveTo(size * 0.55, -size * 0.72, size * 0.72, -size * 0.5);
    ctx.stroke();
    ctx.beginPath();
    ctx.moveTo(size * 0.12, -size * 0.4);
    ctx.quadraticCurveTo(-size * 0.35, -size * 0.58, -size * 0.55, -size * 0.38);
    ctx.stroke();

    ctx.fillStyle = "#2d4a38";
    ctx.beginPath();
    ctx.ellipse(size * 0.58, -size * 0.62, size * 0.36, size * 0.14, -0.35, 0, Math.PI * 2);
    ctx.fill();
    ctx.beginPath();
    ctx.ellipse(-size * 0.42, -size * 0.48, size * 0.3, size * 0.12, 0.25, 0, Math.PI * 2);
    ctx.fill();
    ctx.beginPath();
    ctx.ellipse(size * 0.32, -size * 0.38, size * 0.32, size * 0.13, -0.18, 0, Math.PI * 2);
    ctx.fill();
    ctx.fillStyle = "#1f3a2e";
    ctx.beginPath();
    ctx.ellipse(size * 0.1, -size * 0.7, size * 0.2, size * 0.1, -0.1, 0, Math.PI * 2);
    ctx.fill();

    ctx.restore();
  }

  function drawPavilion(x, y, size) {
    ctx.save();
    ctx.translate(x, y);
    ctx.fillStyle = "#8b7355";
    ctx.fillRect(-size * 0.5, size * 0.06, size, size * 0.14);

    ctx.strokeStyle = "#c4a97d";
    ctx.lineWidth = 1.5;
    for (var p = 0; p < 4; p += 1) {
      var colX = -size * 0.36 + p * size * 0.24;
      ctx.beginPath();
      ctx.moveTo(colX, size * 0.06);
      ctx.lineTo(colX, -size * 0.18);
      ctx.stroke();
    }

    ctx.fillStyle = "#5c3d2e";
    ctx.beginPath();
    ctx.moveTo(-size * 0.65, -size * 0.1);
    ctx.quadraticCurveTo(-size * 0.68, -size * 0.38, -size * 0.48, -size * 0.32);
    ctx.lineTo(size * 0.22, -size * 0.44);
    ctx.quadraticCurveTo(size * 0.5, -size * 0.3, size * 0.38, -size * 0.1);
    ctx.quadraticCurveTo(size * 0.6, -size * 0.32, size * 0.56, -size * 0.2);
    ctx.lineTo(-size * 0.55, -size * 0.24);
    ctx.quadraticCurveTo(-size * 0.64, -size * 0.18, -size * 0.65, -size * 0.1);
    ctx.closePath();
    ctx.fill();
    ctx.strokeStyle = "#3a2015";
    ctx.lineWidth = 1.2;
    ctx.stroke();

    ctx.strokeStyle = "#3a2015";
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.moveTo(-size * 0.42, -size * 0.36);
    ctx.quadraticCurveTo(-size * 0.15, -size * 0.58, size * 0.18, -size * 0.46);
    ctx.stroke();

    ctx.fillStyle = "#c9a84c";
    ctx.beginPath();
    ctx.arc(size * 0.02, -size * 0.5, size * 0.07, 0, Math.PI * 2);
    ctx.fill();

    ctx.restore();
  }

  function drawFloatingIsland(x, y, size, alpha, time) {
    ctx.save();
    ctx.globalAlpha = alpha;
    ctx.shadowColor = "rgba(100, 140, 130, 0.3)";
    ctx.shadowBlur = 18;
    ctx.translate(x, y);

    var rockGrad = ctx.createLinearGradient(0, -size * 0.5, 0, size * 0.5);
    rockGrad.addColorStop(0, "#5c8078");
    rockGrad.addColorStop(1, "#2d4a44");
    ctx.fillStyle = rockGrad;
    ctx.beginPath();
    ctx.moveTo(-size * 0.52, size * 0.12);
    ctx.bezierCurveTo(-size * 0.58, -size * 0.18, -size * 0.36, -size * 0.55, -size * 0.12, -size * 0.62);
    ctx.bezierCurveTo(size * 0.1, -size * 0.68, size * 0.35, -size * 0.45, size * 0.48, -size * 0.22);
    ctx.bezierCurveTo(size * 0.62, 0, size * 0.38, size * 0.2, size * 0.15, size * 0.42);
    ctx.lineTo(size * 0.04, size * 0.8);
    ctx.quadraticCurveTo(-size * 0.1, size * 0.55, -size * 0.28, size * 0.28);
    ctx.lineTo(-size * 0.35, size * 0.18);
    ctx.closePath();
    ctx.fill();

    ctx.shadowBlur = 0;
    ctx.globalAlpha = alpha * 0.5;
    ctx.strokeStyle = "rgba(200, 185, 155, 0.45)";
    ctx.lineWidth = 1.5;
    for (var t = 0; t < 4; t += 1) {
      var sx = -size * 0.28 + t * size * 0.16;
      var sy = -size * 0.45 + t * 6;
      ctx.beginPath();
      ctx.moveTo(sx, sy);
      ctx.lineTo(sx + 5, sy + 12 + (t % 2) * 8);
      ctx.stroke();
    }

    ctx.globalAlpha = alpha;
    drawPineTree(-size * 0.32, -size * 0.45, size * 0.38, -0.3);
    drawPineTree(size * 0.22, -size * 0.52, size * 0.32, 0.15);

    if (size > 120) {
      drawPavilion(-size * 0.08, -size * 0.38, size * 0.32);
    } else {
      drawPineTree(size * 0.05, -size * 0.55, size * 0.28, -0.1);
    }

    ctx.globalAlpha = alpha * 0.45;
    var rockOsc = Math.sin(time * 0.002 + x * 0.01) * 3;
    ctx.fillStyle = "#4a6a62";
    for (var r = 0; r < 4; r += 1) {
      ctx.beginPath();
      var rx = -size * 0.15 + r * size * 0.18;
      var ry = size * 0.3 + r * size * 0.14 + rockOsc * (r % 3);
      ctx.moveTo(rx - 5, ry);
      ctx.lineTo(rx + 4 + r, ry - 6 - r);
      ctx.lineTo(rx + 8 + r, ry + 3);
      ctx.lineTo(rx - 2, ry + 7);
      ctx.closePath();
      ctx.fill();
    }

    ctx.restore();
  }

  function drawCrane(crane, time) {
    ctx.save();
    ctx.translate(crane.x, crane.y);
    ctx.scale(crane.size, crane.size);
    ctx.globalAlpha = crane.alpha;

    var wingAnim = Math.sin(crane.phase) * 0.3;

    ctx.fillStyle = "#f8f8f4";
    ctx.beginPath();
    ctx.ellipse(0, 0, 22, 7, -0.08, 0, Math.PI * 2);
    ctx.fill();
    ctx.strokeStyle = "rgba(180, 175, 165, 0.3)";
    ctx.lineWidth = 0.5;
    ctx.stroke();

    ctx.strokeStyle = "#3a3835";
    ctx.lineWidth = 1.8;
    ctx.lineCap = "round";
    ctx.beginPath();
    ctx.moveTo(3, 0);
    ctx.bezierCurveTo(18, -10 + wingAnim * 6, 34, -24, 50, -26);
    ctx.stroke();
    ctx.beginPath();
    ctx.moveTo(2, 1);
    ctx.bezierCurveTo(16, 14 - wingAnim * 4, 30, 28, 46, 30);
    ctx.stroke();

    ctx.strokeStyle = "#2a2825";
    ctx.lineWidth = 1.2;
    ctx.beginPath();
    ctx.moveTo(50, -26);
    ctx.lineTo(56, -30);
    ctx.moveTo(46, 30);
    ctx.lineTo(52, 34);
    ctx.stroke();

    ctx.fillStyle = "#d44235";
    ctx.shadowColor = "rgba(212, 66, 53, 0.45)";
    ctx.shadowBlur = 5;
    ctx.beginPath();
    ctx.arc(18, -3, 3, 0, Math.PI * 2);
    ctx.fill();
    ctx.shadowBlur = 0;

    if (crane.pose === "glide") {
      ctx.fillStyle = "#f8f8f4";
      ctx.beginPath();
      ctx.ellipse(-8, -16 + wingAnim * 4, 28, 9, -0.25, 0, Math.PI * 2);
      ctx.fill();
      ctx.fillStyle = "#1a1a18";
      ctx.beginPath();
      ctx.ellipse(-26, -18 + wingAnim * 4, 10, 4, -0.25, 0, Math.PI * 2);
      ctx.fill();
      ctx.beginPath();
      ctx.ellipse(12, -28 + wingAnim * 4, 8, 3, 0.1, 0, Math.PI * 2);
      ctx.fill();
    } else if (crane.pose === "fly") {
      var upAngle = -0.6 + wingAnim * 1.2;
      ctx.save();
      ctx.rotate(upAngle);
      ctx.fillStyle = "#f8f8f4";
      ctx.beginPath();
      ctx.ellipse(-6, -18, 26, 8, 0, 0, Math.PI * 2);
      ctx.fill();
      ctx.fillStyle = "#1a1a18";
      ctx.beginPath();
      ctx.ellipse(-22, -20, 9, 4, 0, 0, Math.PI * 2);
      ctx.fill();
      ctx.restore();
    } else {
      ctx.fillStyle = "#f8f8f4";
      ctx.beginPath();
      ctx.ellipse(2, -22, 30, 11, 0.4, 0, Math.PI * 2);
      ctx.fill();
      ctx.fillStyle = "#1a1a18";
      ctx.beginPath();
      ctx.ellipse(-15, -28, 12, 5, 0.3, 0, Math.PI * 2);
      ctx.fill();
    }

    ctx.strokeStyle = "#3a3835";
    ctx.lineWidth = 2;
    ctx.lineCap = "round";
    for (var t = 0; t < 3; t += 1) {
      ctx.beginPath();
      ctx.moveTo(-18 + t * 6, 4);
      ctx.quadraticCurveTo(-28 + t * 8, 18 + t * 5, -35 + t * 10, 28 + t * 6);
      ctx.stroke();
    }

    ctx.restore();
  }

  function drawPlayer() {
    ctx.save();
    ctx.translate(player.x, player.y);
    ctx.rotate(player.tilt);
    var characterScale = Math.max(0.48, Math.min(1, width / 960));
    ctx.scale(characterScale, characterScale);
    ctx.rotate(0.08);

    var wf = Math.max(0.7, Math.min(1.5, speed / 360));

    ctx.shadowColor = "rgba(62, 240, 216, 0.55)";
    ctx.shadowBlur = 14;
    ctx.strokeStyle = "rgba(62, 240, 216, 0.35)";
    ctx.lineWidth = 8;
    ctx.lineCap = "round";
    ctx.beginPath();
    ctx.moveTo(-240 * wf, 10);
    ctx.bezierCurveTo(-170, -22, -90, -28, 20, -4);
    ctx.bezierCurveTo(-60, 18, -140, 34, -240 * wf, 20);
    ctx.stroke();

    ctx.shadowBlur = 6;
    ctx.lineWidth = 3;
    ctx.strokeStyle = "rgba(120, 255, 240, 0.5)";
    ctx.beginPath();
    ctx.moveTo(-220 * wf, 12);
    ctx.bezierCurveTo(-150, -14, -80, -20, 15, -2);
    ctx.stroke();

    for (var tp = 0; tp < 10; tp += 1) {
      ctx.fillStyle = "rgba(160, 255, 242, " + (0.3 + tp * 0.06) + ")";
      ctx.beginPath();
      ctx.arc(-180 * wf + tp * 22, 12 + Math.sin(tp * 1.3) * 8, 1.5 + Math.random() * 2, 0, Math.PI * 2);
      ctx.fill();
    }

    ctx.shadowBlur = 0;
    ctx.shadowColor = "rgba(62, 240, 216, 0.65)";
    ctx.shadowBlur = 15;

    var bladeGrad = ctx.createLinearGradient(0, 10, 0, 20);
    bladeGrad.addColorStop(0, "#e0f0ec");
    bladeGrad.addColorStop(0.45, "#9ad8ce");
    bladeGrad.addColorStop(1, "#3a8a7c");
    ctx.fillStyle = bladeGrad;
    ctx.beginPath();
    ctx.moveTo(-120, 16);
    ctx.bezierCurveTo(-60, 11, 30, 9, 110, 10);
    ctx.lineTo(116, 13);
    ctx.bezierCurveTo(30, 15, -50, 19, -120, 19);
    ctx.closePath();
    ctx.fill();

    ctx.shadowBlur = 0;
    ctx.strokeStyle = "rgba(255, 255, 255, 0.6)";
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.moveTo(-118, 15);
    ctx.bezierCurveTo(-50, 10, 30, 8, 112, 9);
    ctx.stroke();

    ctx.strokeStyle = "#d4af5e";
    ctx.lineWidth = 1.5;
    for (var gp = 0; gp < 8; gp += 1) {
      var gx = -85 + gp * 24;
      ctx.beginPath();
      ctx.moveTo(gx, 12);
      ctx.lineTo(gx + 3, 19);
      ctx.stroke();
      ctx.beginPath();
      ctx.arc(gx + 1, 13, 2, 0, Math.PI * 2);
      ctx.stroke();
    }

    ctx.fillStyle = "#3a9e8a";
    for (var jd = 0; jd < 4; jd += 1) {
      ctx.fillRect(-25 + jd * 14, 12, 4, 4);
    }

    ctx.strokeStyle = "#c9a84c";
    ctx.lineWidth = 2.5;
    ctx.strokeRect(-16, 9, 8, 16);

    ctx.fillStyle = "#2a3028";
    ctx.fillRect(-55, 12, 42, 8);
    ctx.strokeStyle = "#c9a84c";
    ctx.lineWidth = 1.5;
    for (var hw = 0; hw < 4; hw += 1) {
      ctx.beginPath();
      ctx.moveTo(-52 + hw * 10, 13);
      ctx.lineTo(-52 + hw * 10, 19);
      ctx.stroke();
    }

    ctx.fillStyle = "#c9a84c";
    ctx.beginPath();
    ctx.arc(-57, 16, 5, 0, Math.PI * 2);
    ctx.fill();
    ctx.strokeStyle = "#8a6020";
    ctx.lineWidth = 1.5;
    ctx.stroke();

    ctx.strokeStyle = "rgba(210, 190, 145, 0.4)";
    ctx.lineWidth = 4;
    ctx.beginPath();
    ctx.moveTo(-100, 15);
    ctx.bezierCurveTo(-40, -8, 40, -10, 100, 7);
    ctx.stroke();

    ctx.fillStyle = "rgba(225, 235, 225, 0.85)";
    ctx.beginPath();
    ctx.moveTo(-18, -46);
    ctx.bezierCurveTo(-60, -68, -100, -55, -125 * wf, -48);
    ctx.bezierCurveTo(-95, -30, -65, -22, -14, -10);
    ctx.closePath();
    ctx.fill();
    ctx.strokeStyle = "rgba(140, 165, 150, 0.5)";
    ctx.lineWidth = 1.2;
    ctx.stroke();

    ctx.fillStyle = "rgba(220, 232, 218, 0.8)";
    ctx.beginPath();
    ctx.moveTo(0, -58);
    ctx.bezierCurveTo(-45, -78, -80, -72, -110 * wf, -62);
    ctx.bezierCurveTo(-75, -36, -40, -28, 5, -28);
    ctx.closePath();
    ctx.fill();
    ctx.stroke();

    ctx.fillStyle = "rgba(215, 228, 212, 0.78)";
    ctx.beginPath();
    ctx.moveTo(2, -52);
    ctx.bezierCurveTo(-35, -72, -70, -64, -95 * wf, -55);
    ctx.bezierCurveTo(-60, -35, -30, -25, 8, -24);
    ctx.closePath();
    ctx.fill();
    ctx.stroke();

    ctx.fillStyle = "#2a3830";
    ctx.beginPath();
    ctx.moveTo(-8, -18);
    ctx.quadraticCurveTo(-6, 0, -5, 10);
    ctx.quadraticCurveTo(-1, 12, 8, 10);
    ctx.quadraticCurveTo(9, 0, 7, -18);
    ctx.closePath();
    ctx.fill();
    ctx.strokeStyle = "rgba(60, 80, 70, 0.5)";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(-6, -16);
    ctx.lineTo(-4, 9);
    ctx.moveTo(6, -16);
    ctx.lineTo(7, 9);
    ctx.stroke();

    ctx.fillStyle = "rgba(228, 238, 225, 0.85)";
    ctx.beginPath();
    ctx.moveTo(-10, -18);
    ctx.quadraticCurveTo(-16, -4, -12, 22);
    ctx.lineTo(16, 22);
    ctx.quadraticCurveTo(12, -4, 12, -18);
    ctx.closePath();
    ctx.fill();
    ctx.strokeStyle = "rgba(140, 165, 150, 0.4)";
    ctx.lineWidth = 1;
    ctx.stroke();

    ctx.fillStyle = "#e8f0e5";
    ctx.beginPath();
    ctx.moveTo(-9, -68);
    ctx.quadraticCurveTo(-14, -42, -16, -20);
    ctx.lineTo(18, -20);
    ctx.quadraticCurveTo(16, -42, 12, -68);
    ctx.quadraticCurveTo(4, -72, -9, -68);
    ctx.closePath();
    ctx.fill();
    ctx.strokeStyle = "rgba(130, 160, 148, 0.5)";
    ctx.lineWidth = 1;
    ctx.stroke();

    ctx.strokeStyle = "#5a7a6a";
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.moveTo(-4, -70);
    ctx.lineTo(8, -56);
    ctx.stroke();
    ctx.beginPath();
    ctx.moveTo(8, -70);
    ctx.lineTo(3, -56);
    ctx.stroke();
    ctx.fillStyle = "#f0f5ed";
    ctx.beginPath();
    ctx.moveTo(-3, -69);
    ctx.lineTo(7, -57);
    ctx.lineTo(7, -68);
    ctx.closePath();
    ctx.fill();

    ctx.fillStyle = "#a08050";
    ctx.fillRect(-13, -28, 27, 5);
    ctx.beginPath();
    ctx.arc(3, -28, 3, 0, Math.PI * 2);
    ctx.fill();

    ctx.fillStyle = "#ead5c0";
    ctx.beginPath();
    ctx.ellipse(3, -84, 7, 9, 0, 0, Math.PI * 2);
    ctx.fill();
    ctx.strokeStyle = "#d4b896";
    ctx.lineWidth = 1;
    ctx.stroke();

    ctx.fillStyle = "#1a1815";
    ctx.beginPath();
    ctx.moveTo(-6, -94);
    ctx.bezierCurveTo(-4, -104, 2, -108, 6, -106);
    ctx.bezierCurveTo(3, -100, -2, -96, -6, -94);
    ctx.fill();
    ctx.beginPath();
    ctx.ellipse(0, -102, 5, 6, 0, 0, Math.PI * 2);
    ctx.fill();

    ctx.strokeStyle = "#c84931";
    ctx.lineWidth = 1.2;
    ctx.beginPath();
    ctx.moveTo(-3, -105);
    ctx.bezierCurveTo(-15, -108, -30, -106, -50 * wf, -100);
    ctx.stroke();

    ctx.fillStyle = "#1a1815";
    ctx.beginPath();
    ctx.moveTo(4, -92);
    ctx.bezierCurveTo(-4, -96, -8, -94, -10, -88);
    ctx.bezierCurveTo(-35, -82, -65, -78, -85 * wf, -72);
    ctx.lineTo(-82 * wf, -68);
    ctx.bezierCurveTo(-55, -74, -30, -78, -8, -82);
    ctx.closePath();
    ctx.fill();

    ctx.fillStyle = "#3a3530";
    ctx.fillRect(14, -58, 8, 10);
    ctx.fillStyle = "#c9a84c";
    ctx.strokeStyle = "#c9a84c";
    ctx.lineWidth = 1;
    ctx.strokeRect(14, -58, 8, 10);
    ctx.fillStyle = "#3a3530";
    ctx.fillRect(-22, -57, 8, 10);
    ctx.strokeRect(-22, -57, 8, 10);

    ctx.fillStyle = "#ead5c0";
    ctx.beginPath();
    ctx.ellipse(18, -53, 4, 3, 0.2, 0, Math.PI * 2);
    ctx.fill();
    ctx.beginPath();
    ctx.ellipse(-16, -52, 4, 3, -0.1, 0, Math.PI * 2);
    ctx.fill();

    ctx.restore();
  }

  function drawMountain(item) {
    ctx.save();
    var mg = ctx.createLinearGradient(item.x, item.y, item.x, height);
    mg.addColorStop(0, "#5a8078");
    mg.addColorStop(1, "#2d4a44");
    ctx.fillStyle = mg;
    ctx.beginPath();
    ctx.moveTo(item.x, height);
    ctx.quadraticCurveTo(item.x + item.w * 0.28, item.y + 30, item.x + item.w * 0.5, item.y);
    ctx.quadraticCurveTo(item.x + item.w * 0.72, item.y + 28, item.x + item.w, height);
    ctx.closePath();
    ctx.fill();

    ctx.globalAlpha = 0.28;
    ctx.fillStyle = "#e8e5d8";
    ctx.beginPath();
    ctx.moveTo(item.x + item.w * 0.5, item.y + 10);
    ctx.lineTo(item.x + item.w * 0.36, item.y + 88);
    ctx.lineTo(item.x + item.w * 0.6, item.y + 58);
    ctx.closePath();
    ctx.fill();

    ctx.globalAlpha = 0.3;
    ctx.strokeStyle = "#1a3530";
    ctx.lineWidth = 1.2;
    for (var ts = 0; ts < 3; ts += 1) {
      ctx.beginPath();
      ctx.moveTo(item.x + item.w * 0.4 + ts * item.w * 0.08, item.y + 30 + ts * 14);
      ctx.lineTo(item.x + item.w * 0.44 + ts * item.w * 0.06, item.y + 48 + ts * 12);
      ctx.stroke();
    }

    if (item.tree) {
      ctx.globalAlpha = 1;
      ctx.strokeStyle = "#2a3020";
      ctx.lineWidth = 2.5;
      ctx.beginPath();
      ctx.moveTo(item.x + item.w * 0.54, item.y + 28);
      ctx.lineTo(item.x + item.w * 0.54, item.y - 22);
      ctx.stroke();
      ctx.fillStyle = "#1f3a2e";
      ctx.beginPath();
      ctx.ellipse(item.x + item.w * 0.48, item.y - 18, 28, 7, -0.22, 0, Math.PI * 2);
      ctx.ellipse(item.x + item.w * 0.62, item.y - 6, 22, 6, 0.18, 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.restore();
  }

  function drawBird(item) {
    ctx.save();
    ctx.translate(item.x + item.w * 0.5, item.y + item.h * 0.5);
    ctx.strokeStyle = "#2a3a34";
    ctx.lineWidth = 2.5;
    ctx.lineCap = "round";
    ctx.fillStyle = "rgba(220, 225, 215, 0.85)";
    ctx.beginPath();
    ctx.ellipse(0, 4, 18, 6, 0, 0, Math.PI * 2);
    ctx.fill();
    ctx.beginPath();
    ctx.moveTo(-48, 4);
    ctx.quadraticCurveTo(-18, -34 + Math.sin(item.phase) * 10, 0, 2);
    ctx.quadraticCurveTo(22, -38 - Math.sin(item.phase) * 10, 54, 0);
    ctx.moveTo(16, 5);
    ctx.lineTo(42, 11);
    ctx.moveTo(12, 7);
    ctx.lineTo(34, 19);
    ctx.stroke();
    ctx.fillStyle = "#8a4030";
    ctx.globalAlpha = 0.7;
    ctx.beginPath();
    ctx.arc(19, 1, 1.8, 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();
  }

  function drawRock(item) {
    ctx.save();
    ctx.translate(item.x + item.w / 2, item.y + item.h / 2);
    ctx.rotate(item.spin);
    ctx.fillStyle = "#4a6a62";
    ctx.beginPath();
    ctx.moveTo(-18, -26);
    ctx.lineTo(18, -19);
    ctx.lineTo(24, 8);
    ctx.lineTo(4, 27);
    ctx.lineTo(-23, 16);
    ctx.lineTo(-25, -10);
    ctx.closePath();
    ctx.fill();
    ctx.strokeStyle = "rgba(200, 180, 145, 0.3)";
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.moveTo(-9, -18);
    ctx.lineTo(8, 14);
    ctx.stroke();
    ctx.globalAlpha = 0.3;
    ctx.strokeStyle = "#1a3028";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(-4, -12);
    ctx.lineTo(14, 4);
    ctx.stroke();
    ctx.restore();
  }

  function drawOrb(orb) {
    var pulse = Math.sin(orb.pulse);
    ctx.save();

    ctx.shadowColor = "rgba(62, 240, 216, 0.7)";
    ctx.shadowBlur = 26;
    ctx.fillStyle = "rgba(62, 240, 216, 0.15)";
    ctx.beginPath();
    ctx.arc(orb.x, orb.y, orb.r * 1.8, 0, Math.PI * 2);
    ctx.fill();

    for (var s = 0; s < 3; s += 1) {
      var angle = orb.pulse + s * Math.PI * 0.67;
      ctx.strokeStyle = "rgba(143, 248, 232, " + (0.55 + s * 0.12) + ")";
      ctx.lineWidth = 2.5 - s * 0.5;
      ctx.beginPath();
      ctx.moveTo(orb.x, orb.y);
      ctx.bezierCurveTo(
        orb.x + Math.cos(angle) * orb.r * 0.6, orb.y + Math.sin(angle) * orb.r * 0.6,
        orb.x + Math.cos(angle + 0.8) * orb.r * 1.1, orb.y + Math.sin(angle + 0.8) * orb.r * 1.1,
        orb.x + Math.cos(angle + 1.6 + pulse) * orb.r * 1.2, orb.y + Math.sin(angle + 1.6 + pulse) * orb.r * 1.2
      );
      ctx.stroke();
    }

    ctx.shadowBlur = 0;
    var coreGrad = ctx.createRadialGradient(orb.x, orb.y, 0, orb.x, orb.y, orb.r * 0.5);
    coreGrad.addColorStop(0, "rgba(255, 255, 255, 0.95)");
    coreGrad.addColorStop(0.35, "rgba(180, 255, 240, 0.7)");
    coreGrad.addColorStop(1, "rgba(62, 240, 216, 0)");
    ctx.fillStyle = coreGrad;
    ctx.beginPath();
    ctx.arc(orb.x, orb.y, orb.r * 0.5, 0, Math.PI * 2);
    ctx.fill();

    ctx.fillStyle = "#fff";
    ctx.beginPath();
    ctx.arc(orb.x, orb.y, 3 + pulse * 1.5, 0, Math.PI * 2);
    ctx.fill();

    for (var m = 0; m < 5; m += 1) {
      var ma = m * Math.PI * 0.4 + orb.pulse * 0.5;
      var md = orb.r * 1.1 + m * 2;
      ctx.fillStyle = "rgba(190, 255, 242, " + (0.5 - m * 0.08) + ")";
      ctx.beginPath();
      ctx.arc(orb.x + Math.cos(ma) * md, orb.y + Math.sin(ma) * md, 1.5 + Math.random(), 0, Math.PI * 2);
      ctx.fill();
    }

    ctx.restore();
  }

  function drawSparks() {
    ctx.save();
    ctx.shadowColor = "rgba(62, 240, 216, 0.55)";
    ctx.shadowBlur = 6;
    sparks.forEach(function (spark) {
      ctx.globalAlpha = Math.max(0, spark.life * 2);
      ctx.fillStyle = "#7ffee8";
      ctx.beginPath();
      ctx.arc(spark.x, spark.y, spark.size || 3, 0, Math.PI * 2);
      ctx.fill();
    });

    swordTrail.forEach(function (t) {
      ctx.globalAlpha = Math.max(0, t.life * 1.3);
      ctx.fillStyle = "rgba(140, 255, 242, 0.5)";
      ctx.beginPath();
      ctx.arc(t.x, t.y, t.size, 0, Math.PI * 2);
      ctx.fill();
    });
    ctx.shadowBlur = 0;
    ctx.restore();
  }

  function draw(time) {
    drawBackground(time);

    cranes.forEach(function (crane) {
      if (crane.depth === "far" || crane.depth === "mid") {
        drawCrane(crane, time);
      }
    });

    orbs.forEach(drawOrb);
    obstacles.forEach(function (item) {
      if (item.type === "mountain") {
        drawMountain(item);
      } else if (item.type === "bird") {
        drawBird(item);
      } else {
        drawRock(item);
      }
    });
    drawSparks();

    cranes.forEach(function (crane) {
      if (crane.depth === "near") {
        drawCrane(crane, time);
      }
    });

    drawPlayer();

    cranes.forEach(function (crane) {
      if (crane.depth === "foreground") {
        drawCrane(crane, time);
      }
    });
  }

  function loop(now) {
    if (!running || paused) {
      return;
    }
    var dt = Math.min(0.032, (now - lastTime) / 1000 || 0.016);
    lastTime = now;
    update(dt);
    draw(now);
    if (running) {
      requestAnimationFrame(loop);
    }
  }

  function setHolding(value) {
    holding = value;
  }

  startButton.addEventListener("click", startGame);
  restartButton.addEventListener("click", startGame);
  pauseButton.addEventListener("click", function () {
    togglePause();
  });
  resumeButton.addEventListener("click", function () {
    togglePause(false);
  });

  window.addEventListener("resize", function () {
    fitCanvas();
    if (!running) {
      draw(performance.now());
    }
  });

  window.addEventListener("keydown", function (event) {
    if (event.code === "KeyP") {
      event.preventDefault();
      togglePause();
      return;
    }
    if (event.code === "Space" || event.code === "ArrowUp") {
      event.preventDefault();
      if (paused) {
        return;
      }
      if (!running && gameOverPanel.hidden && startPanel.hidden === false) {
        startGame();
      }
      setHolding(true);
    }
  });

  window.addEventListener("keyup", function (event) {
    if (event.code === "Space" || event.code === "ArrowUp") {
      event.preventDefault();
      setHolding(false);
    }
  });

  canvas.addEventListener("pointerdown", function (event) {
    event.preventDefault();
    if (paused) {
      return;
    }
    if (!running && !startPanel.hidden) {
      startGame();
      return;
    }
    setHolding(true);
  });

  window.addEventListener("pointerup", function () {
    setHolding(false);
  });

  fitCanvas();
  resetGame();
  draw(performance.now());
})();
