import React from 'react'
import { Tabs, Card, Button, Space, message, Modal } from 'antd'
import ImsiManager from './ImsiManager'

const { TabPane } = Tabs

export default function AdminPanel(){
  const [output, setOutput] = React.useState('')
  const [loading, setLoading] = React.useState(false)
  const [confirmVisible, setConfirmVisible] = React.useState(false)
  const [confirmCmd, setConfirmCmd] = React.useState('')

  async function callApi(path, opts){
    setLoading(true)
    try {
      const res = await fetch(path, opts)
      const j = await res.json()
      if (!j.ok) throw new Error(j.reason || 'failed')
      setOutput(j.out || j.config || JSON.stringify(j, null, 2))
    } catch (e){ message.error(e.message); setOutput(e.message) }
    finally{ setLoading(false) }
  }

  function askConfirm(cmd){ setConfirmCmd(cmd); setConfirmVisible(true) }

  async function performConfirmed(){
    setConfirmVisible(false)
    setLoading(true)
    try{
      const res = await fetch('/api/cli', { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify({ cmd: confirmCmd }) })
      const j = await res.json()
      if (!j.ok) throw new Error(j.reason || 'failed')
      setOutput(j.out || '')
      message.success('Command executed')
    } catch(e){ message.error(e.message); setOutput(e.message) }
    finally{ setLoading(false); setConfirmCmd('') }
  }

  return (
    <Card title="Admin" style={{marginTop:12}}>
      <Tabs defaultActiveKey="interfaces">
        <TabPane tab="Interfaces" key="interfaces">
          <Space style={{marginBottom:8}}>
            <Button onClick={()=>callApi('/api/interfaces')}>Refresh Interfaces</Button>
            <Button onClick={()=>callApi('/api/runtime')}>Show Runtime</Button>
          </Space>
        </TabPane>

        <TabPane tab="Physical Ports" key="ports">
          <Space style={{marginBottom:8}}>
            <Button onClick={()=>callApi('/api/interfaces')}>Show Physical Ports</Button>
          </Space>
          <div style={{marginTop:8}}>
            <small>Physical ports are derived from interfaces output.</small>
          </div>
        </TabPane>

        <TabPane tab="IMSI" key="imsi">
          <ImsiManager />
        </TabPane>

        <TabPane tab="Runtime / Config" key="runtime">
          <Space style={{marginBottom:8}}>
            <Button onClick={()=>callApi('/api/runtime')}>Show Runtime</Button>
            <Button onClick={()=>callApi('/api/show-config')}>Show Config</Button>
          </Space>
        </TabPane>

        <TabPane tab="Admin" key="admin">
          <Space style={{marginBottom:8}}>
            <Button danger onClick={()=>askConfirm('restart')}>Restart vEPC</Button>
            <Button danger onClick={()=>askConfirm('stop')}>Stop vEPC</Button>
            <Button onClick={()=>callApi('/api/cli', { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify({ cmd: 'show imsi-group' }) })}>Show IMSI Groups</Button>
          </Space>
        </TabPane>
      </Tabs>

      <div style={{marginTop:12, minHeight:160}}>
        <pre style={{whiteSpace:'pre-wrap', fontFamily:'monospace', fontSize:12}}>{output || 'Output will appear here'}</pre>
      </div>

      <Modal title="Confirm command" open={confirmVisible} onOk={performConfirmed} onCancel={()=>setConfirmVisible(false)} okText="Execute" okButtonProps={{danger:true}}>
        <p>Execute command: <b>{confirmCmd}</b> ?</p>
        <p>This will affect running services.</p>
      </Modal>
    </Card>
  )
}
