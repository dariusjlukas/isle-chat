import { useState, useRef, useCallback, useEffect, useMemo } from 'react';
import { Button, Progress, Tooltip } from '@heroui/react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
  faPaperclip,
  faXmark,
  faFaceSmile,
} from '@fortawesome/free-solid-svg-icons';
import { formatFileSize } from '../../utils/format';
import { useChatStore } from '../../stores/chatStore';
import { MentionAutocomplete } from './MentionAutocomplete';
import { getFilteredOptions, type MentionOption } from './mentionUtils';
import { EmojiPickerPopup } from './EmojiPickerPopup';

interface Props {
  onSend: (content: string) => void;
  onTyping: () => void;
  onUpload?: (file: File, message: string) => void;
  uploadProgress?: number | null;
  uploadError?: string | null;
}

interface MentionContext {
  query: string;
  startOffset: number;
}

function getMentionContext(node: Node, offset: number): MentionContext | null {
  if (node.nodeType !== Node.TEXT_NODE) return null;
  const text = node.textContent || '';
  const before = text.slice(0, offset);
  const match = before.match(/@(\w*)$/);
  if (!match) return null;
  return {
    query: match[1],
    startOffset: match.index!,
  };
}

function serializeContent(container: HTMLElement): string {
  let result = '';
  for (const node of container.childNodes) {
    if (node.nodeType === Node.TEXT_NODE) {
      result += node.textContent || '';
    } else if (node.nodeType === Node.ELEMENT_NODE) {
      const el = node as HTMLElement;
      if (el.dataset.mentionValue) {
        result += `@${el.dataset.mentionValue}`;
      } else {
        result += el.textContent || '';
      }
    }
  }
  return result;
}

function isContentEmpty(container: HTMLElement): boolean {
  return serializeContent(container).trim().length === 0;
}

export function MessageInput({
  onSend,
  onTyping,
  onUpload,
  uploadProgress,
  uploadError,
}: Props) {
  const [selectedFile, setSelectedFile] = useState<File | null>(null);
  const [mentionState, setMentionState] = useState<MentionContext | null>(null);
  const [selectedIndex, setSelectedIndex] = useState(0);
  const [isEmpty, setIsEmpty] = useState(true);
  const [emojiPickerOpen, setEmojiPickerOpen] = useState(false);
  const lastTyping = useRef(0);
  const fileInputRef = useRef<HTMLInputElement>(null);
  const editorRef = useRef<HTMLDivElement>(null);

  const activeChannelId = useChatStore((s) => s.activeChannelId);
  const channels = useChatStore((s) => s.channels);
  const currentUser = useChatStore((s) => s.user);

  const activeChannel = channels.find((c) => c.id === activeChannelId);
  const members = useMemo(
    () => activeChannel?.members || [],
    [activeChannel?.members],
  );

  const isUploading = uploadProgress !== null && uploadProgress !== undefined;

  const updateEmpty = useCallback(() => {
    if (editorRef.current) {
      setIsEmpty(isContentEmpty(editorRef.current));
    }
  }, []);

  const insertMentionChip = useCallback(
    (option: MentionOption) => {
      if (!mentionState || !editorRef.current) return;

      const { startOffset } = mentionState;

      // Find the current text node at the cursor
      const sel = window.getSelection();
      if (!sel || sel.rangeCount === 0) return;
      const cursorNode = sel.getRangeAt(0).startContainer;
      if (cursorNode.nodeType !== Node.TEXT_NODE) return;

      const textNode = cursorNode as Text;
      const text = textNode.textContent || '';
      const afterAt = startOffset + 1;
      let endOffset = afterAt;
      while (endOffset < text.length && /\w/.test(text[endOffset])) {
        endOffset++;
      }

      // Create the chip element
      const chip = document.createElement('span');
      chip.contentEditable = 'false';
      chip.dataset.mentionValue = option.value;
      chip.className =
        'inline-flex items-center gap-1 px-1.5 mx-0.5 rounded-full bg-primary/15 text-primary text-sm font-medium select-none leading-[inherit]';
      chip.textContent = option.label;

      // Split text node and insert chip
      const afterText = text.slice(endOffset);
      const beforeText = text.slice(0, startOffset);

      textNode.textContent = beforeText;
      const afterNode = document.createTextNode(afterText || '\u00A0');

      const parent = textNode.parentNode!;
      parent.insertBefore(chip, textNode.nextSibling);
      parent.insertBefore(afterNode, chip.nextSibling);

      // Place cursor after the chip
      const range = document.createRange();
      range.setStart(afterNode, afterText ? 0 : 1);
      range.collapse(true);
      sel.removeAllRanges();
      sel.addRange(range);

      setMentionState(null);
      setSelectedIndex(0);
      updateEmpty();
    },
    [mentionState, updateEmpty],
  );

  const checkMentionTrigger = useCallback(() => {
    const sel = window.getSelection();
    if (!sel || sel.rangeCount === 0) {
      setMentionState(null);
      return;
    }
    const range = sel.getRangeAt(0);
    if (!range.collapsed) {
      setMentionState(null);
      return;
    }

    const ctx = getMentionContext(range.startContainer, range.startOffset);
    if (ctx) {
      const options = getFilteredOptions(
        ctx.query,
        members,
        currentUser?.id || '',
      );
      if (options.length > 0) {
        setMentionState(ctx);
        setSelectedIndex(0);
        return;
      }
    }
    setMentionState(null);
  }, [members, currentUser]);

  const handleInput = useCallback(() => {
    const now = Date.now();
    if (now - lastTyping.current > 2000) {
      lastTyping.current = now;
      onTyping();
    }
    updateEmpty();
    checkMentionTrigger();
  }, [onTyping, updateEmpty, checkMentionTrigger]);

  const handleSubmit = useCallback(() => {
    if (isUploading || !editorRef.current) return;

    if (selectedFile && onUpload) {
      const content = serializeContent(editorRef.current).trim();
      onUpload(selectedFile, content);
      editorRef.current.innerHTML = '';
      setSelectedFile(null);
      setIsEmpty(true);
      return;
    }

    const content = serializeContent(editorRef.current).trim();
    if (!content) return;
    onSend(content);
    editorRef.current.innerHTML = '';
    setIsEmpty(true);
    setMentionState(null);
  }, [isUploading, selectedFile, onUpload, onSend]);

  const handleKeyDown = useCallback(
    (e: React.KeyboardEvent) => {
      if (mentionState) {
        const options = getFilteredOptions(
          mentionState.query,
          members,
          currentUser?.id || '',
        );

        if (e.key === 'ArrowDown') {
          e.preventDefault();
          setSelectedIndex((i) => (i + 1) % options.length);
          return;
        }
        if (e.key === 'ArrowUp') {
          e.preventDefault();
          setSelectedIndex((i) => (i - 1 + options.length) % options.length);
          return;
        }
        if (e.key === 'Enter' || e.key === 'Tab') {
          e.preventDefault();
          if (options[selectedIndex]) {
            insertMentionChip(options[selectedIndex]);
          }
          return;
        }
        if (e.key === 'Escape') {
          e.preventDefault();
          setMentionState(null);
          return;
        }
      }

      if (e.key === 'Enter' && !e.shiftKey) {
        e.preventDefault();
        handleSubmit();
      }
    },
    [
      mentionState,
      members,
      currentUser,
      selectedIndex,
      insertMentionChip,
      handleSubmit,
    ],
  );

  const handleFileSelect = (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (file) setSelectedFile(file);
    if (fileInputRef.current) fileInputRef.current.value = '';
  };

  const removeFile = () => {
    setSelectedFile(null);
  };

  const insertEmoji = useCallback(
    (emoji: string) => {
      if (!editorRef.current) return;
      editorRef.current.focus();
      const sel = window.getSelection();
      if (sel && sel.rangeCount > 0) {
        const range = sel.getRangeAt(0);
        range.deleteContents();
        range.insertNode(document.createTextNode(emoji));
        range.collapse(false);
        sel.removeAllRanges();
        sel.addRange(range);
      } else {
        editorRef.current.appendChild(document.createTextNode(emoji));
      }
      updateEmpty();
    },
    [updateEmpty],
  );

  // Close mention popup on outside click
  useEffect(() => {
    const handleClick = (e: MouseEvent) => {
      if (editorRef.current && !editorRef.current.contains(e.target as Node)) {
        setMentionState(null);
      }
    };
    document.addEventListener('mousedown', handleClick);
    return () => document.removeEventListener('mousedown', handleClick);
  }, []);

  return (
    <div className="border-t border-default-100 p-2 sm:p-4">
      {selectedFile && (
        <div className="flex items-center gap-2 mb-2 px-2 py-1.5 bg-content2 rounded-lg text-sm">
          <FontAwesomeIcon
            icon={faPaperclip}
            className="text-default-400 text-xs"
          />
          <span className="truncate flex-1">{selectedFile.name}</span>
          <span className="text-default-400 text-xs">
            {formatFileSize(selectedFile.size)}
          </span>
          <button
            onClick={removeFile}
            className="text-default-400 hover:text-danger"
          >
            <FontAwesomeIcon icon={faXmark} />
          </button>
        </div>
      )}
      {uploadError && (
        <div className="mb-2 px-2 py-1.5 bg-danger-50 dark:bg-danger-50/10 text-danger text-sm rounded-lg">
          {uploadError}
        </div>
      )}
      {isUploading && (
        <div className="mb-2 px-2">
          <Progress
            size="sm"
            value={uploadProgress}
            color="primary"
            label="Uploading..."
            showValueLabel
            classNames={{ label: 'text-xs', value: 'text-xs' }}
          />
        </div>
      )}
      <div className="flex gap-2 items-end">
        <input
          ref={fileInputRef}
          type="file"
          className="hidden"
          onChange={handleFileSelect}
        />
        <div className="flex items-end">
          <Tooltip content="Attach file" placement="top" delay={400}>
            <Button
              isIconOnly
              variant="light"
              size="sm"
              onPress={() => fileInputRef.current?.click()}
              isDisabled={isUploading}
              className="mb-1"
            >
              <FontAwesomeIcon icon={faPaperclip} />
            </Button>
          </Tooltip>
          <div className="relative">
            {emojiPickerOpen && (
              <EmojiPickerPopup
                onSelect={(emoji) => {
                  insertEmoji(emoji);
                  setEmojiPickerOpen(false);
                }}
                onClose={() => setEmojiPickerOpen(false)}
              />
            )}
            <Tooltip content="Emoji" placement="top" delay={400}>
              <Button
                isIconOnly
                variant="light"
                size="sm"
                onPress={() => setEmojiPickerOpen((o) => !o)}
                isDisabled={isUploading}
                className="mb-1"
              >
                <FontAwesomeIcon icon={faFaceSmile} />
              </Button>
            </Tooltip>
          </div>
        </div>
        <div className="flex-1 relative">
          {mentionState && (
            <MentionAutocomplete
              query={mentionState.query}
              members={members}
              currentUserId={currentUser?.id || ''}
              onSelect={insertMentionChip}
              selectedIndex={selectedIndex}
            />
          )}
          <div className="relative">
            <div
              ref={editorRef}
              contentEditable={!isUploading}
              role="textbox"
              aria-multiline="true"
              data-placeholder={
                selectedFile
                  ? 'Add a message (optional)...'
                  : 'Type a message...'
              }
              className={`min-h-[40px] max-h-[120px] overflow-y-auto px-3 py-2 rounded-xl border-2 border-default-200 hover:border-default-400 focus:border-primary focus:outline-none text-sm text-foreground bg-transparent transition-colors whitespace-pre-wrap break-words ${
                isUploading ? 'opacity-50 pointer-events-none' : ''
              }`}
              onInput={handleInput}
              onKeyDown={handleKeyDown}
              suppressContentEditableWarning
            />
            {isEmpty && (
              <div className="absolute top-0 left-0 px-3 py-2 text-sm text-default-400 pointer-events-none select-none">
                {selectedFile
                  ? 'Add a message (optional)...'
                  : 'Type a message...'}
              </div>
            )}
          </div>
        </div>
        <Button
          color="primary"
          isDisabled={(isEmpty && !selectedFile) || isUploading}
          onPress={handleSubmit}
        >
          {selectedFile ? 'Upload' : 'Send'}
        </Button>
      </div>
    </div>
  );
}
