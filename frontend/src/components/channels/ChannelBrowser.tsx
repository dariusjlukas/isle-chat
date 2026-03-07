import { useState, useEffect } from 'react';
import {
  Modal,
  ModalContent,
  ModalHeader,
  ModalBody,
  Input,
  Button,
  Card,
  CardBody,
  Chip,
} from '@heroui/react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
  faHashtag,
  faLock,
  faBoxArchive,
} from '@fortawesome/free-solid-svg-icons';
import * as api from '../../services/api';
import { useChatStore } from '../../stores/chatStore';

interface Props {
  onClose: () => void;
  spaceId?: string;
}

export function ChannelBrowser({ onClose, spaceId }: Props) {
  const [search, setSearch] = useState('');
  const [channels, setChannels] = useState<
    Array<{
      id: string;
      name: string;
      description: string;
      is_public: boolean;
      is_archived: boolean;
      default_role: string;
      created_at: string;
    }>
  >([]);
  const [loading, setLoading] = useState(false);
  const setActiveChannel = useChatStore((s) => s.setActiveChannel);

  useEffect(() => {
    let cancelled = false;
    api
      .listPublicChannels(search || undefined, spaceId)
      .then((result) => {
        if (!cancelled) setChannels(result);
      })
      .catch(() => {})
      .finally(() => {
        if (!cancelled) setLoading(false);
      });
    return () => {
      cancelled = true;
    };
  }, [search, spaceId]);

  const handleJoin = async (channelId: string) => {
    try {
      await api.joinChannel(channelId);
      const allChannels = await api.listChannels();
      useChatStore.getState().setChannels(allChannels);
      setActiveChannel(channelId);
      onClose();
    } catch (e) {
      console.error('Channel join failed:', e);
    }
  };

  return (
    <Modal
      isOpen
      onOpenChange={(open) => {
        if (!open) onClose();
      }}
      size='lg'
      scrollBehavior='inside'
      backdrop='opaque'
    >
      <ModalContent>
        <ModalHeader>
          {spaceId ? 'Browse Channels' : 'Browse Public Channels'}
        </ModalHeader>
        <ModalBody className='pb-6'>
          <Input
            placeholder='Search channels...'
            value={search}
            onValueChange={setSearch}
            variant='bordered'
          />
          <div className='space-y-2'>
            {channels.map((ch) => (
              <Card key={ch.id} shadow='sm'>
                <CardBody className='flex flex-row items-center justify-between gap-4'>
                  <div className='min-w-0'>
                    <p className='font-semibold text-foreground flex items-center gap-1.5'>
                      <FontAwesomeIcon
                        icon={ch.is_public ? faHashtag : faLock}
                        className='text-xs'
                      />
                      {ch.name}
                      {ch.is_archived && (
                        <Chip
                          size='sm'
                          variant='flat'
                          color='warning'
                          startContent={
                            <FontAwesomeIcon
                              icon={faBoxArchive}
                              className='text-[10px]'
                            />
                          }
                        >
                          Archived
                        </Chip>
                      )}
                    </p>
                    {ch.description && (
                      <p className='text-sm text-default-400 truncate'>
                        {ch.description}
                      </p>
                    )}
                    <p className='text-xs text-default-300'>
                      Default role: {ch.default_role}
                    </p>
                  </div>
                  <Button
                    size='sm'
                    color='primary'
                    className='flex-shrink-0'
                    onPress={() => handleJoin(ch.id)}
                  >
                    Join
                  </Button>
                </CardBody>
              </Card>
            ))}
          </div>
          {channels.length === 0 && !loading && (
            <p className='text-center text-default-400 py-4'>
              No channels available to join
            </p>
          )}
        </ModalBody>
      </ModalContent>
    </Modal>
  );
}
