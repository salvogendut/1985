'use strict';

const assert = require('assert');
const path = require('path');
const create1985 = require('./dist/1985.js');

(async () => {
  const module = await create1985({
    locateFile: file => path.join(__dirname, 'dist', file),
  });

  assert.strictEqual(module._poc_init(), 0);
  assert.strictEqual(module._poc_dksound_enabled(), 0);

  assert.strictEqual(module._poc_set_dksound(1), 1);
  assert.strictEqual(module._poc_dksound_enabled(), 1);
  module._poc_reset();
  assert.strictEqual(module._poc_dksound_enabled(), 1);

  assert.strictEqual(module._poc_init_model(1), 0);
  assert.strictEqual(module._poc_dksound_enabled(), 1);

  assert.strictEqual(module._poc_set_dksound(0), 0);
  assert.strictEqual(module._poc_dksound_enabled(), 0);

  console.log('WASM expansion tests passed');
})().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
