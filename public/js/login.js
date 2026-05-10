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
  var registerSendCodeBtn = document.getElementById("register-send-code-btn");

  function showPanel(name) {
    switchButtons.forEach(function (button) {
      button.classList.toggle("active", button.getAttribute("data-panel") === name);
    });

    panels.forEach(function (panel) {
      panel.classList.toggle("active", panel.id === "panel-" + name);
    });

    if (name === "reset") {
      setStatus("请输入手机号、邮箱验证码和新密码完成密码重置。", "");
    } else {
      setStatus("", "");
    }
  }

  function setStatus(text, state) {
    statusBox.textContent = text;
    statusBox.classList.remove("ok", "err");
    if (state) {
      statusBox.classList.add(state);
    }
  }

  function isValidPhone(phone) {
    return /^1\d{10}$/.test(phone);
  }

  function isValidEmail(email) {
    return /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email);
  }

  function togglePassword(targetId) {
    var input = document.getElementById(targetId);
    if (!input) {
      return;
    }
    input.type = input.type === "password" ? "text" : "password";
  }

  function countdown(button) {
    var seconds = 30;
    var origin = button.textContent;
    button.disabled = true;
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
      setStatus("请先输入手机号和密码。", "err");
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

  registerSendCodeBtn.addEventListener("click", function () {
    var email = document.getElementById("register-email").value.trim();

    if (!isValidEmail(email)) {
      setStatus("请先输入正确的邮箱地址。", "err");
      return;
    }

    registerSendCodeBtn.disabled = true;
    setStatus("正在发送邮箱验证码...", "");

    var body = new URLSearchParams();
    body.set("email", email);

    postForm("/api/send-email-code", body)
      .then(function (result) {
        if (result.ok && result.data.ok) {
          setStatus(result.data.message || "验证码已发送，请查看邮箱。", "ok");
          countdown(registerSendCodeBtn);
          return;
        }

        registerSendCodeBtn.disabled = false;
        setStatus(result.data.message || "验证码发送失败。", "err");
      })
      .catch(function () {
        registerSendCodeBtn.disabled = false;
        setStatus("验证码请求失败，请确认服务端已经启动。", "err");
      });
  });

  registerForm.addEventListener("submit", function (event) {
    event.preventDefault();

    var phone = document.getElementById("register-phone").value.trim();
    var email = document.getElementById("register-email").value.trim();
    var emailCode = document.getElementById("register-email-code").value.trim();
    var password = document.getElementById("register-pwd").value;
    var confirmPassword = document.getElementById("register-pwd-confirm").value;

    if (!isValidPhone(phone)) {
      setStatus("请输入正确的 11 位手机号。", "err");
      return;
    }

    if (!isValidEmail(email)) {
      setStatus("请输入正确的邮箱地址。", "err");
      return;
    }

    if (!/^\d{6}$/.test(emailCode)) {
      setStatus("请输入 6 位邮箱验证码。", "err");
      return;
    }

    if (!password) {
      setStatus("请填写密码。", "err");
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
    body.set("phone", phone);
    body.set("email", email);
    body.set("email_code", emailCode);
    body.set("password", password);

    postForm("/api/register", body)
      .then(function (result) {
        if (result.ok && result.data.ok) {
          setStatus(result.data.message || "注册成功，请直接登录。", "ok");
          registerForm.reset();
          document.getElementById("login-user").value = phone;
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
    var phone = document.getElementById("reset-phone").value.trim();
    var email = document.getElementById("reset-email").value.trim();
    var code = document.getElementById("reset-code").value.trim();
    var password = document.getElementById("reset-password").value;
    var passwordConfirm = document.getElementById("reset-password-confirm").value;

    if (!isValidPhone(phone)) {
      setStatus("请输入正确的手机号。", "err");
      return;
    }

    if (!isValidEmail(email)) {
      setStatus("请输入正确的邮箱地址。", "err");
      return;
    }

    if (!/^\d{6}$/.test(code)) {
      setStatus("请输入 6 位邮箱验证码。", "err");
      return;
    }

    if (password.length < 4) {
      setStatus("新密码至少需要 4 个字符。", "err");
      return;
    }

    if (password !== passwordConfirm) {
      setStatus("两次输入的新密码不一致。", "err");
      return;
    }

    resetSubmit.disabled = true;
    resetSubmit.textContent = "正在重置...";

    var body = new URLSearchParams();
    body.set("phone", phone);
    body.set("email", email);
    body.set("email_code", code);
    body.set("password", password);

    postForm("/api/reset", body)
      .then(function (result) {
        if (result.ok && result.data.ok) {
          document.getElementById("reset-phone").value = "";
          document.getElementById("reset-email").value = "";
          document.getElementById("reset-code").value = "";
          document.getElementById("reset-password").value = "";
          document.getElementById("reset-password-confirm").value = "";
          showPanel("login");
          setStatus(result.data.message || "密码已重置，请使用新密码登录。", "ok");
          return;
        }

        setStatus(result.data.message || "密码重置失败。", "err");
      })
      .catch(function () {
        setStatus("密码重置请求失败，请确认服务端已经启动。", "err");
      })
      .finally(function () {
        resetSubmit.disabled = false;
        resetSubmit.textContent = "重置密码";
      });
  });

  sendCodeBtn.addEventListener("click", function () {
    var email = document.getElementById("reset-email").value.trim();

    if (!isValidEmail(email)) {
      setStatus("请先输入正确的邮箱地址。", "err");
      return;
    }

    sendCodeBtn.disabled = true;
    setStatus("正在发送找回密码验证码...", "");

    var body = new URLSearchParams();
    body.set("email", email);

    postForm("/api/send-email-code", body)
      .then(function (result) {
        if (result.ok && result.data.ok) {
          setStatus(result.data.message || "验证码已发送，请查看邮箱。", "ok");
          countdown(sendCodeBtn);
          return;
        }

        sendCodeBtn.disabled = false;
        setStatus(result.data.message || "验证码发送失败。", "err");
      })
      .catch(function () {
        sendCodeBtn.disabled = false;
        setStatus("验证码请求失败，请确认服务端已经启动。", "err");
      });
  });

  setStatus("手机号注册和找回密码已接入邮箱验证码。演示账号：admin / 12345", "");
})();
