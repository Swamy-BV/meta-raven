// SPDX-License-Identifier: AGPL-3.0-only
// A small client for rosbridge's standard subscribe and call_service
// operations.
export class RosLink {
  constructor(onState) {
    this.onState = onState;
    this.socket = null;
    this.pending = new Map();
    this.topics = new Map();
    this.nextId = 0;
  }

  connect(url) {
    this.close();
    const endpoint = new URL(url);
    if (!['ws:', 'wss:'].includes(endpoint.protocol))
      throw new Error('Use a ws:// or wss:// ROS endpoint');
    const socket = new WebSocket(endpoint);
    this.socket = socket;
    socket.onopen = () => {
      if (this.socket === socket) this.onState(true, 'ROS connected');
    };
    socket.onclose = () => {
      if (this.socket === socket)
        this.close('ROS disconnected; pending requests are not retried');
    };
    socket.onerror = () => {
      if (this.socket === socket) this.onState(false, 'ROS connection failed');
    };
    socket.onmessage = (event) => {
      if (this.socket !== socket) return;
      try {
        this.receive(JSON.parse(event.data));
      } catch (error) {
        this.onState(false, `Invalid ROS response: ${error.message}`);
      }
    };
  }

  send(message) {
    if (this.socket?.readyState !== WebSocket.OPEN)
      throw new Error('ROS connection is not open');
    this.socket.send(JSON.stringify(message));
  }

  subscribe(topic, type, callback, durable = false) {
    this.topics.set(topic, callback);
    this.send({
      op: 'subscribe',
      id: `topic:${topic}`,
      topic,
      type,
      throttle_rate: 0,
      queue_length: 1,
      qos: {
        reliability: durable ? 'reliable' : 'best_effort',
        durability: durable ? 'transient_local' : 'volatile',
        history: 'keep_last',
        depth: 1
      }
    });
  }

  call(service, type, args) {
    const id = `select:${++this.nextId}`;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(
            'Selection outcome unknown; check the active target. No retry was sent.'));
      }, 6500);
      this.pending.set(id, {resolve, reject, timer});
      try {
        this.send({op: 'call_service', id, service, type, args, timeout: 5});
      } catch (error) {
        clearTimeout(timer);
        this.pending.delete(id);
        reject(error);
      }
    });
  }

  receive(message) {
    if (message.op === 'publish') this.topics.get(message.topic)?.(message.msg);
    if (message.op === 'service_response' && this.pending.has(message.id)) {
      const request = this.pending.get(message.id);
      this.pending.delete(message.id);
      clearTimeout(request.timer);
      if (message.result === false)
        request.reject(
            new Error('ROS service request failed; check active target'));
      else
        request.resolve(message.values);
    }
    if (message.op === 'status' && message.level === 'error')
      this.onState(false, message.msg || 'ROS bridge error');
  }

  close(reason = 'Disconnected') {
    const socket = this.socket;
    this.socket = null;
    socket?.close();
    this.topics.clear();
    for (const request of this.pending.values()) {
      clearTimeout(request.timer);
      request.reject(new Error(
          'Connection closed; selection outcome unknown. No retry was sent.'));
    }
    this.pending.clear();
    this.onState(false, reason);
  }
}
