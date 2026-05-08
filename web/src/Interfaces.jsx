import React from 'react'
import { Card, Table, Button, Space, message, Spin, Modal } from 'antd'

export default function Interfaces({ filterPhysical = false }){
  const [ifaces, setIfaces] = React.useState([])
  const [loading, setLoading] = React.useState(false)
  const [expandedRowKeys, setExpandedRowKeys] = React.useState([])
  const [diagModalVisible, setDiagModalVisible] = React.useState(false)
  const [diagModalContent, setDiagModalContent] = React.useState('')
  const [createVisible, setCreateVisible] = React.useState(false)
  const [ports, setPorts] = React.useState([])
  const [creating, setCreating] = React.useState(false)
  const [form] = React.useState({ name: '', proto: 's1', address: '', phys: '' })

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

  React.useEffect(()=>{
    // fetch physical ports available for binding
    fetch('/api/ports').then(r=>r.json()).then(j=>{ if (j.ok) setPorts(j.ports || []) }).catch(()=>{})
  }, [])

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

  function openCreate() { setFormDefaults(); setCreateVisible(true) }
  function setFormDefaults(){ form.name=''; form.proto='s1'; form.address=''; form.phys=(ports[0] && ports[0].name) || '' }

  async function doCreate(){
    try{
      setCreating(true)
      const body = { name: form.name, proto: form.proto, address: form.address, phys: form.phys }
      const res = await fetch('/api/interfaces', { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify(body) })
      const j = await res.json()
      if (!j.ok) throw new Error(j.reason || 'failed')
      message.success('Интерфейс создан')
      setCreateVisible(false)
      load()
    } catch(e){ message.error(e.message) } finally { setCreating(false) }
  }

  async function doDelete(id){
    try{
      const res = await fetch('/api/interfaces/'+encodeURIComponent(id), { method: 'DELETE' })
      const j = await res.json()
      if (!j.ok) throw new Error(j.reason || 'delete failed')
      message.success('Удалено')
      load()
    } catch(e){ message.error(e.message) }
  }

  return (
    <Card title="Интерфейсы" extra={<Space><Button onClick={load}>Обновить</Button><Button type="primary" onClick={openCreate}>Добавить интерфейс</Button></Space>} style={{marginTop:12}}>
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
      <Modal title="Создать интерфейс" open={createVisible} onCancel={()=>setCreateVisible(false)} okText="Создать" onOk={doCreate} confirmLoading={creating}>
        <div style={{display:'grid', gap:8}}>
          <label>Имя</label>
          <input value={form.name} onChange={e=>form.name=e.target.value} />
          <label>Тип</label>
          <select value={form.proto} onChange={e=>form.proto=e.target.value}>
            <option value="s1">s1</option>
            <option value="s11">s11</option>
            <option value="s6a">s6a</option>
            <option value="gtp">gtp</option>
          </select>
          <label>IP адрес</label>
          <input value={form.address} onChange={e=>form.address=e.target.value} />
          <label>Физический порт</label>
          <select value={form.phys} onChange={e=>form.phys=e.target.value}>
            {ports.map(p=> <option key={p.name} value={p.name}>{p.name} ({p.state})</option>)}
          </select>
        </div>
      </Modal>
      <Modal title="Diagnostic details" open={diagModalVisible} onCancel={()=>setDiagModalVisible(false)} footer={null} width={800}>
        <div style={{whiteSpace:'pre-wrap', wordBreak:'break-word'}}>{diagModalContent}</div>
      </Modal>
    </Card>
  )
}
