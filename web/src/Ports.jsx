import React from 'react'
import { Card, Table, Button, Space, message, Spin, Modal, Form, Input } from 'antd'

export default function Ports(){
  const [ports, setPorts] = React.useState([])
  const [loading, setLoading] = React.useState(false)
  const [creatingVlanFor, setCreatingVlanFor] = React.useState(null)
  const [assigningIpFor, setAssigningIpFor] = React.useState(null)
  const [addingPortVisible, setAddingPortVisible] = React.useState(false)
  const [form] = Form.useForm()
  const [interfaces, setInterfaces] = React.useState([])

  async function load(){
    setLoading(true)
    try{
      const [pRes, iRes] = await Promise.all([ fetch('/api/ports'), fetch('/api/interfaces') ])
      const pj = await pRes.json()
      const ij = await iRes.json()
      if (!pj.ok) throw new Error(pj.reason || 'failed ports')
      if (!ij.ok) throw new Error(ij.reason || 'failed interfaces')
      setPorts(pj.ports || [])
      setInterfaces(ij.interfaces || [])
    } catch(e){ message.error(e.message) }
    finally{ setLoading(false) }
  }

  React.useEffect(()=>{ load() }, [])

  // adding/deleting physical ports is not allowed from UI; only VLANs/IP may be assigned

  async function createVlan(parent){
    try{
      const vals = await form.validateFields()
      const name = `${parent.name}.${vals.vlan}`
      const body = { name, proto: 'vlan', address: vals.address || null, phys: parent.name }
      const res = await fetch('/api/interfaces', { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify(body) })
      const j = await res.json()
      if (!j.ok) throw new Error(j.reason || 'failed')
      message.success('VLAN создан как интерфейс')
      setCreatingVlanFor(null)
      form.resetFields()
      load()
    } catch(e){ message.error(e.message) }
  }

  async function assignIpTo(name){
    try{
      const vals = await form.validateFields()
      const body = { name, proto: 'physical', address: vals.address || null, phys: name }
      const res = await fetch('/api/interfaces', { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify(body) })
      const j = await res.json()
      if (!j.ok) throw new Error(j.reason || 'failed')
      message.success('IP назначен')
      setAssigningIpFor(null)
      form.resetFields()
      load()
    } catch(e){ message.error(e.message) }
  }

  const columns = [
    { title: 'Name', dataIndex: 'name', key: 'name' },
    { title: 'State', dataIndex: 'state', key: 'state' },
    { title: 'MAC', dataIndex: 'mac', key: 'mac' },
    { title: 'Actions', key: 'a', render: (_, record) => (
      <Space>
        <Button size="small" disabled={!record.editable} onClick={() => { form.resetFields(); setCreatingVlanFor(record) }}>Добавить VLAN</Button>
        <Button size="small" disabled={!record.editable} onClick={() => { form.resetFields(); setAssigningIpFor(record) }}>Назначить IP</Button>
      </Space>
    ) }
  ]

  return (
    <Card title="Физические порты" extra={<Space><Button onClick={load}>Обновить</Button></Space>} style={{marginTop:12}}>
      <Spin spinning={loading}>
        <Table dataSource={ports} columns={columns} rowKey={r=>r.name} pagination={false} />

        {creatingVlanFor && (
          <Modal title={`Добавить VLAN на ${creatingVlanFor.name}`} open onCancel={()=>setCreatingVlanFor(null)} onOk={() => createVlan(creatingVlanFor)}>
            <Form form={form} layout="vertical">
              <Form.Item name="vlan" label="VLAN ID/Name" rules={[{ required: true }]}> <Input /> </Form.Item>
              <Form.Item name="address" label="IP (опционально)"> <Input /> </Form.Item>
            </Form>
          </Modal>
        )}

        {assigningIpFor && (
          <Modal title={`Назначить IP ${assigningIpFor.name}`} open onCancel={()=>setAssigningIpFor(null)} onOk={() => assignIpTo(assigningIpFor.name)}>
            <Form form={form} layout="vertical">
              <Form.Item name="address" label="IP адрес" rules={[{ required: true, message: 'Введите IP' }]}>
                <Input />
              </Form.Item>
            </Form>
          </Modal>
        )}

        {/* adding physical ports from UI is disabled by design */}
      </Spin>
    </Card>
  )
}
