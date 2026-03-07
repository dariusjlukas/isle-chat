import { Popover, PopoverTrigger, PopoverContent } from '@heroui/react';
import type { User } from '../../types';
import { useChatStore } from '../../stores/chatStore';
import { OnlineStatusDot } from './OnlineStatusDot';
import { relativeTime } from '../../utils/time';

interface Props {
  user?: User;
  userId?: string;
  children: React.ReactElement;
}

export function UserPopoverCard({ user: userProp, userId, children }: Props) {
  const storeUser = useChatStore((s) =>
    userId ? s.users.find((u) => u.id === userId) : undefined,
  );
  const user = userProp ?? storeUser;

  if (!user) return children;

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
        </div>
      </PopoverContent>
    </Popover>
  );
}
