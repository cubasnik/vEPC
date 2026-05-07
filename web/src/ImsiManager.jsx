import React from 'react'
import { Table, Button, Form, Input, Select, Card, Space, Modal, message, Spin, InputNumber } from 'antd'

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
  const [syncStatus, setSyncStatus] = React.useState({})

  const load = React.useCallback(() => {
    setLoading(true)
    ApiFetch('/api/imsi', {}, token).then(r => r.json()).then(j => {
      if (j.ok) {
        // normalize API shape to UI fields
        const normalized = (j.groups || []).map(g => ({
          name: g.name,
          kind: g.kind || g.type,
          plmn: g.plmn || g.plmns,
          series: g.series || null,
          rangeStart: g.rangeStart || g.range_start || null,
          rangeEnd: g.rangeEnd || g.range_end || null,
          apnProfile: g.apnProfile || g.apn_profile || null,
          count: g.count || g.cnt || null
        }))
        setGroups(normalized)
        // also load sync status
        ApiFetch('/api/imsi/sync-status', {}, token).then(r2 => r2.json()).then(sj => {
          if (sj.ok) {
            const map = {}
            for (const e of sj.entries || []) map[`${e.name}::${e.plmn}`] = e
            setSyncStatus(map)
          }
        }).catch(()=>{})
      }
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
      const newGroup = { name: body.name, kind: body.kind === 'series' ? 'series' : 'range', plmn: body.plmns }
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
    { title: 'Тип', dataIndex: 'kind', key: 'kind', render: (_, r) => r.kind },
    { title: 'PLMN', dataIndex: 'plmn', key: 'plmn' },
    { title: 'Детали', key: 'details', render: (_, r) => r.kind === 'range' ? `${r.rangeStart || ''}-${r.rangeEnd || ''}` : r.series || '' },
    { title: 'Sync', key: 'sync', render: (_, r) => {
        const key = `${r.name}::${r.plmn}`
        const s = syncStatus[key]
        if (!s) return '—'
        const dt = new Date(s.ts)
        return s.ok ? `OK (${dt.toLocaleTimeString()})` : `FAILED (${dt.toLocaleTimeString()})`
      } },
    { title: 'APN', dataIndex: 'apnProfile', key: 'apnProfile' },
    { title: 'Действия', key: 'actions', render: (_, r) => (<Space><Button size="small" onClick={()=>openEdit(r)}>Ред.</Button><Button danger size="small" onClick={()=>confirmDelete(r.name)}>Удал.</Button></Space>) }
  ]

  function openEdit(r) {
    // prefill form with group data and mark editing name
    const values = Object.assign({}, r)
    // normalize fields to expected form names
    if (values.kind === 'series') { values.kind = 'series'; values.series = values.series }
    else if (values.kind === 'range') { values.kind = 'range'; values.start = values.rangeStart; values.end = values.rangeEnd }
    values.plmns = values.plmn
    values._editingName = values.name
    form.setFieldsValue(values)
    setModalVisible(true)
  }

  return (
    <Card title="Группы IMSI" extra={<Space><Input placeholder="API токен (необязательно)" value={token} onChange={e=>setToken(e.target.value)} style={{width:260}} /><Button onClick={load}>Перезагрузить</Button><Button type="primary" onClick={()=>{ form.resetFields(); setModalVisible(true); }}>Создать</Button></Space>}>
      <Spin spinning={loading}>
        <Table rowKey="name" dataSource={groups} columns={columns} pagination={false} />
      </Spin>

      <Modal title={form.getFieldValue('_editingName') ? "Редактировать группу IMSI" : "Создать группу IMSI"} open={modalVisible} onCancel={()=>{ setModalVisible(false); form.resetFields(); }} footer={null}>
        <Form form={form} layout="vertical" onFinish={createGroup} initialValues={{ kind: 'series', count: 10 }}>
          <Form.Item name="name" label="Имя" rules={[{ required: true, message: 'Введите имя группы' }]}> 
            <Input />
          </Form.Item>
          <Form.Item name="kind" label="Тип" rules={[{ required: true, message: 'Выберите тип группы' }]}> 
            <Select>
              <Option value="series">Series</Option>
              <Option value="range">Range</Option>
            </Select>
          </Form.Item>
          <Form.Item name="plmns" label="PLMNы (через запятую)" rules={[{ required: true, message: 'Укажите PLMN(ы)' }, { validator: (_, val) => {
            if (!val) return Promise.reject()
            const parts = String(val).split(',').map(s=>s.trim()).filter(Boolean)
            const ok = parts.every(p => /^[0-9]{5,6}$/.test(p))
            return ok ? Promise.resolve() : Promise.reject(new Error('Каждый PLMN должен быть MCC+MNC (5-6 цифр)'))
          }}]}> 
            <Input placeholder="например: 25001,25002" />
          </Form.Item>

          <Form.Item shouldUpdate={(prev, cur) => prev.kind !== cur.kind} noStyle>
            {() => (
              form.getFieldValue('kind') === 'series' ? (
                  <>
                  <Form.Item name="series" label="Префикс серии" rules={[{ required: true, message: 'Укажите префикс серии' }, { validator: (_, val) => (/^[0-9]+$/.test(String(val||'')) ? Promise.resolve() : Promise.reject(new Error('Префикс должен содержать только цифры'))) }]}><Input placeholder="например: 55555" /></Form.Item>
                  <Form.Item name="count" label="Кол-во" rules={[{ type: 'number', min: 1, message: 'Кол-во должно быть положительным числом' }]}><InputNumber min={1} /></Form.Item>
                </>
              ) : (
                <>
                  <Form.Item name="start" label="Начало MSIN" rules={[{ required: true, message: 'Укажите начало диапазона' }, { validator: (_, val) => (/^[0-9]+$/.test(String(val||'')) ? Promise.resolve() : Promise.reject(new Error('Начало должно быть числом'))) }]}><Input placeholder="например: 1000" /></Form.Item>
                  <Form.Item name="end" label="Конец MSIN" rules={[{ required: true, message: 'Укажите конец диапазона' }, { validator: (_, val) => (/^[0-9]+$/.test(String(val||'')) ? Promise.resolve() : Promise.reject(new Error('Конец должен быть числом'))) }, { validator: (_, val) => {
                    const start = form.getFieldValue('start')
                    if (!start || !val) return Promise.resolve()
                    if (String(start).match(/^[0-9]+$/) && String(val).match(/^[0-9]+$/) && (parseInt(start,10) <= parseInt(val,10))) return Promise.resolve()
                    return Promise.reject(new Error('Конец должен быть >= Начала'))
                  }]}><Input placeholder="например: 9999" /></Form.Item>
                </>
              )
            )}
          </Form.Item>
          <Form.Item name="apnProfile" label="APN профиль (необязательно)"><Input /></Form.Item>
          <Form.Item>
            <Space>
              <Button onClick={()=>{ setModalVisible(false); form.resetFields(); }}>Отмена</Button>
              <Button type="primary" htmlType="submit">{form.getFieldValue('_editingName') ? 'Сохранить' : 'Создать'}</Button>
            </Space>
          </Form.Item>
        </Form>
      </Modal>
    </Card>
  )
}
