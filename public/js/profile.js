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
  var favoritesGrid = document.getElementById("favorites-grid");
  var favoritesCount = document.getElementById("favorites-count");
  var favoritesStatus = document.getElementById("favorites-status");
  var adminOnlyLinks = Array.prototype.slice.call(document.querySelectorAll("[data-admin-only]"));

  var favoritePage = 1;
  var favoritePageSize = 12;
  var favoriteHasMore = true;
  var favoriteLoading = false;
  var favoriteItems = [];

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

  function formatFileSize(bytes) {
    if (!bytes && bytes !== 0) {
      return "--";
    }
    if (bytes >= 1024 * 1024) {
      return (bytes / 1024 / 1024).toFixed(1) + "MB";
    }
    if (bytes >= 1024) {
      return (bytes / 1024).toFixed(1) + "KB";
    }
    return bytes + "B";
  }

  function formatCount(value) {
    var number = Number(value) || 0;
    if (number >= 10000) {
      return (number / 10000).toFixed(number >= 100000 ? 0 : 1).replace(/\.0$/, "") + "w";
    }
    if (number >= 1000) {
      return (number / 1000).toFixed(number >= 10000 ? 0 : 1).replace(/\.0$/, "") + "k";
    }
    return String(number);
  }

  function displayFileName(item) {
    var value = item.filename || item.title || item.path || item.url || "";
    try {
      return decodeURIComponent(String(value).split("?")[0].split("#")[0].split("/").pop());
    } catch (error) {
      return value;
    }
  }

  function setFavoritesStatus(text, state) {
    favoritesStatus.textContent = text || "";
    favoritesStatus.classList.remove("ok", "err");
    if (state) {
      favoritesStatus.classList.add(state);
    }
  }

  function renderFavoriteCards() {
    favoritesGrid.innerHTML = "";
    favoritesCount.textContent = favoriteItems.length;

    if (!favoriteItems.length) {
      favoritesGrid.innerHTML = [
        '<div class="favorites-empty">',
        '<strong>还没有收藏任何图片。</strong>',
        '<a href="/app.html">去图片库看看</a>',
        '</div>'
      ].join("");
      return;
    }

    favoriteItems.forEach(function (item) {
      var card = document.createElement("article");
      var imageWrap = document.createElement("a");
      var image = document.createElement("img");
      var body = document.createElement("div");
      var title = document.createElement("strong");
      var meta = document.createElement("div");
      var stats = document.createElement("div");
      var remove = document.createElement("button");

      card.className = "favorite-card";
      imageWrap.className = "favorite-thumb";
      imageWrap.href = "/app.html?image=" + encodeURIComponent(item.id);
      image.loading = "lazy";
      image.decoding = "async";
      image.src = item.url || item.path || "";
      image.alt = item.title || item.filename || "收藏图片";
      imageWrap.appendChild(image);

      body.className = "favorite-card-body";
      title.textContent = displayFileName(item);
      meta.className = "favorite-meta";
      meta.textContent = "收藏于 " + (item.favorited_at || "--") + " · " + formatFileSize(item.size);
      stats.className = "favorite-stats";
      stats.innerHTML = [
        "<span>赞 " + formatCount(item.like_count) + "</span>",
        "<span>评 " + formatCount(item.comment_count) + "</span>",
        "<span>藏 " + formatCount(item.favorite_count) + "</span>"
      ].join("");

      remove.className = "favorite-remove";
      remove.type = "button";
      remove.textContent = "取消收藏";
      remove.addEventListener("click", function () {
        removeFavorite(item, remove);
      });

      body.appendChild(title);
      body.appendChild(meta);
      body.appendChild(stats);
      body.appendChild(remove);
      card.appendChild(imageWrap);
      card.appendChild(body);
      favoritesGrid.appendChild(card);
    });
  }

  function loadFavorites(reset) {
    if (favoriteLoading || (!reset && !favoriteHasMore)) {
      return Promise.resolve();
    }

    favoriteLoading = true;
    if (reset) {
      favoritePage = 1;
      favoriteHasMore = true;
      setFavoritesStatus("正在加载收藏图片。");
    }

    return fetch("/api/me/favorites?page=" + favoritePage + "&limit=" + favoritePageSize, {
      credentials: "same-origin"
    })
      .then(function (response) {
        if (response.status === 401) {
          window.location.href = "/login.html";
          return null;
        }
        if (!response.ok) {
          throw new Error("favorites load failed");
        }
        return response.json();
      })
      .then(function (data) {
        if (!data) {
          return;
        }
        var items = Array.isArray(data.items) ? data.items : [];
        favoriteItems = reset ? items : favoriteItems.concat(items);
        favoriteHasMore = !!data.has_more;
        favoritePage += 1;
        renderFavoriteCards();
        setFavoritesStatus(favoriteHasMore ? "继续向下滚动加载更多收藏。" : "");
      })
      .catch(function () {
        setFavoritesStatus("收藏列表加载失败，请刷新页面重试。", "err");
      })
      .then(function () {
        favoriteLoading = false;
      });
  }

  function removeFavorite(item, button) {
    if (!item || !item.id) {
      return;
    }

    button.disabled = true;
    fetch("/api/images/" + encodeURIComponent(item.id) + "/favorite", {
      method: "DELETE",
      credentials: "same-origin"
    })
      .then(function (response) {
        if (!response.ok) {
          throw new Error("remove favorite failed");
        }
        return response.json();
      })
      .then(function () {
        favoriteItems = favoriteItems.filter(function (candidate) {
          return candidate.id !== item.id;
        });
        renderFavoriteCards();
        setFavoritesStatus(favoriteItems.length ? "已取消收藏。" : "");
      })
      .catch(function () {
        button.disabled = false;
        setFavoritesStatus("取消收藏失败，请稍后再试。", "err");
      });
  }

  function renderUser(user) {
    var isAdmin = user.role === "admin";
    var roleLabel = isAdmin ? "管理员" : "普通用户";

    avatar.textContent = initials(user.username);
    rolePill.textContent = roleLabel;
    usernameTitle.textContent = "当前账号：" + user.username;
    accountName.textContent = user.username;
    accountRole.textContent = roleLabel;
    adminOnlyLinks.forEach(function (link) {
      link.hidden = !isAdmin;
    });

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
        loadFavorites(true);
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

  window.addEventListener("scroll", function () {
    var nearBottom = window.innerHeight + window.scrollY >= document.documentElement.scrollHeight - 360;
    if (nearBottom) {
      loadFavorites(false);
    }
  }, { passive: true });

  loadCurrentUser();
})();
