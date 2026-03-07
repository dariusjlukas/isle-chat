import type { ChannelMemberInfo } from '../../types';

export interface MentionOption {
  type: 'channel' | 'user';
  label: string;
  value: string;
  member?: ChannelMemberInfo;
}

export function getFilteredOptions(
  query: string,
  members: ChannelMemberInfo[],
  currentUserId: string,
): MentionOption[] {
  const q = query.toLowerCase();
  const options: MentionOption[] = [];

  if ('channel'.startsWith(q)) {
    options.push({ type: 'channel', label: '@channel', value: 'channel' });
  }

  const filteredMembers = members
    .filter(
      (m) =>
        m.id !== currentUserId &&
        (m.username.toLowerCase().startsWith(q) ||
          m.display_name.toLowerCase().startsWith(q)),
    )
    .sort((a, b) => {
      if (a.is_online !== b.is_online) return a.is_online ? -1 : 1;
      return a.username.localeCompare(b.username);
    });

  for (const m of filteredMembers) {
    options.push({
      type: 'user',
      label: `@${m.username}`,
      value: m.username,
      member: m,
    });
  }

  return options.slice(0, 10);
}
