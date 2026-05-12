(function () {
  var avatar = document.getElementById("profile-avatar");
  var rolePill = document.getElementById("profile-role");
  var usernameTitle = document.getElementById("profile-username");
  var permissionText = document.getElementById("profile-permission");
  var accountName = document.getElementById("account-name");
  var accountRole = document.getElementById("account-role");
  var permissionList = document.getElementById("permission-list");
  var logoutButton = document.getElementById("logout-button");
  var statusBox = document.getElementById("profile-status");

  function setStatus(text, state) {
    statusBox.textContent = text || "";
    statusBox.classList.remove("show", "ok", "err");
    if (text) {
      statusBox.classList.add("show");
    }
    if (state) {
      statusBox.classList.add(state);
    }
  }

  function initials(username) {
    if (!username) {
      return "XC";
    }
    if (username === "admin") {
      return "AD";
    }
    return username.slice(-2).toUpperCase();
  }

  function renderUser(user) {
    var isAdmin = user.role === "admin";
    var roleLabel = isAdmin ? "管理员" : "普通用户";

    avatar.textContent = initials(user.username);
    rolePill.textContent = roleLabel;
    usernameTitle.textContent = "当前账号：" + user.username;
    accountName.textContent = user.username;
    accountRole.textContent = roleLabel;

    if (isAdmin) {
      permissionText.textContent = "你可以上传、浏览和下载图片与视频资源。";
      permissionList.innerHTML = [
        "<li>可以上传图片到图片库。</li>",
        "<li>可以上传视频到视频库。</li>",
        "<li>可以浏览和下载已有图片、视频。</li>"
      ].join("");
      return;
    }

    permissionText.textContent = "你可以浏览和下载图片、视频，但不能上传资源。";
    permissionList.innerHTML = [
      "<li>可以浏览图片库和视频库。</li>",
      "<li>可以下载已有图片资源。</li>",
      "<li>不能上传图片或视频。</li>"
    ].join("");
  }

  function loadCurrentUser() {
    return fetch("/api/me", { credentials: "same-origin" })
      .then(function (response) {
        if (!response.ok) {
          window.location.href = "/login.html";
          return null;
        }
        return response.json();
      })
      .then(function (user) {
        if (!user) {
          return;
        }
        if (!user.ok) {
          window.location.href = user.redirect || "/login.html";
          return;
        }
        renderUser(user);
      })
      .catch(function () {
        setStatus("账号信息加载失败，请刷新页面或重新登录。", "err");
      });
  }

  logoutButton.addEventListener("click", function () {
    logoutButton.disabled = true;
    setStatus("正在退出登录...", "");

    fetch("/api/logout", {
      method: "POST",
      credentials: "same-origin"
    })
      .then(function (response) {
        return response.json().catch(function () {
          return {};
        });
      })
      .then(function (data) {
        setStatus(data.message || "已退出登录。", "ok");
        window.setTimeout(function () {
          window.location.href = data.redirect || "/login.html";
        }, 400);
      })
      .catch(function () {
        setStatus("退出登录失败，请稍后再试。", "err");
        logoutButton.disabled = false;
      });
  });

  loadCurrentUser();
})();
