import { useMemo } from 'react';
import { Button } from '@heroui/react';
import { useChatStore } from '../../stores/chatStore';
import { OnlineStatusDot } from '../common/OnlineStatusDot';
import { UserPopoverCard } from '../common/UserPopoverCard';

interface Props {
  onCreateConversation: () => void;
  onSelect?: () => void;
}

export function ConversationList({ onCreateConversation, onSelect }: Props) {
  const allChannels = useChatStore((s) => s.channels);
  const activeChannelId = useChatStore((s) => s.activeChannelId);
  const setActiveChannel = useChatStore((s) => s.setActiveChannel);
  const currentUser = useChatStore((s) => s.user);
  const unreadCounts = useChatStore((s) => s.unreadCounts);

  const conversations = useMemo(
    () => allChannels.filter((c) => c.is_direct),
    [allChannels],
  );

  const getConversationName = (channel: (typeof conversations)[0]) => {
    if (channel.conversation_name) return channel.conversation_name;
    if (
      channel.members?.length === 1 &&
      channel.members[0].id === currentUser?.id
    ) {
      return `${currentUser?.display_name || 'You'} (you)`;
    }
    const others =
      channel.members?.filter((m) => m.id !== currentUser?.id) || [];
    if (others.length === 0) return 'Unknown';
    if (others.length === 1)
      return others[0].display_name || others[0].username;
    return others.map((o) => o.display_name || o.username).join(', ');
  };

  const getOtherUserId = (channel: (typeof conversations)[0]) => {
    if (channel.conversation_name) return undefined;
    const others =
      channel.members?.filter((m) => m.id !== currentUser?.id) || [];
    if (others.length === 1) return others[0].id;
    return undefined;
  };

  const isGroupChat = (channel: (typeof conversations)[0]) => {
    return (channel.members?.length || 0) > 2;
  };

  const getOnlineInfo = (channel: (typeof conversations)[0]) => {
    if (
      channel.members?.length === 1 &&
      channel.members[0].id === currentUser?.id
    )
      return { isOnline: true, lastSeen: undefined };
    const others =
      channel.members?.filter((m) => m.id !== currentUser?.id) || [];
    if (others.length === 1)
      return { isOnline: others[0].is_online, lastSeen: others[0].last_seen };
    return {
      isOnline: others.some((o) => o.is_online),
      lastSeen: others
        .filter((o) => !o.is_online && o.last_seen)
        .sort(
          (a, b) =>
            new Date(b.last_seen!).getTime() - new Date(a.last_seen!).getTime(),
        )[0]?.last_seen,
    };
  };

  return (
    <div className="flex flex-col h-full">
      <div className="p-3 border-b border-default-100">
        <div className="flex items-center justify-between">
          <h3 className="text-sm font-semibold text-foreground">Messages</h3>
          <Button
            isIconOnly
            variant="light"
            size="sm"
            onPress={onCreateConversation}
            title="New conversation"
          >
            +
          </Button>
        </div>
      </div>
      <div className="flex-1 overflow-y-auto p-2">
        {conversations.map((ch) => (
          <button
            key={ch.id}
            onClick={() => {
              setActiveChannel(ch.id);
              onSelect?.();
            }}
            className={`w-full text-left px-3 py-2.5 text-sm rounded-md flex items-center gap-2 transition-colors ${
              activeChannelId === ch.id
                ? 'bg-primary/20 text-primary'
                : 'text-default-500 hover:bg-content2/50 hover:text-foreground'
            }`}
          >
            <OnlineStatusDot {...getOnlineInfo(ch)} />
            {getOtherUserId(ch) ? (
              <UserPopoverCard userId={getOtherUserId(ch)}>
                <span
                  className={`truncate cursor-pointer hover:underline ${(unreadCounts[ch.id] || 0) > 0 ? 'font-semibold text-foreground' : ''}`}
                >
                  {getConversationName(ch)}
                </span>
              </UserPopoverCard>
            ) : (
              <span
                className={`truncate ${(unreadCounts[ch.id] || 0) > 0 ? 'font-semibold text-foreground' : ''}`}
              >
                {getConversationName(ch)}
              </span>
            )}
            {isGroupChat(ch) && (
              <span className="text-xs text-default-400 flex-shrink-0">
                ({ch.members?.length})
              </span>
            )}
            {(unreadCounts[ch.id] || 0) > 0 && (
              <span className="ml-auto flex-shrink-0 min-w-[20px] h-5 rounded-full bg-danger text-white text-[11px] font-bold flex items-center justify-center px-1.5">
                {unreadCounts[ch.id] > 99 ? '99+' : unreadCounts[ch.id]}
              </span>
            )}
          </button>
        ))}
        {conversations.length === 0 && (
          <p className="text-center text-default-400 text-sm py-8">
            No conversations yet
          </p>
        )}
      </div>
    </div>
  );
}
