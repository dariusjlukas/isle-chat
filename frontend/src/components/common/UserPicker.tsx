import { useState, useMemo } from 'react';
import { Input, Checkbox, Chip } from '@heroui/react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faCircleInfo } from '@fortawesome/free-solid-svg-icons';
import { useChatStore } from '../../stores/chatStore';
import { OnlineStatusDot } from './OnlineStatusDot';
import { UserPopoverCard } from './UserPopoverCard';

interface UserPickerProps {
  mode: 'single' | 'multi';
  selected: string[];
  onChange: (selected: string[]) => void;
  excludeIds?: string[];
  label?: string;
  placeholder?: string;
}

export function UserPicker({
  mode,
  selected,
  onChange,
  excludeIds = [],
  label,
  placeholder = 'Search users...',
}: UserPickerProps) {
  const [search, setSearch] = useState('');
  const users = useChatStore((s) => s.users);

  const excludeSet = useMemo(() => new Set(excludeIds), [excludeIds]);

  const filtered = useMemo(() => {
    const available = users.filter((u) => !excludeSet.has(u.id));
    if (!search.trim()) return available;
    const q = search.toLowerCase();
    return available.filter(
      (u) =>
        u.username.toLowerCase().includes(q) ||
        (u.display_name && u.display_name.toLowerCase().includes(q)),
    );
  }, [users, excludeSet, search]);

  const selectedSet = useMemo(() => new Set(selected), [selected]);

  const selectedUsers = useMemo(
    () => users.filter((u) => selectedSet.has(u.id)),
    [users, selectedSet],
  );

  const toggle = (userId: string) => {
    if (mode === 'single') {
      onChange(selectedSet.has(userId) ? [] : [userId]);
    } else {
      if (selectedSet.has(userId)) {
        onChange(selected.filter((id) => id !== userId));
      } else {
        onChange([...selected, userId]);
      }
    }
  };

  return (
    <div>
      {label && (
        <p className="text-sm font-medium text-default-600 mb-2">{label}</p>
      )}

      {mode === 'multi' && selectedUsers.length > 0 && (
        <div className="flex flex-wrap gap-1 mb-2">
          {selectedUsers.map((u) => (
            <Chip
              key={u.id}
              size="sm"
              variant="flat"
              onClose={() => toggle(u.id)}
            >
              {u.display_name || u.username}
            </Chip>
          ))}
        </div>
      )}

      <Input
        placeholder={placeholder}
        variant="bordered"
        size="sm"
        value={search}
        onValueChange={setSearch}
        isClearable
        onClear={() => setSearch('')}
        className="mb-2"
      />

      <div className="max-h-48 overflow-y-auto space-y-0.5">
        {filtered.map((u) => (
          <div
            key={u.id}
            className={`flex items-center gap-2 p-2 rounded-lg cursor-pointer transition-colors ${
              selectedSet.has(u.id) ? 'bg-primary/10' : 'hover:bg-content2/50'
            }`}
            onClick={() => toggle(u.id)}
          >
            {mode === 'multi' && (
              <Checkbox
                isSelected={selectedSet.has(u.id)}
                onValueChange={() => toggle(u.id)}
                size="sm"
              />
            )}
            <OnlineStatusDot isOnline={u.is_online} lastSeen={u.last_seen} />
            <span className="text-sm truncate">
              {u.display_name}{' '}
              <span className="text-default-400">@{u.username}</span>
            </span>
            <UserPopoverCard user={u}>
              <button
                type="button"
                className="ml-auto flex-shrink-0 text-default-400 hover:text-primary transition-colors"
                onClick={(e) => e.stopPropagation()}
              >
                <FontAwesomeIcon icon={faCircleInfo} className="text-sm" />
              </button>
            </UserPopoverCard>
            {mode === 'single' && selectedSet.has(u.id) && (
              <span className="flex-shrink-0 text-primary text-xs">
                Selected
              </span>
            )}
          </div>
        ))}
        {filtered.length === 0 && (
          <p className="text-sm text-default-400 py-4 text-center">
            No users found
          </p>
        )}
      </div>
    </div>
  );
}
