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
  var commentDrawer = document.getElementById("comment-drawer");
  var commentDrawerTitle = document.getElementById("comment-drawer-title");
  var commentDrawerClose = document.getElementById("comment-drawer-close");
  var commentCountLabel = document.getElementById("comment-count-label");
  var commentList = document.getElementById("comment-list");
  var commentInput = document.getElementById("comment-input");
  var commentCharCount = document.getElementById("comment-char-count");
  var commentSubmit = document.getElementById("comment-submit");

  var galleryItems = [];
  var imagePage = 1;
  var imagePageSize = 12;
  var imageHasMore = true;
  var imageLoading = false;
  var lastUploadFile = null;
  var canUpload = false;
  var isLoggedIn = false;
  var activeFilter = "all";
  var imageMetaByUrl = {};
  var imageLazyObserver = null;
  var activeCommentImage = null;
  var commentPage = 1;
  var commentHasMore = false;
  var commentLoading = false;
  var pendingPreviewImageId = new URLSearchParams(window.location.search).get("image");
  var shareAutoOpened = false;

  if ("IntersectionObserver" in window) {
    imageLazyObserver = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) {
        if (!entry.isIntersecting) {
          return;
        }
        var image = entry.target;
        image.src = image.dataset.src;
        imageLazyObserver.unobserve(image);
      });
    }, { rootMargin: "260px 0px" });
  }

  function redirectToLogin() {
    window.location.href = "/login.html?next=" + encodeURIComponent(window.location.pathname + window.location.search);
  }

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

  function mediaFallbackUrl(url) {
    if (!url || url.indexOf("/media/images/") !== 0) {
      return "";
    }
    return "/images/" + url.slice("/media/images/".length);
  }

  function currentImageUrl(item) {
    return item.__activeUrl || item.url || item.path;
  }

  function currentThumbnailUrl(item) {
    return item.__activeThumbUrl || item.thumb_url || item.thumbnail_url || currentImageUrl(item);
  }

  function setImageSource(image, item, url, thumbnail) {
    if (thumbnail) {
      item.__activeThumbUrl = url;
    } else {
      item.__activeUrl = url;
    }
    if (imageLazyObserver) {
      image.dataset.src = url;
      imageLazyObserver.observe(image);
    } else {
      image.src = url;
    }
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

  function updateReactionButton(button, active, count, activeText, inactiveText) {
    button.classList.toggle("active", !!active);
    button.setAttribute("aria-pressed", active ? "true" : "false");
    button.querySelector(".action-label").textContent = active ? activeText : inactiveText;
    button.querySelector(".action-count").textContent = formatCount(count);
  }

  function requestImageReaction(item, action, active) {
    if (!item.id) {
      return Promise.reject(new Error("image id is missing"));
    }

    return fetch("/api/images/" + encodeURIComponent(item.id) + "/" + action, {
      method: active ? "POST" : "DELETE",
      credentials: "same-origin"
    }).then(function (response) {
      if (response.status === 401) {
        redirectToLogin();
        throw new Error("login required");
      }
      if (!response.ok) {
        throw new Error("image action failed");
      }
      return response.json();
    });
  }

  function tryOpenRequestedImage() {
    if (!pendingPreviewImageId) {
      return;
    }

    var target = galleryItems.find(function (item) {
      return String(item.id || "") === String(pendingPreviewImageId);
    });
    if (target) {
      pendingPreviewImageId = "";
      openPreview(currentImageUrl(target), target.title || target.url);
      return;
    }

    if (imageHasMore && !imageLoading) {
      loadImages(false);
    }
  }

  function updateCommentButton(button, count) {
    button.querySelector(".action-count").textContent = formatCount(count);
  }

  function getShareUrl(item) {
    return window.location.origin + "/app.html?image=" + encodeURIComponent(item.id);
  }

  function showToast(message) {
    var toast = document.getElementById("toast");
    if (!toast) {
      return;
    }
    if (toast.__timer) {
      clearTimeout(toast.__timer);
    }
    toast.textContent = message;
    toast.classList.add("show");
    toast.__timer = setTimeout(function () {
      toast.classList.remove("show");
      toast.__timer = null;
    }, 2000);
  }

  function legacyCopy(text) {
    var textarea = document.createElement("textarea");
    textarea.value = text;
    textarea.style.position = "fixed";
    textarea.style.opacity = "0";
    document.body.appendChild(textarea);
    textarea.select();
    try {
      document.execCommand("copy");
      showToast("链接已复制");
    } catch (e) {
      showToast("复制失败，请手动复制链接");
    }
    document.body.removeChild(textarea);
  }

  function fallbackCopy(text) {
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(text).then(function () {
        showToast("链接已复制");
      }).catch(function () {
        legacyCopy(text);
      });
    } else {
      legacyCopy(text);
    }
  }

  function shareImage(item) {
    if (!item.id) {
      return;
    }

    var shareUrl = getShareUrl(item);
    var title = item.title || item.url || "分享图片";

    if (navigator.share) {
      navigator.share({
        title: title,
        url: shareUrl
      }).catch(function (err) {
        if (err.name !== "AbortError") {
          fallbackCopy(shareUrl);
        }
      });
    } else {
      fallbackCopy(shareUrl);
    }
  }

  function getImageParamFromUrl() {
    var params = new URLSearchParams(window.location.search);
    return params.get("image");
  }

  function openPreviewById(imageId, items) {
    var found = null;
    var candidates = items && items.length ? items : galleryItems;

    for (var i = 0; i < candidates.length; i++) {
      if (String(candidates[i].id) === String(imageId)) {
        found = candidates[i];
        break;
      }
    }

    if (found) {
      openPreview(currentImageUrl(found), found.title || found.url);
    }
  }

  function openCommentDrawer(item) {
    if (!item.id || !commentDrawer) {
      return;
    }

    activeCommentImage = item;
    commentPage = 1;
    commentHasMore = false;
    commentDrawerTitle.textContent = displayFileName(item);
    commentCountLabel.textContent = formatCount(item.comment_count) + " 条评论";
    commentInput.value = "";
    commentCharCount.textContent = "0/300";
    commentList.innerHTML = '<div class="comment-empty">正在加载评论...</div>';
    commentDrawer.classList.add("open");
    commentDrawer.setAttribute("aria-hidden", "false");
    loadComments(1, false);
  }

  function closeCommentDrawer() {
    if (!commentDrawer) {
      return;
    }

    commentDrawer.classList.remove("open");
    commentDrawer.setAttribute("aria-hidden", "true");
    commentList.innerHTML = "";
    activeCommentImage = null;
  }

  function renderComments(items, append) {
    if (!append) {
      commentList.innerHTML = "";
    }

    if (!items.length && !append) {
      commentList.innerHTML = '<div class="comment-empty">还没有评论。</div>';
      return;
    }

    items.forEach(function (comment) {
      var item = document.createElement("div");
      var meta = document.createElement("div");
      var name = document.createElement("strong");
      var time = document.createElement("span");
      var content = document.createElement("p");

      item.className = "comment-item";
      meta.className = "comment-meta";
      name.textContent = comment.username || "用户";
      time.textContent = comment.created_at || "";
      content.textContent = comment.content || "";
      meta.appendChild(name);
      meta.appendChild(time);
      item.appendChild(meta);
      item.appendChild(content);

      if (comment.is_owner) {
        var remove = document.createElement("button");
        remove.className = "comment-delete";
        remove.type = "button";
        remove.textContent = "删除";
        remove.addEventListener("click", function () {
          deleteComment(comment.id);
        });
        item.appendChild(remove);
      }

      commentList.appendChild(item);
    });
  }

  function loadComments(page, append) {
    if (!activeCommentImage || commentLoading || (!append && !activeCommentImage.id)) {
      return Promise.resolve();
    }
    if (append && !commentHasMore) {
      return Promise.resolve();
    }

    commentLoading = true;
    return fetch("/api/images/" + encodeURIComponent(activeCommentImage.id) + "/comments?page=" + page + "&limit=20", {
      credentials: "same-origin"
    })
      .then(function (response) {
        if (!response.ok) {
          throw new Error("comments load failed");
        }
        return response.json();
      })
      .then(function (data) {
        var items = Array.isArray(data.items) ? data.items : [];
        activeCommentImage.comment_count = Number(data.total) || 0;
        commentCountLabel.textContent = formatCount(activeCommentImage.comment_count) + " 条评论";
        renderComments(items, append);
        commentHasMore = !!data.has_more;
        commentPage = page + 1;
        renderGallery();
      })
      .catch(function () {
        if (!append) {
          commentList.innerHTML = '<div class="comment-empty">评论加载失败，请稍后重试。</div>';
        }
      })
      .then(function () {
        commentLoading = false;
      });
  }

  function submitComment() {
    if (!activeCommentImage || !activeCommentImage.id) {
      return;
    }

    var content = commentInput.value.trim();
    if (!content) {
      return;
    }
    if (content.length > 300) {
      return;
    }

    commentSubmit.disabled = true;
    fetch("/api/images/" + encodeURIComponent(activeCommentImage.id) + "/comments", {
      method: "POST",
      credentials: "same-origin",
      headers: { "Content-Type": "application/x-www-form-urlencoded; charset=UTF-8" },
      body: "content=" + encodeURIComponent(content)
    })
      .then(function (response) {
        if (response.status === 401) {
          redirectToLogin();
          throw new Error("login required");
        }
        if (!response.ok) {
          throw new Error("comment submit failed");
        }
        return response.json();
      })
      .then(function () {
        commentInput.value = "";
        commentCharCount.textContent = "0/300";
        activeCommentImage.comment_count = (Number(activeCommentImage.comment_count) || 0) + 1;
        commentCountLabel.textContent = formatCount(activeCommentImage.comment_count) + " 条评论";
        loadComments(1, false);
      })
      .then(function () {
        commentSubmit.disabled = false;
      })
      .catch(function () {
        commentSubmit.disabled = false;
      });
  }

  function deleteComment(commentId) {
    if (!activeCommentImage || !commentId) {
      return;
    }
    if (!window.confirm("删除这条评论？")) {
      return;
    }

    fetch("/api/comments/" + encodeURIComponent(commentId), {
      method: "DELETE",
      credentials: "same-origin"
    })
      .then(function (response) {
        if (response.status === 401) {
          redirectToLogin();
          throw new Error("login required");
        }
        if (!response.ok) {
          throw new Error("comment delete failed");
        }
        return response.json();
      })
      .then(function () {
        activeCommentImage.comment_count = Math.max(0, (Number(activeCommentImage.comment_count) || 0) - 1);
        commentCountLabel.textContent = formatCount(activeCommentImage.comment_count) + " 条评论";
        loadComments(1, false);
      })
      .catch(function () {});
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
      resetImages();
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
    xhr.withCredentials = true;
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
      var url = currentThumbnailUrl(item);
      return imageMetaByUrl[url] && imageMetaByUrl[url].width;
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

    heroPreview.src = currentThumbnailUrl(items[0]);
    heroPreview.alt = items[0].title || "图片预览";
    heroPlaceholder.hidden = true;

    items.forEach(function (item, index) {
      var fragment = template.content.cloneNode(true);
      var card = fragment.querySelector(".redesign-image-card");
      var image = fragment.querySelector("img");
      var title = fragment.querySelector(".redesign-image-name");
      var path = fragment.querySelector(".redesign-image-path");
      var size = fragment.querySelector(".redesign-image-size");
      var format = fragment.querySelector(".img-fmt");
      var isNew = fragment.querySelector(".img-new");
      var resolution = fragment.querySelector(".img-resolution");
      var previewButton = fragment.querySelector(".preview-action");
      var copyButton = fragment.querySelector(".copy-action");
      var shareButton = fragment.querySelector(".share-action");
      var downloadButtons = fragment.querySelectorAll(".download-action, .image-mini-action");
      var footer = fragment.querySelector(".redesign-image-footer");
      var miniDownload = fragment.querySelector(".image-mini-action");
      var ext = fileExt(item.title || item.url).toUpperCase();
      var downloadUrl = item.id ? "/api/images/" + encodeURIComponent(item.id) + "/download" : item.url;
      var actionGroup = document.createElement("div");
      var likeButton = document.createElement("button");
      var favoriteButton = document.createElement("button");
      var commentButton = document.createElement("button");

      actionGroup.className = "image-card-actions";
      likeButton.className = "image-mini-action image-reaction-action like-action";
      likeButton.type = "button";
      likeButton.innerHTML = '<span class="action-label">点赞</span><span class="action-count">0</span>';
      favoriteButton.className = "image-mini-action image-reaction-action favorite-action";
      favoriteButton.type = "button";
      favoriteButton.innerHTML = '<span class="action-label">收藏</span><span class="action-count">0</span>';
      commentButton.className = "image-mini-action comment-action";
      commentButton.type = "button";
      commentButton.innerHTML = '<span class="action-label">评论</span><span class="action-count">0</span>';
      actionGroup.appendChild(likeButton);
      actionGroup.appendChild(favoriteButton);
      actionGroup.appendChild(commentButton);
      if (miniDownload) {
        actionGroup.appendChild(miniDownload);
      }
      footer.appendChild(actionGroup);

      image.alt = item.title || "";
      image.loading = "lazy";
      image.decoding = "async";
      setImageSource(image, item, currentThumbnailUrl(item), true);
      title.textContent = item.title || item.url;
      path.textContent = displayFileName(item);
      size.textContent = formatFileSize(item.size);
      format.textContent = ext;
      isNew.hidden = index > 1;
      resolution.textContent = imageMetaByUrl[currentThumbnailUrl(item)]
        ? imageMetaByUrl[currentThumbnailUrl(item)].width + "x" + imageMetaByUrl[currentThumbnailUrl(item)].height
        : "--";

      downloadButtons.forEach(function (button) {
        button.href = downloadUrl;
        button.download = getDownloadName(item);
        button.addEventListener("click", function (event) {
          if (!isLoggedIn) {
            event.preventDefault();
            redirectToLogin();
            return;
          }
          item.download_count = (Number(item.download_count) || 0) + 1;
        });
      });
      updateReactionButton(likeButton, item.liked, item.like_count, "已赞", "点赞");
      updateReactionButton(favoriteButton, item.favorited, item.favorite_count, "已藏", "收藏");
      updateCommentButton(commentButton, item.comment_count);
      if (!item.id) {
        likeButton.disabled = true;
        favoriteButton.disabled = true;
        commentButton.disabled = true;
        likeButton.title = "图片数据尚未入库";
        favoriteButton.title = "图片数据尚未入库";
        commentButton.title = "图片数据尚未入库";
      }

      previewButton.addEventListener("click", function () {
        openPreview(currentImageUrl(item), item.title || item.url);
      });

      copyButton.addEventListener("click", function () {
        var value = item.path || currentImageUrl(item);
        if (navigator.clipboard && navigator.clipboard.writeText) {
          navigator.clipboard.writeText(value);
        }
        copyButton.textContent = "已复制";
        window.setTimeout(function () {
          copyButton.textContent = "复制";
        }, 1200);
      });

      shareButton.addEventListener("click", function () {
        shareImage(item);
      });

      likeButton.addEventListener("click", function () {
        if (!isLoggedIn) {
          redirectToLogin();
          return;
        }
        var nextActive = !item.liked;
        likeButton.disabled = true;
        requestImageReaction(item, "like", nextActive)
          .then(function (data) {
            item.liked = !!data.liked;
            item.favorited = !!data.favorited;
            item.like_count = Number(data.like_count) || 0;
            item.favorite_count = Number(data.favorite_count) || 0;
            updateReactionButton(likeButton, item.liked, item.like_count, "已赞", "点赞");
            updateReactionButton(favoriteButton, item.favorited, item.favorite_count, "已藏", "收藏");
          })
          .catch(function () {
            updateReactionButton(likeButton, item.liked, item.like_count, "已赞", "点赞");
          })
          .then(function () {
            likeButton.disabled = false;
          });
      });

      favoriteButton.addEventListener("click", function () {
        if (!isLoggedIn) {
          redirectToLogin();
          return;
        }
        var nextActive = !item.favorited;
        favoriteButton.disabled = true;
        requestImageReaction(item, "favorite", nextActive)
          .then(function (data) {
            item.liked = !!data.liked;
            item.favorited = !!data.favorited;
            item.like_count = Number(data.like_count) || 0;
            item.favorite_count = Number(data.favorite_count) || 0;
            updateReactionButton(likeButton, item.liked, item.like_count, "已赞", "点赞");
            updateReactionButton(favoriteButton, item.favorited, item.favorite_count, "已藏", "收藏");
          })
          .catch(function () {
            updateReactionButton(favoriteButton, item.favorited, item.favorite_count, "已藏", "收藏");
          })
          .then(function () {
            favoriteButton.disabled = false;
          });
      });

      commentButton.addEventListener("click", function () {
        if (!isLoggedIn) {
          redirectToLogin();
          return;
        }
        openCommentDrawer(item);
      });

      image.addEventListener("load", function () {
        item.__missing = false;
        card.classList.remove("image-load-failed");
        imageMetaByUrl[currentThumbnailUrl(item)] = {
          width: image.naturalWidth,
          height: image.naturalHeight
        };
        resolution.textContent = image.naturalWidth + "x" + image.naturalHeight;
        updateStats(filteredImages());
      });

      image.addEventListener("error", function () {
        var original = item.url || item.path || "";
        if (original && currentThumbnailUrl(item) !== original) {
          item.__activeThumbUrl = original;
          image.src = original;
          return;
        }

        var fallback = mediaFallbackUrl(original);
        if (fallback && currentThumbnailUrl(item) !== fallback) {
          item.__activeThumbUrl = fallback;
          image.src = fallback;
          return;
        }

        item.__missing = true;
        card.classList.add("image-load-failed");
        image.removeAttribute("src");
        resolution.textContent = "加载失败";
        updateStats(filteredImages());
      });

      galleryGrid.appendChild(fragment);
    });

    tryOpenRequestedImage();
  }

  function loadImages(reset) {
    if (imageLoading || (!reset && !imageHasMore)) {
      return Promise.resolve();
    }

    imageLoading = true;
    return fetch("/api/images?page=" + imagePage + "&limit=" + imagePageSize)
      .then(function (response) {
        if (!response.ok) {
          throw new Error("图片列表加载失败");
        }
        return response.json();
      })
      .then(function (data) {
        var items = Array.isArray(data) ? data : (Array.isArray(data.items) ? data.items : []);
        galleryItems = reset ? items : galleryItems.concat(items);
        imageHasMore = Array.isArray(data) ? false : !!data.has_more;
        imagePage += 1;
        renderGallery();

        if (!shareAutoOpened && items.length > 0) {
          var targetId = getImageParamFromUrl();
          if (targetId) {
            openPreviewById(targetId, items);
          }
          shareAutoOpened = true;
        }
      })
      .catch(function () {
        galleryGrid.innerHTML = '<div class="gallery-empty">图片列表加载失败，请刷新页面或确认服务正在运行。</div>';
        updateStats([]);
      })
      .then(function () {
        imageLoading = false;
        tryOpenRequestedImage();
      });
  }

  function resetImages() {
    imagePage = 1;
    imageHasMore = true;
    galleryItems = [];
    galleryGrid.innerHTML = "";
    return loadImages(true);
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
  commentDrawerClose.addEventListener("click", closeCommentDrawer);

  commentDrawer.addEventListener("click", function (event) {
    if (event.target && event.target.getAttribute("data-close") === "comment-drawer") {
      closeCommentDrawer();
    }
  });

  commentSubmit.addEventListener("click", submitComment);

  commentInput.addEventListener("input", function () {
    commentCharCount.textContent = commentInput.value.length + "/300";
  });

  commentInput.addEventListener("keydown", function (event) {
    if ((event.ctrlKey || event.metaKey) && event.key === "Enter") {
      submitComment();
    }
  });

  commentList.addEventListener("scroll", function () {
    var nearBottom = commentList.scrollTop + commentList.clientHeight >= commentList.scrollHeight - 80;
    if (nearBottom) {
      loadComments(commentPage, true);
    }
  });

  lightbox.addEventListener("click", function (event) {
    if (event.target && event.target.getAttribute("data-close") === "lightbox") {
      closePreview();
    }
  });

  document.addEventListener("keydown", function (event) {
    if (event.key === "Escape") {
      closePreview();
      closeCommentDrawer();
    }
  });

  openFirstButton.addEventListener("click", function () {
    var items = filteredImages();
    if (!items.length) {
      return;
    }
    openPreview(currentImageUrl(items[0]), items[0].title || items[0].url);
  });

  heroPreview.addEventListener("error", function () {
    var fallback = mediaFallbackUrl(heroPreview.getAttribute("src") || "");
    if (fallback && heroPreview.getAttribute("src") !== fallback) {
      heroPreview.src = fallback;
    }
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

  window.addEventListener("scroll", function () {
    var nearBottom = window.innerHeight + window.scrollY >= document.documentElement.scrollHeight - 420;
    if (nearBottom) {
      loadImages(false);
    }
  }, { passive: true });

  loadCurrentUser().then(resetImages);
})();
