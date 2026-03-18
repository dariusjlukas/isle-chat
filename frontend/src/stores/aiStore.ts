import { create } from 'zustand';
import type { AiConversation, AiMessage, AiToolUse } from '../types';

interface AiState {
  conversations: AiConversation[];
  activeConversationId: string | null;
  messages: Record<string, AiMessage[]>;
  streamingContent: string;
  streamingConversationId: string | null;
  isStreaming: boolean;
  streamError: string | null;
  toolUses: AiToolUse[];
  agentEnabled: boolean;
  enabledToolCategories: Record<string, boolean>;

  setConversations: (conversations: AiConversation[]) => void;
  addConversation: (conversation: AiConversation) => void;
  removeConversation: (id: string) => void;
  updateConversationTitle: (id: string, title: string) => void;
  setActiveConversation: (id: string | null) => void;
  setMessages: (conversationId: string, messages: AiMessage[]) => void;
  addMessage: (message: AiMessage) => void;
  appendStreamDelta: (conversationId: string, content: string) => void;
  addToolUse: (toolUse: AiToolUse) => void;
  finalizeStream: (conversationId: string, messageId: string) => void;
  startStream: (conversationId: string) => void;
  stopStream: () => void;
  setStreamError: (error: string) => void;
  clearToolUses: () => void;
  setAgentEnabled: (enabled: boolean) => void;
  setToolCategory: (category: string, enabled: boolean) => void;
  setPreferences: (prefs: Record<string, string>) => void;
}

export const useAiStore = create<AiState>((set) => ({
  conversations: [],
  activeConversationId: null,
  messages: {},
  streamingContent: '',
  streamingConversationId: null,
  isStreaming: false,
  streamError: null,
  toolUses: [],
  agentEnabled: true,
  enabledToolCategories: {
    search: true,
    messaging_read: true,
    messaging_write: true,
    tasks_read: true,
    tasks_write: true,
    calendar_read: true,
    calendar_write: true,
    wiki_read: true,
    wiki_write: true,
    files_read: true,
  },

  setConversations: (conversations) => set({ conversations }),
  addConversation: (conversation) =>
    set((s) => ({
      conversations: [conversation, ...s.conversations],
    })),
  removeConversation: (id) =>
    set((s) => ({
      conversations: s.conversations.filter((c) => c.id !== id),
      activeConversationId:
        s.activeConversationId === id ? null : s.activeConversationId,
      messages: Object.fromEntries(
        Object.entries(s.messages).filter(([key]) => key !== id),
      ),
    })),
  updateConversationTitle: (id, title) =>
    set((s) => ({
      conversations: s.conversations.map((c) =>
        c.id === id ? { ...c, title } : c,
      ),
    })),
  setActiveConversation: (id) => set({ activeConversationId: id }),
  setMessages: (conversationId, messages) =>
    set((s) => ({
      messages: { ...s.messages, [conversationId]: messages },
    })),
  addMessage: (message) =>
    set((s) => ({
      messages: {
        ...s.messages,
        [message.conversation_id]: [
          ...(s.messages[message.conversation_id] || []),
          message,
        ],
      },
    })),
  appendStreamDelta: (conversationId, content) =>
    set((s) => {
      if (s.streamingConversationId !== conversationId) return s;
      return { streamingContent: s.streamingContent + content };
    }),
  addToolUse: (toolUse) => set((s) => ({ toolUses: [...s.toolUses, toolUse] })),
  finalizeStream: (conversationId, messageId) =>
    set((s) => {
      const finalMessage: AiMessage = {
        id: messageId,
        conversation_id: conversationId,
        role: 'assistant',
        content: s.streamingContent,
        created_at: new Date().toISOString(),
      };
      return {
        isStreaming: false,
        streamingContent: '',
        streamingConversationId: null,
        toolUses: [],
        messages: {
          ...s.messages,
          [conversationId]: [
            ...(s.messages[conversationId] || []),
            finalMessage,
          ],
        },
      };
    }),
  startStream: (conversationId) =>
    set({
      isStreaming: true,
      streamingContent: '',
      streamingConversationId: conversationId,
      streamError: null,
      toolUses: [],
    }),
  stopStream: () =>
    set({
      isStreaming: false,
      streamingContent: '',
      streamingConversationId: null,
    }),
  setStreamError: (error) =>
    set({
      isStreaming: false,
      streamingContent: '',
      streamingConversationId: null,
      streamError: error,
    }),
  clearToolUses: () => set({ toolUses: [] }),
  setAgentEnabled: (enabled) => set({ agentEnabled: enabled }),
  setToolCategory: (category, enabled) =>
    set((s) => ({
      enabledToolCategories: {
        ...s.enabledToolCategories,
        [category]: enabled,
      },
    })),
  setPreferences: (prefs) =>
    set({
      agentEnabled: prefs.agent_enabled !== 'false',
      enabledToolCategories: {
        search: prefs.agent_tools_search !== 'false',
        messaging_read: prefs.agent_tools_messaging_read !== 'false',
        messaging_write: prefs.agent_tools_messaging_write !== 'false',
        tasks_read: prefs.agent_tools_tasks_read !== 'false',
        tasks_write: prefs.agent_tools_tasks_write !== 'false',
        calendar_read: prefs.agent_tools_calendar_read !== 'false',
        calendar_write: prefs.agent_tools_calendar_write !== 'false',
        wiki_read: prefs.agent_tools_wiki_read !== 'false',
        wiki_write: prefs.agent_tools_wiki_write !== 'false',
        files_read: prefs.agent_tools_files_read !== 'false',
      },
    }),
}));
