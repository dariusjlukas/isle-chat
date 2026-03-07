import { useEffect, useRef } from 'react';
import Picker from '@emoji-mart/react';
import data from '@emoji-mart/data';

interface Props {
  onSelect: (emoji: string) => void;
  onClose: () => void;
}

export function EmojiPickerPopup({ onSelect, onClose }: Props) {
  const containerRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const handleClickOutside = (e: MouseEvent) => {
      if (
        containerRef.current &&
        !containerRef.current.contains(e.target as Node)
      ) {
        onClose();
      }
    };
    document.addEventListener('mousedown', handleClickOutside);
    return () => document.removeEventListener('mousedown', handleClickOutside);
  }, [onClose]);

  return (
    <div ref={containerRef} className='absolute bottom-full left-0 mb-2 z-50'>
      <Picker
        data={data}
        onEmojiSelect={(emoji: { native: string }) => onSelect(emoji.native)}
        theme='auto'
        previewPosition='none'
        skinTonePosition='search'
        maxFrequentRows={2}
      />
    </div>
  );
}
