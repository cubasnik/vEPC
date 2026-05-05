import React from 'react'
import { Card, Select, Button, Input, Space, message, Modal } from 'antd'

const { Option } = Select

export default function CliConsole(){
  const [cmd, setCmd] = React.useState('show iface')
  const [output, setOutput] = React.useState('')
  const [loading, setLoading] = React.useState(false)
  const [token, setToken] = React.useState('')
  const [confirmVisible, setConfirmVisible] = React.useState(false)
  const [confirmCmd, setConfirmCmd] = React.useState('')

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

  async function runShowConfig() {
    setLoading(true)
    try {
      const res = await fetch('/api/show-config', { headers: token ? { 'X-API-Token': token } : {} })
      const j = await res.json()
      if (!j.ok) throw new Error(j.reason || 'failed')
      setOutput(j.out || j.config || '')
    } catch (e) { message.error(e.message) }
    finally { setLoading(false) }
  }

  function askConfirm(command) {
    setConfirmCmd(command)
    setConfirmVisible(true)
  }

  async function performConfirmed() {
    setConfirmVisible(false)
    setLoading(true)
    try {
      const res = await fetch('/api/cli', { method: 'POST', headers: Object.assign({'Content-Type':'application/json'}, token ? {'X-API-Token': token} : {}), body: JSON.stringify({ cmd: confirmCmd }) })
      const j = await res.json()
      if (!j.ok) throw new Error(j.reason || 'failed')
      setOutput(j.out || '')
      message.success('Command executed')
    } catch (e) { message.error(e.message) }
    finally { setLoading(false); setConfirmCmd('') }
  }

  return (
    <>
    <Card title="CLI" extra={<Space><Input placeholder="API token (optional)" value={token} onChange={e=>setToken(e.target.value)} style={{width:220}} /><Button onClick={run} loading={loading}>Run</Button></Space>} style={{marginTop:12}}>
      <Space direction="vertical" style={{width:'100%'}}>
        <Space style={{marginBottom:8}}>
          <Select value={cmd} style={{width:300}} onChange={v=>setCmd(v)}>
            {presets.map(p => <Option key={p} value={p}>{p}</Option>)}
          </Select>
          <Input style={{width:400}} value={cmd} onChange={e=>setCmd(e.target.value)} />
        </Space>

        <Space style={{marginBottom:8}}>
          <Button onClick={() => { setCmd('show iface'); run() }}>Show Interfaces</Button>
          <Button onClick={() => { setCmd('show state'); run() }}>Show State</Button>
          <Button onClick={() => runShowConfig()}>Show Config</Button>
          <Button onClick={() => { setCmd('show imsi-group'); run() }}>Show IMSI Groups</Button>
          <Button danger onClick={() => askConfirm('restart')}>Restart vEPC</Button>
          <Button danger onClick={() => askConfirm('stop')}>Stop vEPC</Button>
        </Space>

        <div style={{marginTop:8, minHeight:120}}>
          <pre style={{whiteSpace:'pre-wrap', fontFamily:'monospace', fontSize:12}}>{output || 'Output will appear here'}</pre>
        </div>
      </Space>
    </Card>

    <Modal title="Confirm command" open={confirmVisible} onOk={performConfirmed} onCancel={()=>setConfirmVisible(false)} okText="Execute" okButtonProps={{danger:true}}>
      <p>Execute command: <b>{confirmCmd}</b> ?</p>
      <p>This will affect running services.</p>
    </Modal>
    </>
  )
}
