import { useMemo } from 'react';
import { Tooltip } from '@heroui/react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faComment, faPlus } from '@fortawesome/free-solid-svg-icons';
import { useChatStore } from '../../stores/chatStore';
import type { SidebarView } from '../../types';

function Badge({ count }: { count: number }) {
  if (count <= 0) return null;
  return (
    <span className='absolute -top-1 -right-1 min-w-[18px] h-[18px] rounded-full bg-danger text-white text-[10px] font-bold flex items-center justify-center px-1 pointer-events-none'>
      {count > 99 ? '99+' : count}
    </span>
  );
}

interface Props {
  onBrowseSpaces: () => void;
}

export function IconRail({ onBrowseSpaces }: Props) {
  const spaces = useChatStore((s) => s.spaces);
  const activeView = useChatStore((s) => s.activeView);
  const setActiveView = useChatStore((s) => s.setActiveView);
  const channels = useChatStore((s) => s.channels);
  const unreadCounts = useChatStore((s) => s.unreadCounts);
  const mentionCounts = useChatStore((s) => s.mentionCounts);

  const messagesUnread = useMemo(() => {
    return channels
      .filter((c) => c.is_direct)
      .reduce((sum, c) => sum + (unreadCounts[c.id] || 0), 0);
  }, [channels, unreadCounts]);

  const spaceUnread = useMemo(() => {
    const result: Record<string, number> = {};
    for (const ch of channels) {
      if (ch.space_id && mentionCounts[ch.id]) {
        result[ch.space_id] = (result[ch.space_id] || 0) + mentionCounts[ch.id];
      }
    }
    return result;
  }, [channels, mentionCounts]);

  const isActive = (view: SidebarView) => {
    if (!activeView) return false;
    if (view.type === 'messages' && activeView.type === 'messages') return true;
    if (
      view.type === 'space' &&
      activeView.type === 'space' &&
      view.spaceId === activeView.spaceId
    )
      return true;
    return false;
  };

  return (
    <div className='w-16 flex-shrink-0 bg-content2/50 border-r border-default-100 flex flex-col items-center py-3 gap-2 overflow-y-auto'>
      <Tooltip content='Messages' placement='right'>
        <button
          onClick={() => setActiveView({ type: 'messages' })}
          className={`relative w-11 h-11 rounded-xl flex items-center justify-center transition-all ${
            isActive({ type: 'messages' })
              ? 'bg-primary text-primary-foreground'
              : 'bg-content2 text-default-500 hover:bg-content3 hover:text-foreground'
          }`}
        >
          <FontAwesomeIcon icon={faComment} className='text-lg' />
          <Badge count={messagesUnread} />
        </button>
      </Tooltip>

      <div className='w-8 border-t border-default-200 my-1' />

      {spaces.map((space) => (
        <Tooltip key={space.id} content={space.name} placement='right'>
          <button
            onClick={() => setActiveView({ type: 'space', spaceId: space.id })}
            className={`relative w-11 h-11 rounded-xl flex items-center justify-center transition-all text-sm font-semibold ${
              isActive({ type: 'space', spaceId: space.id })
                ? 'bg-primary text-primary-foreground'
                : 'bg-content2 text-default-500 hover:bg-content3 hover:text-foreground'
            }`}
          >
            {space.icon || space.name.charAt(0).toUpperCase()}
            <Badge count={spaceUnread[space.id] || 0} />
          </button>
        </Tooltip>
      ))}

      <Tooltip content='Add or browse spaces' placement='right'>
        <button
          onClick={onBrowseSpaces}
          className='w-11 h-11 rounded-xl flex items-center justify-center bg-content2/50 text-default-400 hover:bg-content3 hover:text-foreground transition-all border-2 border-dashed border-default-200'
        >
          <FontAwesomeIcon icon={faPlus} />
        </button>
      </Tooltip>
    </div>
  );
}
