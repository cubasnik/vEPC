import React from 'react'
import { Layout, Typography, Row, Col, Card, Descriptions, Tag } from 'antd'
import dayjs from 'dayjs'
import 'antd/dist/reset.css'
import ImsiManager from './ImsiManager'
import ShowConfig from './ShowConfig'
import Interfaces from './Interfaces'

const { Header, Content } = Layout
const { Title } = Typography

export default function App() {
  const [ping, setPing] = React.useState(null)

  React.useEffect(() => {
    fetch('/api/ping').then(r => r.json()).then(setPing).catch(() => setPing({ok:false}))
  }, [])

  return (
    <Layout style={{ minHeight: '100vh' }}>
      <Header style={{ background: '#fff', padding: '12px 20px' }}>
        <Title level={4} style={{ margin: 0 }}>EricssonSoft vEPC</Title>
      </Header>
      <Content style={{ padding: 20 }}>
        <Row gutter={16}>
          <Col xs={24} lg={8}>
            <Card title="Status">
              {ping ? (
                <Descriptions column={1} size="small">
                  <Descriptions.Item label="API">{ping.ok ? <Tag color="green">OK</Tag> : <Tag color="red">DOWN</Tag>}</Descriptions.Item>
                  <Descriptions.Item label="Time">{ping.time ? dayjs(ping.time).format('YYYY-MM-DD HH:mm:ss') : 'unknown'}</Descriptions.Item>
                </Descriptions>
              ) : (
                <div>loading...</div>
              )}
            </Card>
          </Col>
          <Col xs={24} lg={16}>
            <div>
              <ImsiManager />
              <ShowConfig />
              <Interfaces />
            </div>
          </Col>
        </Row>
      </Content>
    </Layout>
  )
}
