import { useState } from 'react';
import { Button, Textarea, Dropdown, DropdownTrigger, DropdownMenu, DropdownItem } from '@heroui/react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faEllipsis, faPencil, faTrashCan } from '@fortawesome/free-solid-svg-icons';
import type { Message } from '../../types';
import { useChatStore } from '../../stores/chatStore';

interface Props {
  message: Message;
  onEdit?: (messageId: string, content: string) => void;
  onDelete?: (messageId: string) => void;
}

export function MessageBubble({ message, onEdit, onDelete }: Props) {
  const currentUser = useChatStore((s) => s.user);
  const isOwn = currentUser?.id === message.user_id;
  const [editing, setEditing] = useState(false);
  const [editContent, setEditContent] = useState(message.content);
  const [menuOpen, setMenuOpen] = useState(false);

  const time = new Date(message.created_at).toLocaleTimeString([], {
    hour: '2-digit',
    minute: '2-digit',
  });

  const handleSaveEdit = () => {
    const trimmed = editContent.trim();
    if (trimmed && trimmed !== message.content) {
      onEdit?.(message.id, trimmed);
    }
    setEditing(false);
  };

  const handleDelete = () => {
    if (confirm('Delete this message?')) {
      onDelete?.(message.id);
    }
  };

  if (message.is_deleted) {
    return (
      <div className={`flex ${isOwn ? 'justify-end' : 'justify-start'} mb-2`}>
        <div className="max-w-[85%] sm:max-w-[70%] rounded-2xl px-4 py-2 bg-content1 border border-divider rounded-br-md">
          {!isOwn && (
            <p className="text-xs font-semibold text-default-400 mb-1">{message.username}</p>
          )}
          <p className="text-sm italic text-default-400">This message was deleted</p>
          <p className="text-xs mt-1 text-default-300">{time}</p>
        </div>
      </div>
    );
  }

  return (
    <div className={`flex ${isOwn ? 'justify-end' : 'justify-start'} mb-2 group`}>
      <div
        className={`max-w-[85%] sm:max-w-[70%] rounded-2xl px-4 py-2 relative ${
          isOwn
            ? 'bg-primary text-primary-foreground rounded-br-md'
            : 'bg-content2 text-foreground rounded-bl-md'
        }`}
      >
        {!isOwn && (
          <p className="text-xs font-semibold text-primary mb-1">{message.username}</p>
        )}

        {editing ? (
          <div className="space-y-2">
            <Textarea
              variant="bordered"
              value={editContent}
              onChange={(e) => setEditContent(e.target.value)}
              minRows={1}
              maxRows={4}
              size="sm"
              classNames={{
                input: 'text-sm',
                inputWrapper: 'bg-background/50',
              }}
              onKeyDown={(e) => {
                if (e.key === 'Enter' && !e.shiftKey) {
                  e.preventDefault();
                  handleSaveEdit();
                }
                if (e.key === 'Escape') {
                  setEditing(false);
                  setEditContent(message.content);
                }
              }}
              autoFocus
            />
            <div className="flex gap-1">
              <Button size="sm" color="primary" variant="solid" onPress={handleSaveEdit}>
                Save
              </Button>
              <Button size="sm" variant="flat" onPress={() => { setEditing(false); setEditContent(message.content); }}>
                Cancel
              </Button>
            </div>
          </div>
        ) : (
          <p className="text-sm whitespace-pre-wrap break-words">{message.content}</p>
        )}

        <p className={`text-xs mt-1 ${isOwn ? 'text-primary-200' : 'text-default-400'}`}>
          {time}
          {message.edited_at && <span className="ml-1">(edited)</span>}
        </p>

        {isOwn && !editing && (
          <div className={`absolute -bottom-2 right-1 ${menuOpen ? 'block' : 'hidden group-hover:block'}`}>
            <Dropdown placement="bottom-end" isOpen={menuOpen} onOpenChange={setMenuOpen}>
              <DropdownTrigger>
                <button
                  className="w-6 h-6 rounded-full bg-content1 border border-divider flex items-center justify-center text-xs hover:bg-content2 text-foreground shadow-sm"
                >
                  <FontAwesomeIcon icon={faEllipsis} />
                </button>
              </DropdownTrigger>
              <DropdownMenu
                aria-label="Message actions"
                onAction={(key) => {
                  if (key === 'edit') {
                    setEditContent(message.content);
                    setEditing(true);
                  } else if (key === 'delete') {
                    handleDelete();
                  }
                }}
              >
                <DropdownItem key="edit" startContent={<FontAwesomeIcon icon={faPencil} />}>Edit</DropdownItem>
                <DropdownItem key="delete" className="text-danger" color="danger" startContent={<FontAwesomeIcon icon={faTrashCan} />}>Delete</DropdownItem>
              </DropdownMenu>
            </Dropdown>
          </div>
        )}
      </div>
    </div>
  );
}
