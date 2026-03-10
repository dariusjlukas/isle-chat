/**
 * Minimal TOTP implementation using Node.js crypto.
 * Generates RFC 6238 TOTP codes from a base32-encoded secret.
 */

import { createHmac } from "crypto";

const BASE32_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

function base32Decode(encoded: string): Buffer {
  const stripped = encoded.replace(/=+$/, "").toUpperCase();
  const bits: number[] = [];
  for (const ch of stripped) {
    const val = BASE32_CHARS.indexOf(ch);
    if (val === -1) throw new Error(`Invalid base32 character: ${ch}`);
    for (let i = 4; i >= 0; i--) bits.push((val >> i) & 1);
  }
  const bytes: number[] = [];
  for (let i = 0; i + 8 <= bits.length; i += 8) {
    let byte = 0;
    for (let j = 0; j < 8; j++) byte = (byte << 1) | bits[i + j];
    bytes.push(byte);
  }
  return Buffer.from(bytes);
}

/**
 * Generate a TOTP code from a base32-encoded secret.
 * Uses HMAC-SHA1 with 30-second time steps and 6-digit codes (RFC 6238).
 */
export function generateTotpCode(base32Secret: string): string {
  const key = base32Decode(base32Secret);
  const timeStep = Math.floor(Date.now() / 1000 / 30);

  const timeBuffer = Buffer.alloc(8);
  timeBuffer.writeBigUInt64BE(BigInt(timeStep));

  const hmac = createHmac("sha1", key).update(timeBuffer).digest();
  const offset = hmac[hmac.length - 1] & 0x0f;
  const code =
    ((hmac[offset] & 0x7f) << 24) |
    ((hmac[offset + 1] & 0xff) << 16) |
    ((hmac[offset + 2] & 0xff) << 8) |
    (hmac[offset + 3] & 0xff);

  return String(code % 1_000_000).padStart(6, "0");
}
