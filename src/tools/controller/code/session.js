// SPDX-License-Identifier: AGPL-3.0-only
import {stampMs, validBox} from './geometry.js';

export class SelectionSession {
  constructor(select, notify) {
    this.select = select;
    this.notify = notify;
    this.reset();
  }

  reset() {
    this.camera = null;
    this.tracks = null;
    this.health = {};
    this.healthReceived = -Infinity;
    this.pending = null;
    this.connected = false;
    this.labels = new Map();
  }

  receiveCamera(camera) {
    if (!camera || !Number.isInteger(camera.width) ||
        !Number.isInteger(camera.height) || camera.width <= 0 ||
        camera.height <= 0 || camera.width > 4096 || camera.height > 4096 ||
        !camera.header?.frame_id)
      return;
    if (this.camera && this.camera.header.frame_id !== camera.header.frame_id)
      this.tracks = null;
    this.camera = camera;
  }

  receiveLabels(message) {
    if (!Array.isArray(message?.class_map) || message.class_map.length > 512)
      return;
    this.labels = new Map(message.class_map.map(
        (entry) => [String(entry.class_id), entry.class_name]));
  }

  receiveTracks(tracks) {
    const stamp = stampMs(tracks?.header);
    if (!Number.isFinite(stamp) || !Array.isArray(tracks.detections) ||
        tracks.detections.length > 128)
      return;
    if (this.tracks && stamp <= stampMs(this.tracks.header)) return;
    this.tracks = tracks;
  }

  receiveHealth(report) {
    const status = report?.status?.find((s) => s.name === 'autonomy/guidance');
    if (!status) return;
    this.health =
        Object.fromEntries(status.values.map((v) => [v.key, v.value]));
    this.healthReceived = performance.now();
    if (this.pending?.accepted &&
        this.health.selected_track === this.pending.id) {
      this.notify(
          `Following target ${this.pending.id.split(':').at(-1)}`, false);
      this.pending = null;
    }
  }

  unavailable(videoReady, now = Date.now()) {
    if (!this.connected) return 'Connect ROS to select targets';
    if (!videoReady) return 'Waiting for live video';
    if (!this.camera || !this.tracks ||
        this.camera.header.frame_id !== this.tracks.header.frame_id)
      return 'Waiting for matching camera and tracking data';
    const age = now - stampMs(this.tracks.header);
    if (age < -100 || age > 350)
      return 'Tracking data is stale or clocks differ';
    if (performance.now() - this.healthReceived > 2500)
      return 'Guidance status is stale';
    if (this.health.state !== 'following')
      return 'Following must already be active';
    if (this.pending) return 'Waiting for target acknowledgement';
    return '';
  }

  detections() {
    if (!this.camera || !this.tracks) return [];
    return this.tracks.detections.filter(
        (d) => validBox(d, this.camera) && d.results?.length === 1 && [
          'person', 'bicycle', 'car'
        ].includes(this.labels.get(d.results[0].hypothesis.class_id)));
  }

  async choose(id, videoReady) {
    const reason = this.unavailable(videoReady);
    if (reason) {
      this.notify(reason, true);
      return;
    }
    if (!this.detections().some((d) => d.id === id)) {
      this.notify('Target is no longer visible', true);
      return;
    }
    if (this.health.selected_track === id) {
      this.notify('Already following this target', false);
      return;
    }
    const pending = {id, accepted: false};
    this.pending = pending;
    this.notify('Requesting target switch…', false);
    try {
      const reply = await this.select(id);
      if (this.pending !== pending) return;
      if (!reply?.success)
        throw new Error(reply?.message || 'Selection rejected');
      pending.accepted = true;
      if (this.health.selected_track === id) {
        this.pending = null;
        this.notify('Target switch acknowledged', false);
      } else {
        this.notify(
            'Request accepted; waiting for active-target status', false);
      }
    } catch (error) {
      if (this.pending === pending) {
        this.pending = null;
        this.notify(error.message, true);
      }
    }
  }
}
