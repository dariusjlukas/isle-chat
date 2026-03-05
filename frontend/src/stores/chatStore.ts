import { create } from 'zustand';
import type { User, Channel, Message } from '../types';

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
  setUserOnline: (userId: string, online: boolean) => void;
  updateChannel: (updates: Partial<Channel> & { id: string }) => void;
  removeChannel: (channelId: string) => void;
  setTyping: (channelId: string, username: string) => void;
  clearTyping: (channelId: string, username: string) => void;
  setUploadProgress: (progress: number | null) => void;
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
    });
  },

  setChannels: (channels) => set({ channels }),

  setActiveChannel: (channelId) => set({ activeChannelId: channelId }),

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
      // Deduplicate by ID
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

  setUserOnline: (userId, online) =>
    set((state) => ({
      users: state.users.map((u) =>
        u.id === userId ? { ...u, is_online: online } : u,
      ),
      channels: state.channels.map((c) => ({
        ...c,
        members: c.members.map((m) =>
          m.id === userId ? { ...m, is_online: online } : m,
        ),
      })),
    })),

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
}));
