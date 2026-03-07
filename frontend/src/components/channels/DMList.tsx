import { useMemo } from 'react';
import { Button } from '@heroui/react';
import { useChatStore } from '../../stores/chatStore';

interface Props {
  onStartDM: () => void;
  onSelect?: () => void;
}

export function DMList({ onStartDM, onSelect }: Props) {
  const allChannels = useChatStore((s) => s.channels);
  const activeChannelId = useChatStore((s) => s.activeChannelId);
  const setActiveChannel = useChatStore((s) => s.setActiveChannel);
  const currentUser = useChatStore((s) => s.user);

  const channels = useMemo(
    () => allChannels.filter((c) => c.is_direct),
    [allChannels],
  );

  const isSelfDM = (channel: (typeof channels)[0]) =>
    channel.members?.length === 1 && channel.members[0].id === currentUser?.id;

  const getDMName = (channel: (typeof channels)[0]) => {
    if (isSelfDM(channel)) return `${currentUser?.display_name || 'You'} (you)`;
    const other = channel.members?.find((m) => m.id !== currentUser?.id);
    return other?.display_name || other?.username || 'Unknown';
  };

  const getDMOnline = (channel: (typeof channels)[0]) => {
    if (isSelfDM(channel)) return true;
    const other = channel.members?.find((m) => m.id !== currentUser?.id);
    return other?.is_online || false;
  };

  return (
    <div className='mb-4'>
      <div className='flex items-center justify-between px-3 py-2'>
        <h3 className='text-xs font-semibold text-default-500 uppercase tracking-wider'>
          Direct Messages
        </h3>
        <Button
          isIconOnly
          variant='light'
          size='sm'
          onPress={onStartDM}
          title='New direct message'
        >
          +
        </Button>
      </div>
      {channels.map((ch) => (
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
          <span
            className={`w-2 h-2 rounded-full flex-shrink-0 ${
              getDMOnline(ch) ? 'bg-success' : 'bg-default-600'
            }`}
          />
          {getDMName(ch)}
        </button>
      ))}
    </div>
  );
}
