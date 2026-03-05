import { useState, useEffect } from 'react';
import {
  Modal,
  ModalContent,
  ModalHeader,
  ModalBody,
  Spinner,
  Accordion,
  AccordionItem,
} from '@heroui/react';
import { useAuth } from './hooks/useAuth';
import { useChatStore } from './stores/chatStore';
import { LoginPage } from './components/auth/LoginPage';
import { RegisterPage } from './components/auth/RegisterPage';
import { RequestAccess } from './components/auth/RequestAccess';
import { AddDevice } from './components/auth/AddDevice';
import { Sidebar } from './components/layout/Sidebar';
import { Header } from './components/layout/Header';
import { ChatArea } from './components/layout/ChatArea';
import { CreateChannel } from './components/channels/CreateChannel';
import { ChannelBrowser } from './components/channels/ChannelBrowser';
import { ChannelSettings } from './components/channels/ChannelSettings';
import { InviteManager } from './components/admin/InviteManager';
import { JoinRequests } from './components/admin/JoinRequests';
import { ServerSettings } from './components/admin/ServerSettings';
import { UserSettings } from './components/settings/UserSettings';
import * as api from './services/api';

type AuthPage = 'login' | 'register' | 'request' | 'add-device';

function getDeviceTokenFromUrl(): string | null {
  const params = new URLSearchParams(window.location.search);
  const token = params.get('device_token');
  if (token) {
    const url = new URL(window.location.href);
    url.searchParams.delete('device_token');
    window.history.replaceState({}, '', url.pathname + url.search);
  }
  return token;
}

function App() {
  const { isAuthenticated, loading } = useAuth();
  const [urlDeviceToken] = useState(() => getDeviceTokenFromUrl());
  const [authPage, setAuthPage] = useState<AuthPage>(
    urlDeviceToken ? 'add-device' : 'login',
  );
  const [showCreateModal, setShowCreateModal] = useState<
    'channel' | 'dm' | null
  >(null);
  const [showAdmin, setShowAdmin] = useState(false);
  const [showSettings, setShowSettings] = useState(false);
  const [showChannelBrowser, setShowChannelBrowser] = useState(false);
  const [showChannelSettings, setShowChannelSettings] = useState(false);
  const [sidebarOpen, setSidebarOpen] = useState(false);
  const setChannels = useChatStore((s) => s.setChannels);
  const setUsers = useChatStore((s) => s.setUsers);
  const activeChannelId = useChatStore((s) => s.activeChannelId);
  const channels = useChatStore((s) => s.channels);

  const activeChannel = channels.find((c) => c.id === activeChannelId);

  useEffect(() => {
    if (!isAuthenticated) return;
    api.listChannels().then(setChannels);
    api.listUsers().then(setUsers);
  }, [isAuthenticated, setChannels, setUsers]);

  if (loading) {
    return (
      <div className="min-h-screen flex items-center justify-center bg-background">
        <Spinner size="lg" />
      </div>
    );
  }

  if (!isAuthenticated) {
    switch (authPage) {
      case 'register':
        return <RegisterPage onSwitchToLogin={() => setAuthPage('login')} />;
      case 'request':
        return <RequestAccess onSwitchToLogin={() => setAuthPage('login')} />;
      case 'add-device':
        return (
          <AddDevice
            onSwitchToLogin={() => setAuthPage('login')}
            initialToken={urlDeviceToken ?? ''}
          />
        );
      default:
        return (
          <LoginPage
            onSwitchToRegister={() => setAuthPage('register')}
            onSwitchToRequest={() => setAuthPage('request')}
            onSwitchToAddDevice={() => setAuthPage('add-device')}
          />
        );
    }
  }

  return (
    <div className="h-screen flex flex-col bg-background">
      <Header
        onShowAdmin={() => setShowAdmin(true)}
        onShowSettings={() => setShowSettings(true)}
        onToggleSidebar={() => setSidebarOpen((o) => !o)}
        onShowChannelSettings={() => setShowChannelSettings(true)}
      />
      <div className="flex flex-1 overflow-hidden relative">
        <Sidebar
          onCreateChannel={() => setShowCreateModal('channel')}
          onStartDM={() => setShowCreateModal('dm')}
          onBrowseChannels={() => setShowChannelBrowser(true)}
          open={sidebarOpen}
          onClose={() => setSidebarOpen(false)}
        />
        <ChatArea />
      </div>

      {showCreateModal && (
        <CreateChannel
          mode={showCreateModal}
          onClose={() => setShowCreateModal(null)}
        />
      )}

      {showChannelBrowser && (
        <ChannelBrowser onClose={() => setShowChannelBrowser(false)} />
      )}

      {showChannelSettings && activeChannel && !activeChannel.is_direct && (
        <ChannelSettings
          channel={activeChannel}
          onClose={() => setShowChannelSettings(false)}
        />
      )}

      <Modal
        isOpen={showAdmin}
        onOpenChange={setShowAdmin}
        size="lg"
        scrollBehavior="inside"
        backdrop="opaque"
      >
        <ModalContent>
          <ModalHeader>Admin Panel</ModalHeader>
          <ModalBody className="pb-6">
            <Accordion
              variant="splitted"
              selectionMode="multiple"
              defaultExpandedKeys={['server-settings']}
            >
              <AccordionItem key="server-settings" title="Server Settings">
                <ServerSettings />
              </AccordionItem>
              <AccordionItem key="invite-tokens" title="Invite Tokens">
                <InviteManager />
              </AccordionItem>
              <AccordionItem key="join-requests" title="Join Requests">
                <JoinRequests />
              </AccordionItem>
            </Accordion>
          </ModalBody>
        </ModalContent>
      </Modal>

      {showSettings && <UserSettings onClose={() => setShowSettings(false)} />}
    </div>
  );
}

export default App;
