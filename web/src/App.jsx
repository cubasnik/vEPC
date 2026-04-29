import React from 'react'
import ImsiManager from './ImsiManager'

export default function App() {
  const [ping, setPing] = React.useState(null)

  React.useEffect(() => {
    fetch('/api/ping').then(r => r.json()).then(setPing).catch(() => setPing({ok:false}))
  }, [])

  return (
    <div className="app">
      <header className="header">
        <img src="/logo-placeholder.svg" alt="EricssonSoft" className="logo"/>
        <h1>EricssonSoft vEPC UI (placeholder)</h1>
      </header>
      <main>
        <section className="card">
          <h2>Status</h2>
          <pre>{ping ? JSON.stringify(ping, null, 2) : 'loading...'}</pre>
        </section>
        <ImsiManager />
      </main>
    </div>
  )
}
