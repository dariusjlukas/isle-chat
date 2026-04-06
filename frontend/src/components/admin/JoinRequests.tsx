import { useState, useEffect, useCallback } from 'react';
import { Button, Card, CardBody } from '@heroui/react';
import * as api from '../../services/api';
import { useChatStore } from '../../stores/chatStore';

export function JoinRequests() {
  const setPendingRequestCount = useChatStore((s) => s.setPendingRequestCount);
  const [requests, setRequests] = useState<
    Array<{
      id: string;
      username: string;
      display_name: string;
      status: string;
      created_at: string;
    }>
  >([]);

  const markJoinRequestNotificationsRead = useCallback(async () => {
    const store = useChatStore.getState();
    let notifications = store.notifications;

    // Notifications may not have been fetched yet (bell badge uses a
    // count pushed over WebSocket, not the full list).  Fetch them so
    // we can mark the right ones as read.
    if (notifications.length === 0) {
      try {
        const data = await api.getNotifications();
        useChatStore.getState().setNotifications(data.notifications);
        useChatStore.getState().setUnreadNotificationCount(data.unread_count);
        notifications = data.notifications;
      } catch {
        /* best-effort */
      }
    }

    const unreadJoinNotifications = notifications.filter(
      (n) => n.type === 'join_request' && !n.is_read,
    );
    for (const n of unreadJoinNotifications) {
      api.markNotificationRead(n.id).catch(() => {});
      useChatStore.getState().markNotificationRead(n.id);
    }
  }, []);

  const loadRequests = async () => {
    try {
      const data = await api.listJoinRequests();
      setRequests(data);
      setPendingRequestCount(data.filter((r) => r.status === 'pending').length);
    } catch (e) {
      console.error('Join request operation failed:', e);
    }
  };

  useEffect(() => {
    loadRequests();
    const interval = setInterval(loadRequests, 10000);
    return () => clearInterval(interval);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const handleApprove = async (id: string) => {
    try {
      await api.approveRequest(id);
      await markJoinRequestNotificationsRead();
      await loadRequests();
    } catch (e) {
      console.error('Join request operation failed:', e);
    }
  };

  const handleDeny = async (id: string) => {
    try {
      await api.denyRequest(id);
      await markJoinRequestNotificationsRead();
      await loadRequests();
    } catch (e) {
      console.error('Join request operation failed:', e);
    }
  };

  return (
    <div>
      <div className='space-y-2'>
        {requests.map((req) => (
          <Card key={req.id}>
            <CardBody className='flex-row items-center justify-between py-3'>
              <div>
                <p className='text-foreground font-medium'>
                  {req.display_name}
                </p>
                <p className='text-sm text-default-500'>@{req.username}</p>
                <p className='text-xs text-default-400 mt-1'>
                  {new Date(req.created_at).toLocaleString()}
                </p>
              </div>
              <div className='flex gap-2'>
                <Button
                  color='success'
                  size='sm'
                  onPress={() => handleApprove(req.id)}
                >
                  Approve
                </Button>
                <Button
                  color='danger'
                  size='sm'
                  onPress={() => handleDeny(req.id)}
                >
                  Deny
                </Button>
              </div>
            </CardBody>
          </Card>
        ))}
        {requests.length === 0 && (
          <p className='text-default-500 text-sm'>No pending requests.</p>
        )}
      </div>
    </div>
  );
}
