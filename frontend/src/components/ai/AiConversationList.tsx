import { useState, useEffect, useRef } from 'react';
import {
  Button,
  Dropdown,
  DropdownTrigger,
  DropdownMenu,
  DropdownItem,
  Input,
} from '@heroui/react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
  faEllipsis,
  faHexagonNodes,
  faPencil,
  faTrashCan,
} from '@fortawesome/free-solid-svg-icons';
import { useAiStore } from '../../stores/aiStore';
import * as api from '../../services/api';

interface Props {
  onSelect?: () => void;
}

export function AiConversationList({ onSelect }: Props) {
  const conversations = useAiStore((s) => s.conversations);
  const activeConversationId = useAiStore((s) => s.activeConversationId);
  const setConversations = useAiStore((s) => s.setConversations);
  const addConversation = useAiStore((s) => s.addConversation);
  const removeConversation = useAiStore((s) => s.removeConversation);
  const setActiveConversation = useAiStore((s) => s.setActiveConversation);
  const updateConversationTitle = useAiStore((s) => s.updateConversationTitle);

  const [renamingId, setRenamingId] = useState<string | null>(null);
  const [renameValue, setRenameValue] = useState('');
  const renameInputRef = useRef<HTMLInputElement>(null);

  // Load conversations on mount
  useEffect(() => {
    api
      .listAiConversations()
      .then((convos) => setConversations(convos))
      .catch((err) => console.error('Failed to load AI conversations:', err));
  }, [setConversations]);

  const handleNewConversation = async () => {
    try {
      const convo = await api.createAiConversation();
      addConversation(convo);
      setActiveConversation(convo.id);
      onSelect?.();
    } catch (err) {
      console.error('Failed to create conversation:', err);
    }
  };

  const handleDelete = async (id: string) => {
    try {
      await api.deleteAiConversation(id);
      removeConversation(id);
    } catch (err) {
      console.error('Failed to delete conversation:', err);
    }
  };

  const handleStartRename = (id: string, currentTitle: string) => {
    setRenamingId(id);
    setRenameValue(currentTitle);
    requestAnimationFrame(() => {
      renameInputRef.current?.focus();
    });
  };

  const handleFinishRename = async () => {
    if (!renamingId) return;
    const trimmed = renameValue.trim();
    if (trimmed && trimmed !== '') {
      try {
        await api.updateAiConversationTitle(renamingId, trimmed);
        updateConversationTitle(renamingId, trimmed);
      } catch (err) {
        console.error('Failed to rename conversation:', err);
      }
    }
    setRenamingId(null);
    setRenameValue('');
  };

  const handleRenameKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === 'Enter') {
      e.preventDefault();
      handleFinishRename();
    } else if (e.key === 'Escape') {
      setRenamingId(null);
      setRenameValue('');
    }
  };

  const formatTimestamp = (dateStr: string) => {
    const date = new Date(dateStr);
    const now = new Date();
    const diffMs = now.getTime() - date.getTime();
    const diffDays = Math.floor(diffMs / (1000 * 60 * 60 * 24));

    if (diffDays === 0) {
      return date.toLocaleTimeString([], {
        hour: '2-digit',
        minute: '2-digit',
      });
    }
    if (diffDays === 1) return 'Yesterday';
    if (diffDays < 7) return `${diffDays}d ago`;
    return date.toLocaleDateString([], {
      month: 'short',
      day: 'numeric',
    });
  };

  return (
    <div className='flex flex-col h-full'>
      <div className='p-3 border-b border-default-100'>
        <div className='flex items-center justify-between'>
          <h3 className='text-sm font-semibold text-foreground'>
            AI Conversations
          </h3>
          <Button
            isIconOnly
            variant='light'
            size='sm'
            onPress={handleNewConversation}
            title='New conversation'
          >
            +
          </Button>
        </div>
      </div>

      <div className='flex-1 overflow-y-auto'>
        {conversations.length === 0 && (
          <div className='flex flex-col items-center justify-center p-6 text-default-400 text-sm'>
            <FontAwesomeIcon icon={faHexagonNodes} className='text-2xl mb-2' />
            <p>No conversations yet</p>
          </div>
        )}

        {conversations.map((convo) => {
          const isActive = convo.id === activeConversationId;

          return (
            <div
              key={convo.id}
              className={`flex items-center gap-2 px-3 py-2.5 cursor-pointer border-b border-divider/50 transition-colors group ${
                isActive
                  ? 'bg-primary/10 border-l-2 border-l-primary'
                  : 'hover:bg-content2 border-l-2 border-l-transparent'
              }`}
              onClick={() => {
                setActiveConversation(convo.id);
                onSelect?.();
              }}
            >
              <div className='flex-1 min-w-0'>
                {renamingId === convo.id ? (
                  <Input
                    ref={renameInputRef}
                    size='sm'
                    variant='bordered'
                    value={renameValue}
                    onChange={(e) => setRenameValue(e.target.value)}
                    onBlur={handleFinishRename}
                    onKeyDown={handleRenameKeyDown}
                    classNames={{ input: 'text-sm' }}
                    onClick={(e) => e.stopPropagation()}
                  />
                ) : (
                  <>
                    <p className='text-sm font-medium truncate'>
                      {convo.title}
                    </p>
                    <p className='text-xs text-default-400'>
                      {formatTimestamp(convo.updated_at)}
                    </p>
                  </>
                )}
              </div>

              {renamingId !== convo.id && (
                <div
                  className='opacity-0 group-hover:opacity-100 transition-opacity'
                  onClick={(e) => e.stopPropagation()}
                >
                  <Dropdown placement='bottom-end'>
                    <DropdownTrigger>
                      <button className='w-7 h-7 rounded-md flex items-center justify-center text-default-400 hover:text-foreground hover:bg-content3 transition-colors'>
                        <FontAwesomeIcon
                          icon={faEllipsis}
                          className='text-sm'
                        />
                      </button>
                    </DropdownTrigger>
                    <DropdownMenu
                      aria-label='Conversation actions'
                      onAction={(key) => {
                        if (key === 'rename') {
                          handleStartRename(convo.id, convo.title);
                        } else if (key === 'delete') {
                          handleDelete(convo.id);
                        }
                      }}
                    >
                      <DropdownItem
                        key='rename'
                        startContent={<FontAwesomeIcon icon={faPencil} />}
                      >
                        Rename
                      </DropdownItem>
                      <DropdownItem
                        key='delete'
                        className='text-danger'
                        color='danger'
                        startContent={<FontAwesomeIcon icon={faTrashCan} />}
                      >
                        Delete
                      </DropdownItem>
                    </DropdownMenu>
                  </Dropdown>
                </div>
              )}
            </div>
          );
        })}
      </div>
    </div>
  );
}
