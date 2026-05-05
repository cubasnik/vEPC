import React from 'react'
import { Tabs, Card, Button, Space, message, Modal, Badge } from 'antd'
import ImsiManager from './ImsiManager'
import Interfaces from './Interfaces'

const { TabPane } = Tabs

export default function AdminPanel(){
  const [output, setOutput] = React.useState('')
  const [loading, setLoading] = React.useState(false)
  const [confirmVisible, setConfirmVisible] = React.useState(false)
  const [confirmCmd, setConfirmCmd] = React.useState('')
  const [usedCmd, setUsedCmd] = React.useState(null)
  const [runtimeData, setRuntimeData] = React.useState(null)

  async function callApi(path, opts){
    setLoading(true)
    try {
      const res = await fetch(path, opts)
      const j = await res.json()
      if (!j.ok) throw new Error(j.reason || 'failed')
      // runtime is structured; keep it separate
      if (path === '/api/runtime') {
        setRuntimeData(j)
        if (j.cmd) setUsedCmd(j.cmd)
      } else {
        setOutput(j.out || j.config || JSON.stringify(j, null, 2))
        if (j.cmd) setUsedCmd(j.cmd)
      }
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
        <TabPane tab="Интерфейсы" key="interfaces">
          <Interfaces />
        </TabPane>

        <TabPane tab="Физические порты" key="ports">
          <Interfaces filterPhysical />
        </TabPane>

        <TabPane tab="IMSI" key="imsi">
          <ImsiManager />
        </TabPane>

        <TabPane tab={
          <span>Состояние {usedCmd ? <Badge style={{marginLeft:8}} count={usedCmd} /> : null}</span>
        } key="runtime">
          <Space style={{marginBottom:8}}>
            <Button onClick={()=>{ setOutput(''); setUsedCmd(null); setRuntimeData(null); callApi('/api/runtime') }}>Показать состояние</Button>
          </Space>
          <div style={{marginTop:12}}>
            {runtimeData ? (
              <div>
                <div style={{marginBottom:8}}>PDP контекстов: {runtimeData.pdpContexts ? runtimeData.pdpContexts.length : 0}</div>
                <div style={{marginBottom:8}}>Телеметрия эндпоинтов:</div>
                {runtimeData.endpointTelemetry && runtimeData.endpointTelemetry.length ? (
                  <div>
                    {runtimeData.endpointTelemetry.map((e, idx) => (
                      <Card size="small" key={idx} style={{marginBottom:8}}>
                        <div><b>{e.Name}</b> — {e.Protocol} @ {e.Address}</div>
                        <div>Bind: {e['Effective Bind']}</div>
                        <div>Active: {e.Active} Rx: {e['Rx Packets']} / {e['Rx Bytes']}</div>
                      </Card>
                    ))}
                  </div>
                ) : <div>Нет данных телеметрии</div>}

                <div style={{marginTop:8}}>UE контекстов: {runtimeData.ueContexts ? runtimeData.ueContexts.length : 0}</div>
              </div>
            ) : (
              <pre style={{whiteSpace:'pre-wrap', fontFamily:'monospace', fontSize:12}}>{output || 'Результат появится здесь'}</pre>
            )}
          </div>
        </TabPane>

        <TabPane tab="Администрирование" key="admin">
          <Space style={{marginBottom:8}}>
            <Button danger onClick={()=>askConfirm('restart')}>Перезапустить vEPC</Button>
            <Button danger onClick={()=>askConfirm('stop')}>Остановить vEPC</Button>
            <Button onClick={async ()=>{
              try {
                const res = await fetch('/api/imsi')
                const j = await res.json()
                if (!j.ok) throw new Error(j.reason || 'failed')
                Modal.info({ title: 'Группы IMSI (live)', content: <pre style={{whiteSpace:'pre-wrap'}}>{JSON.stringify(j.groups || [], null, 2)}</pre>, width:800 })
              } catch(e) { message.error(e.message) }
            }}>Показать группы IMSI</Button>
          </Space>
        </TabPane>
      </Tabs>

      {/* Global output removed — results shown in relevant tabs */}

      <Modal title="Confirm command" open={confirmVisible} onOk={performConfirmed} onCancel={()=>setConfirmVisible(false)} okText="Execute" okButtonProps={{danger:true}}>
        <p>Execute command: <b>{confirmCmd}</b> ?</p>
        <p>This will affect running services.</p>
      </Modal>
    </Card>
  )
}
