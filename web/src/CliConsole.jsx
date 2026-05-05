import React from 'react'
import { Card, Select, Button, Input, Space, message } from 'antd'

const { Option } = Select

export default function CliConsole(){
  const [cmd, setCmd] = React.useState('show iface')
  const [output, setOutput] = React.useState('')
  const [loading, setLoading] = React.useState(false)
  const [token, setToken] = React.useState('')

  const presets = [
    'show iface',
    'show state',
    'show running-config',
    'show imsi-group',
    'show subscriber-config',
    'status',
    'logs'
  ]

  async function run() {
    setLoading(true)
    try {
      const res = await fetch('/api/cli', { method: 'POST', headers: Object.assign({'Content-Type':'application/json'}, token ? {'X-API-Token': token} : {}), body: JSON.stringify({ cmd }) })
      const j = await res.json()
      if (!j.ok) throw new Error(j.reason || 'failed')
      setOutput(j.out || '')
    } catch (e) { message.error(e.message) }
    finally { setLoading(false) }
  }

  return (
    <Card title="CLI" extra={<Space><Input placeholder="API token (optional)" value={token} onChange={e=>setToken(e.target.value)} style={{width:220}} /><Button onClick={run} loading={loading}>Run</Button></Space>} style={{marginTop:12}}>
      <Space style={{marginBottom:8}}>
        <Select value={cmd} style={{width:300}} onChange={v=>setCmd(v)}>
          {presets.map(p => <Option key={p} value={p}>{p}</Option>)}
        </Select>
        <Input style={{width:400}} value={cmd} onChange={e=>setCmd(e.target.value)} />
      </Space>
      <div style={{marginTop:8, minHeight:120}}>
        <pre style={{whiteSpace:'pre-wrap', fontFamily:'monospace', fontSize:12}}>{output || 'Output will appear here'}</pre>
      </div>
    </Card>
  )
}
