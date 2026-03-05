import { useEffect, useRef, useCallback } from 'react';
import { wsService } from '../services/websocket';
import { useChatStore } from '../stores/chatStore';
import type { Message, Channel, ChannelRole } from '../types';

export function useWebSocket() {
  const token = useChatStore((s) => s.token);
  const typingTimers = useRef<Map<string, ReturnType<typeof setTimeout>>>(
    new Map(),
  );

  useEffect(() => {
    if (!token) return;

    wsService.connect(token);
    const timers = typingTimers.current;

    const unsubs = [
      wsService.on('new_message', (data: unknown) => {
        const { message } = data as { message: Message };
        useChatStore.getState().addMessage(message);
        useChatStore
          .getState()
          .clearTyping(message.channel_id, message.username);
      }),

      wsService.on('message_edited', (data: unknown) => {
        const { message } = data as { message: Message };
        useChatStore.getState().updateMessage(message);
      }),

      wsService.on('message_deleted', (data: unknown) => {
        const { message } = data as { message: Message };
        useChatStore.getState().updateMessage(message);
      }),

      wsService.on('channel_added', (data: unknown) => {
        const { channel } = data as { channel: Channel };
        useChatStore.getState().addChannel(channel);
      }),

      wsService.on('user_online', (data: unknown) => {
        const { user_id } = data as { user_id: string };
        useChatStore.getState().setUserOnline(user_id, true);
      }),

      wsService.on('user_offline', (data: unknown) => {
        const { user_id } = data as { user_id: string };
        useChatStore.getState().setUserOnline(user_id, false);
      }),

      wsService.on('channel_removed', (data: unknown) => {
        const { channel_id } = data as { channel_id: string };
        useChatStore.getState().removeChannel(channel_id);
      }),

      wsService.on('role_changed', (data: unknown) => {
        const { channel_id, role } = data as {
          channel_id: string;
          role: ChannelRole;
        };
        useChatStore
          .getState()
          .updateChannel({ id: channel_id, my_role: role });
      }),

      wsService.on('channel_updated', (data: unknown) => {
        const { channel } = data as {
          channel: Partial<Channel> & { id: string };
        };
        useChatStore.getState().updateChannel(channel);
      }),

      wsService.on('typing', (data: unknown) => {
        const { channel_id, username } = data as {
          channel_id: string;
          username: string;
        };
        useChatStore.getState().setTyping(channel_id, username);
        const key = `${channel_id}:${username}`;
        const existing = timers.get(key);
        if (existing) clearTimeout(existing);
        timers.set(
          key,
          setTimeout(
            () => useChatStore.getState().clearTyping(channel_id, username),
            3000,
          ),
        );
      }),
    ];

    return () => {
      unsubs.forEach((unsub) => unsub());
      wsService.disconnect();
      timers.forEach((t) => clearTimeout(t));
      timers.clear();
    };
  }, [token]);

  const sendMessage = useCallback((channelId: string, content: string) => {
    wsService.send({ type: 'send_message', channel_id: channelId, content });
  }, []);

  const sendTyping = useCallback((channelId: string) => {
    wsService.send({ type: 'typing', channel_id: channelId });
  }, []);

  const editMessage = useCallback((messageId: string, content: string) => {
    wsService.send({ type: 'edit_message', message_id: messageId, content });
  }, []);

  const deleteMessage = useCallback((messageId: string) => {
    wsService.send({ type: 'delete_message', message_id: messageId });
  }, []);

  return { sendMessage, sendTyping, editMessage, deleteMessage };
}
