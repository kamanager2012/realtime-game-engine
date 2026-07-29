function isLocalHost(hostname: string): boolean {
  return hostname === 'localhost' || hostname === '127.0.0.1';
}

export function getApiBaseUrl(): string {
  if (typeof window === 'undefined') return '';
  return isLocalHost(window.location.hostname) ? 'http://localhost:9001' : '';
}

export function getWebSocketBaseUrl(): string {
  const { hostname, host, protocol } = window.location;
  if (isLocalHost(hostname)) return 'ws://127.0.0.1:9001';
  return `${protocol === 'https:' ? 'wss:' : 'ws:'}//${host}`;
}


export interface AccountInfo {
  player_id: number;
  chips: number;
  display_name?: string;
}

export async function fetchAccountChips(token: string): Promise<AccountInfo | null> {
  if (!token) return null;
  try {
    const resp = await fetch(`${getApiBaseUrl()}/api/account`, {
      headers: { Authorization: `Bearer ${token}` },
    });
    if (!resp.ok) return null;
    return (await resp.json()) as AccountInfo;
  } catch {
    return null;
  }
}
