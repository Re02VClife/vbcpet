// vbcpet 桌宠 — preload (隔离上下文桥)
// 提供最小拖动能力: 在角色区域按住拖动 → 通知主进程移动窗口。
// 情绪 API 直接由 render.html 暴露的 window.* 使用, 无需在此重复。
'use strict';

const { ipcRenderer } = require('electron');

// 拖动: 监听 document 上的 mousedown/mousemove/mouseup
function installDrag() {
  let dragging = false;
  let lastX = 0, lastY = 0;

  window.addEventListener('mousedown', (e) => {
    dragging = true;
    lastX = e.screenX;
    lastY = e.screenY;
  });
  window.addEventListener('mousemove', (e) => {
    if (!dragging) return;
    const dx = e.screenX - lastX;
    const dy = e.screenY - lastY;
    lastX = e.screenX;
    lastY = e.screenY;
    ipcRenderer.send('pet:drag', dx, dy);
  });
  window.addEventListener('mouseup', () => { dragging = false; });
  window.addEventListener('mouseleave', () => { dragging = false; });
}

window.addEventListener('DOMContentLoaded', installDrag);
