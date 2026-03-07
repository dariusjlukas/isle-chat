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
} from '../types';

interface ChatState {
  // Auth
  user: User | null;
  token: string | null;
  isAuthenticated: boolean;

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

  // Server state
  serverArchived: boolean;
  pendingRequestCount: number;
  spaceInvites: SpaceInvite[];

  // Search
  jumpToMessageId: string | null;
  jumpToChannelId: string | null;

  // Unread / Read receipts
  unreadCounts: Record<string, number>;
  mentionCounts: Record<string, number>;
  readReceipts: Record<string, Record<string, ReadReceiptInfo>>;

  // Actions
  setAuth: (user: User, token: string) => void;
  clearAuth: () => void;
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
  setPendingRequestCount: (count: number) => void;
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
}

export const useChatStore = create<ChatState>((set) => ({
  user: null,
  token: null,
  isAuthenticated: false,
  channels: [],
  activeChannelId: null,
  messages: {},
  users: [],
  typingUsers: {},
  uploadProgress: null,
  spaces: [],
  activeView: null,
  serverArchived: false,
  pendingRequestCount: 0,
  spaceInvites: [],
  jumpToMessageId: null,
  jumpToChannelId: null,
  unreadCounts: {},
  mentionCounts: {},
  readReceipts: {},

  setAuth: (user, token) => {
    localStorage.setItem('session_token', token);
    set({ user, token, isAuthenticated: true });
  },

  clearAuth: () => {
    localStorage.removeItem('session_token');
    set({
      user: null,
      token: null,
      isAuthenticated: false,
      channels: [],
      messages: {},
      activeChannelId: null,
      spaces: [],
      activeView: null,
      unreadCounts: {},
      mentionCounts: {},
      readReceipts: {},
      spaceInvites: [],
    });
  },

  setChannels: (channels) => set({ channels }),

  setActiveChannel: (channelId) =>
    set((state) => ({
      activeChannelId: channelId,
      unreadCounts: channelId
        ? { ...state.unreadCounts, [channelId]: 0 }
        : state.unreadCounts,
      mentionCounts: channelId
        ? { ...state.mentionCounts, [channelId]: 0 }
        : state.mentionCounts,
    })),

  addChannel: (channel) =>
    set((state) => ({
      channels: [...state.channels, channel],
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

  setActiveView: (view) => set({ activeView: view }),

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
  setPendingRequestCount: (count) => set({ pendingRequestCount: count }),

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
}));
