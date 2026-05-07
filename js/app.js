(function () {
  var galleryGrid = document.getElementById("gallery-grid");
  var template = document.getElementById("gallery-card-template");
  var lightbox = document.getElementById("lightbox");
  var lightboxImage = document.getElementById("lightbox-image");
  var lightboxTitle = document.getElementById("lightbox-title");
  var lightboxLink = document.getElementById("lightbox-link");
  var closeButton = document.getElementById("lightbox-close");
  var openFirstButton = document.getElementById("open-first-image");
  var uploadForm = document.getElementById("upload-form");
  var uploadInput = document.getElementById("upload-image");
  var uploadSubmit = document.getElementById("upload-submit");
  var uploadStatus = document.getElementById("upload-status");
  var heroPreview = document.getElementById("hero-preview");
  var galleryItems = [];

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

  function setUploadStatus(text, state) {
    uploadStatus.textContent = text;
    uploadStatus.classList.remove("ok", "err");
    if (state) {
      uploadStatus.classList.add(state);
    }
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
      var image = fragment.querySelector("img");
      var title = fragment.querySelector("strong");
      var path = fragment.querySelector(".gallery-copy span");

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

      card.addEventListener("click", function () {
        openPreview(item.url, item.title);
      });

      galleryGrid.appendChild(fragment);
    });
  }

  function loadImages() {
    return fetch("/api/images")
      .then(function (response) {
        if (!response.ok) {
          throw new Error("load images failed");
        }
        return response.json();
      })
      .then(function (items) {
        renderGallery(Array.isArray(items) ? items : []);
      })
      .catch(function () {
        galleryGrid.innerHTML = '<div class="gallery-empty">图片列表加载失败。</div>';
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

  uploadForm.addEventListener("submit", function (event) {
    event.preventDefault();

    if (!uploadInput.files || !uploadInput.files[0]) {
      setUploadStatus("请先选择图片。", "err");
      return;
    }

    if (uploadInput.files[0].size > 20 * 1024 * 1024) {
      setUploadStatus("图片不能超过 20MB。", "err");
      return;
    }

    var formData = new FormData();
    formData.append("image", uploadInput.files[0]);

    uploadSubmit.disabled = true;
    uploadSubmit.textContent = "上传中";
    setUploadStatus("正在上传图片...", "");

    fetch("/api/upload", {
      method: "POST",
      body: formData
    })
      .then(function (response) {
        return response.json().then(function (data) {
          return { ok: response.ok, data: data };
        });
      })
      .then(function (result) {
        if (!result.ok || !result.data.ok) {
          throw new Error(result.data && result.data.message ? result.data.message : "upload failed");
        }

        setUploadStatus("上传成功。", "ok");
        uploadForm.reset();
        return loadImages();
      })
      .catch(function (error) {
        setUploadStatus(error.message || "上传失败。", "err");
      })
      .finally(function () {
        uploadSubmit.disabled = false;
        uploadSubmit.textContent = "上传";
      });
  });

  loadImages();
})();
