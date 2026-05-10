(function () {
  var videoGrid = document.getElementById("video-grid");
  var template = document.getElementById("video-card-template");
  var uploadPanel = document.getElementById("video-upload-panel");
  var uploadForm = document.getElementById("video-upload-form");
  var uploadInput = document.getElementById("upload-video");
  var uploadSubmit = document.getElementById("video-upload-submit");
  var uploadStatus = document.getElementById("video-upload-status");
  var uploadMessage = document.getElementById("video-upload-message");
  var uploadProgress = document.getElementById("video-upload-progress");
  var uploadProgressBar = document.getElementById("video-upload-progress-bar");
  var uploadMeta = document.getElementById("video-upload-meta");
  var uploadRetry = document.getElementById("video-upload-retry");
  var lastUploadFile = null;
  var canUpload = false;

  function formatFileSize(bytes) {
    if (bytes >= 1024 * 1024 * 1024) {
      return (bytes / 1024 / 1024 / 1024).toFixed(2) + "GB";
    }
    if (bytes >= 1024 * 1024) {
      return (bytes / 1024 / 1024).toFixed(1) + "MB";
    }
    if (bytes >= 1024) {
      return (bytes / 1024).toFixed(1) + "KB";
    }
    return bytes + "B";
  }

  function setUploadStatus(text, state, options) {
    options = options || {};
    uploadMessage.textContent = text;
    uploadMeta.textContent = options.meta || "";
    uploadStatus.classList.remove("ok", "err");
    if (state) {
      uploadStatus.classList.add(state);
    }

    if (typeof options.progress === "number") {
      uploadProgress.setAttribute("aria-hidden", "false");
      uploadProgressBar.style.width = Math.max(0, Math.min(100, options.progress)) + "%";
    } else {
      uploadProgress.setAttribute("aria-hidden", "true");
      uploadProgressBar.style.width = "0%";
    }

    uploadRetry.hidden = !options.canRetry;
  }

  function parseUploadResponse(xhr) {
    try {
      return xhr.responseText ? JSON.parse(xhr.responseText) : {};
    } catch (error) {
      return {};
    }
  }

  function loadCurrentUser() {
    return fetch("/api/me", { credentials: "same-origin" })
      .then(function (response) {
        if (!response.ok) {
          return null;
        }
        return response.json();
      })
      .then(function (user) {
        canUpload = !!(user && user.ok && user.role === "admin");
        if (uploadPanel) {
          uploadPanel.hidden = !canUpload;
        }
      })
      .catch(function () {
        canUpload = false;
        if (uploadPanel) {
          uploadPanel.hidden = true;
        }
      });
  }

  function uploadFile(file) {
    if (!canUpload) {
      setUploadStatus("only admin can upload videos", "err");
      return;
    }

    var formData = new FormData();
    var xhr = new XMLHttpRequest();

    formData.append("video", file);
    uploadSubmit.disabled = true;
    uploadSubmit.textContent = "上传中";
    setUploadStatus("正在上传视频...", "", {
      progress: 0,
      meta: file.name + " · " + formatFileSize(file.size)
    });

    xhr.upload.addEventListener("progress", function (event) {
      if (!event.lengthComputable) {
        setUploadStatus("正在上传视频...", "", {
          progress: 12,
          meta: "浏览器正在发送文件，暂时无法估算剩余进度。"
        });
        return;
      }

      var percent = Math.round((event.loaded / event.total) * 100);
      setUploadStatus("正在上传视频...", "", {
        progress: percent,
        meta: percent + "% · 已上传 " + formatFileSize(event.loaded) + " / " + formatFileSize(event.total)
      });
    });

    xhr.addEventListener("load", function () {
      var data = parseUploadResponse(xhr);
      if (xhr.status < 200 || xhr.status >= 300 || !data.ok) {
        var message = data.message || "上传失败，请检查视频格式、大小或网络连接后重试。";
        setUploadStatus(message, "err", {
          progress: 100,
          meta: "文件仍保留在选择框中，可以直接重新上传。",
          canRetry: true
        });
        return;
      }

      setUploadStatus("上传成功，正在刷新视频列表。", "ok", {
        progress: 100,
        meta: data.path ? "已保存到 " + data.path : ""
      });
      lastUploadFile = null;
      uploadForm.reset();
      loadVideos();
    });

    xhr.addEventListener("error", function () {
      setUploadStatus("网络中断，视频没有上传完成。", "err", {
        progress: 100,
        meta: "请确认服务仍在运行，网络恢复后可直接重新上传。",
        canRetry: true
      });
    });

    xhr.addEventListener("timeout", function () {
      setUploadStatus("上传等待时间过长，已停止本次上传。", "err", {
        progress: 100,
        meta: "大视频可能需要更稳定的网络，稍后可直接重新上传。",
        canRetry: true
      });
    });

    xhr.addEventListener("loadend", function () {
      uploadSubmit.disabled = false;
      uploadSubmit.textContent = "上传";
    });

    xhr.open("POST", "/api/upload-video");
    xhr.timeout = 30 * 60 * 1000;
    xhr.send(formData);
  }

  function renderVideos(items) {
    videoGrid.innerHTML = "";

    if (!items.length) {
      videoGrid.innerHTML = '<div class="gallery-empty">视频库为空。</div>';
      return;
    }

    items.forEach(function (item) {
      var fragment = template.content.cloneNode(true);
      var video = fragment.querySelector("video");
      var title = fragment.querySelector("strong");
      var path = fragment.querySelector(".gallery-copy span");

      video.src = item.url;
      title.textContent = item.title;
      path.textContent = item.path || item.url;

      videoGrid.appendChild(fragment);
    });
  }

  function loadVideos() {
    return fetch("/api/videos")
      .then(function (response) {
        if (!response.ok) {
          throw new Error("视频列表加载失败");
        }
        return response.json();
      })
      .then(function (items) {
        renderVideos(Array.isArray(items) ? items : []);
      })
      .catch(function () {
        videoGrid.innerHTML = '<div class="gallery-empty">视频列表加载失败，请刷新页面或确认服务正在运行。</div>';
      });
  }

  uploadInput.addEventListener("change", function () {
    if (!uploadInput.files || !uploadInput.files[0]) {
      lastUploadFile = null;
      setUploadStatus("等待选择视频。");
      return;
    }

    lastUploadFile = uploadInput.files[0];
    setUploadStatus("已选择视频，准备上传。", "", {
      meta: lastUploadFile.name + " · " + formatFileSize(lastUploadFile.size)
    });
  });

  uploadRetry.addEventListener("click", function () {
    if (lastUploadFile) {
      uploadFile(lastUploadFile);
    }
  });

  uploadForm.addEventListener("submit", function (event) {
    event.preventDefault();

    if (!canUpload) {
      setUploadStatus("only admin can upload videos", "err");
      return;
    }

    if (!uploadInput.files || !uploadInput.files[0]) {
      setUploadStatus("请先选择一个视频。", "err");
      return;
    }

    lastUploadFile = uploadInput.files[0];

    if (lastUploadFile.size > 1024 * 1024 * 1024) {
      setUploadStatus("视频不能超过 1GB，请压缩或拆分后再上传。", "err", {
        meta: lastUploadFile.name + " · " + formatFileSize(lastUploadFile.size)
      });
      return;
    }

    uploadFile(lastUploadFile);
  });

  loadCurrentUser().then(loadVideos);
})();
