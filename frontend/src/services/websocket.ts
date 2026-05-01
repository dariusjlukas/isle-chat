type MessageHandler = (data: unknown) => void;
type ConnectionState = 'connected' | 'disconnected' | 'connecting';
type StateListener = (state: ConnectionState) => void;

const HEARTBEAT_INTERVAL = 5_000;
const PONG_TIMEOUT = 10_000;
const RECONNECT_DELAY = 5_000;
const CONNECT_TIMEOUT = 5_000;

export class WebSocketService {
  private ws: WebSocket | null = null;
  private handlers: Map<string, Set<MessageHandler>> = new Map();
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  /**
   * Whether a connection has been requested (i.e. user is logged in).
   * The actual session token lives in the HttpOnly `session` cookie now;
   * we keep a flag here so we know whether to (re)connect on lifecycle events.
   */
  private wantConnection = false;
  private heartbeatInterval: ReturnType<typeof setInterval> | null = null;
  private connectTimer: ReturnType<typeof setTimeout> | null = null;
  private _connectionState: ConnectionState = 'disconnected';
  private _hasConnected = false;
  private stateListeners: Set<StateListener> = new Set();
  private _pingSentAt: number | null = null;
  private _lastPingMs: number | null = null;
  private _lastHeartbeat: Date | null = null;
  /** Monotonic timestamp (ms) of last successful pong, used by watchdog. */
  private _lastPongAt: number = 0;

  get connectionState() {
    return this._connectionState;
  }

  get hasConnected() {
    return this._hasConnected;
  }

  onStateChange(listener: StateListener) {
    this.stateListeners.add(listener);
    return () => {
      this.stateListeners.delete(listener);
    };
  }

  private setState(state: ConnectionState) {
    if (this._connectionState === state) return;
    this._connectionState = state;
    if (state === 'connected') this._hasConnected = true;
    this.stateListeners.forEach((l) => l(state));
  }

  getHeartbeatInfo() {
    return {
      lastHeartbeat: this._lastHeartbeat,
      lastPingMs: this._lastPingMs,
    };
  }

  connect() {
    this.wantConnection = true;
    this.doConnect();
  }

  private doConnect() {
    if (!this.wantConnection) return;

    this.setState('connecting');

    // The session is identified via the HttpOnly `session` cookie which the
    // browser sends automatically on the WS upgrade (same-origin). The legacy
    // `?token=` query param is gone since P1.4 Release B.
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/ws`;

    const socket = new WebSocket(wsUrl);
    this.ws = socket;

    // Force-close if connection doesn't establish quickly (Firefox can hang)
    this.connectTimer = setTimeout(() => {
      console.warn('[WS] Connect timeout, closing socket');
      this.connectTimer = null;
      socket.close();
    }, CONNECT_TIMEOUT);

    socket.onopen = () => {
      // Ignore callbacks from stale sockets (e.g. after logout/re-login)
      if (this.ws !== socket) return;
      console.log('[WS] Connected');
      if (this.connectTimer) {
        clearTimeout(this.connectTimer);
        this.connectTimer = null;
      }
      if (this.reconnectTimer) {
        clearTimeout(this.reconnectTimer);
        this.reconnectTimer = null;
      }
      this.setState('connected');
      this.startHeartbeat();
    };

    socket.onmessage = (event) => {
      if (this.ws !== socket) return;
      try {
        const data = JSON.parse(event.data);
        const type = data.type as string;

        // Handle pong internally
        if (type === 'pong') {
          const now = Date.now();
          this._lastPongAt = now;
          if (this._pingSentAt !== null) {
            this._lastPingMs = now - this._pingSentAt;
            this._pingSentAt = null;
          }
          this._lastHeartbeat = new Date(now);
          return;
        }

        const typeHandlers = this.handlers.get(type);
        if (typeHandlers) {
          typeHandlers.forEach((handler) => handler(data));
        }
        // Also fire to wildcard handlers
        const allHandlers = this.handlers.get('*');
        if (allHandlers) {
          allHandlers.forEach((handler) => handler(data));
        }
      } catch (e) {
        console.error('[WS] Failed to parse message:', e);
      }
    };

    socket.onclose = () => {
      // Ignore if this socket has been replaced (logout then re-login)
      if (this.ws !== socket) return;
      console.log(
        `[WS] Disconnected, reconnecting in ${RECONNECT_DELAY / 1000}s...`,
      );
      if (this.connectTimer) {
        clearTimeout(this.connectTimer);
        this.connectTimer = null;
      }
      this.stopHeartbeat();
      this.setState('disconnected');
      this.reconnectTimer = setTimeout(() => this.doConnect(), RECONNECT_DELAY);
    };

    socket.onerror = (err) => {
      console.error('[WS] Error:', err);
    };
  }

  private startHeartbeat() {
    this.stopHeartbeat();
    // Seed the watchdog so the first check doesn't immediately fire
    this._lastPongAt = Date.now();
    this.heartbeatInterval = setInterval(() => {
      try {
        const now = Date.now();
        const sincePong = now - this._lastPongAt;

        // Watchdog: runs BEFORE the readyState check. If the socket is
        // stuck in CLOSING/CLOSED without onclose firing (e.g. proxy
        // timeout, network glitch), the readyState gate below would
        // skip everything and the connection would be stuck forever.
        if (sincePong > PONG_TIMEOUT) {
          console.warn(
            `[WS] No pong received in ${sincePong}ms, forcing reconnect`,
          );
          this.forceReconnect();
          return;
        }

        // Only send pings on an open connection
        if (!this.ws || this.ws.readyState !== WebSocket.OPEN) return;

        this._pingSentAt = now;
        this.ws.send(JSON.stringify({ type: 'ping' }));
      } catch (e) {
        console.error('[WS] Heartbeat error, forcing reconnect:', e);
        this.forceReconnect();
      }
    }, HEARTBEAT_INTERVAL);
  }

  /** Tear down the current socket immediately and schedule a reconnect. */
  private forceReconnect() {
    this.stopHeartbeat();
    if (this.connectTimer) {
      clearTimeout(this.connectTimer);
      this.connectTimer = null;
    }
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    if (this.ws) {
      const old = this.ws;
      this.ws = null; // Null out first so stale onclose is ignored
      old.onclose = null;
      old.onmessage = null;
      old.onerror = null;
      old.close();
    }
    this.setState('disconnected');
    this.reconnectTimer = setTimeout(() => this.doConnect(), RECONNECT_DELAY);
  }

  private stopHeartbeat() {
    if (this.heartbeatInterval) {
      clearInterval(this.heartbeatInterval);
      this.heartbeatInterval = null;
    }
  }

  disconnect() {
    this.wantConnection = false;
    this._hasConnected = false;
    this.stopHeartbeat();
    if (this.connectTimer) {
      clearTimeout(this.connectTimer);
      this.connectTimer = null;
    }
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    if (this.ws) {
      const old = this.ws;
      this.ws = null; // Null out first so stale onclose is ignored
      old.onclose = null;
      old.onmessage = null;
      old.onerror = null;
      old.close();
    }
  }

  send(data: object) {
    if (this.ws && this.ws.readyState === WebSocket.OPEN) {
      this.ws.send(JSON.stringify(data));
    }
  }

  on(type: string, handler: MessageHandler) {
    if (!this.handlers.has(type)) {
      this.handlers.set(type, new Set());
    }
    this.handlers.get(type)!.add(handler);
    return () => {
      this.handlers.get(type)?.delete(handler);
    };
  }

  off(type: string, handler: MessageHandler) {
    this.handlers.get(type)?.delete(handler);
  }
}

export const wsService = new WebSocketService();

// Expose diagnostics on window for E2E tests
(window as unknown as Record<string, unknown>).__wsDebug = () => ({
  connectionState: wsService.connectionState,
  hasConnected: wsService.hasConnected,
  lastHeartbeat: wsService.getHeartbeatInfo().lastHeartbeat?.getTime() ?? null,
  lastPingMs: wsService.getHeartbeatInfo().lastPingMs,
});
