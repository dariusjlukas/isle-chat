import { useEffect, useRef, useCallback } from 'react';
import { wsService } from '../services/websocket';
import { useChatStore } from '../stores/chatStore';
import { useAiStore } from '../stores/aiStore';
import * as api from '../services/api';
import type {
  Message,
  User,
  Channel,
  ChannelRole,
  SpaceRole,
  Space,
  SpaceInvite,
  ChannelMemberInfo,
  Notification,
  AiToolUse,
} from '../types';

/**
 * Manages the WebSocket connection lifecycle and event subscriptions.
 * Must be called from a component that stays mounted for the entire
 * authenticated session (e.g. App), NOT from a conditionally-rendered
 * component like ChatArea.
 */
export function useWebSocketConnection() {
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
        const { message, mentions } = data as {
          message: Message;
          mentions?: string[];
        };
        const store = useChatStore.getState();
        store.addMessage(message);
        store.clearTyping(message.channel_id, message.username);

        // Increment unread if not own message and not the active channel
        if (
          message.user_id !== store.user?.id &&
          message.channel_id !== store.activeChannelId
        ) {
          store.incrementUnread(message.channel_id);
          // Increment mention count if user is mentioned or @channel was used
          if (
            mentions &&
            (mentions.includes(store.user?.username || '') ||
              mentions.includes('@channel'))
          ) {
            store.incrementMentionCount(message.channel_id);
          }
        }
      }),

      wsService.on('message_edited', (data: unknown) => {
        const { message } = data as { message: Message };
        useChatStore.getState().updateMessage(message);
      }),

      wsService.on('message_deleted', (data: unknown) => {
        const { message } = data as { message: Message };
        useChatStore.getState().updateMessage(message);
      }),

      wsService.on('reaction_added', (data: unknown) => {
        const { message_id, channel_id, emoji, user_id, username } = data as {
          message_id: string;
          channel_id: string;
          emoji: string;
          user_id: string;
          username: string;
        };
        useChatStore
          .getState()
          .addReaction(channel_id, message_id, { emoji, user_id, username });
      }),

      wsService.on('reaction_removed', (data: unknown) => {
        const { message_id, channel_id, emoji, user_id } = data as {
          message_id: string;
          channel_id: string;
          emoji: string;
          user_id: string;
        };
        useChatStore
          .getState()
          .removeReaction(channel_id, message_id, emoji, user_id);
      }),

      wsService.on('channel_added', (data: unknown) => {
        const { channel } = data as { channel: Channel };
        useChatStore.getState().addChannel(channel);
      }),

      wsService.on('user_online', (data: unknown) => {
        const { user_id } = data as { user_id: string };
        const store = useChatStore.getState();
        store.setUserOnline(user_id, true);
        // If this user isn't in the store yet (e.g. just joined), fetch the user list
        if (!store.users.some((u) => u.id === user_id)) {
          api
            .listUsers()
            .then((users) => useChatStore.getState().setUsers(users));
        }
      }),

      wsService.on('user_offline', (data: unknown) => {
        const { user_id, last_seen } = data as {
          user_id: string;
          last_seen?: string;
        };
        useChatStore.getState().setUserOnline(user_id, false, last_seen);
      }),

      wsService.on('user_updated', (data: unknown) => {
        const { user } = data as { user: User };
        useChatStore.getState().updateUserInList(user.id, user);
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

      wsService.on('refresh_spaces', () => {
        api.listSpaces().then((spaces) => {
          useChatStore.getState().setSpaces(spaces);
        });
      }),

      wsService.on('space_added', (data: unknown) => {
        const { space } = data as { space: Space };
        useChatStore.getState().addSpace(space);
      }),

      wsService.on('space_updated', (data: unknown) => {
        const { space } = data as { space: Partial<Space> & { id: string } };
        useChatStore.getState().updateSpace(space);
      }),

      wsService.on('space_role_changed', (data: unknown) => {
        const { space_id, role } = data as {
          space_id: string;
          role: SpaceRole;
        };
        useChatStore.getState().updateSpace({ id: space_id, my_role: role });
      }),

      wsService.on('server_role_changed', (data: unknown) => {
        const { role } = data as { role: string };
        useChatStore
          .getState()
          .updateUser({ role: role as 'owner' | 'admin' | 'user' });
      }),

      wsService.on('banned', () => {
        useChatStore.getState().clearAuth('Your account has been banned');
      }),

      wsService.on('member_role_changed', (data: unknown) => {
        const { channel_id, user_id, role } = data as {
          channel_id: string;
          user_id: string;
          role: ChannelRole;
        };
        const store = useChatStore.getState();
        const ch = store.channels.find((c) => c.id === channel_id);
        if (ch) {
          store.updateChannel({
            id: channel_id,
            members: ch.members.map((m) =>
              m.id === user_id ? { ...m, role } : m,
            ),
          });
        }
      }),

      wsService.on('space_member_role_changed', (data: unknown) => {
        const { space_id, user_id, role } = data as {
          space_id: string;
          user_id: string;
          role: SpaceRole;
        };
        const store = useChatStore.getState();
        const sp = store.spaces.find((s) => s.id === space_id);
        if (sp) {
          store.updateSpace({
            id: space_id,
            members: sp.members.map((m) =>
              m.id === user_id ? { ...m, role } : m,
            ),
          });
        }
      }),

      wsService.on('user_role_changed', (data: unknown) => {
        const { user_id, role } = data as {
          user_id: string;
          role: string;
        };
        const store = useChatStore.getState();
        store.setUsers(
          store.users.map((u) =>
            u.id === user_id
              ? { ...u, role: role as 'owner' | 'admin' | 'user' }
              : u,
          ),
        );
      }),

      wsService.on('server_archived_changed', (data: unknown) => {
        const { archived } = data as { archived: boolean };
        useChatStore.getState().setServerArchived(archived);
      }),

      wsService.on('server_lockdown_changed', (data: unknown) => {
        const { locked_down } = data as { locked_down: boolean };
        useChatStore.getState().setServerLockedDown(locked_down);
      }),

      wsService.on('server_locked_down', () => {
        useChatStore
          .getState()
          .clearAuth(
            'Server is in lockdown mode. Only administrators may log in.',
          );
      }),

      wsService.on('space_removed', (data: unknown) => {
        const { space_id } = data as { space_id: string };
        useChatStore.getState().removeSpace(space_id);
      }),

      wsService.on('member_left', (data: unknown) => {
        const { channel_id, user_id } = data as {
          channel_id: string;
          user_id: string;
        };
        const store = useChatStore.getState();
        const ch = store.channels.find((c) => c.id === channel_id);
        if (ch) {
          store.updateChannel({
            id: channel_id,
            members: ch.members.filter((m) => m.id !== user_id),
          });
        }
      }),

      wsService.on('space_member_left', (data: unknown) => {
        const { space_id, user_id } = data as {
          space_id: string;
          user_id: string;
        };
        const store = useChatStore.getState();
        const sp = store.spaces.find((s) => s.id === space_id);
        if (sp) {
          store.updateSpace({
            id: space_id,
            members: sp.members.filter((m) => m.id !== user_id),
          });
        }
      }),

      wsService.on('join_request_created', () => {
        const store = useChatStore.getState();
        store.setPendingRequestCount(store.pendingRequestCount + 1);
      }),

      wsService.on('space_invite', (data: unknown) => {
        const { invite } = data as { invite: SpaceInvite };
        useChatStore.getState().addSpaceInvite(invite);
      }),

      wsService.on('conversation_member_added', (data: unknown) => {
        const { channel_id, members } = data as {
          channel_id: string;
          members: ChannelMemberInfo[];
        };
        useChatStore.getState().updateChannel({ id: channel_id, members });
      }),

      wsService.on('conversation_renamed', (data: unknown) => {
        const { channel_id, name } = data as {
          channel_id: string;
          name: string;
        };
        useChatStore
          .getState()
          .updateChannel({ id: channel_id, conversation_name: name });
      }),

      wsService.on('new_notification', (data: unknown) => {
        const { notification } = data as { notification: Notification };
        const state = useChatStore.getState();
        state.addNotification(notification);
        // If the user is viewing the channel this notification is for,
        // immediately mark it as read on the server too
        if (
          notification.channel_id &&
          notification.channel_id === state.activeChannelId
        ) {
          api.markNotificationRead(notification.id).catch(() => {});
        }
      }),

      wsService.on('notification_count', (data: unknown) => {
        const { unread_count } = data as { unread_count: number };
        useChatStore.getState().setUnreadNotificationCount(unread_count);
      }),

      wsService.on('unread_counts', (data: unknown) => {
        const { counts, mention_counts } = data as {
          counts: Record<string, number>;
          mention_counts: Record<string, number>;
        };
        useChatStore.getState().setUnreadCounts(counts);
        useChatStore.getState().setMentionCounts(mention_counts);
      }),

      wsService.on('read_receipt', (data: unknown) => {
        const {
          channel_id,
          user_id,
          username,
          last_read_message_id,
          last_read_at,
        } = data as {
          channel_id: string;
          user_id: string;
          username: string;
          last_read_message_id: string;
          last_read_at: string;
        };
        useChatStore.getState().updateReadReceipt(channel_id, user_id, {
          username,
          last_read_message_id,
          last_read_at,
        });
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

      wsService.on('ai_stream_delta', (data: unknown) => {
        const d = data as {
          conversation_id: string;
          content: string;
        };
        useAiStore.getState().appendStreamDelta(d.conversation_id, d.content);
      }),
      wsService.on('ai_tool_use', (data: unknown) => {
        const d = data as AiToolUse & { conversation_id: string };
        const store = useAiStore.getState();
        store.addToolUse({
          tool_name: d.tool_name,
          arguments: d.arguments,
          result: d.result,
          status: d.status,
        });
        // Also persist as a message so it survives stream finalization
        store.addMessage({
          id: `tool-${Date.now()}-${Math.random()}`,
          conversation_id: d.conversation_id,
          role: 'tool',
          tool_name: d.tool_name,
          content: JSON.stringify({
            arguments: d.arguments,
            result: d.result,
            status: d.status,
          }),
          created_at: new Date().toISOString(),
        });
      }),
      wsService.on('ai_stream_end', (data: unknown) => {
        const d = data as {
          conversation_id: string;
          message_id: string;
        };
        useAiStore.getState().finalizeStream(d.conversation_id, d.message_id);
      }),
      wsService.on('ai_stream_error', (data: unknown) => {
        const d = data as { error?: string };
        useAiStore.getState().setStreamError(d.error || 'An error occurred');
      }),
      wsService.on('ai_conversation_titled', (data: unknown) => {
        const d = data as {
          conversation_id: string;
          title: string;
        };
        useAiStore
          .getState()
          .updateConversationTitle(d.conversation_id, d.title);
      }),
    ];

    return () => {
      unsubs.forEach((unsub) => unsub());
      wsService.disconnect();
      timers.forEach((t) => clearTimeout(t));
      timers.clear();
    };
  }, [token]);
}

/**
 * Returns WebSocket action functions (send message, typing, etc.).
 * These are thin wrappers around wsService.send() and do not manage
 * the connection lifecycle — safe to call from any component.
 */
export function useWebSocketActions() {
  const sendMessage = useCallback(
    (channelId: string, content: string, replyToMessageId?: string) => {
      const payload: Record<string, string> = {
        type: 'send_message',
        channel_id: channelId,
        content,
      };
      if (replyToMessageId) {
        payload.reply_to_message_id = replyToMessageId;
      }
      wsService.send(payload);
    },
    [],
  );

  const sendTyping = useCallback((channelId: string) => {
    wsService.send({ type: 'typing', channel_id: channelId });
  }, []);

  const editMessage = useCallback((messageId: string, content: string) => {
    wsService.send({ type: 'edit_message', message_id: messageId, content });
  }, []);

  const deleteMessage = useCallback((messageId: string) => {
    wsService.send({ type: 'delete_message', message_id: messageId });
  }, []);

  const markRead = useCallback(
    (channelId: string, messageId: string, timestamp: string) => {
      wsService.send({
        type: 'mark_read',
        channel_id: channelId,
        message_id: messageId,
        timestamp,
      });
      useChatStore.getState().clearUnread(channelId);
      api.markChannelNotificationsRead(channelId).catch(() => {});
    },
    [],
  );

  const addReaction = useCallback((messageId: string, emoji: string) => {
    wsService.send({ type: 'add_reaction', message_id: messageId, emoji });
  }, []);

  const removeReaction = useCallback((messageId: string, emoji: string) => {
    wsService.send({ type: 'remove_reaction', message_id: messageId, emoji });
  }, []);

  return {
    sendMessage,
    sendTyping,
    editMessage,
    deleteMessage,
    markRead,
    addReaction,
    removeReaction,
  };
}
