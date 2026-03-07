import { ChannelList } from '../channels/ChannelList';
import { DMList } from '../channels/DMList';

interface Props {
  onCreateChannel: () => void;
  onStartDM: () => void;
  onBrowseChannels: () => void;
  open: boolean;
  onClose: () => void;
}

export function Sidebar({
  onCreateChannel,
  onStartDM,
  onBrowseChannels,
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
        className={`fixed inset-y-0 left-0 z-40 w-64 bg-background/95 border-r border-default-100 flex flex-col transform transition-transform duration-200 ease-in-out md:static md:translate-x-0 ${
          open ? 'translate-x-0' : '-translate-x-full'
        }`}
      >
        <div className='p-4 border-b border-default-100'>
          <h1 className='text-xl font-bold text-foreground'>Chat</h1>
        </div>
        <div className='flex-1 overflow-y-auto p-2'>
          <ChannelList
            onCreateChannel={onCreateChannel}
            onBrowseChannels={onBrowseChannels}
            onSelect={onClose}
          />
          <DMList onStartDM={onStartDM} onSelect={onClose} />
        </div>
      </aside>
    </>
  );
}
