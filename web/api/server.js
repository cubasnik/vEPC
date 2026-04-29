const express = require('express')
const path = require('path')
const fs = require('fs')
const cors = require('cors')

const app = express()
app.use(cors())
app.use(express.json())

const net = require('net')
const CLI_SOCKET = process.env.CLI_SOCKET || '/tmp/vepc.sock'

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

function execCliCommand(cmd, timeoutMs = 5000) {
  const retryDelay = 200; // ms between retries when socket missing
    return new Promise((resolve, reject) => {
      const start = Date.now();
      let finished = false;
      let attempt = 0;
      const minDelay = 100; // ms
      const maxDelay = 2000; // ms
      const timeoutMs = 15000; // Increase overall timeout to 15s

      function tryOnce() {
        if (finished) return;
        const elapsed = Date.now() - start;
        if (elapsed >= timeoutMs) {
          finished = true;
          return reject(new Error('cli timeout'));
        }

        attempt += 1;
        const client = net.createConnection({ path: CLI_SOCKET });
        let respBuf = '';
        let to = setTimeout(() => {
          if (finished) return;
          finished = true;
          try { client.end(); } catch (e) {}
          reject(new Error('cli timeout'));
        }, Math.max(1000, timeoutMs - elapsed));

        client.on('connect', () => {
          try { client.write(cmd); } catch (e) {}
        });

        client.on('data', (chunk) => { respBuf += chunk.toString(); });

        const finishWith = (err, result) => {
          if (finished) return;
          finished = true;
          clearTimeout(to);
          if (err) return reject(err);
          return resolve(result);
        };

        client.on('end', () => finishWith(null, respBuf));
        client.on('close', () => finishWith(null, respBuf));

        client.on('error', (err) => {
          clearTimeout(to);
          if (finished) return;
          // Retry for common transient socket errors (service not yet started)
          if (err && (err.code === 'ENOENT' || err.code === 'ECONNREFUSED' || err.code === 'ECONNRESET')) {
            try { client.end(); } catch (e) {}
            // exponential backoff
            const delay = Math.min(maxDelay, Math.max(minDelay, Math.pow(2, attempt) * minDelay));
            console.warn(`[web/api] CLI socket not ready (attempt ${attempt}): ${err.code}, retrying in ${delay}ms`);
            setTimeout(() => { tryOnce(); }, delay);
            return;
          }
          finished = true;
          reject(err);
        });
      }

      tryOnce();
    });
}

    function tryOnce() {
      if (finished) return;
      const elapsed = Date.now() - start;
      if (elapsed >= timeoutMs) {
        finished = true;
        return reject(new Error('cli timeout'));
      }

      const client = net.createConnection({ path: CLI_SOCKET });
      let respBuf = '';
      let to = setTimeout(() => {
        if (finished) return;
        finished = true;
        try { client.end(); } catch (e) {}
        reject(new Error('cli timeout'));
      }, Math.max(1000, timeoutMs - elapsed));

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
        // Retry for common transient socket errors (service not yet started)
        if (err && (err.code === 'ENOENT' || err.code === 'ECONNREFUSED' || err.code === 'ECONNRESET')) {
          try { client.end(); } catch (e) {}
          // small delay then retry
          setTimeout(() => { tryOnce(); }, retryDelay);
          return;
        }
        finished = true;
        reject(err);
      });
    }

    tryOnce();
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
    const out = await execCliCommand('show running-config\n');
    res.json({ ok: true, out });
  } catch (e) {
    res.status(500).json({ ok: false, reason: e.message });
  }
});

// Parse imsi groups from running-config dump
function parseImsiGroups(runningConfigText) {
  const lines = runningConfigText.split(/\r?\n/);
  const groups = [];
  let i = 0;
  while (i < lines.length) {
    const line = lines[i].trimRight();
    const m = line.match(/^imsi-group\s+(\S+)$/);
    if (m) {
      const name = m[1];
      const group = { name, plmn: '', type: '', rangeStart: '', rangeEnd: '', series: '', apnProfile: '' };
      i++;
      while (i < lines.length) {
        const sub = lines[i].trim();
        if (sub === '#exit') { i++; break; }
        const pm = sub.match(/^plmn\s+(\S+)/);
        if (pm) { group.plmn = pm[1]; i++; continue; }
        const rm = sub.match(/^range\s+(\S+)\s+(\S+)/);
        if (rm) { group.type = 'range'; group.rangeStart = rm[1]; group.rangeEnd = rm[2]; i++; continue; }
        const sm = sub.match(/^series\s+(\S+)/);
        if (sm) { group.type = 'series'; group.series = sm[1]; i++; continue; }
        const am = sub.match(/^apn-profile\s+(\S+)/);
        if (am) { group.apnProfile = am[1]; i++; continue; }
        // unknown line
        i++;
      }
      groups.push(group);
      continue;
    }
    i++;
  }
  return groups;
}

// Parse interface overview table produced by `show iface`
function parseInterfaces(text) {
  const lines = text.split(/\r?\n/)
  const out = []
  for (const raw of lines) {
    const line = raw.replace(/\t/g, ' ')
    if (line.length < 86) continue
    const name = line.substring(0, 12).trim()
    const proto = line.substring(12, 24).trim()
    const address = line.substring(24, 46).trim()
    const admin = line.substring(46, 58).trim()
    const oper = line.substring(58, 70).trim()
    const implementation = line.substring(70, 86).trim()
    const peer = line.substring(86).trim()
    if (name) out.push({ name, proto, address, admin, oper, implementation, peer })
  }
  return out
}

// GET /api/imsi - list IMSI groups
app.get('/api/imsi', requireAuth, async (req, res) => {
  try {
    const out = await execCliCommand('show running-config\n');
    const parsed = parseImsiGroups(out);
    res.json({ ok: true, groups: parsed });
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

  const cmds = [];
  cmds.push(`set imsi-group.${name}.type ${kind}`);
  cmds.push(`set imsi-group.${name}.plmn ${plmns}`);
  if (kind === 'range') {
    const start = String(body.start || '').trim();
    const end = String(body.end || '').trim();
    if (!/^\d+$/.test(start) || !/^\d+$/.test(end)) return res.status(400).json({ ok: false, reason: 'invalid range boundaries' });
    cmds.push(`set imsi-group.${name}.range-start ${start}`);
    cmds.push(`set imsi-group.${name}.range-end ${end}`);
  } else if (kind === 'series') {
    const series = String(body.series || '').trim();
    const count = body.count ? parseInt(body.count, 10) : null;
    if (!/^[0-9]+$/.test(series)) return res.status(400).json({ ok: false, reason: 'invalid series prefix' });
    cmds.push(`set imsi-group.${name}.series ${series}`);
    if (count) cmds.push(`set imsi-group.${name}.count ${count}`);
  } else {
    return res.status(400).json({ ok: false, reason: 'unknown kind (range|series)' });
  }
  if (body.apnProfile) cmds.push(`set imsi-group.${name}.apn-profile ${String(body.apnProfile)}`);

  try {
    const out = await execCliCommand(cmds.join('\n') + '\n');
    res.json({ ok: true, out });
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
  const cmds = fields.map(f => `set imsi-group.${name}.${f} `);
  // send clearing commands (set to empty)
  try {
    const out = await execCliCommand(cmds.join('\n') + '\n');
    res.json({ ok: true, out });
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
    const out = await execCliCommand('show state\n')
    const parsed = parseRuntime(out)
    res.json({ ok: true, raw: out, ...parsed })
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
app.listen(port, () => console.log(`vepc-web api listening ${port}`))
