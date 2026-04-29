import React from 'react'

function ApiFetch(path, opts = {}, token) {
  const headers = opts.headers || {}
  if (token) headers['X-API-Token'] = token
  return fetch(path, Object.assign({}, opts, { headers }))
}

export default function ImsiManager() {
  const [groups, setGroups] = React.useState([])
  const [loading, setLoading] = React.useState(false)
  const [error, setError] = React.useState(null)
  const [token, setToken] = React.useState('')

  const [form, setForm] = React.useState({
    name: '', kind: 'series', plmns: '', series: '', count: 10, start: '', end: '', apnProfile: ''
  })

  const load = React.useCallback(() => {
    setLoading(true); setError(null)
    ApiFetch('/api/imsi', {}, token).then(r => r.json()).then(j => {
      if (j.ok) setGroups(j.groups || [])
      else setError(j.reason || 'unknown')
    }).catch(e => setError(e.message)).finally(() => setLoading(false))
  }, [token])

  React.useEffect(() => { load() }, [load])

  function onChange(e) {
    const { name, value } = e.target
    setForm(prev => ({ ...prev, [name]: value }))
  }

  async function createGroup(e) {
    e.preventDefault()
    setError(null)
    const body = {
      name: form.name,
      kind: form.kind,
      plmns: form.plmns,
      apnProfile: form.apnProfile
    }
    if (form.kind === 'series') { body.series = form.series; body.count = Number(form.count || 0) }
    else { body.start = form.start; body.end = form.end }
    try {
      const res = await ApiFetch('/api/imsi', { method: 'POST', body: JSON.stringify(body), headers: { 'Content-Type': 'application/json' } }, token)
      const j = await res.json()
      if (!j.ok) throw new Error(j.reason || 'failed')
      setForm({ name: '', kind: 'series', plmns: '', series: '', count: 10, start: '', end: '', apnProfile: '' })
      load()
    } catch (e) { setError(e.message) }
  }

  async function removeGroup(name) {
    if (!confirm(`Delete IMSI group ${name}?`)) return
    setError(null)
    try {
      const res = await ApiFetch('/api/imsi/' + encodeURIComponent(name), { method: 'DELETE' }, token)
      const j = await res.json()
      if (!j.ok) throw new Error(j.reason || 'failed')
      load()
    } catch (e) { setError(e.message) }
  }

  return (
    <div>
      <h2>IMSI Manager</h2>
      <div style={{marginBottom:12}}>
        <label>API Token (optional): <input value={token} onChange={e=>setToken(e.target.value)} style={{width:320}}/></label>
        <button onClick={load} style={{marginLeft:8}}>Reload</button>
      </div>

      <section className="card">
        <h3>Existing Groups</h3>
        {loading ? <div>Loading...</div> : null}
        {error ? <div style={{color:'red'}}>{error}</div> : null}
        <ul>
          {groups.map(g => (
            <li key={g.name}>
              <strong>{g.name}</strong> — {g.type || 'unknown'} plmn:{g.plmn}
              {g.type==='range' ? ` range:${g.rangeStart}-${g.rangeEnd}` : ''}
              {g.type==='series' ? ` series:${g.series}` : ''}
              {g.apnProfile ? ` apn:${g.apnProfile}` : ''}
              <button style={{marginLeft:8}} onClick={()=>removeGroup(g.name)}>Delete</button>
            </li>
          ))}
        </ul>
      </section>

      <section className="card">
        <h3>Create Group</h3>
        <form onSubmit={createGroup}>
          <div><label>Name: <input name="name" value={form.name} onChange={onChange} required/></label></div>
          <div>
            <label>Kind: 
              <select name="kind" value={form.kind} onChange={onChange}>
                <option value="series">Series</option>
                <option value="range">Range</option>
              </select>
            </label>
          </div>
          <div><label>PLMNs (comma-separated): <input name="plmns" value={form.plmns} onChange={onChange} required/></label></div>
          {form.kind === 'series' ? (
            <>
              <div><label>Series prefix: <input name="series" value={form.series} onChange={onChange} required/></label></div>
              <div><label>Count: <input type="number" name="count" value={form.count} onChange={onChange}/></label></div>
            </>
          ) : (
            <>
              <div><label>Start MSIN: <input name="start" value={form.start} onChange={onChange} required/></label></div>
              <div><label>End MSIN: <input name="end" value={form.end} onChange={onChange} required/></label></div>
            </>
          )}
          <div><label>APN Profile (optional): <input name="apnProfile" value={form.apnProfile} onChange={onChange}/></label></div>
          <div style={{marginTop:8}}><button type="submit">Create</button></div>
        </form>
      </section>
    </div>
  )
}
