import { Tooltip } from '@heroui/react';
import { relativeTime } from '../../utils/time';

interface OnlineStatusDotProps {
  isOnline: boolean;
  lastSeen?: string;
  size?: 'sm' | 'md';
}

export function OnlineStatusDot({
  isOnline,
  lastSeen,
  size = 'sm',
}: OnlineStatusDotProps) {
  const px = size === 'sm' ? 'w-2.5 h-2.5' : 'w-3 h-3';
  const color = isOnline ? 'bg-success' : 'bg-default-300';

  const dot = (
    <span
      className={`${px} ${color} rounded-full inline-block flex-shrink-0`}
    />
  );

  if (!isOnline && lastSeen) {
    return (
      <Tooltip content={`Last seen ${relativeTime(lastSeen)}`} size="sm">
        {dot}
      </Tooltip>
    );
  }

  return dot;
}
