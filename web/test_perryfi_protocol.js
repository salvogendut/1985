"use strict";

const assert = require("node:assert/strict");
const P = require("./perryfi-relay-protocol.js");

const payload = P.concat(new Uint8Array([7]), P.u16(2323), P.encodeText("host"));
const encoded = P.encode(P.Type.TCP_OPEN, 3, 0x1234, payload);
const decoded = P.decode(encoded);

assert.equal(decoded.type, P.Type.TCP_OPEN);
assert.equal(decoded.channel, 3);
assert.equal(decoded.request, 0x1234);
assert.deepEqual([...decoded.payload], [...payload]);
assert.equal(P.readU16(decoded.payload, 1), 2323);
assert.equal(P.decodeText(decoded.payload.subarray(3)), "host");
assert.throws(() => P.decode(encoded.subarray(0, encoded.length - 1)), /length/);

console.log("PerryFi relay protocol tests passed");
