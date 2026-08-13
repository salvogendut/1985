'use strict';

const assert = require('assert');
const { parseStartupMedia, filenameFromUrl } = require('./media-url.js');

const base = 'https://example.test/1985/';

let media = parseStartupMedia(
  '?theme=sapporo-dark&disk=media%2Fboot.dsk&diskb=media%2Fdata.dsk&autorun=GB',
  base
);
assert.deepStrictEqual(media, {
  disk: 'https://example.test/1985/media/boot.dsk',
  diskB: 'https://example.test/1985/media/data.dsk',
  autorun: 'GB',
});

media = parseStartupMedia(
  '?disk=https%3A%2F%2Fcdn.example.test%2Fpcw%2Fsystem.dsk',
  base
);
assert.deepStrictEqual(media, {
  disk: 'https://cdn.example.test/pcw/system.dsk',
  diskB: null,
  autorun: null,
});

assert.deepStrictEqual(parseStartupMedia('?diskb=media%2Fdata.dsk', base), {
  disk: null,
  diskB: 'https://example.test/1985/media/data.dsk',
  autorun: null,
});

assert.strictEqual(
  filenameFromUrl('https://example.test/media/Bomb%20Jack.dsk', 'disk.dsk'),
  'Bomb Jack.dsk'
);
assert.deepStrictEqual(parseStartupMedia('?theme=pcw8256', base), {
  disk: null,
  diskB: null,
  autorun: null,
});

assert.throws(
  () => parseStartupMedia('?disk=file%3A%2F%2F%2Ftmp%2Fprivate.dsk', base),
  /HTTP or HTTPS/
);
assert.throws(
  () => parseStartupMedia('?diskb=file%3A%2F%2F%2Ftmp%2Fprivate.dsk', base),
  /HTTP or HTTPS/
);
assert.throws(
  () => parseStartupMedia('?diskb=data.dsk&autorun=GB', base),
  /requires a disk/
);
assert.throws(
  () => parseStartupMedia('?disk=game.dsk&autorun=bad%22name', base),
  /unsupported characters/
);

console.log('server media URL tests passed');
