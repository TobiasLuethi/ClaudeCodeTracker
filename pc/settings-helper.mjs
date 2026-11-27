// Trägt den Mod-Ordner in ~/.claude/settings.json ein (env.CLAUDE_CODE_PLUGIN_DIRS) oder entfernt ihn.
// Andere Einträge bleiben unverändert; vor jeder Änderung wird eine Sicherung angelegt.
//   node settings-helper.mjs add    <settings.json> <mod-ordner>
//   node settings-helper.mjs remove <settings.json> <mod-ordner>
import fs from 'node:fs'
import path from 'node:path'

const [, , action, file, dir] = process.argv
if (!['add', 'remove'].includes(action) || !file || !dir) {
  console.error('Aufruf: node settings-helper.mjs add|remove <settings.json> <mod-ordner>')
  process.exit(2)
}

const KEY = 'CLAUDE_CODE_PLUGIN_DIRS'
const SEP = path.delimiter // ";" unter Windows
const norm = p => path.resolve(p).toLowerCase()

let settings = {}
if (fs.existsSync(file)) {
  let text = fs.readFileSync(file, 'utf8')
  if (text.charCodeAt(0) === 0xfeff) text = text.slice(1) // BOM, wie ihn Windows-Programme schreiben
  try {
    settings = text.trim() ? JSON.parse(text) : {}
  } catch (e) {
    console.error(`settings.json ist kein gültiges JSON, nichts geändert: ${e.message}`)
    process.exit(1)
  }
  fs.copyFileSync(file, `${file}.bak-touch-gadget`)
}

settings.env ??= {}
const dirs = String(settings.env[KEY] ?? '').split(SEP).map(s => s.trim()).filter(Boolean)
const others = dirs.filter(d => norm(d) !== norm(dir))

if (action === 'add') {
  settings.env[KEY] = [...others, path.resolve(dir)].join(SEP)
  console.log(others.length ? `Mod ergänzt (weitere Mods bleiben: ${others.length})` : 'Mod eingetragen')
} else {
  if (others.length) settings.env[KEY] = others.join(SEP)
  else delete settings.env[KEY]
  if (Object.keys(settings.env).length === 0) delete settings.env
  console.log('Mod-Eintrag entfernt')
}

fs.mkdirSync(path.dirname(file), { recursive: true })
fs.writeFileSync(file, `${JSON.stringify(settings, null, 2)}\n`)
