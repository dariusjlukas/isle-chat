import { useMemo } from 'react';
import { Tooltip } from '@heroui/react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
  faComment,
  faPlus,
  faHouseUser,
  faShareNodes,
  faHexagonNodes,
} from '@fortawesome/free-solid-svg-icons';
import { useChatStore } from '../../stores/chatStore';
import type { SidebarView } from '../../types';
import { SpaceAvatar } from '../common/SpaceAvatar';

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
  onSharedWithMe?: () => void;
}

export function IconRail({ onBrowseSpaces, onSharedWithMe }: Props) {
  const spaces = useChatStore((s) => s.spaces);
  const activeView = useChatStore((s) => s.activeView);
  const setActiveView = useChatStore((s) => s.setActiveView);
  const sidePanelCollapsed = useChatStore((s) => s.sidePanelCollapsed);
  const setSidePanelCollapsed = useChatStore((s) => s.setSidePanelCollapsed);
  const llmEnabled = useChatStore((s) => s.llmEnabled);
  const showAiPanel = useChatStore((s) => s.showAiPanel);
  const setShowAiPanel = useChatStore((s) => s.setShowAiPanel);
  const channels = useChatStore((s) => s.channels);
  const unreadCounts = useChatStore((s) => s.unreadCounts);
  const mentionCounts = useChatStore((s) => s.mentionCounts);

  const messagesUnread = useMemo(() => {
    return channels
      .filter((c) => c.is_direct)
      .reduce((sum, c) => sum + (unreadCounts[c.id] || 0), 0);
  }, [channels, unreadCounts]);

  const personalSpace = useMemo(
    () => spaces.find((s) => s.is_personal),
    [spaces],
  );
  const regularSpaces = useMemo(
    () => spaces.filter((s) => !s.is_personal),
    [spaces],
  );

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
    if (view.type === 'ai' && activeView.type === 'ai') return true;
    if (
      view.type === 'space' &&
      activeView.type === 'space' &&
      view.spaceId === activeView.spaceId
    )
      return true;
    return false;
  };

  const handleViewClick = (view: SidebarView) => {
    if (isActive(view)) {
      setSidePanelCollapsed(!sidePanelCollapsed);
    } else {
      setActiveView(view);
    }
  };

  return (
    <div className='w-16 flex-shrink-0 bg-content2/50 border-r border-default-100 flex flex-col items-center py-3 gap-2 overflow-y-auto'>
      <Tooltip content='Messages' placement='right'>
        <button
          onClick={() => handleViewClick({ type: 'messages' })}
          className={`relative w-11 h-11 rounded-xl flex items-center justify-center transition-all bg-content2 text-default-500 cursor-pointer ${
            isActive({ type: 'messages' })
              ? 'ring-2 ring-primary text-primary'
              : 'hover:ring-2 hover:ring-default-300 hover:text-foreground'
          }`}
        >
          <FontAwesomeIcon icon={faComment} className='text-lg' />
          <Badge count={messagesUnread} />
        </button>
      </Tooltip>

      {(llmEnabled || personalSpace) && <div className='w-8 border-t border-default-200 my-1' />}

      {personalSpace && (
        <>
          <Tooltip content='My Space' placement='right'>
            <button
              onClick={() =>
                handleViewClick({ type: 'space', spaceId: personalSpace.id })
              }
              className={`relative w-11 h-11 rounded-xl flex items-center justify-center transition-all bg-content2 text-default-500 cursor-pointer ${
                isActive({ type: 'space', spaceId: personalSpace.id })
                  ? 'ring-2 ring-primary text-primary'
                  : 'hover:ring-2 hover:ring-default-300 hover:text-foreground'
              }`}
            >
              <FontAwesomeIcon icon={faHouseUser} className='text-lg' />
            </button>
          </Tooltip>
          <Tooltip content='Shared with me' placement='right'>
            <button
              onClick={() => onSharedWithMe?.()}
              className='w-11 h-11 rounded-xl flex items-center justify-center bg-content2/50 text-default-400 hover:bg-content3 hover:text-foreground transition-all cursor-pointer'
            >
              <FontAwesomeIcon icon={faShareNodes} className='text-sm' />
            </button>
          </Tooltip>
        </>
      )}

      {llmEnabled && (
        <Tooltip content='AI Assistant' placement='right'>
          <button
            onClick={() => {
              if (isActive({ type: 'ai' })) {
                setSidePanelCollapsed(!sidePanelCollapsed);
              } else {
                setActiveView({ type: 'ai' });
                if (!showAiPanel) setShowAiPanel(true);
              }
            }}
            className={`relative w-11 h-11 rounded-xl flex items-center justify-center transition-all bg-content2 text-default-500 cursor-pointer ${
              isActive({ type: 'ai' })
                ? 'ring-2 ring-primary text-primary'
                : 'hover:ring-2 hover:ring-default-300 hover:text-foreground'
            }`}
          >
            <FontAwesomeIcon icon={faHexagonNodes} className='text-lg' />
          </button>
        </Tooltip>
      )}

      <div className='w-8 border-t border-default-200 my-1' />

      {regularSpaces.map((space) => (
        <Tooltip key={space.id} content={space.name} placement='right'>
          <button
            onClick={() =>
              handleViewClick({ type: 'space', spaceId: space.id })
            }
            className={`relative w-11 h-11 rounded-xl flex items-center justify-center transition-all text-sm font-semibold overflow-hidden cursor-pointer ${
              isActive({ type: 'space', spaceId: space.id })
                ? 'ring-2 ring-primary'
                : 'hover:ring-2 hover:ring-default-300'
            }`}
          >
            <SpaceAvatar
              name={space.name}
              avatarFileId={space.avatar_file_id}
              profileColor={space.profile_color}
              size='md'
            />
            <Badge count={spaceUnread[space.id] || 0} />
          </button>
        </Tooltip>
      ))}

      <Tooltip content='Add or browse spaces' placement='right'>
        <button
          onClick={onBrowseSpaces}
          className='w-11 h-11 rounded-xl flex items-center justify-center bg-content2/50 text-default-400 hover:bg-content3 hover:text-foreground transition-all border-2 border-dashed border-default-200 cursor-pointer'
        >
          <FontAwesomeIcon icon={faPlus} />
        </button>
      </Tooltip>
    </div>
  );
}
