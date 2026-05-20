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
  var uploadTrigger = document.getElementById("image-upload-trigger");
  var dropZone = document.getElementById("image-drop-zone");
  var uploadStatus = document.getElementById("upload-status");
  var uploadMessage = document.getElementById("upload-message");
  var uploadProgress = document.getElementById("upload-progress");
  var uploadProgressBar = document.getElementById("upload-progress-bar");
  var uploadMeta = document.getElementById("upload-meta");
  var uploadRetry = document.getElementById("upload-retry");
  var heroPreview = document.getElementById("hero-preview");
  var heroPlaceholder = document.getElementById("hero-placeholder");
  var searchInput = document.getElementById("image-search-input");
  var chips = Array.prototype.slice.call(document.querySelectorAll(".video-chip"));
  var viewBtns = Array.prototype.slice.call(document.querySelectorAll(".video-view-toggle button"));
  var countNode = document.getElementById("image-count");
  var heroCountNode = document.getElementById("hero-image-count");
  var heroSizeNode = document.getElementById("hero-image-size");
  var heroFormatNode = document.getElementById("hero-main-format");
  var totalCountNode = document.getElementById("image-total-count");
  var totalSizeNode = document.getElementById("image-total-size");
  var pngCountNode = document.getElementById("image-png-count");
  var averageWidthNode = document.getElementById("image-average-width");

  var galleryItems = [];
  var lastUploadFile = null;
  var canUpload = false;
  var activeFilter = "all";
  var imageMetaByUrl = {};

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

  function fileExt(name) {
    var match = String(name || "").toLowerCase().match(/\.([a-z0-9]+)$/);
    if (!match) {
      return "image";
    }
    return match[1] === "jpeg" ? "jpg" : match[1];
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

  function getDownloadName(item) {
    var source = item.path || item.url || item.title || "image";
    var name = source.split("/").pop() || item.title || "image";
    try {
      return decodeURIComponent(name);
    } catch (error) {
      return name;
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
        uploadPanel.hidden = !canUpload;
        uploadTrigger.hidden = !canUpload;
      })
      .catch(function () {
        canUpload = false;
        uploadPanel.hidden = true;
        uploadTrigger.hidden = true;
      });
  }

  function uploadFile(file) {
    if (!canUpload) {
      setUploadStatus("只有管理员可以上传图片。", "err");
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
        setUploadStatus(data.message || "上传失败，请检查文件格式、大小或网络连接后重试。", "err", {
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

  function filteredImages() {
    var query = searchInput.value.trim().toLowerCase();
    return galleryItems.filter(function (item) {
      var title = String(item.title || "").toLowerCase();
      var ext = fileExt(item.title || item.url);
      var matchesFilter = activeFilter === "all" || ext === activeFilter;
      var matchesSearch = !query || title.indexOf(query) !== -1;
      return matchesFilter && matchesSearch;
    });
  }

  function updateStats(items) {
    var bytes = items.reduce(function (sum, item) {
      return sum + (Number(item.size) || 0);
    }, 0);
    var pngCount = items.filter(function (item) {
      return fileExt(item.title || item.url) === "png";
    }).length;
    var knownWidths = items.map(function (item) {
      return imageMetaByUrl[item.url] && imageMetaByUrl[item.url].width;
    }).filter(Boolean);
    var averageWidth = knownWidths.length
      ? Math.round(knownWidths.reduce(function (sum, value) { return sum + value; }, 0) / knownWidths.length) + "px"
      : "--";
    var firstFormat = items[0] ? fileExt(items[0].title || items[0].url).toUpperCase() : "--";

    countNode.textContent = items.length;
    heroCountNode.textContent = items.length;
    totalCountNode.textContent = items.length;
    heroSizeNode.textContent = bytes ? formatFileSize(bytes) : "--";
    totalSizeNode.textContent = bytes ? formatFileSize(bytes) : "--";
    pngCountNode.textContent = pngCount;
    averageWidthNode.textContent = averageWidth;
    heroFormatNode.textContent = firstFormat;
  }

  function renderGallery() {
    var items = filteredImages();
    galleryGrid.innerHTML = "";
    updateStats(items);

    if (!items.length) {
      galleryGrid.innerHTML = '<div class="gallery-empty">没有找到匹配的图片。</div>';
      heroPreview.removeAttribute("src");
      heroPlaceholder.hidden = false;
      return;
    }

    heroPreview.src = items[0].url;
    heroPreview.alt = items[0].title || "图片预览";
    heroPlaceholder.hidden = true;

    items.forEach(function (item, index) {
      var fragment = template.content.cloneNode(true);
      var image = fragment.querySelector("img");
      var title = fragment.querySelector(".redesign-image-name");
      var path = fragment.querySelector(".redesign-image-path");
      var size = fragment.querySelector(".redesign-image-size");
      var format = fragment.querySelector(".img-fmt");
      var isNew = fragment.querySelector(".img-new");
      var resolution = fragment.querySelector(".img-resolution");
      var previewButton = fragment.querySelector(".preview-action");
      var copyButton = fragment.querySelector(".copy-action");
      var downloadButtons = fragment.querySelectorAll(".download-action, .image-mini-action");
      var ext = fileExt(item.title || item.url).toUpperCase();

      image.src = item.url;
      image.alt = item.title || "";
      title.textContent = item.title || item.url;
      path.textContent = displayFileName(item);
      size.textContent = formatFileSize(item.size);
      format.textContent = ext;
      isNew.hidden = index > 1;
      resolution.textContent = imageMetaByUrl[item.url]
        ? imageMetaByUrl[item.url].width + "x" + imageMetaByUrl[item.url].height
        : "--";

      downloadButtons.forEach(function (button) {
        button.href = item.url;
        button.download = getDownloadName(item);
      });

      previewButton.addEventListener("click", function () {
        openPreview(item.url, item.title || item.url);
      });

      copyButton.addEventListener("click", function () {
        var value = item.path || item.url;
        if (navigator.clipboard && navigator.clipboard.writeText) {
          navigator.clipboard.writeText(value);
        }
        copyButton.textContent = "已复制";
        window.setTimeout(function () {
          copyButton.textContent = "复制";
        }, 1200);
      });

      image.addEventListener("load", function () {
        imageMetaByUrl[item.url] = {
          width: image.naturalWidth,
          height: image.naturalHeight
        };
        resolution.textContent = image.naturalWidth + "x" + image.naturalHeight;
        updateStats(filteredImages());
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
        galleryItems = Array.isArray(items) ? items : [];
        renderGallery();
      })
      .catch(function () {
        galleryGrid.innerHTML = '<div class="gallery-empty">图片列表加载失败，请刷新页面或确认服务正在运行。</div>';
        updateStats([]);
      });
  }

  function chooseFile(file) {
    if (!file) {
      lastUploadFile = null;
      setUploadStatus("等待选择图片。");
      return;
    }

    lastUploadFile = file;
    setUploadStatus("已选择图片，准备上传。", "", {
      meta: lastUploadFile.name + " · " + formatFileSize(lastUploadFile.size)
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
    var items = filteredImages();
    if (!items.length) {
      return;
    }
    openPreview(items[0].url, items[0].title || items[0].url);
  });

  uploadTrigger.addEventListener("click", function () {
    uploadInput.click();
  });

  uploadInput.addEventListener("change", function () {
    chooseFile(uploadInput.files && uploadInput.files[0]);
  });

  uploadRetry.addEventListener("click", function () {
    if (lastUploadFile) {
      uploadFile(lastUploadFile);
    }
  });

  uploadForm.addEventListener("submit", function (event) {
    event.preventDefault();

    if (!canUpload) {
      setUploadStatus("只有管理员可以上传图片。", "err");
      return;
    }

    if (!lastUploadFile && uploadInput.files && uploadInput.files[0]) {
      lastUploadFile = uploadInput.files[0];
    }

    if (!lastUploadFile) {
      setUploadStatus("请先选择一张图片。", "err");
      return;
    }

    if (lastUploadFile.size > 20 * 1024 * 1024) {
      setUploadStatus("图片不能超过 20MB，请压缩后再上传。", "err", {
        meta: lastUploadFile.name + " · " + formatFileSize(lastUploadFile.size)
      });
      return;
    }

    uploadFile(lastUploadFile);
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
      renderGallery();
    });
  });

  viewBtns.forEach(function (btn) {
    btn.addEventListener("click", function () {
      viewBtns.forEach(function (item) {
        item.classList.remove("active");
      });
      btn.classList.add("active");
      galleryGrid.classList.toggle("list-view", btn.dataset.view === "list");
    });
  });

  searchInput.addEventListener("input", renderGallery);

  loadCurrentUser().then(loadImages);
})();
