import { IconRail } from './IconRail';
import { SidePanel } from './SidePanel';

interface Props {
  onCreateConversation: () => void;
  onCreateChannel: () => void;
  onBrowseChannels: () => void;
  onBrowseSpaces: () => void;
  onShowSpaceSettings: () => void;
  open: boolean;
  onClose: () => void;
}

export function NewSidebar({
  onCreateConversation,
  onCreateChannel,
  onBrowseChannels,
  onBrowseSpaces,
  onShowSpaceSettings,
  open,
  onClose,
}: Props) {
  return (
    <>
      {open && (
        <div
          className='fixed inset-0 bg-black/50 z-30 md:hidden'
          onClick={onClose}
        />
      )}
      <aside
        className={`fixed inset-y-0 left-0 z-40 w-72 bg-background/95 border-r border-default-100 flex transform transition-transform duration-200 ease-in-out md:static md:translate-x-0 ${
          open ? 'translate-x-0' : '-translate-x-full'
        }`}
      >
        <IconRail onBrowseSpaces={onBrowseSpaces} />
        <div className='flex-1 flex flex-col overflow-hidden'>
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
