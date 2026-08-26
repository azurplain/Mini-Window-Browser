#include "MediaBridge.h"

#include <algorithm>

namespace xiaochuang {

const wchar_t* MediaBridge::BootstrapScript() {
    return LR"MWBJS(
(() => {
  if (window.__miniWindowBridge) return;

  const state = {
    hold: null,
    reverseTimer: 0,
    originalRate: 1,
    originalPaused: true,
    lastInteracted: null
  };
  const frameCommandType = 'MWB_MEDIA_COMMAND_V1';

  const isEditable = (node) => {
    if (!node || node.nodeType !== Node.ELEMENT_NODE) return false;
    const element = /** @type {HTMLElement} */ (node);
    const tag = element.tagName ? element.tagName.toLowerCase() : '';
    return tag === 'input' || tag === 'textarea' || tag === 'select' ||
      element.isContentEditable || element.getAttribute('role') === 'textbox' ||
      element.getAttribute('role') === 'searchbox' ||
      element.getAttribute('aria-multiline') === 'true';
  };

  let lastTyping = null;
  const publishTyping = () => {
    const typing = document.hasFocus() && isEditable(document.activeElement);
    if (typing === lastTyping) return;
    lastTyping = typing;
    try { chrome.webview.postMessage(typing ? 'MWB_TYPING:1' : 'MWB_TYPING:0'); } catch (_) {}
  };
  document.addEventListener('focusin', publishTyping, true);
  document.addEventListener('focusout', () => queueMicrotask(publishTyping), true);
  document.addEventListener('pointerdown', (event) => {
    const video = event.target && event.target.closest ? event.target.closest('video') : null;
    if (video) state.lastInteracted = video;
    queueMicrotask(publishTyping);
  }, true);
  window.addEventListener('blur', publishTyping, true);
  queueMicrotask(publishTyping);

  const scoreVideo = (video) => {
    const rect = video.getBoundingClientRect();
    const visibleWidth = Math.max(0, Math.min(innerWidth, rect.right) - Math.max(0, rect.left));
    const visibleHeight = Math.max(0, Math.min(innerHeight, rect.bottom) - Math.max(0, rect.top));
    const style = getComputedStyle(video);
    const visibleArea = style.display === 'none' || style.visibility === 'hidden' || Number(style.opacity) === 0
      ? 0 : visibleWidth * visibleHeight;
    let score = visibleArea;
    if (!video.paused && !video.ended) score += 1e12;
    if (video === state.lastInteracted) score += 2e12;
    return score;
  };

  const activeVideo = () => {
    const videos = Array.from(document.querySelectorAll('video'));
    if (!videos.length) return null;
    videos.sort((a, b) => scoreVideo(b) - scoreVideo(a));
    return videos[0];
  };

  const queryDeep = (selector) => {
    const roots = [document];
    const visited = new Set();
    while (roots.length) {
      const root = roots.shift();
      if (!root || visited.has(root)) continue;
      visited.add(root);
      try {
        const match = root.querySelector(selector);
        if (match) return match;
        for (const element of root.querySelectorAll('*')) {
          if (element.shadowRoot) roots.push(element.shadowRoot);
        }
      } catch (_) {}
    }
    return null;
  };

  const clickFirst = (selectors) => {
    for (const selector of selectors) {
      const element = queryDeep(selector);
      if (!element || typeof element.click !== 'function' ||
          !element.getClientRects().length || element.disabled ||
          element.getAttribute('aria-disabled') === 'true') continue;
      try {
        element.click();
        return true;
      } catch (_) {}
    }
    return false;
  };

  const callPlayerMethod = (player, method) => {
    try {
      if (player && typeof player[method] === 'function') {
        player[method]();
        return true;
      }
    } catch (_) {}
    return false;
  };

  const site = () => {
    const host = location.hostname.toLowerCase();
    if (host === 'bilibili.com' || host.endsWith('.bilibili.com')) return 'bilibili';
    if (host === 'douyin.com' || host.endsWith('.douyin.com')) return 'douyin';
    if (host === 'youtube.com' || host.endsWith('.youtube.com') ||
        host === 'youtu.be' || host.endsWith('.youtu.be')) return 'youtube';
    return 'html5';
  };

  const stopHold = () => {
    if (!state.hold) return;
    const video = state.hold.video;
    if (state.reverseTimer) {
      clearInterval(state.reverseTimer);
      state.reverseTimer = 0;
    }
    if (video && video.isConnected) {
      try { video.playbackRate = state.originalRate; } catch (_) {}
      if (state.originalPaused) video.pause();
      else video.play().catch(() => {});
    }
    state.hold = null;
  };

  const handlePreviousNext = (direction) => {
    const currentSite = site();
    if (currentSite === 'youtube') {
      const player = document.getElementById('movie_player');
      const api = direction < 0 ? 'previousVideo' : 'nextVideo';
      if (callPlayerMethod(player, api)) return true;
      return clickFirst(direction < 0
        ? ['.ytp-prev-button', 'a.ytp-prev-button']
        : ['.ytp-next-button', 'a.ytp-next-button']);
    }
    if (currentSite === 'bilibili') {
      const selectors = direction < 0
        ? ['.bpx-player-ctrl-prev', '[data-title="上一个"]', '[data-title="上一集"]',
           '[aria-label="上一个"]', '[aria-label="上一集"]', '[title="上一个"]', '[title="上一集"]']
        : ['.bpx-player-ctrl-next', '[data-title="下一个"]', '[data-title="下一集"]',
           '[aria-label="下一个"]', '[aria-label="下一集"]', '[title="下一个"]', '[title="下一集"]'];
      // Bangumi pages expose player.prev/next functions that currently do
      // nothing. Their real episode navigation is wired to the control-bar
      // buttons, so use those first instead of treating the no-op API as success.
      const isBangumi = location.pathname.startsWith('/bangumi/play/');
      if (isBangumi && clickFirst(selectors)) return true;
      const player = window.player || window.bilibiliPlayer;
      if (callPlayerMethod(player, direction < 0 ? 'prev' : 'next')) return true;
      return clickFirst(selectors);
    }
    if (currentSite === 'douyin') {
      const clicked = clickFirst(direction < 0
        ? ['[data-e2e="arrow-left"]', '[aria-label*="上一"]', '[class*="xgplayer-playswitch-prev"]']
        : ['[data-e2e="arrow-right"]', '[aria-label*="下一"]', '[class*="xgplayer-playswitch-next"]']);
      if (clicked) return true;
      document.dispatchEvent(new KeyboardEvent('keydown', {
        key: direction < 0 ? 'ArrowUp' : 'ArrowDown',
        code: direction < 0 ? 'ArrowUp' : 'ArrowDown',
        bubbles: true
      }));
      return true;
    }
    return clickFirst(direction < 0
      ? ['[data-action="previous"]', '[data-action="prev"]', '[aria-label="上一集"]',
         '[title="上一集"]', 'button[rel="prev"]', 'a[rel="prev"]']
      : ['[data-action="next"]', '[aria-label="下一集"]', '[title="下一集"]',
         'button[rel="next"]', 'a[rel="next"]']);
  };

  const command = (name, value) => {
    if (name === 'stopHold') {
      stopHold();
      return true;
    }
    if (name === 'previous') return handlePreviousNext(-1);
    if (name === 'next') return handlePreviousNext(1);

    const video = activeVideo();
    if (!video) return false;
    state.lastInteracted = video;

    if (name === 'togglePlay') {
      if (video.paused) video.play().catch(() => {});
      else video.pause();
      return true;
    }
    if (name === 'seekBackward' || name === 'seekForward') {
      const delta = name === 'seekBackward' ? -5 : 5;
      const duration = Number.isFinite(video.duration) ? video.duration : Number.MAX_SAFE_INTEGER;
      video.currentTime = Math.max(0, Math.min(duration, video.currentTime + delta));
      return true;
    }
    if (name === 'holdForward' || name === 'holdBackward') {
      stopHold();
      const rate = Math.max(2, Math.min(5, Number(value) || 3));
      state.originalRate = Number(video.playbackRate) || 1;
      state.originalPaused = video.paused;
      state.hold = { video, direction: name === 'holdForward' ? 1 : -1, rate };
      if (name === 'holdForward') {
        video.playbackRate = rate;
        video.play().catch(() => {});
      } else {
        video.pause();
        const stepMs = 100;
        state.reverseTimer = setInterval(() => {
          if (!state.hold || !video.isConnected) return stopHold();
          video.currentTime = Math.max(0, video.currentTime - rate * stepMs / 1000);
        }, stepMs);
      }
      return true;
    }
    return false;
  };

  window.addEventListener('message', (event) => {
    const data = event.data;
    if (window.parent === window || event.source !== window.parent ||
        !data || data.type !== frameCommandType || typeof data.name !== 'string') return;
    if (data.name !== 'stopHold' && !activeVideo()) return;
    command(data.name, data.value);
  });

  const diagnostic = () => {
    const video = activeVideo();
    if (!video) return { site: site(), adapter: site(), video: null };
    let quality = {};
    try { quality = video.getVideoPlaybackQuality ? video.getVideoPlaybackQuality() : {}; } catch (_) {}
    return {
      site: location.hostname,
      adapter: site(),
      video: {
        width: video.videoWidth || 0,
        height: video.videoHeight || 0,
        currentTime: Number(video.currentTime || 0).toFixed(1),
        playbackRate: video.playbackRate,
        paused: video.paused,
        droppedVideoFrames: quality.droppedVideoFrames || video.webkitDroppedFrameCount || 0,
        totalVideoFrames: quality.totalVideoFrames || video.webkitDecodedFrameCount || 0
      }
    };
  };

  window.__miniWindowBridge = { command, diagnostic, activeVideo };
})();
)MWBJS";
}

std::wstring MediaBridge::CommandScript(MediaCommand command, int holdRate) {
    holdRate = std::clamp(holdRate, 2, 5);
    const wchar_t* name = L"togglePlay";
    switch (command) {
    case MediaCommand::TogglePlay: name = L"togglePlay"; break;
    case MediaCommand::SeekBackward: name = L"seekBackward"; break;
    case MediaCommand::SeekForward: name = L"seekForward"; break;
    case MediaCommand::HoldBackward: name = L"holdBackward"; break;
    case MediaCommand::HoldForward: name = L"holdForward"; break;
    case MediaCommand::StopHold: name = L"stopHold"; break;
    case MediaCommand::Previous: name = L"previous"; break;
    case MediaCommand::Next: name = L"next"; break;
    }
    const std::wstring commandName(name);
    const std::wstring rate = std::to_wstring(holdRate);
    return L"(() => { const name='" + commandName + L"'; const value=" + rate +
        L"; if (window.__miniWindowBridge && window.__miniWindowBridge.command(name,value)) return true; "
        L"let posted=false; for (let i=0;i<window.frames.length;++i) { const frame=window.frames[i]; "
        L"try { if (frame.__miniWindowBridge && frame.__miniWindowBridge.command(name,value)) return true; } catch (_) {} "
        L"try { frame.postMessage({type:'MWB_MEDIA_COMMAND_V1',name,value},'*'); posted=true; } catch (_) {} } "
        L"return posted; })();";
}

std::wstring MediaBridge::DiagnosticScript() {
    return L"(() => JSON.stringify(window.__miniWindowBridge ? "
           L"window.__miniWindowBridge.diagnostic() : {adapter:'unavailable',video:null}))();";
}

} // namespace xiaochuang
