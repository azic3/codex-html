(function () {
  var galleryGrid = document.getElementById("gallery-grid");
  var template = document.getElementById("gallery-card-template");
  var lightbox = document.getElementById("lightbox");
  var lightboxImage = document.getElementById("lightbox-image");
  var lightboxTitle = document.getElementById("lightbox-title");
  var lightboxLink = document.getElementById("lightbox-link");
  var closeButton = document.getElementById("lightbox-close");
  var openFirstButton = document.getElementById("open-first-image");
  var uploadPanel = document.getElementById("upload-panel");
  var uploadForm = document.getElementById("upload-form");
  var uploadInput = document.getElementById("upload-image");
  var uploadSubmit = document.getElementById("upload-submit");
  var uploadStatus = document.getElementById("upload-status");
  var uploadMessage = document.getElementById("upload-message");
  var uploadProgress = document.getElementById("upload-progress");
  var uploadProgressBar = document.getElementById("upload-progress-bar");
  var uploadMeta = document.getElementById("upload-meta");
  var uploadRetry = document.getElementById("upload-retry");
  var heroPreview = document.getElementById("hero-preview");
  var galleryItems = [];
  var lastUploadFile = null;
  var canUpload = false;

  function openPreview(src, title) {
    lightboxImage.src = src;
    lightboxImage.alt = title;
    lightboxTitle.textContent = title;
    lightboxLink.href = src;
    lightbox.classList.add("open");
    lightbox.setAttribute("aria-hidden", "false");
  }

  function closePreview() {
    lightbox.classList.remove("open");
    lightbox.setAttribute("aria-hidden", "true");
  }

  function formatFileSize(bytes) {
    if (bytes >= 1024 * 1024) {
      return (bytes / 1024 / 1024).toFixed(1) + "MB";
    }
    if (bytes >= 1024) {
      return (bytes / 1024).toFixed(1) + "KB";
    }
    return bytes + "B";
  }

  function getDownloadName(item) {
    var source = item.path || item.url || item.title || "image";
    var name = source.split("/").pop() || item.title || "image";
    return decodeURIComponent(name);
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
      setUploadStatus("only admin can upload images", "err");
      return;
    }

    var formData = new FormData();
    var xhr = new XMLHttpRequest();

    formData.append("image", file);
    uploadSubmit.disabled = true;
    uploadSubmit.textContent = "上传中";
    setUploadStatus("正在上传图片...", "", {
      progress: 0,
      meta: file.name + " · " + formatFileSize(file.size)
    });

    xhr.upload.addEventListener("progress", function (event) {
      if (!event.lengthComputable) {
        setUploadStatus("正在上传图片...", "", {
          progress: 12,
          meta: "浏览器正在发送文件，暂时无法估算剩余进度。"
        });
        return;
      }

      var percent = Math.round((event.loaded / event.total) * 100);
      setUploadStatus("正在上传图片...", "", {
        progress: percent,
        meta: percent + "% · 已上传 " + formatFileSize(event.loaded) + " / " + formatFileSize(event.total)
      });
    });

    xhr.addEventListener("load", function () {
      var data = parseUploadResponse(xhr);
      if (xhr.status < 200 || xhr.status >= 300 || !data.ok) {
        var message = data.message || "上传失败，请检查文件格式、大小或网络连接后重试。";
        setUploadStatus(message, "err", {
          progress: 100,
          meta: "文件仍保留在选择框中，可以直接重新上传。",
          canRetry: true
        });
        return;
      }

      setUploadStatus("上传成功，正在刷新图片列表。", "ok", {
        progress: 100,
        meta: data.path ? "已保存到 " + data.path : ""
      });
      lastUploadFile = null;
      uploadForm.reset();
      loadImages();
    });

    xhr.addEventListener("error", function () {
      setUploadStatus("网络中断，图片没有上传完成。", "err", {
        progress: 100,
        meta: "请确认服务仍在运行，网络恢复后可直接重新上传。",
        canRetry: true
      });
    });

    xhr.addEventListener("timeout", function () {
      setUploadStatus("上传等待时间过长，已停止本次上传。", "err", {
        progress: 100,
        meta: "大文件可能需要更稳定的网络，稍后可直接重新上传。",
        canRetry: true
      });
    });

    xhr.addEventListener("loadend", function () {
      uploadSubmit.disabled = false;
      uploadSubmit.textContent = "上传";
    });

    xhr.open("POST", "/api/upload");
    xhr.timeout = 10 * 60 * 1000;
    xhr.send(formData);
  }

  function renderGallery(items) {
    galleryGrid.innerHTML = "";
    galleryItems = items.slice();

    if (!galleryItems.length) {
      galleryGrid.innerHTML = '<div class="gallery-empty">图片库为空。</div>';
      return;
    }

    heroPreview.src = galleryItems[0].url;

    galleryItems.forEach(function (item, index) {
      var fragment = template.content.cloneNode(true);
      var card = fragment.querySelector(".gallery-card");
      var previewButton = fragment.querySelector(".gallery-preview-btn");
      var image = fragment.querySelector("img");
      var title = fragment.querySelector("strong");
      var path = fragment.querySelector(".gallery-copy span");
      var download = fragment.querySelector(".gallery-download");

      card.setAttribute("data-full", item.url);
      card.setAttribute("data-title", item.title);
      if (index % 4 === 1) {
        card.classList.add("tall");
      }
      if (index % 4 === 3) {
        card.classList.add("accent");
      }

      image.src = item.url;
      image.alt = item.title;
      title.textContent = item.title;
      path.textContent = item.path || item.url;
      download.href = item.url;
      download.download = getDownloadName(item);

      previewButton.addEventListener("click", function () {
        openPreview(item.url, item.title);
      });

      galleryGrid.appendChild(fragment);
    });
  }

  function loadImages() {
    return fetch("/api/images")
      .then(function (response) {
        if (!response.ok) {
          throw new Error("图片列表加载失败");
        }
        return response.json();
      })
      .then(function (items) {
        renderGallery(Array.isArray(items) ? items : []);
      })
      .catch(function () {
        galleryGrid.innerHTML = '<div class="gallery-empty">图片列表加载失败，请刷新页面或确认服务正在运行。</div>';
      });
  }

  closeButton.addEventListener("click", closePreview);

  lightbox.addEventListener("click", function (event) {
    if (event.target && event.target.getAttribute("data-close") === "lightbox") {
      closePreview();
    }
  });

  document.addEventListener("keydown", function (event) {
    if (event.key === "Escape") {
      closePreview();
    }
  });

  openFirstButton.addEventListener("click", function () {
    if (!galleryItems.length) {
      return;
    }
    openPreview(galleryItems[0].url, galleryItems[0].title);
  });

  uploadInput.addEventListener("change", function () {
    if (!uploadInput.files || !uploadInput.files[0]) {
      lastUploadFile = null;
      setUploadStatus("等待选择图片。");
      return;
    }

    lastUploadFile = uploadInput.files[0];
    setUploadStatus("已选择图片，准备上传。", "", {
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
      setUploadStatus("only admin can upload images", "err");
      return;
    }

    if (!uploadInput.files || !uploadInput.files[0]) {
      setUploadStatus("请先选择一张图片。", "err");
      return;
    }

    lastUploadFile = uploadInput.files[0];

    if (lastUploadFile.size > 20 * 1024 * 1024) {
      setUploadStatus("图片不能超过 20MB，请压缩后再上传。", "err", {
        meta: lastUploadFile.name + " · " + formatFileSize(lastUploadFile.size)
      });
      return;
    }

    uploadFile(lastUploadFile);
  });

  loadCurrentUser().then(loadImages);
})();
