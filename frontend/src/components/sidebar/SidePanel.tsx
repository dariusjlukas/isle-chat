import { useChatStore } from '../../stores/chatStore';
import { ConversationList } from '../conversations/ConversationList';
import { SpacePanel } from '../spaces/SpacePanel';
import { AiConversationList } from '../ai/AiConversationList';

interface Props {
  onCreateConversation: () => void;
  onCreateChannel: () => void;
  onBrowseChannels: () => void;
  onShowSpaceSettings: () => void;
  onSelect?: () => void;
}

export function SidePanel({
  onCreateConversation,
  onCreateChannel,
  onBrowseChannels,
  onShowSpaceSettings,
  onSelect,
}: Props) {
  const activeView = useChatStore((s) => s.activeView);

  if (!activeView) {
    return (
      <div className='flex-1 flex items-center justify-center text-default-400 text-sm p-4 text-center'>
        Select a space or messages
      </div>
    );
  }

  if (activeView.type === 'messages') {
    return (
      <ConversationList
        onCreateConversation={onCreateConversation}
        onSelect={onSelect}
      />
    );
  }

  if (activeView.type === 'ai') {
    return <AiConversationList />;
  }

  return (
    <SpacePanel
      spaceId={activeView.spaceId}
      onCreateChannel={onCreateChannel}
      onBrowseChannels={onBrowseChannels}
      onShowSettings={onShowSpaceSettings}
      onSelect={onSelect}
    />
  );
}
