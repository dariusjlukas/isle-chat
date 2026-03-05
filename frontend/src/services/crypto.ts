import { ed25519 } from '@noble/curves/ed25519.js';

const DB_NAME = 'chat-app-keys';
const STORE_NAME = 'keypairs';

// --- Encoding helpers ---

function bufferToBase64(buffer: Uint8Array): string {
  let binary = '';
  for (let i = 0; i < buffer.byteLength; i++) {
    binary += String.fromCharCode(buffer[i]);
  }
  return btoa(binary);
}

function base64ToBuffer(b64: string): Uint8Array {
  const binary = atob(b64);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) {
    bytes[i] = binary.charCodeAt(i);
  }
  return bytes;
}

function hexToBytes(hex: string): Uint8Array {
  const bytes = new Uint8Array(hex.length / 2);
  for (let i = 0; i < hex.length; i += 2) {
    bytes[i / 2] = parseInt(hex.substr(i, 2), 16);
  }
  return bytes;
}

// SPKI DER prefix for Ed25519: SEQUENCE { SEQUENCE { OID 1.3.101.112 }, BIT STRING }
const SPKI_PREFIX = hexToBytes('302a300506032b6570032100');

function rawPublicKeyToPem(rawPubKey: Uint8Array): string {
  const spkiDer = new Uint8Array(SPKI_PREFIX.length + rawPubKey.length);
  spkiDer.set(SPKI_PREFIX);
  spkiDer.set(rawPubKey, SPKI_PREFIX.length);
  const b64 = bufferToBase64(spkiDer);
  return `-----BEGIN PUBLIC KEY-----\n${b64.match(/.{1,64}/g)!.join('\n')}\n-----END PUBLIC KEY-----`;
}

// --- IndexedDB ---

function openDB(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open(DB_NAME, 1);
    request.onupgradeneeded = () => {
      if (!request.result.objectStoreNames.contains(STORE_NAME)) {
        request.result.createObjectStore(STORE_NAME, { keyPath: 'id' });
      }
    };
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error);
  });
}

// --- Public API ---

export async function generateKeyPair(
  username: string,
): Promise<{ publicKeyPem: string }> {
  const privateKey = ed25519.utils.randomSecretKey();
  const publicKey = ed25519.getPublicKey(privateKey);
  const publicKeyPem = rawPublicKeyToPem(publicKey);

  const db = await openDB();
  const tx = db.transaction(STORE_NAME, 'readwrite');
  tx.objectStore(STORE_NAME).put({
    id: username,
    privateKey: bufferToBase64(privateKey),
    publicKeyPem,
  });
  await new Promise<void>((resolve, reject) => {
    tx.oncomplete = () => resolve();
    tx.onerror = () => reject(tx.error);
  });
  db.close();
  return { publicKeyPem };
}

export async function getStoredKeys(
  username: string,
): Promise<{ privateKey: Uint8Array; publicKeyPem: string } | null> {
  const db = await openDB();
  const tx = db.transaction(STORE_NAME, 'readonly');
  const store = tx.objectStore(STORE_NAME);
  return new Promise((resolve, reject) => {
    const request = store.get(username);
    request.onsuccess = () => {
      db.close();
      if (!request.result) {
        resolve(null);
        return;
      }
      resolve({
        privateKey: base64ToBuffer(request.result.privateKey),
        publicKeyPem: request.result.publicKeyPem,
      });
    };
    request.onerror = () => {
      db.close();
      reject(request.error);
    };
  });
}

export async function signChallenge(
  privateKey: Uint8Array,
  challenge: string,
): Promise<string> {
  const data = new TextEncoder().encode(challenge);
  const signature = ed25519.sign(data, privateKey);
  return bufferToBase64(signature);
}

export async function listStoredUsers(): Promise<string[]> {
  const db = await openDB();
  const tx = db.transaction(STORE_NAME, 'readonly');
  const store = tx.objectStore(STORE_NAME);
  return new Promise((resolve, reject) => {
    const request = store.getAllKeys();
    request.onsuccess = () => {
      db.close();
      resolve(request.result.filter((k) => k !== 'primary') as string[]);
    };
    request.onerror = () => {
      db.close();
      reject(request.error);
    };
  });
}

export async function storeKeysAs(
  username: string,
  privateKey: Uint8Array,
  publicKeyPem: string,
): Promise<void> {
  const db = await openDB();
  const tx = db.transaction(STORE_NAME, 'readwrite');
  tx.objectStore(STORE_NAME).put({
    id: username,
    privateKey: bufferToBase64(privateKey),
    publicKeyPem,
  });
  await new Promise<void>((resolve, reject) => {
    tx.oncomplete = () => resolve();
    tx.onerror = () => reject(tx.error);
  });
  db.close();
}

export async function clearKeys(username?: string): Promise<void> {
  const db = await openDB();
  const tx = db.transaction(STORE_NAME, 'readwrite');
  const store = tx.objectStore(STORE_NAME);
  if (username) {
    store.delete(username);
  } else {
    store.clear();
  }
  await new Promise<void>((resolve, reject) => {
    tx.oncomplete = () => resolve();
    tx.onerror = () => reject(tx.error);
  });
  db.close();
}
