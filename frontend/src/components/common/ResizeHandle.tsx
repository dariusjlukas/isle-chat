interface ResizeHandleProps {
  onMouseDown: (e: React.MouseEvent) => void;
  isResizing: boolean;
}

export function ResizeHandle({ onMouseDown, isResizing }: ResizeHandleProps) {
  return (
    <div className='flex-shrink-0 relative w-0 z-20'>
      <div
        className={`absolute inset-y-0 -left-[3px] w-[6px] cursor-col-resize select-none transition-colors duration-150 ${
          isResizing ? 'bg-primary/40' : 'hover:bg-primary/30'
        }`}
        onMouseDown={onMouseDown}
      />
    </div>
  );
}
