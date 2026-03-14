export interface User {
  id: string;
  username: string;
  display_name: string;
  role: 'owner' | 'admin' | 'user';
  is_online: boolean;
  last_seen?: string;
  bio: string;
  status: string;
  avatar_file_id: string;
  profile_color: string;
  has_password?: boolean;
  has_totp?: boolean;
}

export type ChannelRole = 'owner' | 'admin' | 'write' | 'read';

export interface ChannelMemberInfo {
  id: string;
  username: string;
  display_name: string;
  is_online: boolean;
  last_seen?: string;
  role: ChannelRole;
}

export interface Channel {
  id: string;
  name: string;
  description: string;
  is_direct: boolean;
  is_public: boolean;
  default_role: ChannelRole;
  default_join?: boolean;
  my_role: ChannelRole;
  created_at: string;
  is_archived?: boolean;
  members: ChannelMemberInfo[];
  space_id?: string;
  conversation_name?: string;
}

export interface Space {
  id: string;
  name: string;
  description: string;
  is_public: boolean;
  default_role: ChannelRole;
  is_archived?: boolean;
  avatar_file_id?: string;
  profile_color?: string;
  my_role: ChannelRole;
  created_at: string;
  members: ChannelMemberInfo[];
  enabled_tools?: string[];
}

export type SpaceToolName = 'files' | 'calendar' | 'tasks';

export type SidebarView =
  | { type: 'space'; spaceId: string }
  | { type: 'messages' };

export interface Reaction {
  emoji: string;
  user_id: string;
  username: string;
}

export interface Message {
  id: string;
  channel_id: string;
  user_id: string;
  username: string;
  content: string;
  created_at: string;
  edited_at?: string;
  is_deleted?: boolean;
  file_id?: string;
  file_name?: string;
  file_size?: number;
  file_type?: string;
  reactions?: Reaction[];
  reply_to_message_id?: string;
  reply_to_username?: string;
  reply_to_content?: string;
  reply_to_is_deleted?: boolean;
}

export interface ReadReceiptInfo {
  username: string;
  last_read_message_id: string;
  last_read_at: string;
}

export interface SpaceInvite {
  id: string;
  space_id: string;
  space_name: string;
  invited_by_username: string;
  role: ChannelRole;
  created_at: string;
}

export interface Notification {
  id: string;
  user_id: string;
  type:
    | 'mention'
    | 'reply'
    | 'direct_message'
    | 'space_invite'
    | 'join_request';
  source_user_id: string;
  source_username: string;
  channel_id: string;
  channel_name: string;
  message_id: string;
  space_id: string;
  content: string;
  created_at: string;
  is_read: boolean;
}

export interface SpaceFile {
  id: string;
  space_id: string;
  parent_id: string | null;
  name: string;
  is_folder: boolean;
  file_size: number;
  mime_type: string;
  created_by: string;
  created_by_username: string;
  created_at: string;
  updated_at: string;
  my_permission?: string;
}

export interface SpaceFilePath {
  id: string;
  name: string;
}

export interface SpaceFilePermission {
  id: string;
  file_id: string;
  user_id: string;
  username: string;
  display_name: string;
  permission: string;
  granted_by: string;
  granted_by_username: string;
  created_at: string;
}

export interface SpaceFileVersion {
  id: string;
  file_id: string;
  version_number: number;
  file_size: number;
  mime_type: string;
  uploaded_by: string;
  uploaded_by_username: string;
  created_at: string;
}

export interface CalendarEvent {
  id: string;
  space_id: string;
  title: string;
  description: string;
  location: string;
  color: string;
  start_time: string;
  end_time: string;
  all_day: boolean;
  rrule: string;
  occurrence_date?: string | null;
  is_exception?: boolean;
  created_by: string;
  created_by_username: string;
  created_at: string;
  updated_at: string;
  my_rsvp?: string;
}

export interface CalendarEventRsvp {
  user_id: string;
  username: string;
  display_name: string;
  status: string;
  responded_at: string;
}

export interface CalendarPermission {
  id: string;
  space_id: string;
  user_id: string;
  username: string;
  display_name: string;
  permission: string;
  granted_by: string;
  granted_by_username: string;
  created_at: string;
}

export interface InviteToken {
  id: string;
  token: string;
  created_by: string;
  used: boolean;
  expires_at: string;
  created_at: string;
}

export interface JoinRequest {
  id: string;
  username: string;
  display_name: string;
  status: 'pending' | 'approved' | 'denied';
  created_at: string;
}
