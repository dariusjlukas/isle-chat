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
  onAddReaction?: (messageId: string, emoji: string) => void;
  onRemoveReaction?: (messageId: string, emoji: string) => void;
  onReply?: (message: import('../../types').Message) => void;
}

const EMPTY_MESSAGES: Array<import('../../types').Message> = [];

export function MessageList({
  channelId,
  onEditMessage,
  onDeleteMessage,
  onMarkRead,
  onAddReaction,
  onRemoveReaction,
  onReply,
}: Props) {
  const storeMessages = useChatStore((s) => s.messages[channelId]);
  const messages = storeMessages ?? EMPTY_MESSAGES;
  const setMessages = useChatStore((s) => s.setMessages);
  const setReadReceipts = useChatStore((s) => s.setReadReceipts);
  const jumpToMessageId = useChatStore((s) => s.jumpToMessageId);
  const jumpToChannelId = useChatStore((s) => s.jumpToChannelId);
  const clearJumpToMessage = useChatStore((s) => s.clearJumpToMessage);
  const currentUserId = useChatStore((s) => s.user?.id);
  const bottomRef = useRef<HTMLDivElement>(null);
  const containerRef = useRef<HTMLDivElement>(null);
  const markReadTimer = useRef<ReturnType<typeof setTimeout>>(undefined);
  const hasScrolledToUnreadRef = useRef<string | null>(null);
  const separatorDismissTimer = useRef<ReturnType<typeof setTimeout>>(
    undefined,
  );
  const [isViewingAround, setIsViewingAround] = useState(false);
  const [isScrolledUp, setIsScrolledUp] = useState(false);
  const [initialLastReadId, setInitialLastReadId] = useState<string | null>(
    null,
  );
  const [separatorFading, setSeparatorFading] = useState(false);

  // Reset scroll state when switching channels
  useEffect(() => {
    hasScrolledToUnreadRef.current = null;
    clearTimeout(separatorDismissTimer.current);
    separatorDismissTimer.current = undefined;
    requestAnimationFrame(() => {
      setIsScrolledUp(false);
      setIsViewingAround(false);
      setInitialLastReadId(null);
      setSeparatorFading(false);
    });
  }, [channelId]);

  // Load messages normally (skip if jump is pending for this channel)
  useEffect(() => {
    const { jumpToChannelId: jCh, jumpToMessageId: jMsg } =
      useChatStore.getState();
    if (jCh === channelId && jMsg) return;

    const messagesPromise = api.getMessages(channelId);
    const receiptsPromise = api.getReadReceipts(channelId).catch(() => []);

    Promise.all([messagesPromise, receiptsPromise]).then(
      ([msgs, receipts]) => {
        // Build read receipts map
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

        // Capture the current user's last read message ID at channel-open time
        const myReceipt = currentUserId ? map[currentUserId] : undefined;
        const lastReadId = myReceipt?.last_read_message_id ?? null;

        // Only set initialLastReadRef if this is a fresh channel open
        if (hasScrolledToUnreadRef.current !== channelId) {
          // If user has read ALL messages (or no receipt), don't show separator
          const lastMsg = msgs.length > 0 ? msgs[msgs.length - 1] : null;
          if (lastReadId && lastReadId !== lastMsg?.id) {
            setInitialLastReadId(lastReadId);
          } else {
            setInitialLastReadId(null);
          }
        }

        setMessages(channelId, msgs);
        setIsViewingAround(false);

        // Scroll to first unread message on initial channel open
        if (hasScrolledToUnreadRef.current !== channelId) {
          hasScrolledToUnreadRef.current = channelId;
          if (lastReadId) {
            const unreadIdx = msgs.findIndex((m) => m.id === lastReadId);
            if (unreadIdx >= 0 && unreadIdx < msgs.length - 1) {
              const firstUnreadId = msgs[unreadIdx + 1].id;
              requestAnimationFrame(() => {
                const el = document.getElementById(`msg-${firstUnreadId}`);
                if (el) {
                  el.scrollIntoView({ behavior: 'instant', block: 'start' });
                }
              });
              return;
            }
          }
          // All read or no receipt — scroll to bottom
          requestAnimationFrame(() => {
            bottomRef.current?.scrollIntoView({ behavior: 'instant' });
          });
        }
      },
    );
  }, [channelId, setMessages, setReadReceipts, currentUserId]);

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

    clearTimeout(markReadTimer.current);
    markReadTimer.current = setTimeout(() => {
      onMarkRead(channelId, lastMessage.id, lastMessage.created_at);
    }, 500);

    return () => clearTimeout(markReadTimer.current);
  }, [channelId, lastMessage, onMarkRead]);

  useEffect(() => {
    if (!isViewingAround) {
      requestAnimationFrame(() => {
        const el = containerRef.current;
        if (!el) return;
        const threshold = 100;
        const atBottom =
          el.scrollHeight - el.scrollTop - el.clientHeight < threshold;
        if (atBottom) {
          bottomRef.current?.scrollIntoView({ behavior: 'smooth' });
        } else {
          setIsScrolledUp(true);
        }
      });
    }
  }, [messages.length, isViewingAround]);

  const handleScroll = useCallback(() => {
    const el = containerRef.current;
    if (!el) return;
    const threshold = 100;
    const atBottom =
      el.scrollHeight - el.scrollTop - el.clientHeight < threshold;
    setIsScrolledUp(!atBottom);

    // When user reaches bottom with separator visible, start fade after 5s
    if (atBottom && initialLastReadId && !separatorFading) {
      if (!separatorDismissTimer.current) {
        separatorDismissTimer.current = setTimeout(() => {
          setSeparatorFading(true);
          separatorDismissTimer.current = undefined;
        }, 5000);
      }
    } else if (!atBottom && !separatorFading) {
      // Scrolled away from bottom — cancel pending dismiss
      clearTimeout(separatorDismissTimer.current);
      separatorDismissTimer.current = undefined;
    }
  }, [initialLastReadId, separatorFading]);

  const handleJumpToLatest = useCallback(() => {
    if (isViewingAround) {
      api.getMessages(channelId).then((msgs) => {
        setMessages(channelId, msgs);
        setIsViewingAround(false);
        requestAnimationFrame(() => {
          bottomRef.current?.scrollIntoView({ behavior: 'smooth' });
        });
      });
    } else {
      bottomRef.current?.scrollIntoView({ behavior: 'smooth' });
    }
  }, [channelId, setMessages, isViewingAround]);

  return (
    <div
      ref={containerRef}
      className='flex-1 overflow-y-auto p-4 relative'
      onScroll={handleScroll}
    >
      {messages.length === 0 && (
        <div className='flex items-center justify-center h-full text-default-400'>
          No messages yet. Start the conversation!
        </div>
      )}
      {messages.map((msg, idx) => {
        const showSeparator =
          initialLastReadId &&
          idx > 0 &&
          messages[idx - 1].id === initialLastReadId;

        return (
          <div key={msg.id} className={showSeparator ? 'relative' : undefined}>
            {showSeparator && (
              <div
                className={`absolute left-0 right-0 top-0 -translate-y-3/4 z-10 flex items-center gap-2 pointer-events-none transition-opacity duration-500 ${separatorFading ? 'opacity-0' : 'opacity-100'}`}
                onTransitionEnd={() => {
                  if (separatorFading) {
                    setInitialLastReadId(null);
                    setSeparatorFading(false);
                  }
                }}
              >
                <div className='flex-1 border-t border-danger' />
                <span className='text-xs text-danger font-medium whitespace-nowrap'>
                  New messages
                </span>
                <div className='flex-1 border-t border-danger' />
              </div>
            )}
            <MessageBubble
              message={msg}
              onEdit={onEditMessage}
              onDelete={onDeleteMessage}
              onAddReaction={onAddReaction}
              onRemoveReaction={onRemoveReaction}
              onReply={onReply}
            />
          </div>
        );
      })}
      <TypingIndicator channelId={channelId} />
      <div ref={bottomRef} />

      {isScrolledUp && (
        <div
          className='sticky bottom-4 flex justify-center'
          onMouseDown={(e) => e.stopPropagation()}
        >
          <Button
            size='sm'
            color='primary'
            variant='shadow'
            onPress={handleJumpToLatest}
            startContent={
              <FontAwesomeIcon icon={faArrowDown} className='text-xs' />
            }
          >
            Jump to latest
          </Button>
        </div>
      )}
    </div>
  );
}
