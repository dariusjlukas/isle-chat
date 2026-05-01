import { useState, useEffect, useRef, lazy, Suspense } from 'react';
import { Spinner } from '@heroui/react';
import {
  faGear,
  faUsers,
  faTicket,
  faKey,
  faUserPlus,
  faHardDrive,
  faGauge,
  faTriangleExclamation,
} from '@fortawesome/free-solid-svg-icons';
import { useAuth } from './hooks/useAuth';
import { useChatStore } from './stores/chatStore';
import { LoginPage } from './components/auth/LoginPage';
import { RegisterPage } from './components/auth/RegisterPage';
import { RecoveryLogin } from './components/auth/RecoveryLogin';
import { AddDevice } from './components/auth/AddDevice';
import { NewSidebar } from './components/sidebar/NewSidebar';
import { Header } from './components/layout/Header';
import { ServerStatusBanner } from './components/layout/ServerStatusBanner';
import { ChatArea } from './components/layout/ChatArea';
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
import { AiChatPanel } from './components/ai/AiChatPanel';

// Lazy-loaded feature views (split into separate chunks).
const FileBrowser = lazy(() =>
  import('./components/files/FileBrowser').then((m) => ({
    default: m.FileBrowser,
  })),
);
const CalendarView = lazy(() =>
  import('./components/calendar/CalendarView').then((m) => ({
    default: m.CalendarView,
  })),
);
const TaskBoardView = lazy(() =>
  import('./components/tasks/TaskBoardView').then((m) => ({
    default: m.TaskBoardView,
  })),
);
const WikiView = lazy(() =>
  import('./components/wiki/WikiView').then((m) => ({ default: m.WikiView })),
);
const MinigamesView = lazy(() =>
  import('./components/minigames/MinigamesView').then((m) => ({
    default: m.MinigamesView,
  })),
);
const SandboxView = lazy(() =>
  import('./components/sandbox/SandboxView').then((m) => ({
    default: m.SandboxView,
  })),
);

// Admin / settings panels (only opened on demand).
const InviteManager = lazy(() =>
  import('./components/admin/InviteManager').then((m) => ({
    default: m.InviteManager,
  })),
);
const JoinRequests = lazy(() =>
  import('./components/admin/JoinRequests').then((m) => ({
    default: m.JoinRequests,
  })),
);
const ServerSettings = lazy(() =>
  import('./components/admin/ServerSettings').then((m) => ({
    default: m.ServerSettings,
  })),
);
const RecoveryTokenManager = lazy(() =>
  import('./components/admin/RecoveryTokenManager').then((m) => ({
    default: m.RecoveryTokenManager,
  })),
);
const SetupWizard = lazy(() =>
  import('./components/admin/SetupWizard').then((m) => ({
    default: m.SetupWizard,
  })),
);
const UserManager = lazy(() =>
  import('./components/admin/UserManager').then((m) => ({
    default: m.UserManager,
  })),
);
const StorageManager = lazy(() =>
  import('./components/admin/StorageManager').then((m) => ({
    default: m.StorageManager,
  })),
);
const DangerZone = lazy(() =>
  import('./components/admin/DangerZone').then((m) => ({
    default: m.DangerZone,
  })),
);
const SystemMonitor = lazy(() =>
  import('./components/admin/SystemMonitor').then((m) => ({
    default: m.SystemMonitor,
  })),
);
const UserSettings = lazy(() =>
  import('./components/settings/UserSettings').then((m) => ({
    default: m.UserSettings,
  })),
);

// Fallback used while a lazy chunk loads. Matches the app shell so layout
// doesn't shift around the spinner.
function ViewLoadingFallback() {
  return (
    <div className='flex-1 flex items-center justify-center bg-background'>
      <Spinner size='lg' />
    </div>
  );
}
import { ResizeHandle } from './components/common/ResizeHandle';
import { ConnectionLostModal } from './components/common/ConnectionLostModal';
import { useResizablePanel } from './hooks/useResizablePanel';
import { useConnectionState } from './hooks/useConnectionState';
import { useWebSocketConnection } from './hooks/useWebSocket';
import { wsService } from './services/websocket';
import * as api from './services/api';

type AuthPage = 'login' | 'register' | 'recovery' | 'add-device';

function App() {
  const { isAuthenticated, loading } = useAuth();
  // WebSocket connection lifecycle — must live here (always mounted),
  // not in ChatArea which unmounts when navigating to tools.
  useWebSocketConnection();
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
  const [adminDefaultCategory, setAdminDefaultCategory] = useState<
    string | undefined
  >(undefined);
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
  const setLlmEnabled = useChatStore((s) => s.setLlmEnabled);
  const showAiPanel = useChatStore((s) => s.showAiPanel);
  const setShowAiPanel = useChatStore((s) => s.setShowAiPanel);
  const sidePanelCollapsed = useChatStore((s) => s.sidePanelCollapsed);

  const {
    width: sidebarWidth,
    isResizing: isSidebarResizing,
    handleMouseDown: handleSidebarResize,
  } = useResizablePanel({
    defaultWidth: 288,
    minWidth: 200,
    maxWidth: 500,
    side: 'left',
    storageKey: 'sidebar-width',
  });

  const {
    width: aiPanelWidth,
    isResizing: isAiPanelResizing,
    handleMouseDown: handleAiPanelResize,
  } = useResizablePanel({
    defaultWidth: 380,
    minWidth: 300,
    maxWidth: 700,
    side: 'right',
    storageKey: 'ai-panel-width',
  });

  const isOwner = user?.role === 'owner';
  const wrapAdmin = (node: React.ReactNode) => (
    <Suspense fallback={<ViewLoadingFallback />}>{node}</Suspense>
  );
  const adminCategories: SettingsCategory[] = [
    ...(isOwner
      ? [
          {
            key: 'server-settings',
            label: 'Server Settings',
            icon: faGear,
            content: wrapAdmin(
              <ServerSettings onDirtyChange={setServerSettingsDirty} />,
            ),
          },
        ]
      : []),
    {
      key: 'user-management',
      label: 'User Management',
      icon: faUsers,
      content: wrapAdmin(<UserManager />),
    },
    {
      key: 'invite-tokens',
      label: 'Invite Tokens',
      icon: faTicket,
      content: wrapAdmin(<InviteManager />),
    },
    {
      key: 'recovery-tokens',
      label: 'Account Recovery',
      icon: faKey,
      content: wrapAdmin(<RecoveryTokenManager />),
    },
    {
      key: 'storage',
      label: 'Storage',
      icon: faHardDrive,
      content: wrapAdmin(<StorageManager />),
    },
    {
      key: 'system',
      label: 'System',
      icon: faGauge,
      content: wrapAdmin(<SystemMonitor />),
    },
    {
      key: 'join-requests',
      label: 'Join Requests',
      icon: faUserPlus,
      badge: pendingRequestCount,
      content: wrapAdmin(<JoinRequests />),
    },
    ...(isOwner
      ? [
          {
            key: 'danger-zone',
            label: 'Danger Zone',
            icon: faTriangleExclamation,
            content: wrapAdmin(<DangerZone />),
          },
        ]
      : []),
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
    ]).catch((err) => {
      console.error('Initial workspace load failed', err);
      // TODO: surface via a user-visible toast once a toast system is added.
    });

    // Load server status (archived, setup, name, icon)
    api.getPublicConfig().then((config) => {
      const store = useChatStore.getState();
      store.setServerArchived(config.server_archived);
      store.setServerLockedDown(config.server_locked_down);
      store.setServerName(config.server_name);
      store.setServerIconFileId(config.server_icon_file_id || null);
      store.setServerIconDarkFileId(config.server_icon_dark_file_id || null);
      if (config.llm_enabled !== undefined) {
        setLlmEnabled(config.llm_enabled);
      }
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
    setLlmEnabled,
    user?.role,
  ]);

  // Open admin panel when requested from notification click
  useEffect(() => {
    const unsub = useChatStore.subscribe((state, prev) => {
      if (state.adminPanelRequest && !prev.adminPanelRequest) {
        setAdminDefaultCategory(state.adminPanelRequest);
        setShowAdmin(true);
        state.clearAdminPanelRequest();
      }
    });
    return unsub;
  }, []);

  // Poll pending join requests for admin badge
  useEffect(() => {
    if (!isAuthenticated || (user?.role !== 'admin' && user?.role !== 'owner'))
      return;

    const controller = new AbortController();
    const fetchCount = async () => {
      try {
        const reqs = await api.listJoinRequests(controller.signal);
        setPendingRequestCount(
          reqs.filter((r) => r.status === 'pending').length,
        );
      } catch (err) {
        if (err instanceof DOMException && err.name === 'AbortError') return;
        console.error('Failed to fetch join requests', err);
      }
    };

    fetchCount();
    const interval = setInterval(fetchCount, 15000);
    return () => {
      controller.abort();
      clearInterval(interval);
    };
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
      <ServerStatusBanner />
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
          width={sidebarWidth}
          isResizing={isSidebarResizing}
        />
        {!sidePanelCollapsed && (
          <ResizeHandle
            onMouseDown={handleSidebarResize}
            isResizing={isSidebarResizing}
          />
        )}
        <Suspense fallback={<ViewLoadingFallback />}>
          {activeToolView?.type === 'files' ? (
            <FileBrowser spaceId={activeToolView.spaceId} />
          ) : activeToolView?.type === 'calendar' ? (
            <CalendarView spaceId={activeToolView.spaceId} />
          ) : activeToolView?.type === 'tasks' ? (
            <TaskBoardView spaceId={activeToolView.spaceId} />
          ) : activeToolView?.type === 'wiki' ? (
            <WikiView spaceId={activeToolView.spaceId} />
          ) : activeToolView?.type === 'minigames' ? (
            <MinigamesView spaceId={activeToolView.spaceId} />
          ) : activeView?.type === 'sandbox' ? (
            <SandboxView />
          ) : (
            <ChatArea />
          )}
        </Suspense>
        {showAiPanel && (
          <div
            className='flex shrink-0 animate-[slide-in-right_200ms_ease-out]'
            style={{ width: aiPanelWidth + 4 }}
          >
            <ResizeHandle
              onMouseDown={handleAiPanelResize}
              isResizing={isAiPanelResizing}
            />
            <AiChatPanel
              onClose={() => setShowAiPanel(false)}
              width={aiPanelWidth}
            />
          </div>
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
        key={adminDefaultCategory ?? 'admin'}
        isOpen={showAdmin}
        onClose={() => {
          setShowAdmin(false);
          setAdminDefaultCategory(undefined);
        }}
        title='Admin Panel'
        categories={adminCategories}
        defaultCategory={adminDefaultCategory}
        warning={isOwner && serverSettingsDirty ? 'Unsaved changes' : undefined}
      />

      {showSettings && (
        <Suspense fallback={<ViewLoadingFallback />}>
          <UserSettings onClose={() => setShowSettings(false)} />
        </Suspense>
      )}

      {showSetupWizard && (
        <Suspense fallback={<ViewLoadingFallback />}>
          <SetupWizard onComplete={() => setShowSetupWizard(false)} />
        </Suspense>
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
