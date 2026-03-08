import {
  Popover,
  PopoverTrigger,
  PopoverContent,
  Chip,
  Button,
} from '@heroui/react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faMessage } from '@fortawesome/free-solid-svg-icons';
import type { User } from '../../types';
import { useChatStore } from '../../stores/chatStore';
import { OnlineStatusDot } from './OnlineStatusDot';
import { relativeTime } from '../../utils/time';
import * as api from '../../services/api';

interface Props {
  user?: User;
  userId?: string;
  channelId?: string;
  children: React.ReactElement;
}

export function UserPopoverCard({
  user: userProp,
  userId,
  channelId,
  children,
}: Props) {
  const storeUser = useChatStore((s) =>
    userId ? s.users.find((u) => u.id === userId) : undefined,
  );
  const user = userProp ?? storeUser;
  const currentUser = useChatStore((s) => s.user);

  const channel = useChatStore((s) =>
    channelId ? s.channels.find((c) => c.id === channelId) : undefined,
  );
  const space = useChatStore((s) => {
    if (!channel?.space_id) return undefined;
    return s.spaces.find((sp) => sp.id === channel.space_id);
  });

  if (!user) return children;

  const uid = userProp?.id ?? userId;
  const isSelf = currentUser?.id === uid;
  const channelMember = channel?.members.find((m) => m.id === uid);
  const spaceMember = space?.members.find((m) => m.id === uid);

  const roleBadges: {
    label: string;
    color:
      | 'warning'
      | 'secondary'
      | 'primary'
      | 'success'
      | 'danger'
      | 'default';
  }[] = [];

  if (user.role === 'owner') {
    roleBadges.push({ label: 'Server Owner', color: 'warning' });
  } else if (user.role === 'admin') {
    roleBadges.push({ label: 'Server Admin', color: 'secondary' });
  }

  if (spaceMember?.role === 'owner') {
    roleBadges.push({ label: 'Space Owner', color: 'warning' });
  } else if (spaceMember?.role === 'admin') {
    roleBadges.push({ label: 'Space Admin', color: 'secondary' });
  }

  if (channelMember?.role === 'owner') {
    roleBadges.push({ label: 'Channel Owner', color: 'warning' });
  } else if (channelMember?.role === 'admin') {
    roleBadges.push({ label: 'Channel Admin', color: 'secondary' });
  }

  const handleStartDM = async () => {
    if (!uid) return;
    try {
      const dm = await api.createDM(uid);
      const channels = await api.listChannels();
      useChatStore.getState().setChannels(channels);
      useChatStore.getState().setActiveView({ type: 'messages' });
      useChatStore.getState().setActiveChannel(dm.id);
    } catch (e) {
      console.error('Failed to start DM:', e);
    }
  };

  return (
    <Popover placement='bottom-start' showArrow offset={6}>
      <PopoverTrigger>{children}</PopoverTrigger>
      <PopoverContent className='p-3 max-w-[240px]'>
        <div className='flex flex-col gap-1.5'>
          <div className='flex items-center gap-2'>
            <OnlineStatusDot
              isOnline={user.is_online}
              lastSeen={user.last_seen}
            />
            <span className='font-semibold text-sm text-foreground truncate'>
              {user.display_name}
            </span>
          </div>
          <p className='text-xs text-default-400'>@{user.username}</p>
          {roleBadges.length > 0 && (
            <div className='flex flex-wrap gap-1'>
              {roleBadges.map((badge) => (
                <Chip
                  key={badge.label}
                  size='sm'
                  variant='flat'
                  color={badge.color}
                  classNames={{ content: 'text-[10px] font-medium' }}
                >
                  {badge.label}
                </Chip>
              ))}
            </div>
          )}
          {user.bio && (
            <p className='text-xs text-default-500 line-clamp-3'>{user.bio}</p>
          )}
          {user.status && (
            <p className='text-xs text-default-500 italic'>{user.status}</p>
          )}
          <p className='text-xs text-default-400 pt-0.5'>
            {user.is_online ? (
              <span className='text-success'>Online</span>
            ) : user.last_seen ? (
              `Last seen ${relativeTime(user.last_seen)}`
            ) : (
              'Offline'
            )}
          </p>
          {!isSelf && (
            <Button
              size='sm'
              variant='solid'
              color='primary'
              className='mt-1'
              onPress={handleStartDM}
              startContent={
                <FontAwesomeIcon icon={faMessage} className='text-xs' />
              }
            >
              Message
            </Button>
          )}
        </div>
      </PopoverContent>
    </Popover>
  );
}
