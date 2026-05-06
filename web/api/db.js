const mysql = require('mysql2/promise')

const DB_HOST = process.env.DB_HOST || 'db'
const DB_PORT = parseInt(process.env.DB_PORT || '3306', 10)
const DB_USER = process.env.DB_USER || 'vepc'
const DB_PASS = process.env.DB_PASS || 'vepcpass'
const DB_NAME = process.env.DB_NAME || 'vepc'

let pool = null

async function getPool() {
  if (pool) return pool
  pool = mysql.createPool({
    host: DB_HOST,
    port: DB_PORT,
    user: DB_USER,
    password: DB_PASS,
    database: DB_NAME,
    waitForConnections: true,
    connectionLimit: 10,
    queueLimit: 0
  })
  return pool
}

async function init() {
  const p = await getPool()
  // create imsi_groups table if not exists
  await p.query(`
    CREATE TABLE IF NOT EXISTS imsi_groups (
      id INT AUTO_INCREMENT PRIMARY KEY,
      name VARCHAR(128) NOT NULL,
      kind VARCHAR(32) NOT NULL,
      plmn VARCHAR(32) NOT NULL,
      series VARCHAR(64),
      range_start VARCHAR(64),
      range_end VARCHAR(64),
      apn_profile VARCHAR(128),
      cnt INT DEFAULT NULL,
      created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
      UNIQUE KEY uniq_name_plmn (name, plmn)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
  `)

  return p
}

module.exports = { getPool, init }
