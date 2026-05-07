#!/usr/bin/env node
const net = require('net')
const argv = require('minimist')(process.argv.slice(2))
const host = argv.host || process.env.DB_HOST || 'db'
const port = parseInt(argv.port || process.env.DB_PORT || 3306, 10)
const timeout = parseInt(argv.timeout || 60, 10)

function waitForPort(host, port, timeoutSeconds) {
  return new Promise((resolve, reject) => {
    const start = Date.now()
    function attempt() {
      const socket = new net.Socket()
      socket.setTimeout(3000)
      socket.once('connect', () => {
        socket.destroy()
        resolve(true)
      })
      socket.once('timeout', () => { socket.destroy(); next() })
      socket.once('error', () => { socket.destroy(); next() })
      socket.connect(port, host)
    }
    function next() {
      if ((Date.now() - start) / 1000 >= timeoutSeconds) return reject(new Error('timeout'))
      setTimeout(attempt, 1000)
    }
    attempt()
  })
}

waitForPort(host, port, timeout)
  .then(() => {
    console.log(`wait-for-db: ${host}:${port} reachable`)
    process.exit(0)
  })
  .catch((err) => {
    console.error('wait-for-db: timeout or error:', err && err.message)
    process.exit(1)
  })
