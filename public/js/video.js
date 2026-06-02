(function () {
  var videoGrid = document.getElementById("video-grid");
  var template = document.getElementById("video-card-template");
  var uploadPanel = document.getElementById("video-upload-panel");
  var uploadForm = document.getElementById("video-upload-form");
  var uploadInput = document.getElementById("upload-video");
  var uploadSubmit = document.getElementById("video-upload-submit");
  var uploadTrigger = document.getElementById("video-upload-trigger");
  var dropZone = document.getElementById("video-drop-zone");
  var uploadStatus = document.getElementById("video-upload-status");
  var uploadMessage = document.getElementById("video-upload-message");
  var uploadProgress = document.getElementById("video-upload-progress");
  var uploadProgressBar = document.getElementById("video-upload-progress-bar");
  var uploadMeta = document.getElementById("video-upload-meta");
  var uploadRetry = document.getElementById("video-upload-retry");
  var searchInput = document.getElementById("video-search-input");
  var chips = Array.prototype.slice.call(document.querySelectorAll(".video-chip"));
  var viewBtns = Array.prototype.slice.call(document.querySelectorAll(".video-view-toggle button"));
  var countNode = document.getElementById("video-count");
  var totalCountNode = document.getElementById("video-total-count");
  var totalSizeNode = document.getElementById("video-total-size");
  var totalDurationNode = document.getElementById("video-total-duration");

  var lastUploadFile = null;
  var canUpload = false;
  var isLoggedIn = false;
  var allVideos = [];
  var activeFilter = "all";
  var durationByUrl = {};
  var videoChunkSize = 2 * 1024 * 1024;

  function redirectToLogin() {
    window.location.href = "/login.html?next=" + encodeURIComponent(window.location.pathname + window.location.search);
  }

  function formatFileSize(bytes) {
    if (!bytes && bytes !== 0) {
      return "--";
    }
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

  function formatDuration(seconds) {
    if (!Number.isFinite(seconds) || seconds <= 0) {
      return "--";
    }
    var whole = Math.round(seconds);
    var minutes = Math.floor(whole / 60);
    var remaining = whole % 60;
    return minutes + ":" + String(remaining).padStart(2, "0");
  }

  function fileExt(name) {
    var match = String(name || "").toLowerCase().match(/\.([a-z0-9]+)$/);
    return match ? match[1] : "video";
  }

  function displayFileName(item) {
    var value = item.title || item.path || item.url || "";
    var filename = String(value).split("?")[0].split("#")[0].split("/").pop();

    try {
      return decodeURIComponent(filename);
    } catch (error) {
      return filename;
    }
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

  function createUploadId(file) {
    return [
      "video",
      Date.now(),
      Math.random().toString(36).slice(2),
      String(file.name || "upload").replace(/[^a-zA-Z0-9_.-]/g, "_")
    ].join("-");
  }

  function sendVideoChunk(file, uploadId, chunkIndex, totalChunks) {
    return new Promise(function (resolve, reject) {
      var start = chunkIndex * videoChunkSize;
      var end = Math.min(file.size, start + videoChunkSize);
      var xhr = new XMLHttpRequest();

      xhr.addEventListener("load", function () {
        var data = parseUploadResponse(xhr);
        if (xhr.status < 200 || xhr.status >= 300 || !data.ok) {
          reject(new Error(data.message || "video chunk upload failed"));
          return;
        }
        resolve(data);
      });

      xhr.addEventListener("error", function () {
        reject(new Error("network interrupted"));
      });

      xhr.addEventListener("timeout", function () {
        reject(new Error("chunk upload timeout"));
      });

      xhr.open("POST", "/api/upload-video-chunk");
      xhr.timeout = 2 * 60 * 1000;
      xhr.setRequestHeader("Content-Type", "application/octet-stream");
      xhr.setRequestHeader("X-Upload-Id", uploadId);
      xhr.setRequestHeader("X-File-Name", encodeURIComponent(file.name || "video"));
      xhr.setRequestHeader("X-Chunk-Index", String(chunkIndex));
      xhr.setRequestHeader("X-Total-Chunks", String(totalChunks));
      xhr.setRequestHeader("X-File-Size", String(file.size));
      xhr.send(file.slice(start, end));
    });
  }

  function uploadChunkedFile(file) {
    if (!canUpload) {
      setUploadStatus("only admin can upload videos", "err");
      return;
    }

    var uploadId = createUploadId(file);
    var totalChunks = Math.ceil(file.size / videoChunkSize);
    var uploadedBytes = 0;
    var finalPath = "";
    var chain = Promise.resolve();

    uploadSubmit.disabled = true;
    uploadSubmit.textContent = "上传中";
    setUploadStatus("正在分片上传视频...", "", {
      progress: 0,
      meta: file.name + " - 0 / " + totalChunks + " chunks"
    });

    for (var i = 0; i < totalChunks; i += 1) {
      (function (chunkIndex) {
        chain = chain.then(function () {
          return sendVideoChunk(file, uploadId, chunkIndex, totalChunks).then(function (data) {
            uploadedBytes = Math.min(file.size, (chunkIndex + 1) * videoChunkSize);
            if (data.complete && data.path) {
              finalPath = data.path;
            }
            var percent = Math.round((uploadedBytes / file.size) * 100);
            setUploadStatus("正在分片上传视频...", "", {
              progress: percent,
              meta: (chunkIndex + 1) + " / " + totalChunks + " chunks - " +
                formatFileSize(uploadedBytes) + " / " + formatFileSize(file.size)
            });
          });
        });
      })(i);
    }

    chain
      .then(function () {
        setUploadStatus("上传成功，正在刷新视频列表。", "ok", {
          progress: 100,
          meta: finalPath ? "已保存到 " + finalPath : ""
        });
        lastUploadFile = null;
        uploadForm.reset();
        loadVideos();
      })
      .catch(function (error) {
        setUploadStatus(error.message || "上传失败，请检查视频格式、大小或网络连接后重试。", "err", {
          progress: Math.round((uploadedBytes / file.size) * 100),
          meta: "文件仍保留在选择框中，可以直接重新上传。",
          canRetry: true
        });
      })
      .then(function () {
        uploadSubmit.disabled = false;
        uploadSubmit.textContent = "上传";
      });
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
        isLoggedIn = !!(user && user.ok);
        canUpload = !!(isLoggedIn && user.role === "admin");
        uploadPanel.hidden = !canUpload;
        uploadTrigger.hidden = !canUpload;
      })
      .catch(function () {
        isLoggedIn = false;
        canUpload = false;
        uploadPanel.hidden = true;
        uploadTrigger.hidden = true;
      });
  }

  function uploadFile(file) {
    if (!canUpload) {
      setUploadStatus("只有管理员可以上传视频。", "err");
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
        setUploadStatus(data.message || "上传失败，请检查视频格式、大小或网络连接后重试。", "err", {
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

  function updateStats(items) {
    var knownBytes = items.reduce(function (sum, item) {
      return sum + (Number(item.size) || 0);
    }, 0);
    var knownDuration = items.reduce(function (sum, item) {
      return sum + (Number(durationByUrl[item.url]) || 0);
    }, 0);

    countNode.textContent = items.length;
    totalCountNode.textContent = items.length;
    totalSizeNode.textContent = knownBytes ? formatFileSize(knownBytes) : "--";
    totalDurationNode.textContent = knownDuration ? formatDuration(knownDuration) : "--";
  }

  function filteredVideos() {
    var query = searchInput.value.trim().toLowerCase();
    return allVideos.filter(function (item) {
      var title = String(item.title || "").toLowerCase();
      var ext = fileExt(item.title || item.url);
      var matchesFilter = activeFilter === "all" || ext === activeFilter;
      var matchesSearch = !query || title.indexOf(query) !== -1;
      return matchesFilter && matchesSearch;
    });
  }

  function renderVideos() {
    var items = filteredVideos();
    videoGrid.innerHTML = "";
    updateStats(items);

    if (!items.length) {
      videoGrid.innerHTML = '<div class="gallery-empty">没有找到匹配的视频。</div>';
      return;
    }

    items.forEach(function (item, index) {
      var fragment = template.content.cloneNode(true);
      var card = fragment.querySelector(".redesign-video-card");
      var video = fragment.querySelector("video");
      var title = fragment.querySelector(".redesign-video-name");
      var path = fragment.querySelector(".redesign-video-path");
      var extBadge = fragment.querySelector(".format-badge");
      var newBadge = fragment.querySelector(".new-badge");
      var duration = fragment.querySelector(".duration-badge");
      var size = fragment.querySelector(".redesign-video-size");
      var download = fragment.querySelector(".icon-btn");
      var ext = fileExt(item.title || item.url).toUpperCase();

      card.dataset.ext = ext.toLowerCase();
      video.src = item.url;
      title.textContent = item.title || item.url;
      path.textContent = displayFileName(item);
      extBadge.textContent = ext;
      newBadge.hidden = index > 1;
      duration.textContent = formatDuration(durationByUrl[item.url]);
      size.textContent = formatFileSize(item.size);
      download.href = item.url;
      download.addEventListener("click", function (event) {
        if (!isLoggedIn) {
          event.preventDefault();
          redirectToLogin();
        }
      });

      video.addEventListener("loadedmetadata", function () {
        durationByUrl[item.url] = video.duration;
        duration.textContent = formatDuration(video.duration);
        updateStats(filteredVideos());
      });

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
        allVideos = Array.isArray(items) ? items : [];
        renderVideos();
      })
      .catch(function () {
        videoGrid.innerHTML = '<div class="gallery-empty">视频列表加载失败，请刷新页面或确认服务正在运行。</div>';
        updateStats([]);
      });
  }

  function chooseFile(file) {
    if (!file) {
      lastUploadFile = null;
      setUploadStatus("等待选择视频。");
      return;
    }

    lastUploadFile = file;
    setUploadStatus("已选择视频，准备上传。", "", {
      meta: lastUploadFile.name + " · " + formatFileSize(lastUploadFile.size)
    });
  }

  uploadTrigger.addEventListener("click", function () {
    uploadInput.click();
  });

  uploadInput.addEventListener("change", function () {
    chooseFile(uploadInput.files && uploadInput.files[0]);
  });

  uploadRetry.addEventListener("click", function () {
    if (lastUploadFile) {
      uploadChunkedFile(lastUploadFile);
    }
  });

  uploadForm.addEventListener("submit", function (event) {
    event.preventDefault();

    if (!canUpload) {
      setUploadStatus("只有管理员可以上传视频。", "err");
      return;
    }

    if (!lastUploadFile && uploadInput.files && uploadInput.files[0]) {
      lastUploadFile = uploadInput.files[0];
    }

    if (!lastUploadFile) {
      setUploadStatus("请先选择一个视频。", "err");
      return;
    }

    if (lastUploadFile.size > 1024 * 1024 * 1024) {
      setUploadStatus("视频不能超过 1GB，请压缩或拆分后再上传。", "err", {
        meta: lastUploadFile.name + " · " + formatFileSize(lastUploadFile.size)
      });
      return;
    }

    uploadChunkedFile(lastUploadFile);
  });

  ["dragenter", "dragover"].forEach(function (eventName) {
    dropZone.addEventListener(eventName, function (event) {
      event.preventDefault();
      dropZone.classList.add("dragging");
    });
  });

  ["dragleave", "drop"].forEach(function (eventName) {
    dropZone.addEventListener(eventName, function (event) {
      event.preventDefault();
      dropZone.classList.remove("dragging");
    });
  });

  dropZone.addEventListener("drop", function (event) {
    var file = event.dataTransfer && event.dataTransfer.files && event.dataTransfer.files[0];
    if (file) {
      uploadInput.files = event.dataTransfer.files;
      chooseFile(file);
    }
  });

  chips.forEach(function (chip) {
    chip.addEventListener("click", function () {
      chips.forEach(function (item) {
        item.classList.remove("active");
      });
      chip.classList.add("active");
      activeFilter = chip.dataset.filter || "all";
      renderVideos();
    });
  });

  viewBtns.forEach(function (btn) {
    btn.addEventListener("click", function () {
      viewBtns.forEach(function (item) {
        item.classList.remove("active");
      });
      btn.classList.add("active");
      videoGrid.classList.toggle("list-view", btn.dataset.view === "list");
    });
  });

  searchInput.addEventListener("input", renderVideos);

  loadCurrentUser().then(loadVideos);
})();
