"use strict";

const assert = require("node:assert/strict");
const dgram = require("node:dgram");
const net = require("node:net");
const { after, before, test } = require("node:test");
const WebSocket = require("ws");
const P = require("../../perryfi-relay-protocol.js");
const { createRelayServer, isPublicIPv4 } = require("../server.js");

let tcpServer;
let udpServer;
let relay;
let relayAddress;

function listen(server, ...args) {
  return new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(...args, () => {
      server.off("error", reject);
      resolve(server.address());
    });
  });
}

function close(server) {
  return new Promise(resolve => server.close(resolve));
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

function sendDatagram(socket, data, port, address) {
  return new Promise((resolve, reject) => {
    socket.send(data, port, address, error => error ? reject(error) : resolve());
  });
}

function connectRelay(origin = "http://127.0.0.1:8080") {
  return new Promise((resolve, reject) => {
    const socket = new WebSocket(
      `ws://127.0.0.1:${relayAddress.port}/perryfi`,
      { origin }
    );
    socket.once("open", () => resolve(socket));
    socket.once("error", reject);
  });
}

function frameReader(socket) {
  const frames = [];
  const waiters = [];
  socket.on("message", data => {
    const frame = P.decode(data);
    const index = waiters.findIndex(waiter => waiter.predicate(frame));
    if (index >= 0) {
      const [waiter] = waiters.splice(index, 1);
      clearTimeout(waiter.timer);
      waiter.resolve(frame);
    } else {
      frames.push(frame);
    }
  });
  return (predicate, timeoutMs = 2000) => {
    const index = frames.findIndex(predicate);
    if (index >= 0) return Promise.resolve(frames.splice(index, 1)[0]);
    return new Promise((resolve, reject) => {
      const waiter = { predicate, resolve, reject };
      waiter.timer = setTimeout(() => {
        const index = waiters.indexOf(waiter);
        if (index >= 0) waiters.splice(index, 1);
        reject(new Error("timed out waiting for relay frame"));
      }, timeoutMs);
      waiters.push(waiter);
    });
  };
}

before(async () => {
  tcpServer = net.createServer(socket => socket.on("data", data => socket.write(data)));
  const tcpAddress = await listen(tcpServer, 0, "127.0.0.1");

  udpServer = dgram.createSocket("udp4");
  udpServer.on("message", (data, remote) =>
    udpServer.send(data, remote.port, remote.address));
  const udpAddress = await bind(udpServer, 0, "127.0.0.1");

  relay = createRelayServer({
    host: "127.0.0.1",
    port: 0,
    origins: ["http://127.0.0.1:8080"],
    allowPrivate: true,
    tcpPorts: [tcpAddress.port],
    udpPorts: [udpAddress.port],
    idleTimeoutMs: 10000,
  });
  relayAddress = await relay.listen();
  relay.tcpPort = tcpAddress.port;
  relay.udpPort = udpAddress.port;
});

after(async () => {
  if (relay) await relay.close();
  if (tcpServer?.listening) await close(tcpServer);
  if (udpServer) await close(udpServer);
});

test("private and reserved IPv4 ranges are denied by default", () => {
  assert.equal(isPublicIPv4("127.0.0.1"), false);
  assert.equal(isPublicIPv4("192.168.1.5"), false);
  assert.equal(isPublicIPv4("169.254.1.1"), false);
  assert.equal(isPublicIPv4("8.8.8.8"), true);
});

test("relay carries DNS, TCP and UDP operations", async () => {
  const socket = await connectRelay();
  const next = frameReader(socket);

  socket.send(P.encode(P.Type.HELLO));
  assert.equal((await next(frame => frame.type === P.Type.READY)).type, P.Type.READY);

  socket.send(P.encode(P.Type.DNS, 0, 1, P.encodeText("127.0.0.1")));
  let frame = await next(item => item.type === P.Type.DNS_RESULT && item.request === 1);
  assert.equal(frame.payload[0], P.Status.OK);
  assert.deepEqual([...frame.payload.subarray(1)], [127, 0, 0, 1]);

  socket.send(P.encode(P.Type.TCP_OPEN, 1, 2,
                       P.concat(new Uint8Array([1]), P.u16(relay.tcpPort),
                                P.encodeText("127.0.0.1"))));
  frame = await next(item => item.type === P.Type.TCP_OPEN_RESULT && item.request === 2);
  assert.equal(frame.payload[0], P.Status.OK);
  socket.send(P.encode(P.Type.TCP_SEND, 1, 0, P.encodeText("tcp echo")));
  frame = await next(item => item.type === P.Type.TCP_DATA && item.channel === 1);
  assert.equal(P.decodeText(frame.payload), "tcp echo");

  socket.send(P.encode(P.Type.UDP_OPEN, 2, 3, P.u16(0)));
  frame = await next(item => item.type === P.Type.UDP_OPEN_RESULT && item.request === 3);
  assert.equal(frame.payload[0], P.Status.OK);
  const relayUdpPort = P.readU16(frame.payload, 1);

  await sendDatagram(udpServer, Buffer.from("unsolicited"), relayUdpPort,
                     "127.0.0.1");
  await assert.rejects(
    next(item => item.type === P.Type.UDP_DATA && item.channel === 2, 100),
    /timed out/
  );

  socket.send(P.encode(P.Type.UDP_SEND, 2, 0,
                       P.concat(new Uint8Array([127, 0, 0, 1]),
                                P.u16(relay.udpPort), P.encodeText("udp echo"))));
  frame = await next(item => item.type === P.Type.UDP_DATA && item.channel === 2);
  assert.equal(P.decodeText(frame.payload.subarray(6)), "udp echo");

  socket.send(P.encode(P.Type.UDP_OPEN, 3, 4, P.u16(12345)));
  frame = await next(item => item.type === P.Type.UDP_OPEN_RESULT && item.request === 4);
  assert.equal(frame.payload[0], P.Status.BAD_ARGUMENT);

  socket.close();
  await new Promise(resolve => socket.once("close", resolve));
});

test("relay rejects an unexpected browser origin", async () => {
  await assert.rejects(connectRelay("https://untrusted.example"), /403/);
});
