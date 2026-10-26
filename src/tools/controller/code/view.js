// SPDX-License-Identifier: AGPL-3.0-only
import {contentRect, hits, imagePoint} from './geometry.js';

export class TargetView {
  constructor(video, canvas, session, showChoices) {
    this.video = video;
    this.canvas = canvas;
    this.session = session;
    this.showChoices = showChoices;
    this.lastFrame = -Infinity;
    this.rect = null;
    this.displayed = [];
    this.hover = null;
    canvas.addEventListener('pointermove', (event) => {
      this.hover = this.candidates(event);
    });
    canvas.addEventListener('pointerleave', () => {
      this.hover = null;
    });
    canvas.addEventListener('click', (event) => {
      if (event.button !== 0) return;
      const reason = session.unavailable(this.ready());
      if (reason) {
        session.notify(reason, true);
        return;
      }
      const candidates = this.candidates(event);
      if (!candidates.length)
        session.notify('Click inside an outlined target', false);
      if (candidates.length === 1)
        void session.choose(candidates[0].id, this.ready());
      else if (candidates.length > 1)
        showChoices(candidates);
    });
    const frame = () => {
      this.lastFrame = performance.now();
      video.requestVideoFrameCallback(frame);
    };
    video.requestVideoFrameCallback(frame);
  }

  ready() {
    const camera = this.session.camera;
    const compatible = !camera ||
        Math.abs(
            camera.width / camera.height /
                (this.video.videoWidth / this.video.videoHeight) -
            1) <= .01;
    return compatible && !this.video.paused && !this.video.ended &&
        this.video.readyState >= 2 &&
        performance.now() - this.lastFrame < 500 && !document.hidden;
  }

  candidates(event) {
    if (this.session.unavailable(this.ready()) || !this.session.camera)
      return [];
    const bounds = this.canvas.getBoundingClientRect();
    return hits(
        imagePoint(
            event.clientX - bounds.left, event.clientY - bounds.top, this.rect,
            this.session.camera),
        this.displayed);
  }

  draw() {
    const bounds = this.canvas.getBoundingClientRect();
    const scale = devicePixelRatio || 1;
    const width = Math.round(bounds.width * scale);
    const height = Math.round(bounds.height * scale);
    if (this.canvas.width !== width || this.canvas.height !== height) {
      this.canvas.width = width;
      this.canvas.height = height;
    }
    const ctx = this.canvas.getContext('2d');
    ctx.setTransform(scale, 0, 0, scale, 0, 0);
    ctx.clearRect(0, 0, bounds.width, bounds.height);
    this.rect = contentRect(
        bounds.width, bounds.height, this.video.videoWidth,
        this.video.videoHeight);
    const camera = this.session.camera;
    const videoAspect = this.video.videoWidth / this.video.videoHeight;
    const mismatch = camera &&
        Math.abs(camera.width / camera.height / videoAspect - 1) > .01;
    const reason = mismatch ?
        'Camera/video geometry differs; selection disabled' :
        this.session.unavailable(this.ready());
    this.displayed =
        !reason || this.session.pending ? this.session.detections() : [];
    if (mismatch || !this.rect || !camera) {
      this.displayed = [];
      return reason;
    }
    ctx.save();
    ctx.beginPath();
    ctx.rect(this.rect.x, this.rect.y, this.rect.width, this.rect.height);
    ctx.clip();
    for (const detection of this.displayed)
      this.drawBox(ctx, detection, camera);
    ctx.restore();
    this.canvas.style.cursor =
        this.hover?.length && !reason ? 'pointer' : 'default';
    return reason;
  }

  drawBox(ctx, detection, camera) {
    const b = detection.bbox;
    const r = this.rect;
    const x =
        r.x + (b.center.position.x - b.size_x / 2) / camera.width * r.width;
    const y =
        r.y + (b.center.position.y - b.size_y / 2) / camera.height * r.height;
    const w = b.size_x / camera.width * r.width;
    const h = b.size_y / camera.height * r.height;
    const active = detection.id === this.session.health.selected_track;
    const pending = detection.id === this.session.pending?.id;
    ctx.strokeStyle = active ? '#68ffa2' : pending ? '#72bdff' : '#f7d76a';
    ctx.lineWidth = active || pending ? 3 : 1.5;
    ctx.setLineDash(pending ? [6, 4] : []);
    ctx.strokeRect(x, y, w, h);
    const label = `${
        active      ? 'FOLLOWING' :
            pending ? 'REQUESTED' :
                      'TARGET'} · ${detection.id.split(':').at(-1)}`;
    ctx.font = '13px system-ui';
    const textY = Math.max(r.y + 18, y - 6);
    ctx.fillStyle = '#10151ce6';
    ctx.fillRect(
        Math.max(r.x, x), textY - 16, ctx.measureText(label).width + 12, 22);
    ctx.fillStyle = ctx.strokeStyle;
    ctx.fillText(label, Math.max(r.x, x) + 6, textY);
  }
}
