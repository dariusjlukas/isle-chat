import { useState, useEffect, useCallback, useRef, useMemo } from 'react';
import {
  Button,
  Spinner,
  Input,
  Modal,
  ModalContent,
  ModalHeader,
  ModalBody,
  ModalFooter,
  Select,
  SelectItem,
} from '@heroui/react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
  faFolder,
  faFolderPlus,
  faUpload,
  faDownload,
  faTrash,
  faPen,
  faChevronRight,
  faHome,
  faFile,
  faFileImage,
  faFilePdf,
  faFileVideo,
  faFileAudio,
  faFileCode,
  faFileLines,
  faFileZipper,
  faShield,
  faClockRotateLeft,
  faEye,
  faRotateLeft,
  faUserPlus,
  faUserMinus,
} from '@fortawesome/free-solid-svg-icons';
import { useChatStore } from '../../stores/chatStore';
import * as api from '../../services/api';
import type {
  SpaceFile,
  SpaceFilePath,
  SpaceFilePermission,
  SpaceFileVersion,
} from '../../types';

function formatSize(bytes: number): string {
  if (bytes === 0) return '—';
  if (bytes < 1024) return bytes + ' B';
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
  if (bytes < 1024 * 1024 * 1024)
    return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
  return (bytes / (1024 * 1024 * 1024)).toFixed(1) + ' GB';
}

function formatDate(dateStr: string): string {
  const d = new Date(dateStr);
  return d.toLocaleDateString(undefined, {
    month: 'short',
    day: 'numeric',
    year: 'numeric',
  });
}

function formatDateTime(dateStr: string): string {
  const d = new Date(dateStr);
  return d.toLocaleString(undefined, {
    month: 'short',
    day: 'numeric',
    year: 'numeric',
    hour: '2-digit',
    minute: '2-digit',
  });
}

function getFileIcon(mimeType: string) {
  if (mimeType.startsWith('image/')) return faFileImage;
  if (mimeType === 'application/pdf') return faFilePdf;
  if (mimeType.startsWith('video/')) return faFileVideo;
  if (mimeType.startsWith('audio/')) return faFileAudio;
  if (
    mimeType.startsWith('text/') ||
    mimeType.includes('json') ||
    mimeType.includes('xml') ||
    mimeType.includes('javascript') ||
    mimeType.includes('typescript')
  )
    return mimeType.startsWith('text/plain') ? faFileLines : faFileCode;
  if (
    mimeType.includes('zip') ||
    mimeType.includes('tar') ||
    mimeType.includes('gzip') ||
    mimeType.includes('compressed')
  )
    return faFileZipper;
  return faFile;
}

function permRank(p: string): number {
  if (p === 'owner') return 2;
  if (p === 'edit') return 1;
  return 0;
}

function canEdit(perm: string): boolean {
  return permRank(perm) >= 1;
}

function canOwn(perm: string): boolean {
  return permRank(perm) >= 2;
}

function isPreviewable(mimeType: string): boolean {
  return (
    mimeType.startsWith('image/') ||
    mimeType === 'application/pdf' ||
    mimeType.startsWith('video/') ||
    mimeType.startsWith('audio/') ||
    mimeType.startsWith('text/') ||
    mimeType.includes('json') ||
    mimeType.includes('xml') ||
    mimeType.includes('javascript') ||
    mimeType.includes('typescript') ||
    mimeType.includes('markdown')
  );
}

interface Props {
  spaceId: string;
}

export function FileBrowser({ spaceId }: Props) {
  const spaces = useChatStore((s) => s.spaces);
  const space = spaces.find((s) => s.id === spaceId);
  const currentUser = useChatStore((s) => s.user);
  const users = useChatStore((s) => s.users);

  const [files, setFiles] = useState<SpaceFile[]>([]);
  const [path, setPath] = useState<SpaceFilePath[]>([]);
  const [parentId, setParentId] = useState<string | undefined>(undefined);
  const [myPermission, setMyPermission] = useState<string>('view');
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  // Upload state
  const [uploading, setUploading] = useState(false);
  const [uploadProgress, setUploadProgress] = useState<number | null>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);

  // Create folder state
  const [showNewFolder, setShowNewFolder] = useState(false);
  const [newFolderName, setNewFolderName] = useState('');
  const [creatingFolder, setCreatingFolder] = useState(false);

  // Rename state
  const [renamingId, setRenamingId] = useState<string | null>(null);
  const [renameValue, setRenameValue] = useState('');

  // Permissions panel state
  const [permFile, setPermFile] = useState<SpaceFile | null>(null);
  const [permissions, setPermissions] = useState<SpaceFilePermission[]>([]);
  const [permLoading, setPermLoading] = useState(false);
  const [permMyLevel, setPermMyLevel] = useState('view');
  const [addPermUserId, setAddPermUserId] = useState('');
  const [addPermLevel, setAddPermLevel] = useState('view');
  const [addPermSearch, setAddPermSearch] = useState('');

  // Version panel state
  const [versionFile, setVersionFile] = useState<SpaceFile | null>(null);
  const [versions, setVersions] = useState<SpaceFileVersion[]>([]);
  const [versionLoading, setVersionLoading] = useState(false);
  const [versionUploading, setVersionUploading] = useState(false);
  const [versionUploadProgress, setVersionUploadProgress] = useState<
    number | null
  >(null);
  const versionFileInputRef = useRef<HTMLInputElement>(null);

  // Drag-and-drop state
  const [dragId, setDragId] = useState<string | null>(null);
  const [dropTargetId, setDropTargetId] = useState<string | null>(null);
  const dragAllowedRef = useRef(true);

  // Preview state
  const [previewFile, setPreviewFile] = useState<SpaceFile | null>(null);
  const [previewContent, setPreviewContent] = useState<string | null>(null);
  const [previewLoading, setPreviewLoading] = useState(false);

  const loadFiles = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      const result = await api.listSpaceFilesWithPermission(spaceId, parentId);
      setFiles(result.files);
      setPath(result.path);
      setMyPermission(result.my_permission);
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Failed to load files');
    } finally {
      setLoading(false);
    }
  }, [spaceId, parentId]);

  useEffect(() => {
    loadFiles();
  }, [loadFiles]);

  const navigateTo = (folderId?: string) => {
    setParentId(folderId);
    setShowNewFolder(false);
    setRenamingId(null);
  };

  const handleCreateFolder = async () => {
    if (!newFolderName.trim()) return;
    setCreatingFolder(true);
    try {
      await api.createSpaceFolder(spaceId, newFolderName.trim(), parentId);
      setNewFolderName('');
      setShowNewFolder(false);
      await loadFiles();
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Failed to create folder');
    } finally {
      setCreatingFolder(false);
    }
  };

  const handleUpload = async (e: React.ChangeEvent<HTMLInputElement>) => {
    const fileList = e.target.files;
    if (!fileList || fileList.length === 0) return;

    setUploading(true);
    setError(null);
    try {
      for (let i = 0; i < fileList.length; i++) {
        await api.uploadSpaceFile(spaceId, fileList[i], parentId, (p) =>
          setUploadProgress(p),
        );
      }
      await loadFiles();
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Upload failed');
    } finally {
      setUploading(false);
      setUploadProgress(null);
      if (fileInputRef.current) fileInputRef.current.value = '';
    }
  };

  const handleDownload = async (file: SpaceFile) => {
    try {
      await api.downloadSpaceFile(spaceId, file.id, file.name);
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Download failed');
    }
  };

  const handleDelete = async (file: SpaceFile) => {
    const label = file.is_folder ? 'folder' : 'file';
    if (!confirm(`Delete ${label} "${file.name}"?`)) return;
    try {
      await api.deleteSpaceFile(spaceId, file.id);
      await loadFiles();
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Delete failed');
    }
  };

  const handleRename = async (fileId: string) => {
    if (!renameValue.trim()) {
      setRenamingId(null);
      return;
    }
    try {
      await api.updateSpaceFile(spaceId, fileId, { name: renameValue.trim() });
      setRenamingId(null);
      await loadFiles();
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Rename failed');
    }
  };

  // --- Drag-and-drop move ---

  const isInteractive = (target: HTMLElement, container: HTMLElement) => {
    let el: HTMLElement | null = target;
    while (el && el !== container) {
      const tag = el.tagName;
      if (
        tag === 'BUTTON' ||
        tag === 'INPUT' ||
        tag === 'A' ||
        el.getAttribute('role') === 'button' ||
        el.hasAttribute('data-nodrag')
      )
        return true;
      el = el.parentElement;
    }
    return false;
  };

  const handleRowMouseDown = (e: React.MouseEvent) => {
    dragAllowedRef.current = !isInteractive(
      e.target as HTMLElement,
      e.currentTarget as HTMLElement,
    );
  };

  const handleDragStart = (e: React.DragEvent, file: SpaceFile) => {
    if (!dragAllowedRef.current) {
      e.preventDefault();
      return;
    }
    setDragId(file.id);
    e.dataTransfer.effectAllowed = 'move';
    e.dataTransfer.setData('text/plain', file.id);
  };

  const handleDragOver = (e: React.DragEvent, targetFile: SpaceFile) => {
    if (!targetFile.is_folder || targetFile.id === dragId) return;
    e.preventDefault();
    e.dataTransfer.dropEffect = 'move';
    setDropTargetId(targetFile.id);
  };

  const handleDragLeave = (e: React.DragEvent, targetId: string) => {
    if (dropTargetId === targetId) {
      const rect = e.currentTarget.getBoundingClientRect();
      const { clientX: x, clientY: y } = e;
      if (x < rect.left || x > rect.right || y < rect.top || y > rect.bottom) {
        setDropTargetId(null);
      }
    }
  };

  const handleDrop = async (e: React.DragEvent, targetFolder: SpaceFile) => {
    e.preventDefault();
    const fileId = e.dataTransfer.getData('text/plain');
    setDragId(null);
    setDropTargetId(null);
    if (!fileId || fileId === targetFolder.id) return;
    try {
      await api.updateSpaceFile(spaceId, fileId, {
        parent_id: targetFolder.id,
      });
      await loadFiles();
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to move file');
    }
  };

  const handleDragEnd = () => {
    setDragId(null);
    setDropTargetId(null);
  };

  // --- Permissions ---

  const openPermissions = async (file: SpaceFile) => {
    setPermFile(file);
    setPermLoading(true);
    setAddPermSearch('');
    setAddPermUserId('');
    setAddPermLevel('view');
    try {
      const result = await api.getFilePermissions(spaceId, file.id);
      setPermissions(result.permissions);
      setPermMyLevel(result.my_permission);
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Failed to load permissions');
      setPermFile(null);
    } finally {
      setPermLoading(false);
    }
  };

  const handleSetPermission = async () => {
    if (!permFile || !addPermUserId) return;
    try {
      await api.setFilePermission(
        spaceId,
        permFile.id,
        addPermUserId,
        addPermLevel,
      );
      const result = await api.getFilePermissions(spaceId, permFile.id);
      setPermissions(result.permissions);
      setAddPermUserId('');
      setAddPermSearch('');
      setAddPermLevel('view');
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Failed to set permission');
    }
  };

  const handleRemovePermission = async (userId: string) => {
    if (!permFile) return;
    try {
      await api.removeFilePermission(spaceId, permFile.id, userId);
      const result = await api.getFilePermissions(spaceId, permFile.id);
      setPermissions(result.permissions);
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Failed to remove permission');
    }
  };

  const permUserSearch = useMemo(() => {
    if (!addPermSearch.trim()) return [];
    const q = addPermSearch.toLowerCase();
    const existingIds = new Set(permissions.map((p) => p.user_id));
    return users
      .filter(
        (u) =>
          !existingIds.has(u.id) &&
          (u.username.toLowerCase().includes(q) ||
            (u.display_name && u.display_name.toLowerCase().includes(q))),
      )
      .slice(0, 8);
  }, [users, permissions, addPermSearch]);

  // --- Versions ---

  const openVersions = async (file: SpaceFile) => {
    setVersionFile(file);
    setVersionLoading(true);
    try {
      const result = await api.listFileVersions(spaceId, file.id);
      setVersions(result.versions);
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Failed to load versions');
      setVersionFile(null);
    } finally {
      setVersionLoading(false);
    }
  };

  const handleVersionUpload = async (
    e: React.ChangeEvent<HTMLInputElement>,
  ) => {
    if (!versionFile) return;
    const fileList = e.target.files;
    if (!fileList || fileList.length === 0) return;

    setVersionUploading(true);
    try {
      await api.uploadFileVersion(spaceId, versionFile.id, fileList[0], (p) =>
        setVersionUploadProgress(p),
      );
      const result = await api.listFileVersions(spaceId, versionFile.id);
      setVersions(result.versions);
      await loadFiles();
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Version upload failed');
    } finally {
      setVersionUploading(false);
      setVersionUploadProgress(null);
      if (versionFileInputRef.current) versionFileInputRef.current.value = '';
    }
  };

  const handleRevert = async (versionId: string) => {
    if (!versionFile) return;
    if (!confirm('Revert to this version? A new version will be created.'))
      return;
    try {
      await api.revertFileVersion(spaceId, versionFile.id, versionId);
      const result = await api.listFileVersions(spaceId, versionFile.id);
      setVersions(result.versions);
      await loadFiles();
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Revert failed');
    }
  };

  const handleVersionDownload = async (
    version: SpaceFileVersion,
    fileName: string,
  ) => {
    if (!versionFile) return;
    try {
      await api.downloadFileVersion(
        spaceId,
        versionFile.id,
        version.id,
        `v${version.version_number}_${fileName}`,
      );
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Download failed');
    }
  };

  // --- Preview ---

  const openPreview = async (file: SpaceFile) => {
    setPreviewFile(file);
    setPreviewContent(null);
    const mime = file.mime_type || '';

    // For text-based files, fetch content as text
    if (
      mime.startsWith('text/') ||
      mime.includes('json') ||
      mime.includes('xml') ||
      mime.includes('javascript') ||
      mime.includes('typescript') ||
      mime.includes('markdown')
    ) {
      setPreviewLoading(true);
      try {
        const token = localStorage.getItem('session_token');
        const res = await fetch(
          `/api/spaces/${spaceId}/files/${file.id}/download`,
          { headers: token ? { Authorization: `Bearer ${token}` } : {} },
        );
        if (!res.ok) throw new Error('Failed to load');
        const text = await res.text();
        setPreviewContent(text);
      } catch {
        setPreviewContent('Failed to load file content.');
      } finally {
        setPreviewLoading(false);
      }
    }
  };

  const previewUrl = previewFile
    ? api.getSpaceFileDownloadUrl(spaceId, previewFile.id, true)
    : '';

  return (
    <div className='flex-1 flex flex-col bg-background overflow-hidden'>
      {/* Header */}
      <div className='shrink-0 border-b border-default-100 px-4 py-3'>
        <div className='flex items-center justify-between'>
          <div className='flex items-center gap-2 min-w-0'>
            <h2 className='text-sm font-semibold text-foreground'>
              {space?.name} — Files
            </h2>
            <span className='text-xs text-default-400 bg-default-100 px-1.5 py-0.5 rounded'>
              {myPermission}
            </span>
          </div>
          <div className='flex items-center gap-1'>
            {canEdit(myPermission) && (
              <>
                <Button
                  size='sm'
                  variant='flat'
                  startContent={<FontAwesomeIcon icon={faFolderPlus} />}
                  onPress={() => {
                    setShowNewFolder(true);
                    setNewFolderName('');
                  }}
                >
                  New Folder
                </Button>
                <Button
                  size='sm'
                  variant='flat'
                  startContent={<FontAwesomeIcon icon={faUpload} />}
                  isLoading={uploading}
                  onPress={() => fileInputRef.current?.click()}
                >
                  {uploading && uploadProgress !== null
                    ? `${uploadProgress}%`
                    : 'Upload'}
                </Button>
                <input
                  ref={fileInputRef}
                  type='file'
                  multiple
                  className='hidden'
                  onChange={handleUpload}
                />
              </>
            )}
          </div>
        </div>

        {/* Breadcrumbs */}
        <div className='flex items-center gap-1 mt-2 text-xs text-default-500 overflow-x-auto'>
          <button
            className={`hover:text-foreground transition-colors shrink-0 rounded px-1 ${dropTargetId === 'root' ? 'bg-primary/15 ring-1 ring-primary/40' : ''}`}
            onClick={() => navigateTo(undefined)}
            onDragOver={
              parentId
                ? (e) => {
                    e.preventDefault();
                    e.dataTransfer.dropEffect = 'move';
                    setDropTargetId('root');
                  }
                : undefined
            }
            onDragLeave={() => {
              if (dropTargetId === 'root') setDropTargetId(null);
            }}
            onDrop={
              parentId
                ? async (e) => {
                    e.preventDefault();
                    const fileId = e.dataTransfer.getData('text/plain');
                    setDragId(null);
                    setDropTargetId(null);
                    if (!fileId) return;
                    try {
                      await api.updateSpaceFile(spaceId, fileId, {
                        parent_id: null,
                      });
                      await loadFiles();
                    } catch (err) {
                      setError(
                        err instanceof Error
                          ? err.message
                          : 'Failed to move file',
                      );
                    }
                  }
                : undefined
            }
          >
            <FontAwesomeIcon icon={faHome} className='text-xs' />
          </button>
          {path.map((p, i) => (
            <span key={p.id} className='flex items-center gap-1 shrink-0'>
              <FontAwesomeIcon
                icon={faChevronRight}
                className='text-[10px] text-default-300'
              />
              {i === path.length - 1 ? (
                <span className='text-foreground font-medium'>{p.name}</span>
              ) : (
                <button
                  className={`hover:text-foreground transition-colors rounded px-1 ${dropTargetId === p.id ? 'bg-primary/15 ring-1 ring-primary/40' : ''}`}
                  onClick={() => navigateTo(p.id)}
                  onDragOver={(e) => {
                    e.preventDefault();
                    e.dataTransfer.dropEffect = 'move';
                    setDropTargetId(p.id);
                  }}
                  onDragLeave={() => {
                    if (dropTargetId === p.id) setDropTargetId(null);
                  }}
                  onDrop={async (e) => {
                    e.preventDefault();
                    const fileId = e.dataTransfer.getData('text/plain');
                    setDragId(null);
                    setDropTargetId(null);
                    if (!fileId) return;
                    try {
                      await api.updateSpaceFile(spaceId, fileId, {
                        parent_id: p.id,
                      });
                      await loadFiles();
                    } catch (err) {
                      setError(
                        err instanceof Error
                          ? err.message
                          : 'Failed to move file',
                      );
                    }
                  }}
                >
                  {p.name}
                </button>
              )}
            </span>
          ))}
        </div>
      </div>

      {/* Error banner */}
      {error && (
        <div className='shrink-0 bg-danger/10 text-danger text-sm px-4 py-2 flex items-center justify-between'>
          <span>{error}</span>
          <button
            className='text-xs underline ml-2'
            onClick={() => setError(null)}
          >
            Dismiss
          </button>
        </div>
      )}

      {/* Content */}
      <div className='flex-1 overflow-y-auto'>
        {loading ? (
          <div className='flex items-center justify-center py-16'>
            <Spinner size='lg' />
          </div>
        ) : (
          <div className='px-4 py-2'>
            {/* New folder input row */}
            {showNewFolder && (
              <div className='flex items-center gap-2 py-2 px-3 mb-1 rounded-md bg-content2/50'>
                <FontAwesomeIcon
                  icon={faFolder}
                  className='text-warning text-sm w-5'
                />
                <Input
                  size='sm'
                  variant='flat'
                  placeholder='Folder name'
                  value={newFolderName}
                  onValueChange={setNewFolderName}
                  autoFocus
                  classNames={{ base: 'flex-1 max-w-[300px]' }}
                  onKeyDown={(e) => {
                    if (e.key === 'Enter') handleCreateFolder();
                    if (e.key === 'Escape') setShowNewFolder(false);
                  }}
                />
                <Button
                  size='sm'
                  color='primary'
                  variant='flat'
                  isLoading={creatingFolder}
                  onPress={handleCreateFolder}
                >
                  Create
                </Button>
                <Button
                  size='sm'
                  variant='light'
                  onPress={() => setShowNewFolder(false)}
                >
                  Cancel
                </Button>
              </div>
            )}

            {/* File table header */}
            {files.length > 0 && (
              <div className='grid grid-cols-[1fr_100px_120px_120px_220px] gap-2 px-3 py-1.5 text-xs text-default-400 font-medium border-b border-default-100'>
                <span>Name</span>
                <span>Size</span>
                <span>Modified</span>
                <span>Created by</span>
                <span></span>
              </div>
            )}

            {/* File rows */}
            {files.map((file) => {
              const filePerm = file.my_permission || myPermission;
              return (
                <div
                  key={file.id}
                  draggable={canEdit(filePerm)}
                  onMouseDown={
                    canEdit(filePerm) ? handleRowMouseDown : undefined
                  }
                  onDragStart={
                    canEdit(filePerm)
                      ? (e) => handleDragStart(e, file)
                      : undefined
                  }
                  onDragEnd={canEdit(filePerm) ? handleDragEnd : undefined}
                  onDragOver={
                    file.is_folder ? (e) => handleDragOver(e, file) : undefined
                  }
                  onDragLeave={
                    file.is_folder
                      ? (e) => handleDragLeave(e, file.id)
                      : undefined
                  }
                  onDrop={
                    file.is_folder ? (e) => handleDrop(e, file) : undefined
                  }
                  className={`grid grid-cols-[1fr_100px_120px_120px_220px] gap-2 items-center px-3 py-2 text-sm rounded-md hover:bg-content2/50 transition-colors group ${
                    dragId === file.id ? 'opacity-40' : ''
                  } ${dropTargetId === file.id ? 'bg-primary/15 ring-1 ring-primary/40' : ''}`}
                >
                  {/* Name cell */}
                  <div className='flex items-center gap-2 min-w-0'>
                    <FontAwesomeIcon
                      icon={
                        file.is_folder
                          ? faFolder
                          : getFileIcon(file.mime_type || '')
                      }
                      className={`text-sm w-5 shrink-0 ${file.is_folder ? 'text-warning cursor-pointer' : isPreviewable(file.mime_type || '') ? 'text-default-400 cursor-pointer' : 'text-default-400'}`}
                      data-nodrag={
                        file.is_folder || isPreviewable(file.mime_type || '')
                          ? true
                          : undefined
                      }
                      onClick={() => {
                        if (file.is_folder) navigateTo(file.id);
                        else if (isPreviewable(file.mime_type || ''))
                          openPreview(file);
                      }}
                    />
                    {renamingId === file.id ? (
                      <Input
                        size='sm'
                        variant='bordered'
                        value={renameValue}
                        onValueChange={setRenameValue}
                        autoFocus
                        classNames={{ base: 'flex-1 min-w-0' }}
                        onKeyDown={(e) => {
                          if (e.key === 'Enter') handleRename(file.id);
                          if (e.key === 'Escape') setRenamingId(null);
                        }}
                        onBlur={() => handleRename(file.id)}
                      />
                    ) : file.is_folder ? (
                      <button
                        className='truncate text-left hover:text-primary transition-colors cursor-pointer'
                        onClick={() => navigateTo(file.id)}
                      >
                        {file.name}
                      </button>
                    ) : (
                      <button
                        className={`truncate text-left ${isPreviewable(file.mime_type || '') ? 'hover:text-primary cursor-pointer' : 'cursor-default'} text-default-700`}
                        onClick={() => {
                          if (isPreviewable(file.mime_type || ''))
                            openPreview(file);
                        }}
                      >
                        {file.name}
                      </button>
                    )}
                  </div>

                  {/* Size */}
                  <span className='text-xs text-default-400'>
                    {formatSize(file.file_size)}
                  </span>

                  {/* Modified */}
                  <span className='text-xs text-default-400'>
                    {formatDate(file.updated_at)}
                  </span>

                  {/* Created by */}
                  <span className='text-xs text-default-400 truncate'>
                    {file.created_by_username}
                  </span>

                  {/* Actions */}
                  <div className='flex items-center gap-0.5 opacity-0 group-hover:opacity-100 transition-opacity'>
                    {!file.is_folder && isPreviewable(file.mime_type || '') && (
                      <Button
                        isIconOnly
                        size='sm'
                        variant='light'
                        title='Preview'
                        onPress={() => openPreview(file)}
                      >
                        <FontAwesomeIcon icon={faEye} className='text-xs' />
                      </Button>
                    )}
                    {canEdit(filePerm) && (
                      <Button
                        isIconOnly
                        size='sm'
                        variant='light'
                        title='Rename'
                        onPress={() => {
                          setRenamingId(file.id);
                          setRenameValue(file.name);
                        }}
                      >
                        <FontAwesomeIcon icon={faPen} className='text-xs' />
                      </Button>
                    )}
                    {!file.is_folder && (
                      <>
                        <Button
                          isIconOnly
                          size='sm'
                          variant='light'
                          title='Download'
                          onPress={() => handleDownload(file)}
                        >
                          <FontAwesomeIcon
                            icon={faDownload}
                            className='text-xs'
                          />
                        </Button>
                        <Button
                          isIconOnly
                          size='sm'
                          variant='light'
                          title='Versions'
                          onPress={() => openVersions(file)}
                        >
                          <FontAwesomeIcon
                            icon={faClockRotateLeft}
                            className='text-xs'
                          />
                        </Button>
                      </>
                    )}
                    <Button
                      isIconOnly
                      size='sm'
                      variant='light'
                      title='Permissions'
                      onPress={() => openPermissions(file)}
                    >
                      <FontAwesomeIcon icon={faShield} className='text-xs' />
                    </Button>
                    {canOwn(filePerm) && (
                      <Button
                        isIconOnly
                        size='sm'
                        variant='light'
                        color='danger'
                        title='Delete'
                        onPress={() => handleDelete(file)}
                      >
                        <FontAwesomeIcon icon={faTrash} className='text-xs' />
                      </Button>
                    )}
                  </div>
                </div>
              );
            })}

            {/* Empty state */}
            {files.length === 0 && !showNewFolder && (
              <div className='text-center py-16 text-default-400'>
                <FontAwesomeIcon
                  icon={faFolder}
                  className='text-3xl mb-3 text-default-200'
                />
                <p className='text-sm'>This folder is empty</p>
                <p className='text-xs mt-1'>
                  Upload files or create a folder to get started
                </p>
              </div>
            )}
          </div>
        )}
      </div>

      {/* Permissions Modal */}
      <Modal
        isOpen={!!permFile}
        onClose={() => setPermFile(null)}
        size='lg'
        scrollBehavior='inside'
      >
        <ModalContent>
          <ModalHeader className='flex items-center gap-2'>
            <FontAwesomeIcon icon={faShield} className='text-primary' />
            Permissions — {permFile?.name}
          </ModalHeader>
          <ModalBody>
            {permLoading ? (
              <div className='flex justify-center py-8'>
                <Spinner />
              </div>
            ) : (
              <>
                {/* Current permissions list */}
                {permissions.length > 0 ? (
                  <div className='space-y-2'>
                    {permissions.map((p) => (
                      <div
                        key={p.id}
                        className='flex items-center justify-between px-3 py-2 rounded-lg bg-content2/50'
                      >
                        <div className='min-w-0'>
                          <span className='text-sm font-medium'>
                            {p.display_name || p.username}
                          </span>
                          <span className='text-xs text-default-400 ml-1'>
                            @{p.username}
                          </span>
                        </div>
                        <div className='flex items-center gap-2'>
                          <span
                            className={`text-xs px-2 py-0.5 rounded-full ${
                              p.permission === 'owner'
                                ? 'bg-warning/20 text-warning'
                                : p.permission === 'edit'
                                  ? 'bg-primary/20 text-primary'
                                  : 'bg-default-100 text-default-500'
                            }`}
                          >
                            {p.permission}
                          </span>
                          {canOwn(permMyLevel) &&
                            p.user_id !== currentUser?.id && (
                              <Button
                                isIconOnly
                                size='sm'
                                variant='light'
                                color='danger'
                                title='Remove permission'
                                onPress={() =>
                                  handleRemovePermission(p.user_id)
                                }
                              >
                                <FontAwesomeIcon
                                  icon={faUserMinus}
                                  className='text-xs'
                                />
                              </Button>
                            )}
                        </div>
                      </div>
                    ))}
                  </div>
                ) : (
                  <p className='text-sm text-default-400 text-center py-4'>
                    No explicit permissions set. Access is inherited from the
                    space role.
                  </p>
                )}

                {/* Add permission - only for owners */}
                {canOwn(permMyLevel) && (
                  <div className='mt-4 pt-4 border-t border-divider'>
                    <p className='text-sm font-medium mb-2'>
                      <FontAwesomeIcon icon={faUserPlus} className='mr-1.5' />
                      Grant Permission
                    </p>
                    <div className='flex gap-2 items-end'>
                      <div className='flex-1 relative'>
                        <Input
                          size='sm'
                          variant='bordered'
                          placeholder='Search user...'
                          value={addPermSearch}
                          onValueChange={(v) => {
                            setAddPermSearch(v);
                            if (!v) setAddPermUserId('');
                          }}
                          description={
                            addPermUserId
                              ? `Selected: ${users.find((u) => u.id === addPermUserId)?.username}`
                              : undefined
                          }
                        />
                        {addPermSearch && !addPermUserId && (
                          <div className='absolute z-20 top-full left-0 right-0 mt-1 bg-content1 border border-divider rounded-lg shadow-lg max-h-40 overflow-y-auto'>
                            {permUserSearch.map((u) => (
                              <button
                                key={u.id}
                                className='w-full text-left px-3 py-2 text-sm hover:bg-content2 transition-colors'
                                onClick={() => {
                                  setAddPermUserId(u.id);
                                  setAddPermSearch(
                                    u.display_name || u.username,
                                  );
                                }}
                              >
                                {u.display_name || u.username}{' '}
                                <span className='text-default-400'>
                                  @{u.username}
                                </span>
                              </button>
                            ))}
                            {permUserSearch.length === 0 && (
                              <p className='px-3 py-2 text-sm text-default-400'>
                                No users found
                              </p>
                            )}
                          </div>
                        )}
                      </div>
                      <Select
                        size='sm'
                        variant='bordered'
                        className='w-28'
                        selectedKeys={[addPermLevel]}
                        onSelectionChange={(keys) => {
                          const val = Array.from(keys)[0] as string;
                          if (val) setAddPermLevel(val);
                        }}
                      >
                        <SelectItem key='view'>View</SelectItem>
                        <SelectItem key='edit'>Edit</SelectItem>
                        <SelectItem key='owner'>Owner</SelectItem>
                      </Select>
                      <Button
                        size='sm'
                        color='primary'
                        isDisabled={!addPermUserId}
                        onPress={handleSetPermission}
                      >
                        Grant
                      </Button>
                    </div>
                  </div>
                )}
              </>
            )}
          </ModalBody>
          <ModalFooter>
            <Button variant='flat' onPress={() => setPermFile(null)}>
              Close
            </Button>
          </ModalFooter>
        </ModalContent>
      </Modal>

      {/* Versions Modal */}
      <Modal
        isOpen={!!versionFile}
        onClose={() => setVersionFile(null)}
        size='lg'
        scrollBehavior='inside'
      >
        <ModalContent>
          <ModalHeader className='flex items-center gap-2'>
            <FontAwesomeIcon
              icon={faClockRotateLeft}
              className='text-primary'
            />
            Version History — {versionFile?.name}
          </ModalHeader>
          <ModalBody>
            {versionLoading ? (
              <div className='flex justify-center py-8'>
                <Spinner />
              </div>
            ) : (
              <>
                {/* Upload new version */}
                {canEdit(myPermission) && (
                  <div className='mb-4'>
                    <Button
                      size='sm'
                      variant='flat'
                      startContent={<FontAwesomeIcon icon={faUpload} />}
                      isLoading={versionUploading}
                      onPress={() => versionFileInputRef.current?.click()}
                    >
                      {versionUploading && versionUploadProgress !== null
                        ? `Uploading ${versionUploadProgress}%`
                        : 'Upload New Version'}
                    </Button>
                    <input
                      ref={versionFileInputRef}
                      type='file'
                      className='hidden'
                      onChange={handleVersionUpload}
                    />
                  </div>
                )}

                {versions.length > 0 ? (
                  <div className='space-y-2'>
                    {versions.map((v, idx) => (
                      <div
                        key={v.id}
                        className={`flex items-center justify-between px-3 py-2 rounded-lg ${idx === 0 ? 'bg-primary/10 border border-primary/30' : 'bg-content2/50'}`}
                      >
                        <div className='min-w-0'>
                          <div className='flex items-center gap-2'>
                            <span className='text-sm font-medium'>
                              v{v.version_number}
                            </span>
                            {idx === 0 && (
                              <span className='text-[10px] bg-primary/20 text-primary px-1.5 py-0.5 rounded-full'>
                                current
                              </span>
                            )}
                          </div>
                          <p className='text-xs text-default-400'>
                            {formatDateTime(v.created_at)} by{' '}
                            {v.uploaded_by_username} — {formatSize(v.file_size)}
                          </p>
                        </div>
                        <div className='flex items-center gap-1'>
                          <Button
                            isIconOnly
                            size='sm'
                            variant='light'
                            title='Download this version'
                            onPress={() =>
                              handleVersionDownload(
                                v,
                                versionFile?.name || 'file',
                              )
                            }
                          >
                            <FontAwesomeIcon
                              icon={faDownload}
                              className='text-xs'
                            />
                          </Button>
                          {idx !== 0 && canEdit(myPermission) && (
                            <Button
                              isIconOnly
                              size='sm'
                              variant='light'
                              color='warning'
                              title='Revert to this version'
                              onPress={() => handleRevert(v.id)}
                            >
                              <FontAwesomeIcon
                                icon={faRotateLeft}
                                className='text-xs'
                              />
                            </Button>
                          )}
                        </div>
                      </div>
                    ))}
                  </div>
                ) : (
                  <p className='text-sm text-default-400 text-center py-4'>
                    No version history available.
                  </p>
                )}
              </>
            )}
          </ModalBody>
          <ModalFooter>
            <Button variant='flat' onPress={() => setVersionFile(null)}>
              Close
            </Button>
          </ModalFooter>
        </ModalContent>
      </Modal>

      {/* Preview Modal */}
      <Modal
        isOpen={!!previewFile}
        onClose={() => setPreviewFile(null)}
        size='5xl'
        scrollBehavior='normal'
        classNames={{
          wrapper: 'overflow-hidden items-center',
          body: 'px-3 pb-3',
        }}
      >
        <ModalContent>
          <ModalHeader className='flex items-center gap-2 pr-20'>
            <FontAwesomeIcon icon={faEye} className='text-primary' />
            <span className='truncate'>{previewFile?.name}</span>
            <Button
              isIconOnly
              size='sm'
              variant='light'
              className='absolute top-1 right-10'
              onPress={() => {
                if (previewFile) handleDownload(previewFile);
              }}
              title='Download'
            >
              <FontAwesomeIcon icon={faDownload} className='text-xs' />
            </Button>
          </ModalHeader>
          <ModalBody>
            {previewFile && (
              <PreviewContent
                mime={previewFile.mime_type || ''}
                url={previewUrl}
                textContent={previewContent}
                loading={previewLoading}
              />
            )}
          </ModalBody>
        </ModalContent>
      </Modal>
    </div>
  );
}

function PreviewContent({
  mime,
  url,
  textContent,
  loading,
}: {
  mime: string;
  url: string;
  textContent: string | null;
  loading: boolean;
}) {
  if (mime.startsWith('image/')) {
    return (
      <div className='flex justify-center'>
        <img
          src={url}
          alt='Preview'
          className='max-w-full max-h-[calc(80dvh-5rem)] object-contain rounded'
        />
      </div>
    );
  }

  if (mime === 'application/pdf') {
    return (
      <iframe
        src={url}
        className='w-full h-[calc(80dvh-5rem)] rounded border border-divider'
        title='PDF Preview'
      />
    );
  }

  if (mime.startsWith('video/')) {
    return (
      <div className='flex justify-center'>
        <video
          src={url}
          controls
          className='max-w-full max-h-[calc(80dvh-5rem)] rounded'
        />
      </div>
    );
  }

  if (mime.startsWith('audio/')) {
    return (
      <div className='flex justify-center py-8'>
        <audio src={url} controls className='w-full max-w-lg' />
      </div>
    );
  }

  // Text / code
  if (loading) {
    return (
      <div className='flex justify-center py-8'>
        <Spinner />
      </div>
    );
  }

  if (textContent !== null) {
    return (
      <pre className='bg-content2 rounded-lg p-4 text-sm overflow-auto max-h-[70vh] whitespace-pre-wrap break-words font-mono text-default-700'>
        {textContent}
      </pre>
    );
  }

  return (
    <p className='text-sm text-default-400 text-center py-8'>
      No preview available for this file type.
    </p>
  );
}
