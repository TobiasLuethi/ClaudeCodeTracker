import type { EngineInterface, Register } from 'claude-code'

// Touch-Dashboard (reine Anzeige): jede Session schreibt ihren Status nach
// ~/.claude/touch-dashboard/sessions/<id>.json. Der Dashboard-Server liest die
// Dateien, ergänzt Chatnamen und Link aus der Claude-App und zeigt sie an.
// Beantwortet wird immer in der Claude-App.

type SessionState = 'working' | 'idle' | 'input' | 'approval' | 'ended'

type Call = {
  key: string
  tool: string
  summary: string
  detail: string
  agent: string | null
  since: number
}

type Question = { text: string; options: string[]; since: number }

type Agent = { id: string; description: string; type: string; since: number }

type Status = {
  id: string
  project: string
  cwd: string
  title: string
  state: SessionState
  since: number
  updatedAt: number
  question: Question | null
  agents: Agent[]
  pending: Call[]
}

const HEARTBEAT_MS = 3_000
const APPROVAL_GUESS_MS = 2_000
const META_KEYS = new Set(['tool', 'agentId', 'tool_use_id', 'ref', 'mcp', 'isReadOnly'])

const basename = (p: string) => p.replace(/[\\/]+$/, '').split(/[\\/]/).pop() ?? p
const safe = (s: string) => s.replace(/[^A-Za-z0-9_-]/g, '_')
const clip = (s: string, n: number) => (s.length > n ? `${s.slice(0, n - 1)}…` : s)

const inputOf = (e: object): Record<string, unknown> =>
  Object.fromEntries(Object.entries(e).filter(([k]) => !META_KEYS.has(k)))

const summarize = (tool: string, input: Record<string, unknown>): string => {
  const pick = ['command', 'file_path', 'url', 'path', 'pattern', 'description', 'prompt']
    .map(k => input[k])
    .find(v => typeof v === 'string') as string | undefined
  return clip(pick ?? JSON.stringify(input), 300) || tool
}

let s: Status | null = null
let base = ''
let heartbeat = false
let chain: Promise<unknown> = Promise.resolve()
// laufende Tool-Calls je Loop ("main" oder Agent-ID)
const current = new Map<string, Call>()
// Loops, deren aktueller Call schon läuft (tool.run gehört)
const started = new Set<string>()
let sawToolRun = false

async function ensure($: EngineInterface): Promise<Status> {
  if (s) return s
  const home = (await $.env.get('USERPROFILE')) ?? (await $.env.get('HOME')) ?? ''
  base = `${home}/.claude/touch-dashboard`
  const id = await $.session.id()
  const cwd = await $.session.cwd()
  const now = await $.clock.now()
  let restored: Partial<Status> = {}
  try {
    restored = JSON.parse(await $.fs.read(`${base}/sessions/${safe(id)}.json`))
  } catch {
    // neue Session
  }
  s = {
    id,
    cwd,
    project: basename(cwd),
    title: restored.title?.startsWith('<') ? '' : (restored.title ?? ''),
    state: 'idle',
    since: now,
    updatedAt: now,
    question: null,
    agents: [],
    pending: [],
  }
  return s
}

async function save($: EngineInterface) {
  const st = await ensure($)
  st.updatedAt = await $.clock.now()
  const text = JSON.stringify(st)
  chain = chain
    .then(() => $.fs.write(`${base}/sessions/${safe(st.id)}.json`, text))
    .catch(() => undefined)
  return chain
}

async function setState($: EngineInterface, state: SessionState) {
  const st = await ensure($)
  if (state !== 'ended') {
    if (st.pending.length > 0) state = 'approval'
    else if (st.question) state = 'input'
  }
  if (st.state !== state) st.since = await $.clock.now()
  st.state = state
  await save($)
}

// Laufende Subagents direkt aus der Engine: auch solche, die auf eigene
// Hintergrundarbeit warten, gelten als laufend.
async function refreshAgents($: EngineInterface) {
  const st = await ensure($)
  try {
    const list = await $.agent.list()
    st.agents = list
      .filter(a => a.taskStatus === 'running' || a.taskStatus === 'pending' || a.taskStatus === 'paused')
      .map(a => ({
        id: a.id,
        description: clip(a.name ?? a.description ?? a.label ?? a.type, 80),
        type: a.type,
        since: a.startTime,
      }))
  } catch {
    // Liste nicht verfügbar: letzten Stand behalten
  }
  await save($)
}

async function agentName($: EngineInterface, agentId: string | undefined) {
  if (!agentId) return null
  const st = await ensure($)
  return st.agents.find(a => a.id === agentId)?.description ?? 'Subagent'
}

export const register: Register = on => {
  on('session.start', async ($, e, next) => {
    const started = await next(e)
    await ensure($)
    await setState($, 'idle')
    if (!heartbeat) {
      heartbeat = true
      $.clock.every(HEARTBEAT_MS, () => refreshAgents($))
    }
    return started
  })

  on('prompt.submit', async ($, e, next) => {
    const st = await ensure($)
    const typed = e.origin.kind === 'composer' && !e.isMeta && !e.text.trimStart().startsWith('<')
    if (!st.title && typed && e.text.trim()) st.title = clip(e.text.trim().replace(/\s+/g, ' '), 90)
    return next(e)
  })

  on('turn.start', async ($, e, next) => {
    if (!e.agentId) await setState($, 'working')
    return next(e)
  })

  on('turn.complete', async ($, e, next) => {
    const done = await next(e)
    if (!e.agentId) {
      // Nur Einträge der Hauptsession aufräumen: Subagents im Hintergrund
      // können nach dem Ende des Haupt-Turns weiter auf eine Freigabe warten.
      const st = await ensure($)
      st.question = null
      st.pending = st.pending.filter(p => p.key !== 'main')
      current.delete('main')
      await setState($, 'idle')
    }
    return done
  })

  on('agent.spawn', async ($, e, next) => {
    const spawned = await next(e)
    await refreshAgents($)
    return spawned
  })

  // Jeder Tool-Call wird gemerkt, damit eine Freigabe-Meldung zeigen kann,
  // was genau freigegeben werden muss. Fragen werden mit Antworten angezeigt.
  on('tool.call', async ($, e, next) => {
    const st = await ensure($)
    const key = e.agentId ?? 'main'
    const input = inputOf(e)
    const tool = String(e.tool)
    const now = await $.clock.now()

    if (tool === 'AskUserQuestion') {
      const qs = (input.questions ?? []) as { question?: string; options?: { label?: string }[] }[]
      st.question = {
        text: clip(qs.map(q => q.question ?? '').filter(Boolean).join(' · ') || 'Frage offen', 300),
        options: (qs[0]?.options ?? []).map(o => clip(o.label ?? '', 40)).filter(Boolean),
        since: now,
      }
      await setState($, 'input')
    } else if (tool === 'ExitPlanMode') {
      st.question = { text: 'Plan zur Freigabe', options: ['Plan annehmen', 'Weiter planen'], since: now }
      await setState($, 'input')
    } else {
      const call: Call = {
        key,
        tool,
        summary: summarize(tool, input),
        detail: clip(JSON.stringify(input, null, 2), 1500),
        agent: await agentName($, e.agentId),
        since: now,
      }
      current.set(key, call)
      started.delete(key)
      $.clock.after(APPROVAL_GUESS_MS, async () => {
        if (!sawToolRun || started.has(key) || current.get(key) !== call) return
        st.pending = [...st.pending.filter(p => p.key !== key), call]
        await setState($, 'approval')
      })
    }

    try {
      return await next(e)
    } finally {
      if (current.get(key)?.since === now) current.delete(key)
      if (st.question?.since === now) st.question = null
      const before = st.pending.length
      st.pending = st.pending.filter(p => p.key !== key)
      if (before !== st.pending.length || tool === 'AskUserQuestion' || tool === 'ExitPlanMode') {
        await setState($, 'working')
      }
    }
  })

  // Freigabe erkennen: Freigabe-Ereignisse erreichen eigene Mods nicht
  // (cc-plugin-sec-default). tool.run feuert, sobald ein Call erlaubt ist und
  // startet. Hängt ein Call länger zwischen tool.call und tool.run, wartet er
  // auf eine Freigabe. Erst aktiv, wenn tool.run in dieser Ladung gehört wurde.
  on('tool.run', async ($, e, next) => {
    sawToolRun = true
    const key = e.agentId ?? 'main'
    started.add(key)
    const st = await ensure($)
    if (st.pending.some(p => p.key === key)) {
      st.pending = st.pending.filter(p => p.key !== key)
      await setState($, 'working')
    }
    return next(e)
  })

  on('session.end', async ($, e, next) => {
    await setState($, 'ended')
    await chain
    return next(e)
  })
}
