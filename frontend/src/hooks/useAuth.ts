import { useEffect, useState } from 'react';
import { useChatStore } from '../stores/chatStore';
import * as api from '../services/api';

export function useAuth() {
  const isAuthenticated = useChatStore((s) => s.isAuthenticated);
  const user = useChatStore((s) => s.user);
  const setAuth = useChatStore((s) => s.setAuth);
  const clearAuth = useChatStore((s) => s.clearAuth);
  const [loading, setLoading] = useState(
    () => !!localStorage.getItem('session_token'),
  );

  useEffect(() => {
    const token = localStorage.getItem('session_token');
    if (!token) return;

    api
      .getMe()
      .then((userData) => setAuth(userData, token))
      .catch(() => clearAuth())
      .finally(() => setLoading(false));
  }, [setAuth, clearAuth]);

  return { isAuthenticated, loading, user };
}
