import { useEffect, useRef, useMemo } from 'react';
import { useAiStore } from '../../stores/aiStore';
import { AiMessageBubble } from './AiMessageBubble';
import { AiToolUseCard } from './AiToolUseCard';
import type { AiMessage } from '../../types';

const EMPTY_MESSAGES: AiMessage[] = [];

interface Props {
  conversationId: string;
}

export function AiMessageList({ conversationId }: Props) {
  const storeMessages = useAiStore((s) => s.messages[conversationId]);
  const messages = storeMessages ?? EMPTY_MESSAGES;
  const isStreaming = useAiStore((s) => s.isStreaming);
  const streamingConversationId = useAiStore((s) => s.streamingConversationId);
  const streamingContent = useAiStore((s) => s.streamingContent);
  const streamError = useAiStore((s) => s.streamError);

  const bottomRef = useRef<HTMLDivElement>(null);
  const containerRef = useRef<HTMLDivElement>(null);

  const isStreamingHere =
    isStreaming && streamingConversationId === conversationId;

  const streamingMessage: AiMessage | null = useMemo(() => {
    if (!isStreamingHere || !streamingContent) return null;
    return {
      id: '__streaming__',
      conversation_id: conversationId,
      role: 'assistant',
      content: streamingContent,
      created_at: new Date().toISOString(),
    };
  }, [isStreamingHere, streamingContent, conversationId]);

  // Auto-scroll to bottom when new messages arrive or streaming content changes
  useEffect(() => {
    const el = containerRef.current;
    if (!el) return;
    const threshold = 150;
    const atBottom =
      el.scrollHeight - el.scrollTop - el.clientHeight < threshold;
    if (atBottom) {
      bottomRef.current?.scrollIntoView({ behavior: 'smooth' });
    }
  }, [messages.length, streamingContent]);

  // Scroll to bottom on initial load
  useEffect(() => {
    requestAnimationFrame(() => {
      bottomRef.current?.scrollIntoView({ behavior: 'instant' });
    });
  }, [conversationId]);

  return (
    <div ref={containerRef} className='flex-1 overflow-y-auto p-4'>
      {messages.length === 0 && !isStreamingHere && (
        <div className='flex items-center justify-center h-full text-default-400 text-sm'>
          Send a message to start the conversation.
        </div>
      )}
      {messages.map((msg) => {
        if (msg.role === 'tool') {
          // Try to parse structured content (from WS events)
          let toolUse: {
            tool_name: string;
            arguments: Record<string, unknown>;
            result: unknown;
            status: 'success' | 'error';
          };
          try {
            const parsed = JSON.parse(msg.content);
            toolUse = {
              tool_name: msg.tool_name || 'unknown',
              arguments: parsed.arguments ?? {},
              result: parsed.result ?? msg.content,
              status: parsed.status ?? 'success',
            };
          } catch {
            // Fallback for plain-text tool results (loaded from DB)
            toolUse = {
              tool_name: msg.tool_name || 'unknown',
              arguments: {},
              result: msg.content,
              status: 'success',
            };
          }
          return <AiToolUseCard key={msg.id} toolUse={toolUse} />;
        }
        return <AiMessageBubble key={msg.id} message={msg} />;
      })}

      {/* Show streaming assistant message */}
      {streamingMessage && (
        <AiMessageBubble message={streamingMessage} isStreaming />
      )}

      {/* Show streaming indicator when waiting for first content */}
      {isStreamingHere && !streamingContent && (
        <div className='flex justify-start mb-3'>
          <div className='bg-content2 rounded-2xl rounded-bl-md px-4 py-3'>
            <div className='flex gap-1.5'>
              <span className='w-2 h-2 rounded-full bg-default-400 animate-bounce [animation-delay:0ms]' />
              <span className='w-2 h-2 rounded-full bg-default-400 animate-bounce [animation-delay:150ms]' />
              <span className='w-2 h-2 rounded-full bg-default-400 animate-bounce [animation-delay:300ms]' />
            </div>
          </div>
        </div>
      )}

      {/* Show error */}
      {streamError && (
        <div className='flex justify-start mb-3'>
          <div className='bg-danger/10 border border-danger/30 text-danger rounded-xl px-4 py-2 text-sm max-w-[85%]'>
            {streamError}
          </div>
        </div>
      )}

      <div ref={bottomRef} />
    </div>
  );
}
