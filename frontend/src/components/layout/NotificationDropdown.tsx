import { useState, useRef, useEffect, useCallback } from 'react';
import { Button, Spinner } from '@heroui/react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
  faBell,
  faAt,
  faReply,
  faCheck,
  faComment,
  faEnvelope,
  faUserPlus,
} from '@fortawesome/free-solid-svg-icons';
import { useChatStore } from '../../stores/chatStore';
import * as api from '../../services/api';
import { UserAvatar } from '../common/UserAvatar';
import { relativeTime } from '../../utils/time';

export function NotificationDropdown() {
  const [isOpen, setIsOpen] = useState(false);
  const [loading, setLoading] = useState(false);
  const containerRef = useRef<HTMLDivElement>(null);

  const notifications = useChatStore((s) => s.notifications);
  const unreadCount = useChatStore((s) => s.unreadNotificationCount);
  const users = useChatStore((s) => s.users);
  const channels = useChatStore((s) => s.channels);
  const setActiveChannel = useChatStore((s) => s.setActiveChannel);
  const setJumpToMessage = useChatStore((s) => s.setJumpToMessage);

  const fetchNotifications = useCallback(async () => {
    setLoading(true);
    try {
      const data = await api.getNotifications();
      useChatStore.getState().setNotifications(data.notifications);
      useChatStore.getState().setUnreadNotificationCount(data.unread_count);
    } catch (e) {
      console.error('Failed to fetch notifications:', e);
    } finally {
      setLoading(false);
    }
  }, []);

  const handleToggle = () => {
    if (!isOpen) {
      fetchNotifications();
    }
    setIsOpen(!isOpen);
  };

  // Close on click outside
  useEffect(() => {
    function handleClickOutside(e: MouseEvent) {
      if (
        containerRef.current &&
        !containerRef.current.contains(e.target as Node)
      ) {
        setIsOpen(false);
      }
    }
    if (isOpen) {
      document.addEventListener('mousedown', handleClickOutside);
      return () =>
        document.removeEventListener('mousedown', handleClickOutside);
    }
  }, [isOpen]);

  const handleNotificationClick = async (
    notification: (typeof notifications)[0],
  ) => {
    // Mark as read
    if (!notification.is_read) {
      try {
        await api.markNotificationRead(notification.id);
        useChatStore.getState().markNotificationRead(notification.id);
      } catch (e) {
        console.error('Failed to mark notification read:', e);
      }
    }

    // Navigate to the relevant location
    if (notification.type === 'join_request') {
      useChatStore.getState().requestAdminPanel('join-requests');
    } else if (notification.channel_id && notification.message_id) {
      const channel = channels.find((c) => c.id === notification.channel_id);
      if (channel?.space_id) {
        useChatStore
          .getState()
          .setActiveView({ type: 'space', spaceId: channel.space_id });
      } else if (channel?.is_direct) {
        useChatStore.getState().setActiveView({ type: 'messages' });
      }
      setActiveChannel(notification.channel_id);
      setJumpToMessage(notification.channel_id, notification.message_id);
    }
    setIsOpen(false);
  };

  const handleMarkAllRead = async () => {
    try {
      await api.markAllNotificationsRead();
      useChatStore.getState().markAllNotificationsRead();
    } catch (e) {
      console.error('Failed to mark all notifications read:', e);
    }
  };

  const getSourceUser = (notification: (typeof notifications)[0]) => {
    return users.find((u) => u.id === notification.source_user_id);
  };

  return (
    <div ref={containerRef} className='relative'>
      <Button
        isIconOnly
        variant='light'
        size='sm'
        onPress={handleToggle}
        className='relative overflow-visible'
      >
        <FontAwesomeIcon icon={faBell} />
        {unreadCount > 0 && (
          <span className='absolute -bottom-1 -right-1 bg-danger text-white text-[10px] font-bold rounded-full min-w-[18px] h-[18px] flex items-center justify-center px-1'>
            {unreadCount > 99 ? '99+' : unreadCount}
          </span>
        )}
      </Button>

      {isOpen && (
        <div className='absolute top-full right-0 mt-1 w-80 sm:w-96 bg-content1 border border-default-200 rounded-xl shadow-lg z-50 overflow-hidden flex flex-col max-h-[28rem]'>
          <div className='flex items-center justify-between px-3 py-2 border-b border-default-100'>
            <span className='text-sm font-semibold'>Notifications</span>
            {unreadCount > 0 && (
              <span className='text-xs text-default-400'>
                {unreadCount} unread
              </span>
            )}
          </div>

          <div className='flex-1 overflow-y-auto'>
            {loading && notifications.length === 0 ? (
              <div className='flex items-center justify-center py-8'>
                <Spinner size='sm' />
              </div>
            ) : notifications.length === 0 ? (
              <p className='text-sm text-default-400 text-center py-8'>
                No notifications
              </p>
            ) : (
              notifications.map((n) => {
                const sourceUser = getSourceUser(n);
                return (
                  <div
                    key={n.id}
                    className={`notif-row flex items-start gap-2 px-3 py-2.5 cursor-pointer transition-colors hover:bg-content2/50 ${
                      !n.is_read ? 'bg-primary/5' : ''
                    }`}
                    onClick={() => handleNotificationClick(n)}
                  >
                    <div className='relative flex-shrink-0 mt-0.5'>
                      <UserAvatar
                        username={n.source_username}
                        avatarFileId={sourceUser?.avatar_file_id}
                        profileColor={sourceUser?.profile_color}
                        size='sm'
                      />
                      <span className='absolute -bottom-0.5 -right-0.5 bg-content1 rounded-full p-px'>
                        <FontAwesomeIcon
                          icon={
                            n.type === 'mention'
                              ? faAt
                              : n.type === 'reply'
                                ? faReply
                                : n.type === 'direct_message'
                                  ? faComment
                                  : n.type === 'space_invite'
                                    ? faEnvelope
                                    : faUserPlus
                          }
                          className='text-[8px] text-default-500'
                        />
                      </span>
                    </div>
                    <div className='flex-1 min-w-0'>
                      <p className='text-xs'>
                        <span className='font-semibold'>
                          {sourceUser?.display_name || n.source_username}
                        </span>{' '}
                        <span className='text-default-500'>
                          {n.type === 'mention'
                            ? 'mentioned you'
                            : n.type === 'reply'
                              ? 'replied to your message'
                              : n.type === 'direct_message'
                                ? 'sent you a message'
                                : n.type === 'space_invite'
                                  ? 'invited you to a space'
                                  : 'requested to join'}
                        </span>
                      </p>
                      <p className='text-xs text-default-400 truncate mt-0.5'>
                        {n.content}
                      </p>
                      <span className='text-[10px] text-default-300 mt-0.5 block'>
                        {relativeTime(n.created_at)}
                      </span>
                    </div>
                    {!n.is_read && (
                      <button
                        className='w-5 h-5 flex items-center justify-center flex-shrink-0 mt-0.5 group'
                        title='Mark as read'
                        onClick={(e) => {
                          e.stopPropagation();
                          api.markNotificationRead(n.id).then(() => {
                            useChatStore.getState().markNotificationRead(n.id);
                          });
                        }}
                      >
                        <span className='w-3 h-3 rounded-full bg-danger block group-hover:hidden' />
                        <span className='hidden group-hover:block'>
                          <FontAwesomeIcon
                            icon={faCheck}
                            className='text-[10px] text-primary'
                          />
                        </span>
                      </button>
                    )}
                  </div>
                );
              })
            )}
          </div>

          {unreadCount > 0 && (
            <div className='border-t border-default-100 px-3 py-2'>
              <Button
                size='sm'
                variant='flat'
                className='w-full'
                startContent={
                  <FontAwesomeIcon icon={faCheck} className='text-xs' />
                }
                onPress={handleMarkAllRead}
              >
                Mark all as read
              </Button>
            </div>
          )}
        </div>
      )}
    </div>
  );
}
