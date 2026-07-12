// HKDF comparison test
const crypto = require('crypto');

// Test: HKDF-SHA256(ikm, 64, salt, info='')
const ikm = Buffer.alloc(32, 0xBB);
const salt = Buffer.alloc(32);
// Fill salt with a known pattern
for (let i = 0; i < 32; i++) salt[i] = i;

const result = crypto.hkdfSync('sha256', ikm, salt, '', 64);
console.log('Node.js HKDF-SHA256(ikm=0xBB*32, salt=0..31, info="", length=64):');
console.log('  out[0..4]:', result.subarray(0, 4).toString('hex'));
console.log('  out[32..36]:', result.subarray(32, 36).toString('hex'));
console.log('  full[0..32]:', result.subarray(0, 32).toString('hex'));
console.log('  full[32..64]:', result.subarray(32, 64).toString('hex'));
