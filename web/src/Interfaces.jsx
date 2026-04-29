import React from 'react'
import { Card, Table, Button, Space, message, Spin } from 'antd'

export default function Interfaces(){
  const [ifaces, setIfaces] = React.useState([])
  const [loading, setLoading] = React.useState(false)

  async function load(){
    setLoading(true)
    try {
      const res = await fetch('/api/interfaces')
      const j = await res.json()
      if (!j.ok) throw new Error(j.reason || 'failed')
      setIfaces(j.interfaces || [])
    } catch (e) { message.error(e.message) }
    finally { setLoading(false) }
  }

  React.useEffect(()=>{ load() }, [])

  const columns = [
    { title: 'Name', dataIndex: 'name', key: 'name' },
    { title: 'Proto', dataIndex: 'proto', key: 'proto' },
    { title: 'Address', dataIndex: 'address', key: 'address' },
    { title: 'Admin', dataIndex: 'admin', key: 'admin' },
    { title: 'Oper', dataIndex: 'oper', key: 'oper' },
    { title: 'Impl', dataIndex: 'implementation', key: 'implementation' },
    { title: 'Peer', dataIndex: 'peer', key: 'peer' }
  ]

  return (
    <Card title="Interfaces" extra={<Space><Button onClick={load}>Refresh</Button></Space>} style={{marginTop:12}}>
      <Spin spinning={loading}>
        <Table dataSource={ifaces} columns={columns} rowKey={r=>r.name} pagination={false} />
      </Spin>
    </Card>
  )
}
