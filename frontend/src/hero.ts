import { heroui } from '@heroui/react';

// --- Primary color scales ---

const modernPrimary = {
  50: '#f8fafc',
  100: '#f1f5f9',
  200: '#e2e8f0',
  300: '#cbd5e1',
  400: '#94a3b8',
  500: '#64748b',
  600: '#475569',
  700: '#334155',
  800: '#1e293b',
  900: '#0f172a',
};

const elegantPrimary = {
  50: '#fef7ff',
  100: '#fce4ff',
  200: '#f9c8ff',
  300: '#e8a5e0',
  400: '#d67bc8',
  500: '#b14f9a',
  600: '#993d82',
  700: '#7a2d67',
  800: '#5c1f4e',
  900: '#3e1235',
};

const coffeePrimary = {
  50: '#fef9f0',
  100: '#fdf0d5',
  200: '#fad9a1',
  300: '#f5be6b',
  400: '#e6a040',
  500: '#d4874a',
  600: '#b06d30',
  700: '#8b5422',
  800: '#6b3f18',
  900: '#4a2c10',
};

const emeraldPrimary = {
  50: '#ecfdf5',
  100: '#d1fae5',
  200: '#a7f3d0',
  300: '#6ee7b7',
  400: '#34d399',
  500: '#10b981',
  600: '#059669',
  700: '#047857',
  800: '#065f46',
  900: '#064e3b',
};

// --- Theme definitions ---

export default heroui({
  defaultTheme: 'dark',
  themes: {
    // Modern (Slate) — default
    'modern-light': {
      extend: 'light',
      colors: {
        background: '#ffffff',
        foreground: '#0f172a',
        primary: {
          ...modernPrimary,
          DEFAULT: '#475569',
          foreground: '#ffffff',
        },
        content1: '#f8fafc',
        content2: '#f1f5f9',
        content3: '#e2e8f0',
        content4: '#94a3b8',
        divider: '#e2e8f0',
      },
    },
    'modern-dark': {
      extend: 'dark',
      colors: {
        background: '#0f172a',
        foreground: '#f8fafc',
        primary: {
          ...modernPrimary,
          DEFAULT: '#94a3b8',
          foreground: '#0f172a',
        },
        content1: '#1e293b',
        content2: '#334155',
        content3: '#475569',
        content4: '#64748b',
        divider: '#334155',
      },
    },

    // Elegant (Purple/Rose)
    'elegant-light': {
      extend: 'light',
      colors: {
        background: '#fffbfe',
        foreground: '#1c1b1f',
        primary: {
          ...elegantPrimary,
          DEFAULT: '#b14f9a',
          foreground: '#ffffff',
        },
        content1: '#f7f2f7',
        content2: '#ede7ed',
        content3: '#ddd6dd',
        content4: '#a89da8',
        divider: '#ede7ed',
      },
    },
    'elegant-dark': {
      extend: 'dark',
      colors: {
        background: '#1c1b2e',
        foreground: '#f0ecf4',
        primary: {
          ...elegantPrimary,
          DEFAULT: '#d67bc8',
          foreground: '#1c1b2e',
        },
        content1: '#2a2840',
        content2: '#383552',
        content3: '#4a4764',
        content4: '#7a7690',
        divider: '#383552',
      },
    },

    // Coffee (Brown/Amber)
    'coffee-light': {
      extend: 'light',
      colors: {
        background: '#fffdf8',
        foreground: '#1a120b',
        primary: {
          ...coffeePrimary,
          DEFAULT: '#d4874a',
          foreground: '#ffffff',
        },
        content1: '#f8f3ec',
        content2: '#f0e6d8',
        content3: '#ddd0c0',
        content4: '#a89680',
        divider: '#f0e6d8',
      },
    },
    'coffee-dark': {
      extend: 'dark',
      colors: {
        background: '#1a120b',
        foreground: '#f5efe8',
        primary: {
          ...coffeePrimary,
          DEFAULT: '#e6a040',
          foreground: '#1a120b',
        },
        content1: '#2a1f14',
        content2: '#3d2e1f',
        content3: '#52402d',
        content4: '#7a6650',
        divider: '#3d2e1f',
      },
    },

    // Emerald (Green)
    'emerald-light': {
      extend: 'light',
      colors: {
        background: '#fafdfb',
        foreground: '#0c1a12',
        primary: {
          ...emeraldPrimary,
          DEFAULT: '#10b981',
          foreground: '#ffffff',
        },
        content1: '#f0faf4',
        content2: '#e2f5ea',
        content3: '#cee8d8',
        content4: '#86b89a',
        divider: '#e2f5ea',
      },
    },
    'emerald-dark': {
      extend: 'dark',
      colors: {
        background: '#0c1a12',
        foreground: '#ecfdf5',
        primary: {
          ...emeraldPrimary,
          DEFAULT: '#34d399',
          foreground: '#0c1a12',
        },
        content1: '#152e1e',
        content2: '#1e4030',
        content3: '#2d5542',
        content4: '#5a8a6e',
        divider: '#1e4030',
      },
    },
    // Classic (stock HeroUI — blue primary, pure white/black backgrounds)
    'classic-light': {
      extend: 'light',
    },
    'classic-dark': {
      extend: 'dark',
    },
  },
});
