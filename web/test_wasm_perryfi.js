"use strict";

const assert = require("node:assert/strict");
const dgram = require("node:dgram");
const net = require("node:net");
const path = require("node:path");
const WebSocket = require("./relay/node_modules/ws");
const P = require("./perryfi-relay-protocol.js");
const { PerryfiBridge } = require("./perryfi-bridge.js");
const { createRelayServer } = require("./relay/server.js");
const create1985 = require("./dist/1985.js");

const Op = {
  HELLO: 0x01,
  DNS: 0x20,
  TCP_OPEN: 0x30,
  TCP_CLOSE: 0x31,
  TCP_SEND: 0x32,
  TCP_RECV: 0x35,
  UDP_OPEN: 0x40,
  UDP_CLOSE: 0x41,
  UDP_SEND: 0x42,
  ACK: 0x80,
  EVENT: 0x81,
  UDP_DATA: 0x83,
};

function crc16(data) {
  let crc = 0xffff;
  for (const byte of data) {
    crc ^= byte << 8;
    for (let bit = 0; bit < 8; bit++)
      crc = crc & 0x8000 ? ((crc << 1) ^ 0x1021) & 0xffff : (crc << 1) & 0xffff;
  }
  return crc;
}

function perrynetFrame(opcode, sequence, channel, payload = new Uint8Array()) {
  const body = P.concat(
    new Uint8Array([1, opcode, sequence, channel]),
    P.u16(payload.length),
    payload
  );
  const crc = crc16(body);
  const framed = [0xc0];
  for (const byte of P.concat(body, P.u16(crc))) {
    if (byte === 0xc0) framed.push(0xdb, 0xdc);
    else if (byte === 0xdb) framed.push(0xdb, 0xdd);
    else framed.push(byte);
  }
  framed.push(0xc0);
  return new Uint8Array(framed);
}

class PerryNetDecoder {
  constructor() {
    this.frames = [];
    this.current = [];
    this.escaped = false;
  }

  push(bytes) {
    for (let byte of bytes) {
      if (byte === 0xc0) {
        if (this.current.length) this.finish();
        this.current = [];
        this.escaped = false;
      } else if (this.escaped) {
        if (byte === 0xdc) byte = 0xc0;
        else if (byte === 0xdd) byte = 0xdb;
        else throw new Error("invalid SLIP escape");
        this.current.push(byte);
        this.escaped = false;
      } else if (byte === 0xdb) {
        this.escaped = true;
      } else {
        this.current.push(byte);
      }
    }
  }

  finish() {
    const frame = new Uint8Array(this.current);
    assert.ok(frame.length >= 8, "short PerryNet frame");
    const length = P.readU16(frame, 4);
    assert.equal(frame.length, 8 + length, "PerryNet frame length");
    assert.equal(P.readU16(frame, frame.length - 2), crc16(frame.subarray(0, -2)));
    this.frames.push({
      opcode: frame[1],
      sequence: frame[2],
      channel: frame[3],
      payload: frame.subarray(6, frame.length - 2),
    });
  }
}

function listen(server, ...args) {
  return new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(...args, () => {
      server.off("error", reject);
      resolve(server.address());
    });
  });
}

function bind(socket, ...args) {
  return new Promise((resolve, reject) => {
    socket.once("error", reject);
    socket.bind(...args, () => {
      socket.off("error", reject);
      resolve(socket.address());
    });
  });
}

function close(server) {
  return new Promise(resolve => server.close(resolve));
}

function delay(milliseconds) {
  return new Promise(resolve => setTimeout(resolve, milliseconds));
}

(async () => {
  const tcpServer = net.createServer(socket => socket.on("data", data => socket.write(data)));
  const tcpAddress = await listen(tcpServer, 0, "127.0.0.1");
  const udpServer = dgram.createSocket("udp4");
  udpServer.on("message", (data, remote) => udpServer.send(data, remote.port, remote.address));
  const udpAddress = await bind(udpServer, 0, "127.0.0.1");
  const relay = createRelayServer({
    host: "127.0.0.1",
    port: 0,
    allowMissingOrigin: true,
    allowPrivate: true,
    tcpPorts: [tcpAddress.port],
    udpPorts: [udpAddress.port],
  });

  let module;
  let bridge;
  let writePointer = 0;
  let readPointer = 0;
  try {
    const relayAddress = await relay.listen();
    bridge = new PerryfiBridge({
      WebSocketCtor: WebSocket,
      endpoint: `ws://127.0.0.1:${relayAddress.port}/perryfi`,
    });
    globalThis.JS1985PerryfiBridge = bridge;
    module = await create1985({
      locateFile: file => path.join(__dirname, "dist", file),
    });
    bridge.attachModule(module);
    assert.equal(module._poc_init(), 0);
    assert.equal(module._poc_set_perryfi(1, 1), 1);

    const online = new Promise((resolve, reject) => {
      const timeout = setTimeout(() => reject(new Error("relay did not connect")), 2000);
      const unsubscribe = bridge.onStatus(status => {
        if (status === "online") {
          clearTimeout(timeout);
          unsubscribe();
          resolve();
        }
      });
    });
    await online;

    writePointer = module._malloc(2048);
    readPointer = module._malloc(8192);
    const decoder = new PerryNetDecoder();

    function serialWrite(bytes) {
      module.HEAPU8.set(bytes, writePointer);
      assert.equal(module._poc_perryfi_serial_write(writePointer, bytes.length), bytes.length);
    }

    function serialRead() {
      const count = module._poc_perryfi_serial_read(readPointer, 8192);
      if (count > 0)
        decoder.push(new Uint8Array(module.HEAPU8.subarray(readPointer, readPointer + count)));
    }

    async function waitFrame(predicate) {
      const deadline = Date.now() + 2500;
      while (Date.now() < deadline) {
        module._poc_step();
        serialRead();
        const index = decoder.frames.findIndex(predicate);
        if (index >= 0) return decoder.frames.splice(index, 1)[0];
        await delay(5);
      }
      throw new Error("timed out waiting for PerryNet frame");
    }

    serialWrite(perrynetFrame(Op.HELLO, 1, 0));
    let frame = await waitFrame(item => item.opcode === Op.ACK && item.sequence === 1);
    assert.equal(frame.payload[0], 0);

    serialWrite(perrynetFrame(Op.DNS, 2, 0, P.encodeText("127.0.0.1")));
    frame = await waitFrame(item => item.opcode === Op.ACK && item.sequence === 2);
    assert.equal(frame.payload[0], 0);
    assert.deepEqual([...frame.payload.subarray(1)], [127, 0, 0, 1]);

    const host = P.encodeText("127.0.0.1");
    serialWrite(perrynetFrame(
      Op.TCP_OPEN, 3, 0,
      P.concat(new Uint8Array([host.length]), host, P.u16(tcpAddress.port),
               new Uint8Array([0x03]))
    ));
    frame = await waitFrame(item => item.opcode === Op.ACK && item.sequence === 3);
    assert.equal(frame.payload[0], 0);
    const tcpChannel = frame.payload[1];

    serialWrite(perrynetFrame(Op.TCP_SEND, 4, tcpChannel, P.encodeText("wasm tcp")));
    frame = await waitFrame(item => item.opcode === Op.ACK && item.sequence === 4);
    assert.equal(frame.payload[0], 0);
    await delay(20);
    serialWrite(perrynetFrame(Op.TCP_RECV, 5, tcpChannel, P.u16(192)));
    frame = await waitFrame(item => item.opcode === Op.ACK && item.sequence === 5);
    assert.equal(frame.payload[0], 0);
    assert.equal(P.decodeText(frame.payload.subarray(1)), "wasm tcp");

    serialWrite(perrynetFrame(Op.TCP_CLOSE, 6, tcpChannel));
    frame = await waitFrame(item => item.opcode === Op.ACK && item.sequence === 6);
    assert.equal(frame.payload[0], 0);

    serialWrite(perrynetFrame(Op.UDP_OPEN, 7, 0, P.u16(0)));
    frame = await waitFrame(item => item.opcode === Op.ACK && item.sequence === 7);
    assert.equal(frame.payload[0], 0);
    const udpChannel = frame.payload[1];
    serialWrite(perrynetFrame(
      Op.UDP_SEND, 8, udpChannel,
      P.concat(new Uint8Array([127, 0, 0, 1]), P.u16(udpAddress.port),
               P.encodeText("wasm udp"))
    ));
    frame = await waitFrame(item => item.opcode === Op.ACK && item.sequence === 8);
    assert.equal(frame.payload[0], 0);
    frame = await waitFrame(item => item.opcode === Op.UDP_DATA && item.channel === udpChannel);
    assert.equal(P.decodeText(frame.payload.subarray(6)), "wasm udp");
    serialWrite(perrynetFrame(Op.UDP_CLOSE, 9, udpChannel));
    await waitFrame(item => item.opcode === Op.ACK && item.sequence === 9);

    assert.equal(module._poc_set_perryfi(1, 0), 1);
    while (!bridge.isConnected()) await delay(5);
    const encoder = new TextEncoder();
    const decoderText = new TextDecoder();
    let hayesText = "";
    function hayesWrite(text) { serialWrite(encoder.encode(text)); }
    async function waitHayes(needle) {
      const deadline = Date.now() + 2500;
      while (Date.now() < deadline) {
        module._poc_step();
        const count = module._poc_perryfi_serial_read(readPointer, 8192);
        if (count > 0)
          hayesText += decoderText.decode(module.HEAPU8.subarray(readPointer, readPointer + count));
        if (hayesText.includes(needle)) return;
        await delay(5);
      }
      throw new Error(`timed out waiting for Hayes text: ${needle}`);
    }

    hayesWrite("AT\r");
    await waitHayes("OK");
    hayesWrite(`ATD127.0.0.1:${tcpAddress.port}\r`);
    await waitHayes("CONNECT");
    hayesText = "";
    hayesWrite("hayes tcp");
    await waitHayes("hayes tcp");

    console.log("WASM PerryFi relay tests passed");
  } finally {
    if (module && writePointer) module._free(writePointer);
    if (module && readPointer) module._free(readPointer);
    if (module) module._poc_set_perryfi(0, 0);
    if (bridge) bridge.setDevice(false, 0);
    await relay.close();
    await close(tcpServer);
    await close(udpServer);
  }
})().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
