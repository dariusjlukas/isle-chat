import { useState, useRef, useCallback } from 'react';
import {
  Button,
  Textarea,
  Dropdown,
  DropdownTrigger,
  DropdownMenu,
  DropdownItem,
  Progress,
  Modal,
  ModalContent,
  ModalHeader,
  ModalBody,
  ModalFooter,
  Tooltip,
} from '@heroui/react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
  faEllipsis,
  faPencil,
  faTrashCan,
  faDownload,
  faFile,
  faMagnifyingGlassPlus,
  faMagnifyingGlassMinus,
  faArrowsRotate,
  faEye,
} from '@fortawesome/free-solid-svg-icons';
import ReactMarkdown from 'react-markdown';
import remarkGfm from 'remark-gfm';
import type { Message } from '../../types';
import { useChatStore } from '../../stores/chatStore';
import { getFileUrl, downloadFile } from '../../services/api';
import { UserPopoverCard } from '../common/UserPopoverCard';

function formatFileSize(bytes: number): string {
  if (bytes < 1024) return bytes + ' B';
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
  return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
}

const MIN_ZOOM = 0.1;
const MAX_ZOOM = 10;
const ZOOM_STEP = 0.25;

function ImageViewer({
  fileUrl,
  fileName,
}: {
  fileUrl: string;
  fileName: string;
}) {
  const [zoom, setZoom] = useState(1);
  const [pan, setPan] = useState({ x: 0, y: 0 });
  const [dragging, setDragging] = useState(false);
  const dragStart = useRef({ x: 0, y: 0 });
  const panStart = useRef({ x: 0, y: 0 });
  const containerRef = useRef<HTMLDivElement>(null);

  const resetView = useCallback(() => {
    setZoom(1);
    setPan({ x: 0, y: 0 });
  }, []);

  const handleZoom = useCallback((delta: number) => {
    setZoom((z) => Math.min(MAX_ZOOM, Math.max(MIN_ZOOM, z + delta)));
  }, []);

  const handleWheel = useCallback((e: React.WheelEvent) => {
    e.preventDefault();
    const delta = e.deltaY > 0 ? -ZOOM_STEP : ZOOM_STEP;
    setZoom((z) => Math.min(MAX_ZOOM, Math.max(MIN_ZOOM, z + delta)));
  }, []);

  const handlePointerDown = useCallback(
    (e: React.PointerEvent) => {
      if (e.button !== 0) return;
      setDragging(true);
      dragStart.current = { x: e.clientX, y: e.clientY };
      panStart.current = { ...pan };
      (e.target as HTMLElement).setPointerCapture(e.pointerId);
    },
    [pan],
  );

  const handlePointerMove = useCallback(
    (e: React.PointerEvent) => {
      if (!dragging) return;
      setPan({
        x: panStart.current.x + (e.clientX - dragStart.current.x),
        y: panStart.current.y + (e.clientY - dragStart.current.y),
      });
    },
    [dragging],
  );

  const handlePointerUp = useCallback(() => {
    setDragging(false);
  }, []);

  return (
    <div className="flex flex-col h-full min-h-0">
      <div
        ref={containerRef}
        className="flex-1 min-h-0 overflow-hidden bg-black/20 rounded-lg relative"
        style={{
          cursor: dragging ? 'grabbing' : zoom > 1 ? 'grab' : 'default',
        }}
        onWheel={handleWheel}
        onPointerDown={handlePointerDown}
        onPointerMove={handlePointerMove}
        onPointerUp={handlePointerUp}
      >
        <div
          className="w-full h-full flex items-center justify-center"
          style={{
            transform: `translate(${pan.x}px, ${pan.y}px) scale(${zoom})`,
            transformOrigin: 'center center',
            transition: dragging ? 'none' : 'transform 0.15s ease-out',
          }}
        >
          <img
            src={fileUrl}
            alt={fileName}
            className="max-w-full max-h-full object-contain select-none pointer-events-none"
            draggable={false}
          />
        </div>
      </div>
      <div className="flex items-center justify-center gap-1 pt-2">
        <Button
          isIconOnly
          size="sm"
          variant="flat"
          onPress={() => handleZoom(-ZOOM_STEP)}
          isDisabled={zoom <= MIN_ZOOM}
        >
          <FontAwesomeIcon icon={faMagnifyingGlassMinus} />
        </Button>
        <span className="text-xs text-default-500 w-14 text-center">
          {Math.round(zoom * 100)}%
        </span>
        <Button
          isIconOnly
          size="sm"
          variant="flat"
          onPress={() => handleZoom(ZOOM_STEP)}
          isDisabled={zoom >= MAX_ZOOM}
        >
          <FontAwesomeIcon icon={faMagnifyingGlassPlus} />
        </Button>
        <Button
          isIconOnly
          size="sm"
          variant="flat"
          onPress={resetView}
          className="ml-1"
        >
          <FontAwesomeIcon icon={faArrowsRotate} />
        </Button>
      </div>
    </div>
  );
}

function FilePreviewModal({
  isOpen,
  onClose,
  fileUrl,
  fileName,
  fileType,
  fileId,
}: {
  isOpen: boolean;
  onClose: () => void;
  fileUrl: string;
  fileName: string;
  fileType: string;
  fileId: string;
}) {
  const [downloadProgress, setDownloadProgress] = useState<number | null>(null);
  const isImage = fileType.startsWith('image/');
  const isDownloading = downloadProgress !== null;

  const handleDownload = async () => {
    if (isDownloading) return;
    setDownloadProgress(0);
    try {
      await downloadFile(fileId, fileName, (p) => setDownloadProgress(p));
    } catch (e) {
      console.error('Download failed:', e);
    } finally {
      setDownloadProgress(null);
    }
  };

  return (
    <Modal
      isOpen={isOpen}
      onOpenChange={(open) => !open && onClose()}
      size="5xl"
      scrollBehavior="inside"
      backdrop="opaque"
      classNames={
        isImage
          ? {
              body: 'overflow-hidden',
              wrapper: 'overflow-hidden',
            }
          : undefined
      }
    >
      <ModalContent className={isImage ? 'h-[90vh] max-h-[90vh]' : ''}>
        <ModalHeader className="flex-col items-start gap-1 flex-shrink-0">
          <span className="truncate max-w-full">{fileName}</span>
        </ModalHeader>
        <ModalBody
          className={isImage ? 'overflow-hidden min-h-0' : 'items-center'}
        >
          {isImage ? (
            <ImageViewer fileUrl={fileUrl} fileName={fileName} />
          ) : (
            <iframe
              src={fileUrl}
              title={fileName}
              className="w-full rounded-lg border border-divider"
              style={{ height: '80vh' }}
            />
          )}
        </ModalBody>
        <ModalFooter className="flex-shrink-0">
          {isDownloading && (
            <Progress
              size="sm"
              value={downloadProgress}
              className="flex-1 mr-2"
            />
          )}
          <Button variant="flat" onPress={onClose}>
            Close
          </Button>
          <Button
            color="primary"
            onPress={handleDownload}
            isDisabled={isDownloading}
            startContent={<FontAwesomeIcon icon={faDownload} />}
          >
            Download
          </Button>
        </ModalFooter>
      </ModalContent>
    </Modal>
  );
}

function FileAttachment({
  message,
  isOwn,
}: {
  message: Message;
  isOwn: boolean;
}) {
  const [downloadProgress, setDownloadProgress] = useState<number | null>(null);
  const [imageError, setImageError] = useState(false);
  const [previewOpen, setPreviewOpen] = useState(false);

  if (!message.file_id) return null;

  const isImage = message.file_type?.startsWith('image/') && !imageError;
  const isPdf = message.file_type === 'application/pdf';
  const isPreviewable = isImage || isPdf;
  const isDownloading = downloadProgress !== null;

  const handleDownload = async () => {
    if (isDownloading) return;
    setDownloadProgress(0);
    try {
      await downloadFile(message.file_id!, message.file_name!, (p) =>
        setDownloadProgress(p),
      );
    } catch (e) {
      console.error('Download failed:', e);
    } finally {
      setDownloadProgress(null);
    }
  };

  if (isImage) {
    return (
      <>
        <div className="mt-2">
          <img
            src={getFileUrl(message.file_id!)}
            alt={message.file_name}
            className="max-w-full max-h-64 rounded-lg cursor-pointer hover:opacity-90 transition-opacity"
            onClick={() => setPreviewOpen(true)}
            onError={() => setImageError(true)}
          />
        </div>
        <FilePreviewModal
          isOpen={previewOpen}
          onClose={() => setPreviewOpen(false)}
          fileUrl={getFileUrl(message.file_id!)}
          fileName={message.file_name || 'image'}
          fileType={message.file_type || 'image/png'}
          fileId={message.file_id!}
        />
      </>
    );
  }

  return (
    <>
      <div
        className={`mt-2 flex items-center gap-3 p-2.5 rounded-lg ${
          isOwn ? 'bg-black/15' : 'bg-content1'
        } ${isPreviewable ? 'cursor-pointer hover:opacity-90 transition-opacity' : ''}`}
        onClick={isPreviewable ? () => setPreviewOpen(true) : undefined}
      >
        <FontAwesomeIcon icon={faFile} className="text-lg flex-shrink-0" />
        <div className="flex-1 min-w-0">
          <p className="text-sm font-medium truncate">{message.file_name}</p>
          <p className={`text-xs text-default-400`}>
            {formatFileSize(message.file_size || 0)}
          </p>
          {isDownloading && (
            <Progress
              size="sm"
              value={downloadProgress}
              color={isOwn ? 'default' : 'primary'}
              className="mt-1"
            />
          )}
        </div>
        <Button
          isIconOnly
          size="sm"
          variant="light"
          onPress={handleDownload}
          isDisabled={isDownloading}
        >
          <FontAwesomeIcon icon={faDownload} />
        </Button>
      </div>
      {isPreviewable && (
        <FilePreviewModal
          isOpen={previewOpen}
          onClose={() => setPreviewOpen(false)}
          fileUrl={getFileUrl(message.file_id!)}
          fileName={message.file_name || 'file'}
          fileType={message.file_type || 'application/octet-stream'}
          fileId={message.file_id!}
        />
      )}
    </>
  );
}

interface Props {
  message: Message;
  onEdit?: (messageId: string, content: string) => void;
  onDelete?: (messageId: string) => void;
}

export function MessageBubble({ message, onEdit, onDelete }: Props) {
  const currentUser = useChatStore((s) => s.user);
  const isOwn = currentUser?.id === message.user_id;
  const receipts = useChatStore((s) => s.readReceipts[message.channel_id]);
  const users = useChatStore((s) => s.users);
  const author = !isOwn ? users.find((u) => u.id === message.user_id) : null;
  const [editing, setEditing] = useState(false);
  const [editContent, setEditContent] = useState(message.content);
  const [menuOpen, setMenuOpen] = useState(false);

  const time = new Date(message.created_at).toLocaleTimeString([], {
    hour: '2-digit',
    minute: '2-digit',
  });

  const handleSaveEdit = () => {
    const trimmed = editContent.trim();
    if (trimmed && trimmed !== message.content) {
      onEdit?.(message.id, trimmed);
    }
    setEditing(false);
  };

  const handleDelete = () => {
    if (confirm('Delete this message?')) {
      onDelete?.(message.id);
    }
  };

  if (message.is_deleted) {
    return (
      <div className={`flex ${isOwn ? 'justify-end' : 'justify-start'} mb-2`}>
        <div className="max-w-[85%] sm:max-w-[70%] rounded-2xl px-4 py-2 bg-content1 border border-divider rounded-br-md">
          {!isOwn && (
            <p className="text-xs font-semibold text-default-400 mb-1">
              {author ? (
                <UserPopoverCard user={author}>
                  <span className="cursor-pointer hover:underline">
                    {message.username}
                  </span>
                </UserPopoverCard>
              ) : (
                message.username
              )}
            </p>
          )}
          <p className="text-sm italic text-default-400">
            This message was deleted
          </p>
          <p className="text-xs mt-1 text-default-300">{time}</p>
        </div>
      </div>
    );
  }

  return (
    <div
      id={`msg-${message.id}`}
      className={`flex ${isOwn ? 'justify-end' : 'justify-start'} mb-2 group`}
    >
      <div
        className={`max-w-[85%] sm:max-w-[70%] rounded-2xl px-4 py-2 relative text-foreground ${
          isOwn ? 'bg-content3 rounded-br-md' : 'bg-content2 rounded-bl-md'
        }`}
      >
        {!isOwn && (
          <p className="text-xs font-semibold text-primary mb-1">
            {author ? (
              <UserPopoverCard user={author}>
                <span className="cursor-pointer hover:underline">
                  {message.username}
                </span>
              </UserPopoverCard>
            ) : (
              message.username
            )}
          </p>
        )}

        {editing ? (
          <div className="space-y-2">
            <Textarea
              variant="bordered"
              value={editContent}
              onChange={(e) => setEditContent(e.target.value)}
              minRows={1}
              maxRows={4}
              size="sm"
              classNames={{
                input: 'text-sm',
                inputWrapper: 'bg-background/50',
              }}
              onKeyDown={(e) => {
                if (e.key === 'Enter' && !e.shiftKey) {
                  e.preventDefault();
                  handleSaveEdit();
                }
                if (e.key === 'Escape') {
                  setEditing(false);
                  setEditContent(message.content);
                }
              }}
              autoFocus
            />
            <div className="flex gap-1">
              <Button
                size="sm"
                color="primary"
                variant="solid"
                onPress={handleSaveEdit}
              >
                Save
              </Button>
              <Button
                size="sm"
                variant="flat"
                onPress={() => {
                  setEditing(false);
                  setEditContent(message.content);
                }}
              >
                Cancel
              </Button>
            </div>
          </div>
        ) : (
          <>
            {message.content && (
              <div
                className={`text-sm break-words prose prose-sm max-w-none dark:prose-invert ${
                  isOwn ? 'prose-pre:bg-black/20' : 'prose-pre:bg-content1'
                }`}
              >
                <ReactMarkdown remarkPlugins={[remarkGfm]}>
                  {message.content}
                </ReactMarkdown>
              </div>
            )}
            <FileAttachment message={message} isOwn={isOwn} />
          </>
        )}

        <p className={`text-xs mt-1 text-default-400 flex items-center gap-1`}>
          <span>
            {time}
            {message.edited_at && <span className="ml-1">(edited)</span>}
          </span>
          {isOwn &&
            !editing &&
            receipts &&
            (() => {
              const seenByEntries = Object.entries(receipts)
                .filter(
                  ([uid, r]) =>
                    uid !== currentUser?.id &&
                    r.last_read_at >= message.created_at,
                )
                .map(([, r]) => r);
              if (seenByEntries.length === 0) return null;
              const tooltipContent =
                seenByEntries.length === 1
                  ? `Seen by ${seenByEntries[0].username} at ${new Date(seenByEntries[0].last_read_at).toLocaleString([], { hour: '2-digit', minute: '2-digit', month: 'short', day: 'numeric' })}`
                  : seenByEntries
                      .map(
                        (r) =>
                          `${r.username} - ${new Date(r.last_read_at).toLocaleString([], { hour: '2-digit', minute: '2-digit', month: 'short', day: 'numeric' })}`,
                      )
                      .join('\n');
              return (
                <Tooltip
                  content={
                    <span className="whitespace-pre-line text-xs">
                      {tooltipContent}
                    </span>
                  }
                  placement="top"
                  delay={200}
                >
                  <span className="text-primary/70 cursor-default">
                    <FontAwesomeIcon icon={faEye} className="text-[10px]" />
                  </span>
                </Tooltip>
              );
            })()}
        </p>

        {isOwn && !editing && (
          <div
            className={`absolute -bottom-2 -right-3 ${menuOpen ? 'block' : 'hidden group-hover:block'}`}
          >
            <Dropdown
              placement="bottom-end"
              isOpen={menuOpen}
              onOpenChange={setMenuOpen}
            >
              <DropdownTrigger>
                <button className="w-6 h-6 rounded-full bg-content1 border border-divider flex items-center justify-center text-xs hover:bg-content2 text-foreground shadow-sm">
                  <FontAwesomeIcon icon={faEllipsis} />
                </button>
              </DropdownTrigger>
              <DropdownMenu
                aria-label="Message actions"
                onAction={(key) => {
                  if (key === 'edit') {
                    setEditContent(message.content);
                    setEditing(true);
                  } else if (key === 'delete') {
                    handleDelete();
                  }
                }}
              >
                {!message.file_id ? (
                  <DropdownItem
                    key="edit"
                    startContent={<FontAwesomeIcon icon={faPencil} />}
                  >
                    Edit
                  </DropdownItem>
                ) : null}
                <DropdownItem
                  key="delete"
                  className="text-danger"
                  color="danger"
                  startContent={<FontAwesomeIcon icon={faTrashCan} />}
                >
                  Delete
                </DropdownItem>
              </DropdownMenu>
            </Dropdown>
          </div>
        )}
      </div>
    </div>
  );
}
