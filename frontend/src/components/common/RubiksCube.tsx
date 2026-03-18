import { useRef, useState, useCallback, useMemo, useEffect } from 'react';
import { Canvas, useFrame } from '@react-three/fiber';
import type { ThreeEvent } from '@react-three/fiber';
import { OrbitControls } from '@react-three/drei';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
  faArrowsRotate,
  faTrophy,
  faWandMagicSparkles,
} from '@fortawesome/free-solid-svg-icons';
import * as THREE from 'three';

// ── Types ───────────────────────────────────────────────────────────────────

type FaceName = 'U' | 'D' | 'L' | 'R' | 'F' | 'B';
type Move =
  | 'U'
  | "U'"
  | 'D'
  | "D'"
  | 'L'
  | "L'"
  | 'R'
  | "R'"
  | 'F'
  | "F'"
  | 'B'
  | "B'";

// ── Colors ──────────────────────────────────────────────────────────────────

const FACE_COLORS: Record<FaceName, string> = {
  U: '#ffffff', // white
  D: '#ffd500', // yellow
  F: '#009b48', // green
  B: '#0045ad', // blue
  R: '#b90000', // red
  L: '#ff5900', // orange
};

const INNER_COLOR = '#1a1a1a';

// ── Cube state: 6 faces × 9 stickers ───────────────────────────────────────
// Each face is a 3×3 array stored as flat [0..8], reading order:
//   0 1 2
//   3 4 5
//   6 7 8

type CubeState = Record<FaceName, FaceName[]>;

function solvedState(): CubeState {
  const faces: FaceName[] = ['U', 'D', 'L', 'R', 'F', 'B'];
  const state = {} as CubeState;
  for (const f of faces) {
    state[f] = Array(9).fill(f);
  }
  return state;
}

function cloneState(s: CubeState): CubeState {
  const c = {} as CubeState;
  for (const f of ['U', 'D', 'L', 'R', 'F', 'B'] as FaceName[]) {
    c[f] = [...s[f]];
  }
  return c;
}

// CW rotation transform for each face, matching the Three.js animation.
// Each maps [x,y,z] → [x',y',z'] for one 90° CW turn as seen from that face.
type Vec3 = [number, number, number];
type DirName = 'px' | 'nx' | 'py' | 'ny' | 'pz' | 'nz';
const DIR_VECS: Record<DirName, Vec3> = {
  px: [1, 0, 0],
  nx: [-1, 0, 0],
  py: [0, 1, 0],
  ny: [0, -1, 0],
  pz: [0, 0, 1],
  nz: [0, 0, -1],
};
const VEC_TO_DIR = new Map<string, DirName>([
  ['1,0,0', 'px'],
  ['-1,0,0', 'nx'],
  ['0,1,0', 'py'],
  ['0,-1,0', 'ny'],
  ['0,0,1', 'pz'],
  ['0,0,-1', 'nz'],
]);

function faceCWTransform(face: FaceName, [x, y, z]: Vec3): Vec3 {
  switch (face) {
    // Derived from the Euler rotation matrices in faceRotationEuler
    case 'U':
      return [-z, y, x]; // Euler(0, -π/2, 0)
    case 'D':
      return [z, y, -x]; // Euler(0, +π/2, 0)
    case 'R':
      return [x, z, -y]; // Euler(-π/2, 0, 0)
    case 'L':
      return [x, -z, y]; // Euler(+π/2, 0, 0)
    case 'F':
      return [y, -x, z]; // Euler(0, 0, -π/2)
    case 'B':
      return [-y, x, z]; // Euler(0, 0, +π/2)
  }
}

function onFace(face: FaceName, x: number, y: number, z: number): boolean {
  switch (face) {
    case 'U':
      return y === 1;
    case 'D':
      return y === -1;
    case 'R':
      return x === 1;
    case 'L':
      return x === -1;
    case 'F':
      return z === 1;
    case 'B':
      return z === -1;
  }
}

/**
 * Apply a move by physically rotating cubies and remapping stickers.
 * Instead of hand-coded edge cycles, this derives the correct mapping
 * from the rotation transform and getStickerInfo — correct by construction.
 */
function applyMove(state: CubeState, move: Move): CubeState {
  const face = move[0] as FaceName;
  const prime = move.length > 1;
  const times = prime ? 3 : 1;
  const s = cloneState(state);

  for (let t = 0; t < times; t++) {
    const prev = cloneState(s);

    // Rotate ALL stickers (face + edges) via the physical transform.
    // No separate rotateFaceCW needed — the transform handles everything
    // correctly regardless of each face's index layout orientation.
    for (let cx = -1; cx <= 1; cx++) {
      for (let cy = -1; cy <= 1; cy++) {
        for (let cz = -1; cz <= 1; cz++) {
          if (!onFace(face, cx, cy, cz)) continue;
          const np = faceCWTransform(face, [cx, cy, cz]);
          for (const dn of Object.keys(DIR_VECS) as DirName[]) {
            const oldInfo = getStickerInfo(cx, cy, cz, dn);
            if (!oldInfo) continue;
            const ndv = faceCWTransform(face, DIR_VECS[dn]);
            const ndn = VEC_TO_DIR.get(ndv.join(','));
            if (!ndn) continue;
            const newInfo = getStickerInfo(np[0], np[1], np[2], ndn);
            if (!newInfo) continue;
            s[newInfo.face][newInfo.index] = prev[oldInfo.face][oldInfo.index];
          }
        }
      }
    }
  }
  return s;
}

function invertMove(move: Move): Move {
  return move.length > 1 ? (move[0] as Move) : (`${move}'` as Move);
}

function isSolved(state: CubeState): boolean {
  for (const f of ['U', 'D', 'L', 'R', 'F', 'B'] as FaceName[]) {
    if (!state[f].every((c) => c === state[f][4])) return false;
  }
  return true;
}

/** Scramble with n random moves, avoiding back-to-back same-face moves. */
function scramble(n: number): Move[] {
  const allMoves: Move[] = [
    'U',
    "U'",
    'D',
    "D'",
    'L',
    "L'",
    'R',
    "R'",
    'F',
    "F'",
    'B',
    "B'",
  ];
  const moves: Move[] = [];
  let lastFace = '';
  for (let i = 0; i < n; i++) {
    let move: Move;
    do {
      move = allMoves[Math.floor(Math.random() * allMoves.length)];
    } while (move[0] === lastFace);
    moves.push(move);
    lastFace = move[0];
  }
  return moves;
}

// ── 3D Cubie mesh ───────────────────────────────────────────────────────────

// Position in grid: x, y, z each in {-1, 0, 1}
// Map each cubie position + direction to a face+sticker index

interface StickerInfo {
  face: FaceName;
  index: number;
}

function getStickerInfo(
  x: number,
  y: number,
  z: number,
  dir: 'px' | 'nx' | 'py' | 'ny' | 'pz' | 'nz',
): StickerInfo | null {
  // Map cubie grid pos to sticker. Grid coords: x=-1..1, y=-1..1, z=-1..1
  // Face U = y=1, D = y=-1, R = x=1, L = x=-1, F = z=1, B = z=-1
  const col = x + 1; // 0,1,2
  const row = 1 - y; // 0,1,2 (top=0)
  const dep = z + 1; // 0,1,2

  switch (dir) {
    case 'py':
      return y === 1 ? { face: 'U', index: (2 - dep) * 3 + col } : null;
    case 'ny':
      return y === -1 ? { face: 'D', index: dep * 3 + col } : null;
    case 'px':
      return x === 1 ? { face: 'R', index: row * 3 + (2 - dep) } : null;
    case 'nx':
      return x === -1 ? { face: 'L', index: row * 3 + dep } : null;
    case 'pz':
      return z === 1 ? { face: 'F', index: row * 3 + col } : null;
    case 'nz':
      return z === -1 ? { face: 'B', index: row * 3 + (2 - col) } : null;
  }
}

const CUBIE_SIZE = 0.93;
const GAP = 1.0;
const STICKER_INSET = 0.08;

const stickerGeom = new THREE.PlaneGeometry(
  CUBIE_SIZE - STICKER_INSET * 2,
  CUBIE_SIZE - STICKER_INSET * 2,
);
const cubieGeom = new THREE.BoxGeometry(CUBIE_SIZE, CUBIE_SIZE, CUBIE_SIZE);

const STICKER_DIRS: Array<{
  dir: 'px' | 'nx' | 'py' | 'ny' | 'pz' | 'nz';
  pos: [number, number, number];
  rot: [number, number, number];
}> = [
  { dir: 'px', pos: [CUBIE_SIZE / 2 + 0.001, 0, 0], rot: [0, Math.PI / 2, 0] },
  {
    dir: 'nx',
    pos: [-CUBIE_SIZE / 2 - 0.001, 0, 0],
    rot: [0, -Math.PI / 2, 0],
  },
  { dir: 'py', pos: [0, CUBIE_SIZE / 2 + 0.001, 0], rot: [-Math.PI / 2, 0, 0] },
  { dir: 'ny', pos: [0, -CUBIE_SIZE / 2 - 0.001, 0], rot: [Math.PI / 2, 0, 0] },
  { dir: 'pz', pos: [0, 0, CUBIE_SIZE / 2 + 0.001], rot: [0, 0, 0] },
  { dir: 'nz', pos: [0, 0, -CUBIE_SIZE / 2 - 0.001], rot: [0, Math.PI, 0] },
];

/** A single cubie at grid position (gx, gy, gz). No animation logic — the
 *  parent group handles face rotation. */
function Cubie({
  gx,
  gy,
  gz,
  cubeState,
}: {
  gx: number;
  gy: number;
  gz: number;
  cubeState: CubeState;
}) {
  return (
    <group position={[gx * GAP, gy * GAP, gz * GAP]}>
      {/* Black cubie body */}
      <mesh geometry={cubieGeom}>
        <meshStandardMaterial color={INNER_COLOR} roughness={0.4} />
      </mesh>

      {/* Stickers */}
      {STICKER_DIRS.map(({ dir, pos, rot }) => {
        const info = getStickerInfo(gx, gy, gz, dir);
        if (!info) return null;
        const colorName = cubeState[info.face][info.index];
        return (
          <mesh
            key={dir}
            geometry={stickerGeom}
            position={pos}
            rotation={new THREE.Euler(...rot)}
          >
            <meshStandardMaterial
              color={FACE_COLORS[colorName]}
              roughness={0.3}
              metalness={0.1}
            />
          </mesh>
        );
      })}
    </group>
  );
}

function faceRotationEuler(face: FaceName, angle: number): THREE.Euler {
  switch (face) {
    case 'U':
      return new THREE.Euler(0, -angle, 0);
    case 'D':
      return new THREE.Euler(0, angle, 0);
    case 'R':
      return new THREE.Euler(-angle, 0, 0);
    case 'L':
      return new THREE.Euler(angle, 0, 0);
    case 'F':
      return new THREE.Euler(0, 0, -angle);
    case 'B':
      return new THREE.Euler(0, 0, angle);
  }
}

// ── Clickable face overlay for move input ───────────────────────────────────

const DRAG_THRESHOLD = 5; // pixels — movement beyond this counts as a drag

function FaceClickZone({
  face,
  onMove,
}: {
  face: FaceName;
  onMove: (move: Move) => void;
}) {
  const size = 3.2;
  const offset = 1.55;
  const pointerStart = useRef<{ x: number; y: number } | null>(null);

  const posRot: {
    position: [number, number, number];
    rotation: [number, number, number];
  } = useMemo(() => {
    switch (face) {
      case 'U':
        return { position: [0, offset, 0], rotation: [-Math.PI / 2, 0, 0] };
      case 'D':
        return { position: [0, -offset, 0], rotation: [Math.PI / 2, 0, 0] };
      case 'R':
        return { position: [offset, 0, 0], rotation: [0, Math.PI / 2, 0] };
      case 'L':
        return { position: [-offset, 0, 0], rotation: [0, -Math.PI / 2, 0] };
      case 'F':
        return { position: [0, 0, offset], rotation: [0, 0, 0] };
      case 'B':
        return { position: [0, 0, -offset], rotation: [0, Math.PI, 0] };
    }
  }, [face]);

  const handlePointerDown = useCallback((e: ThreeEvent<PointerEvent>) => {
    pointerStart.current = {
      x: e.nativeEvent.clientX,
      y: e.nativeEvent.clientY,
    };
  }, []);

  const wasDrag = useCallback(
    (e: { nativeEvent: { clientX: number; clientY: number } }) => {
      if (!pointerStart.current) return true;
      const dx = e.nativeEvent.clientX - pointerStart.current.x;
      const dy = e.nativeEvent.clientY - pointerStart.current.y;
      return Math.sqrt(dx * dx + dy * dy) > DRAG_THRESHOLD;
    },
    [],
  );

  const handleClick = useCallback(
    (e: ThreeEvent<MouseEvent>) => {
      e.stopPropagation();
      if (wasDrag(e)) return;
      const move: Move = e.nativeEvent.shiftKey ? (`${face}'` as Move) : face;
      onMove(move);
    },
    [face, onMove, wasDrag],
  );

  const handleContextMenu = useCallback(
    (e: ThreeEvent<MouseEvent>) => {
      e.stopPropagation();
      e.nativeEvent.preventDefault();
      if (wasDrag(e)) return;
      onMove(`${face}'` as Move);
    },
    [face, onMove, wasDrag],
  );

  return (
    <mesh
      position={posRot.position}
      rotation={new THREE.Euler(...posRot.rotation)}
      onPointerDown={handlePointerDown}
      onClick={handleClick}
      onContextMenu={handleContextMenu}
    >
      <planeGeometry args={[size, size]} />
      <meshBasicMaterial transparent opacity={0} side={THREE.DoubleSide} />
    </mesh>
  );
}

// ── Scene ───────────────────────────────────────────────────────────────────

const ANIM_SPEED = 8; // radians per second

const COORDS = [-1, 0, 1];
const ALL_FACES: FaceName[] = ['U', 'D', 'L', 'R', 'F', 'B'];

function CubeScene({
  cubeState,
  onMove,
  animatingFace,
  animAngle,
}: {
  cubeState: CubeState;
  onMove: (move: Move) => void;
  animatingFace: FaceName | null;
  animAngle: number;
}) {
  const animRotation = animatingFace
    ? faceRotationEuler(animatingFace, animAngle)
    : undefined;

  return (
    <>
      <ambientLight intensity={0.6} />
      <directionalLight position={[5, 8, 5]} intensity={0.8} />
      <directionalLight position={[-3, -2, 4]} intensity={0.3} />

      {/* Static cubies (not on the animating face) */}
      {COORDS.map((x) =>
        COORDS.map((y) =>
          COORDS.map((z) => {
            if (animatingFace && onFace(animatingFace, x, y, z)) return null;
            return (
              <Cubie
                key={`${x},${y},${z}`}
                gx={x}
                gy={y}
                gz={z}
                cubeState={cubeState}
              />
            );
          }),
        ),
      )}

      {/* Animated face group — rotates all 9 cubies around the world origin */}
      {animatingFace && (
        <group rotation={animRotation}>
          {COORDS.map((x) =>
            COORDS.map((y) =>
              COORDS.map((z) => {
                if (!onFace(animatingFace, x, y, z)) return null;
                return (
                  <Cubie
                    key={`a${x},${y},${z}`}
                    gx={x}
                    gy={y}
                    gz={z}
                    cubeState={cubeState}
                  />
                );
              }),
            ),
          )}
        </group>
      )}

      {/* Invisible click zones on each face */}
      {ALL_FACES.map((f) => (
        <FaceClickZone key={f} face={f} onMove={onMove} />
      ))}

      <OrbitControls
        enablePan={false}
        enableZoom={true}
        minDistance={6}
        maxDistance={12}
        dampingFactor={0.15}
      />
    </>
  );
}

// ── Animation driver ────────────────────────────────────────────────────────

function AnimationDriver({
  targetAngle,
  speed,
  onAngleUpdate,
  onComplete,
}: {
  targetAngle: number;
  speed: number;
  onAngleUpdate: (angle: number) => void;
  onComplete: () => void;
}) {
  const angleRef = useRef(0);
  const doneRef = useRef(false);

  useEffect(() => {
    angleRef.current = 0;
    doneRef.current = false;
  }, [targetAngle]);

  useFrame((state, delta) => {
    if (doneRef.current) return;
    state.invalidate(); // Request next frame to keep animation running
    angleRef.current += delta * speed;
    if (angleRef.current >= Math.abs(targetAngle)) {
      angleRef.current = Math.abs(targetAngle);
      doneRef.current = true;
      onAngleUpdate(0);
      onComplete();
    } else {
      onAngleUpdate(angleRef.current * Math.sign(targetAngle));
    }
  });

  return null;
}

// ── Main component ──────────────────────────────────────────────────────────

export function RubiksCube({ large }: { large?: boolean } = {}) {
  const [cubeState, setCubeState] = useState<CubeState>(solvedState);
  const [moveCount, setMoveCount] = useState(0);
  const [solved, setSolved] = useState(false);
  const [scrambleLength, setScrambleLength] = useState(5);
  const [moveQueue, setMoveQueue] = useState<Move[]>([]);
  const [animatingMove, setAnimatingMove] = useState<Move | null>(null);
  const [animAngle, setAnimAngle] = useState(0);
  const [started, setStarted] = useState(false);
  const [solving, setSolving] = useState(false);
  const [autoSolved, setAutoSolved] = useState(false);
  const moveHistoryRef = useRef<Move[]>([]);

  const animatingFace = animatingMove ? (animatingMove[0] as FaceName) : null;
  const targetAngle = animatingMove
    ? animatingMove.length > 1
      ? -Math.PI / 2
      : Math.PI / 2
    : 0;

  // Process move queue
  useEffect(() => {
    if (animatingMove) return;
    if (moveQueue.length === 0) return;
    const [next, ...rest] = moveQueue;
    setAnimatingMove(next);
    setMoveQueue(rest);
  }, [moveQueue, animatingMove]);

  const handleAnimComplete = useCallback(() => {
    if (!animatingMove) return;
    if (!solving) {
      moveHistoryRef.current.push(animatingMove);
    }
    setCubeState((prev) => {
      const next = applyMove(prev, animatingMove);
      if (started && isSolved(next)) {
        setSolved(true);
        if (solving) setSolving(false);
      }
      return next;
    });
    setAnimatingMove(null);
  }, [animatingMove, started, solving]);

  const handleMove = useCallback(
    (move: Move) => {
      if (solved || solving) return;
      if (!started) return;
      setMoveQueue((q) => [...q, move]);
      setMoveCount((c) => c + 1);
    },
    [solved, solving, started],
  );

  const doScramble = useCallback((len: number) => {
    const moves = scramble(len);
    // Apply all scramble moves instantly (no animation)
    let state = solvedState();
    for (const m of moves) {
      state = applyMove(state, m);
    }
    setCubeState(state);
    setSolved(false);
    setMoveCount(0);
    setMoveQueue([]);
    setAnimatingMove(null);
    setAnimAngle(0);
    setStarted(true);
    setSolving(false);
    setAutoSolved(false);
    moveHistoryRef.current = [...moves];
  }, []);

  // Initial scramble
  useEffect(() => {
    doScramble(scrambleLength);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const newGame = useCallback(
    (len: number) => {
      setScrambleLength(len);
      doScramble(len);
    },
    [doScramble],
  );

  const handleSolve = useCallback(() => {
    if (solved || solving || !started) return;

    // Apply any in-progress animation and queued moves instantly
    let state = cubeState;
    const pending: Move[] = [];
    if (animatingMove) pending.push(animatingMove);
    pending.push(...moveQueue);

    for (const m of pending) {
      state = applyMove(state, m);
      moveHistoryRef.current.push(m);
    }

    setCubeState(state);
    setAnimatingMove(null);
    setAnimAngle(0);

    // Compute solution: reverse all history and invert each move
    const solutionMoves = [...moveHistoryRef.current].reverse().map(invertMove);

    setSolving(true);
    setAutoSolved(true);
    setMoveQueue(solutionMoves);
    moveHistoryRef.current = [];
  }, [solved, solving, started, cubeState, animatingMove, moveQueue]);

  return (
    <div className='flex flex-col items-center gap-2.5 select-none'>
      {/* Difficulty selector */}
      <div className='flex gap-1'>
        {(
          [
            { label: 'easy', moves: 5 },
            { label: 'medium', moves: 12 },
            { label: 'hard', moves: 20 },
          ] as const
        ).map(({ label, moves }) => (
          <button
            key={label}
            onClick={() => newGame(moves)}
            className={`
              px-2.5 py-0.5 rounded text-xs font-medium capitalize cursor-pointer
              transition-colors
              ${
                scrambleLength === moves
                  ? 'bg-primary text-primary-foreground'
                  : 'bg-default-100 text-default-500 hover:bg-default-200'
              }
            `}
          >
            {label}
          </button>
        ))}
      </div>

      {/* Status */}
      <div className='flex items-center justify-between w-full px-1 text-xs'>
        <span className='text-default-400'>
          {solving ? 'Solving...' : `Moves: ${moveCount}`}
        </span>
        <div className='flex items-center gap-2'>
          {started && !solved && !solving && (
            <button
              onClick={handleSolve}
              className='cursor-pointer hover:scale-110 transition-transform text-default-400'
              title='Show solution'
            >
              <FontAwesomeIcon icon={faWandMagicSparkles} />
            </button>
          )}
          <button
            onClick={() => doScramble(scrambleLength)}
            className='cursor-pointer hover:scale-110 transition-transform text-default-400'
            title='New scramble'
          >
            <FontAwesomeIcon icon={solved ? faTrophy : faArrowsRotate} />
          </button>
        </div>
      </div>

      {/* 3D Canvas */}
      <div
        className='rounded-lg overflow-hidden rubiks-viewport'
        style={{
          width: '100%',
          maxWidth: large ? 800 : 600,
          maxHeight: large ? 'calc(100vh - 400px)' : undefined,
          aspectRatio: '1',
          background: '#0d1520',
        }}
        onContextMenu={(e) => e.preventDefault()}
      >
        <Canvas frameloop='demand' camera={{ position: [5, 4, 5], fov: 45 }}>
          <CubeScene
            cubeState={cubeState}
            onMove={handleMove}
            animatingFace={animatingFace}
            animAngle={animAngle}
          />
          {animatingMove && (
            <AnimationDriver
              targetAngle={targetAngle}
              speed={solving ? 12 : ANIM_SPEED}
              onAngleUpdate={setAnimAngle}
              onComplete={handleAnimComplete}
            />
          )}
        </Canvas>
      </div>

      {/* Status text */}
      {solved && !autoSolved && (
        <p className='text-sm font-medium text-success'>
          Solved in {moveCount} moves!
        </p>
      )}
      {solved && autoSolved && (
        <p className='text-sm font-medium text-success'>Cube solved!</p>
      )}
      {!solved && solving && (
        <p className='text-xs text-default-400'>Solving...</p>
      )}
      {!solved && !solving && (
        <p className='text-xs text-default-400'>
          Click a face to rotate CW. Shift+click or right-click for CCW.
        </p>
      )}
    </div>
  );
}
