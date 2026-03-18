import { IconRail } from './IconRail';
import { SidePanel } from './SidePanel';
import { useChatStore } from '../../stores/chatStore';

interface Props {
  onCreateConversation: () => void;
  onCreateChannel: () => void;
  onBrowseChannels: () => void;
  onBrowseSpaces: () => void;
  onShowSpaceSettings: () => void;
  onSharedWithMe?: () => void;
  open: boolean;
  onClose: () => void;
  width?: number;
  isResizing?: boolean;
}

export function NewSidebar({
  onCreateConversation,
  onCreateChannel,
  onBrowseChannels,
  onBrowseSpaces,
  onShowSpaceSettings,
  onSharedWithMe,
  open,
  onClose,
  width = 288,
  isResizing = false,
}: Props) {
  const sidePanelCollapsed = useChatStore((s) => s.sidePanelCollapsed);

  return (
    <>
      {open && (
        <div
          className='fixed inset-0 bg-black/50 z-30 md:hidden'
          onClick={onClose}
        />
      )}
      <aside
        className={`fixed inset-y-0 left-0 z-40 bg-background/95 border-r border-default-100 flex transform ${
          isResizing ? '' : 'transition-all duration-200 ease-in-out'
        } md:static md:translate-x-0 ${
          open ? 'translate-x-0' : '-translate-x-full'
        }`}
        style={{ width: sidePanelCollapsed ? 64 : width }}
      >
        <IconRail
          onBrowseSpaces={onBrowseSpaces}
          onSharedWithMe={onSharedWithMe}
        />
        <div
          className={`flex flex-col overflow-hidden ${
            isResizing ? '' : 'transition-all duration-200'
          } ${sidePanelCollapsed ? 'w-0 opacity-0' : 'flex-1 opacity-100'}`}
        >
          <SidePanel
            onCreateConversation={onCreateConversation}
            onCreateChannel={onCreateChannel}
            onBrowseChannels={onBrowseChannels}
            onShowSpaceSettings={onShowSpaceSettings}
            onSelect={onClose}
          />
        </div>
      </aside>
    </>
  );
}
