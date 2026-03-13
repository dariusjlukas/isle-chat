import {
  Button,
  Dropdown,
  DropdownTrigger,
  DropdownMenu,
  DropdownItem,
} from '@heroui/react';
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
import logoLight from '../../assets/enclavestation-light-mode-icon.png';
import logoDark from '../../assets/enclavestation-dark-mode-icon.png';
import { GlobalSearch } from '../search/GlobalSearch';
import { UserAvatar } from '../common/UserAvatar';
import { NotificationDropdown } from './NotificationDropdown';

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
  const serverName = useChatStore((s) => s.serverName);
  const serverIconFileId = useChatStore((s) => s.serverIconFileId);
  const serverIconDarkFileId = useChatStore((s) => s.serverIconDarkFileId);

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
    <header className='bg-content1 border-b border-default-100 px-3 sm:px-4 py-2 grid grid-cols-[minmax(0,1fr)_auto_auto] sm:grid-cols-[minmax(0,1fr)_minmax(0,2fr)_minmax(0,1fr)] items-center gap-2'>
      <div className='flex items-center gap-2 sm:gap-3 min-w-0 overflow-hidden'>
        <Button
          isIconOnly
          variant='light'
          size='sm'
          className='md:hidden flex-shrink-0'
          onPress={onToggleSidebar}
        >
          <FontAwesomeIcon icon={faBars} />
        </Button>
        {serverIconFileId && serverIconDarkFileId ? (
          <>
            <img
              src={api.getAvatarUrl(serverIconFileId)}
              alt={serverName}
              className='h-7 w-7 flex-shrink-0 rounded-md object-cover dark:hidden'
            />
            <img
              src={api.getAvatarUrl(serverIconDarkFileId)}
              alt={serverName}
              className='h-7 w-7 flex-shrink-0 rounded-md object-cover hidden dark:block'
            />
          </>
        ) : serverIconFileId ? (
          <img
            src={api.getAvatarUrl(serverIconFileId)}
            alt={serverName}
            className='h-7 w-7 flex-shrink-0 rounded-md object-cover'
          />
        ) : (
          <>
            <img
              src={logoLight}
              alt={serverName}
              className='h-7 w-7 flex-shrink-0 dark:hidden'
            />
            <img
              src={logoDark}
              alt={serverName}
              className='h-7 w-7 flex-shrink-0 hidden dark:block'
            />
          </>
        )}
        <span className='text-foreground font-bold hidden md:inline flex-shrink-0'>
          {serverName}
        </span>
        {activeChannel && (
          <h2 className='text-foreground font-semibold truncate'>
            {activeSpace && (
              <span className='text-default-400 font-normal'>
                <button
                  className='hover:text-default-600 transition-colors cursor-pointer'
                  onClick={() =>
                    useChatStore.getState().setActiveView({
                      type: 'space',
                      spaceId: activeSpace.id,
                    })
                  }
                >
                  {activeSpace.name}
                </button>
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
            {activeChannel?.description && !activeChannel.is_direct && (
              <span className='ml-1 text-default-500 text-sm'>
                | {activeChannel.description}
              </span>
            )}
          </h2>
        )}
        {showChannelSettings && (
          <Button
            isIconOnly
            variant='light'
            size='sm'
            className='flex-shrink-0'
            onPress={onShowChannelSettings}
            title='Channel Settings'
          >
            <FontAwesomeIcon icon={faGear} />
          </Button>
        )}
      </div>

      <GlobalSearch />

      <div className='flex items-center gap-1 sm:gap-2 justify-end'>
        <NotificationDropdown />
        <Dropdown placement='bottom-end'>
          <DropdownTrigger>
            <button className='cursor-pointer rounded-full relative focus:outline-none focus-visible:ring-2 focus-visible:ring-primary'>
              {user && (
                <UserAvatar
                  username={user.username}
                  avatarFileId={user.avatar_file_id}
                  profileColor={user.profile_color}
                  size='md'
                />
              )}
              {adminNotificationCount > 0 &&
                (user?.role === 'admin' || user?.role === 'owner') && (
                  <span className='absolute -bottom-1 -right-1 bg-danger text-white text-[10px] font-bold rounded-full min-w-[18px] h-[18px] flex items-center justify-center px-1'>
                    {adminNotificationCount}
                  </span>
                )}
            </button>
          </DropdownTrigger>
          <DropdownMenu aria-label='User menu'>
            {user?.role === 'admin' || user?.role === 'owner' ? (
              <DropdownItem
                key='admin'
                startContent={
                  <FontAwesomeIcon
                    icon={faShieldHalved}
                    className='text-default-500'
                  />
                }
                onPress={onShowAdmin}
              >
                Admin Panel
                {adminNotificationCount > 0 && (
                  <span className='ml-2 bg-danger text-white text-[10px] font-bold rounded-full min-w-[18px] h-[18px] inline-flex items-center justify-center px-1'>
                    {adminNotificationCount}
                  </span>
                )}
              </DropdownItem>
            ) : null}
            <DropdownItem
              key='settings'
              startContent={
                <FontAwesomeIcon
                  icon={faSliders}
                  className='text-default-500'
                />
              }
              onPress={onShowSettings}
            >
              User Settings
            </DropdownItem>
            <DropdownItem
              key='logout'
              startContent={
                <FontAwesomeIcon
                  icon={faRightFromBracket}
                  className='text-danger'
                />
              }
              className='text-danger hover:text-white-50'
              color='danger'
              onPress={handleLogout}
            >
              Logout
            </DropdownItem>
          </DropdownMenu>
        </Dropdown>
      </div>
    </header>
  );
}
