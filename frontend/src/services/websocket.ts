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
  private token: string | null = null;
  private heartbeatInterval: ReturnType<typeof setInterval> | null = null;
  private pongTimer: ReturnType<typeof setTimeout> | null = null;
  private connectTimer: ReturnType<typeof setTimeout> | null = null;
  private _connectionState: ConnectionState = 'disconnected';
  private _hasConnected = false;
  private stateListeners: Set<StateListener> = new Set();
  private _pingSentAt: number | null = null;
  private _lastPingMs: number | null = null;
  private _lastHeartbeat: Date | null = null;

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

  connect(token: string) {
    this.token = token;
    this.doConnect();
  }

  private doConnect() {
    if (!this.token) return;

    this.setState('connecting');

    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/ws?token=${this.token}`;

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
          if (this.pongTimer) {
            clearTimeout(this.pongTimer);
            this.pongTimer = null;
          }
          if (this._pingSentAt !== null) {
            this._lastPingMs = Date.now() - this._pingSentAt;
            this._pingSentAt = null;
          }
          this._lastHeartbeat = new Date();
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
    this.heartbeatInterval = setInterval(() => {
      if (this.ws && this.ws.readyState === WebSocket.OPEN) {
        this._pingSentAt = Date.now();
        this.ws.send(JSON.stringify({ type: 'ping' }));
        this.pongTimer = setTimeout(() => {
          console.warn('[WS] Pong timeout, closing connection');
          this.ws?.close();
        }, PONG_TIMEOUT);
      }
    }, HEARTBEAT_INTERVAL);
  }

  private stopHeartbeat() {
    if (this.heartbeatInterval) {
      clearInterval(this.heartbeatInterval);
      this.heartbeatInterval = null;
    }
    if (this.pongTimer) {
      clearTimeout(this.pongTimer);
      this.pongTimer = null;
    }
  }

  disconnect() {
    this.token = null;
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
