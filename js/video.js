(function () {
  var videoGrid = document.getElementById("video-grid");
  var template = document.getElementById("video-card-template");
  var uploadForm = document.getElementById("video-upload-form");
  var uploadInput = document.getElementById("upload-video");
  var uploadSubmit = document.getElementById("video-upload-submit");
  var uploadStatus = document.getElementById("video-upload-status");

  function setUploadStatus(text, state) {
    uploadStatus.textContent = text;
    uploadStatus.classList.remove("ok", "err");
    if (state) {
      uploadStatus.classList.add(state);
    }
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
          throw new Error("load videos failed");
        }
        return response.json();
      })
      .then(function (items) {
        renderVideos(Array.isArray(items) ? items : []);
      })
      .catch(function () {
        videoGrid.innerHTML = '<div class="gallery-empty">视频列表加载失败。</div>';
      });
  }

  uploadForm.addEventListener("submit", function (event) {
    event.preventDefault();

    if (!uploadInput.files || !uploadInput.files[0]) {
      setUploadStatus("请先选择视频。", "err");
      return;
    }

    if (uploadInput.files[0].size > 20 * 1024 * 1024) {
      setUploadStatus("视频不能超过 20MB。", "err");
      return;
    }

    var formData = new FormData();
    formData.append("video", uploadInput.files[0]);

    uploadSubmit.disabled = true;
    uploadSubmit.textContent = "上传中";
    setUploadStatus("正在上传视频...", "");

    fetch("/api/upload-video", {
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
        return loadVideos();
      })
      .catch(function (error) {
        setUploadStatus(error.message || "上传失败。", "err");
      })
      .finally(function () {
        uploadSubmit.disabled = false;
        uploadSubmit.textContent = "上传";
      });
  });

  loadVideos();
})();
