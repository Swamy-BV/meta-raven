// SPDX-License-Identifier: AGPL-3.0-only
export function contentRect(width, height, videoWidth, videoHeight) {
  if (![width, height, videoWidth, videoHeight].every(
          (v) => Number.isFinite(v) && v > 0))
    return null;
  const scale = Math.min(width / videoWidth, height / videoHeight);
  const w = videoWidth * scale;
  const h = videoHeight * scale;
  return {x: (width - w) / 2, y: (height - h) / 2, width: w, height: h};
}

export function imagePoint(x, y, rect, camera) {
  if (!rect || x < rect.x || y < rect.y || x > rect.x + rect.width ||
      y > rect.y + rect.height)
    return null;
  return {
    x: ((x - rect.x) / rect.width) * camera.width,
    y: ((y - rect.y) / rect.height) * camera.height
  };
}

export function hits(point, detections) {
  if (!point) return [];
  return detections.filter((d) => {
    const b = d.bbox;
    return Math.abs(point.x - b.center.position.x) <= b.size_x / 2 &&
        Math.abs(point.y - b.center.position.y) <= b.size_y / 2;
  });
}

export function validBox(detection, camera) {
  const b = detection?.bbox;
  return typeof detection?.id === 'string' && detection.id.length > 0 &&
      detection.id.length <= 256 && b &&
      [b.center?.position?.x, b.center?.position?.y, b.size_x, b.size_y].every(
          Number.isFinite) &&
      b.center.theta === 0 && b.size_x > 0 && b.size_y > 0 &&
      b.size_x <= camera.width * 2 && b.size_y <= camera.height * 2;
}

export function stampMs(header) {
  const stamp = header?.stamp;
  return stamp && Number.isInteger(stamp.sec) &&
          Number.isInteger(stamp.nanosec) && stamp.nanosec >= 0 &&
          stamp.nanosec < 1e9 ?
      stamp.sec * 1000 + stamp.nanosec / 1e6 :
      NaN;
}
