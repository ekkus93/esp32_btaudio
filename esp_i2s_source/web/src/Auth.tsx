// FIX3 11.2: token-entry panel. Opens automatically on AUTH_REQUIRED/401
// (via api.ts's onAuthRequired subscription) or manually via the header
// lock button. Never renders the token as plain text; never puts it in a
// URL/query string (api.ts only ever sends it as an Authorization header).
import { useEffect, useState } from "react";
import { getAuthToken, setAuthToken, clearAuthToken, onAuthRequired } from "./api";

const TOKEN_RE = /^[0-9a-f]{64}$/;

export function AuthPanel() {
  const [open, setOpen] = useState(false);
  const [value, setValue] = useState("");
  const [remember, setRemember] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [hasToken, setHasToken] = useState(false);

  useEffect(() => {
    setHasToken(!!getAuthToken());
    return onAuthRequired(() => {
      setError("A device token is required for this action.");
      setOpen(true);
    });
  }, []);

  const save = (e: React.FormEvent) => {
    e.preventDefault();
    try {
      setAuthToken(value.trim(), remember);
      setHasToken(true);
      setError(null);
      setValue("");
      setOpen(false);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  };

  const clear = () => {
    clearAuthToken();
    setHasToken(false);
    setValue("");
  };

  return (
    <>
      <button
        type="button"
        className={`auth-toggle ${hasToken ? "ok" : "bad"}`}
        title={hasToken ? "Device token set" : "Device token required for changes"}
        onClick={() => setOpen((o) => !o)}
      >
        {hasToken ? "🔓" : "🔒"}
      </button>

      {open && (
        <div className="auth-panel">
          <form onSubmit={save}>
            <label>
              Device token
              <input
                type="password"
                inputMode="text"
                autoComplete="off"
                spellCheck={false}
                value={value}
                onChange={(e) => setValue(e.target.value)}
                placeholder="64 lowercase hex characters"
              />
            </label>
            <label className="auth-remember">
              <input
                type="checkbox"
                checked={remember}
                onChange={(e) => setRemember(e.target.checked)}
              />
              Remember on this browser
            </label>
            {error && <div className="banner err">{error}</div>}
            <div className="auth-actions">
              <button type="submit" className="primary" disabled={!TOKEN_RE.test(value.trim())}>
                Save
              </button>
              {hasToken && (
                <button type="button" onClick={clear}>
                  Clear token
                </button>
              )}
              <button type="button" onClick={() => setOpen(false)}>
                Close
              </button>
            </div>
          </form>
        </div>
      )}
    </>
  );
}
