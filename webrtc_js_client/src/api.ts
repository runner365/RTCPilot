import { ref } from 'vue'

const token = ref<string>('')

export function getToken(): string {
  return token.value
}

export function setToken(t: string): void {
  token.value = t
}

function makeHeaders(): Record<string, string> {
  const headers: Record<string, string> = {
    'Content-Type': 'application/json',
  }
  const t = getToken()
  if (t) {
    headers['Authorization'] = `Bearer ${t}`
  }
  return headers
}

export async function apiGet<T = any>(url: string): Promise<T> {
  const resp = await fetch(url, {
    method: 'GET',
    headers: makeHeaders(),
  })
  const data = await resp.json()
  if (!resp.ok) {
    throw new Error((data.error as string) || `HTTP ${resp.status}`)
  }
  return data as T
}

export async function apiPost<T = any>(url: string, body?: unknown): Promise<T> {
  const resp = await fetch(url, {
    method: 'POST',
    headers: makeHeaders(),
    body: body !== undefined ? JSON.stringify(body) : undefined,
  })
  const data = await resp.json()
  if (!resp.ok) {
    throw new Error((data.error as string) || `HTTP ${resp.status}`)
  }
  return data as T
}
