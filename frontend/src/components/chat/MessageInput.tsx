import { useState, useRef, useCallback } from 'react';
import { Textarea, Button, Progress } from '@heroui/react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faPaperclip, faXmark } from '@fortawesome/free-solid-svg-icons';

interface Props {
  onSend: (content: string) => void;
  onTyping: () => void;
  onUpload?: (file: File, message: string) => void;
  uploadProgress?: number | null;
  uploadError?: string | null;
}

function formatFileSize(bytes: number): string {
  if (bytes < 1024) return bytes + ' B';
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
  return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
}

export function MessageInput({
  onSend,
  onTyping,
  onUpload,
  uploadProgress,
  uploadError,
}: Props) {
  const [content, setContent] = useState('');
  const [selectedFile, setSelectedFile] = useState<File | null>(null);
  const lastTyping = useRef(0);
  const fileInputRef = useRef<HTMLInputElement>(null);

  const isUploading = uploadProgress !== null && uploadProgress !== undefined;

  const handleChange = useCallback(
    (value: string) => {
      setContent(value);
      const now = Date.now();
      if (now - lastTyping.current > 2000) {
        lastTyping.current = now;
        onTyping();
      }
    },
    [onTyping],
  );

  const handleSubmit = () => {
    if (isUploading) return;

    if (selectedFile && onUpload) {
      onUpload(selectedFile, content.trim());
      setContent('');
      setSelectedFile(null);
      return;
    }

    const trimmed = content.trim();
    if (!trimmed) return;
    onSend(trimmed);
    setContent('');
  };

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      handleSubmit();
    }
  };

  const handleFileSelect = (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (file) setSelectedFile(file);
    if (fileInputRef.current) fileInputRef.current.value = '';
  };

  const removeFile = () => {
    setSelectedFile(null);
  };

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
        <Textarea
          value={content}
          onValueChange={handleChange}
          onKeyDown={handleKeyDown}
          placeholder={
            selectedFile ? 'Add a message (optional)...' : 'Type a message...'
          }
          minRows={1}
          maxRows={4}
          variant="bordered"
          className="flex-1"
          isDisabled={isUploading}
        />
        <Button
          color="primary"
          isDisabled={(!content.trim() && !selectedFile) || isUploading}
          onPress={handleSubmit}
        >
          {selectedFile ? 'Upload' : 'Send'}
        </Button>
      </div>
    </div>
  );
}
