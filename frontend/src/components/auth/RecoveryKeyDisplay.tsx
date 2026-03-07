import { useState } from 'react';
import {
  Modal,
  ModalContent,
  ModalHeader,
  ModalBody,
  ModalFooter,
  Button,
  Checkbox,
  Alert,
} from '@heroui/react';

interface Props {
  keys: string[];
  onDone: () => void;
}

export function RecoveryKeyDisplay({ keys, onDone }: Props) {
  const [saved, setSaved] = useState(false);

  const handleCopy = () => {
    navigator.clipboard.writeText(keys.join('\n'));
  };

  return (
    <Modal isOpen isDismissable={false} hideCloseButton size='lg'>
      <ModalContent>
        <ModalHeader>Recovery Keys</ModalHeader>
        <ModalBody>
          <Alert color='warning' variant='flat' className='mb-4'>
            These keys will only be shown once. Each key can only be used once.
            Save them somewhere safe.
          </Alert>

          <div className='grid grid-cols-2 gap-2'>
            {keys.map((key, i) => (
              <div
                key={i}
                className='font-mono text-sm bg-default-100 rounded-lg px-3 py-2 text-center'
              >
                {key}
              </div>
            ))}
          </div>

          <Button
            variant='bordered'
            fullWidth
            onPress={handleCopy}
            className='mt-3'
          >
            Copy all to clipboard
          </Button>

          <Checkbox
            isSelected={saved}
            onValueChange={setSaved}
            className='mt-2'
          >
            I have saved these recovery keys
          </Checkbox>
        </ModalBody>
        <ModalFooter>
          <Button
            color='primary'
            isDisabled={!saved}
            onPress={onDone}
            fullWidth
          >
            Continue
          </Button>
        </ModalFooter>
      </ModalContent>
    </Modal>
  );
}
