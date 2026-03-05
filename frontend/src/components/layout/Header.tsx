import { Button } from '@heroui/react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
  faBars,
  faGear,
  faHashtag,
  faLock,
} from '@fortawesome/free-solid-svg-icons';
import { useChatStore } from '../../stores/chatStore';
import * as api from '../../services/api';
import logoSmall from '../../assets/isle-chat-logo-small.png';
import logoSmallDark from '../../assets/isle-chat-logo-small-dark.png';

interface Props {
  onShowAdmin: () => void;
  onShowSettings: () => void;
  onToggleSidebar: () => void;
  onShowChannelSettings: () => void;
}

export function Header({
  onShowAdmin,
  onShowSettings,
  onToggleSidebar,
  onShowChannelSettings,
}: Props) {
  const user = useChatStore((s) => s.user);
  const clearAuth = useChatStore((s) => s.clearAuth);
  const activeChannelId = useChatStore((s) => s.activeChannelId);
  const channels = useChatStore((s) => s.channels);

  const activeChannel = channels.find((c) => c.id === activeChannelId);
  const canManageChannel =
    activeChannel &&
    !activeChannel.is_direct &&
    (activeChannel.my_role === 'admin' || user?.role === 'admin');

  const handleLogout = async () => {
    try {
      await api.logout();
    } catch {
      /* ignored */
    }
    clearAuth();
  };

  return (
    <header className="bg-content1 border-b border-default-100 px-3 sm:px-4 py-2 flex items-center justify-between">
      <div className="flex items-center gap-2 sm:gap-3 min-w-0">
        <Button
          isIconOnly
          variant="light"
          size="sm"
          className="md:hidden flex-shrink-0"
          onPress={onToggleSidebar}
        >
          <FontAwesomeIcon icon={faBars} />
        </Button>
        <img
          src={logoSmall}
          alt="Isle Chat"
          className="h-7 w-7 flex-shrink-0 dark:hidden"
        />
        <img
          src={logoSmallDark}
          alt="Isle Chat"
          className="h-7 w-7 flex-shrink-0 hidden dark:block"
        />
        <span className="text-foreground font-bold hidden sm:inline flex-shrink-0">
          Isle Chat
        </span>
        {activeChannel && (
          <h2 className="text-foreground font-semibold truncate">
            {!activeChannel.is_direct && (
              <FontAwesomeIcon
                icon={activeChannel.is_public ? faHashtag : faLock}
                className="text-xs mr-1.5"
              />
            )}
            {activeChannel.name || 'Direct Message'}
          </h2>
        )}
        {activeChannel?.description && (
          <span className="text-default-500 text-sm hidden md:inline">
            | {activeChannel.description}
          </span>
        )}
        {canManageChannel && (
          <Button
            isIconOnly
            variant="light"
            size="sm"
            onPress={onShowChannelSettings}
            title="Channel Settings"
          >
            <FontAwesomeIcon icon={faGear} />
          </Button>
        )}
      </div>

      <div className="flex items-center gap-1 sm:gap-2 flex-shrink-0">
        {user?.role === 'admin' && (
          <Button variant="light" size="sm" onPress={onShowAdmin}>
            Admin
          </Button>
        )}
        <Button variant="light" size="sm" onPress={onShowSettings}>
          {user?.display_name}
        </Button>
        <Button
          variant="light"
          size="sm"
          color="default"
          onPress={handleLogout}
        >
          Logout
        </Button>
      </div>
    </header>
  );
}
