/** @type {import('tailwindcss').Config} */

export default {
  darkMode: "class",
  content: ["./index.html", "./src/**/*.{js,ts,jsx,tsx}"],
  theme: {
    container: {
      center: true,
    },
    extend: {
      colors: {
        primary: {
          DEFAULT: "var(--color-primary)",
          hover: "var(--color-primary-hover)",
          active: "var(--color-primary-active)",
          muted: "var(--color-primary-muted)",
          foreground: "var(--color-primary-foreground)",
        },
        bg: {
          base: "var(--color-bg-base)",
          surface: "var(--color-bg-surface)",
          elevated: "var(--color-bg-elevated)",
          overlay: "var(--color-bg-overlay)",
          hover: "var(--color-bg-hover)",
          active: "var(--color-bg-active)",
        },
        text: {
          primary: "var(--color-text-primary)",
          secondary: "var(--color-text-secondary)",
          tertiary: "var(--color-text-tertiary)",
          inverse: "var(--color-text-inverse)",
        },
        border: {
          DEFAULT: "var(--color-border)",
          hover: "var(--color-border-hover)",
          active: "var(--color-border-active)",
        },
        state: {
          success: "var(--state-success)",
          warning: "var(--state-warning)",
          error: "var(--state-error)",
          info: "var(--state-info)",
        },
        voice: {
          active: "var(--color-voice-active)",
          idle: "var(--color-voice-idle)",
        },
      },
      fontFamily: {
        display: ["Inter", "sans-serif"],
        body: ["Inter", "sans-serif"],
        mono: ["JetBrains Mono", "monospace"],
      },
      borderRadius: {
        sm: "4px",
        md: "8px",
        lg: "12px",
      },
      transitionDuration: {
        fast: "150ms",
        normal: "200ms",
        slow: "300ms",
      },
      animation: {
        "voice-bar": "voice-bar 0.4s ease-in-out infinite alternate",
        "voice-bar-idle": "voice-bar-idle 1.2s ease-in-out infinite alternate",
        "pulse-dot": "pulse-dot 2s ease-in-out infinite",
        "meter-pulse": "meter-pulse 2s ease-in-out infinite",
      },
    },
  },
  plugins: [],
};
