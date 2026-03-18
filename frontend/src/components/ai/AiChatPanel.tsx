import { useEffect } from 'react';
import { Button } from '@heroui/react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faHexagonNodes, faXmark } from '@fortawesome/free-solid-svg-icons';
import { useAiStore } from '../../stores/aiStore';
import * as api from '../../services/api';
import { AiMessageList } from './AiMessageList';
import { AiMessageInput } from './AiMessageInput';

interface Props {
  onClose: () => void;
  width?: number;
}

export function AiChatPanel({ onClose, width = 380 }: Props) {
  const activeConversationId = useAiStore((s) => s.activeConversationId);
  const setMessages = useAiStore((s) => s.setMessages);
  const addConversation = useAiStore((s) => s.addConversation);
  const setActiveConversation = useAiStore((s) => s.setActiveConversation);

  // Load conversation messages when active conversation changes
  useEffect(() => {
    if (!activeConversationId) return;
    api
      .getAiConversation(activeConversationId)
      .then((data) => {
        setMessages(activeConversationId, data.messages);
      })
      .catch((err) => console.error('Failed to load conversation:', err));
  }, [activeConversationId, setMessages]);

  const handleNewConversation = async () => {
    try {
      const convo = await api.createAiConversation();
      addConversation(convo);
      setActiveConversation(convo.id);
    } catch (err) {
      console.error('Failed to create conversation:', err);
    }
  };

  return (
    <div className='w-[380px] lg:w-[440px] flex-shrink-0 border-l border-divider bg-background flex flex-col h-full'>
      {/* Header */}
      <div className='flex items-center justify-between px-3 py-2 border-b border-divider bg-content1'>
        <div className='flex items-center gap-2'>
          <FontAwesomeIcon
            icon={faHexagonNodes}
            className='text-primary text-sm'
          />
          <span className='font-medium text-sm'>AI Assistant</span>
        </div>
        <Button
          isIconOnly
          variant='light'
          size='sm'
          onPress={onClose}
          className='text-default-500'
        >
          <FontAwesomeIcon icon={faXmark} className='text-sm' />
        </Button>
      </div>

      {activeConversationId ? (
        <>
          <AiMessageList conversationId={activeConversationId} />
          <AiMessageInput conversationId={activeConversationId} />
        </>
      ) : (
        <div className='flex-1 flex items-center justify-center'>
          <div className='text-center space-y-3 px-6'>
            <FontAwesomeIcon
              icon={faHexagonNodes}
              className='text-4xl text-default-300'
            />
            <p className='text-default-500 text-sm font-medium'>AI Assistant</p>
            <p className='text-default-400 text-xs'>
              Select a conversation from the sidebar, or start a new one.
            </p>
            <Button
              color='primary'
              size='sm'
              variant='bordered'
              onPress={handleNewConversation}
            >
              New Conversation
            </Button>
          </div>
        </div>
      )}
    </div>
  );
}
