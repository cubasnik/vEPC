import React from 'react'
import { Card, Button, Typography, Space, message } from 'antd'

const { Paragraph, Text } = Typography

export default function ShowConfig(){
  const [loading, setLoading] = React.useState(false)
  const [config, setConfig] = React.useState(null)

  async function load(){
    setLoading(true)
    try {
      const res = await fetch('/api/show-config')
      const j = await res.json()
      if (!j.ok) throw new Error(j.reason || 'no config')
      setConfig(j.out || j.config || '')
    } catch (e) {
      message.error(e.message)
    } finally { setLoading(false) }
  }

  return (
    <Card title="Running Config" extra={<Space><Button onClick={load}>Reload</Button></Space>} style={{marginTop:12}}>
      <div style={{minHeight:120}}>
        {config ? (
          <pre style={{whiteSpace:'pre-wrap', fontFamily:'monospace', fontSize:12}}>{config}</pre>
        ) : (
          <Paragraph type="secondary">No config loaded. Click Reload to fetch running-config.</Paragraph>
        )}
      </div>
    </Card>
  )
}
