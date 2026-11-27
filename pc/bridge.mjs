// Brücke zum Claude Touch Gadget: liest die Status-Dateien, die der Mod "touch-dashboard" pro
// Session schreibt (~/.claude/touch-dashboard/sessions/<id>.json), ergänzt Chatname und Link aus
// der Claude-App und schickt den Zustand jede Sekunde an das Gadget, per USB-Seriell oder WLAN.
// Tippt man dort auf eine Kachel, öffnet die Brücke den Chat in der Claude-App.
//
// WLAN ist erst aktiv, nachdem ein Gadget per USB gekoppelt wurde (Einstellungen am Gadget).
// Dann lauscht die Brücke auf UDP 47390 (Suche) und TCP 47391 (Verbindung); jedes Gadget meldet
// sich mit HMAC-SHA256 über einen Zufallswert an, der Schlüssel wird nur über USB übertragen.
// Start: node bridge.mjs   (Port automatisch; sonst GADGET_PORT=COM5 setzen)
import fs from 'node:fs/promises'
import path from 'node:path'
import os from 'node:os'
import crypto from 'node:crypto'
import dgram from 'node:dgram'
import net from 'node:net'
import { spawn } from 'node:child_process'
import { SerialPort } from 'serialport'
import { ReadlineParser } from '@serialport/parser-readline'

const ESPRESSIF_VID = '303a'
const BASE = path.join(os.homedir(), '.claude', 'touch-dashboard')
const SESSIONS = path.join(BASE, 'sessions')
const PAIRING_FILE = path.join(BASE, 'gadgets.json')
const DISCOVERY_PORT = 47390
const TCP_PORT = 47391
const MAX_LINE = 64 * 1024
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

// --- Gekoppelte Gadgets -----------------------------------------------------
// { "<gid>": { "token": "<hex>", "pairedAt": "<iso>" } }, nur für den eigenen Benutzer lesbar
let gadgets = {}

const loadGadgets = async () => {
  try {
    gadgets = JSON.parse(await fs.readFile(PAIRING_FILE, 'utf8'))
  } catch {
    gadgets = {}
  }
}

const saveGadgets = async () => {
  await fs.mkdir(BASE, { recursive: true })
  await fs.writeFile(PAIRING_FILE, JSON.stringify(gadgets, null, 2), { mode: 0o600 })
}

const pair = async gid => {
  if (typeof gid !== 'string' || !/^[0-9a-f]{4,16}$/.test(gid)) return null
  const token = crypto.randomBytes(32).toString('hex')
  gadgets[gid] = { token, pairedAt: new Date().toISOString() }
  await saveGadgets()
  startNetwork()
  log('Gadget gekoppelt:', gid)
  return token
}

// --- Verbindungen zu Gadgets (USB und WLAN) --------------------------------
// Jede Verbindung: { kind, name, write(line) }
const links = new Set()

const stateMessage = async () =>
  `${JSON.stringify({ t: 'state', ts: Math.floor(Date.now() / 1000), sessions: await readSessions() })}\n`

const broadcast = async () => {
  if (links.size === 0) return
  const line = await stateMessage()
  for (const l of links) l.write(line)
}

setInterval(() => broadcast().catch(e => log('Fehler:', e.message)), SEND_EVERY_MS)

const handleMessage = async (link, m) => {
  if (m.t === 'open' && typeof m.id === 'string') openChat(m.id)
  else if (m.t === 'log') log(`Gadget (${link.name}):`, m.msg)
  else if (m.t === 'hello' && link.kind === 'usb') log('Gadget meldet sich, Firmware', m.fw, 'ID', m.gid)
  else if (m.t === 'pair' && link.kind === 'usb') {
    // Koppeln nur über USB: physischer Zugriff auf das Gerät
    const token = await pair(m.gid)
    if (token) link.write(`${JSON.stringify({ t: 'paired', token, pc: os.hostname() })}\n`)
  }
}

const parseLine = line => {
  try {
    return JSON.parse(line)
  } catch {
    return null // Bootmeldungen des ESP32 o. ä.
  }
}

// --- USB ---------------------------------------------------------------------
let usb = null

const findPort = async () => {
  if (process.env.GADGET_PORT) return process.env.GADGET_PORT
  const ports = await SerialPort.list()
  return ports.find(p => (p.vendorId ?? '').toLowerCase() === ESPRESSIF_VID)?.path ?? null
}

const connectUsb = async () => {
  const p = await findPort().catch(() => null)
  if (!p) return setTimeout(connectUsb, RECONNECT_MS)
  const sp = new SerialPort({ path: p, baudRate: 115200, autoOpen: false, hupcl: false })
  const link = { kind: 'usb', name: p, write: line => sp.isOpen && sp.write(line) }
  sp.open(err => {
    if (err) {
      setTimeout(connectUsb, RECONNECT_MS)
      return
    }
    // DTR an, RTS aus: kein Reset des ESP32-S3 beim Verbinden
    sp.set({ dtr: true, rts: false }, () => {})
    usb = sp
    links.add(link)
    log('USB verbunden:', p)
    sp.pipe(new ReadlineParser({ delimiter: '\n' })).on('data', l => {
      const m = parseLine(l.trim())
      if (m) handleMessage(link, m).catch(e => log('Fehler:', e.message))
    })
    broadcast()
  })
  sp.on('close', () => {
    links.delete(link)
    if (usb === sp) {
      log('USB getrennt')
      usb = null
      setTimeout(connectUsb, RECONNECT_MS)
    }
  })
  sp.on('error', e => log('USB-Fehler:', e.message))
}

// --- WLAN --------------------------------------------------------------------
let networkStarted = false

const verify = (gid, nonce, mac) => {
  const g = gadgets[gid]
  if (!g || typeof mac !== 'string' || !/^[0-9a-f]{64}$/.test(mac)) return false
  const expected = crypto.createHmac('sha256', g.token).update(nonce).digest()
  return crypto.timingSafeEqual(expected, Buffer.from(mac, 'hex'))
}

const startNetwork = () => {
  if (networkStarted || Object.keys(gadgets).length === 0) return
  networkStarted = true

  // Suche: Gadget schickt Broadcast {"t":"discover","gid"}; nur gekoppelte bekommen eine Antwort
  const udp = dgram.createSocket({ type: 'udp4', reuseAddr: true })
  udp.on('message', (msg, rinfo) => {
    const m = parseLine(msg.toString('utf8'))
    if (m?.t !== 'discover' || !gadgets[m.gid]) return
    const reply = Buffer.from(JSON.stringify({ t: 'here', port: TCP_PORT, pc: os.hostname() }))
    udp.send(reply, rinfo.port, rinfo.address)
  })
  udp.on('error', e => log('UDP-Fehler:', e.message))
  udp.bind(DISCOVERY_PORT, () => log(`WLAN aktiv: Suche auf UDP ${DISCOVERY_PORT}, Verbindung auf TCP ${TCP_PORT}`))

  const server = net.createServer(sock => {
    const nonce = crypto.randomBytes(16).toString('hex')
    const name = `${sock.remoteAddress}`
    let authed = false
    let buf = ''
    const link = { kind: 'wlan', name, write: line => !sock.destroyed && sock.write(line) }
    const authTimer = setTimeout(() => !authed && sock.destroy(), 5000)
    sock.setNoDelay(true)
    sock.setKeepAlive(true, 10_000)
    sock.write(`${JSON.stringify({ t: 'challenge', nonce })}\n`)
    sock.on('data', chunk => {
      buf += chunk.toString('utf8')
      if (buf.length > MAX_LINE) return sock.destroy()
      let i
      while ((i = buf.indexOf('\n')) >= 0) {
        const m = parseLine(buf.slice(0, i).trim())
        buf = buf.slice(i + 1)
        if (!m) continue
        if (!authed) {
          if (m.t === 'auth' && verify(m.gid, nonce, m.mac)) {
            authed = true
            clearTimeout(authTimer)
            link.name = `${m.gid} @ ${name}`
            links.add(link)
            log('WLAN verbunden:', link.name)
            broadcast()
          } else {
            log('WLAN-Anmeldung abgelehnt:', name)
            return sock.destroy()
          }
        } else {
          handleMessage(link, m).catch(e => log('Fehler:', e.message))
        }
      }
    })
    const drop = () => {
      clearTimeout(authTimer)
      if (links.delete(link)) log('WLAN getrennt:', link.name)
    }
    sock.on('close', drop)
    sock.on('error', drop)
  })
  server.on('error', e => log('TCP-Fehler:', e.message))
  server.listen(TCP_PORT, '0.0.0.0')
}

// --- Start -------------------------------------------------------------------
log('Claude Touch Gadget Bridge - suche Gadget ...')
await loadGadgets()
startNetwork()
connectUsb()
