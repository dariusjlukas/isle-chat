import { useState, useEffect } from 'react';
import { Select, SelectItem } from '@heroui/react';
import { useChatStore } from '../../stores/chatStore';
import * as api from '../../services/api';
import { OnlineStatusDot } from '../common/OnlineStatusDot';
import { UserPopoverCard } from '../common/UserPopoverCard';

interface AdminUser {
  id: string;
  username: string;
  display_name: string;
  role: string;
  is_online: boolean;
  last_seen: string;
}

export function UserManager() {
  const [users, setUsers] = useState<AdminUser[]>([]);
  const [error, setError] = useState<string | null>(null);
  const currentUser = useChatStore((s) => s.user);

  useEffect(() => {
    api
      .listAdminUsers()
      .then(setUsers)
      .catch(() => {});
  }, []);

  const handleChangeRole = async (userId: string, newRole: string) => {
    setError(null);
    try {
      await api.changeUserRole(userId, newRole);
      const data = await api.listAdminUsers();
      setUsers(data);
    } catch (e) {
      const msg = e instanceof Error ? e.message : 'Failed to change role';
      setError(msg);
    }
  };

  const SERVER_RANK: Record<string, number> = {
    owner: 2,
    admin: 1,
    user: 0,
  };
  const actorRank = SERVER_RANK[currentUser?.role ?? 'user'] ?? 0;

  const ALL_ROLES = [
    { key: 'owner', label: 'Owner', rank: 2 },
    { key: 'admin', label: 'Admin', rank: 1 },
    { key: 'user', label: 'User', rank: 0 },
  ];

  // Can edit if: target rank is strictly below actor's rank, OR self (for self-demotion)
  const canEditUser = (u: AdminUser) => {
    const targetRank = SERVER_RANK[u.role] ?? 0;
    const isSelf = u.id === currentUser?.id;
    return targetRank < actorRank || isSelf;
  };

  // Show roles up to actor's rank (backend enforces promotion/demotion rules)
  const roleItems = ALL_ROLES.filter((r) => r.rank <= actorRank);

  return (
    <div>
      {error && <p className='text-xs text-danger mb-2'>{error}</p>}
      <div className='space-y-2'>
        {users.map((u) => {
          const canEdit = canEditUser(u);
          return (
            <div
              key={u.id}
              className='flex items-center justify-between p-2 rounded-lg bg-content1'
            >
              <UserPopoverCard userId={u.id}>
                <div className='flex items-center gap-2 min-w-0 cursor-pointer'>
                  <OnlineStatusDot
                    isOnline={u.is_online}
                    lastSeen={u.last_seen}
                  />
                  <span className='text-sm truncate hover:underline'>
                    {u.display_name}
                  </span>
                  <span className='text-xs text-default-400'>
                    @{u.username}
                  </span>
                </div>
              </UserPopoverCard>
              <div className='flex items-center gap-2 flex-shrink-0'>
                {canEdit ? (
                  <Select
                    size='sm'
                    variant='bordered'
                    className='w-28'
                    selectedKeys={[u.role]}
                    onChange={(e) => handleChangeRole(u.id, e.target.value)}
                    aria-label='Role'
                    items={roleItems}
                  >
                    {(item) => (
                      <SelectItem key={item.key}>{item.label}</SelectItem>
                    )}
                  </Select>
                ) : (
                  <span className='text-xs text-default-400 capitalize'>
                    {u.role}
                  </span>
                )}
              </div>
            </div>
          );
        })}
        {users.length === 0 && (
          <p className='text-default-500 text-sm'>No users found.</p>
        )}
      </div>
    </div>
  );
}
