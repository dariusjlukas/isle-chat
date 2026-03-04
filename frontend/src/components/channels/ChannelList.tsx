import { useMemo } from 'react';
import { Button } from '@heroui/react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faHashtag, faLock, faMagnifyingGlass } from '@fortawesome/free-solid-svg-icons';
import { useChatStore } from '../../stores/chatStore';

interface Props {
  onCreateChannel: () => void;
  onBrowseChannels: () => void;
  onSelect?: () => void;
}

export function ChannelList({ onCreateChannel, onBrowseChannels, onSelect }: Props) {
  const allChannels = useChatStore((s) => s.channels);
  const activeChannelId = useChatStore((s) => s.activeChannelId);
  const setActiveChannel = useChatStore((s) => s.setActiveChannel);

  const channels = useMemo(() => allChannels.filter((c) => !c.is_direct), [allChannels]);

  return (
    <div className="mb-4">
      <div className="flex items-center justify-between px-3 py-2">
        <h3 className="text-xs font-semibold text-default-500 uppercase tracking-wider">Channels</h3>
        <div className="flex gap-0.5">
          <Button isIconOnly variant="light" size="sm" onPress={onBrowseChannels} title="Browse public channels">
            <FontAwesomeIcon icon={faMagnifyingGlass} />
          </Button>
          <Button isIconOnly variant="light" size="sm" onPress={onCreateChannel} title="Create channel">
            +
          </Button>
        </div>
      </div>
      {channels.map((ch) => (
        <button
          key={ch.id}
          onClick={() => { setActiveChannel(ch.id); onSelect?.(); }}
          className={`w-full text-left px-3 py-2.5 text-sm rounded-md transition-colors ${
            activeChannelId === ch.id
              ? 'bg-primary/20 text-primary'
              : 'text-default-500 hover:bg-content2/50 hover:text-foreground'
          }`}
        >
          <span><FontAwesomeIcon icon={ch.is_public ? faHashtag : faLock} className="text-xs mr-1.5" />{ch.name}</span>
          {ch.my_role === 'read' && (
            <span className="ml-1 text-xs text-default-400">(read-only)</span>
          )}
        </button>
      ))}
    </div>
  );
}
