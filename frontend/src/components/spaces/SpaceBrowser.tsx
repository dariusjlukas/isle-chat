import { useState, useEffect } from 'react';
import {
  Modal,
  ModalContent,
  ModalHeader,
  ModalBody,
  ModalFooter,
  Input,
  Button,
  Card,
  CardBody,
} from '@heroui/react';
import * as api from '../../services/api';
import { useChatStore } from '../../stores/chatStore';

interface Props {
  onClose: () => void;
  onCreateSpace: () => void;
}

export function SpaceBrowser({ onClose, onCreateSpace }: Props) {
  const [search, setSearch] = useState('');
  const [spaces, setSpaces] = useState<
    Array<{
      id: string;
      name: string;
      description: string;
      icon: string;
      is_public: boolean;
      default_role: string;
      created_at: string;
    }>
  >([]);
  const [loading, setLoading] = useState(true);
  const setActiveView = useChatStore((s) => s.setActiveView);

  useEffect(() => {
    let cancelled = false;
    api
      .listPublicSpaces(search || undefined)
      .then((result) => {
        if (!cancelled) setSpaces(result);
      })
      .catch(() => {})
      .finally(() => {
        if (!cancelled) setLoading(false);
      });
    return () => {
      cancelled = true;
    };
  }, [search]);

  const handleJoin = async (spaceId: string) => {
    try {
      await api.joinSpace(spaceId);
      const allSpaces = await api.listSpaces();
      useChatStore.getState().setSpaces(allSpaces);
      setActiveView({ type: 'space', spaceId });
      onClose();
    } catch (e) {
      console.error('Space join failed:', e);
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
        <ModalHeader>Browse Spaces</ModalHeader>
        <ModalBody className='pb-4'>
          <Input
            placeholder='Search spaces...'
            value={search}
            onValueChange={setSearch}
            variant='bordered'
          />
          <div className='space-y-2'>
            {spaces.map((sp) => (
              <Card key={sp.id} shadow='sm'>
                <CardBody className='flex flex-row items-center justify-between gap-4'>
                  <div className='min-w-0'>
                    <p className='font-semibold text-foreground'>
                      {sp.icon && <span className='mr-1'>{sp.icon}</span>}
                      {sp.name}
                    </p>
                    {sp.description && (
                      <p className='text-sm text-default-400 truncate'>
                        {sp.description}
                      </p>
                    )}
                  </div>
                  <Button
                    size='sm'
                    color='primary'
                    className='flex-shrink-0'
                    onPress={() => handleJoin(sp.id)}
                  >
                    Join
                  </Button>
                </CardBody>
              </Card>
            ))}
          </div>
          {spaces.length === 0 && !loading && (
            <p className='text-center text-default-400 py-4'>
              No public spaces available to join
            </p>
          )}
        </ModalBody>
        <ModalFooter>
          <Button variant='light' color='default' onPress={onClose}>
            Close
          </Button>
          <Button
            color='primary'
            variant='ghost'
            onPress={() => {
              onClose();
              onCreateSpace();
            }}
          >
            Create New Space
          </Button>
        </ModalFooter>
      </ModalContent>
    </Modal>
  );
}
