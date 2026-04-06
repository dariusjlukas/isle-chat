import { create } from 'zustand';
import type {
  User,
  Channel,
  Message,
  Reaction,
  Space,
  SpaceInvite,
  SidebarView,
  ReadReceiptInfo,
  Notification,
} from '../types';

interface ChatState {
  // Auth
  user: User | null;
  token: string | null;
  isAuthenticated: boolean;
  authError: string | null;

  // Chat
  channels: Channel[];
  activeChannelId: string | null;
  messages: Record<string, Message[]>; // channel_id -> messages
  users: User[];
  typingUsers: Record<string, string[]>; // channel_id -> usernames
  uploadProgress: number | null;

  // Spaces
  spaces: Space[];
  activeView: SidebarView | null;
  sidePanelCollapsed: boolean;
  activeToolView:
    | { type: 'files'; spaceId: string }
    | { type: 'calendar'; spaceId: string }
    | { type: 'tasks'; spaceId: string }
    | { type: 'wiki'; spaceId: string }
    | { type: 'minigames'; spaceId: string }
    | null;
  wikiSidebarOpen: boolean;

  // Server state
  serverArchived: boolean;
  serverLockedDown: boolean;
  serverName: string;
  serverIconFileId: string | null;
  serverIconDarkFileId: string | null;
  pendingRequestCount: number;
  adminPanelRequest: string | null;
  spaceInvites: SpaceInvite[];

  // Search
  jumpToMessageId: string | null;
  jumpToChannelId: string | null;

  // Unread / Read receipts
  unreadCounts: Record<string, number>;
  mentionCounts: Record<string, number>;
  readReceipts: Record<string, Record<string, ReadReceiptInfo>>;

  // Notifications
  notifications: Notification[];
  unreadNotificationCount: number;

  // AI
  llmEnabled: boolean;
  showAiPanel: boolean;

  // Actions
  setAuth: (user: User, token: string) => void;
  clearAuth: (reason?: string) => void;
  setChannels: (channels: Channel[]) => void;
  setActiveChannel: (channelId: string | null) => void;
  addChannel: (channel: Channel) => void;
  setMessages: (channelId: string, messages: Message[]) => void;
  addMessage: (message: Message) => void;
  updateMessage: (message: Message) => void;
  setUsers: (users: User[]) => void;
  updateUser: (fields: Partial<User>) => void;
  updateUserInList: (userId: string, fields: Partial<User>) => void;
  setUserOnline: (userId: string, online: boolean, lastSeen?: string) => void;
  updateChannel: (updates: Partial<Channel> & { id: string }) => void;
  removeChannel: (channelId: string) => void;
  setTyping: (channelId: string, username: string) => void;
  clearTyping: (channelId: string, username: string) => void;
  setUploadProgress: (progress: number | null) => void;
  setSpaces: (spaces: Space[]) => void;
  addSpace: (space: Space) => void;
  updateSpace: (updates: Partial<Space> & { id: string }) => void;
  removeSpace: (spaceId: string) => void;
  setActiveView: (view: SidebarView | null) => void;
  setSidePanelCollapsed: (collapsed: boolean) => void;
  setActiveToolView: (
    view:
      | { type: 'files'; spaceId: string }
      | { type: 'calendar'; spaceId: string }
      | { type: 'tasks'; spaceId: string }
      | { type: 'wiki'; spaceId: string }
      | { type: 'minigames'; spaceId: string }
      | null,
  ) => void;
  setWikiSidebarOpen: (open: boolean) => void;
  toggleWikiSidebar: () => void;
  setUnreadCounts: (counts: Record<string, number>) => void;
  setMentionCounts: (counts: Record<string, number>) => void;
  incrementUnread: (channelId: string) => void;
  incrementMentionCount: (channelId: string) => void;
  clearUnread: (channelId: string) => void;
  updateReadReceipt: (
    channelId: string,
    userId: string,
    info: ReadReceiptInfo,
  ) => void;
  setReadReceipts: (
    channelId: string,
    receipts: Record<string, ReadReceiptInfo>,
  ) => void;
  setServerArchived: (archived: boolean) => void;
  setServerLockedDown: (lockedDown: boolean) => void;
  setServerName: (name: string) => void;
  setServerIconFileId: (fileId: string | null) => void;
  setServerIconDarkFileId: (fileId: string | null) => void;
  setPendingRequestCount: (count: number) => void;
  requestAdminPanel: (tab: string) => void;
  clearAdminPanelRequest: () => void;
  setSpaceInvites: (invites: SpaceInvite[]) => void;
  addSpaceInvite: (invite: SpaceInvite) => void;
  removeSpaceInvite: (inviteId: string) => void;
  setJumpToMessage: (channelId: string, messageId: string) => void;
  clearJumpToMessage: () => void;
  addReaction: (
    channelId: string,
    messageId: string,
    reaction: Reaction,
  ) => void;
  removeReaction: (
    channelId: string,
    messageId: string,
    emoji: string,
    userId: string,
  ) => void;
  setNotifications: (notifications: Notification[]) => void;
  addNotification: (notification: Notification) => void;
  setUnreadNotificationCount: (count: number) => void;
  markNotificationRead: (notificationId: string) => void;
  markAllNotificationsRead: () => void;
  setLlmEnabled: (enabled: boolean) => void;
  setShowAiPanel: (show: boolean) => void;
  toggleAiPanel: () => void;
}

export const useChatStore = create<ChatState>((set) => ({
  user: null,
  token: null,
  isAuthenticated: false,
  authError: null,
  channels: [],
  activeChannelId: null,
  messages: {},
  users: [],
  typingUsers: {},
  uploadProgress: null,
  spaces: [],
  activeView: null,
  sidePanelCollapsed: false,
  activeToolView: null,
  wikiSidebarOpen: true,
  serverArchived: false,
  serverLockedDown: false,
  serverName: 'EnclaveStation',
  serverIconFileId: null,
  serverIconDarkFileId: null,
  pendingRequestCount: 0,
  adminPanelRequest: null,
  spaceInvites: [],
  notifications: [],
  unreadNotificationCount: 0,
  llmEnabled: false,
  showAiPanel: false,
  jumpToMessageId: null,
  jumpToChannelId: null,
  unreadCounts: {},
  mentionCounts: {},
  readReceipts: {},

  setAuth: (user, token) => {
    localStorage.setItem('session_token', token);
    set({ user, token, isAuthenticated: true, authError: null });
  },

  clearAuth: (reason?) => {
    localStorage.removeItem('session_token');
    set({
      user: null,
      token: null,
      isAuthenticated: false,
      authError: reason || null,
      channels: [],
      messages: {},
      activeChannelId: null,
      spaces: [],
      activeView: null,
      activeToolView: null,
      unreadCounts: {},
      mentionCounts: {},
      readReceipts: {},
      spaceInvites: [],
      notifications: [],
      unreadNotificationCount: 0,
    });
  },

  setChannels: (channels) => set({ channels }),

  setActiveChannel: (channelId) =>
    set((state) => {
      if (!channelId) {
        return { activeChannelId: null };
      }

      // Count unread notifications for this channel that will be marked read
      const channelNotifCount = state.notifications.filter(
        (n) => !n.is_read && n.channel_id === channelId,
      ).length;

      return {
        activeChannelId: channelId,
        activeToolView: null,
        unreadCounts: { ...state.unreadCounts, [channelId]: 0 },
        mentionCounts: { ...state.mentionCounts, [channelId]: 0 },
        notifications:
          channelNotifCount > 0
            ? state.notifications.map((n) =>
                n.channel_id === channelId && !n.is_read
                  ? { ...n, is_read: true }
                  : n,
              )
            : state.notifications,
        unreadNotificationCount: Math.max(
          0,
          state.unreadNotificationCount - channelNotifCount,
        ),
      };
    }),

  addChannel: (channel) =>
    set((state) => ({
      channels: state.channels.some((c) => c.id === channel.id)
        ? state.channels
        : [...state.channels, channel],
    })),

  setMessages: (channelId, messages) =>
    set((state) => ({
      messages: { ...state.messages, [channelId]: messages },
    })),

  addMessage: (message) =>
    set((state) => {
      const existing = state.messages[message.channel_id] || [];
      if (existing.some((m) => m.id === message.id)) return state;
      return {
        messages: {
          ...state.messages,
          [message.channel_id]: [...existing, message],
        },
      };
    }),

  updateMessage: (message) =>
    set((state) => {
      const existing = state.messages[message.channel_id];
      if (!existing) return state;
      return {
        messages: {
          ...state.messages,
          [message.channel_id]: existing.map((m) =>
            m.id === message.id ? message : m,
          ),
        },
      };
    }),

  updateUser: (fields) =>
    set((state) => ({
      user: state.user ? { ...state.user, ...fields } : null,
    })),

  setUsers: (users) => set({ users }),

  updateUserInList: (userId, fields) =>
    set((state) => ({
      users: state.users.map((u) =>
        u.id === userId ? { ...u, ...fields } : u,
      ),
    })),

  setUserOnline: (userId, online, lastSeen) =>
    set((state) => {
      const extra = lastSeen ? { last_seen: lastSeen } : {};
      return {
        users: state.users.map((u) =>
          u.id === userId ? { ...u, is_online: online, ...extra } : u,
        ),
        channels: state.channels.map((c) => ({
          ...c,
          members: c.members.map((m) =>
            m.id === userId ? { ...m, is_online: online, ...extra } : m,
          ),
        })),
      };
    }),

  updateChannel: (updates) =>
    set((state) => ({
      channels: state.channels.map((c) =>
        c.id === updates.id ? { ...c, ...updates } : c,
      ),
    })),

  removeChannel: (channelId) =>
    set((state) => ({
      channels: state.channels.filter((c) => c.id !== channelId),
      activeChannelId:
        state.activeChannelId === channelId ? null : state.activeChannelId,
    })),

  setTyping: (channelId, username) =>
    set((state) => {
      const current = state.typingUsers[channelId] || [];
      if (current.includes(username)) return state;
      return {
        typingUsers: {
          ...state.typingUsers,
          [channelId]: [...current, username],
        },
      };
    }),

  clearTyping: (channelId, username) =>
    set((state) => {
      const current = state.typingUsers[channelId] || [];
      return {
        typingUsers: {
          ...state.typingUsers,
          [channelId]: current.filter((u) => u !== username),
        },
      };
    }),

  setUploadProgress: (progress) => set({ uploadProgress: progress }),

  setSpaces: (spaces) => set({ spaces }),

  addSpace: (space) =>
    set((state) => ({
      spaces: [...state.spaces, space],
    })),

  updateSpace: (updates) =>
    set((state) => ({
      spaces: state.spaces.map((s) =>
        s.id === updates.id ? { ...s, ...updates } : s,
      ),
    })),

  removeSpace: (spaceId) =>
    set((state) => ({
      spaces: state.spaces.filter((s) => s.id !== spaceId),
      activeView:
        state.activeView?.type === 'space' &&
        state.activeView.spaceId === spaceId
          ? { type: 'messages' }
          : state.activeView,
    })),

  setActiveView: (view) => set({ activeView: view, sidePanelCollapsed: false }),

  setSidePanelCollapsed: (collapsed) => set({ sidePanelCollapsed: collapsed }),

  setActiveToolView: (view) =>
    set((state) => ({
      activeToolView: view,
      // Clear active channel when entering a tool view
      activeChannelId: view ? null : state.activeChannelId,
    })),

  setWikiSidebarOpen: (open) => set({ wikiSidebarOpen: open }),
  toggleWikiSidebar: () =>
    set((state) => ({ wikiSidebarOpen: !state.wikiSidebarOpen })),

  setUnreadCounts: (counts) => set({ unreadCounts: counts }),
  setMentionCounts: (counts) => set({ mentionCounts: counts }),

  incrementUnread: (channelId) =>
    set((state) => ({
      unreadCounts: {
        ...state.unreadCounts,
        [channelId]: (state.unreadCounts[channelId] || 0) + 1,
      },
    })),

  incrementMentionCount: (channelId) =>
    set((state) => ({
      mentionCounts: {
        ...state.mentionCounts,
        [channelId]: (state.mentionCounts[channelId] || 0) + 1,
      },
    })),

  clearUnread: (channelId) =>
    set((state) => ({
      unreadCounts: { ...state.unreadCounts, [channelId]: 0 },
      mentionCounts: { ...state.mentionCounts, [channelId]: 0 },
    })),

  updateReadReceipt: (channelId, userId, info) =>
    set((state) => ({
      readReceipts: {
        ...state.readReceipts,
        [channelId]: {
          ...(state.readReceipts[channelId] || {}),
          [userId]: info,
        },
      },
    })),

  setReadReceipts: (channelId, receipts) =>
    set((state) => ({
      readReceipts: {
        ...state.readReceipts,
        [channelId]: receipts,
      },
    })),

  setServerArchived: (archived) => set({ serverArchived: archived }),
  setServerLockedDown: (lockedDown) => set({ serverLockedDown: lockedDown }),
  setServerName: (name) => set({ serverName: name }),
  setServerIconFileId: (fileId) => set({ serverIconFileId: fileId }),
  setServerIconDarkFileId: (fileId) => set({ serverIconDarkFileId: fileId }),
  setPendingRequestCount: (count) => set({ pendingRequestCount: count }),
  requestAdminPanel: (tab) => set({ adminPanelRequest: tab }),
  clearAdminPanelRequest: () => set({ adminPanelRequest: null }),

  setSpaceInvites: (invites) => set({ spaceInvites: invites }),
  addSpaceInvite: (invite) =>
    set((state) => ({ spaceInvites: [...state.spaceInvites, invite] })),
  removeSpaceInvite: (inviteId) =>
    set((state) => ({
      spaceInvites: state.spaceInvites.filter((i) => i.id !== inviteId),
    })),

  setJumpToMessage: (channelId, messageId) =>
    set({ jumpToChannelId: channelId, jumpToMessageId: messageId }),

  clearJumpToMessage: () =>
    set({ jumpToChannelId: null, jumpToMessageId: null }),

  addReaction: (channelId, messageId, reaction) =>
    set((state) => {
      const msgs = state.messages[channelId];
      if (!msgs) return state;
      return {
        messages: {
          ...state.messages,
          [channelId]: msgs.map((m) => {
            if (m.id !== messageId) return m;
            const existing = m.reactions || [];
            if (
              existing.some(
                (r) =>
                  r.emoji === reaction.emoji && r.user_id === reaction.user_id,
              )
            )
              return m;
            return { ...m, reactions: [...existing, reaction] };
          }),
        },
      };
    }),

  removeReaction: (channelId, messageId, emoji, userId) =>
    set((state) => {
      const msgs = state.messages[channelId];
      if (!msgs) return state;
      return {
        messages: {
          ...state.messages,
          [channelId]: msgs.map((m) => {
            if (m.id !== messageId) return m;
            return {
              ...m,
              reactions: (m.reactions || []).filter(
                (r) => !(r.emoji === emoji && r.user_id === userId),
              ),
            };
          }),
        },
      };
    }),

  setNotifications: (notifications) => set({ notifications }),

  addNotification: (notification) =>
    set((state) => {
      // If the user is currently viewing the channel this notification is for,
      // add it as already read so the bell badge doesn't flash
      const isActiveChannel =
        notification.channel_id &&
        notification.channel_id === state.activeChannelId;

      return {
        notifications: [
          isActiveChannel ? { ...notification, is_read: true } : notification,
          ...state.notifications,
        ],
        unreadNotificationCount: isActiveChannel
          ? state.unreadNotificationCount
          : state.unreadNotificationCount + 1,
      };
    }),

  setUnreadNotificationCount: (count) =>
    set({ unreadNotificationCount: count }),

  markNotificationRead: (notificationId) =>
    set((state) => {
      const notification = state.notifications.find(
        (n) => n.id === notificationId,
      );
      if (!notification || notification.is_read) return state;

      const updates: Partial<ChatState> = {
        notifications: state.notifications.map((n) =>
          n.id === notificationId ? { ...n, is_read: true } : n,
        ),
        unreadNotificationCount: Math.max(0, state.unreadNotificationCount - 1),
      };

      // Sync admin badge for join request notifications
      if (notification.type === 'join_request') {
        updates.pendingRequestCount = Math.max(
          0,
          state.pendingRequestCount - 1,
        );
      }

      // Sync channel badge counts for message-related notifications
      if (
        notification.channel_id &&
        (notification.type === 'mention' ||
          notification.type === 'reply' ||
          notification.type === 'direct_message')
      ) {
        const cid = notification.channel_id;
        updates.unreadCounts = {
          ...state.unreadCounts,
          [cid]: Math.max(0, (state.unreadCounts[cid] || 0) - 1),
        };
        if (notification.type === 'mention') {
          updates.mentionCounts = {
            ...state.mentionCounts,
            [cid]: Math.max(0, (state.mentionCounts[cid] || 0) - 1),
          };
        }
      }

      return updates;
    }),

  markAllNotificationsRead: () =>
    set((state) => {
      const newUnreadCounts = { ...state.unreadCounts };
      const newMentionCounts = { ...state.mentionCounts };
      let joinRequestDismissals = 0;

      for (const n of state.notifications) {
        if (!n.is_read) {
          if (n.type === 'join_request') {
            joinRequestDismissals++;
          } else if (
            n.channel_id &&
            (n.type === 'mention' ||
              n.type === 'reply' ||
              n.type === 'direct_message')
          ) {
            newUnreadCounts[n.channel_id] = Math.max(
              0,
              (newUnreadCounts[n.channel_id] || 0) - 1,
            );
            if (n.type === 'mention') {
              newMentionCounts[n.channel_id] = Math.max(
                0,
                (newMentionCounts[n.channel_id] || 0) - 1,
              );
            }
          }
        }
      }

      return {
        notifications: state.notifications.map((n) => ({
          ...n,
          is_read: true,
        })),
        unreadNotificationCount: 0,
        unreadCounts: newUnreadCounts,
        mentionCounts: newMentionCounts,
        pendingRequestCount: Math.max(
          0,
          state.pendingRequestCount - joinRequestDismissals,
        ),
      };
    }),

  setLlmEnabled: (enabled) => set({ llmEnabled: enabled }),
  setShowAiPanel: (show) => set({ showAiPanel: show }),
  toggleAiPanel: () => set((s) => ({ showAiPanel: !s.showAiPanel })),
}));
