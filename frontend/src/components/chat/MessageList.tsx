import { useEffect, useRef } from 'react';
import { useChatStore } from '../../stores/chatStore';
import { MessageBubble } from './MessageBubble';
import { TypingIndicator } from './TypingIndicator';
import * as api from '../../services/api';

interface Props {
  channelId: string;
  onEditMessage?: (messageId: string, content: string) => void;
  onDeleteMessage?: (messageId: string) => void;
}

const EMPTY_MESSAGES: Array<import('../../types').Message> = [];

export function MessageList({
  channelId,
  onEditMessage,
  onDeleteMessage,
}: Props) {
  const storeMessages = useChatStore((s) => s.messages[channelId]);
  const messages = storeMessages ?? EMPTY_MESSAGES;
  const setMessages = useChatStore((s) => s.setMessages);
  const bottomRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    api.getMessages(channelId).then((msgs) => {
      setMessages(channelId, msgs);
    });
  }, [channelId, setMessages]);

  useEffect(() => {
    bottomRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [messages.length]);

  return (
    <div className="flex-1 overflow-y-auto p-4">
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
    </div>
  );
}
