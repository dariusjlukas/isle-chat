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
export type SpaceRole = 'owner' | 'admin' | 'user';

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

export interface SpaceMemberInfo {
  id: string;
  username: string;
  display_name: string;
  is_online: boolean;
  last_seen?: string;
  role: SpaceRole;
}

export interface Space {
  id: string;
  name: string;
  description: string;
  is_public: boolean;
  default_role: SpaceRole;
  is_archived?: boolean;
  avatar_file_id?: string;
  profile_color?: string;
  is_personal?: boolean;
  personal_owner_id?: string;
  my_role: SpaceRole;
  created_at: string;
  members: SpaceMemberInfo[];
  enabled_tools?: string[];
  allowed_tools?: string[];
}

export type SpaceToolName =
  | 'files'
  | 'calendar'
  | 'tasks'
  | 'wiki'
  | 'minigames';

export interface WikiPage {
  id: string;
  space_id: string;
  parent_id: string | null;
  title: string;
  slug: string;
  is_folder: boolean;
  content: string;
  content_text: string;
  icon: string;
  cover_image_file_id: string;
  position: number;
  created_by: string;
  created_by_username: string;
  created_at: string;
  updated_at: string;
  last_edited_by: string;
  last_edited_by_username: string;
  my_permission?: string;
}

export interface WikiPageVersion {
  id: string;
  page_id: string;
  version_number: number;
  title: string;
  content: string;
  content_text: string;
  edited_by: string;
  edited_by_username: string;
  created_at: string;
}

export interface WikiPagePermission {
  id: string;
  page_id: string;
  user_id: string;
  username: string;
  display_name: string;
  permission: string;
  granted_by: string;
  granted_by_username: string;
  created_at: string;
}

export interface WikiPermission {
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

export interface WikiSearchResult {
  id: string;
  space_id: string;
  space_name: string;
  title: string;
  snippet: string;
  created_at: string;
  created_by_username: string;
}

export type SidebarView =
  | { type: 'space'; spaceId: string }
  | { type: 'messages' }
  | { type: 'ai' };

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
  role: SpaceRole;
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

export interface TaskBoard {
  id: string;
  space_id: string;
  name: string;
  description: string;
  created_by: string;
  created_by_username: string;
  created_at: string;
  updated_at: string;
  columns?: TaskColumn[];
  tasks?: TaskItem[];
  board_labels?: TaskLabel[];
  dependencies?: TaskDependency[];
  my_permission?: string;
}

export interface TaskColumn {
  id: string;
  board_id: string;
  name: string;
  position: number;
  wip_limit: number;
  color: string;
  created_at: string;
}

export interface TaskItem {
  id: string;
  board_id: string;
  column_id: string;
  title: string;
  description: string;
  priority: 'low' | 'medium' | 'high' | 'critical';
  due_date: string;
  start_date: string;
  duration_days: number;
  color: string;
  position: number;
  created_by: string;
  created_by_username: string;
  created_at: string;
  updated_at: string;
  assignees?: TaskAssignee[];
  labels?: TaskLabel[];
  checklists?: TaskChecklist[];
  activity?: TaskActivity[];
  my_permission?: string;
}

export interface TaskDependency {
  id: string;
  task_id: string;
  depends_on_id: string;
  dependency_type: string;
  created_at: string;
}

export interface TaskAssignee {
  user_id: string;
  username: string;
  display_name: string;
}

export interface TaskLabel {
  id: string;
  board_id: string;
  name: string;
  color: string;
}

export interface TaskChecklist {
  id: string;
  task_id: string;
  title: string;
  position: number;
  items?: TaskChecklistItem[];
}

export interface TaskChecklistItem {
  id: string;
  checklist_id: string;
  content: string;
  is_checked: boolean;
  position: number;
}

export interface TaskActivity {
  id: string;
  task_id: string;
  user_id: string;
  username: string;
  display_name: string;
  action: string;
  details: string;
  created_at: string;
}

export interface TaskBoardPermission {
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

export interface SharedResource {
  id: string;
  name: string;
  space_id: string;
  owner_username: string;
  permission: string;
}

export interface SharedWithMe {
  files: SharedResource[];
  wiki_pages: SharedResource[];
  calendar_events: SharedResource[];
  task_boards: SharedResource[];
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

export interface AiConversation {
  id: string;
  title: string;
  created_at: string;
  updated_at: string;
}

export interface AiMessage {
  id: string;
  conversation_id: string;
  role: 'user' | 'assistant' | 'system' | 'tool';
  content: string;
  tool_calls?: {
    id: string;
    type: 'function';
    function: { name: string; arguments: string };
  }[];
  tool_call_id?: string;
  tool_name?: string;
  created_at: string;
}

export interface AiToolUse {
  tool_name: string;
  arguments: Record<string, unknown>;
  result: unknown;
  status: 'success' | 'error';
}
