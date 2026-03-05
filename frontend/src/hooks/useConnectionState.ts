import { useSyncExternalStore } from 'react';
import { wsService } from '../services/websocket';

export function useConnectionState() {
  return useSyncExternalStore(
    (callback) => wsService.onStateChange(callback),
    () => wsService.connectionState,
  );
}

export function useHasConnected() {
  return useSyncExternalStore(
    (callback) => wsService.onStateChange(callback),
    () => wsService.hasConnected,
  );
}
