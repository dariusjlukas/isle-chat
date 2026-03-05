import { useChatStore } from '../../stores/chatStore';

interface Props {
  channelId: string;
}

const EMPTY_TYPING: string[] = [];

export function TypingIndicator({ channelId }: Props) {
  const storeTyping = useChatStore((s) => s.typingUsers[channelId]);
  const typingUsers = storeTyping ?? EMPTY_TYPING;
  const currentUser = useChatStore((s) => s.user);

  const others = typingUsers.filter((u) => u !== currentUser?.username);
  if (others.length === 0) return null;

  const text =
    others.length === 1
      ? `${others[0]} is typing...`
      : others.length === 2
        ? `${others[0]} and ${others[1]} are typing...`
        : `${others[0]} and ${others.length - 1} others are typing...`;

  return (
    <div className="px-4 py-1 text-xs text-default-400 italic">{text}</div>
  );
}
