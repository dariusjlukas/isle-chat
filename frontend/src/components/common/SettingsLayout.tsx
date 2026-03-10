import { useState } from 'react';
import {
  Modal,
  ModalContent,
  ModalHeader,
  ModalBody,
  Select,
  SelectItem,
} from '@heroui/react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import type { IconDefinition } from '@fortawesome/fontawesome-svg-core';
import { SettingsNavContext } from './settingsNavContext';

export interface SettingsCategory {
  key: string;
  label: string;
  icon?: IconDefinition;
  badge?: number;
  className?: string;
  content: React.ReactNode;
}

interface Props {
  isOpen: boolean;
  onClose: () => void;
  title: string;
  categories: SettingsCategory[];
  defaultCategory?: string;
}

export function SettingsLayout({
  isOpen,
  onClose,
  title,
  categories,
  defaultCategory,
}: Props) {
  const [activeKey, setActiveKey] = useState<string>(
    defaultCategory ?? categories[0]?.key ?? '',
  );

  const activeCategory = categories.find((c) => c.key === activeKey);

  return (
    <SettingsNavContext.Provider value={setActiveKey}>
      <Modal
        isOpen={isOpen}
        onOpenChange={(open) => {
          if (!open) onClose();
        }}
        size='4xl'
        backdrop='opaque'
        classNames={{
          body: 'p-0',
        }}
      >
        <ModalContent>
          <ModalHeader>{title}</ModalHeader>
          <ModalBody>
            <div className='flex h-[min(70vh,600px)]'>
              <nav className='w-48 flex-shrink-0 border-r border-default-200 p-2 hidden sm:flex flex-col gap-0.5'>
                {categories.map((cat) => {
                  const isActive = cat.key === activeKey;
                  return (
                    <button
                      key={cat.key}
                      type='button'
                      onClick={() => setActiveKey(cat.key)}
                      className={`flex items-center gap-2 w-full px-3 py-2 rounded-lg text-sm text-left transition-colors ${
                        isActive
                          ? 'border border-primary bg-primary/10 text-primary font-medium'
                          : 'border border-transparent hover:bg-default/40 text-foreground'
                      } ${cat.className ?? ''}`}
                    >
                      {cat.icon && (
                        <FontAwesomeIcon
                          icon={cat.icon}
                          className='w-4 flex-shrink-0'
                        />
                      )}
                      <span className='truncate flex-1'>{cat.label}</span>
                      {cat.badge != null && cat.badge > 0 && (
                        <span className='bg-danger text-white text-[10px] font-bold rounded-full min-w-[18px] h-[18px] flex items-center justify-center px-1'>
                          {cat.badge}
                        </span>
                      )}
                    </button>
                  );
                })}
              </nav>

              <div className='flex-1 flex flex-col min-h-0'>
                <div className='sm:hidden px-3 pt-2 pb-1 border-b border-default-200'>
                  <Select
                    aria-label={`${title} category`}
                    selectedKeys={[activeKey]}
                    onChange={(e) => {
                      if (e.target.value) setActiveKey(e.target.value);
                    }}
                    variant='bordered'
                    size='sm'
                  >
                    {categories.map((cat) => (
                      <SelectItem key={cat.key}>
                        {cat.badge && cat.badge > 0
                          ? `${cat.label} (${cat.badge})`
                          : cat.label}
                      </SelectItem>
                    ))}
                  </Select>
                </div>

                <div className='flex-1 overflow-y-auto p-4 sm:p-6'>
                  {activeCategory?.content}
                </div>
              </div>
            </div>
          </ModalBody>
        </ModalContent>
      </Modal>
    </SettingsNavContext.Provider>
  );
}
