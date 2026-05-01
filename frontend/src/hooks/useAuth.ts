import { useEffect, useState } from 'react';
import { useChatStore } from '../stores/chatStore';
import * as api from '../services/api';

/**
 * On mount, optimistically check whether we have a logged-in session by
 * calling /api/me — the server will reject with 401 if the cookie is
 * missing, expired, or invalid. The session itself lives in an HttpOnly
 * cookie (P1.4 Release B) and is not visible to JS, so we can no longer
 * read it directly; instead we use a non-sensitive `logged_in` flag in
 * localStorage to decide whether to attempt the /me probe at all.
 */
export function useAuth() {
  const isAuthenticated = useChatStore((s) => s.isAuthenticated);
  const user = useChatStore((s) => s.user);
  const setAuth = useChatStore((s) => s.setAuth);
  const clearAuth = useChatStore((s) => s.clearAuth);
  const [loading, setLoading] = useState(
    () => !!localStorage.getItem('logged_in'),
  );

  useEffect(() => {
    // No prior session hint — nothing to verify, render the login page.
    if (!localStorage.getItem('logged_in')) return;

    api
      .getMe()
      .then((userData) => setAuth(userData))
      .catch(() => clearAuth())
      .finally(() => setLoading(false));
  }, [setAuth, clearAuth]);

  return { isAuthenticated, loading, user };
}
