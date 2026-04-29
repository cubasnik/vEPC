import React from 'react'
import { Table, Button, Form, Input, Select, Card, Space, Modal, message, Spin } from 'antd'

const { Option } = Select

function ApiFetch(path, opts = {}, token) {
  const headers = opts.headers || {}
  if (token) headers['X-API-Token'] = token
  return fetch(path, Object.assign({}, opts, { headers }))
}

export default function ImsiManager() {
  const [groups, setGroups] = React.useState([])
  const [loading, setLoading] = React.useState(false)
  const [token, setToken] = React.useState('')
  const [form] = Form.useForm()
  const [modalVisible, setModalVisible] = React.useState(false)

  const load = React.useCallback(() => {
    setLoading(true)
    ApiFetch('/api/imsi', {}, token).then(r => r.json()).then(j => {
      if (j.ok) setGroups(j.groups || [])
      else message.error(j.reason || 'failed to load')
    }).catch(e => message.error(e.message)).finally(() => setLoading(false))
  }, [token])

  React.useEffect(() => { load() }, [load])

  async function createGroup(values) {
    try {
      const body = Object.assign({}, values)
      const res = await ApiFetch('/api/imsi', { method: 'POST', body: JSON.stringify(body), headers: { 'Content-Type': 'application/json' } }, token)
      const j = await res.json()
      if (!j.ok) throw new Error(j.reason || 'failed')
      message.success('IMSI group created')
      setModalVisible(false)
      form.resetFields()
      load()
    } catch (e) { message.error(e.message) }
  }

  function confirmDelete(name) {
    Modal.confirm({
      title: `Delete IMSI group ${name}?`,
      okText: 'Delete',
      okType: 'danger',
      onOk: async () => {
        try {
          const res = await ApiFetch('/api/imsi/' + encodeURIComponent(name), { method: 'DELETE' }, token)
          const j = await res.json()
          if (!j.ok) throw new Error(j.reason || 'failed')
          message.success('Deleted')
          load()
        } catch (e) { message.error(e.message) }
      }
    })
  }

  const columns = [
    { title: 'Name', dataIndex: 'name', key: 'name' },
    { title: 'Type', dataIndex: 'type', key: 'type' },
    { title: 'PLMN', dataIndex: 'plmn', key: 'plmn' },
    { title: 'Details', key: 'details', render: (_, r) => r.type === 'range' ? `${r.rangeStart}-${r.rangeEnd}` : r.series },
    { title: 'APN', dataIndex: 'apnProfile', key: 'apnProfile' },
    { title: 'Actions', key: 'actions', render: (_, r) => (<Space><Button danger size="small" onClick={()=>confirmDelete(r.name)}>Delete</Button></Space>) }
  ]

  return (
    <Card title="IMSI Groups" extra={<Space><Input placeholder="API token (optional)" value={token} onChange={e=>setToken(e.target.value)} style={{width:260}} /><Button onClick={load}>Reload</Button><Button type="primary" onClick={()=>setModalVisible(true)}>Create</Button></Space>}>
      <Spin spinning={loading}>
        <Table rowKey="name" dataSource={groups} columns={columns} pagination={false} />
      </Spin>

      <Modal title="Create IMSI Group" open={modalVisible} onCancel={()=>setModalVisible(false)} footer={null}>
        <Form form={form} layout="vertical" onFinish={createGroup} initialValues={{ kind: 'series', count: 10 }}>
          <Form.Item name="name" label="Name" rules={[{ required: true }]}>
            <Input />
          </Form.Item>
          <Form.Item name="kind" label="Kind" rules={[{ required: true }]}>
            <Select>
              <Option value="series">Series</Option>
              <Option value="range">Range</Option>
            </Select>
          </Form.Item>
          <Form.Item name="plmns" label="PLMNs (comma-separated)" rules={[{ required: true }]}>
            <Input />
          </Form.Item>

          <Form.Item shouldUpdate={(prev, cur) => prev.kind !== cur.kind} noStyle>
            {() => (
              form.getFieldValue('kind') === 'series' ? (
                <>
                  <Form.Item name="series" label="Series prefix" rules={[{ required: true }]}><Input /></Form.Item>
                  <Form.Item name="count" label="Count"><Input type="number" /></Form.Item>
                </>
              ) : (
                <>
                  <Form.Item name="start" label="Start MSIN" rules={[{ required: true }]}><Input /></Form.Item>
                  <Form.Item name="end" label="End MSIN" rules={[{ required: true }]}><Input /></Form.Item>
                </>
              )
            )}
          </Form.Item>

          <Form.Item name="apnProfile" label="APN Profile (optional)"><Input /></Form.Item>
          <Form.Item>
            <Space>
              <Button onClick={()=>setModalVisible(false)}>Cancel</Button>
              <Button type="primary" htmlType="submit">Create</Button>
            </Space>
          </Form.Item>
        </Form>
      </Modal>
    </Card>
  )
}
