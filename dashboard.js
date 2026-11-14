'use strict';
const root = 'lab3/de2/vuon-dht22-5c8f14';
const byId = id => document.getElementById(id);
const series = [];
let device = null, deviceSeen = 0, sampleSeen = 0;
const pending = new Map();
byId('namespace').textContent = root;

function draw(id, key, color) {
  const canvas = byId(id);
  const width = canvas.clientWidth, height = canvas.clientHeight;
  const scale = window.devicePixelRatio || 1;
  canvas.width = width * scale;
  canvas.height = height * scale;
  const ctx = canvas.getContext('2d');
  ctx.scale(scale, scale);
  ctx.font = '11px system-ui';
  const x0 = 32, x1 = width - 12, y0 = 10, y1 = height - 25;
  const values = series.map(row => row[key]);
  const low = values.length ? Math.floor(Math.min(...values) - 2) : 0;
  const high = values.length ? Math.ceil(Math.max(...values) + 2) : 100;
  for (let i = 0; i < 5; i++) {
    const y = y0 + i * (y1 - y0) / 4;
    ctx.strokeStyle = '#d8e5de';
    ctx.beginPath(); ctx.moveTo(x0, y); ctx.lineTo(x1, y); ctx.stroke();
    ctx.fillStyle = '#516861';
    ctx.fillText((high - i * (high - low) / 4).toFixed(0), 0, y + 4);
  }
  if (!values.length) {
    ctx.textAlign = 'center';
    ctx.fillText('Chờ dữ liệu từ ESP32', (x0 + x1) / 2, (y0 + y1) / 2);
    return;
  }
  const from = series[0].time, to = series.at(-1).time;
  const xAt = i => x0 + (series[i].time - from) / Math.max(1, to - from) * (x1 - x0);
  const yAt = value => y1 - (value - low) / (high - low) * (y1 - y0);
  ctx.strokeStyle = color; ctx.lineWidth = 2; ctx.beginPath();
  values.forEach((value, i) => i ? ctx.lineTo(xAt(i), yAt(value)) : ctx.moveTo(xAt(i), yAt(value)));
  ctx.stroke();
  ctx.fillStyle = color; ctx.beginPath();
  ctx.arc(xAt(values.length - 1), yAt(values.at(-1)), 3.5, 0, 2 * Math.PI); ctx.fill();
  ctx.fillStyle = '#516861';
  ctx.fillText(new Date(from).toLocaleTimeString('vi-VN'), x0, height - 6);
  if (to > from && x1 - x0 > 120) {
    ctx.textAlign = 'right';
    ctx.fillText(new Date(to).toLocaleTimeString('vi-VN'), x1, height - 6);
  }
}
function drawCharts() {
  draw('tempChart', 'temperature', '#ac552a');
  draw('humChart', 'humidity', '#167970');
}
window.addEventListener('resize', drawCharts);
drawCharts();

const client = window.mqtt?.connect('wss://broker.hivemq.com:8884/mqtt', {
  reconnectPeriod: 3000, connectTimeout: 8000, clean: true, keepalive: 20, queueQoSZero: false
});
function renderOutputs() {
  const live = !!client?.connected && device && Date.now() - deviceSeen < 8200;
  for (const [key, label] of [['pump', 'bơm'], ['fan', 'quạt']]) {
    const button = byId(key);
    const on = live && device[key] === 1;
    button.textContent = pending.has(key) ? 'Đang gửi…' : live ? (on ? 'Tắt ' : 'Bật ') + label : '—';
    button.setAttribute('aria-pressed', String(!!on));
    button.disabled = !live || pending.has(key);
  }
  byId('mist').textContent = live ? device.mist + '% PWM' : '—';
}
function clearDevice(message) {
  device = null;
  pending.clear();
  renderOutputs();
  byId('commandStatus').textContent = message;
}
function receive(topic, payload) {
  const kind = topic.slice(root.length + 1);
  if (kind === 'availability') {
    if (payload.toString() === 'offline') clearDevice('ESP32 đã ngắt kết nối.');
    return;
  }
  let data;
  try { data = JSON.parse(payload.toString()); } catch { return; }
  if (!data || typeof data !== 'object') return;
  const now = Date.now();
  if (kind === 'telemetry') {
    sampleSeen = now;
    const valid = data.valid === true && Number.isFinite(data.temperature) &&
      Number.isFinite(data.humidity) && data.humidity >= 0 && data.humidity <= 100;
    for (const key of ['temperature', 'humidity']) byId(key).textContent = valid ? data[key].toFixed(1) : '—';
    if (valid) {
      series.push({time: now, temperature: data.temperature, humidity: data.humidity});
      if (series.length > 40) series.shift();
      drawCharts();
    }
    byId('sampleStatus').textContent = valid ? 'Cập nhật ' + new Date(now).toLocaleTimeString('vi-VN') :
      'ESP32 báo lỗi đọc DHT22.';
  }
  if (kind === 'state' && [0, 1].includes(data.pump) && [0, 1].includes(data.fan) &&
      [0, 25, 50, 100].includes(data.mist)) {
    if (!device) byId('commandStatus').textContent = 'Đã nhận trạng thái từ ESP32.';
    device = data;
    deviceSeen = now;
    for (const [key, command] of pending) {
      if (data[key] === command.desired) {
        pending.delete(key);
        byId('commandStatus').textContent = 'ESP32 đã phản hồi trạng thái mới.';
      }
    }
    renderOutputs();
  }
}
if (client) {
  client.on('connect', () => {
    client.subscribe(['telemetry', 'state', 'availability'].map(name => root + '/' + name), error => {
      byId('connection').textContent = error ? 'Lỗi đăng ký MQTT' : 'MQTT đã kết nối';
    });
  });
  client.on('message', receive);
  client.on('close', () => {
    byId('connection').textContent = 'MQTT mất kết nối · đang thử lại';
    clearDevice('Chờ kết nối lại.');
  });
  client.on('error', () => { byId('connection').textContent = 'Lỗi kết nối MQTT'; });
} else {
  byId('connection').textContent = 'Không tải được thư viện MQTT';
}
for (const key of ['pump', 'fan']) {
  byId(key).addEventListener('click', () => {
    if (!client?.connected || !device || Date.now() - deviceSeen >= 8200 || pending.has(key)) return;
    const desired = 1 - device[key];
    pending.set(key, {desired, deadline: Date.now() + 5000});
    byId('commandStatus').textContent = 'Đang chờ ESP32 xác nhận…';
    renderOutputs();
    client.publish(root + '/cmd/' + key, String(desired), {qos: 0, retain: false}, error => {
      if (error) {
        pending.delete(key);
        byId('commandStatus').textContent = 'Gửi lệnh thất bại.';
        renderOutputs();
      }
    });
  });
}
setInterval(() => {
  const now = Date.now();
  if (device && now - deviceSeen >= 8200) clearDevice('Đã quá thời gian chờ trạng thái ESP32.');
  for (const [key, command] of pending) {
    if (now >= command.deadline) {
      pending.delete(key);
      byId('commandStatus').textContent = 'Chưa nhận xác nhận từ ESP32.';
      renderOutputs();
    }
  }
  if (sampleSeen && now - sampleSeen >= 8200) {
    for (const key of ['temperature', 'humidity']) byId(key).textContent = '—';
    byId('sampleStatus').textContent = 'Mẫu cảm biến quá hạn.';
    sampleSeen = 0;
  }
}, 500);
