import { useState, useRef } from 'react';
import { useChatStore } from '../../stores/chatStore';
import { MessageList } from '../chat/MessageList';
import { MessageInput } from '../chat/MessageInput';
import { useWebSocket } from '../../hooks/useWebSocket';
import { uploadFile } from '../../services/api';

export function ChatArea() {
  const activeChannelId = useChatStore((s) => s.activeChannelId);
  const channels = useChatStore((s) => s.channels);
  const uploadProgress = useChatStore((s) => s.uploadProgress);
  const setUploadProgress = useChatStore((s) => s.setUploadProgress);
  const [uploadError, setUploadError] = useState<string | null>(null);
  const errorTimer = useRef<ReturnType<typeof setTimeout>>(undefined);
  const { sendMessage, sendTyping, editMessage, deleteMessage, markRead } =
    useWebSocket();

  const serverArchived = useChatStore((s) => s.serverArchived);
  const activeChannel = channels.find((c) => c.id === activeChannelId);
  const isArchived = activeChannel?.is_archived || serverArchived;
  const canWrite =
    !isArchived &&
    (activeChannel?.my_role === 'admin' ||
      activeChannel?.my_role === 'owner' ||
      activeChannel?.my_role === 'write');

  const handleUpload = async (file: File, message: string) => {
    if (!activeChannelId) return;
    setUploadError(null);
    clearTimeout(errorTimer.current);
    setUploadProgress(0);
    try {
      await uploadFile(activeChannelId, file, message, (p) =>
        setUploadProgress(p),
      );
    } catch (e) {
      const msg = e instanceof Error ? e.message : 'Upload failed';
      setUploadError(`Upload failed: ${msg}`);
      errorTimer.current = setTimeout(() => setUploadError(null), 8000);
    } finally {
      setUploadProgress(null);
    }
  };

  if (!activeChannelId) {
    return (
      <div className="flex-1 flex items-center justify-center bg-background text-default-400">
        <div className="text-center">
          <p className="text-2xl mb-2">Welcome to Isle Chat</p>
          <p>Select a channel or start a conversation</p>
        </div>
      </div>
    );
  }

  return (
    <div className="flex-1 flex flex-col bg-background">
      <MessageList
        channelId={activeChannelId}
        onEditMessage={editMessage}
        onDeleteMessage={deleteMessage}
        onMarkRead={markRead}
      />
      {canWrite ? (
        <MessageInput
          onSend={(content) => sendMessage(activeChannelId, content)}
          onTyping={() => sendTyping(activeChannelId)}
          onUpload={handleUpload}
          uploadProgress={uploadProgress}
          uploadError={uploadError}
        />
      ) : (
        <div className="border-t border-default-100 p-4 text-center text-default-400 text-sm">
          {serverArchived
            ? 'The server is archived — no new messages can be sent'
            : isArchived
              ? 'This channel is archived — no new messages can be sent'
              : 'You have read-only access to this channel'}
        </div>
      )}
    </div>
  );
}
