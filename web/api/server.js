const express = require('express')
const path = require('path')
const fs = require('fs')
const cors = require('cors')

const app = express()
app.use(cors())
app.use(express.json())

const net = require('net')
const CLI_SOCKET = process.env.CLI_SOCKET || '/tmp/vepc.sock'
const db = require('./db')

// simple token auth and whitelist
const API_TOKEN = process.env.API_TOKEN || ''
const CLI_WHITELIST = [
  /^show /i,
  /^show running-config$/i,
  /^set imsi-group\./i,
  /^show imsi-group/i,
  /^show subscriber-config/i
]

function isCliAllowed(cmd) {
  for (const re of CLI_WHITELIST) {
    if (re.test(cmd.trim())) return true;
  }
  return false;
}

function requireAuth(req, res, next) {
  if (!API_TOKEN) return next(); // no token configured => allow (dev)
  const token = req.headers['x-api-token'] || req.query.token;
  if (!token || String(token) !== API_TOKEN) {
    return res.status(401).json({ ok: false, reason: 'unauthorized' });
  }
  next();
}

function execCliCommand(cmd, timeoutMs = 15000) {
  const retryDelay = 200; // ms between retries when socket missing
  return new Promise((resolve, reject) => {
    const start = Date.now();
    let finished = false;

    function attempt() {
      if (finished) return;
      const elapsed = Date.now() - start;
      if (elapsed >= timeoutMs) {
        finished = true;
        return reject(new Error('cli timeout'));
      }

      const client = net.createConnection({ path: CLI_SOCKET });
      let respBuf = '';
      const remaining = Math.max(1000, timeoutMs - (Date.now() - start));
      const to = setTimeout(() => {
        if (finished) return;
        finished = true;
        try { client.end(); } catch (e) {}
        reject(new Error('cli timeout'));
      }, remaining);

      client.on('connect', () => {
        try { client.write(cmd); } catch (e) {}
      });

      client.on('data', (chunk) => { respBuf += chunk.toString(); });

      client.on('end', () => {
        if (finished) return;
        finished = true;
        clearTimeout(to);
        resolve(respBuf);
      });

      client.on('error', (err) => {
        clearTimeout(to);
        if (finished) return;
        try { client.end(); } catch (e) {}
        if (err && (err.code === 'ENOENT' || err.code === 'ECONNREFUSED' || err.code === 'ECONNRESET')) {
          // transient, retry after short delay
          setTimeout(attempt, retryDelay);
          return;
        }
        finished = true;
        reject(err);
      });
    }

    attempt();
  });
}

// POST /api/cli { cmd: "show running-config" } - guarded by whitelist
app.post('/api/cli', requireAuth, async (req, res) => {
  const cmd = req.body && req.body.cmd ? String(req.body.cmd) : '';
  if (!cmd) return res.status(400).json({ ok: false, reason: 'missing cmd' });
  if (!isCliAllowed(cmd)) return res.status(403).json({ ok: false, reason: 'cmd not allowed' });
  try {
    const out = await execCliCommand(cmd + '\n');
    res.json({ ok: true, out });
  } catch (e) {
    res.status(500).json({ ok: false, reason: e.message });
  }
});

app.get('/api/show-config', requireAuth, async (req, res) => {
  try {
    const cfgPath = '/etc/vepc/vepc.config'
    if (fs.existsSync(cfgPath)) {
      const data = fs.readFileSync(cfgPath, 'utf8')
      return res.json({ ok: true, out: data, config: data })
    }
    // fallback to CLI if config file isn't mounted
    try {
      const out = await execCliCommand('show running-config\n');
      return res.json({ ok: true, out });
    } catch (e) {
      return res.status(500).json({ ok: false, reason: 'running-config not available: ' + e.message })
    }
  } catch (e) {
    res.status(500).json({ ok: false, reason: e.message });
  }
});

// Parse imsi groups from running-config dump
function parseImsiGroups(runningConfigText) {
  // Support two running-config formats:
  // 1) Block format:
  //    imsi-group <name>
  //      plmn <plmn>
  //      series <prefix>
  //      #exit
  // 2) Key/value dotted format (table-style):
  //    imsi-group.<name>.<field> | value
  const lines = runningConfigText.split(/\r?\n/);
  const groupsMap = {}

  // First pass: dotted key/value lines
  for (let raw of lines) {
    if (!raw) continue
    // normalize pipes/tables: remove '|' and collapse spaces
    const clean = raw.replace(/\|/g, ' ').trim()
    // match dotted keys like imsi-group.NAME.FIELD value
    const m = clean.match(/^imsi-group\.([^\.\s]+)\.([^\s]+)\s+(.*)$/i)
    if (m) {
      const name = m[1]
      const field = m[2].toLowerCase()
      const val = m[3].trim()
      if (!groupsMap[name]) groupsMap[name] = { name, plmn: '', type: '', rangeStart: '', rangeEnd: '', series: '', apnProfile: '' }
      if (field === 'plmn') groupsMap[name].plmn = val
      else if (field === 'type') groupsMap[name].type = val
      else if (field === 'range-start' || field === 'rangestart') groupsMap[name].rangeStart = val
      else if (field === 'range-end' || field === 'rangeend') groupsMap[name].rangeEnd = val
      else if (field === 'series') groupsMap[name].series = val
      else if (field === 'apn-profile' || field === 'apnprofile') groupsMap[name].apnProfile = val
      else if (field === 'count') groupsMap[name].count = val
    }
  }

  // Second pass: block-style entries (legacy)
  let i = 0
  while (i < lines.length) {
    const line = lines[i].trimRight()
    const m = line.match(/^imsi-group\s+(\S+)$/i)
    if (m) {
      const name = m[1]
      const group = groupsMap[name] || { name, plmn: '', type: '', rangeStart: '', rangeEnd: '', series: '', apnProfile: '' }
      i++
      while (i < lines.length) {
        const sub = lines[i].trim()
        if (sub === '#exit') { i++; break }
        const pm = sub.match(/^plmn\s+(\S+)/i)
        if (pm) { group.plmn = pm[1]; i++; continue }
        const rm = sub.match(/^range\s+(\S+)\s+(\S+)/i)
        if (rm) { group.type = 'range'; group.rangeStart = rm[1]; group.rangeEnd = rm[2]; i++; continue }
        const sm = sub.match(/^series\s+(\S+)/i)
        if (sm) { group.type = 'series'; group.series = sm[1]; i++; continue }
        const am = sub.match(/^apn-profile\s+(\S+)/i)
        if (am) { group.apnProfile = am[1]; i++; continue }
        i++
      }
      groupsMap[name] = group
      continue
    }
    i++
  }

  // Convert map to array
  const groups = Object.keys(groupsMap).map(k => groupsMap[k])
  return groups
}

// Parse interface overview table produced by `show iface`
function parseInterfaces(text) {
  const lines = text.split(/\r?\n/)
  const out = []
  for (let i = 0; i < lines.length; i++) {
    let raw = lines[i]
    if (!raw) continue
    const line = raw.replace(/\t/g, ' ')
    // main summary line is expected to have fixed-width columns
    if (line.length < 86) continue
    const name = line.substring(0, 12).trim()
    const proto = line.substring(12, 24).trim()
    const address = line.substring(24, 46).trim()
    const admin = line.substring(46, 58).trim()
    const oper = line.substring(58, 70).trim()
    const implementation = line.substring(70, 86).trim()
    const peer = line.substring(86).trim()

    // skip visual separator or filler lines made of dashes
    if (/^[-\s]+$/.test(name) || /^[-\s]+$/.test(proto) || /^[-\s]+$/.test(address)) continue

    const iface = { name, proto, address, admin, oper, implementation, peer }

    // collect following diagnostic lines (they often start with 'diag:' or are indented)
    let diagLines = []
    let j = i + 1
    while (j < lines.length) {
      const nxt = lines[j]
      if (!nxt) { j++; continue }
      const t = nxt.trim()
      // treat as diagnostic/detail line if it starts with 'diag:' or begins with whitespace (continuation)
      if (/^diag[:\s]/i.test(t) || /^\s+/.test(nxt)) {
        diagLines.push(nxt.trim())
        j++
        continue
      }
      break
    }
    if (diagLines.length) iface.diagnostic = diagLines.join(' ').replace(/\s+/g, ' ')

    if (name) out.push(iface)
    // advance index to skip consumed diagnostic lines
    i = j - 1
  }
  return out
}

// GET /api/imsi - list IMSI groups
app.get('/api/imsi', requireAuth, async (req, res) => {
  try {
    // If DB is available, use it as authoritative source
    try {
      await db.init()
      const pool = await db.getPool()
      const [rows] = await pool.query('SELECT * FROM imsi_groups ORDER BY id ASC')
      return res.json({ ok: true, groups: rows.map(r => ({ name: r.name, kind: r.kind, plmn: r.plmn, series: r.series, rangeStart: r.range_start, rangeEnd: r.range_end, apnProfile: r.apn_profile, count: r.cnt })) })
    } catch (dbe) {
      // DB not available, fall back to previous behavior using CLI/config
    }

    // Try to fetch running-config first (more likely to contain imsi-group entries),
    // then fall back to the generic 'show' or mounted config file.
    let out = null
    try {
      out = await execCliCommand('show running-config\n')
      // treat 'Unknown command' or 'Available:' as not supported and fall back
      if (out && (/Unknown command:/i.test(out) || /Available:/i.test(out))) {
        out = null
      }
    } catch (e) {
      out = null
    }

    if (!out) {
      try { out = await execCliCommand('show\n') } catch (e2) { out = null }
    }

    if (out) {
      const parsed = parseImsiGroups(out)
      return res.json({ ok: true, groups: parsed })
    }

    // CLI may be unavailable; fallback to reading mounted config file for persisted groups
    const cfgPath = '/etc/vepc/vepc.config'
    if (fs.existsSync(cfgPath)) {
      const data = fs.readFileSync(cfgPath, 'utf8')
      const parsed = parseImsiGroups(data)
      return res.json({ ok: true, groups: parsed })
    }

    return res.status(500).json({ ok: false, reason: 'running-config not available' })
  } catch (e) {
    res.status(500).json({ ok: false, reason: e.message });
  }
});

// POST /api/imsi - create IMSI group (range or series)
app.post('/api/imsi', requireAuth, async (req, res) => {
  const body = req.body || {};
  const name = String(body.name || '').trim();
  const kind = String(body.kind || '').trim();
  const plmns = String(body.plmns || '').trim();
  if (!name || !kind || !plmns) return res.status(400).json({ ok: false, reason: 'missing fields (name, kind, plmns required)' });
  if (!/^[A-Za-z0-9_-]+$/.test(name)) return res.status(400).json({ ok: false, reason: 'invalid name' });
  // validate plmns as comma-separated words
  const plmnList = plmns.split(',').map(s => s.trim()).filter(s => s.length > 0);
  if (plmnList.length === 0) return res.status(400).json({ ok: false, reason: 'invalid plmns' });
  // Insert into DB (one row per PLMN)
  try {
    await db.init()
    const pool = await db.getPool()
    const inserted = []
    for (const p of plmnList) {
      const params = {
        name,
        kind,
        plmn: p,
        series: null,
        range_start: null,
        range_end: null,
        apn_profile: body.apnProfile || null,
        cnt: body.count ? parseInt(body.count, 10) : null
      }
      if (kind === 'range') {
        const start = String(body.start || '').trim();
        const end = String(body.end || '').trim();
        if (!/^\d+$/.test(start) || !/^\d+$/.test(end)) return res.status(400).json({ ok: false, reason: 'invalid range boundaries' });
        params.range_start = start
        params.range_end = end
      } else if (kind === 'series') {
        const series = String(body.series || '').trim();
        if (!/^[0-9]+$/.test(series)) return res.status(400).json({ ok: false, reason: 'invalid series prefix' });
        params.series = series
      } else {
        return res.status(400).json({ ok: false, reason: 'unknown kind (range|series)' });
      }

      // Upsert row
      const sql = `INSERT INTO imsi_groups (name, kind, plmn, series, range_start, range_end, apn_profile, cnt) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        ON DUPLICATE KEY UPDATE kind=VALUES(kind), series=VALUES(series), range_start=VALUES(range_start), range_end=VALUES(range_end), apn_profile=VALUES(apn_profile), cnt=VALUES(cnt)`
      await pool.query(sql, [params.name, params.kind, params.plmn, params.series, params.range_start, params.range_end, params.apn_profile, params.cnt])
      inserted.push({ ...params })
    }

    // best-effort: send CLI commands to core to keep runtime in sync
    try {
      const cmds = []
      for (const item of inserted) {
        if (item.kind === 'range') cmds.push(`imsi range ${item.name} ${item.plmn} ${item.range_start} ${item.range_end} ${item.apn_profile ? item.apn_profile : ''}`)
        else cmds.push(`imsi series ${item.name} ${item.plmn} ${item.series} ${item.apn_profile ? item.apn_profile : ''}`)
        if (item.cnt) cmds.push(`set imsi-group.${item.name}.count ${item.cnt}`)
      }
      if (cmds.length) {
        try {
          const out = await execCliCommand(cmds.join('\n') + '\n')
          // if core replies Unknown/Available, fall back to individual 'set' commands
          if (out && (/Unknown command:/i.test(out) || /Available:/i.test(out))) {
            const setCmds = []
            for (const item of inserted) {
              setCmds.push(`set imsi-group.${item.name}.plmn ${item.plmn}`)
              if (item.kind === 'series') setCmds.push(`set imsi-group.${item.name}.series ${item.series}`)
              if (item.kind === 'range') {
                setCmds.push(`set imsi-group.${item.name}.range-start ${item.range_start}`)
                setCmds.push(`set imsi-group.${item.name}.range-end ${item.range_end}`)
              }
              if (item.apn_profile) setCmds.push(`set imsi-group.${item.name}.apn-profile ${item.apn_profile}`)
              if (item.cnt) setCmds.push(`set imsi-group.${item.name}.count ${item.cnt}`)
            }
            try { await execCliCommand(setCmds.join('\n') + '\n') } catch (e) { /* ignore */ }
          }
        } catch (e) { /* ignore CLI errors */ }
      }
    } catch (e) {}

    const [rows] = await pool.query('SELECT * FROM imsi_groups ORDER BY id ASC')
    return res.json({ ok: true, groups: rows.map(r => ({ name: r.name, kind: r.kind, plmn: r.plmn, series: r.series, rangeStart: r.range_start, rangeEnd: r.range_end, apnProfile: r.apn_profile, count: r.cnt })) })
  } catch (e) {
    res.status(500).json({ ok: false, reason: e.message });
  }
});

// DELETE /api/imsi/:name - clear imsi group fields (best-effort)
app.delete('/api/imsi/:name', requireAuth, async (req, res) => {
  const name = String(req.params.name || '').trim();
  if (!name) return res.status(400).json({ ok: false, reason: 'missing name' });
  const fields = ['type','plmn','series','range-start','range-end','apn-profile','count'];
  // create commands to clear each field (set to empty)
  try {
    await db.init()
    const pool = await db.getPool()
    // delete all rows with given name
    await pool.query('DELETE FROM imsi_groups WHERE name = ?', [name])

    // best-effort: clear in core runtime
    try {
      const cmds = fields.map(f => `set imsi-group.${name}.${f} `)
      try { await execCliCommand(cmds.join('\n') + '\n') } catch (e) { /* ignore */ }
    } catch (e) {}

    const [rows] = await pool.query('SELECT * FROM imsi_groups ORDER BY id ASC')
    return res.json({ ok: true, groups: rows.map(r => ({ name: r.name, kind: r.kind, plmn: r.plmn, series: r.series, rangeStart: r.range_start, rangeEnd: r.range_end, apnProfile: r.apn_profile, count: r.cnt })) })
  } catch (e) {
    res.status(500).json({ ok: false, reason: e.message });
  }
});

// PUT /api/imsi/:name - update IMSI group fields (partial update)
app.put('/api/imsi/:name', requireAuth, async (req, res) => {
  const name = String(req.params.name || '').trim();
  if (!name) return res.status(400).json({ ok: false, reason: 'missing name' });
  const body = req.body || {};
  try {
    await db.init()
    const pool = await db.getPool()
    // We treat updates as possibly affecting multiple rows if plmns provided.
    const updates = []
    if (body.plmns) {
      // replace plmns: delete existing rows for name and insert new ones
      const plmnList = String(body.plmns).split(',').map(s => s.trim()).filter(Boolean)
      if (plmnList.length === 0) return res.status(400).json({ ok: false, reason: 'invalid plmns' });
      await pool.query('DELETE FROM imsi_groups WHERE name = ?', [name])
      for (const p of plmnList) {
        updates.push({ name, plmn: p })
      }
    }

    // For simple field updates, update existing rows
    const fields = {}
    if (body.kind) fields.kind = String(body.kind)
    if (body.apnProfile) fields.apn_profile = String(body.apnProfile)
    if (body.start) fields.range_start = String(body.start)
    if (body.end) fields.range_end = String(body.end)
    if (body.series) fields.series = String(body.series)
    if (body.count) fields.cnt = parseInt(body.count, 10)

    if (updates.length > 0) {
      for (const u of updates) {
        const sql = `INSERT INTO imsi_groups (name, kind, plmn, series, range_start, range_end, apn_profile, cnt) VALUES (?, ?, ?, ?, ?, ?, ?, ?) ON DUPLICATE KEY UPDATE kind=VALUES(kind), series=VALUES(series), range_start=VALUES(range_start), range_end=VALUES(range_end), apn_profile=VALUES(apn_profile), cnt=VALUES(cnt)`
        await pool.query(sql, [u.name, body.kind || 'series', u.plmn, body.series || null, body.start || null, body.end || null, body.apnProfile || null, body.count ? parseInt(body.count, 10) : null])
      }
    }

    if (Object.keys(fields).length > 0) {
      const setParts = []
      const params = []
      for (const k of Object.keys(fields)) { setParts.push(`${k} = ?`); params.push(fields[k]) }
      params.push(name)
      await pool.query(`UPDATE imsi_groups SET ${setParts.join(', ')} WHERE name = ?`, params)
    }

    // best-effort: reflect changes to core runtime
    try {
      const [rows] = await pool.query('SELECT * FROM imsi_groups WHERE name = ? ORDER BY id ASC', [name])
      const cmds = []
      for (const r of rows) {
        if (r.kind === 'range') cmds.push(`imsi range ${r.name} ${r.plmn} ${r.range_start} ${r.range_end} ${r.apn_profile ? r.apn_profile : ''}`)
        else cmds.push(`imsi series ${r.name} ${r.plmn} ${r.series} ${r.apn_profile ? r.apn_profile : ''}`)
        if (r.cnt) cmds.push(`set imsi-group.${r.name}.count ${r.cnt}`)
      }
      if (cmds.length) { try { const out = await execCliCommand(cmds.join('\n') + '\n'); if (out && (/Unknown command:/i.test(out) || /Available:/i.test(out))) { const setCmds = []; for (const r of rows) { if (r.kind === 'range') setCmds.push(`set imsi-group.${r.name}.plmn ${r.plmn}`); else setCmds.push(`set imsi-group.${r.name}.plmn ${r.plmn}`); if (r.kind === 'range') { setCmds.push(`set imsi-group.${r.name}.range-start ${r.range_start}`); setCmds.push(`set imsi-group.${r.name}.range-end ${r.range_end}`); } else { setCmds.push(`set imsi-group.${r.name}.series ${r.series}`); } if (r.apn_profile) setCmds.push(`set imsi-group.${r.name}.apn-profile ${r.apn_profile}`); if (r.cnt) setCmds.push(`set imsi-group.${r.name}.count ${r.cnt}`); } try { await execCliCommand(setCmds.join('\n') + '\n') } catch (e) {} } } catch (e) {} }
    } catch (e) {}

    const [rows] = await pool.query('SELECT * FROM imsi_groups ORDER BY id ASC')
    return res.json({ ok: true, groups: rows.map(r => ({ name: r.name, kind: r.kind, plmn: r.plmn, series: r.series, rangeStart: r.range_start, rangeEnd: r.range_end, apnProfile: r.apn_profile, count: r.cnt })) })
  } catch (e) {
    res.status(500).json({ ok: false, reason: e.message });
  }
});


const DIST = path.join(__dirname, '..', 'dist')

app.use('/api/ping', (req, res) => {
  res.json({ok: true, time: new Date().toISOString()})
})

// GET /api/interfaces - parse `show iface` output into structured JSON
app.get('/api/interfaces', requireAuth, async (req, res) => {
  try {
    const out = await execCliCommand('show iface\n')
    const ifaces = parseInterfaces(out)
    res.json({ ok: true, interfaces: ifaces })
  } catch (e) {
    res.status(500).json({ ok: false, reason: e.message })
  }
})

// Parse runtime/state output into structured JSON
function parseRuntime(text) {
  const lines = text.split(/\r?\n/)
  const result = { pdpContexts: [], endpointTelemetry: [], ueContexts: [] }
  let i = 0

  function consumeBlock(startRegex) {
    const block = []
    while (i < lines.length) {
      const l = lines[i]
      if (startRegex && startRegex.test(l)) break
      block.push(l)
      i++
    }
    return block
  }

  // simple state machine
  while (i < lines.length) {
    const line = lines[i].trim()
    if (line.startsWith('PDP contexts:')) {
      i++
      // read until next blank or next section
      while (i < lines.length && lines[i].trim().startsWith('- TEID:')) {
        const teidLine = lines[i].trim()
        const m = teidLine.match(/- TEID:\s*0x([0-9A-Fa-f]+)/)
        const entry = {}
        if (m) { entry.teid = parseInt(m[1], 16) }
        i++
        while (i < lines.length && lines[i].startsWith('  ')) {
          const s = lines[i].trim()
          const parts = s.split(':')
          if (parts.length >= 2) {
            const key = parts[0].trim()
            const val = parts.slice(1).join(':').trim()
            entry[key] = val
          }
          i++
        }
        result.pdpContexts.push(entry)
      }
      continue
    }

    if (line.startsWith('Endpoint telemetry:')) {
      i++
      while (i < lines.length && lines[i].trim().startsWith('- Name:')) {
        const entry = {}
        // lines for one endpoint
        while (i < lines.length && (lines[i].startsWith('- ') || lines[i].startsWith('  '))) {
          const s = lines[i].trim()
          const parts = s.split(':')
          if (parts.length >= 2) {
            const key = parts[0].replace(/^- /, '').trim()
            const val = parts.slice(1).join(':').trim()
            entry[key] = val
          }
          i++
        }
        result.endpointTelemetry.push(entry)
      }
      continue
    }

    if (line.startsWith('UE contexts:')) {
      i++
      while (i < lines.length && lines[i].trim().startsWith('- IMSI:')) {
        const entry = {}
        // first line
        while (i < lines.length && (lines[i].startsWith('- ') || lines[i].startsWith('  '))) {
          const s = lines[i].trim()
          const parts = s.split(':')
          if (parts.length >= 2) entry[parts[0].replace(/^- /, '').trim()] = parts.slice(1).join(':').trim()
          i++
        }
        result.ueContexts.push(entry)
      }
      continue
    }

    i++
  }

  return result
}

// GET /api/runtime - structured runtime/state
app.get('/api/runtime', requireAuth, async (req, res) => {
  try {
    // Try a list of candidate commands to obtain runtime/state info
    const candidates = ['show state', 'state', 'show', 'status']
    let out = null
    let used = null
    for (const c of candidates) {
      try {
        out = await execCliCommand(c + '\n')
        // if CLI replies with unknown command, continue to next
        if (/Unknown command:/i.test(out) || /Available:/i.test(out)) {
          out = null
          continue
        }
        used = c
        break
      } catch (e) {
        // try next candidate
        out = null
      }
    }

    if (!out) return res.status(500).json({ ok: false, reason: 'no supported runtime command available' })

    const parsed = parseRuntime(out)
    res.json({ ok: true, cmd: used, raw: out, ...parsed })
  } catch (e) {
    res.status(500).json({ ok: false, reason: e.message })
  }
})

app.get('/api/config', (req, res) => {
  // Try to read mounted config if available
  try {
    const cfgPath = '/etc/vepc/vepc.config'
    if (fs.existsSync(cfgPath)) {
      const data = fs.readFileSync(cfgPath, 'utf8')
      return res.json({ok: true, config: data})
    }
  } catch (e) {
    // fallthrough
  }
  res.json({ok: false, reason: 'config not available in container (mount ./config in compose)'});
})

// serve built frontend
if (fs.existsSync(DIST)) {
  app.use(express.static(DIST))
  app.get('/*', (req, res) => res.sendFile(path.join(DIST, 'index.html')))
} else {
  app.get('/', (req, res) => res.send('Frontend not built. Run `npm run build` in /web.'))
}

const port = process.env.PORT || 3000
// initialize DB if possible
async function seedFromConfig() {
  try {
    const cfgPath = '/etc/vepc/vepc.config'
    if (!fs.existsSync(cfgPath)) return
    const data = fs.readFileSync(cfgPath, 'utf8')
    const groups = parseImsiGroups(data)
    if (!groups || groups.length === 0) return
    await db.init()
    const pool = await db.getPool()
    for (const g of groups) {
      // g may represent a single plmn; ensure values are normalized
      const name = g.name
      const kind = g.type && g.type.length ? g.type : (g.series ? 'series' : (g.rangeStart || g.rangeEnd ? 'range' : 'series'))
      const plmn = g.plmn || ''
      const series = g.series || null
      const range_start = g.rangeStart || g.range_start || null
      const range_end = g.rangeEnd || g.range_end || null
      const apn = g.apnProfile || g.apn_profile || null
      const cnt = g.count ? parseInt(g.count, 10) : (g.cnt ? parseInt(g.cnt, 10) : null)

      const sql = `INSERT INTO imsi_groups (name, kind, plmn, series, range_start, range_end, apn_profile, cnt) VALUES (?, ?, ?, ?, ?, ?, ?, ?) ON DUPLICATE KEY UPDATE kind=VALUES(kind), series=VALUES(series), range_start=VALUES(range_start), range_end=VALUES(range_end), apn_profile=VALUES(apn_profile), cnt=VALUES(cnt)`
      await pool.query(sql, [name, kind, plmn, series, range_start, range_end, apn, cnt])
    }
    console.log('Seeded imsi_groups from config:', groups.length)
  } catch (e) {
    console.warn('seedFromConfig failed:', e && e.message)
  }
}

db.init().then(() => {
  console.log('DB initialized')
  // attempt seeding from mounted config (non-fatal)
  seedFromConfig().catch(() => {})
}).catch(() => {/* ignore db init errors here */})

app.listen(port, () => console.log(`vepc-web api listening ${port}`))
