// Test script to verify Noise XX key derivation matches Baileys
// Usage: node test-noise-keys.js
const crypto = require('crypto');

// From Baileys Defaults
const NOISE_MODE = Buffer.from('Noise_XX_25519_AESGCM_SHA256\0\0\0\0');
const NOISE_WA_HEADER = Buffer.from([87, 65, 6, 3]);

// HKDF (matching Baileys' usage through whatsapp-rust-bridge)
function hkdf(ikm, length, { salt, info }) {
    return crypto.hkdfSync('sha256', ikm, salt, info || '', length);
}

// SHA256
function sha256(data) {
    return crypto.createHash('sha256').update(data).digest();
}

// Simulate Baileys makeNoiseHandler initialization
const ephemeralPair = crypto.generateKeyPairSync('x25519');
const publicKey = ephemeralPair.publicKey.export({ type: 'spki', format: 'der' }).slice(-32);

let data = NOISE_MODE;
let hash = data.byteLength === 32 ? data : sha256(data);
let salt = hash;
let encKey = hash;
let counter = 0;

function authenticate(data) {
    hash = sha256(Buffer.concat([hash, data]));
}

function mixIntoKey(data) {
    const key = hkdf(Buffer.from(data), 64, { salt, info: '' });
    salt = key.subarray(0, 32);
    encKey = key.subarray(32);
    counter = 0;
}

// Init
authenticate(NOISE_WA_HEADER);
authenticate(publicKey);

console.log('After init:');
console.log('  hash[0..4]:', hash.subarray(0, 4).toString('hex'));
console.log('  salt[0..4]:', salt.subarray(0, 4).toString('hex'));
console.log('  encKey[0..4]:', encKey.subarray(0, 4).toString('hex'));
console.log('  counter:', counter);
console.log('  pubkey[0..4]:', publicKey.subarray(0, 4).toString('hex'));

// Simulate receiving server hello fields
// (We'll use test data to trace the path)
const serverEph = Buffer.alloc(32, 0xAA);  // fake
authenticate(serverEph);
console.log('\nAfter auth(serverEph):');
console.log('  hash[0..4]:', hash.subarray(0, 4).toString('hex'));
console.log('  counter:', counter);

// DH
const sharedKey = crypto.diffieHellman({
    privateKey: ephemeralPair.privateKey,
    publicKey: crypto.createPublicKey({ key: ephemeralPair.publicKey.export({ type: 'spki', format: 'der' }), format: 'der', type: 'spki' })
});
// Can't DH with own key easily in this test, but the point is to show the key derivation path

// Just show mixIntoKey with fake data
const fakeDH = Buffer.alloc(32, 0xBB);
mixIntoKey(fakeDH);
console.log('\nAfter mixIntoKey(fakeDH):');
console.log('  salt[0..4]:', salt.subarray(0, 4).toString('hex'));
console.log('  encKey[0..4]:', encKey.subarray(0, 4).toString('hex'));
console.log('  counter:', counter);

// Verify HKDF output
const testHKDF = hkdf(Buffer.alloc(32, 0xBB), 64, { salt: Buffer.alloc(32, 0).fill(0xab), info: '' });
console.log('\nHKDF test (ikm=0xBB*32, salt=0xab*32, info=""):');
console.log('  out[0..4]:', testHKDF.subarray(0, 4).toString('hex'));
console.log('  out[32..36]:', testHKDF.subarray(32, 36).toString('hex'));
