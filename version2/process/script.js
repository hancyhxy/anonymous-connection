/* ============================================
   Presenter Shell — Controller
   Loads slides.md, parses playback order, fetches
   each slide fragment, handles keyboard nav + TOC.
   ============================================ */

(function () {
  const state = {
    slides: [],          // [{ file, title }]
    currentIndex: 0,
    tocVisible: false,
  };

  const $stage       = document.querySelector('.stage-inner');
  const $counter     = document.querySelector('.counter');
  const $toc         = document.querySelector('.toc');
  const $tocList     = document.querySelector('.toc-list');

  /* ---------- 1. Load and parse slides.md ---------- */
  async function loadSlideList() {
    const res = await fetch('slides.md');
    if (!res.ok) throw new Error('Failed to fetch slides.md');
    const text = await res.text();
    return parsePlaybackOrder(text);
  }

  /**
   * Parses the `## Playback Order` section and extracts
   * ordered list items matching: `1. [Title](file.html)`
   */
  function parsePlaybackOrder(md) {
    const lines = md.split('\n');
    let inSection = false;
    const slides = [];
    const pattern = /^\s*\d+\.\s*\[([^\]]+)\]\(([^)]+)\)\s*$/;

    for (const line of lines) {
      if (/^##\s+Playback Order/i.test(line)) {
        inSection = true;
        continue;
      }
      if (inSection && /^##\s+/.test(line)) break;  // next heading ends the section
      if (!inSection) continue;

      const m = line.match(pattern);
      if (m) slides.push({ title: m[1].trim(), file: m[2].trim() });
    }

    return slides;
  }

  /* ---------- 2. Fetch each slide fragment ---------- */
  async function loadAllFragments(slides) {
    const results = await Promise.all(
      slides.map(s => fetch(s.file).then(r => {
        if (!r.ok) throw new Error(`Failed to load ${s.file}`);
        return r.text();
      }))
    );
    return results;
  }

  /* ---------- 3. Render all slides into the stage ---------- */
  function renderSlides(fragments) {
    $stage.innerHTML = '';
    fragments.forEach((html, i) => {
      const wrapper = document.createElement('div');
      wrapper.className = 'slide';
      wrapper.dataset.index = i;
      wrapper.innerHTML = html;
      $stage.appendChild(wrapper);
    });
  }

  /* ---------- 4. Canvas scaling (fit 1920x1080 into viewport) ---------- */
  function fitStage() {
    const vw = window.innerWidth;
    const vh = window.innerHeight;
    const scale = Math.min(vw / 1920, vh / 1080);
    // translate first anchors center-to-center, then scale shrinks around that anchor
    $stage.style.transform = `translate(-50%, -50%) scale(${scale})`;
  }

  /* ---------- 5. Slide switching ---------- */
  function showSlide(index) {
    const all = document.querySelectorAll('.slide');
    if (index < 0 || index >= all.length) return;
    all.forEach(el => el.classList.remove('active'));
    all[index].classList.add('active');
    state.currentIndex = index;
    updateCounter();
    updateTocHighlight();
  }

  function updateCounter() {
    if (!$counter) return;  // counter is optional; skipped if absent from DOM
    const total = state.slides.length;
    const cur = state.currentIndex + 1;
    const title = state.slides[state.currentIndex]?.title || '';
    $counter.innerHTML = `${cur} / ${total}<span class="counter-title">${title}</span>`;
  }

  /* ---------- 6. Table of contents ---------- */
  function buildToc() {
    $tocList.innerHTML = '';
    state.slides.forEach((s, i) => {
      const li = document.createElement('li');
      li.textContent = s.title;
      li.dataset.index = i;
      li.addEventListener('click', () => {
        showSlide(i);
        toggleToc(false);
      });
      $tocList.appendChild(li);
    });
  }

  function updateTocHighlight() {
    $tocList.querySelectorAll('li').forEach((li, i) => {
      li.classList.toggle('active-slide', i === state.currentIndex);
    });
  }

  function toggleToc(forceState) {
    state.tocVisible = (typeof forceState === 'boolean') ? forceState : !state.tocVisible;
    $toc.classList.toggle('visible', state.tocVisible);
  }

  /* ---------- 7. Fullscreen ---------- */
  function toggleFullscreen() {
    if (!document.fullscreenElement) {
      document.documentElement.requestFullscreen?.();
    } else {
      document.exitFullscreen?.();
    }
  }

  /* ---------- 8. Keyboard ---------- */
  function handleKey(e) {
    // Close TOC on Escape first
    if (state.tocVisible && e.key === 'Escape') {
      toggleToc(false);
      return;
    }

    switch (e.key) {
      case 'ArrowRight':
      case 'PageDown':
      case ' ':
        e.preventDefault();
        showSlide(state.currentIndex + 1);
        break;
      case 'ArrowLeft':
      case 'PageUp':
        e.preventDefault();
        showSlide(state.currentIndex - 1);
        break;
      case 'Home':
        e.preventDefault();
        showSlide(0);
        break;
      case 'End':
        e.preventDefault();
        showSlide(state.slides.length - 1);
        break;
      case 'f':
      case 'F':
        e.preventDefault();
        toggleFullscreen();
        break;
      case 't':
      case 'T':
        e.preventDefault();
        toggleToc();
        break;
      case 'Escape':
        if (document.fullscreenElement) document.exitFullscreen?.();
        break;
    }
  }

  /* ---------- 9. Error rendering ---------- */
  function renderError(err) {
    document.body.innerHTML = `
      <div class="error-box">
        <div class="error-inner">
          <h3>Presenter failed to start</h3>
          <p>${err.message}</p>
          <p style="margin-top: 12px;">
            If you opened this file by double-clicking, browsers block <code>fetch()</code>
            on the <code>file://</code> protocol. Serve the folder with a local server:
          </p>
          <p style="margin-top: 10px;">
            <code>cd process &amp;&amp; python3 -m http.server 8000</code>
          </p>
          <p style="margin-top: 8px;">then open <code>http://localhost:8000</code></p>
        </div>
      </div>
    `;
  }

  /* ---------- 10. Boot ---------- */
  async function boot() {
    try {
      state.slides = await loadSlideList();
      if (state.slides.length === 0) {
        throw new Error('No slides found under "## Playback Order" in slides.md');
      }
      const fragments = await loadAllFragments(state.slides);
      renderSlides(fragments);
      buildToc();
      fitStage();
      showSlide(0);

      window.addEventListener('resize', fitStage);
      window.addEventListener('keydown', handleKey);
    } catch (err) {
      console.error(err);
      renderError(err);
    }
  }

  boot();
})();
