import { Modal, ModalContent, ModalHeader, ModalBody } from '@heroui/react';
import { ServerSettings } from './ServerSettings';

interface Props {
  onComplete: () => void;
}

export function SetupWizard({ onComplete }: Props) {
  return (
    <Modal
      isOpen
      isDismissable={false}
      hideCloseButton
      size='3xl'
      scrollBehavior='inside'
    >
      <ModalContent>
        <ModalHeader>
          <div>
            <h2 className='text-xl font-bold'>Welcome! Server Setup</h2>
            <p className='text-sm text-default-500 font-normal mt-1'>
              Configure your server settings to get started.
            </p>
          </div>
        </ModalHeader>
        <ModalBody className='pb-6'>
          <ServerSettings isSetup onComplete={onComplete} />
        </ModalBody>
      </ModalContent>
    </Modal>
  );
}
