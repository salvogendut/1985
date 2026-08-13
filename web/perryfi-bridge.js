(function (root, factory) {
  const protocol = root.JS1985PerryfiProtocol ||
    (typeof require === "function" ? require("./perryfi-relay-protocol.js") : null);
  const api = factory(protocol);
  if (typeof module === "object" && module.exports) module.exports = api;
  root.JS1985Perryfi = api;
  if (!root.JS1985PerryfiBridge) root.JS1985PerryfiBridge = api.createBridge();
}(typeof globalThis !== "undefined" ? globalThis : this, function (P) {
  "use strict";

  if (!P) throw new Error("PerryFi relay protocol is unavailable");

  class ByteQueue {
    constructor(limit) {
      this.limit = limit;
      this.parts = [];
      this.offset = 0;
      this.length = 0;
    }

    push(value) {
      const data = new Uint8Array(P.bytes(value));
      if (this.length + data.length > this.limit) return false;
      if (data.length) {
        this.parts.push(data);
        this.length += data.length;
      }
      return true;
    }

    read(target, offset, maximum) {
      let written = 0;
      while (written < maximum && this.parts.length) {
        const part = this.parts[0];
        const count = Math.min(maximum - written, part.length - this.offset);
        target.set(part.subarray(this.offset, this.offset + count), offset + written);
        written += count;
        this.offset += count;
        this.length -= count;
        if (this.offset === part.length) {
          this.parts.shift();
          this.offset = 0;
        }
      }
      return written;
    }
  }

  function pageDefaultEndpoint(locationObject) {
    if (!locationObject || !locationObject.host) return "";
    const scheme = locationObject.protocol === "https:" ? "wss:" : "ws:";
    return scheme + "//" + locationObject.host + "/perryfi";
  }

  function validEndpoint(value) {
    if (!value) return "";
    const url = new URL(value, typeof location === "object" ? location.href : undefined);
    if (url.protocol !== "ws:" && url.protocol !== "wss:")
      throw new Error("PerryFi relay must use WS or WSS");
    if (typeof location === "object" && location.protocol === "https:" &&
        url.protocol !== "wss:")
      throw new Error("HTTPS pages require a secure WSS relay");
    return url.href;
  }

  class PerryfiBridge {
    constructor(options = {}) {
      this.WebSocketCtor = options.WebSocketCtor ||
        (typeof WebSocket === "function" ? WebSocket : null);
      this.endpoint = options.endpoint || pageDefaultEndpoint(
        typeof location === "object" ? location : null
      );
      this.maxQueueBytes = options.maxQueueBytes || 65536;
      this.maxBufferedAmount = options.maxBufferedAmount || 262144;
      this.requestTimeoutMs = options.requestTimeoutMs || 10000;
      this.handshakeTimeoutMs = options.handshakeTimeoutMs || 10000;
      this.module = null;
      this.enabled = false;
      this.mode = 0;
      this.networkRequested = true;
      this.socket = null;
      this.ready = false;
      this.status = "disabled";
      this.statusDetail = "";
      this.statusListeners = new Set();
      this.pending = new Map();
      this.tcp = new Map();
      this.udp = new Map();
      this.nextRequest = 1;
      this.reconnectTimer = 0;
      this.handshakeTimer = 0;
      this.reconnectDelay = 500;
    }

    attachModule(module) { this.module = module; }

    onStatus(listener) {
      this.statusListeners.add(listener);
      listener(this.status, this.statusDetail);
      return () => this.statusListeners.delete(listener);
    }

    _setStatus(status, detail = "") {
      if (this.status === status && this.statusDetail === detail) return;
      this.status = status;
      this.statusDetail = detail;
      for (const listener of this.statusListeners) listener(status, detail);
    }

    setEndpoint(value) {
      let endpoint;
      try {
        endpoint = validEndpoint(value);
      } catch (error) {
        this.endpoint = "";
        this._disconnect(false);
        this._setStatus("error", error.message);
        return false;
      }
      if (endpoint === this.endpoint) return true;
      this.endpoint = endpoint;
      this._disconnect(false);
      if (this.enabled && this.networkRequested) this.connect();
      return true;
    }

    setDevice(enabled, mode) {
      const nextEnabled = Boolean(enabled);
      const nextMode = Number(mode) === 1 ? 1 : 0;
      const modeChanged = nextMode !== this.mode;
      this.enabled = nextEnabled;
      this.mode = nextMode;
      this.networkRequested = nextEnabled;
      if (!nextEnabled) {
        this._disconnect(false);
        this._setStatus("disabled");
      } else if (modeChanged) {
        this._disconnect(false);
        this.connect();
      } else {
        this.connect();
      }
    }

    isConnected() { return this.ready; }

    connect() {
      this.networkRequested = true;
      if (!this.enabled || this.ready ||
          (this.socket && this.socket.readyState === 0)) return;
      clearTimeout(this.reconnectTimer);
      if (!this.endpoint || !this.WebSocketCtor) {
        this._setStatus("offline", this.endpoint ? "WebSocket unavailable" : "No relay endpoint");
        return;
      }

      let socket;
      try {
        socket = new this.WebSocketCtor(this.endpoint);
      } catch (error) {
        this._setStatus("error", error.message);
        this._scheduleReconnect();
        return;
      }
      this.socket = socket;
      socket.binaryType = "arraybuffer";
      this._setStatus("connecting");
      clearTimeout(this.handshakeTimer);
      this.handshakeTimer = setTimeout(() => {
        if (this.socket === socket && !this.ready)
          socket.close(1002, "relay handshake timeout");
      }, this.handshakeTimeoutMs);

      socket.addEventListener("open", () => {
        if (this.socket !== socket) return;
        this._send(P.encode(P.Type.HELLO));
      });
      socket.addEventListener("message", event => {
        if (this.socket !== socket) return;
        try {
          this._handleFrame(P.decode(event.data));
        } catch (error) {
          this._setStatus("error", error.message);
          socket.close(1002, "invalid relay frame");
        }
      });
      socket.addEventListener("error", () => {
        if (this.socket === socket) this._setStatus("error", "Relay connection failed");
      });
      socket.addEventListener("close", () => {
        if (this.socket !== socket) return;
        clearTimeout(this.handshakeTimer);
        this.socket = null;
        this.ready = false;
        this._failPending(P.Status.CONNECT_FAILED);
        this._markChannels("error");
        if (this.enabled && this.networkRequested) {
          this._setStatus("offline", "Relay disconnected");
          this._scheduleReconnect();
        } else if (this.enabled) {
          this._setStatus("offline");
        }
      });
    }

    wifiDisconnect() {
      this.networkRequested = false;
      this._disconnect(true);
      if (this.enabled) this._setStatus("offline");
    }

    resetChannels() {
      for (const channel of this.tcp.keys())
        this._send(P.encode(P.Type.TCP_CLOSE, channel));
      for (const channel of this.udp.keys())
        this._send(P.encode(P.Type.UDP_CLOSE, channel));
      this._failPending(P.Status.IO_ERROR);
      this.tcp.clear();
      this.udp.clear();
    }

    _disconnect(sendClose) {
      clearTimeout(this.reconnectTimer);
      clearTimeout(this.handshakeTimer);
      const socket = this.socket;
      this.socket = null;
      this.ready = false;
      this._failPending(P.Status.IO_ERROR);
      this._markChannels("error");
      if (socket && sendClose && socket.readyState === 1)
        socket.close(1000, "network disconnected");
      else if (socket)
        socket.close();
    }

    _scheduleReconnect() {
      clearTimeout(this.reconnectTimer);
      if (!this.enabled || !this.networkRequested) return;
      const delay = this.reconnectDelay;
      this.reconnectDelay = Math.min(this.reconnectDelay * 2, 10000);
      this.reconnectTimer = setTimeout(() => this.connect(), delay);
    }

    _send(frame) {
      if (!this.socket || this.socket.readyState !== 1) return false;
      if (this.socket.bufferedAmount > this.maxBufferedAmount) return false;
      this.socket.send(frame);
      return true;
    }

    _request(type, channel, payload, pending) {
      if (!this.ready) return false;
      let request = this.nextRequest++;
      if (this.nextRequest > 0xffff) this.nextRequest = 1;
      while (this.pending.has(request)) {
        request = this.nextRequest++;
        if (this.nextRequest > 0xffff) this.nextRequest = 1;
      }
      this.pending.set(request, pending);
      if (!this._send(P.encode(type, channel, request, payload))) {
        this.pending.delete(request);
        return false;
      }
      pending.timer = setTimeout(() => {
        if (this.pending.get(request) !== pending) return;
        this.pending.delete(request);
        if (pending.kind === "tcp") {
          const channel = pending.slot + 1;
          this._send(P.encode(P.Type.TCP_CLOSE, channel));
          this.tcp.delete(channel);
        } else if (pending.kind === "udp") {
          const channel = pending.slot + 1;
          this._send(P.encode(P.Type.UDP_CLOSE, channel));
          this.udp.delete(channel);
        }
        this._notifyPending(pending, P.Status.CONNECT_FAILED);
      }, this.requestTimeoutMs);
      return true;
    }

    dns(host) {
      return this._request(P.Type.DNS, 0, P.encodeText(host), { kind: "dns" });
    }

    tcpOpen(slot, host, port, flags) {
      const channel = slot + 1;
      const payload = P.concat(new Uint8Array([flags & 0xff]), P.u16(port), P.encodeText(host));
      const state = { state: "opening", queue: new ByteQueue(this.maxQueueBytes) };
      this.tcp.set(channel, state);
      if (this._request(P.Type.TCP_OPEN, channel, payload, { kind: "tcp", slot }))
        return true;
      this.tcp.delete(channel);
      return false;
    }

    tcpSend(slot, value) {
      const channel = slot + 1;
      const state = this.tcp.get(channel);
      return Boolean(state && state.state === "open" &&
                     this._send(P.encode(P.Type.TCP_SEND, channel, 0, value)));
    }

    tcpClose(slot) {
      const channel = slot + 1;
      if (this.tcp.has(channel)) this._send(P.encode(P.Type.TCP_CLOSE, channel));
      this.tcp.delete(channel);
    }

    tcpRead(slot, heap, pointer, maximum) {
      const state = this.tcp.get(slot + 1);
      if (!state) return -2;
      if (state.queue.length) return state.queue.read(heap, pointer, maximum);
      if (state.state === "closed") return -1;
      if (state.state === "error") return -2;
      return 0;
    }

    udpOpen(slot, localPort) {
      const channel = slot + 1;
      const state = { state: "opening", datagrams: [], bytes: 0 };
      this.udp.set(channel, state);
      if (this._request(P.Type.UDP_OPEN, channel, P.u16(localPort), { kind: "udp", slot }))
        return true;
      this.udp.delete(channel);
      return false;
    }

    udpSend(slot, address, port, value) {
      const channel = slot + 1;
      const state = this.udp.get(channel);
      const payload = P.concat(P.bytes(address), P.u16(port), P.bytes(value));
      return Boolean(state && state.state === "open" &&
                     this._send(P.encode(P.Type.UDP_SEND, channel, 0, payload)));
    }

    udpClose(slot) {
      const channel = slot + 1;
      if (this.udp.has(channel)) this._send(P.encode(P.Type.UDP_CLOSE, channel));
      this.udp.delete(channel);
    }

    udpRead(slot, heap, addressPointer, portPointer, dataPointer, maximum) {
      const state = this.udp.get(slot + 1);
      if (!state) return -2;
      const datagram = state.datagrams.shift();
      if (datagram) {
        state.bytes -= datagram.data.length;
        heap.set(datagram.address, addressPointer);
        heap[portPointer] = datagram.port & 0xff;
        heap[portPointer + 1] = (datagram.port >>> 8) & 0xff;
        const count = Math.min(maximum, datagram.data.length);
        heap.set(datagram.data.subarray(0, count), dataPointer);
        return count;
      }
      if (state.state === "error" || state.state === "closed") return -2;
      return 0;
    }

    _handleFrame(frame) {
      switch (frame.type) {
        case P.Type.READY:
          clearTimeout(this.handshakeTimer);
          this.ready = true;
          this.reconnectDelay = 500;
          this._setStatus("online");
          return;
        case P.Type.DNS_RESULT:
          this._handleDnsResult(frame);
          return;
        case P.Type.TCP_OPEN_RESULT:
          this._handleTcpOpenResult(frame);
          return;
        case P.Type.TCP_DATA:
          this._handleTcpData(frame);
          return;
        case P.Type.TCP_CLOSED:
          this._handleTcpClosed(frame);
          return;
        case P.Type.UDP_OPEN_RESULT:
          this._handleUdpOpenResult(frame);
          return;
        case P.Type.UDP_DATA:
          this._handleUdpData(frame);
          return;
        case P.Type.UDP_CLOSED:
          this._handleUdpClosed(frame);
          return;
        default:
          throw new Error("unknown relay message " + frame.type);
      }
    }

    _handleDnsResult(frame) {
      const pending = this.pending.get(frame.request);
      if (!pending || pending.kind !== "dns" || frame.payload.length < 1) return;
      this.pending.delete(frame.request);
      clearTimeout(pending.timer);
      const status = frame.payload[0];
      const address = status === P.Status.OK && frame.payload.length >= 5
        ? frame.payload.subarray(1, 5) : new Uint8Array(4);
      if (this.module)
        this.module._poc_perryfi_dns_result(status, ...address);
    }

    _handleTcpOpenResult(frame) {
      const pending = this.pending.get(frame.request);
      if (!pending || pending.kind !== "tcp" || frame.payload.length < 1) return;
      this.pending.delete(frame.request);
      clearTimeout(pending.timer);
      const status = frame.payload[0];
      const address = status === P.Status.OK && frame.payload.length >= 7
        ? frame.payload.subarray(1, 5) : new Uint8Array(4);
      const port = status === P.Status.OK && frame.payload.length >= 7
        ? P.readU16(frame.payload, 5) : 0;
      const state = this.tcp.get(pending.slot + 1);
      if (state) state.state = status === P.Status.OK ? "open" : "error";
      if (this.module)
        this.module._poc_perryfi_tcp_open_result(pending.slot, status, ...address, port);
    }

    _handleTcpData(frame) {
      const state = this.tcp.get(frame.channel);
      if (!state || !state.queue.push(frame.payload)) {
        if (state) state.state = "error";
        this._send(P.encode(P.Type.TCP_CLOSE, frame.channel));
      }
    }

    _handleTcpClosed(frame) {
      const state = this.tcp.get(frame.channel);
      if (!state) return;
      const status = frame.payload[0] || P.Status.OK;
      state.state = status === P.Status.OK ? "closed" : "error";
    }

    _handleUdpOpenResult(frame) {
      const pending = this.pending.get(frame.request);
      if (!pending || pending.kind !== "udp" || frame.payload.length < 1) return;
      this.pending.delete(frame.request);
      clearTimeout(pending.timer);
      const status = frame.payload[0];
      const port = status === P.Status.OK && frame.payload.length >= 3
        ? P.readU16(frame.payload, 1) : 0;
      const state = this.udp.get(pending.slot + 1);
      if (state) state.state = status === P.Status.OK ? "open" : "error";
      if (this.module)
        this.module._poc_perryfi_udp_open_result(pending.slot, status, port);
    }

    _handleUdpData(frame) {
      const state = this.udp.get(frame.channel);
      if (!state || frame.payload.length < 6) return;
      const data = new Uint8Array(frame.payload.subarray(6));
      if (state.bytes + data.length > this.maxQueueBytes) {
        state.state = "error";
        this._send(P.encode(P.Type.UDP_CLOSE, frame.channel));
        return;
      }
      state.datagrams.push({
        address: new Uint8Array(frame.payload.subarray(0, 4)),
        port: P.readU16(frame.payload, 4),
        data,
      });
      state.bytes += data.length;
    }

    _handleUdpClosed(frame) {
      const state = this.udp.get(frame.channel);
      if (state) state.state = "error";
    }

    _notifyPending(pending, status) {
      if (!this.module) return;
      if (pending.kind === "dns")
        this.module._poc_perryfi_dns_result(status, 0, 0, 0, 0);
      else if (pending.kind === "tcp")
        this.module._poc_perryfi_tcp_open_result(pending.slot, status, 0, 0, 0, 0, 0);
      else if (pending.kind === "udp")
        this.module._poc_perryfi_udp_open_result(pending.slot, status, 0);
    }

    _failPending(status) {
      for (const pending of this.pending.values()) {
        clearTimeout(pending.timer);
        this._notifyPending(pending, status);
      }
      this.pending.clear();
    }

    _markChannels(state) {
      for (const channel of this.tcp.values()) channel.state = state;
      for (const channel of this.udp.values()) channel.state = state;
    }
  }

  return {
    ByteQueue,
    PerryfiBridge,
    createBridge: options => new PerryfiBridge(options),
    pageDefaultEndpoint,
    validEndpoint,
  };
}));
