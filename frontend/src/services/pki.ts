export function isWebCryptoAvailable(): boolean {
  return typeof crypto !== 'undefined' && !!crypto.subtle;
}

const DB_NAME = 'enclave-station-pki';
const STORE_NAME = 'keys';
const KEY_ID = 'default';

const PBKDF2_ITERATIONS = 600_000;
const SALT_BYTES = 16;
const IV_BYTES = 12;

function openDB(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open(DB_NAME, 1);
    req.onupgradeneeded = () => {
      req.result.createObjectStore(STORE_NAME, { keyPath: 'id' });
    };
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}

export function bufferToBase64url(buffer: ArrayBuffer): string {
  const bytes = new Uint8Array(buffer);
  let binary = '';
  for (const b of bytes) binary += String.fromCharCode(b);
  return btoa(binary)
    .replace(/\+/g, '-')
    .replace(/\//g, '_')
    .replace(/=+$/, '');
}

export function base64urlToBuffer(b64url: string): ArrayBuffer {
  const b64 = b64url.replace(/-/g, '+').replace(/_/g, '/');
  const padded = b64 + '='.repeat((4 - (b64.length % 4)) % 4);
  const binary = atob(padded);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
  return bytes.buffer;
}

// --- PIN-based key derivation and encryption ---

async function deriveKeyFromPin(
  pin: string,
  salt: Uint8Array,
): Promise<CryptoKey> {
  const pinBytes = new TextEncoder().encode(pin);
  const baseKey = await crypto.subtle.importKey(
    'raw',
    pinBytes,
    'PBKDF2',
    false,
    ['deriveKey'],
  );
  return crypto.subtle.deriveKey(
    {
      name: 'PBKDF2',
      salt: salt as BufferSource,
      iterations: PBKDF2_ITERATIONS,
      hash: 'SHA-256',
    },
    baseKey,
    { name: 'AES-GCM', length: 256 },
    false,
    ['encrypt', 'decrypt'],
  );
}

async function encryptPrivateKey(
  pkcs8: ArrayBuffer,
  pin: string,
): Promise<{ encrypted: ArrayBuffer; salt: Uint8Array; iv: Uint8Array }> {
  const salt = crypto.getRandomValues(new Uint8Array(SALT_BYTES));
  const iv = crypto.getRandomValues(new Uint8Array(IV_BYTES));
  const aesKey = await deriveKeyFromPin(pin, salt);
  const encrypted = await crypto.subtle.encrypt(
    { name: 'AES-GCM', iv: iv as BufferSource },
    aesKey,
    pkcs8,
  );
  return { encrypted, salt, iv };
}

async function decryptPrivateKey(
  encrypted: ArrayBuffer,
  salt: Uint8Array,
  iv: Uint8Array,
  pin: string,
): Promise<CryptoKey> {
  const aesKey = await deriveKeyFromPin(pin, salt);
  let pkcs8: ArrayBuffer;
  try {
    pkcs8 = await crypto.subtle.decrypt(
      { name: 'AES-GCM', iv: iv as BufferSource },
      aesKey,
      encrypted,
    );
  } catch {
    throw new Error('Incorrect PIN');
  }
  return crypto.subtle.importKey(
    'pkcs8',
    pkcs8,
    { name: 'ECDSA', namedCurve: 'P-256' },
    false,
    ['sign'],
  );
}

// --- Storage type ---

interface StoredKey {
  id: string;
  encryptedPrivateKey: ArrayBuffer;
  salt: Uint8Array;
  iv: Uint8Array;
  publicKeyB64: string;
}

// --- Public API ---

export async function generateKeyPair(pin: string): Promise<string> {
  const keyPair = await crypto.subtle.generateKey(
    { name: 'ECDSA', namedCurve: 'P-256' },
    true, // extractable so we can encrypt with PIN
    ['sign', 'verify'],
  );

  const spkiBytes = await crypto.subtle.exportKey('spki', keyPair.publicKey);
  const publicKeyB64 = bufferToBase64url(spkiBytes);

  const pkcs8 = await crypto.subtle.exportKey('pkcs8', keyPair.privateKey);
  const { encrypted, salt, iv } = await encryptPrivateKey(pkcs8, pin);

  const db = await openDB();
  await new Promise<void>((resolve, reject) => {
    const tx = db.transaction(STORE_NAME, 'readwrite');
    tx.objectStore(STORE_NAME).put({
      id: KEY_ID,
      encryptedPrivateKey: encrypted,
      salt,
      iv,
      publicKeyB64,
    } satisfies StoredKey);
    tx.oncomplete = () => resolve();
    tx.onerror = () => reject(tx.error);
  });
  db.close();

  return publicKeyB64;
}

export async function signChallenge(
  challenge: string,
  pin: string,
): Promise<string> {
  const entry = await loadStoredKey();
  const privateKey = await decryptPrivateKey(
    entry.encryptedPrivateKey,
    entry.salt,
    entry.iv,
    pin,
  );

  const data = new TextEncoder().encode(challenge);
  const signature = await crypto.subtle.sign(
    { name: 'ECDSA', hash: 'SHA-256' },
    privateKey,
    data,
  );
  return bufferToBase64url(signature);
}

export async function hasStoredKey(): Promise<boolean> {
  try {
    const db = await openDB();
    const result = await new Promise<boolean>((resolve) => {
      const tx = db.transaction(STORE_NAME, 'readonly');
      const req = tx.objectStore(STORE_NAME).get(KEY_ID);
      req.onsuccess = () => resolve(!!req.result);
      req.onerror = () => resolve(false);
    });
    db.close();
    return result;
  } catch {
    return false;
  }
}

export async function getStoredPublicKey(): Promise<string | null> {
  try {
    const db = await openDB();
    const result = await new Promise<string | null>((resolve) => {
      const tx = db.transaction(STORE_NAME, 'readonly');
      const req = tx.objectStore(STORE_NAME).get(KEY_ID);
      req.onsuccess = () => resolve(req.result?.publicKeyB64 ?? null);
      req.onerror = () => resolve(null);
    });
    db.close();
    return result;
  } catch {
    return null;
  }
}

export async function clearStoredKey(): Promise<void> {
  const db = await openDB();
  await new Promise<void>((resolve, reject) => {
    const tx = db.transaction(STORE_NAME, 'readwrite');
    tx.objectStore(STORE_NAME).delete(KEY_ID);
    tx.oncomplete = () => resolve();
    tx.onerror = () => reject(tx.error);
  });
  db.close();
}

/**
 * Change the PIN on a stored key. Requires the current PIN.
 */
export async function changePin(
  currentPin: string,
  newPin: string,
): Promise<void> {
  const entry = await loadStoredKey();

  // Decrypt with current PIN to get raw PKCS8
  const aesKey = await deriveKeyFromPin(currentPin, entry.salt);
  let pkcs8: ArrayBuffer;
  try {
    pkcs8 = await crypto.subtle.decrypt(
      { name: 'AES-GCM', iv: entry.iv as BufferSource },
      aesKey,
      entry.encryptedPrivateKey,
    );
  } catch {
    throw new Error('Incorrect PIN');
  }

  // Re-encrypt with new PIN
  const { encrypted, salt, iv } = await encryptPrivateKey(pkcs8, newPin);

  const db = await openDB();
  await new Promise<void>((resolve, reject) => {
    const tx = db.transaction(STORE_NAME, 'readwrite');
    tx.objectStore(STORE_NAME).put({
      id: KEY_ID,
      encryptedPrivateKey: encrypted,
      salt,
      iv,
      publicKeyB64: entry.publicKeyB64,
    } satisfies StoredKey);
    tx.oncomplete = () => resolve();
    tx.onerror = () => reject(tx.error);
  });
  db.close();
}

// --- Internal helpers ---

async function loadStoredKey(): Promise<StoredKey> {
  const db = await openDB();
  const result = await new Promise<StoredKey | null>((resolve) => {
    const tx = db.transaction(STORE_NAME, 'readonly');
    const req = tx.objectStore(STORE_NAME).get(KEY_ID);
    req.onsuccess = () => resolve(req.result ?? null);
    req.onerror = () => resolve(null);
  });
  db.close();

  if (!result) throw new Error('No stored key pair found');
  return result;
}
