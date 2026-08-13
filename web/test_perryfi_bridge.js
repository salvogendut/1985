"use strict";

const assert = require("node:assert/strict");
const P = require("./perryfi-relay-protocol.js");
const { PerryfiBridge } = require("./perryfi-bridge.js");

class FakeWebSocket {
  static instances = [];

  constructor(url) {
    this.url = url;
    this.readyState = 0;
    this.bufferedAmount = 0;
    this.listeners = new Map();
    this.sent = [];
    FakeWebSocket.instances.push(this);
  }

  addEventListener(type, listener) {
    if (!this.listeners.has(type)) this.listeners.set(type, []);
    this.listeners.get(type).push(listener);
  }

  emit(type, value = {}) {
    for (const listener of this.listeners.get(type) || []) listener(value);
  }

  open() {
    this.readyState = 1;
    this.emit("open");
  }

  send(value) { this.sent.push(new Uint8Array(value)); }

  close() {
    if (this.readyState === 3) return;
    this.readyState = 3;
    this.emit("close");
  }
}

const callbacks = [];
const wasmModule = {
  _poc_perryfi_dns_result: (...args) => callbacks.push(["dns", ...args]),
  _poc_perryfi_tcp_open_result: (...args) => callbacks.push(["tcp", ...args]),
  _poc_perryfi_udp_open_result: (...args) => callbacks.push(["udp", ...args]),
};

const bridge = new PerryfiBridge({
  WebSocketCtor: FakeWebSocket,
  endpoint: "ws://127.0.0.1:1985/perryfi",
});
bridge.attachModule(wasmModule);
bridge.setDevice(true, 1);
const socket = FakeWebSocket.instances.at(-1);
socket.open();
assert.equal(P.decode(socket.sent.shift()).type, P.Type.HELLO);
socket.emit("message", { data: P.encode(P.Type.READY) });
assert.equal(bridge.isConnected(), true);

assert.equal(bridge.dns("example.com"), true);
let request = P.decode(socket.sent.shift());
assert.equal(request.type, P.Type.DNS);
socket.emit("message", {
  data: P.encode(P.Type.DNS_RESULT, 0, request.request,
                 new Uint8Array([P.Status.OK, 93, 184, 216, 34])),
});
assert.deepEqual(callbacks.shift(), ["dns", P.Status.OK, 93, 184, 216, 34]);

assert.equal(bridge.tcpOpen(0, "example.com", 80, 3), true);
request = P.decode(socket.sent.shift());
socket.emit("message", {
  data: P.encode(P.Type.TCP_OPEN_RESULT, 1, request.request,
                 P.concat(new Uint8Array([P.Status.OK, 192, 0, 2, 1]), P.u16(40000))),
});
assert.deepEqual(callbacks.shift(),
                 ["tcp", 0, P.Status.OK, 192, 0, 2, 1, 40000]);

socket.emit("message", {
  data: P.encode(P.Type.TCP_DATA, 1, 0, P.encodeText("hello")),
});
const heap = new Uint8Array(32);
assert.equal(bridge.tcpRead(0, heap, 3, 4), 4);
assert.equal(P.decodeText(heap.subarray(3, 7)), "hell");
assert.equal(bridge.tcpRead(0, heap, 0, 8), 1);
assert.equal(P.decodeText(heap.subarray(0, 1)), "o");

assert.equal(bridge.udpOpen(1, 0), true);
request = P.decode(socket.sent.shift());
socket.emit("message", {
  data: P.encode(P.Type.UDP_OPEN_RESULT, 2, request.request,
                 P.concat(new Uint8Array([P.Status.OK]), P.u16(42000))),
});
assert.deepEqual(callbacks.shift(), ["udp", 1, P.Status.OK, 42000]);
socket.emit("message", {
  data: P.encode(P.Type.UDP_DATA, 2, 0,
                 P.concat(new Uint8Array([8, 8, 8, 8]), P.u16(123),
                          P.encodeText("time"))),
});
assert.equal(bridge.udpRead(1, heap, 8, 12, 16, 8), 4);
assert.deepEqual([...heap.subarray(8, 12)], [8, 8, 8, 8]);
assert.equal(heap[12] | (heap[13] << 8), 123);
assert.equal(P.decodeText(heap.subarray(16, 20)), "time");

bridge.setDevice(false, 0);
assert.equal(bridge.isConnected(), false);

async function testRequestTimeout() {
  const timedCallbacks = [];
  const timedBridge = new PerryfiBridge({
    WebSocketCtor: FakeWebSocket,
    endpoint: "ws://127.0.0.1:1985/perryfi",
    requestTimeoutMs: 10,
  });
  timedBridge.attachModule({
    _poc_perryfi_tcp_open_result: (...args) => timedCallbacks.push(args),
  });
  timedBridge.setDevice(true, 1);
  const timedSocket = FakeWebSocket.instances.at(-1);
  timedSocket.open();
  assert.equal(P.decode(timedSocket.sent.shift()).type, P.Type.HELLO);
  timedSocket.emit("message", { data: P.encode(P.Type.READY) });

  assert.equal(timedBridge.tcpOpen(2, "example.com", 80, 0), true);
  assert.equal(P.decode(timedSocket.sent.shift()).type, P.Type.TCP_OPEN);
  await new Promise(resolve => setTimeout(resolve, 30));
  assert.deepEqual(timedCallbacks.shift(),
                   [2, P.Status.CONNECT_FAILED, 0, 0, 0, 0, 0]);
  const close = P.decode(timedSocket.sent.shift());
  assert.equal(close.type, P.Type.TCP_CLOSE);
  assert.equal(close.channel, 3);

  timedBridge.setDevice(false, 0);
}

testRequestTimeout().then(() => {
  console.log("PerryFi browser bridge tests passed");
}).catch(error => {
  console.error(error);
  process.exitCode = 1;
});
