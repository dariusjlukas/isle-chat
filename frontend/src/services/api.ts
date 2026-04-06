import type {
  User,
  Channel,
  Message,
  Space,
  SpaceInvite,
  SpaceFile,
  SpaceFilePath,
  SpaceFilePermission,
  SpaceFileVersion,
  ReadReceiptInfo,
  Notification,
  CalendarEvent,
  CalendarEventRsvp,
  CalendarPermission,
  TaskBoard,
  TaskColumn,
  TaskItem,
  TaskLabel,
  TaskChecklist,
  TaskChecklistItem,
  TaskDependency,
  TaskBoardPermission,
  WikiPage,
  WikiPageVersion,
  WikiPagePermission,
  WikiPermission,
  WikiSearchResult,
  AiConversation,
  AiMessage,
} from '../types';
import type {
  PublicKeyCredentialCreationOptionsJSON,
  PublicKeyCredentialRequestOptionsJSON,
  RegistrationResponseJSON,
  AuthenticationResponseJSON,
} from '@simplewebauthn/browser';
import { useChatStore } from '../stores/chatStore';

const API_BASE = '/api';

function getToken(): string | null {
  return localStorage.getItem('session_token');
}

async function request<T>(path: string, options: RequestInit = {}): Promise<T> {
  const token = getToken();
  const headers: Record<string, string> = {
    'Content-Type': 'application/json',
    ...((options.headers as Record<string, string>) || {}),
  };
  if (token) {
    headers['Authorization'] = `Bearer ${token}`;
  }

  const res = await fetch(`${API_BASE}${path}`, { ...options, headers });

  if (!res.ok) {
    const body = await res.json().catch(() => ({ error: res.statusText }));
    // If the server rejects our session (expired, banned, etc.), clear auth
    if (res.status === 401 && token) {
      const store = useChatStore.getState();
      if (store.isAuthenticated) {
        store.clearAuth(
          body.error === 'Unauthorized'
            ? 'Your session is no longer valid. Please sign in again.'
            : body.error ||
                'Your session is no longer valid. Please sign in again.',
        );
      }
    }
    throw new Error(body.error || res.statusText);
  }

  return res.json();
}

// Auth — WebAuthn
export function getRegistrationOptions(data: {
  username: string;
  display_name: string;
  token?: string;
}) {
  return request<PublicKeyCredentialCreationOptionsJSON>(
    '/auth/register/options',
    { method: 'POST', body: JSON.stringify(data) },
  );
}

export function verifyRegistration(credential: RegistrationResponseJSON) {
  return request<{ token: string; user: User }>('/auth/register/verify', {
    method: 'POST',
    body: JSON.stringify(credential),
  });
}

export function getLoginOptions() {
  return request<PublicKeyCredentialRequestOptionsJSON>('/auth/login/options', {
    method: 'POST',
    body: '{}',
  });
}

export type LoginResult =
  | {
      token: string;
      user: User;
      must_change_password?: boolean;
      mfa_required?: false;
      must_setup_totp?: false;
    }
  | {
      mfa_required: true;
      mfa_token: string;
    }
  | {
      must_setup_totp: true;
      mfa_token: string;
    };

export function verifyLogin(credential: AuthenticationResponseJSON) {
  return request<LoginResult>('/auth/login/verify', {
    method: 'POST',
    body: JSON.stringify(credential),
  });
}

export function requestAccessOptions(data: {
  username: string;
  display_name: string;
}) {
  return request<PublicKeyCredentialCreationOptionsJSON>(
    '/auth/request-access/options',
    { method: 'POST', body: JSON.stringify(data) },
  );
}

export function requestAccess(data: {
  username: string;
  display_name: string;
  auth_method: string;
  public_key?: string;
  challenge?: string;
  signature?: string;
  credential?: RegistrationResponseJSON;
  password?: string;
}) {
  return request<{ request_id: string; status: string }>(
    '/auth/request-access',
    { method: 'POST', body: JSON.stringify(data) },
  );
}

export function getRequestStatus(requestId: string) {
  return request<{ status: string; token?: string; user?: User }>(
    `/auth/request-status/${encodeURIComponent(requestId)}`,
  );
}

export function logout() {
  return request('/auth/logout', { method: 'POST' });
}

// Users
export function getMe() {
  return request<User>('/users/me');
}

export function listUsers() {
  return request<User[]>('/users');
}

// Channels
export function listChannels() {
  return request<Channel[]>('/channels');
}

export function createChannel(
  name: string,
  description?: string,
  memberIds?: string[],
  isPublic = true,
  defaultRole = 'write',
) {
  return request<{
    id: string;
    name: string;
    description: string;
    is_direct: boolean;
    created_at: string;
  }>('/channels', {
    method: 'POST',
    body: JSON.stringify({
      name,
      description,
      member_ids: memberIds,
      is_public: isPublic,
      default_role: defaultRole,
    }),
  });
}

export function getMessages(
  channelId: string,
  before?: string,
  limit?: number,
) {
  const params = new URLSearchParams();
  if (before) params.set('before', before);
  if (limit) params.set('limit', limit.toString());
  const qs = params.toString();
  return request<Message[]>(
    `/channels/${channelId}/messages${qs ? '?' + qs : ''}`,
  );
}

export function getReadReceipts(channelId: string) {
  return request<(ReadReceiptInfo & { user_id: string })[]>(
    `/channels/${channelId}/read-receipts`,
  );
}

export function createDM(userId: string) {
  return request<{
    id: string;
    name: string;
    is_direct: boolean;
    created_at: string;
  }>('/channels/dm', {
    method: 'POST',
    body: JSON.stringify({ user_id: userId }),
  });
}

export function joinChannel(channelId: string) {
  return request<{ ok: boolean }>(`/channels/${channelId}/join`, {
    method: 'POST',
  });
}

export function listPublicChannels(search?: string, spaceId?: string) {
  const params = new URLSearchParams();
  if (search) params.set('search', search);
  if (spaceId) params.set('space_id', spaceId);
  const qs = params.toString();
  return request<
    Array<{
      id: string;
      name: string;
      description: string;
      is_public: boolean;
      default_role: string;
      is_archived: boolean;
      created_at: string;
    }>
  >(`/channels/public${qs ? '?' + qs : ''}`);
}

export function inviteToChannel(
  channelId: string,
  userId: string,
  role?: string,
) {
  return request<{ ok: boolean }>(`/channels/${channelId}/members`, {
    method: 'POST',
    body: JSON.stringify({ user_id: userId, role }),
  });
}

export function kickFromChannel(channelId: string, userId: string) {
  return request<{ ok: boolean }>(`/channels/${channelId}/members/${userId}`, {
    method: 'DELETE',
  });
}

export function changeMemberRole(
  channelId: string,
  userId: string,
  role: string,
) {
  return request<{ ok: boolean }>(`/channels/${channelId}/members/${userId}`, {
    method: 'PUT',
    body: JSON.stringify({ role }),
  });
}

export function updateChannelSettings(
  channelId: string,
  data: {
    name?: string;
    description?: string;
    is_public?: boolean;
    default_role?: string;
    default_join?: boolean;
  },
) {
  return request<Channel>(`/channels/${channelId}`, {
    method: 'PUT',
    body: JSON.stringify(data),
  });
}

// Admin
export function createInvite(expiryHours = 24, maxUses = 1) {
  return request<{ token: string }>('/admin/invites', {
    method: 'POST',
    body: JSON.stringify({ expiry_hours: expiryHours, max_uses: maxUses }),
  });
}

export interface InviteUse {
  username: string;
  used_at: string;
}

export interface Invite {
  id: string;
  token: string;
  created_by: string;
  used: boolean;
  expires_at: string;
  created_at: string;
  max_uses: number;
  use_count: number;
  uses: InviteUse[];
}

export function listInvites() {
  return request<Invite[]>('/admin/invites');
}

export function revokeInvite(id: string) {
  return request<{ ok: boolean }>(`/admin/invites/${id}`, {
    method: 'DELETE',
  });
}

export function listJoinRequests() {
  return request<
    Array<{
      id: string;
      username: string;
      display_name: string;
      auth_method: string;
      status: string;
      created_at: string;
    }>
  >('/admin/requests');
}

export function approveRequest(requestId: string) {
  return request<{ ok: boolean }>(`/admin/requests/${requestId}/approve`, {
    method: 'POST',
  });
}

export function denyRequest(requestId: string) {
  return request<{ ok: boolean }>(`/admin/requests/${requestId}/deny`, {
    method: 'POST',
  });
}

// Admin: recovery tokens
export function createRecoveryToken(userId: string, expiryHours = 24) {
  return request<{ token: string }>('/admin/recovery-tokens', {
    method: 'POST',
    body: JSON.stringify({ user_id: userId, expiry_hours: expiryHours }),
  });
}

export function listRecoveryTokens() {
  return request<
    Array<{
      id: string;
      token: string;
      created_by: string;
      for_user: string;
      for_user_id: string;
      used: boolean;
      expires_at: string;
      created_at: string;
      used_at?: string;
    }>
  >('/admin/recovery-tokens');
}

export function revokeRecoveryToken(id: string) {
  return request<{ ok: boolean }>(`/admin/recovery-tokens/${id}`, {
    method: 'DELETE',
  });
}

// Auth: recovery token redemption
export function recoverAccount(token: string) {
  return request<{ token: string; user: User; must_setup_key: boolean }>(
    '/auth/recover-account',
    { method: 'POST', body: JSON.stringify({ token }) },
  );
}

// Device linking
export function createDeviceToken() {
  return request<{ token: string }>('/users/me/device-tokens', {
    method: 'POST',
  });
}

export function listDevices() {
  return request<
    Array<{ id: string; device_name: string; created_at: string }>
  >('/users/me/devices');
}

export function removeDevice(id: string) {
  return request<{ ok: boolean }>(`/users/me/devices/${id}`, {
    method: 'DELETE',
  });
}

export function addDevicePkiChallenge(deviceToken: string) {
  return request<{ challenge: string }>('/auth/add-device/pki/challenge', {
    method: 'POST',
    body: JSON.stringify({ device_token: deviceToken }),
  });
}

export function addDevicePki(data: {
  device_token: string;
  public_key: string;
  challenge: string;
  signature: string;
  device_name: string;
}) {
  return request<{ token: string; user: User }>('/auth/add-device/pki', {
    method: 'POST',
    body: JSON.stringify(data),
  });
}

export function addDevicePasskeyOptions(deviceToken: string) {
  return request<Record<string, unknown>>('/auth/add-device/passkey/options', {
    method: 'POST',
    body: JSON.stringify({ device_token: deviceToken }),
  });
}

export function addDevicePasskeyVerify(data: {
  id: string;
  response: Record<string, unknown>;
  device_name: string;
}) {
  return request<{ token: string; user: User }>(
    '/auth/add-device/passkey/verify',
    {
      method: 'POST',
      body: JSON.stringify(data),
    },
  );
}

// Profile
export function updateProfile(data: {
  display_name?: string;
  bio?: string;
  status?: string;
  profile_color?: string;
}) {
  return request<User>('/users/me', {
    method: 'PUT',
    body: JSON.stringify(data),
  });
}

export function uploadAvatar(file: File): Promise<User> {
  return new Promise((resolve, reject) => {
    const token = getToken();
    const params = new URLSearchParams({
      content_type: file.type || 'image/png',
    });

    const xhr = new XMLHttpRequest();
    xhr.open('POST', `${API_BASE}/users/me/avatar?${params}`);
    if (token) xhr.setRequestHeader('Authorization', `Bearer ${token}`);

    xhr.onload = () => {
      if (xhr.status >= 200 && xhr.status < 300) {
        resolve(JSON.parse(xhr.responseText));
      } else {
        try {
          const body = JSON.parse(xhr.responseText);
          reject(new Error(body.error || xhr.statusText));
        } catch {
          reject(new Error(xhr.statusText));
        }
      }
    };
    xhr.onerror = () => reject(new Error('Upload failed'));
    xhr.send(file);
  });
}

export function deleteAvatar() {
  return request<User>('/users/me/avatar', { method: 'DELETE' });
}

export function getAvatarUrl(avatarFileId: string): string {
  return `${API_BASE}/avatars/${avatarFileId}`;
}

export function deleteAccount() {
  return request<{ ok: boolean }>('/users/me', { method: 'DELETE' });
}

// --- Chunked upload helper ---
const CHUNK_SIZE = 5 * 1024 * 1024; // 5 MB
const MAX_RETRIES = 3;

async function sha256Hex(data: ArrayBuffer): Promise<string> {
  if (!crypto.subtle) return '';
  const hash = await crypto.subtle.digest('SHA-256', data);
  return Array.from(new Uint8Array(hash))
    .map((b) => b.toString(16).padStart(2, '0'))
    .join('');
}

function uploadChunk(
  url: string,
  token: string | null,
  chunk: Blob,
  onProgress?: (loaded: number) => void,
): Promise<void> {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open('POST', url);
    if (token) xhr.setRequestHeader('Authorization', `Bearer ${token}`);
    xhr.upload.onprogress = (e) => {
      if (e.lengthComputable && onProgress) onProgress(e.loaded);
    };
    xhr.onload = () => {
      if (xhr.status >= 200 && xhr.status < 300) resolve();
      else {
        try {
          const body = JSON.parse(xhr.responseText);
          reject(new Error(body.error || xhr.statusText));
        } catch {
          reject(new Error(xhr.statusText));
        }
      }
    };
    xhr.onerror = () => reject(new Error('Chunk upload failed'));
    xhr.send(chunk);
  });
}

async function chunkedUpload<T>(
  baseUrl: string,
  file: File,
  initBody: Record<string, unknown>,
  onProgress?: (percent: number) => void,
): Promise<T> {
  const token = getToken();
  const chunkCount = Math.max(1, Math.ceil(file.size / CHUNK_SIZE));

  // Init
  const initRes = await fetch(`${baseUrl}/init`, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      ...(token ? { Authorization: `Bearer ${token}` } : {}),
    },
    body: JSON.stringify({
      ...initBody,
      total_size: file.size,
      chunk_count: chunkCount,
      chunk_size: CHUNK_SIZE,
    }),
  });
  if (!initRes.ok) {
    const body = await initRes.json().catch(() => ({}));
    throw new Error(body.error || initRes.statusText);
  }
  const { upload_id } = await initRes.json();

  // Upload chunks with retry
  let completedBytes = 0;
  for (let i = 0; i < chunkCount; i++) {
    const start = i * CHUNK_SIZE;
    const end = Math.min(start + CHUNK_SIZE, file.size);
    const chunk = file.slice(start, end);
    const chunkBytes = end - start;
    const chunkHash = await sha256Hex(await chunk.arrayBuffer());

    let lastError: Error | undefined;
    for (let attempt = 0; attempt < MAX_RETRIES; attempt++) {
      try {
        await uploadChunk(
          `${baseUrl}/${upload_id}/chunk?index=${i}&hash=${chunkHash}`,
          token,
          chunk,
          (loaded) => {
            if (onProgress && file.size > 0) {
              onProgress(
                Math.round(((completedBytes + loaded) / file.size) * 100),
              );
            }
          },
        );
        lastError = undefined;
        break;
      } catch (err) {
        lastError = err instanceof Error ? err : new Error(String(err));
        if (attempt < MAX_RETRIES - 1) {
          await new Promise((r) => setTimeout(r, 1000 * (attempt + 1)));
        }
      }
    }
    if (lastError) throw lastError;
    completedBytes += chunkBytes;
  }

  // Complete (send empty body so uWebSockets triggers onData)
  const completeRes = await fetch(`${baseUrl}/${upload_id}/complete`, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      ...(token ? { Authorization: `Bearer ${token}` } : {}),
    },
    body: '{}',
  });
  if (!completeRes.ok) {
    const body = await completeRes.json().catch(() => ({}));
    throw new Error(body.error || completeRes.statusText);
  }
  return completeRes.json();
}

// Files
export function uploadFile(
  channelId: string,
  file: File,
  messageText: string,
  onProgress?: (percent: number) => void,
): Promise<Record<string, unknown>> {
  return chunkedUpload(
    `${API_BASE}/channels/${channelId}/upload`,
    file,
    {
      filename: file.name,
      content_type: file.type || 'application/octet-stream',
      message: messageText,
    },
    onProgress,
  );
}

export function getFileUrl(fileId: string): string {
  const token = getToken();
  return `${API_BASE}/files/${fileId}?token=${token}`;
}

export async function downloadFile(
  fileId: string,
  fileName: string,
  onProgress?: (percent: number) => void,
) {
  const token = getToken();
  const res = await fetch(`${API_BASE}/files/${fileId}`, {
    headers: token ? { Authorization: `Bearer ${token}` } : {},
  });

  if (!res.ok) throw new Error('Download failed');

  const contentLength = res.headers.get('Content-Length');
  const total = contentLength ? parseInt(contentLength, 10) : 0;

  if (!res.body || !total) {
    const blob = await res.blob();
    triggerDownload(blob, fileName);
    return;
  }

  const reader = res.body.getReader();
  const chunks: ArrayBuffer[] = [];
  let received = 0;

  while (true) {
    const { done, value } = await reader.read();
    if (done) break;
    chunks.push(
      value.buffer.slice(value.byteOffset, value.byteOffset + value.byteLength),
    );
    received += value.length;
    if (onProgress) {
      onProgress(Math.round((received / total) * 100));
    }
  }

  const blob = new Blob(chunks);
  triggerDownload(blob, fileName);
}

function triggerDownload(blob: Blob, fileName: string) {
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = fileName;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);
}

// Admin settings
export interface AdminSettings {
  max_file_size: number;
  max_storage_size: number;
  storage_used: number;
  auth_methods: string[];
  server_name: string;
  server_icon_file_id: string;
  server_icon_dark_file_id: string;
  registration_mode: string;
  file_uploads_enabled: boolean;
  session_expiry_hours: number;
  setup_completed: boolean;
  server_archived: boolean;
  server_locked_down: boolean;
  password_min_length: number;
  password_require_uppercase: boolean;
  password_require_lowercase: boolean;
  password_require_number: boolean;
  password_require_special: boolean;
  password_max_age_days: number;
  password_history_count: number;
  mfa_required_password: boolean;
  mfa_required_pki: boolean;
  mfa_required_passkey: boolean;
  default_space_storage_limit: number;
  personal_spaces_enabled: boolean;
  personal_spaces_files_enabled: boolean;
  personal_spaces_calendar_enabled: boolean;
  personal_spaces_tasks_enabled: boolean;
  personal_spaces_wiki_enabled: boolean;
  personal_spaces_minigames_enabled: boolean;
  personal_spaces_sandbox_enabled: boolean;
  personal_spaces_storage_limit: number;
  personal_spaces_total_storage_limit: number;
  llm_enabled: boolean;
  llm_api_url: string;
  llm_model: string;
  llm_api_key: string;
  llm_max_tokens: number;
  llm_system_prompt: string;
}

export function getAdminSettings() {
  return request<AdminSettings>('/admin/settings');
}

export function updateAdminSettings(
  data: Partial<Omit<AdminSettings, 'storage_used' | 'setup_completed'>>,
) {
  return request<{ ok: boolean }>('/admin/settings', {
    method: 'PUT',
    body: JSON.stringify(data),
  });
}

export function completeSetup(
  data: Partial<Omit<AdminSettings, 'storage_used' | 'setup_completed'>>,
) {
  return request<{ ok: boolean }>('/admin/setup', {
    method: 'POST',
    body: JSON.stringify(data),
  });
}

export function uploadServerIcon(
  file: File,
): Promise<{ server_icon_file_id: string }> {
  return new Promise((resolve, reject) => {
    const token = getToken();
    const xhr = new XMLHttpRequest();
    xhr.open('POST', `${API_BASE}/admin/server-icon`);
    if (token) xhr.setRequestHeader('Authorization', `Bearer ${token}`);
    xhr.onload = () => {
      if (xhr.status >= 200 && xhr.status < 300) {
        resolve(JSON.parse(xhr.responseText));
      } else {
        try {
          const body = JSON.parse(xhr.responseText);
          reject(new Error(body.error || xhr.statusText));
        } catch {
          reject(new Error(xhr.statusText));
        }
      }
    };
    xhr.onerror = () => reject(new Error('Upload failed'));
    xhr.send(file);
  });
}

export function deleteServerIcon() {
  return request<{ ok: boolean }>('/admin/server-icon', { method: 'DELETE' });
}

export function uploadServerIconDark(
  file: File,
): Promise<{ server_icon_dark_file_id: string }> {
  return new Promise((resolve, reject) => {
    const token = getToken();
    const xhr = new XMLHttpRequest();
    xhr.open('POST', `${API_BASE}/admin/server-icon-dark`);
    if (token) xhr.setRequestHeader('Authorization', `Bearer ${token}`);
    xhr.onload = () => {
      if (xhr.status >= 200 && xhr.status < 300) {
        resolve(JSON.parse(xhr.responseText));
      } else {
        try {
          const body = JSON.parse(xhr.responseText);
          reject(new Error(body.error || xhr.statusText));
        } catch {
          reject(new Error(xhr.statusText));
        }
      }
    };
    xhr.onerror = () => reject(new Error('Upload failed'));
    xhr.send(file);
  });
}

export function deleteServerIconDark() {
  return request<{ ok: boolean }>('/admin/server-icon-dark', {
    method: 'DELETE',
  });
}

// Config
export interface PasswordPolicy {
  min_length: number;
  require_uppercase: boolean;
  require_lowercase: boolean;
  require_number: boolean;
  require_special: boolean;
}

export interface PublicConfig {
  public_url: string;
  auth_methods: string[];
  server_name: string;
  server_icon_file_id: string;
  server_icon_dark_file_id: string;
  registration_mode: string;
  setup_completed: boolean;
  has_users: boolean;
  file_uploads_enabled: boolean;
  server_archived: boolean;
  server_locked_down: boolean;
  password_policy?: PasswordPolicy;
  mfa_required_password?: boolean;
  mfa_required_pki?: boolean;
  mfa_required_passkey?: boolean;
  llm_enabled?: boolean;
}

export function getPublicConfig() {
  return request<PublicConfig>('/config');
}

// Spaces
export function listSpaces() {
  return request<Space[]>('/spaces');
}

export function createSpace(
  name: string,
  description?: string,
  isPublic = true,
  defaultRole = 'user',
) {
  return request<Space>('/spaces', {
    method: 'POST',
    body: JSON.stringify({
      name,
      description,
      is_public: isPublic,
      default_role: defaultRole,
    }),
  });
}

export function getSpace(spaceId: string) {
  return request<Space>(`/spaces/${spaceId}`);
}

export function updateSpaceSettings(
  spaceId: string,
  data: {
    name?: string;
    description?: string;
    is_public?: boolean;
    default_role?: string;
    profile_color?: string;
  },
) {
  return request<Partial<Space>>(`/spaces/${spaceId}`, {
    method: 'PUT',
    body: JSON.stringify(data),
  });
}

export function uploadSpaceAvatar(
  spaceId: string,
  file: File,
): Promise<Partial<Space>> {
  return new Promise((resolve, reject) => {
    const token = getToken();
    const params = new URLSearchParams({
      content_type: file.type || 'image/png',
    });

    const xhr = new XMLHttpRequest();
    xhr.open('POST', `${API_BASE}/spaces/${spaceId}/avatar?${params}`);
    if (token) xhr.setRequestHeader('Authorization', `Bearer ${token}`);

    xhr.onload = () => {
      if (xhr.status >= 200 && xhr.status < 300) {
        resolve(JSON.parse(xhr.responseText));
      } else {
        try {
          const body = JSON.parse(xhr.responseText);
          reject(new Error(body.error || xhr.statusText));
        } catch {
          reject(new Error(xhr.statusText));
        }
      }
    };
    xhr.onerror = () => reject(new Error('Upload failed'));
    xhr.send(file);
  });
}

export function deleteSpaceAvatar(spaceId: string) {
  return request<Partial<Space>>(`/spaces/${spaceId}/avatar`, {
    method: 'DELETE',
  });
}

export function getSpaceTools(spaceId: string) {
  return request<string[]>(`/spaces/${spaceId}/tools`);
}

export function setSpaceTool(spaceId: string, tool: string, enabled: boolean) {
  return request<{ ok: boolean; enabled_tools: string[] }>(
    `/spaces/${spaceId}/tools`,
    {
      method: 'PUT',
      body: JSON.stringify({ tool, enabled }),
    },
  );
}

export function joinSpace(spaceId: string) {
  return request<{ ok: boolean }>(`/spaces/${spaceId}/join`, {
    method: 'POST',
  });
}

export function listPublicSpaces(search?: string) {
  const params = new URLSearchParams();
  if (search) params.set('search', search);
  const qs = params.toString();
  return request<Space[]>(`/spaces/public${qs ? '?' + qs : ''}`);
}

export function inviteToSpace(spaceId: string, userId: string, role?: string) {
  return request<{ ok: boolean }>(`/spaces/${spaceId}/members`, {
    method: 'POST',
    body: JSON.stringify({ user_id: userId, role }),
  });
}

export function listSpaceInvites() {
  return request<SpaceInvite[]>('/space-invites');
}

export function acceptSpaceInvite(inviteId: string) {
  return request<{ ok: boolean }>(`/space-invites/${inviteId}/accept`, {
    method: 'POST',
  });
}

export function declineSpaceInvite(inviteId: string) {
  return request<{ ok: boolean }>(`/space-invites/${inviteId}/decline`, {
    method: 'POST',
  });
}

export function kickFromSpace(spaceId: string, userId: string) {
  return request<{ ok: boolean }>(`/spaces/${spaceId}/members/${userId}`, {
    method: 'DELETE',
  });
}

export function changeSpaceMemberRole(
  spaceId: string,
  userId: string,
  role: string,
) {
  return request<{ ok: boolean }>(`/spaces/${spaceId}/members/${userId}`, {
    method: 'PUT',
    body: JSON.stringify({ role }),
  });
}

// Leave / Archive
export function leaveChannel(channelId: string) {
  return request<{ ok: boolean }>(`/channels/${channelId}/leave`, {
    method: 'POST',
  });
}

export function archiveChannel(channelId: string) {
  return request<{ ok: boolean }>(`/channels/${channelId}/archive`, {
    method: 'POST',
  });
}

export function unarchiveChannel(channelId: string) {
  return request<{ ok: boolean }>(`/channels/${channelId}/unarchive`, {
    method: 'POST',
  });
}

export function leaveSpace(spaceId: string) {
  return request<{ ok: boolean }>(`/spaces/${spaceId}/leave`, {
    method: 'POST',
  });
}

export function archiveSpace(spaceId: string) {
  return request<{ ok: boolean }>(`/spaces/${spaceId}/archive`, {
    method: 'POST',
  });
}

export function unarchiveSpace(spaceId: string) {
  return request<{ ok: boolean }>(`/spaces/${spaceId}/unarchive`, {
    method: 'POST',
  });
}

export function archiveServer() {
  return request<{ ok: boolean }>('/admin/archive-server', { method: 'POST' });
}

export function unarchiveServer() {
  return request<{ ok: boolean }>('/admin/unarchive-server', {
    method: 'POST',
  });
}

export function lockdownServer() {
  return request<{ ok: boolean }>('/admin/lockdown-server', {
    method: 'POST',
  });
}

export function unlockServer() {
  return request<{ ok: boolean }>('/admin/unlock-server', {
    method: 'POST',
  });
}

export function listAdminUsers() {
  return request<
    Array<{
      id: string;
      username: string;
      display_name: string;
      role: string;
      is_online: boolean;
      last_seen: string;
      is_banned: boolean;
    }>
  >('/admin/users');
}

export function changeUserRole(userId: string, role: string) {
  return request<{ ok: boolean }>(`/admin/users/${userId}/role`, {
    method: 'PUT',
    body: JSON.stringify({ role }),
  });
}

export function banUser(userId: string) {
  return request<{ ok: boolean }>(`/admin/users/${userId}/ban`, {
    method: 'POST',
    body: JSON.stringify({}),
  });
}

export function unbanUser(userId: string) {
  return request<{ ok: boolean }>(`/admin/users/${userId}/ban`, {
    method: 'DELETE',
  });
}

export function listSpaceChannels(spaceId: string) {
  return request<Channel[]>(`/spaces/${spaceId}/channels`);
}

export function createSpaceChannel(
  spaceId: string,
  name: string,
  description?: string,
  memberIds?: string[],
  isPublic = true,
  defaultRole = 'write',
  defaultJoin = false,
) {
  return request<Channel>(`/spaces/${spaceId}/channels`, {
    method: 'POST',
    body: JSON.stringify({
      name,
      description,
      member_ids: memberIds,
      is_public: isPublic,
      default_role: defaultRole,
      default_join: defaultJoin,
    }),
  });
}

// Conversations (group messages)
export function listConversations() {
  return request<Channel[]>('/conversations');
}

export function createConversation(memberIds: string[], name?: string) {
  return request<Channel>('/conversations', {
    method: 'POST',
    body: JSON.stringify({ member_ids: memberIds, name }),
  });
}

export function addConversationMember(channelId: string, userId: string) {
  return request<{ ok: boolean }>(`/conversations/${channelId}/members`, {
    method: 'POST',
    body: JSON.stringify({ user_id: userId }),
  });
}

export function renameConversation(channelId: string, name: string) {
  return request<{ ok: boolean }>(`/conversations/${channelId}`, {
    method: 'PUT',
    body: JSON.stringify({ name }),
  });
}

// Password auth
export function passwordRegister(data: {
  username: string;
  display_name: string;
  password: string;
  token?: string;
}) {
  return request<{ token: string; user: User }>('/auth/password/register', {
    method: 'POST',
    body: JSON.stringify(data),
  });
}

export function passwordLogin(data: { username: string; password: string }) {
  return request<LoginResult>('/auth/password/login', {
    method: 'POST',
    body: JSON.stringify(data),
  });
}

export function changePassword(data: {
  current_password: string;
  new_password: string;
}) {
  return request<{ ok: boolean }>('/auth/password/change', {
    method: 'POST',
    body: JSON.stringify(data),
  });
}

export function setPassword(password: string) {
  return request<{ ok: boolean }>('/auth/password/set', {
    method: 'POST',
    body: JSON.stringify({ password }),
  });
}

export function deletePassword() {
  return request<{ ok: boolean }>('/auth/password', {
    method: 'DELETE',
  });
}

// TOTP / MFA
export function setupTotp() {
  return request<{ secret: string; uri: string }>('/users/me/totp/setup', {
    method: 'POST',
  });
}

export function verifyTotpSetup(code: string) {
  return request<{ ok: boolean }>('/users/me/totp/verify', {
    method: 'POST',
    body: JSON.stringify({ code }),
  });
}

export function removeTotp(code: string) {
  return request<{ ok: boolean }>('/users/me/totp', {
    method: 'DELETE',
    body: JSON.stringify({ code }),
  });
}

export function getTotpStatus() {
  return request<{ enabled: boolean }>('/users/me/totp/status');
}

export function verifyMfa(data: { mfa_token: string; totp_code: string }) {
  return request<{ token: string; user: User }>('/auth/mfa/verify', {
    method: 'POST',
    body: JSON.stringify(data),
  });
}

export function mfaSetup(mfa_token: string) {
  return request<{ secret: string; uri: string }>('/auth/mfa/setup', {
    method: 'POST',
    body: JSON.stringify({ mfa_token }),
  });
}

export function mfaSetupVerify(data: { mfa_token: string; code: string }) {
  return request<{ token: string; user: User }>('/auth/mfa/setup/verify', {
    method: 'POST',
    body: JSON.stringify(data),
  });
}

// PKI auth
export function getPkiChallenge(publicKey?: string) {
  return request<{ challenge: string }>('/auth/pki/challenge', {
    method: 'POST',
    body: JSON.stringify({ public_key: publicKey }),
  });
}

export function pkiRegister(data: {
  username: string;
  display_name: string;
  token?: string;
  public_key: string;
  challenge: string;
  signature: string;
}) {
  return request<{ token: string; user: User; recovery_keys: string[] }>(
    '/auth/pki/register',
    { method: 'POST', body: JSON.stringify(data) },
  );
}

export function pkiLogin(data: {
  public_key: string;
  challenge: string;
  signature: string;
}) {
  return request<LoginResult>('/auth/pki/login', {
    method: 'POST',
    body: JSON.stringify(data),
  });
}

export function recoveryLogin(recoveryKey: string) {
  return request<{ token: string; user: User; must_setup_key: boolean }>(
    '/auth/recovery',
    { method: 'POST', body: JSON.stringify({ recovery_key: recoveryKey }) },
  );
}

// PKI key management
export function getPkiKeyChallenge() {
  return request<{ challenge: string }>('/users/me/keys/challenge', {
    method: 'POST',
  });
}

export function addPkiKey(data: {
  public_key: string;
  challenge: string;
  signature: string;
  device_name?: string;
}) {
  return request<{ ok: boolean; recovery_keys?: string[] }>('/users/me/keys', {
    method: 'POST',
    body: JSON.stringify(data),
  });
}

export function listPkiKeys() {
  return request<
    Array<{ id: string; device_name: string; created_at: string }>
  >('/users/me/keys');
}

export function removePkiKey(id: string) {
  return request<{ ok: boolean }>(`/users/me/keys/${encodeURIComponent(id)}`, {
    method: 'DELETE',
  });
}

// Recovery key management
export function getRecoveryKeyCount() {
  return request<{ remaining: number }>('/users/me/recovery-keys/count');
}

export function regenerateRecoveryKeys() {
  return request<{ recovery_keys: string[] }>(
    '/users/me/recovery-keys/regenerate',
    { method: 'POST' },
  );
}

// Passkeys
export function getPasskeyRegistrationOptions() {
  return request<
    import('@simplewebauthn/browser').PublicKeyCredentialCreationOptionsJSON
  >('/users/me/passkeys/options', { method: 'POST' });
}

export function verifyPasskeyRegistration(
  credential: RegistrationResponseJSON,
  deviceName?: string,
) {
  return request<{ ok: boolean }>('/users/me/passkeys/verify', {
    method: 'POST',
    body: JSON.stringify({ ...credential, device_name: deviceName }),
  });
}

export function listPasskeys() {
  return request<
    Array<{ id: string; device_name: string; created_at: string }>
  >('/users/me/passkeys');
}

export function removePasskey(credentialId: string) {
  return request<{ ok: boolean }>(
    `/users/me/passkeys/${encodeURIComponent(credentialId)}`,
    { method: 'DELETE' },
  );
}

// Search
export interface MessageSearchResult {
  id: string;
  channel_id: string;
  channel_name: string;
  space_name: string;
  user_id: string;
  username: string;
  content: string;
  created_at: string;
  is_direct: boolean;
}

export interface FileSearchResult {
  message_id: string;
  channel_id: string;
  channel_name: string;
  user_id: string;
  username: string;
  file_id: string;
  file_name: string;
  file_type: string;
  file_size: number;
  created_at: string;
  source: 'message' | 'space';
  space_id: string;
  space_name: string;
  is_folder: boolean;
}

export interface SearchResponse<T> {
  type: string;
  results: T[];
}

export interface SpaceSearchResult {
  id: string;
  name: string;
  description: string;
  is_public: boolean;
  avatar_file_id: string;
  profile_color: string;
}

export interface ChannelSearchResult {
  id: string;
  name: string;
  description: string;
  space_name: string;
  space_id: string;
  is_public: boolean;
}

export { type WikiSearchResult } from '../types';

export interface SearchFilter {
  type: string;
  value: string;
}

export function searchUsers(query: string, limit = 20, offset = 0) {
  const params = new URLSearchParams({
    q: query,
    type: 'users',
    limit: String(limit),
    offset: String(offset),
  });
  return request<SearchResponse<User>>(`/search?${params}`);
}

export function searchMessages(
  query: string,
  mode = 'and',
  limit = 20,
  offset = 0,
) {
  const params = new URLSearchParams({
    q: query,
    type: 'messages',
    mode,
    limit: String(limit),
    offset: String(offset),
  });
  return request<SearchResponse<MessageSearchResult>>(`/search?${params}`);
}

export function searchFiles(query: string, limit = 20, offset = 0) {
  const params = new URLSearchParams({
    q: query,
    type: 'files',
    limit: String(limit),
    offset: String(offset),
  });
  return request<SearchResponse<FileSearchResult>>(`/search?${params}`);
}

export function searchSpaces(query: string, limit = 20, offset = 0) {
  const params = new URLSearchParams({
    q: query,
    type: 'spaces',
    limit: String(limit),
    offset: String(offset),
  });
  return request<SearchResponse<SpaceSearchResult>>(`/search?${params}`);
}

export function searchChannels(query: string, limit = 20, offset = 0) {
  const params = new URLSearchParams({
    q: query,
    type: 'channels',
    limit: String(limit),
    offset: String(offset),
  });
  return request<SearchResponse<ChannelSearchResult>>(`/search?${params}`);
}

export function searchWiki(
  query: string,
  mode = 'and',
  limit = 20,
  offset = 0,
) {
  const params = new URLSearchParams({
    q: query,
    type: 'wiki',
    mode,
    limit: String(limit),
    offset: String(offset),
  });
  return request<SearchResponse<WikiSearchResult>>(`/search?${params}`);
}

export function searchComposite<T = MessageSearchResult>(
  filters: SearchFilter[],
  resultType: string,
  mode = 'and',
  limit = 20,
  offset = 0,
) {
  const filtersStr = filters.map((f) => `${f.type}:${f.value}`).join(',');
  const params = new URLSearchParams({
    filters: filtersStr,
    result_type: resultType,
    mode,
    limit: String(limit),
    offset: String(offset),
  });
  return request<SearchResponse<T>>(`/search/composite?${params}`);
}

// Notifications
export function getNotifications(limit = 50, offset = 0) {
  const params = new URLSearchParams({
    limit: String(limit),
    offset: String(offset),
  });
  return request<{ notifications: Notification[]; unread_count: number }>(
    `/notifications?${params}`,
  );
}

export function markNotificationRead(notificationId: string) {
  return request<{ ok: boolean }>(`/notifications/${notificationId}/read`, {
    method: 'POST',
  });
}

export function markAllNotificationsRead() {
  return request<{ ok: boolean }>('/notifications/read-all', {
    method: 'POST',
  });
}

export function markChannelNotificationsRead(channelId: string) {
  return request<{ ok: boolean; marked_count: number }>(
    `/notifications/read-by-channel/${channelId}`,
    { method: 'POST' },
  );
}

export function getMessagesAround(
  channelId: string,
  messageId: string,
  limit = 50,
) {
  const params = new URLSearchParams({
    message_id: messageId,
    limit: String(limit),
  });
  return request<Message[]>(`/channels/${channelId}/messages/around?${params}`);
}

// Space Files
export function listSpaceFiles(spaceId: string, parentId?: string) {
  const params = new URLSearchParams();
  if (parentId) params.set('parent_id', parentId);
  const qs = params.toString();
  return request<{ files: SpaceFile[]; path: SpaceFilePath[] }>(
    `/spaces/${spaceId}/files${qs ? '?' + qs : ''}`,
  );
}

export function createSpaceFolder(
  spaceId: string,
  name: string,
  parentId?: string,
) {
  return request<SpaceFile>(`/spaces/${spaceId}/files/folder`, {
    method: 'POST',
    body: JSON.stringify({ name, parent_id: parentId || '' }),
  });
}

export function uploadSpaceFile(
  spaceId: string,
  file: File,
  parentId?: string,
  onProgress?: (percent: number) => void,
): Promise<SpaceFile> {
  return chunkedUpload(
    `${API_BASE}/spaces/${spaceId}/files/upload`,
    file,
    {
      filename: file.name,
      content_type: file.type || 'application/octet-stream',
      parent_id: parentId || '',
    },
    onProgress,
  );
}

export function getSpaceFile(spaceId: string, fileId: string) {
  return request<SpaceFile & { path: SpaceFilePath[] }>(
    `/spaces/${spaceId}/files/${fileId}`,
  );
}

export function getSpaceFileDownloadUrl(
  spaceId: string,
  fileId: string,
  inline?: boolean,
): string {
  const token = getToken();
  const base = `${API_BASE}/spaces/${spaceId}/files/${fileId}/download?token=${token}`;
  return inline ? `${base}&inline=1` : base;
}

export async function downloadSpaceFile(
  spaceId: string,
  fileId: string,
  fileName: string,
) {
  const token = getToken();
  const res = await fetch(
    `${API_BASE}/spaces/${spaceId}/files/${fileId}/download`,
    {
      headers: token ? { Authorization: `Bearer ${token}` } : {},
    },
  );
  if (!res.ok) throw new Error('Download failed');
  const blob = await res.blob();
  triggerDownload(blob, fileName);
}

export async function downloadSpaceFolderAsZip(
  spaceId: string,
  folderId: string,
  folderName: string,
) {
  const token = getToken();
  const res = await fetch(
    `${API_BASE}/spaces/${spaceId}/files/${folderId}/download-zip`,
    {
      headers: token ? { Authorization: `Bearer ${token}` } : {},
    },
  );
  if (!res.ok) throw new Error('Download failed');
  const blob = await res.blob();
  triggerDownload(blob, folderName + '.zip');
}

export function updateSpaceFile(
  spaceId: string,
  fileId: string,
  data: { name?: string; parent_id?: string | null },
) {
  return request<SpaceFile>(`/spaces/${spaceId}/files/${fileId}`, {
    method: 'PUT',
    body: JSON.stringify(data),
  });
}

export function deleteSpaceFile(spaceId: string, fileId: string) {
  return request<{ ok: boolean }>(`/spaces/${spaceId}/files/${fileId}`, {
    method: 'DELETE',
  });
}

export interface StorageBreakdownEntry {
  name: string;
  type: 'tool' | 'channel';
  used: number;
}

export function getSpaceStorage(spaceId: string) {
  return request<{
    used: number;
    limit: number;
    breakdown: StorageBreakdownEntry[];
  }>(`/spaces/${spaceId}/storage`);
}

// Space File Permissions
export function getFilePermissions(spaceId: string, fileId: string) {
  return request<{ permissions: SpaceFilePermission[]; my_permission: string }>(
    `/spaces/${spaceId}/files/${fileId}/permissions`,
  );
}

export function setFilePermission(
  spaceId: string,
  fileId: string,
  userId: string,
  permission: string,
) {
  return request<{ ok: boolean }>(
    `/spaces/${spaceId}/files/${fileId}/permissions`,
    {
      method: 'PUT',
      body: JSON.stringify({ user_id: userId, permission }),
    },
  );
}

export function removeFilePermission(
  spaceId: string,
  fileId: string,
  userId: string,
) {
  return request<{ ok: boolean }>(
    `/spaces/${spaceId}/files/${fileId}/permissions/${userId}`,
    { method: 'DELETE' },
  );
}

// Space File Versions
export function listFileVersions(spaceId: string, fileId: string) {
  return request<{ versions: SpaceFileVersion[] }>(
    `/spaces/${spaceId}/files/${fileId}/versions`,
  );
}

export function uploadFileVersion(
  spaceId: string,
  fileId: string,
  file: File,
  onProgress?: (percent: number) => void,
): Promise<SpaceFileVersion> {
  return chunkedUpload(
    `${API_BASE}/spaces/${spaceId}/files/${fileId}/versions`,
    file,
    {
      total_size: file.size,
    },
    onProgress,
  );
}

export function getVersionDownloadUrl(
  spaceId: string,
  fileId: string,
  versionId: string,
): string {
  const token = getToken();
  return `${API_BASE}/spaces/${spaceId}/files/${fileId}/versions/${versionId}/download?token=${token}`;
}

export async function downloadFileVersion(
  spaceId: string,
  fileId: string,
  versionId: string,
  fileName: string,
) {
  const token = getToken();
  const res = await fetch(
    `${API_BASE}/spaces/${spaceId}/files/${fileId}/versions/${versionId}/download`,
    {
      headers: token ? { Authorization: `Bearer ${token}` } : {},
    },
  );
  if (!res.ok) throw new Error('Download failed');
  const blob = await res.blob();
  triggerDownload(blob, fileName);
}

export function revertFileVersion(
  spaceId: string,
  fileId: string,
  versionId: string,
) {
  return request<SpaceFileVersion>(
    `/spaces/${spaceId}/files/${fileId}/versions/${versionId}/revert`,
    { method: 'POST' },
  );
}

// Admin Storage
export interface SpaceStorageInfo {
  space_id: string;
  space_name: string;
  storage_used: number;
  storage_limit: number;
  file_count: number;
  is_personal?: boolean;
  personal_owner_name?: string;
}

export function getAdminStorage() {
  return request<{ spaces: SpaceStorageInfo[]; total_used: number }>(
    '/admin/storage',
  );
}

export interface SystemStats {
  cpu_percent: number;
  load_1m: number;
  load_5m: number;
  load_15m: number;
  mem_total_kb: number;
  mem_available_kb: number;
  swap_total_kb: number;
  swap_free_kb: number;
  net_rx_bytes: number;
  net_tx_bytes: number;
}

export function getSystemStats() {
  return request<SystemStats>('/admin/system-stats');
}

// Update listSpaceFiles return type to include my_permission
export function listSpaceFilesWithPermission(
  spaceId: string,
  parentId?: string,
) {
  const params = new URLSearchParams();
  if (parentId) params.set('parent_id', parentId);
  const qs = params.toString();
  return request<{
    files: SpaceFile[];
    path: SpaceFilePath[];
    my_permission: string;
  }>(`/spaces/${spaceId}/files${qs ? '?' + qs : ''}`);
}

// --- Calendar ---

export function listCalendarEvents(
  spaceId: string,
  start: string,
  end: string,
) {
  const params = new URLSearchParams({ start, end });
  return request<{ events: CalendarEvent[]; my_permission: string }>(
    `/spaces/${spaceId}/calendar/events?${params}`,
  );
}

export function createCalendarEvent(
  spaceId: string,
  event: {
    title: string;
    description?: string;
    location?: string;
    color?: string;
    start_time: string;
    end_time: string;
    all_day?: boolean;
    rrule?: string;
  },
) {
  return request<CalendarEvent>(`/spaces/${spaceId}/calendar/events`, {
    method: 'POST',
    body: JSON.stringify(event),
  });
}

export function updateCalendarEvent(
  spaceId: string,
  eventId: string,
  updates: {
    title?: string;
    description?: string;
    location?: string;
    color?: string;
    start_time?: string;
    end_time?: string;
    all_day?: boolean;
    rrule?: string;
  },
) {
  return request<CalendarEvent>(
    `/spaces/${spaceId}/calendar/events/${eventId}`,
    {
      method: 'PUT',
      body: JSON.stringify(updates),
    },
  );
}

export function deleteCalendarEvent(spaceId: string, eventId: string) {
  return request<{ ok: boolean }>(
    `/spaces/${spaceId}/calendar/events/${eventId}`,
    { method: 'DELETE' },
  );
}

export function createEventException(
  spaceId: string,
  eventId: string,
  exception: {
    original_date: string;
    is_deleted?: boolean;
    title?: string;
    description?: string;
    location?: string;
    color?: string;
    start_time?: string;
    end_time?: string;
    all_day?: boolean;
  },
) {
  return request<{ id: string }>(
    `/spaces/${spaceId}/calendar/events/${eventId}/exception`,
    {
      method: 'POST',
      body: JSON.stringify(exception),
    },
  );
}

export function setEventRsvp(
  spaceId: string,
  eventId: string,
  status: 'yes' | 'no' | 'maybe',
  occurrenceDate?: string,
) {
  return request<{ status: string }>(
    `/spaces/${spaceId}/calendar/events/${eventId}/rsvp`,
    {
      method: 'POST',
      body: JSON.stringify({
        status,
        occurrence_date: occurrenceDate || '1970-01-01 00:00:00+00',
      }),
    },
  );
}

export function getEventRsvps(
  spaceId: string,
  eventId: string,
  occurrenceDate?: string,
) {
  const params = new URLSearchParams();
  if (occurrenceDate) params.set('date', occurrenceDate);
  const qs = params.toString();
  return request<{ rsvps: CalendarEventRsvp[] }>(
    `/spaces/${spaceId}/calendar/events/${eventId}/rsvps${qs ? '?' + qs : ''}`,
  );
}

export function getCalendarPermissions(spaceId: string) {
  return request<{
    permissions: CalendarPermission[];
    my_permission: string;
  }>(`/spaces/${spaceId}/calendar/permissions`);
}

export function setCalendarPermission(
  spaceId: string,
  userId: string,
  permission: string,
) {
  return request<{ ok: boolean }>(`/spaces/${spaceId}/calendar/permissions`, {
    method: 'POST',
    body: JSON.stringify({ user_id: userId, permission }),
  });
}

export function removeCalendarPermission(spaceId: string, userId: string) {
  return request<{ ok: boolean }>(
    `/spaces/${spaceId}/calendar/permissions/${userId}`,
    { method: 'DELETE' },
  );
}

// --- Task Boards ---

export function listTaskBoards(spaceId: string) {
  return request<{ boards: TaskBoard[]; my_permission: string }>(
    `/spaces/${spaceId}/tasks/boards`,
  );
}

export function createTaskBoard(
  spaceId: string,
  data: { name: string; description?: string },
) {
  return request<TaskBoard>(`/spaces/${spaceId}/tasks/boards`, {
    method: 'POST',
    body: JSON.stringify(data),
  });
}

export function getTaskBoard(spaceId: string, boardId: string) {
  return request<TaskBoard>(`/spaces/${spaceId}/tasks/boards/${boardId}`);
}

export function updateTaskBoard(
  spaceId: string,
  boardId: string,
  data: { name?: string; description?: string },
) {
  return request<TaskBoard>(`/spaces/${spaceId}/tasks/boards/${boardId}`, {
    method: 'PUT',
    body: JSON.stringify(data),
  });
}

export function deleteTaskBoard(spaceId: string, boardId: string) {
  return request<{ ok: boolean }>(
    `/spaces/${spaceId}/tasks/boards/${boardId}`,
    { method: 'DELETE' },
  );
}

// Task Columns

export function createTaskColumn(
  spaceId: string,
  boardId: string,
  data: { name: string; position?: number; wip_limit?: number; color?: string },
) {
  return request<TaskColumn>(
    `/spaces/${spaceId}/tasks/boards/${boardId}/columns`,
    { method: 'POST', body: JSON.stringify(data) },
  );
}

export function updateTaskColumn(
  spaceId: string,
  boardId: string,
  columnId: string,
  data: { name?: string; wip_limit?: number; color?: string },
) {
  return request<TaskColumn>(
    `/spaces/${spaceId}/tasks/boards/${boardId}/columns/${columnId}`,
    { method: 'PUT', body: JSON.stringify(data) },
  );
}

export function reorderTaskColumns(
  spaceId: string,
  boardId: string,
  columnIds: string[],
) {
  return request<{ ok: boolean }>(
    `/spaces/${spaceId}/tasks/boards/${boardId}/columns/reorder`,
    { method: 'PUT', body: JSON.stringify({ column_ids: columnIds }) },
  );
}

export function deleteTaskColumn(
  spaceId: string,
  boardId: string,
  columnId: string,
) {
  return request<{ ok: boolean }>(
    `/spaces/${spaceId}/tasks/boards/${boardId}/columns/${columnId}`,
    { method: 'DELETE' },
  );
}

// Tasks

export function createTask(
  spaceId: string,
  boardId: string,
  data: {
    column_id: string;
    title: string;
    description?: string;
    priority?: string;
    due_date?: string;
    color?: string;
    position?: number;
    assignee_ids?: string[];
    label_ids?: string[];
  },
) {
  return request<TaskItem>(`/spaces/${spaceId}/tasks/boards/${boardId}/tasks`, {
    method: 'POST',
    body: JSON.stringify(data),
  });
}

export function getTaskDetail(
  spaceId: string,
  boardId: string,
  taskId: string,
) {
  return request<TaskItem>(
    `/spaces/${spaceId}/tasks/boards/${boardId}/tasks/${taskId}`,
  );
}

export function updateTask(
  spaceId: string,
  boardId: string,
  taskId: string,
  data: {
    column_id?: string;
    title?: string;
    description?: string;
    priority?: string;
    due_date?: string;
    start_date?: string;
    duration_days?: number;
    color?: string;
    position?: number;
    assignee_ids?: string[];
    label_ids?: string[];
  },
) {
  return request<TaskItem>(
    `/spaces/${spaceId}/tasks/boards/${boardId}/tasks/${taskId}`,
    { method: 'PUT', body: JSON.stringify(data) },
  );
}

export function deleteTask(spaceId: string, boardId: string, taskId: string) {
  return request<{ ok: boolean }>(
    `/spaces/${spaceId}/tasks/boards/${boardId}/tasks/${taskId}`,
    { method: 'DELETE' },
  );
}

export function reorderTasks(
  spaceId: string,
  boardId: string,
  tasks: { id: string; column_id: string; position: number }[],
) {
  return request<{ ok: boolean }>(
    `/spaces/${spaceId}/tasks/boards/${boardId}/tasks/reorder`,
    { method: 'PUT', body: JSON.stringify({ tasks }) },
  );
}

// Checklists

export function createTaskChecklist(
  spaceId: string,
  boardId: string,
  taskId: string,
  data: { title: string; position?: number },
) {
  return request<TaskChecklist>(
    `/spaces/${spaceId}/tasks/boards/${boardId}/tasks/${taskId}/checklists`,
    { method: 'POST', body: JSON.stringify(data) },
  );
}

export function deleteTaskChecklist(
  spaceId: string,
  boardId: string,
  taskId: string,
  checklistId: string,
) {
  return request<{ ok: boolean }>(
    `/spaces/${spaceId}/tasks/boards/${boardId}/tasks/${taskId}/checklists/${checklistId}`,
    { method: 'DELETE' },
  );
}

export function createChecklistItem(
  spaceId: string,
  boardId: string,
  taskId: string,
  checklistId: string,
  data: { content: string; position?: number },
) {
  return request<TaskChecklistItem>(
    `/spaces/${spaceId}/tasks/boards/${boardId}/tasks/${taskId}/checklists/${checklistId}/items`,
    { method: 'POST', body: JSON.stringify(data) },
  );
}

export function updateChecklistItem(
  spaceId: string,
  boardId: string,
  taskId: string,
  checklistId: string,
  itemId: string,
  data: { content?: string; is_checked?: boolean },
) {
  return request<TaskChecklistItem>(
    `/spaces/${spaceId}/tasks/boards/${boardId}/tasks/${taskId}/checklists/${checklistId}/items/${itemId}`,
    { method: 'PUT', body: JSON.stringify(data) },
  );
}

export function deleteChecklistItem(
  spaceId: string,
  boardId: string,
  taskId: string,
  checklistId: string,
  itemId: string,
) {
  return request<{ ok: boolean }>(
    `/spaces/${spaceId}/tasks/boards/${boardId}/tasks/${taskId}/checklists/${checklistId}/items/${itemId}`,
    { method: 'DELETE' },
  );
}

// Task Labels

export function createTaskLabel(
  spaceId: string,
  boardId: string,
  data: { name: string; color?: string },
) {
  return request<TaskLabel>(
    `/spaces/${spaceId}/tasks/boards/${boardId}/labels`,
    { method: 'POST', body: JSON.stringify(data) },
  );
}

export function updateTaskLabel(
  spaceId: string,
  boardId: string,
  labelId: string,
  data: { name: string; color?: string },
) {
  return request<TaskLabel>(
    `/spaces/${spaceId}/tasks/boards/${boardId}/labels/${labelId}`,
    { method: 'PUT', body: JSON.stringify(data) },
  );
}

export function deleteTaskLabel(
  spaceId: string,
  boardId: string,
  labelId: string,
) {
  return request<{ ok: boolean }>(
    `/spaces/${spaceId}/tasks/boards/${boardId}/labels/${labelId}`,
    { method: 'DELETE' },
  );
}

// Task Dependencies

export function addTaskDependency(
  spaceId: string,
  boardId: string,
  data: { task_id: string; depends_on_id: string; dependency_type?: string },
) {
  return request<TaskDependency>(
    `/spaces/${spaceId}/tasks/boards/${boardId}/dependencies`,
    { method: 'POST', body: JSON.stringify(data) },
  );
}

export function removeTaskDependency(
  spaceId: string,
  boardId: string,
  depId: string,
) {
  return request<{ ok: boolean }>(
    `/spaces/${spaceId}/tasks/boards/${boardId}/dependencies/${depId}`,
    { method: 'DELETE' },
  );
}

// Task Permissions (space-level)

export function getTaskPermissions(spaceId: string) {
  return request<{ permissions: TaskBoardPermission[]; my_permission: string }>(
    `/spaces/${spaceId}/tasks/permissions`,
  );
}

export function setTaskPermission(
  spaceId: string,
  userId: string,
  permission: string,
) {
  return request<{ ok: boolean }>(`/spaces/${spaceId}/tasks/permissions`, {
    method: 'POST',
    body: JSON.stringify({ user_id: userId, permission }),
  });
}

export function removeTaskPermission(spaceId: string, userId: string) {
  return request<{ ok: boolean }>(
    `/spaces/${spaceId}/tasks/permissions/${userId}`,
    { method: 'DELETE' },
  );
}

// --- Wiki ---

export type WikiPagePath = { id: string; name: string };

export function listWikiPages(spaceId: string, parentId?: string) {
  const params = new URLSearchParams();
  if (parentId) params.set('parent_id', parentId);
  const qs = params.toString();
  return request<{
    pages: WikiPage[];
    path: WikiPagePath[];
    my_permission: string;
  }>(`/spaces/${spaceId}/wiki/pages${qs ? '?' + qs : ''}`);
}

export function getWikiTree(spaceId: string) {
  return request<{ pages: WikiPage[]; my_permission: string }>(
    `/spaces/${spaceId}/wiki/tree`,
  );
}

export function createWikiPage(
  spaceId: string,
  data: {
    title: string;
    parent_id?: string;
    is_folder?: boolean;
    content?: string;
  },
) {
  return request<WikiPage>(`/spaces/${spaceId}/wiki/pages`, {
    method: 'POST',
    body: JSON.stringify(data),
  });
}

export function getWikiPage(spaceId: string, pageId: string) {
  return request<WikiPage & { path: WikiPagePath[] }>(
    `/spaces/${spaceId}/wiki/pages/${pageId}`,
  );
}

export function updateWikiPage(
  spaceId: string,
  pageId: string,
  data: {
    title?: string;
    content?: string;
    icon?: string;
    cover_image_file_id?: string;
    create_version?: boolean;
  },
) {
  return request<WikiPage>(`/spaces/${spaceId}/wiki/pages/${pageId}`, {
    method: 'PUT',
    body: JSON.stringify(data),
  });
}

export function deleteWikiPage(spaceId: string, pageId: string) {
  return request<{ ok: boolean }>(`/spaces/${spaceId}/wiki/pages/${pageId}`, {
    method: 'DELETE',
  });
}

export function moveWikiPage(
  spaceId: string,
  pageId: string,
  newParentId: string | null,
) {
  return request<WikiPage>(`/spaces/${spaceId}/wiki/pages/${pageId}/move`, {
    method: 'PUT',
    body: JSON.stringify({ parent_id: newParentId }),
  });
}

export function reorderWikiPages(
  spaceId: string,
  pagePositions: { id: string; position: number }[],
) {
  return request<{ ok: boolean }>(`/spaces/${spaceId}/wiki/pages/reorder`, {
    method: 'POST',
    body: JSON.stringify({ page_positions: pagePositions }),
  });
}

export function listWikiPageVersions(spaceId: string, pageId: string) {
  return request<{ versions: WikiPageVersion[] }>(
    `/spaces/${spaceId}/wiki/pages/${pageId}/versions`,
  );
}

export function getWikiPageVersion(
  spaceId: string,
  pageId: string,
  versionId: string,
) {
  return request<WikiPageVersion>(
    `/spaces/${spaceId}/wiki/pages/${pageId}/versions/${versionId}`,
  );
}

export function revertWikiPage(
  spaceId: string,
  pageId: string,
  versionId: string,
) {
  return request<WikiPage>(
    `/spaces/${spaceId}/wiki/pages/${pageId}/versions/${versionId}/revert`,
    { method: 'POST' },
  );
}

export function getWikiPagePermissions(spaceId: string, pageId: string) {
  return request<{ permissions: WikiPagePermission[]; my_permission: string }>(
    `/spaces/${spaceId}/wiki/pages/${pageId}/permissions`,
  );
}

export function setWikiPagePermission(
  spaceId: string,
  pageId: string,
  userId: string,
  permission: string,
) {
  return request<{ ok: boolean }>(
    `/spaces/${spaceId}/wiki/pages/${pageId}/permissions`,
    {
      method: 'POST',
      body: JSON.stringify({ user_id: userId, permission }),
    },
  );
}

export function removeWikiPagePermission(
  spaceId: string,
  pageId: string,
  userId: string,
) {
  return request<{ ok: boolean }>(
    `/spaces/${spaceId}/wiki/pages/${pageId}/permissions/${userId}`,
    { method: 'DELETE' },
  );
}

export function getWikiPermissions(spaceId: string) {
  return request<{ permissions: WikiPermission[]; my_permission: string }>(
    `/spaces/${spaceId}/wiki/permissions`,
  );
}

export function setWikiPermission(
  spaceId: string,
  userId: string,
  permission: string,
) {
  return request<{ ok: boolean }>(`/spaces/${spaceId}/wiki/permissions`, {
    method: 'POST',
    body: JSON.stringify({ user_id: userId, permission }),
  });
}

export function removeWikiPermission(spaceId: string, userId: string) {
  return request<{ ok: boolean }>(
    `/spaces/${spaceId}/wiki/permissions/${userId}`,
    { method: 'DELETE' },
  );
}

export function uploadWikiMedia(
  spaceId: string,
  file: File,
  onProgress?: (percent: number) => void,
): Promise<{ file_id: string; url: string }> {
  return chunkedUpload(
    `${API_BASE}/spaces/${spaceId}/wiki/upload`,
    file,
    {
      filename: file.name,
      content_type: file.type || 'application/octet-stream',
    },
    onProgress,
  );
}

export function getWikiMediaUrl(url: string, inline?: boolean): string {
  const token = getToken();
  const base = `${url}?token=${token}`;
  return inline ? `${base}&inline=1` : base;
}

// Shared with me
export function getSharedWithMe() {
  return request<import('../types').SharedWithMe>('/shared-with-me');
}

// AI conversations
export async function listAiConversations(limit = 50, offset = 0) {
  return request<AiConversation[]>(
    `/ai/conversations?limit=${limit}&offset=${offset}`,
  );
}

export async function createAiConversation(title?: string) {
  return request<AiConversation>('/ai/conversations', {
    method: 'POST',
    body: JSON.stringify({ title: title || 'New conversation' }),
  });
}

export async function getAiConversation(id: string) {
  return request<AiConversation & { messages: AiMessage[] }>(
    `/ai/conversations/${id}`,
  );
}

export async function deleteAiConversation(id: string) {
  return request<{ ok: boolean }>(`/ai/conversations/${id}`, {
    method: 'DELETE',
  });
}

export async function updateAiConversationTitle(id: string, title: string) {
  return request<{ ok: boolean }>(`/ai/conversations/${id}`, {
    method: 'PUT',
    body: JSON.stringify({ title }),
  });
}

export async function sendAiMessage(
  conversationId: string,
  content: string,
  currentSpaceId?: string,
  currentChannelId?: string,
) {
  return request<{ status: string }>(
    `/ai/conversations/${conversationId}/messages`,
    {
      method: 'POST',
      body: JSON.stringify({
        content,
        current_space_id: currentSpaceId || '',
        current_channel_id: currentChannelId || '',
      }),
    },
  );
}

export async function stopAiGeneration(conversationId: string) {
  return request<{ ok: boolean }>(`/ai/conversations/${conversationId}/stop`, {
    method: 'POST',
  });
}

// User settings
export async function getUserSettings() {
  return request<Record<string, string>>('/users/me/settings');
}

export async function updateUserSettings(settings: Record<string, string>) {
  return request<{ ok: boolean }>('/users/me/settings', {
    method: 'PUT',
    body: JSON.stringify(settings),
  });
}
