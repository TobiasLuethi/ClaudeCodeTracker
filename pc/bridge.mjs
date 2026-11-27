// USB-Brücke: liest die Status-Dateien, die der Mod "touch-dashboard" pro Session schreibt
// (~/.claude/touch-dashboard/sessions/<id>.json), ergänzt Chatname und Link aus der Claude-App
// und schickt den Zustand jede Sekunde per USB-Seriell an das Gadget. Tippt man dort auf eine
// Kachel, öffnet die Brücke den Chat in der Claude-App.
// Start: node bridge.mjs   (Port automatisch; sonst GADGET_PORT=COM5 setzen)
import fs from 'node:fs/promises'
import path from 'node:path'
import os from 'node:os'
import { spawn } from 'node:child_process'
import { SerialPort } from 'serialport'
import { ReadlineParser } from '@serialport/parser-readline'

const ESPRESSIF_VID = '303a'
const SESSIONS = path.join(os.homedir(), '.claude', 'touch-dashboard', 'sessions')
const APP_DATA = path.join(process.env.LOCALAPPDATA ?? path.join(os.homedir(), 'AppData', 'Local'), 'Claude-Data')
const STALE_MS = 30_000 // Mod schreibt alle 3 s
const SEND_EVERY_MS = 1_000
const RECONNECT_MS = 2_000
const APP_CACHE_MS = 5_000
const MAX_SESSIONS = 12

const log = (...a) => console.log(new Date().toLocaleTimeString('de-CH'), ...a)

// --- Text für die Gadget-Schrift --------------------------------------------
// Das Gadget zeichnet Montserrat mit Latin-1 (ä ö ü é ß ...) und €. Andere Zeichen werden
// ersetzt oder entfernt. n ist die Grenze in UTF-8-Bytes (Puffergrössen im Gadget).
const KEEP = /[\x20-\x7e\xa0-\xff€]/
const clean = (s, n) => {
  let t = String(s ?? '')
    .normalize('NFC')
    .replace(/[„“”]/g, '"').replace(/[‘’‚‹›]/g, "'").replace(/[–—·•]/g, '-').replace(/…/g, '...')
    .replace(/\s+/g, ' ')
  t = [...t].map(c => (KEEP.test(c) ? c : c.normalize('NFD').replace(/\p{M}/gu, ''))).filter(c => KEEP.test(c)).join('').trim()
  if (Buffer.byteLength(t) <= n) return t
  let out = ''
  for (const c of t) {
    if (Buffer.byteLength(out + c) > n - 3) break
    out += c
  }
  return `${out.trimEnd()}...`
}

// --- Chats der Claude-App: cliSessionId -> { localId, title } ---------------
let appCache = { at: 0, map: new Map() }

const walk = async (dir, depth) => {
  const out = []
  for (const ent of await fs.readdir(dir, { withFileTypes: true }).catch(() => [])) {
    const full = path.join(dir, ent.name)
    if (ent.isDirectory() && depth > 0) out.push(...(await walk(full, depth - 1)))
    else if (ent.isFile() && /^local_[\w-]+\.json$/.test(ent.name)) out.push(full)
  }
  return out
}

const appSessions = async () => {
  if (Date.now() - appCache.at < APP_CACHE_MS) return appCache.map
  const map = new Map()
  for (const root of await fs.readdir(APP_DATA).catch(() => [])) {
    const dir = path.join(APP_DATA, root, 'claude-code-sessions')
    for (const file of await walk(dir, 3)) {
      try {
        const j = JSON.parse(await fs.readFile(file, 'utf8'))
        if (!j.cliSessionId || !j.sessionId) continue
        const prev = map.get(j.cliSessionId)
        if (prev && (prev.lastActivityAt ?? 0) > (j.lastActivityAt ?? 0)) continue
        map.set(j.cliSessionId, {
          localId: j.sessionId,
          title: j.title ?? '',
          isArchived: !!j.isArchived,
          lastActivityAt: j.lastActivityAt ?? 0,
        })
      } catch {
        // Datei gerade im Schreiben
      }
    }
  }
  appCache = { at: Date.now(), map }
  return map
}

// --- Status der Sessions ----------------------------------------------------
const RANK = { approval: 0, input: 1, working: 2, idle: 3 }

const detailOf = s => {
  if (s.state === 'input' && s.question) {
    const opts = s.question.options?.length ? ` [${s.question.options.join(' / ')}]` : ''
    return s.question.text + opts
  }
  if (s.state === 'approval' && s.pending?.length) {
    const p = s.pending[s.pending.length - 1]
    return `${p.agent ? `${p.agent}: ` : ''}${p.tool}: ${p.summary}`
  }
  return ''
}

const readSessions = async () => {
  const now = Date.now()
  const app = await appSessions()
  const out = []
  for (const f of await fs.readdir(SESSIONS).catch(() => [])) {
    if (!f.endsWith('.json')) continue
    let s
    try {
      s = JSON.parse(await fs.readFile(path.join(SESSIONS, f), 'utf8'))
    } catch {
      continue
    }
    if (s.state === 'ended' || now - (s.updatedAt ?? 0) > STALE_MS) continue
    const a = app.get(s.id)
    if (a?.isArchived) continue
    out.push({
      id: s.id,
      chat: clean(a?.title || s.title || 'Neue Session', 150),
      project: clean(s.project, 60),
      state: s.state in RANK ? s.state : 'idle',
      age: Math.max(0, Math.round((now - (s.since ?? now)) / 1000)),
      detail: clean(detailOf(s), 250),
      agents: Array.isArray(s.agents) ? s.agents.length : 0,
    })
  }
  out.sort((x, y) => RANK[x.state] - RANK[y.state] || y.age - x.age)
  return out.slice(0, MAX_SESSIONS)
}

const openChat = async id => {
  const a = (await appSessions()).get(id)
  if (!a || !/^local_[A-Za-z0-9-]{1,100}$/.test(a.localId)) {
    log('Kein Chat in der App für', id)
    return
  }
  const child = spawn('cmd.exe', ['/c', 'start', '""', `claude://claude.ai/epitaxy/${a.localId}`], {
    windowsHide: true,
    windowsVerbatimArguments: true,
    detached: true,
    stdio: 'ignore',
  })
  child.on('error', e => log('Öffnen fehlgeschlagen:', e.message))
  child.unref()
  log('Chat geöffnet:', a.title || id)
}

// --- Serielle Verbindung ----------------------------------------------------
let port = null
let timer = null

const findPort = async () => {
  if (process.env.GADGET_PORT) return process.env.GADGET_PORT
  const ports = await SerialPort.list()
  return ports.find(p => (p.vendorId ?? '').toLowerCase() === ESPRESSIF_VID)?.path ?? null
}

const send = async () => {
  if (!port?.isOpen) return
  const d = new Date()
  const msg = {
    t: 'state',
    clock: d.toLocaleTimeString('de-CH', { hour: '2-digit', minute: '2-digit' }),
    date: clean(d.toLocaleDateString('de-CH', { weekday: 'short', day: '2-digit', month: '2-digit' }), 18),
    sessions: await readSessions(),
  }
  port.write(`${JSON.stringify(msg)}\n`)
}

const onLine = line => {
  let m
  try {
    m = JSON.parse(line)
  } catch {
    return // Bootmeldungen des ESP32 o. ä.
  }
  if (m.t === 'hello') log('Gadget meldet sich, Firmware', m.fw)
  else if (m.t === 'open' && typeof m.id === 'string') openChat(m.id)
  else if (m.t === 'log') log('Gadget:', m.msg)
}

const connect = async () => {
  const p = await findPort().catch(() => null)
  if (!p) return setTimeout(connect, RECONNECT_MS)
  const sp = new SerialPort({ path: p, baudRate: 115200, autoOpen: false, hupcl: false })
  sp.open(err => {
    if (err) {
      setTimeout(connect, RECONNECT_MS)
      return
    }
    // DTR an, RTS aus: kein Reset des ESP32-S3 beim Verbinden
    sp.set({ dtr: true, rts: false }, () => {})
    port = sp
    log('Verbunden mit', p)
    sp.pipe(new ReadlineParser({ delimiter: '\n' })).on('data', l => onLine(l.trim()))
    send()
    timer = setInterval(() => send().catch(e => log('Fehler:', e.message)), SEND_EVERY_MS)
  })
  sp.on('close', () => {
    if (port === sp) {
      log('Verbindung getrennt')
      port = null
      clearInterval(timer)
      setTimeout(connect, RECONNECT_MS)
    }
  })
  sp.on('error', e => log('Port-Fehler:', e.message))
}

log('Claude Touch Gadget Bridge - suche Gadget ...')
connect()
