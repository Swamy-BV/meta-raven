// SPDX-License-Identifier: AGPL-3.0-only
import {stampMs} from './geometry.js';
import {RosLink} from './ros-link.js';
import {SelectionSession} from './session.js';
import {TargetView} from './view.js';

const byId = (id) => document.getElementById(id);
const guidance = '/raven/vehicle_1/autonomy/guidance';
let reader = null;
let videoError = '';
let subscriptionStarted = false;
let targetKey = '';
const notify = (message, error) => {
  byId('message').textContent = message;
  byId('message').classList.toggle('error', error);
};
const session = new SelectionSession(
    (id) => ros.call(
        `${guidance}/select_target`, 'raven_interfaces/srv/SelectTarget',
        {track_id: id}),
    notify);
const ros = new RosLink((connected, message) => {
  session.connected = connected;
  byId('ros-status').textContent =
      connected ? 'ROS connected' : 'ROS disconnected';
  byId('ros-status').classList.toggle('online', connected);
  if (!connected) {
    session.tracks = null;
    session.healthReceived = -Infinity;
    subscriptionStarted = false;
  }
  if (connected && !subscriptionStarted) {
    subscriptionStarted = true;
    ros.subscribe(
        '/raven/vehicle_1/perception/tracks',
        'vision_msgs/msg/Detection2DArray',
        (msg) => session.receiveTracks(msg));
    ros.subscribe(
        '/raven/vehicle_1/cameras/front/camera_info',
        'sensor_msgs/msg/CameraInfo', (msg) => session.receiveCamera(msg));
    ros.subscribe(
        '/raven/vehicle_1/perception/label_info', 'vision_msgs/msg/LabelInfo',
        (msg) => session.receiveLabels(msg), true);
    ros.subscribe(
        `${guidance}/health`, 'diagnostic_msgs/msg/DiagnosticArray',
        (msg) => session.receiveHealth(msg), true);
  }
  notify(message, !connected);
});

function showChoices(detections) {
  const choices = byId('choices');
  choices.replaceChildren(
      document.createTextNode('Overlapping boxes — choose a target: '));
  for (const d of detections) {
    const button = document.createElement('button');
    button.textContent = `Target ${d.id.split(':').at(-1)}`;
    button.onclick = () => {
      choices.hidden = true;
      void session.choose(d.id, view.ready());
    };
    choices.append(button);
  }
  choices.hidden = false;
}
const view = new TargetView(byId('video'), byId('boxes'), session, showChoices);

function disconnect() {
  reader?.close();
  reader = null;
  byId('video').srcObject = null;
  ros.close();
  session.reset();
  byId('choices').hidden = true;
}

byId('settings').addEventListener('submit', (event) => {
  event.preventDefault();
  disconnect();
  try {
    const endpoint = new URL(byId('video-url').value);
    if (!['http:', 'https:'].includes(endpoint.protocol) ||
        !endpoint.pathname.endsWith('/whep'))
      throw new Error('Use a WebRTC /whep endpoint');
    ros.connect(byId('ros-url').value);
    reader = new window.MediaMTXWebRTCReader({
      url: endpoint.href,
      onTrack: (event) => {
        const video = byId('video');
        if (video.srcObject !== event.streams[0])
          video.srcObject = event.streams[0];
        video.play().catch((error) => {
          if (error.name !== 'AbortError') videoError = error.message;
        });
      },
      onError: (message) => {
        videoError = message;
        view.lastFrame = -Infinity;
      }
    });
    byId('settings').hidden = true;
    byId('settings-toggle').setAttribute('aria-expanded', 'false');
  } catch (error) {
    disconnect();
    notify(error.message, true);
  }
});
byId('disconnect').onclick = disconnect;
byId('settings-toggle').onclick = () => {
  byId('settings').hidden = !byId('settings').hidden;
  byId('settings-toggle')
      .setAttribute('aria-expanded', String(!byId('settings').hidden));
};
window.addEventListener('pagehide', disconnect);

function updateTargets(ready) {
  const targets = byId('targets');
  const detections = session.detections();
  const key = detections.map((d) => d.id).join('|');
  if (key !== targetKey) {
    targetKey = key;
    targets.replaceChildren();
    for (const detection of detections) {
      const button = document.createElement('button');
      button.textContent = `Target ${detection.id.split(':').at(-1)}`;
      button.dataset.trackId = detection.id;
      button.onclick = () => void session.choose(detection.id, view.ready());
      targets.append(button);
    }
  }
  const unavailable = Boolean(session.unavailable(ready));
  for (const button of targets.children) {
    button.disabled = unavailable;
    button.setAttribute(
        'aria-pressed',
        String(button.dataset.trackId === session.health.selected_track));
  }
}

function update() {
  const ready = view.ready();
  const reason = view.draw();
  updateTargets(ready);
  const h = session.health;
  byId('video-status').textContent = ready ? 'Video live' : 'Video waiting';
  byId('video-status').classList.toggle('online', ready);
  byId('empty-state').hidden = ready;
  if (ready) videoError = '';
  byId('empty-state').textContent =
      videoError || 'Waiting for the WFB-ng video stream';
  byId('hint').textContent =
      reason || 'Click an outlined target to switch following';
  byId('mode').textContent = h.state || 'Disconnected';
  byId('target').textContent = h.selected_track?.split(':').at(-1) || '—';
  byId('target').title = h.selected_track || '';
  const speed = Number(h.target_speed_mps), range = Number(h.target_range_m);
  byId('motion').textContent =
      Number.isFinite(speed) && Number.isFinite(range) ?
      `${(speed * 2.236936).toFixed(1)} mph / ${range.toFixed(1)} m` :
      '—';
  const age = Date.now() - stampMs(session.tracks?.header);
  byId('tracking').textContent = Number.isFinite(age) ?
      `${Math.max(0, age).toFixed(0)} ms · ${h.target_switches || 0} switches` :
      'Waiting';
  requestAnimationFrame(update);
}
requestAnimationFrame(update);
