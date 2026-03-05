import { useEffect, useRef, useState, useCallback } from 'react';
import { Button } from '@heroui/react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faArrowDown } from '@fortawesome/free-solid-svg-icons';
import { useChatStore } from '../../stores/chatStore';
import { MessageBubble } from './MessageBubble';
import { TypingIndicator } from './TypingIndicator';
import * as api from '../../services/api';

interface Props {
  channelId: string;
  onEditMessage?: (messageId: string, content: string) => void;
  onDeleteMessage?: (messageId: string) => void;
  onMarkRead?: (
    channelId: string,
    messageId: string,
    timestamp: string,
  ) => void;
}

const EMPTY_MESSAGES: Array<import('../../types').Message> = [];

export function MessageList({
  channelId,
  onEditMessage,
  onDeleteMessage,
  onMarkRead,
}: Props) {
  const storeMessages = useChatStore((s) => s.messages[channelId]);
  const messages = storeMessages ?? EMPTY_MESSAGES;
  const setMessages = useChatStore((s) => s.setMessages);
  const setReadReceipts = useChatStore((s) => s.setReadReceipts);
  const jumpToMessageId = useChatStore((s) => s.jumpToMessageId);
  const jumpToChannelId = useChatStore((s) => s.jumpToChannelId);
  const clearJumpToMessage = useChatStore((s) => s.clearJumpToMessage);
  const bottomRef = useRef<HTMLDivElement>(null);
  const markReadTimer = useRef<ReturnType<typeof setTimeout>>(undefined);
  const [isViewingAround, setIsViewingAround] = useState(false);

  // Load messages normally (skip if jump is pending for this channel)
  useEffect(() => {
    if (jumpToChannelId === channelId && jumpToMessageId) return;
    api.getMessages(channelId).then((msgs) => {
      setMessages(channelId, msgs);
      setIsViewingAround(false);
    });
    api
      .getReadReceipts(channelId)
      .then((receipts) => {
        const map: Record<
          string,
          {
            username: string;
            last_read_message_id: string;
            last_read_at: string;
          }
        > = {};
        for (const r of receipts) {
          map[r.user_id] = {
            username: r.username,
            last_read_message_id: r.last_read_message_id,
            last_read_at: r.last_read_at,
          };
        }
        setReadReceipts(channelId, map);
      })
      .catch(() => {});
  }, [
    channelId,
    setMessages,
    setReadReceipts,
    jumpToChannelId,
    jumpToMessageId,
  ]);

  // Jump-to-message
  useEffect(() => {
    if (!jumpToMessageId || jumpToChannelId !== channelId) return;

    api.getMessagesAround(channelId, jumpToMessageId).then((msgs) => {
      setMessages(channelId, msgs);
      setIsViewingAround(true);
      clearJumpToMessage();

      requestAnimationFrame(() => {
        const el = document.getElementById(`msg-${jumpToMessageId}`);
        if (el) {
          el.scrollIntoView({ behavior: 'smooth', block: 'center' });
          el.classList.add('highlight-flash');
          setTimeout(() => el.classList.remove('highlight-flash'), 2000);
        }
      });
    });
  }, [
    jumpToMessageId,
    jumpToChannelId,
    channelId,
    setMessages,
    clearJumpToMessage,
  ]);

  // Mark the channel as read when messages load or new messages arrive
  const lastMessage =
    messages.length > 0 ? messages[messages.length - 1] : null;
  useEffect(() => {
    if (!onMarkRead || !lastMessage) return;
    const userId = useChatStore.getState().user?.id;
    if (lastMessage.user_id === userId) return; // skip if last message is own

    clearTimeout(markReadTimer.current);
    markReadTimer.current = setTimeout(() => {
      onMarkRead(channelId, lastMessage.id, lastMessage.created_at);
    }, 500);

    return () => clearTimeout(markReadTimer.current);
  }, [channelId, lastMessage, onMarkRead]);

  useEffect(() => {
    if (!isViewingAround) {
      bottomRef.current?.scrollIntoView({ behavior: 'smooth' });
    }
  }, [messages.length, isViewingAround]);

  const handleJumpToLatest = useCallback(() => {
    api.getMessages(channelId).then((msgs) => {
      setMessages(channelId, msgs);
      setIsViewingAround(false);
      requestAnimationFrame(() => {
        bottomRef.current?.scrollIntoView({ behavior: 'smooth' });
      });
    });
  }, [channelId, setMessages]);

  return (
    <div className="flex-1 overflow-y-auto p-4 relative">
      {messages.length === 0 && (
        <div className="flex items-center justify-center h-full text-default-400">
          No messages yet. Start the conversation!
        </div>
      )}
      {messages.map((msg) => (
        <MessageBubble
          key={msg.id}
          message={msg}
          onEdit={onEditMessage}
          onDelete={onDeleteMessage}
        />
      ))}
      <TypingIndicator channelId={channelId} />
      <div ref={bottomRef} />

      {isViewingAround && (
        <div className="sticky bottom-4 flex justify-center">
          <Button
            size="sm"
            color="primary"
            variant="shadow"
            onPress={handleJumpToLatest}
            startContent={
              <FontAwesomeIcon icon={faArrowDown} className="text-xs" />
            }
          >
            Jump to latest
          </Button>
        </div>
      )}
    </div>
  );
}
