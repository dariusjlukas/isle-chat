import { useEffect, useRef, useCallback } from 'react';
import { OnlineStatusDot } from '../common/OnlineStatusDot';
import type { ChannelMemberInfo } from '../../types';
import { getFilteredOptions, type MentionOption } from './mentionUtils';

interface Props {
  query: string;
  members: ChannelMemberInfo[];
  currentUserId: string;
  onSelect: (option: MentionOption) => void;
  selectedIndex: number;
}

export function MentionAutocomplete({
  query,
  members,
  currentUserId,
  onSelect,
  selectedIndex,
}: Props) {
  const listRef = useRef<HTMLDivElement>(null);

  const options: MentionOption[] = getFilteredOptions(
    query,
    members,
    currentUserId,
  );

  useEffect(() => {
    if (listRef.current && selectedIndex >= 0) {
      const item = listRef.current.children[selectedIndex] as HTMLElement;
      item?.scrollIntoView({ block: 'nearest' });
    }
  }, [selectedIndex]);

  const handleMouseDown = useCallback(
    (e: React.MouseEvent, option: MentionOption) => {
      e.preventDefault();
      onSelect(option);
    },
    [onSelect],
  );

  if (options.length === 0) {
    return null;
  }

  return (
    <div className="absolute bottom-full left-0 right-0 mb-1 z-50">
      <div
        className="bg-content1 border border-divider rounded-lg shadow-lg max-h-48 overflow-y-auto"
        ref={listRef}
      >
        {options.map((option, i) => (
          <div
            key={option.value}
            className={`flex items-center gap-2 px-3 py-2 cursor-pointer text-sm transition-colors ${
              i === selectedIndex
                ? 'bg-primary/15 text-primary'
                : 'hover:bg-content2/50'
            }`}
            onMouseDown={(e) => handleMouseDown(e, option)}
          >
            {option.type === 'channel' ? (
              <>
                <span className="w-2.5 h-2.5 rounded-full bg-warning inline-block flex-shrink-0" />
                <span className="font-medium">@channel</span>
                <span className="text-default-400 text-xs ml-auto">
                  Notify everyone
                </span>
              </>
            ) : (
              <>
                <OnlineStatusDot
                  isOnline={option.member!.is_online}
                  lastSeen={option.member!.last_seen}
                />
                <span className="truncate">
                  {option.member!.display_name}{' '}
                  <span className="text-default-400">
                    @{option.member!.username}
                  </span>
                </span>
              </>
            )}
          </div>
        ))}
      </div>
    </div>
  );
}
