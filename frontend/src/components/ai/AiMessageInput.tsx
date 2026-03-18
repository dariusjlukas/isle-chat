import { useState, useRef, useEffect, useCallback } from 'react';
import { Button, Textarea } from '@heroui/react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faPaperPlane, faStop } from '@fortawesome/free-solid-svg-icons';
import { useAiStore } from '../../stores/aiStore';
import { useChatStore } from '../../stores/chatStore';
import * as api from '../../services/api';

interface Props {
  conversationId: string;
}

export function AiMessageInput({ conversationId }: Props) {
  const [content, setContent] = useState('');
  const textareaRef = useRef<HTMLTextAreaElement>(null);

  const isStreaming = useAiStore((s) => s.isStreaming);
  const streamingConversationId = useAiStore((s) => s.streamingConversationId);
  const addMessage = useAiStore((s) => s.addMessage);
  const startStream = useAiStore((s) => s.startStream);
  const stopStream = useAiStore((s) => s.stopStream);

  const activeView = useChatStore((s) => s.activeView);
  const activeChannelId = useChatStore((s) => s.activeChannelId);

  const isStreamingHere =
    isStreaming && streamingConversationId === conversationId;

  const currentSpaceId =
    activeView?.type === 'space' ? activeView.spaceId : undefined;

  const handleSend = useCallback(async () => {
    const trimmed = content.trim();
    if (!trimmed || isStreamingHere) return;

    // Optimistically add user message
    const userMessage = {
      id: `temp-${Date.now()}`,
      conversation_id: conversationId,
      role: 'user' as const,
      content: trimmed,
      created_at: new Date().toISOString(),
    };
    addMessage(userMessage);
    setContent('');

    // Start streaming state
    startStream(conversationId);

    try {
      await api.sendAiMessage(
        conversationId,
        trimmed,
        currentSpaceId,
        activeChannelId ?? undefined,
      );
    } catch (err) {
      console.error('Failed to send AI message:', err);
      stopStream();
    }
  }, [
    content,
    conversationId,
    isStreamingHere,
    addMessage,
    startStream,
    stopStream,
    currentSpaceId,
    activeChannelId,
  ]);

  const handleStop = useCallback(async () => {
    try {
      await api.stopAiGeneration(conversationId);
    } catch (err) {
      console.error('Failed to stop generation:', err);
    }
    stopStream();
  }, [conversationId, stopStream]);

  const handleKeyDown = useCallback(
    (e: React.KeyboardEvent) => {
      if (e.key === 'Enter' && !e.shiftKey) {
        e.preventDefault();
        handleSend();
      }
    },
    [handleSend],
  );

  // Focus textarea when conversation changes
  useEffect(() => {
    textareaRef.current?.focus();
  }, [conversationId]);

  return (
    <div className='border-t border-default-100 p-3 sm:p-4'>
      <div className='flex gap-2 items-end'>
        <Textarea
          ref={textareaRef}
          value={content}
          onChange={(e) => setContent(e.target.value)}
          onKeyDown={handleKeyDown}
          placeholder='Message AI assistant...'
          minRows={1}
          maxRows={6}
          variant='bordered'
          isDisabled={isStreamingHere}
          classNames={{
            input: 'text-sm',
            inputWrapper: 'bg-transparent',
          }}
          className='flex-1'
        />
        {isStreamingHere ? (
          <Button
            isIconOnly
            color='danger'
            variant='flat'
            onPress={handleStop}
            className='mb-1'
          >
            <FontAwesomeIcon icon={faStop} />
          </Button>
        ) : (
          <Button
            isIconOnly
            color='primary'
            isDisabled={!content.trim()}
            onPress={handleSend}
            className='mb-1'
          >
            <FontAwesomeIcon icon={faPaperPlane} />
          </Button>
        )}
      </div>
    </div>
  );
}
