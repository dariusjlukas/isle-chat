import { Button, Tooltip } from '@heroui/react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
  faBars,
  faGear,
  faHashtag,
  faLock,
  faRightFromBracket,
  faShieldHalved,
  faSliders,
} from '@fortawesome/free-solid-svg-icons';
import { useChatStore } from '../../stores/chatStore';
import * as api from '../../services/api';
import logoSmall from '../../assets/isle-chat-logo-small.png';
import logoSmallDark from '../../assets/isle-chat-logo-small-dark.png';
import { GlobalSearch } from '../search/GlobalSearch';

interface Props {
  onShowAdmin: () => void;
  onShowSettings: () => void;
  onToggleSidebar: () => void;
  onShowChannelSettings: () => void;
  adminNotificationCount?: number;
}

export function Header({
  onShowAdmin,
  onShowSettings,
  onToggleSidebar,
  onShowChannelSettings,
  adminNotificationCount = 0,
}: Props) {
  const user = useChatStore((s) => s.user);
  const clearAuth = useChatStore((s) => s.clearAuth);
  const activeChannelId = useChatStore((s) => s.activeChannelId);
  const channels = useChatStore((s) => s.channels);
  const spaces = useChatStore((s) => s.spaces);

  const activeChannel = channels.find((c) => c.id === activeChannelId);
  const activeSpace = activeChannel?.space_id
    ? spaces.find((s) => s.id === activeChannel.space_id)
    : null;

  const showChannelSettings = activeChannel && !activeChannel.is_direct;

  const getChannelDisplayName = () => {
    if (!activeChannel) return null;
    if (activeChannel.is_direct) {
      if (activeChannel.conversation_name)
        return activeChannel.conversation_name;
      const others =
        activeChannel.members?.filter((m) => m.id !== user?.id) || [];
      if (others.length === 0) return `${user?.display_name || 'You'} (you)`;
      return others.map((o) => o.display_name || o.username).join(', ');
    }
    return activeChannel.name;
  };

  const handleLogout = async () => {
    try {
      await api.logout();
    } catch (e) {
      console.error('Logout failed:', e);
    }
    clearAuth();
  };

  return (
    <header className='bg-content1 border-b border-default-100 px-3 sm:px-4 py-2 grid grid-cols-[minmax(0,1fr)_minmax(0,2fr)_minmax(0,1fr)] items-center gap-2'>
      <div className='flex items-center gap-2 sm:gap-3 min-w-0'>
        <Button
          isIconOnly
          variant='light'
          size='sm'
          className='md:hidden flex-shrink-0'
          onPress={onToggleSidebar}
        >
          <FontAwesomeIcon icon={faBars} />
        </Button>
        <img
          src={logoSmall}
          alt='Isle Chat'
          className='h-7 w-7 flex-shrink-0 dark:hidden'
        />
        <img
          src={logoSmallDark}
          alt='Isle Chat'
          className='h-7 w-7 flex-shrink-0 hidden dark:block'
        />
        <span className='text-foreground font-bold hidden sm:inline flex-shrink-0'>
          Isle Chat
        </span>
        {activeChannel && (
          <h2 className='text-foreground font-semibold truncate'>
            {activeSpace && (
              <span className='text-default-400 font-normal'>
                {activeSpace.icon && (
                  <span className='mr-1'>{activeSpace.icon}</span>
                )}
                {activeSpace.name}
                <span className='mx-1.5'>/</span>
              </span>
            )}
            {!activeChannel.is_direct && (
              <FontAwesomeIcon
                icon={activeChannel.is_public ? faHashtag : faLock}
                className='text-xs mr-1.5'
              />
            )}
            {getChannelDisplayName()}
          </h2>
        )}
        {activeChannel?.description && !activeChannel.is_direct && (
          <span className='text-default-500 text-sm hidden md:inline'>
            | {activeChannel.description}
          </span>
        )}
        {showChannelSettings && (
          <Button
            isIconOnly
            variant='light'
            size='sm'
            onPress={onShowChannelSettings}
            title='Channel Settings'
          >
            <FontAwesomeIcon icon={faGear} />
          </Button>
        )}
      </div>

      <GlobalSearch />

      <div className='flex items-center gap-1 sm:gap-2 justify-end'>
        {(user?.role === 'admin' || user?.role === 'owner') && (
          <Tooltip content='Admin Panel'>
            <Button
              isIconOnly
              variant='light'
              size='sm'
              onPress={onShowAdmin}
              className='relative overflow-visible'
            >
              <FontAwesomeIcon icon={faShieldHalved} />
              {adminNotificationCount > 0 && (
                <span className='absolute -bottom-1 -right-1 bg-danger text-white text-[10px] font-bold rounded-full min-w-[18px] h-[18px] flex items-center justify-center px-1'>
                  {adminNotificationCount}
                </span>
              )}
            </Button>
          </Tooltip>
        )}
        <Tooltip content='User Settings'>
          <Button isIconOnly variant='light' size='sm' onPress={onShowSettings}>
            <FontAwesomeIcon icon={faSliders} />
          </Button>
        </Tooltip>
        <Tooltip content='Logout'>
          <Button
            isIconOnly
            variant='light'
            size='sm'
            color='default'
            onPress={handleLogout}
          >
            <FontAwesomeIcon icon={faRightFromBracket} />
          </Button>
        </Tooltip>
      </div>
    </header>
  );
}
