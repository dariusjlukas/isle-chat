import { useState, useEffect, useRef } from 'react';
import { Spinner } from '@heroui/react';
import {
  faGear,
  faUsers,
  faTicket,
  faKey,
  faUserPlus,
  faHardDrive,
} from '@fortawesome/free-solid-svg-icons';
import { useAuth } from './hooks/useAuth';
import { useChatStore } from './stores/chatStore';
import { LoginPage } from './components/auth/LoginPage';
import { RegisterPage } from './components/auth/RegisterPage';
import { RecoveryLogin } from './components/auth/RecoveryLogin';
import { AddDevice } from './components/auth/AddDevice';
import { NewSidebar } from './components/sidebar/NewSidebar';
import { Header } from './components/layout/Header';
import { ChatArea } from './components/layout/ChatArea';
import { FileBrowser } from './components/files/FileBrowser';
import { CreateChannel } from './components/channels/CreateChannel';
import { ChannelBrowser } from './components/channels/ChannelBrowser';
import { ChannelSettings } from './components/channels/ChannelSettings';
import { CreateConversation } from './components/conversations/CreateConversation';
import { CreateSpace } from './components/spaces/CreateSpace';
import { SpaceBrowser } from './components/spaces/SpaceBrowser';
import { SpaceSettings } from './components/spaces/SpaceSettings';
import { SpaceInviteNotification } from './components/spaces/SpaceInviteNotification';
import {
  SettingsLayout,
  type SettingsCategory,
} from './components/common/SettingsLayout';
import { InviteManager } from './components/admin/InviteManager';
import { JoinRequests } from './components/admin/JoinRequests';
import { ServerSettings } from './components/admin/ServerSettings';
import { RecoveryTokenManager } from './components/admin/RecoveryTokenManager';
import { SetupWizard } from './components/admin/SetupWizard';
import { UserManager } from './components/admin/UserManager';
import { StorageManager } from './components/admin/StorageManager';
import { UserSettings } from './components/settings/UserSettings';
import { ConnectionLostModal } from './components/common/ConnectionLostModal';
import { useConnectionState } from './hooks/useConnectionState';
import { wsService } from './services/websocket';
import * as api from './services/api';

type AuthPage = 'login' | 'register' | 'recovery' | 'add-device';

function App() {
  const { isAuthenticated, loading } = useAuth();
  // Check for device_token or invite in URL
  const urlParams = new URLSearchParams(window.location.search);
  const urlDeviceToken = urlParams.get('device_token');
  const urlInviteToken = urlParams.get('invite');
  const [authPage, setAuthPage] = useState<AuthPage>(
    urlDeviceToken ? 'add-device' : urlInviteToken ? 'register' : 'login',
  );
  const [showCreateChannel, setShowCreateChannel] = useState(false);
  const [showCreateConversation, setShowCreateConversation] = useState(false);
  const [showCreateSpace, setShowCreateSpace] = useState(false);
  const [showSpaceBrowser, setShowSpaceBrowser] = useState(false);
  const [showSpaceSettings, setShowSpaceSettings] = useState(false);
  const [showAdmin, setShowAdmin] = useState(false);
  const [showSettings, setShowSettings] = useState(false);
  const [showChannelBrowser, setShowChannelBrowser] = useState(false);
  const [showChannelSettings, setShowChannelSettings] = useState(false);
  const [sidebarOpen, setSidebarOpen] = useState(false);
  const [showSetupWizard, setShowSetupWizard] = useState(false);
  const pendingRequestCount = useChatStore((s) => s.pendingRequestCount);
  const setPendingRequestCount = useChatStore((s) => s.setPendingRequestCount);
  const [serverSettingsDirty, setServerSettingsDirty] = useState(false);
  const [showBuildTime, setShowBuildTime] = useState(false);
  const [showConnectionCard, setShowConnectionCard] = useState(false);
  const [heartbeatInfo, setHeartbeatInfo] = useState<{
    lastHeartbeat: Date | null;
    lastPingMs: number | null;
  }>({ lastHeartbeat: null, lastPingMs: null });
  const connectionCardRef = useRef<HTMLDivElement>(null);
  const connectionState = useConnectionState();
  const setChannels = useChatStore((s) => s.setChannels);
  const setUsers = useChatStore((s) => s.setUsers);
  const setSpaces = useChatStore((s) => s.setSpaces);
  const setSpaceInvites = useChatStore((s) => s.setSpaceInvites);
  const setActiveView = useChatStore((s) => s.setActiveView);
  const user = useChatStore((s) => s.user);
  const activeChannelId = useChatStore((s) => s.activeChannelId);
  const channels = useChatStore((s) => s.channels);
  const spaces = useChatStore((s) => s.spaces);
  const activeView = useChatStore((s) => s.activeView);
  const activeToolView = useChatStore((s) => s.activeToolView);
  const serverName = useChatStore((s) => s.serverName);
  const serverIconFileId = useChatStore((s) => s.serverIconFileId);

  const isOwner = user?.role === 'owner';
  const adminCategories: SettingsCategory[] = [
    ...(isOwner
      ? [
          {
            key: 'server-settings',
            label: 'Server Settings',
            icon: faGear,
            content: <ServerSettings onDirtyChange={setServerSettingsDirty} />,
          },
        ]
      : []),
    {
      key: 'user-management',
      label: 'User Management',
      icon: faUsers,
      content: <UserManager />,
    },
    {
      key: 'invite-tokens',
      label: 'Invite Tokens',
      icon: faTicket,
      content: <InviteManager />,
    },
    {
      key: 'recovery-tokens',
      label: 'Account Recovery',
      icon: faKey,
      content: <RecoveryTokenManager />,
    },
    {
      key: 'storage',
      label: 'Storage',
      icon: faHardDrive,
      content: <StorageManager />,
    },
    {
      key: 'join-requests',
      label: 'Join Requests',
      icon: faUserPlus,
      badge: pendingRequestCount,
      content: <JoinRequests />,
    },
  ];

  const activeChannel = channels.find((c) => c.id === activeChannelId);
  const activeSpace =
    activeView?.type === 'space'
      ? spaces.find((s) => s.id === activeView.spaceId)
      : null;

  useEffect(() => {
    if (!isAuthenticated) return;
    Promise.all([
      api.listChannels().then(setChannels),
      api.listUsers().then(setUsers),
      api
        .listSpaceInvites()
        .then(setSpaceInvites)
        .catch(() => {}),
      api.listSpaces().then((loadedSpaces) => {
        setSpaces(loadedSpaces);
        // Auto-select first space or messages view
        const currentView = useChatStore.getState().activeView;
        if (!currentView) {
          if (loadedSpaces.length > 0) {
            setActiveView({ type: 'space', spaceId: loadedSpaces[0].id });
          } else {
            setActiveView({ type: 'messages' });
          }
        }
      }),
    ]).catch(() => {});

    // Load server status (archived, setup, name, icon)
    api.getPublicConfig().then((config) => {
      const store = useChatStore.getState();
      store.setServerArchived(config.server_archived);
      store.setServerName(config.server_name);
      store.setServerIconFileId(config.server_icon_file_id || null);
      store.setServerIconDarkFileId(config.server_icon_dark_file_id || null);
      if (
        !config.setup_completed &&
        (user?.role === 'admin' || user?.role === 'owner')
      ) {
        setShowSetupWizard(true);
      }
    });
  }, [
    isAuthenticated,
    setChannels,
    setUsers,
    setSpaces,
    setActiveView,
    setSpaceInvites,
    user?.role,
  ]);

  // Poll pending join requests for admin badge
  useEffect(() => {
    if (!isAuthenticated || (user?.role !== 'admin' && user?.role !== 'owner'))
      return;

    const fetchCount = () => {
      api
        .listJoinRequests()
        .then((reqs) => {
          setPendingRequestCount(
            reqs.filter((r) => r.status === 'pending').length,
          );
        })
        .catch(() => {});
    };

    fetchCount();
    const interval = setInterval(fetchCount, 15000);
    return () => clearInterval(interval);
  }, [isAuthenticated, user?.role, setPendingRequestCount]);

  // Live-update heartbeat info while card is open
  useEffect(() => {
    if (!showConnectionCard) return;
    const interval = setInterval(() => {
      setHeartbeatInfo(wsService.getHeartbeatInfo());
    }, 500);
    return () => clearInterval(interval);
  }, [showConnectionCard]);

  // Close connection card on click outside
  useEffect(() => {
    if (!showConnectionCard) return;
    const handleClick = (e: MouseEvent) => {
      if (
        connectionCardRef.current &&
        !connectionCardRef.current.contains(e.target as Node)
      ) {
        setShowConnectionCard(false);
      }
    };
    document.addEventListener('mousedown', handleClick);
    return () => document.removeEventListener('mousedown', handleClick);
  }, [showConnectionCard]);

  // Keep browser tab title in sync with server name
  useEffect(() => {
    document.title = serverName;
  }, [serverName]);

  // Keep favicon in sync with server icon
  useEffect(() => {
    const link =
      document.querySelector<HTMLLinkElement>("link[rel='icon']") ||
      document.createElement('link');
    if (serverIconFileId) {
      link.rel = 'icon';
      link.href = api.getAvatarUrl(serverIconFileId);
      if (!link.parentNode) document.head.appendChild(link);
    } else {
      link.href = '/favicon.ico';
    }
  }, [serverIconFileId]);

  if (loading) {
    return (
      <div className='min-h-screen flex items-center justify-center bg-background'>
        <Spinner size='lg' />
      </div>
    );
  }

  if (!isAuthenticated) {
    switch (authPage) {
      case 'register':
        return (
          <RegisterPage
            onSwitchToLogin={() => setAuthPage('login')}
            initialInviteToken={urlInviteToken || undefined}
          />
        );
      case 'recovery':
        return <RecoveryLogin onSwitchToLogin={() => setAuthPage('login')} />;
      case 'add-device':
        return (
          <AddDevice
            initialToken={urlDeviceToken || ''}
            onSwitchToLogin={() => setAuthPage('login')}
          />
        );
      default:
        return (
          <LoginPage
            onSwitchToRegister={() => setAuthPage('register')}
            onSwitchToRecovery={() => setAuthPage('recovery')}
            onSwitchToAddDevice={() => setAuthPage('add-device')}
          />
        );
    }
  }

  return (
    <div className='h-screen flex flex-col bg-background'>
      <Header
        onShowAdmin={() => setShowAdmin(true)}
        onShowSettings={() => setShowSettings(true)}
        onToggleSidebar={() => setSidebarOpen((o) => !o)}
        onShowChannelSettings={() => setShowChannelSettings(true)}
        adminNotificationCount={pendingRequestCount}
      />
      <div className='flex flex-1 overflow-hidden relative'>
        <NewSidebar
          onCreateConversation={() => setShowCreateConversation(true)}
          onCreateChannel={() => setShowCreateChannel(true)}
          onBrowseChannels={() => setShowChannelBrowser(true)}
          onBrowseSpaces={() => setShowSpaceBrowser(true)}
          onShowSpaceSettings={() => setShowSpaceSettings(true)}
          open={sidebarOpen}
          onClose={() => setSidebarOpen(false)}
        />
        {activeToolView?.type === 'files' ? (
          <FileBrowser spaceId={activeToolView.spaceId} />
        ) : (
          <ChatArea />
        )}
      </div>

      {showCreateChannel && activeView?.type === 'space' && (
        <CreateChannel
          spaceId={activeView.spaceId}
          onClose={() => setShowCreateChannel(false)}
        />
      )}

      {showCreateConversation && (
        <CreateConversation onClose={() => setShowCreateConversation(false)} />
      )}

      {showCreateSpace && (
        <CreateSpace onClose={() => setShowCreateSpace(false)} />
      )}

      {showSpaceBrowser && (
        <SpaceBrowser
          onClose={() => setShowSpaceBrowser(false)}
          onCreateSpace={() => {
            setShowSpaceBrowser(false);
            setShowCreateSpace(true);
          }}
        />
      )}

      {showSpaceSettings && activeSpace && (
        <SpaceSettings
          space={activeSpace}
          onClose={() => setShowSpaceSettings(false)}
        />
      )}

      {showChannelBrowser && (
        <ChannelBrowser
          onClose={() => setShowChannelBrowser(false)}
          spaceId={
            activeView?.type === 'space' ? activeView.spaceId : undefined
          }
        />
      )}

      {showChannelSettings && activeChannel && !activeChannel.is_direct && (
        <ChannelSettings
          channel={activeChannel}
          onClose={() => setShowChannelSettings(false)}
        />
      )}

      <SettingsLayout
        isOpen={showAdmin}
        onClose={() => setShowAdmin(false)}
        title='Admin Panel'
        categories={adminCategories}
        warning={isOwner && serverSettingsDirty ? 'Unsaved changes' : undefined}
      />

      {showSettings && <UserSettings onClose={() => setShowSettings(false)} />}

      {showSetupWizard && (
        <SetupWizard onComplete={() => setShowSetupWizard(false)} />
      )}

      <SpaceInviteNotification />
      <ConnectionLostModal />
      <div className='shrink-0 flex items-center justify-between px-3 py-0.5 text-[10px] text-default-400 bg-content1 border-t border-divider'>
        <div className='flex items-center gap-2'>
          <span className='font-medium text-default-500'>EnclaveStation</span>
          <span>·</span>
          <a
            href='https://github.com/dariusjlukas/enclave-station'
            target='_blank'
            rel='noopener noreferrer'
            className='hover:text-default-500'
          >
            GitHub
          </a>
          <span>·</span>
          <a
            href='https://github.com/dariusjlukas/enclave-station/blob/main/LICENSE'
            target='_blank'
            rel='noopener noreferrer'
            className='hover:text-default-500'
          >
            License
          </a>
        </div>
        <div className='flex items-center gap-2'>
          <div className='relative' ref={connectionCardRef}>
            <span
              className={`inline-block w-2 h-2 rounded-full cursor-pointer ${
                connectionState === 'connected'
                  ? 'bg-success'
                  : connectionState === 'connecting'
                    ? 'bg-warning'
                    : 'bg-danger'
              }`}
              onClick={() => setShowConnectionCard((v) => !v)}
            />
            {showConnectionCard && (
              <div className='absolute bottom-5 right-0 w-52 p-3 rounded-lg shadow-lg bg-content2 border border-divider text-[11px] text-default-600 z-50'>
                <div className='font-semibold mb-1.5 text-default-700'>
                  Connection
                </div>
                <div className='flex justify-between mb-1'>
                  <span>Status</span>
                  <span
                    className={
                      connectionState === 'connected'
                        ? 'text-success'
                        : connectionState === 'connecting'
                          ? 'text-warning'
                          : 'text-danger'
                    }
                  >
                    {connectionState === 'connected'
                      ? 'Connected'
                      : connectionState === 'connecting'
                        ? 'Connecting...'
                        : 'Disconnected'}
                  </span>
                </div>
                <div className='flex justify-between mb-1'>
                  <span>Ping</span>
                  <span>
                    {heartbeatInfo.lastPingMs !== null
                      ? `${heartbeatInfo.lastPingMs}ms`
                      : '—'}
                  </span>
                </div>
                <div className='flex justify-between'>
                  <span>Last heartbeat</span>
                  <span>
                    {heartbeatInfo.lastHeartbeat
                      ? heartbeatInfo.lastHeartbeat.toLocaleTimeString()
                      : '—'}
                  </span>
                </div>
              </div>
            )}
          </div>
          {showBuildTime && (
            <span>Built {new Date(__BUILD_TIME__).toLocaleString()}</span>
          )}
          <span
            className='cursor-pointer select-none hover:text-default-500'
            onClick={() => setShowBuildTime((v) => !v)}
          >
            {__BUILD_INFO__}
          </span>
        </div>
      </div>
    </div>
  );
}

export default App;
