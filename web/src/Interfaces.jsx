import React from 'react'
import { Card, Table, Button, Space, message, Spin, Modal } from 'antd'

export default function Interfaces({ filterPhysical = false }){
  const [ifaces, setIfaces] = React.useState([])
  const [loading, setLoading] = React.useState(false)
  const [expandedRowKeys, setExpandedRowKeys] = React.useState([])
  const [diagModalVisible, setDiagModalVisible] = React.useState(false)
  const [diagModalContent, setDiagModalContent] = React.useState('')

  async function load(){
    setLoading(true)
    try {
      const res = await fetch('/api/interfaces')
      const j = await res.json()
      if (!j.ok) throw new Error(j.reason || 'failed')
      let list = j.interfaces || []
      if (filterPhysical) {
        list = list.filter(i => (i.implementation || '').toUpperCase() === 'IMPLEMENTED')
      }
      setIfaces(list)
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
    { title: 'Peer', dataIndex: 'peer', key: 'peer' },
    { title: 'Diag', dataIndex: 'diagnostic', key: 'diagnostic', render: t => (
        t ? <div style={{display:'flex', alignItems:'center', gap:8}}>
          <div style={{flex: 1, minWidth: 0}}>
            <div style={{whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis'}} title={t}>{t}</div>
          </div>
          <Button size="small" style={{flex: '0 0 auto'}} onClick={() => { setDiagModalContent(t); setDiagModalVisible(true) }}>Подробнее</Button>
        </div> : null
      ) }
  ]

  return (
    <Card title="Интерфейсы" extra={<Space><Button onClick={load}>Обновить</Button></Space>} style={{marginTop:12}}>
      <Spin spinning={loading}>
        <div style={{overflowX: 'hidden'}}>
          <Table
            dataSource={ifaces}
            columns={columns}
            rowKey={r=>r.name}
            pagination={false}
            expandable={{
              expandedRowRender: record => <div style={{whiteSpace:'pre-wrap', wordBreak:'break-word'}}>{record.diagnostic || ''}</div>,
              rowExpandable: record => !!record.diagnostic,
              expandRowByClick: true
            }}
          />
        </div>
      </Spin>
      <Modal title="Diagnostic details" open={diagModalVisible} onCancel={()=>setDiagModalVisible(false)} footer={null} width={800}>
        <div style={{whiteSpace:'pre-wrap', wordBreak:'break-word'}}>{diagModalContent}</div>
      </Modal>
    </Card>
  )
}
