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
    orbs.push({
      x: width + 48,
      y: rand(height * 0.22, height * 0.68),
      r: 14,
      taken: false,
      pulse: rand(0, Math.PI * 2)
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

    obstacles = obstacles.filter(function (item) {
      return item.x > -180;
    });
    orbs = orbs.filter(function (orb) {
      return orb.x > -60 && !orb.taken;
    });
    sparks = sparks.filter(function (spark) {
      return spark.life > 0;
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
        for (var i = 0; i < 8; i += 1) {
          sparks.push({
            x: orb.x,
            y: orb.y,
            vx: rand(-90, 90),
            vy: rand(-90, 90),
            life: rand(0.25, 0.55)
          });
        }
      }
    });
  }

  function drawBackground(time) {
    var sky = ctx.createLinearGradient(0, 0, 0, height);
    sky.addColorStop(0, "#c9ddd8");
    sky.addColorStop(0.42, "#e3eadf");
    sky.addColorStop(1, "#f5ecd7");
    ctx.fillStyle = sky;
    ctx.fillRect(0, 0, width, height);

    ctx.save();
    ctx.globalAlpha = 0.74;
    ctx.fillStyle = "#f5edd2";
    ctx.beginPath();
    ctx.arc(width * 0.19, height * 0.18, Math.max(48, width * 0.045), 0, Math.PI * 2);
    ctx.fill();
    ctx.globalAlpha = 0.13;
    ctx.fillStyle = "#9d9a85";
    for (var m = 0; m < 8; m += 1) {
      ctx.beginPath();
      ctx.arc(width * 0.19 + Math.cos(m) * 28, height * 0.18 + Math.sin(m * 1.7) * 20, 8 + (m % 3) * 4, 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.restore();

    drawMountainLayer(time * 0.008, height * 0.56, 0.2, "#7fa39d", 230);
    drawMountainLayer(time * 0.018, height * 0.7, 0.36, "#527b75", 180);
    drawFloatingIsland(width * 0.55 - (time * 0.018 % (width + 320)), height * 0.22, 110, 0.62);
    drawFloatingIsland(width * 0.93 - (time * 0.03 % (width + 380)), height * 0.34, 145, 0.78);
    drawCloudLayer(time * 0.026, height * 0.34, 0.7, 96);
    drawCloudLayer(time * 0.047, height * 0.56, 0.82, 128);
    drawMountainLayer(time * 0.042, height * 0.9, 0.52, "#315d58", 150);
    drawCloudLayer(time * 0.07, height * 0.83, 0.9, 148);
  }

  function drawMountainLayer(offset, baseY, scale, color, step) {
    ctx.save();
    ctx.globalAlpha = scale;
    ctx.fillStyle = color;
    ctx.beginPath();
    ctx.moveTo(0, height);
    for (var x = -step; x <= width + step; x += step) {
      var peakX = x - (offset % step);
      var peakY = baseY - randLike(x, offset) * height * 0.2 - 72;
      ctx.lineTo(peakX + step * 0.48, peakY);
      ctx.lineTo(peakX + step, height);
    }
    ctx.lineTo(width, height);
    ctx.closePath();
    ctx.fill();
    ctx.globalAlpha = scale * 0.42;
    ctx.strokeStyle = "#143f3d";
    ctx.lineWidth = 1.2;
    for (var i = -step; i <= width + step; i += step) {
      var px = i - (offset % step) + step * 0.48;
      ctx.beginPath();
      ctx.moveTo(px, baseY - 132);
      ctx.lineTo(px - step * 0.18, height);
      ctx.stroke();
    }
    ctx.restore();
  }

  function randLike(x, offset) {
    return 0.55 + Math.abs(Math.sin(x * 0.021 + offset * 0.004)) * 0.9;
  }

  function drawCloudLayer(offset, y, alpha, gap) {
    ctx.save();
    ctx.globalAlpha = alpha;
    ctx.fillStyle = "rgba(255, 255, 255, 0.64)";
    for (var x = -gap * 2; x < width + gap * 2; x += gap * 1.9) {
      var px = x - (offset % (gap * 1.9));
      ctx.beginPath();
      ctx.ellipse(px, y, gap * 0.78, 28, 0, 0, Math.PI * 2);
      ctx.ellipse(px + gap * 0.46, y + 13, gap * 0.62, 23, 0, 0, Math.PI * 2);
      ctx.ellipse(px - gap * 0.48, y + 16, gap * 0.5, 21, 0, 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.restore();
  }

  function drawFloatingIsland(x, y, size, alpha) {
    ctx.save();
    ctx.globalAlpha = alpha;
    ctx.translate(x, y);
    ctx.fillStyle = "#315d57";
    ctx.beginPath();
    ctx.moveTo(-size * 0.46, 0);
    ctx.quadraticCurveTo(-size * 0.18, -size * 0.52, size * 0.25, -size * 0.44);
    ctx.quadraticCurveTo(size * 0.5, -size * 0.08, size * 0.38, size * 0.18);
    ctx.lineTo(size * 0.06, size * 0.72);
    ctx.lineTo(-size * 0.32, size * 0.2);
    ctx.closePath();
    ctx.fill();
    ctx.strokeStyle = "rgba(244, 220, 157, 0.55)";
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(-size * 0.08, -size * 0.42);
    ctx.lineTo(-size * 0.08, -size * 0.92);
    ctx.moveTo(-size * 0.24, -size * 0.68);
    ctx.quadraticCurveTo(-size * 0.08, -size * 0.9, size * 0.12, -size * 0.68);
    ctx.stroke();
    ctx.fillStyle = "#153832";
    for (var t = 0; t < 3; t += 1) {
      ctx.beginPath();
      ctx.ellipse(-size * 0.25 + t * size * 0.18, -size * 0.5 - t * 6, 18, 7, -0.25, 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.restore();
  }

  function drawPlayer() {
    ctx.save();
    ctx.translate(player.x, player.y);
    ctx.rotate(player.tilt);

    var trail = ctx.createLinearGradient(-180, 10, 20, 0);
    trail.addColorStop(0, "rgba(79, 188, 178, 0)");
    trail.addColorStop(0.72, "rgba(64, 218, 205, 0.25)");
    trail.addColorStop(1, "rgba(119, 255, 241, 0.64)");
    ctx.fillStyle = trail;
    ctx.beginPath();
    ctx.moveTo(-178, 18);
    ctx.bezierCurveTo(-112, -14, -48, -15, 28, -7);
    ctx.lineTo(28, 10);
    ctx.bezierCurveTo(-56, 22, -112, 32, -178, 18);
    ctx.closePath();
    ctx.fill();

    ctx.shadowColor = "rgba(91, 243, 229, 0.8)";
    ctx.shadowBlur = 16;
    ctx.strokeStyle = "#f3d38d";
    ctx.lineWidth = 6;
    ctx.lineCap = "round";
    ctx.beginPath();
    ctx.moveTo(-68, 24);
    ctx.lineTo(92, 8);
    ctx.stroke();
    ctx.shadowBlur = 0;

    ctx.strokeStyle = "#77f4e9";
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(-50, 20);
    ctx.lineTo(96, 6);
    ctx.stroke();

    ctx.fillStyle = "#e7fff9";
    ctx.beginPath();
    ctx.moveTo(104, 6);
    ctx.lineTo(70, -8);
    ctx.lineTo(78, 18);
    ctx.closePath();
    ctx.fill();

    ctx.fillStyle = "#102420";
    ctx.beginPath();
    ctx.arc(-12, -35, 10, 0, Math.PI * 2);
    ctx.fill();

    ctx.strokeStyle = "#0f211f";
    ctx.lineWidth = 4;
    ctx.beginPath();
    ctx.moveTo(-20, -37);
    ctx.bezierCurveTo(-54, -48, -80, -42, -112, -56);
    ctx.moveTo(-18, -33);
    ctx.bezierCurveTo(-46, -26, -75, -30, -108, -16);
    ctx.stroke();

    ctx.fillStyle = "#f4f0e4";
    ctx.beginPath();
    ctx.moveTo(-18, -24);
    ctx.lineTo(18, -12);
    ctx.lineTo(2, 24);
    ctx.lineTo(-34, 14);
    ctx.closePath();
    ctx.fill();

    ctx.fillStyle = "#3f8275";
    ctx.beginPath();
    ctx.moveTo(-38, -8);
    ctx.quadraticCurveTo(-72, 10, -96, 0);
    ctx.quadraticCurveTo(-62, 28, -26, 24);
    ctx.closePath();
    ctx.fill();

    ctx.strokeStyle = "#c69b54";
    ctx.lineWidth = 3;
    ctx.beginPath();
    ctx.moveTo(-24, 4);
    ctx.lineTo(10, 7);
    ctx.stroke();
    ctx.restore();
  }

  function drawMountain(item) {
    ctx.save();
    ctx.fillStyle = "#345f58";
    ctx.beginPath();
    ctx.moveTo(item.x, height);
    ctx.lineTo(item.x + item.w * 0.5, item.y);
    ctx.lineTo(item.x + item.w, height);
    ctx.closePath();
    ctx.fill();

    ctx.globalAlpha = 0.34;
    ctx.fillStyle = "#e7e5d1";
    ctx.beginPath();
    ctx.moveTo(item.x + item.w * 0.5, item.y + 12);
    ctx.lineTo(item.x + item.w * 0.34, item.y + 96);
    ctx.lineTo(item.x + item.w * 0.62, item.y + 64);
    ctx.closePath();
    ctx.fill();

    if (item.tree) {
      ctx.globalAlpha = 1;
      ctx.strokeStyle = "#173832";
      ctx.lineWidth = 3;
      ctx.beginPath();
      ctx.moveTo(item.x + item.w * 0.54, item.y + 30);
      ctx.lineTo(item.x + item.w * 0.54, item.y - 26);
      ctx.stroke();
      ctx.fillStyle = "#12362f";
      ctx.beginPath();
      ctx.ellipse(item.x + item.w * 0.5, item.y - 22, 34, 9, -0.22, 0, Math.PI * 2);
      ctx.ellipse(item.x + item.w * 0.62, item.y - 8, 28, 8, 0.18, 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.restore();
  }

  function drawBird(item) {
    ctx.save();
    ctx.translate(item.x + item.w * 0.5, item.y + item.h * 0.5);
    ctx.strokeStyle = "#263833";
    ctx.lineWidth = 3;
    ctx.lineCap = "round";
    ctx.fillStyle = "rgba(255, 255, 255, 0.9)";
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
    ctx.fillStyle = "#c84931";
    ctx.beginPath();
    ctx.arc(19, 1, 2.2, 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();
  }

  function drawRock(item) {
    ctx.save();
    ctx.translate(item.x + item.w / 2, item.y + item.h / 2);
    ctx.rotate(item.spin);
    ctx.fillStyle = "#58766f";
    ctx.beginPath();
    ctx.moveTo(-18, -26);
    ctx.lineTo(18, -19);
    ctx.lineTo(24, 8);
    ctx.lineTo(4, 27);
    ctx.lineTo(-23, 16);
    ctx.lineTo(-25, -10);
    ctx.closePath();
    ctx.fill();
    ctx.strokeStyle = "rgba(239, 218, 160, 0.42)";
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(-9, -18);
    ctx.lineTo(8, 14);
    ctx.stroke();
    ctx.restore();
  }

  function drawOrb(orb) {
    var pulse = Math.sin(orb.pulse) * 3;
    ctx.save();
    ctx.shadowColor = "rgba(53, 195, 178, 0.85)";
    ctx.shadowBlur = 24;
    ctx.strokeStyle = "#58f0dd";
    ctx.lineWidth = 5;
    ctx.beginPath();
    ctx.arc(orb.x, orb.y, orb.r + pulse, orb.pulse, orb.pulse + Math.PI * 1.55);
    ctx.stroke();
    ctx.lineWidth = 2;
    ctx.strokeStyle = "rgba(205, 255, 245, 0.92)";
    ctx.beginPath();
    ctx.arc(orb.x, orb.y, orb.r * 0.62, -orb.pulse, Math.PI * 1.7 - orb.pulse);
    ctx.stroke();
    ctx.shadowBlur = 0;
    ctx.fillStyle = "#fff7bc";
    ctx.beginPath();
    ctx.arc(orb.x - 2, orb.y - 3, 3.5, 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();
  }

  function drawSparks() {
    ctx.save();
    sparks.forEach(function (spark) {
      ctx.globalAlpha = Math.max(0, spark.life * 2);
      ctx.fillStyle = "#f8ce73";
      ctx.beginPath();
      ctx.arc(spark.x, spark.y, 3, 0, Math.PI * 2);
      ctx.fill();
    });
    ctx.restore();
  }

  function draw(time) {
    drawBackground(time);
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
    drawPlayer();
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
