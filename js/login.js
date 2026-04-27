(function () {
  var switchButtons = document.querySelectorAll(".switch-btn");
  var panels = document.querySelectorAll(".panel");
  var panelJumps = document.querySelectorAll("[data-panel-jump]");
  var statusBox = document.getElementById("status-box");
  var loginForm = document.getElementById("login-form");
  var registerForm = document.getElementById("register-form");
  var loginSubmit = document.getElementById("login-submit");
  var registerSubmit = document.getElementById("register-submit");
  var resetSubmit = document.getElementById("reset-submit");
  var sendCodeBtn = document.getElementById("send-code-btn");

  function showPanel(name) {
    switchButtons.forEach(function (button) {
      button.classList.toggle("active", button.getAttribute("data-panel") === name);
    });

    panels.forEach(function (panel) {
      panel.classList.toggle("active", panel.id === "panel-" + name);
    });

    if (name === "login") {
      setStatus("", "");
    } else if (name === "register") {
      setStatus("", "");
    } else {
      setStatus("找回密码当前是接口占位版本，用来验证前后端链路。", "");
    }
  }

  function setStatus(text, state) {
    statusBox.textContent = text;
    statusBox.classList.remove("ok", "err");
    if (state) {
      statusBox.classList.add(state);
    }
  }

  function togglePassword(targetId) {
    var input = document.getElementById(targetId);
    if (!input) {
      return;
    }
    input.type = input.type === "password" ? "text" : "password";
  }

  function countdown(button) {
    var seconds = 8;
    button.disabled = true;
    var origin = button.textContent;
    button.textContent = "已发送 " + seconds + "s";

    var timer = window.setInterval(function () {
      seconds -= 1;
      if (seconds <= 0) {
        window.clearInterval(timer);
        button.disabled = false;
        button.textContent = origin;
        return;
      }
      button.textContent = "已发送 " + seconds + "s";
    }, 1000);
  }

  function postForm(url, payload) {
    return fetch(url, {
      method: "POST",
      headers: {
        "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8"
      },
      body: payload.toString()
    }).then(function (response) {
      return response.json().then(function (data) {
        return { ok: response.ok, status: response.status, data: data };
      });
    });
  }

  switchButtons.forEach(function (button) {
    button.addEventListener("click", function () {
      showPanel(button.getAttribute("data-panel"));
    });
  });

  panelJumps.forEach(function (button) {
    button.addEventListener("click", function () {
      showPanel(button.getAttribute("data-panel-jump"));
    });
  });

  document.querySelectorAll("[data-toggle]").forEach(function (button) {
    button.addEventListener("click", function () {
      togglePassword(button.getAttribute("data-toggle"));
    });
  });

  loginForm.addEventListener("submit", function (event) {
    event.preventDefault();

    var username = document.getElementById("login-user").value.trim();
    var password = document.getElementById("login-pwd").value;
    var remember = document.getElementById("remember-me").checked;

    if (!username || !password) {
      setStatus("请先输入用户名和密码。", "err");
      return;
    }

    loginSubmit.disabled = true;
    loginSubmit.textContent = "登录中...";
    setStatus("正在请求服务端 /api/login ...", "");

    var body = new URLSearchParams();
    body.set("username", username);
    body.set("password", password);
    body.set("remember", remember ? "on" : "");

    postForm("/api/login", body)
      .then(function (result) {
        if (result.ok && result.data.ok) {
          setStatus("登录成功，准备跳转主页。", "ok");
          window.setTimeout(function () {
            window.location.href = result.data.redirect || "/app.html";
          }, 500);
          return;
        }

        setStatus(result.data.message || "登录失败。", "err");
      })
      .catch(function () {
        setStatus("请求失败，请确认服务端已经启动。", "err");
      })
      .finally(function () {
        loginSubmit.disabled = false;
        loginSubmit.textContent = "登录";
      });
  });

  registerForm.addEventListener("submit", function (event) {
    event.preventDefault();

    var username = document.getElementById("register-user").value.trim();
    var phone = document.getElementById("register-phone").value.trim();
    var password = document.getElementById("register-pwd").value;
    var confirmPassword = document.getElementById("register-pwd-confirm").value;

    if (!username || !password) {
      setStatus("请先填写用户名和密码。", "err");
      return;
    }

    if (password !== confirmPassword) {
      setStatus("两次输入的密码不一致。", "err");
      return;
    }

    registerSubmit.disabled = true;
    registerSubmit.textContent = "注册中...";
    setStatus("正在请求服务端 /api/register ...", "");

    var body = new URLSearchParams();
    body.set("username", username);
    body.set("phone", phone);
    body.set("password", password);

    postForm("/api/register", body)
      .then(function (result) {
        if (result.ok && result.data.ok) {
          setStatus(result.data.message || "注册成功，请直接登录。", "ok");
          registerForm.reset();
          document.getElementById("login-user").value = username;
          showPanel("login");
          return;
        }

        setStatus(result.data.message || "注册失败。", "err");
      })
      .catch(function () {
        setStatus("注册请求失败，请确认服务端和数据库已经启动。", "err");
      })
      .finally(function () {
        registerSubmit.disabled = false;
        registerSubmit.textContent = "完成注册";
      });
  });

  resetSubmit.addEventListener("click", function () {
    fetch("/api/reset", { method: "POST" })
      .then(function (response) { return response.json(); })
      .then(function (data) { setStatus(data.message, "ok"); })
      .catch(function () { setStatus("找回密码占位接口请求失败。", "err"); });
  });

  sendCodeBtn.addEventListener("click", function () {
    setStatus("模拟发送验证码成功，这说明前端脚本和服务端静态资源返回都正常。", "ok");
    countdown(sendCodeBtn);
  });

  setStatus("登录和注册按钮现在都会真正向服务端提交表单。演示账号：admin / 12345", "");
})();
