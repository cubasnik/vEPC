import React from 'react'
import { Card, Table, Button, Space, message, Collapse } from 'antd'

const { Panel } = Collapse

export default function Runtime(){
  const [data, setData] = React.useState(null)
  const [loading, setLoading] = React.useState(false)

  async function load(){
    setLoading(true)
    try {
      const res = await fetch('/api/runtime')
      const j = await res.json()
      if (!j.ok) throw new Error(j.reason || 'failed')
      setData(j)
    } catch (e) { message.error(e.message) }
    finally { setLoading(false) }
  }

  React.useEffect(()=>{ load() }, [])

  return (
    <Card title="Runtime / State" extra={<Button onClick={load}>Refresh</Button>} style={{marginTop:12}}>
      <Space direction="vertical" style={{width:'100%'}}>
        <div>PDP contexts: {data ? data.pdpContexts.length : '—'}</div>
        <div>UE contexts: {data ? data.ueContexts.length : '—'}</div>

        <Collapse>
          <Panel header="PDP contexts" key="pdp">
            <Table dataSource={data ? data.pdpContexts : []} columns={[{title:'TEID', render:(v,r)=>r.TEID||r.teid},{title:'IMSI', dataIndex:'IMSI'},{title:'APN', dataIndex:'APN'},{title:'Peer', dataIndex:'Peer' }]} rowKey={(r,i)=>String(i)} pagination={false} />
          </Panel>
          <Panel header="Endpoint telemetry" key="endpoint">
            <Table dataSource={data ? data.endpointTelemetry : []} columns={[{title:'Name', dataIndex:'Name'},{title:'Protocol', dataIndex:'Protocol'},{title:'Active', dataIndex:'Active'},{title:'Last Peer', dataIndex:'Last Peer'}]} rowKey={(r,i)=>String(i)} pagination={false} />
          </Panel>
          <Panel header="UE contexts" key="ue">
            <Table dataSource={data ? data.ueContexts : []} columns={[{title:'IMSI', dataIndex:'IMSI'},{title:'GUTI', dataIndex:'GUTI'},{title:'Peer', dataIndex:'Peer'},{title:'Attached', dataIndex:'Attached'}]} rowKey={(r,i)=>String(i)} pagination={false} />
          </Panel>
          <Panel header="Raw" key="raw">
            <pre style={{whiteSpace:'pre-wrap', fontFamily:'monospace', fontSize:12}}>{data ? data.raw : 'loading...'}</pre>
          </Panel>
        </Collapse>
      </Space>
    </Card>
  )
}
