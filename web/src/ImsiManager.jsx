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
      // if editing existing group, use PUT
      if (body._editingName) {
        const name = body._editingName
        delete body._editingName
        const res = await ApiFetch('/api/imsi/' + encodeURIComponent(name), { method: 'PUT', body: JSON.stringify(body), headers: { 'Content-Type': 'application/json' } }, token)
        const j = await res.json()
        if (!j.ok) throw new Error(j.reason || 'failed')
        message.success('Группа IMSI обновлена')
        // update local state optimistically
        setGroups(prev => (prev || []).map(g => g.name === name ? Object.assign({}, g, {
          plmn: body.plmns || g.plmn,
          type: body.kind || g.type,
          apnProfile: body.apnProfile || g.apnProfile,
          series: body.series || g.series,
          rangeStart: body.start || g.rangeStart,
          rangeEnd: body.end || g.rangeEnd,
        }) : g))
        setModalVisible(false)
        form.resetFields()
        load()
        return
      }
      const res = await ApiFetch('/api/imsi', { method: 'POST', body: JSON.stringify(body), headers: { 'Content-Type': 'application/json' } }, token)
      const j = await res.json()
      if (!j.ok) throw new Error(j.reason || 'failed')
      message.success('Группа IMSI создана')
      // optimistic update: add created group into local table so user sees it immediately
      const newGroup = { name: body.name, type: body.kind === 'series' ? 'series' : 'range', plmn: body.plmns }
      if (body.kind === 'series') { newGroup.series = body.series; if (body.count) newGroup.count = body.count }
      else { newGroup.rangeStart = body.start; newGroup.rangeEnd = body.end }
      setGroups(prev => [newGroup].concat(prev || []))
      setModalVisible(false)
      form.resetFields()
      // try to reload authoritative list in background
      load()
    } catch (e) { message.error(e.message) }
  }

  function confirmDelete(name) {
    Modal.confirm({
      title: `Удалить группу IMSI ${name}?`,
      okText: 'Удалить',
      okType: 'danger',
      onOk: async () => {
        try {
          const res = await ApiFetch('/api/imsi/' + encodeURIComponent(name), { method: 'DELETE' }, token)
          const j = await res.json()
          if (!j.ok) throw new Error(j.reason || 'failed')
          message.success('Удалено')
          // remove locally
          setGroups(prev => (prev || []).filter(g => g.name !== name))
          load()
        } catch (e) { message.error(e.message) }
      }
    })
  }

  const columns = [
    { title: 'Имя', dataIndex: 'name', key: 'name' },
    { title: 'Тип', dataIndex: 'type', key: 'type' },
    { title: 'PLMN', dataIndex: 'plmn', key: 'plmn' },
    { title: 'Детали', key: 'details', render: (_, r) => r.type === 'range' ? `${r.rangeStart}-${r.rangeEnd}` : r.series },
    { title: 'APN', dataIndex: 'apnProfile', key: 'apnProfile' },
    { title: 'Действия', key: 'actions', render: (_, r) => (<Space><Button size="small" onClick={()=>openEdit(r)}>Ред.</Button><Button danger size="small" onClick={()=>confirmDelete(r.name)}>Удал.</Button></Space>) }
  ]

  function openEdit(r) {
    // prefill form with group data and mark editing name
    const values = Object.assign({}, r)
    // normalize fields to expected form names
    if (values.type === 'series') { values.kind = 'series'; values.series = values.series }
    else if (values.type === 'range') { values.kind = 'range'; values.start = values.rangeStart; values.end = values.rangeEnd }
    values.plmns = values.plmn
    values._editingName = values.name
    form.setFieldsValue(values)
    setModalVisible(true)
  }

  return (
    <Card title="Группы IMSI" extra={<Space><Input placeholder="API токен (необязательно)" value={token} onChange={e=>setToken(e.target.value)} style={{width:260}} /><Button onClick={load}>Перезагрузить</Button><Button type="primary" onClick={()=>setModalVisible(true)}>Создать</Button></Space>}>
      <Spin spinning={loading}>
        <Table rowKey="name" dataSource={groups} columns={columns} pagination={false} />
      </Spin>

      <Modal title="Создать группу IMSI" open={modalVisible} onCancel={()=>setModalVisible(false)} footer={null}>
        <Form form={form} layout="vertical" onFinish={createGroup} initialValues={{ kind: 'series', count: 10 }}>
          <Form.Item name="name" label="Имя" rules={[{ required: true }]}> 
            <Input />
          </Form.Item>
          <Form.Item name="kind" label="Тип" rules={[{ required: true }]}> 
            <Select>
              <Option value="series">Series</Option>
              <Option value="range">Range</Option>
            </Select>
          </Form.Item>
          <Form.Item name="plmns" label="PLMNы (через запятую)" rules={[{ required: true }]}> 
            <Input />
          </Form.Item>

          <Form.Item shouldUpdate={(prev, cur) => prev.kind !== cur.kind} noStyle>
            {() => (
              form.getFieldValue('kind') === 'series' ? (
                  <>
                  <Form.Item name="series" label="Префикс серии" rules={[{ required: true }]}><Input /></Form.Item>
                  <Form.Item name="count" label="Кол-во"><Input type="number" /></Form.Item>
                </>
              ) : (
                <>
                  <Form.Item name="start" label="Начало MSIN" rules={[{ required: true }]}><Input /></Form.Item>
                  <Form.Item name="end" label="Конец MSIN" rules={[{ required: true }]}><Input /></Form.Item>
                </>
              )
            )}
          </Form.Item>
          <Form.Item name="apnProfile" label="APN профиль (необязательно)"><Input /></Form.Item>
          <Form.Item>
            <Space>
              <Button onClick={()=>setModalVisible(false)}>Отмена</Button>
              <Button type="primary" htmlType="submit">Создать</Button>
            </Space>
          </Form.Item>
        </Form>
      </Modal>
    </Card>
  )
}
