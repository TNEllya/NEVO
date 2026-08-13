interface ToggleSwitchProps {
  checked: boolean;
  onChange: () => void;
  label?: string;
}

export default function ToggleSwitch({ checked, onChange, label }: ToggleSwitchProps) {
  return (
    <div className="flex items-center gap-4 mb-3">
      {label && (
        <label
          className="shrink-0"
          style={{
            width: 140,
            minWidth: 140,
            fontSize: 13,
            color: "var(--color-text-secondary)",
          }}
        >
          {label}
        </label>
      )}
      <div
        onClick={onChange}
        className="cursor-pointer relative shrink-0 transition-colors duration-fast"
        style={{
          width: 40,
          height: 22,
          background: checked
            ? "var(--color-primary)"
            : "var(--color-bg-active)",
          borderRadius: 9999,
        }}
      >
        <div
          className="absolute rounded-full bg-white transition-all duration-fast"
          style={{
            width: 18,
            height: 18,
            top: 2,
            left: checked ? 20 : 2,
          }}
        />
      </div>
      <span style={{ fontSize: 13, color: "var(--color-text-tertiary)" }}>
        {checked ? "开启" : "关闭"}
      </span>
    </div>
  );
}
